#include "platform/contracts/interaction_transport.h"
#include "interactions/frame.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <map>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace pet::platform {

namespace {
constexpr int maxConnections = 16;
constexpr qint64 maxQueuedBytes = 1024 * 1024; // Unsent output a peer may leave behind before it is dropped.

QString userTag() {
#ifdef Q_OS_UNIX
    return QString::number(getuid());
#else
    return qEnvironmentVariable("USERNAME", "user");
#endif
}

// The private per-user directory: owner-only, beside the status socket's.
QString privateDirectory() {
    auto root = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (root.isEmpty()) root = QDir::tempPath();
    const auto directory = root + "/agent-pet-" + userTag();
    const auto owner = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
    // Fail closed: a directory someone else pre-created in a shared temp location must never host the socket.
    if (!QDir().mkpath(directory) || !QFile::setPermissions(directory, owner)) return {};
    const QFileInfo info(directory);
#ifdef Q_OS_UNIX
    if (!info.isDir() || info.ownerId() != getuid() || (info.permissions() & ~(owner | QFile::ReadUser | QFile::WriteUser | QFile::ExeUser))) return {};
#endif
    return directory;
}
QString safeName(const QString &name) {
    QString out;
    for (const auto ch : name) out += (ch.isLetterOrNumber() || ch == '-' || ch == '_') ? ch : QChar('_');
    return out.isEmpty() ? QStringLiteral("interactions") : out;
}

class LocalInteractionTransport final : public QObject, public InteractionTransport {
public:
    explicit LocalInteractionTransport(const QString &name) : name_(name) {}
    ~LocalInteractionTransport() override {
        for (auto &entry : connections_) {
            entry.second.socket->disconnect(this);
            entry.second.socket->abort();
        }
        connections_.clear();
    }

    bool start(QString &error) override {
        // The lock, not the socket, is what makes one process the owner: a stale socket file is replaced.
        const auto directory = privateDirectory();
        if (directory.isEmpty()) { error = "Cannot secure the interaction directory"; return false; }
        lock_ = std::make_unique<QLockFile>(directory + "/" + safeName(name_.isEmpty() ? "interactions" : name_) + ".lock");
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock(0)) { error = "Another Agent Pet interaction service is running"; return false; }
        server_ = new QLocalServer(this);
        server_->setSocketOptions(QLocalServer::UserAccessOption);
        server_->setMaxPendingConnections(maxConnections);
        const auto endpoint = interactionEndpoint(name_);
        QLocalServer::removeServer(endpoint);
        if (!server_->listen(endpoint)) { error = "Cannot listen on the interaction endpoint: " + server_->errorString(); return false; }
        connect(server_, &QLocalServer::newConnection, this, [this] { accept(); });
        return true;
    }

    bool send(int id, const QByteArray &payload) override {
        const auto it = connections_.find(id);
        if (it == connections_.end()) return false;
        if (payload.isEmpty() || payload.size() > interactions::Limits::frameBytes) return false;
        auto *socket = it->second.socket;
        if (socket->bytesToWrite() > maxQueuedBytes) {
            // Torn down outside the caller: the service may be inside sendTo() on this connection.
            QMetaObject::invokeMethod(this, [this, id] { drop(id, true); }, Qt::QueuedConnection);
            return false;
        }
        socket->write(interactions::encodeFrame(payload));
        return true;
    }

    void close(int id) override { drop(id, true); }

private:
    struct Connection {
        QLocalSocket *socket = nullptr;
        interactions::FrameDecoder decoder;
    };

    void accept() {
        while (auto *socket = server_->nextPendingConnection()) {
            if (int(connections_.size()) >= maxConnections) { socket->abort(); socket->deleteLater(); continue; }
            const int id = next_++;
            socket->setParent(this);
            socket->setReadBufferSize(256 * 1024);
            connections_[id].socket = socket;
            connect(socket, &QLocalSocket::readyRead, this, [this, id] { readable(id); });
            connect(socket, &QLocalSocket::disconnected, this, [this, id] { drop(id, false); });
            if (connected) connected(id);
        }
    }

    void readable(int id) {
        auto it = connections_.find(id);
        if (it == connections_.end()) return;
        QByteArray payload;
        while (it != connections_.end() && it->second.socket->bytesAvailable() > 0) {
            if (!it->second.decoder.feed(it->second.socket->read(64 * 1024))) { drop(id, true); return; }
            while (it->second.decoder.next(payload)) {
                if (received) received(id, payload);
                it = connections_.find(id); // The callback may have closed it.
                if (it == connections_.end()) return;
            }
        }
    }

    void drop(int id, bool abort) {
        const auto it = connections_.find(id);
        if (it == connections_.end()) return;
        auto *socket = it->second.socket;
        connections_.erase(it);
        socket->disconnect(this);
        if (abort) socket->abort();
        socket->deleteLater();
        if (disconnected) disconnected(id);
    }

    QString name_;
    std::unique_ptr<QLockFile> lock_;
    QLocalServer *server_ = nullptr;
    std::map<int, Connection> connections_;
    int next_ = 1;
};
}

QString interactionEndpoint(const QString &name) {
#ifdef Q_OS_UNIX
    const auto directory = privateDirectory();
    return directory.isEmpty() ? QString() : directory + "/" + safeName(name.isEmpty() ? "interactions" : name) + ".sock";
#else
    return "agent-pet-" + userTag() + "-" + safeName(name.isEmpty() ? "interactions" : name);
#endif
}

std::unique_ptr<InteractionTransport> createInteractionTransport(const QString &name) {
    return std::make_unique<LocalInteractionTransport>(name);
}

}
