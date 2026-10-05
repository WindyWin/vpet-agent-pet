#include "local.h"
#include "autostart.h"
#include "providers/adapters.h"
#include "hosts/registry.h"
#include "platform/linux/process.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QUuid>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace pet {
static QByteArray endpoint(QString &error) {
    QString root = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (root.isEmpty()) root = "/tmp";
    const auto directory = QFile::encodeName(root + "/agent-pet-" + QString::number(getuid()));
    if (mkdir(directory.constData(), 0700) != 0 && errno != EEXIST) { error = "Cannot create private runtime directory"; return {}; }
    struct stat st{};
    if (lstat(directory.constData(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0777) != 0700) {
        error = "Runtime directory must be owned by this user with mode 0700"; return {};
    }
    auto path = directory + "/events.sock";
    if (path.size() >= int(sizeof(sockaddr_un::sun_path))) { error = "Runtime socket path too long"; return {}; }
    return path;
}
static sockaddr_un address(const QByteArray &path) {
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.constData(), path.size() + 1); return addr;
}
Receiver::~Receiver() {
    if (socket_ >= 0) { close(socket_); if (!path_.isEmpty()) unlink(path_.constData()); }
    if (lock_ >= 0) close(lock_);
}
bool Receiver::start(QString &error) {
    const auto path = endpoint(error);
    if (path.isEmpty()) return false;
    const auto lockPath = path + ".lock";
    lock_ = open(lockPath.constData(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_ < 0 || flock(lock_, LOCK_EX | LOCK_NB)) { error = "Another Agent Pet monitor is running, or its lock is unavailable"; return false; }
    socket_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (socket_ < 0) { error = "Cannot create local socket"; return false; }
    unlink(path.constData());
    const auto addr = address(path);
    if (bind(socket_, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr))) { error = "Cannot bind local socket"; return false; }
    path_ = path;
    chmod(path_.constData(), 0600);
    auto *notifier = new QSocketNotifier(socket_, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this] {
        // Yield to the desktop after a bounded batch, even under a busy sender.
        for (int i = 0; i < 64; ++i) {
            char buffer[8193];
            const auto size = recv(socket_, buffer, sizeof(buffer), MSG_DONTWAIT | MSG_TRUNC);
            if (size < 0) break;
            if (size > 8192) continue;
            Event event; QString error;
            if (Event::parse(QByteArray(buffer, size), event, error) && received) received(event);
        }
    });
    return true;
}
bool sendEvent(const QByteArray &data, QString &error) {
    const auto path = endpoint(error); if (path.isEmpty()) return false;
    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) { error = "Cannot create sender"; return false; }
    const auto addr = address(path);
    const auto sent = sendto(fd, data.constData(), data.size(), MSG_DONTWAIT | MSG_NOSIGNAL,
                             reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    close(fd);
    if (sent != data.size()) { error = "Monitor unavailable or event queue full"; return false; }
    return true;
}
int eventCommand(const QStringList &args) {
    const bool hook = args.value(1) == "hook";
    QString error;
    auto fail = [&] { if (!hook) std::fprintf(stderr, "%s\n", qPrintable(error)); return hook ? 0 : 1; };
    QString provider;
    for (int i = 2; i < args.size(); ++i) {
        if (args[i] == "--provider" && i + 1 < args.size() && provider.isEmpty()) provider = args[++i];
        else if (hook && args[i] == "--registration" && args.value(i + 1) == "agent-pet-v1") ++i;
        else { error = "Usage: agent-pet hook --provider claude|codex, or agent-pet emit [--provider claude|codex]; hook reads provider JSON; emit reads normalized JSON"; return fail(); }
    }
    if ((hook && provider.isEmpty()) || (!provider.isEmpty() && provider != "claude" && provider != "codex")) {
        error = "Expected provider claude or codex"; return fail();
    }
    const int inputLimit = hook ? 1024 * 1024 : 8192;
    QByteArray data;
    QElapsedTimer deadline; deadline.start();
    while (deadline.elapsed() < 150) {
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const int result = poll(&input, 1, int(150 - deadline.elapsed()));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) { error = "Timed out reading event"; return fail(); }
        char buffer[8193];
        const auto size = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (size == 0) break;
        if (size < 0) { error = "Cannot read event"; return fail(); }
        data.append(buffer, size);
        if (data.size() > inputLimit) { error = "Input exceeds size limit"; return fail(); }
    }
    if (deadline.elapsed() >= 150) { error = "Timed out reading event"; return fail(); }
    auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { error = "Expected normalized JSON event"; return fail(); }
    auto object = doc.object();
    if (hook) {
        object = normalizeHook(provider, object, QDateTime::currentMSecsSinceEpoch());
        if (object.isEmpty()) return 0;
        const auto host = hosts::toV1(hosts::Registry::builtin().capture(QProcessEnvironment::systemEnvironment(),
                                                                         platform::processAncestors(getppid())));
        for (auto it = host.begin(); it != host.end(); ++it) object[it.key()] = it.value();
    }
    if (!provider.isEmpty()) {
        if (object.contains("provider") && object.value("provider").toString() != provider) { error = "Provider mismatch"; return fail(); }
        object["provider"] = provider;
    }
    // A generated ID identifies this delivery only; adapter-level retries need a stable ID.
    if (!object.contains("event_id")) object["event_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!object.contains("timestamp_ms")) object["timestamp_ms"] = QDateTime::currentMSecsSinceEpoch();
    data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    Event event;
    if (!Event::parse(data, event, error)) return fail();
    if (!sendEvent(data, error)) {
        // No pet is listening. A session start may launch one, which applies this event.
        if (hook) autostartPet(data, event.kind, {}, QProcessEnvironment::systemEnvironment(),
                               QCoreApplication::applicationFilePath(), launchDetached);
        return fail();
    }
    return 0;
}
}
