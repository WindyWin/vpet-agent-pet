// The session host: one detached process per Claude Code session, started by the first hook of the session. It owns the
// interaction stream connection and registers the conversation once, so hooks (short-lived, many, and the single
// waiting one) only talk to it over a private local socket and can never take the conversation from each other. It
// exits when its Claude process is gone, the session ends, or the pet has been away for a minute; the next hook
// starts a fresh one (a host restart is a gap in the message history, never a new conversation: the epoch is the
// Claude process, not the host).
#include "claude_bridge.h"
#include "claude_internal.h"
#include "interactions/frame.h"
#include "interactions/model.h"
#include "platform/contracts/interaction_transport.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <memory>
#ifdef Q_OS_UNIX
#include <csignal>
#include <cerrno>
#include <unistd.h>
#endif

namespace pet::claude::detail {

using namespace pet::interactions;

namespace {

constexpr int pollMs = 2000;          // Is Claude Code still there?
constexpr int reconnectMs = 1000;
constexpr int ackMs = 3000;           // A hook that took a delivery must confirm it was written.
constexpr int awayMs = 60 * 1000;     // The pet has not been reachable for this long: let go.
constexpr int windowMessages = 50;
constexpr int maxOutcomes = 100;
constexpr int maxPeerBytes = 1024 * 1024;
constexpr unsigned hostCapabilities = Messages | SingleChoice | CustomAnswer | FollowUp | IdempotentCommands;

struct Peer {
    QLocalSocket *socket = nullptr;
    QByteArray buffer;
    bool attached = false;
};

struct PendingAsk {
    InputRequest request;
    QStringList labels;
    QPointer<QLocalSocket> peer;
    QTimer *deadline = nullptr;
    QString commandId; // Set while an answer waits for the hook to confirm it printed it.
    QTimer *confirm = nullptr;
};

class Host : public QObject {
public:
    struct Config { QString sessionId, title; qint64 pid = 0, startedAt = 0; };

    explicit Host(const Config &config) : config_(config) {
        key_ = {"claude", config.sessionId, QString::number(config.pid) + "." + QString::number(config.startedAt)};
        nonce_ = QUuid::createUuid().toString(QUuid::Id128).left(8);
        sequence_ = QDateTime::currentMSecsSinceEpoch(); // Past anything an earlier host sent, so a restart shows as a gap.
        connect(&poll_, &QTimer::timeout, this, [this] { checkClaude(); });
        poll_.start(pollMs);
        connect(&reconnect_, &QTimer::timeout, this, [this] { connectPet(); });
        reconnect_.setSingleShot(true);
        away_.setSingleShot(true);
        connect(&away_, &QTimer::timeout, this, [this] { QCoreApplication::exit(0); });
        connect(&pet_, &QLocalSocket::connected, this, [this] { onPetConnected(); });
        connect(&pet_, &QLocalSocket::readyRead, this, [this] { onPetData(); });
        connect(&pet_, &QLocalSocket::disconnected, this, [this] { onPetLost(); });
        connect(&pet_, &QLocalSocket::errorOccurred, this, [this] { if (pet_.state() == QLocalSocket::UnconnectedState) onPetLost(); });
    }

    ~Host() override {
        pet_.disconnect(this); // Its signals must not reach a half-destroyed host.
        pet_.abort();
        if (server_) server_->close(); // Removes the socket file.
    }

    bool start() {
        const auto endpoint = relayEndpoint(config_.sessionId);
        if (endpoint.isEmpty()) return false;
        // One host per session; a second one started by a racing hook finds the lock taken and leaves.
        lock_ = std::make_unique<QLockFile>(endpoint + ".lock");
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock(0)) return false;
        server_ = new QLocalServer(this);
        server_->setSocketOptions(QLocalServer::UserAccessOption);
        QLocalServer::removeServer(endpoint);
        if (!server_->listen(endpoint)) return false;
        connect(server_, &QLocalServer::newConnection, this, [this] { acceptPeers(); });
        connectPet();
        return true;
    }

private:
    // ---- The pet side ---------------------------------------------------------------------------------------------

    void connectPet() {
        if (pet_.state() != QLocalSocket::UnconnectedState) return;
        if (!away_.isActive()) away_.start(awayMs);
        pet_.connectToServer(petEndpoint());
    }
    void sendFrame(QJsonObject frame) {
        if (pet_.state() != QLocalSocket::ConnectedState) return;
        frame["version"] = protocolVersion;
        pet_.write(encodeFrame(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
    }
    QJsonObject entry() const {
        return {{"conversation", key_.toJson()}, {"title", config_.title.isEmpty() ? config_.sessionId.left(8) : config_.title.left(128)},
                {"ready", ready()}, {"capabilities", QJsonArray::fromStringList(capabilityNames(hostCapabilities))}};
    }
    void onPetConnected() {
        decoder_ = FrameDecoder();
        QJsonObject hello{{"type", "hello"}, {"provider", "claude"}, {"generation", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                          {"conversations", QJsonArray{entry()}}};
        sendFrame(hello);
    }
    void onPetLost() {
        registered_ = false;
        decoder_ = FrameDecoder();
        pet_.abort();
        announcedReady_ = false;
        // A question the pet could still be showing is not answerable any more; the terminal takes it back.
        for (const auto &id : pendingAsks_.keys()) giveUp(id, RequestState::Cancelled, false);
        if (!reconnect_.isActive()) reconnect_.start(reconnectMs);
    }
    void onPetData() {
        if (!decoder_.feed(pet_.readAll())) { pet_.abort(); return; }
        QByteArray payload;
        while (decoder_.next(payload)) onFrame(QJsonDocument::fromJson(payload).object());
    }
    void onFrame(const QJsonObject &frame) {
        const auto type = frame.value("type").toString();
        if (type == "welcome") {
            bool refused = false;
            for (const auto &value : frame.value("refused").toArray()) {
                ConversationKey k;
                QString error;
                if (ConversationKey::fromJson(value.toObject(), k, error) && k == key_) refused = true;
            }
            registered_ = !refused;
            if (registered_) away_.stop();
            for (const auto &value : frame.value("resync").toArray()) {
                ConversationKey k;
                QString error;
                if (ConversationKey::fromJson(value.toObject(), k, error) && k == key_) sendSnapshot();
            }
        } else if (type == "resync") {
            sendSnapshot();
        } else if (type == "command") {
            Command command;
            QString error;
            if (Command::fromJson(frame, command, error) && command.conversation == key_) onCommand(command);
        }
    }
    void sendSnapshot() {
        QJsonArray messages, requests, outcomes;
        for (const auto &m : messages_) messages.append(m.toJson());
        for (const auto &ask : pendingAsks_) requests.append(ask.request.toJson());
        for (const auto &r : resolved_) requests.append(r.toJson());
        for (auto it = outcomes_.begin(); it != outcomes_.end(); ++it) {
            auto o = it.value();
            o["command_id"] = it.key();
            outcomes.append(o);
        }
        QJsonObject snapshot{{"type", "snapshot"}, {"conversation", key_.toJson()}, {"watermark", double(sequence_)},
                             {"first_sequence", messages_.isEmpty() ? 0.0 : double(messages_.first().sequence)}, {"ready", ready()},
                             {"capabilities", QJsonArray::fromStringList(capabilityNames(hostCapabilities))}, {"messages", messages},
                             {"requests", requests}, {"command_outcomes", outcomes}};
        sendFrame(snapshot);
        announcedReady_ = ready();
    }

    // ---- Conversation state ---------------------------------------------------------------------------------------

    // Ready means a follow-up can be delivered now: the turn ended and a waiting hook is armed to wake the session.
    bool ready() const { return registered_ && idle_ && waiter_ && !wake_ && pendingAsks_.isEmpty(); }
    void publishReady() {
        const bool now = ready();
        if (now == announcedReady_) return;
        announcedReady_ = now;
        if (registered_) sendFrame({{"type", "ready"}, {"conversation", key_.toJson()}, {"ready", now}});
    }
    void addMessage(Role role, const QString &text, const QString &stem) {
        if (text.trimmed().isEmpty()) return;
        Message m;
        m.messageId = nonce_ + "-" + stem + QString::number(++counter_);
        m.sequence = ++sequence_;
        m.role = role;
        m.text = text;
        m.createdAt = QDateTime::currentMSecsSinceEpoch();
        messages_.append(m);
        while (messages_.size() > windowMessages) messages_.removeFirst();
        if (registered_) sendFrame({{"type", "message"}, {"conversation", key_.toJson()}, {"message", m.toJson()}});
    }
    void result(const QString &commandId, const QString &outcome, const QString &reason = {}, bool stillAnswerable = true) {
        QJsonObject o{{"outcome", outcome}, {"still_answerable", stillAnswerable}};
        if (!reason.isEmpty()) o["reason"] = reason;
        if (outcome != "unknown") {
            outcomes_.insert(commandId, o);
            while (outcomes_.size() > maxOutcomes) outcomes_.erase(outcomes_.begin());
        }
        o["type"] = "command_result";
        o["command_id"] = commandId;
        sendFrame(o);
    }

    // ---- Commands from the pet ------------------------------------------------------------------------------------

    void onCommand(const Command &command) {
        const auto known = outcomes_.constFind(command.commandId);
        if (known != outcomes_.constEnd()) { // A repeated command ID gets its recorded answer.
            result(command.commandId, known->value("outcome").toString(), known->value("reason").toString(), known->value("still_answerable").toBool(true));
            return;
        }
        if (command.action == Action::AnswerRequest) answer(command);
        else followUp(command);
    }

    void answer(const Command &command) {
        auto it = pendingAsks_.find(command.requestId);
        if (it == pendingAsks_.end() || !it->peer || !it->commandId.isEmpty()) {
            result(command.commandId, "rejected", reason::requestClosed, false);
            return;
        }
        QString text = command.text;
        if (!command.optionId.isEmpty()) {
            bool ok = false;
            const int index = command.optionId.toInt(&ok);
            if (!ok || index < 1 || index > it->labels.size()) { result(command.commandId, "rejected", reason::invalidOption, true); return; }
            text = it->labels[index - 1];
        }
        it->commandId = command.commandId;
        it->deadline->stop(); // The pet's answer won the race against the deadline; the hook now only has to print it.
        writeLine(it->peer, {{"answer", text}});
        const auto id = command.requestId;
        it->confirm = new QTimer(this);
        it->confirm->setSingleShot(true);
        connect(it->confirm, &QTimer::timeout, this, [this, id] { settleUnconfirmed(id); });
        it->confirm->start(ackMs);
    }

    void followUp(const Command &command) {
        if (!idle_ || !waiter_ || wake_) {
            result(command.commandId, "rejected", reason::notReady, true);
            publishReady();
            return;
        }
        // Claude Code says whether it is working or waiting for the terminal user; a follow-up never steers a turn.
        SessionFile file;
        if (readSessionFile(config_.pid, file) && !file.status.isEmpty() && file.status != "idle") {
            idle_ = false;
            result(command.commandId, "rejected", reason::notReady, true);
            publishReady();
            return;
        }
        wake_ = std::make_unique<Wake>(Wake{command.commandId, command.text, new QTimer(this)});
        wake_->confirm->setSingleShot(true);
        connect(wake_->confirm, &QTimer::timeout, this, [this] { settleWake(false, "hook_unconfirmed"); });
        wake_->confirm->start(ackMs);
        writeLine(waiter_, {{"wake", command.text}});
        publishReady();
    }

    void settleWake(bool delivered, const QString &why = {}) {
        if (!wake_) return;
        const auto w = std::move(wake_);
        w->confirm->deleteLater();
        if (delivered) {
            idle_ = false; // The woken session is working now.
            addMessage(Role::User, w->text, "f");
            result(w->commandId, "accepted");
        } else {
            result(w->commandId, "unknown", why); // The wake text may or may not have reached Claude Code.
        }
        publishReady();
    }
    void settleUnconfirmed(const QString &requestId) {
        const auto it = pendingAsks_.find(requestId);
        if (it == pendingAsks_.end()) return;
        result(it->commandId, "unknown", "hook_unconfirmed");
        resolve(requestId, RequestState::Answered, false);
    }

    // ---- Questions ------------------------------------------------------------------------------------------------

    void onAsk(Peer &peer, const QJsonObject &message) {
        const auto id = message.value("id").toString();
        const auto question = message.value("question").toString();
        const int waitMs = qBound(1000, message.value("wait_ms").toInt(20000), 120000);
        QStringList labels;
        for (const auto &v : message.value("options").toArray()) labels << v.toString();
        if (!registered_ || id.isEmpty() || question.isEmpty() || pendingAsks_.contains(id) || labels.size() > Limits::options) {
            writeLine(peer.socket, {{"unavailable", true}}); // The terminal dialog opens as if no pet were there.
            return;
        }
        PendingAsk ask;
        ask.request.requestId = id;
        ask.request.question = question;
        ask.request.customAllowed = true;
        for (int i = 0; i < labels.size(); ++i) ask.request.options.append({QString::number(i + 1), labels[i]});
        ask.request.expiresAt = QDateTime::currentMSecsSinceEpoch() + waitMs;
        ask.labels = labels;
        ask.peer = peer.socket;
        ask.deadline = new QTimer(this);
        ask.deadline->setSingleShot(true);
        connect(ask.deadline, &QTimer::timeout, this, [this, id] { giveUp(id, RequestState::Expired, true); });
        ask.deadline->start(waitMs);
        idle_ = false; // A question means a turn is running, whatever the hooks last said.
        sendFrame({{"type", "request"}, {"conversation", key_.toJson()}, {"request", ask.request.toJson()}});
        pendingAsks_.insert(id, ask);
        publishReady();
    }
    // No answer will be used: let the hook fall through to the terminal dialog (if it still listens) and close the pet's card.
    void giveUp(const QString &id, RequestState state, bool tellPet) {
        const auto it = pendingAsks_.find(id);
        if (it == pendingAsks_.end() || !it->commandId.isEmpty()) return;
        writeLine(it->peer, {{"timeout", true}});
        resolve(id, state, tellPet);
    }
    void resolve(const QString &id, RequestState state, bool tellPet) {
        const auto it = pendingAsks_.find(id);
        if (it == pendingAsks_.end()) return;
        auto request = it->request;
        request.state = state;
        it->deadline->deleteLater();
        if (it->confirm) it->confirm->deleteLater();
        pendingAsks_.erase(it);
        resolved_.append(request);
        while (resolved_.size() > 5) resolved_.removeFirst();
        if (tellPet && registered_) sendFrame({{"type", "request_state"}, {"conversation", key_.toJson()}, {"request_id", id}, {"state", requestStateName(state)}});
        publishReady();
    }

    // ---- Hooks ----------------------------------------------------------------------------------------------------

    void acceptPeers() {
        while (auto *socket = server_->nextPendingConnection()) {
            socket->setParent(this);
            peers_.insert(socket, Peer{socket, {}, false});
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onPeerData(socket); });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] { onPeerGone(socket); });
        }
    }
    void writeLine(QLocalSocket *socket, const QJsonObject &message) {
        if (socket && socket->state() == QLocalSocket::ConnectedState) socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + "\n");
    }
    void onPeerData(QLocalSocket *socket) {
        const auto it = peers_.find(socket);
        if (it == peers_.end()) return;
        it->buffer += socket->readAll();
        if (it->buffer.size() > maxPeerBytes) { socket->abort(); return; }
        int at;
        while ((at = it->buffer.indexOf('\n')) >= 0) {
            const auto line = it->buffer.left(at);
            it->buffer.remove(0, at + 1);
            onPeerMessage(*it, QJsonDocument::fromJson(line).object());
            if (!peers_.contains(socket)) return;
        }
    }
    void onPeerMessage(Peer &peer, const QJsonObject &message) {
        const auto op = message.contains("ack") ? QString("ack") : message.value("op").toString();
        if (op == "attach") {
            if (message.value("session_id").toString() != config_.sessionId || message.value("epoch").toString() != key_.epoch) {
                // Another Claude process uses this session ID now (a resume): this host is finished.
                writeLine(peer.socket, {{"error", "epoch"}});
                QTimer::singleShot(100, this, [] { QCoreApplication::exit(0); });
                return;
            }
            peer.attached = true;
            writeLine(peer.socket, {{"ok", true}, {"linked", registered_}});
            return;
        }
        if (!peer.attached) { peer.socket->abort(); return; }
        if (op == "prompt") {
            promptSeen_ = true;
            idle_ = false;
            addMessage(Role::User, message.value("text").toString(), "u");
            writeLine(peer.socket, {{"ok", true}});
            publishReady();
        } else if (op == "stop") {
            idle_ = true;
            addMessage(Role::Agent, message.value("text").toString(), "a");
            writeLine(peer.socket, {{"ok", true}});
            publishReady();
        } else if (op == "wait") {
            // A session that has not been prompted yet is idle at its start; later waiters come after a Stop.
            if (message.value("start").toBool(false) && !promptSeen_ && !wake_) idle_ = true;
            // One armed waiter is enough: a newer one replaces the older, which lets go without waking anything.
            if (wake_) { writeLine(peer.socket, {{"release", true}}); return; }
            if (waiter_ && waiter_ != peer.socket) writeLine(waiter_, {{"release", true}});
            waiter_ = peer.socket;
            publishReady();
        } else if (op == "ask") {
            onAsk(peer, message);
        } else if (op == "ack") {
            onAck(peer.socket);
        } else if (op == "end") {
            writeLine(peer.socket, {{"ok", true}});
            endConversation();
        }
    }
    void onAck(QLocalSocket *socket) {
        if (wake_ && socket == waiter_) { settleWake(true); return; }
        for (auto it = pendingAsks_.begin(); it != pendingAsks_.end(); ++it)
            if (it->peer == socket && !it->commandId.isEmpty()) {
                result(it->commandId, "accepted");
                resolve(it.key(), RequestState::Answered, false); // The service marks it answered on acceptance.
                return;
            }
    }
    void onPeerGone(QLocalSocket *socket) {
        // A hook sends its confirmation and exits at once; the close can be noticed before the last bytes are read.
        if (socket->bytesAvailable() > 0) onPeerData(socket);
        peers_.remove(socket);
        socket->deleteLater();
        if (waiter_ == socket) {
            waiter_ = nullptr;
            if (wake_) settleWake(false, "hook_exited"); // It exits right after confirming, so an ack always comes first.
        }
        for (auto it = pendingAsks_.begin(); it != pendingAsks_.end(); ++it)
            if (it->peer == socket) {
                if (it->commandId.isEmpty()) { // Interrupted in the terminal, or the hook was killed: no answer will be used.
                    resolve(it.key(), RequestState::Cancelled, true);
                } else {
                    result(it->commandId, "unknown", "hook_exited");
                    resolve(it.key(), RequestState::Answered, false);
                }
                break;
            }
        publishReady();
    }

    // ---- Lifetime -------------------------------------------------------------------------------------------------

    void checkClaude() {
        bool gone = false;
#ifdef Q_OS_UNIX
        gone = config_.pid > 0 && kill(pid_t(config_.pid), 0) != 0 && errno == ESRCH;
#endif
        SessionFile file;
        if (readSessionFile(config_.pid, file) && file.startedAt != config_.startedAt) gone = true; // The pid belongs to another process.
        if (gone) endConversation();
    }
    void endConversation() {
        if (registered_) {
            sendFrame({{"type", "conversation_ended"}, {"conversation", key_.toJson()}});
            pet_.flush();
            pet_.waitForBytesWritten(200);
        }
        QCoreApplication::exit(0);
    }

    struct Wake {
        QString commandId, text;
        QTimer *confirm;
    };

    Config config_;
    ConversationKey key_;
    QString nonce_;
    qint64 sequence_ = 0;
    int counter_ = 0;
    std::unique_ptr<QLockFile> lock_;
    QLocalServer *server_ = nullptr;
    QHash<QLocalSocket *, Peer> peers_;
    QLocalSocket pet_;
    FrameDecoder decoder_;
    QTimer poll_, reconnect_, away_;
    bool registered_ = false, announcedReady_ = false, idle_ = false, promptSeen_ = false;
    QVector<Message> messages_;
    QVector<InputRequest> resolved_;
    QHash<QString, PendingAsk> pendingAsks_;
    QHash<QString, QJsonObject> outcomes_;
    QPointer<QLocalSocket> waiter_;
    std::unique_ptr<Wake> wake_;
};

}

int runHost(const QStringList &args) {
    Host::Config config;
    for (int i = 3; i + 1 < args.size(); i += 2) {
        if (args[i] == "--session-id") config.sessionId = args[i + 1];
        else if (args[i] == "--pid") config.pid = args[i + 1].toLongLong();
        else if (args[i] == "--started") config.startedAt = args[i + 1].toLongLong();
        else if (args[i] == "--title") config.title = args[i + 1];
        else return 0;
    }
    if (config.sessionId.isEmpty() || config.pid <= 0 || config.startedAt <= 0) return 0;
    Host host(config);
    if (!host.start()) return 0;
    return QCoreApplication::exec();
}

}
