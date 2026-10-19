#include "claude_bridge.h"
#include "claude_internal.h"
#include "platform/contracts/hook_input.h"
#include "platform/contracts/interaction_transport.h"
#include "platform/contracts/startup.h"
#include "platform/headless.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QThread>
#include <cstdio>
#ifdef Q_OS_UNIX
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace pet::claude {

namespace {
constexpr int maxMessageBytes = 12 * 1024;   // Prompts and replies are display text, shortened to fit a frame.
constexpr int maxQuestionBytes = 12 * 1024;  // A question is never shortened: one that does not fit is left to the terminal.
constexpr int maxInputBytes = 1024 * 1024;
constexpr int inputDeadlineMs = 1000;
constexpr int quickReplyMs = 500;
}

// ---- Pure pieces ---------------------------------------------------------------------------------------------------

QString clipped(const QString &text) {
    auto out = text;
    while (out.toUtf8().size() > maxMessageBytes) out = out.left(qMax(1, out.size() * 3 / 4 - 1)).trimmed() + QChar(0x2026);
    return out;
}

bool parseQuestion(const QJsonObject &input, Question &question) {
    const auto tool = input.value("tool_name");
    if (!tool.isUndefined() && tool.toString() != "AskUserQuestion") return false;
    question.toolUseId = input.value("tool_use_id").toString();
    if (question.toolUseId.isEmpty() || question.toolUseId.size() > 200) return false;
    if (!input.value("tool_input").isObject()) return false;
    question.toolInput = input.value("tool_input").toObject();
    const auto questions = question.toolInput.value("questions").toArray();
    // One single-select question: the interaction stream's request is one question with one chosen option. Several
    // questions in one call, or a multi-select one, are answered in the terminal as always.
    if (questions.size() != 1 || !questions.first().isObject()) return false;
    const auto entry = questions.first().toObject();
    question.text = entry.value("question").toString();
    if (question.text.isEmpty() || question.text.toUtf8().size() > maxQuestionBytes) return false;
    if (entry.value("multiSelect").toBool(false)) return false;
    question.labels.clear();
    const auto options = entry.value("options");
    if (!options.isUndefined() && !options.isArray()) return false;
    for (const auto &option : options.toArray()) {
        const auto label = option.toObject().value("label").toString();
        if (label.isEmpty() || label.size() > 256) return false;
        question.labels << label;
    }
    return question.labels.size() <= 20;
}

QByteArray questionAnswerJson(const Question &question, const QString &answer) {
    auto updated = question.toolInput;
    updated["answers"] = QJsonObject{{question.text, answer}};
    const QJsonObject decision{{"hookEventName", "PreToolUse"}, {"permissionDecision", "allow"}, {"updatedInput", updated}};
    return QJsonDocument(QJsonObject{{"hookSpecificOutput", decision}}).toJson(QJsonDocument::Compact) + "\n";
}

QString wakeText(const QString &followUp) {
    return "The user sent this message from their desktop pet (Agent Pet). Treat it as their next message to you:\n\n" + followUp + "\n";
}

// ---- Hook behaviour ------------------------------------------------------------------------------------------------

HookResult handleHook(const QString &event, int waitMs, const QJsonObject &input, Relay &relay,
                      const std::function<void(const HookResult &)> &deliver) {
    HookResult result;
    QJsonObject reply;
    // A hook in the background waits for the host to ask it to wake the session, or to let go.
    const auto wait = [&](bool start) {
        if (!relay.send({{"op", "wait"}, {"start", start}}) || !relay.receive(reply, -1)) return;
        if (reply.value("wake").isString() && !reply.value("wake").toString().isEmpty()) {
            result.exitCode = 2;
            result.err = wakeText(reply.value("wake").toString()).toUtf8();
            deliver(result);
            relay.send({{"ack", true}}); // Written to our stderr: the host may now report the follow-up delivered.
        }
    };
    if (event == "UserPromptSubmit") {
        if (relay.send({{"op", "prompt"}, {"text", clipped(input.value("prompt").toString())}})) relay.receive(reply, quickReplyMs);
    } else if (event == "Stop") {
        if (relay.send({{"op", "stop"}, {"text", clipped(input.value("last_assistant_message").toString())}}) && relay.receive(reply, quickReplyMs))
            wait(false);
    } else if (event == "SessionStart") {
        wait(true);
    } else if (event == "SessionEnd") {
        if (relay.send({{"op", "end"}})) relay.receive(reply, quickReplyMs);
    } else if (event == "PreToolUse") {
        Question question;
        if (!parseQuestion(input, question)) return result;
        QJsonArray labels;
        for (const auto &label : question.labels) labels.append(label);
        if (!relay.send({{"op", "ask"}, {"id", question.toolUseId}, {"question", question.text}, {"options", labels}, {"wait_ms", waitMs}})) return result;
        // The host enforces the deadline; this is only the backstop for a host that went away.
        if (!relay.receive(reply, waitMs + 2000) || !reply.value("answer").isString() || reply.value("answer").toString().isEmpty()) return result;
        result.out = questionAnswerJson(question, reply.value("answer").toString());
        deliver(result);
        relay.send({{"ack", true}});
    }
    return result;
}

// ---- Locating the session ------------------------------------------------------------------------------------------

namespace detail {
QString sessionsDirectory() {
    return qEnvironmentVariable("CLAUDE_CONFIG_DIR", QDir::homePath() + "/.claude") + "/sessions";
}
bool readSessionFile(qint64 pid, SessionFile &file) {
    QFile f(sessionsDirectory() + "/" + QString::number(pid) + ".json");
    if (!f.open(QIODevice::ReadOnly) || f.size() > 256 * 1024) return false;
    const auto o = QJsonDocument::fromJson(f.readAll()).object();
    file.pid = qint64(o.value("pid").toDouble());
    file.startedAt = qint64(o.value("startedAt").toDouble());
    file.sessionId = o.value("sessionId").toString();
    file.status = o.value("status").toString();
    return file.pid == pid && file.startedAt > 0 && !file.sessionId.isEmpty();
}
QString relayEndpoint(const QString &sessionId) {
    return platform::interactionEndpoint("claude-" + QString::fromLatin1(QCryptographicHash::hash(sessionId.toUtf8(), QCryptographicHash::Sha256).toHex().left(20)));
}
QString petEndpoint() {
    const auto set = qEnvironmentVariable("AGENT_PET_INTERACTION_ENDPOINT");
    return set.isEmpty() ? platform::interactionEndpoint() : set;
}
}

namespace {

// The hook's side of the relay: a blocking local socket, no event loop.
class SocketRelay : public Relay {
public:
    bool open(const QString &endpoint, int timeoutMs) {
        socket_.connectToServer(endpoint);
        return socket_.waitForConnected(timeoutMs);
    }
    bool send(const QJsonObject &message) override {
        const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + "\n";
        return socket_.state() == QLocalSocket::ConnectedState && socket_.write(bytes) == bytes.size() && socket_.waitForBytesWritten(1000);
    }
    bool receive(QJsonObject &message, int timeoutMs) override {
        QElapsedTimer clock; clock.start();
        for (;;) {
            const int at = buffer_.indexOf('\n');
            if (at >= 0) {
                const auto line = buffer_.left(at);
                buffer_.remove(0, at + 1);
                message = QJsonDocument::fromJson(line).object();
                return true;
            }
            if (buffer_.size() > maxInputBytes) return false;
            const int left = timeoutMs < 0 ? -1 : int(qMax<qint64>(0, timeoutMs - clock.elapsed()));
            if (timeoutMs >= 0 && left == 0) return false;
            if (!socket_.waitForReadyRead(left)) return false;
            buffer_ += socket_.readAll();
        }
    }
private:
    QLocalSocket socket_;
    QByteArray buffer_;
};

// Whether a pet is serving the interaction stream: a bare connect, so a stale socket file does not count.
bool petListening() {
    QLocalSocket probe;
    probe.connectToServer(detail::petEndpoint());
    const bool up = probe.waitForConnected(150);
    probe.abort();
    return up;
}

bool readInput(QByteArray &data) {
#ifdef Q_OS_UNIX
    QElapsedTimer deadline; deadline.start();
    for (;;) {
        const auto left = inputDeadlineMs - deadline.elapsed();
        if (left <= 0) return false;
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&input, 1, int(left));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) return false;
        char chunk[8192];
        const auto size = read(STDIN_FILENO, chunk, sizeof(chunk));
        if (size == 0) return true;
        if (size < 0) return false;
        data.append(chunk, int(size));
        if (data.size() > maxInputBytes) return false;
    }
#else
    QString error;
    return platform::readHookInput(maxInputBytes, data, error);
#endif
}

// Connects to the session's host, starting it first when a pet is listening and none runs yet.
std::unique_ptr<SocketRelay> connectHost(const QString &sessionId, const detail::SessionFile &session, const QString &title, bool start) {
    const auto endpoint = detail::relayEndpoint(sessionId);
    const auto attach = [&](SocketRelay &relay) {
        QJsonObject reply;
        return relay.send({{"op", "attach"}, {"session_id", sessionId}, {"epoch", session.epoch()}}) && relay.receive(reply, quickReplyMs) && reply.value("ok").toBool();
    };
    int mismatches = 0;
    bool launched = false;
    for (int attempt = 0; attempt < 60; ++attempt) {
        auto relay = std::make_unique<SocketRelay>();
        if (relay->open(endpoint, 200)) {
            if (attach(*relay)) return relay;
            // A host of another process using this session ID (a resume) is going away; one retry after it has.
            if (++mismatches > 1) return nullptr;
            QThread::msleep(300);
            continue;
        }
        if (!start) return nullptr;
        if (!launched) {
            if (!petListening()) return nullptr;
            launched = launchDetached(QCoreApplication::applicationFilePath(),
                {"claude-interact", "host", "--session-id", sessionId, "--pid", QString::number(session.pid), "--started", QString::number(session.startedAt),
                 "--title", title});
            if (!launched) return nullptr;
        }
        QThread::msleep(25);
    }
    return nullptr;
}

}

int interactCommand(const QStringList &args) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    if (args.value(2) == "host") return detail::runHost(args);
    QString event;
    int waitSeconds = defaultQuestionWaitSeconds;
    for (int i = 2; i < args.size(); ++i) {
        if (args[i] == "--event" && i + 1 < args.size() && event.isEmpty()) event = args[++i];
        else if (args[i] == "--wait" && i + 1 < args.size()) waitSeconds = qBound(1, args[++i].toInt(), maxQuestionWaitSeconds);
        else if (args[i] == "--registration" && args.value(i + 1) == interactionRegistration) ++i;
        else return 0;
    }
    if (!interactionEvents().contains(event)) return 0;
    QByteArray data;
    if (!readInput(data)) return 0;
    const auto input = QJsonDocument::fromJson(data).object();
    const auto sessionId = input.value("session_id").toString();
    if (sessionId.isEmpty()) return 0;
    // Claude Code is the nearest ancestor with a session file of this session; without it the hook does nothing.
    detail::SessionFile session;
    bool found = false;
    const auto parent = platform::parentProcessId();
    auto chain = platform::createProcessServices()->ancestors(parent);
    chain.prepend(parent);
    for (const auto pid : chain)
        if (detail::readSessionFile(pid, session) && session.sessionId == sessionId) { found = true; break; }
    if (!found) return 0;
    const auto title = QFileInfo(input.value("cwd").toString()).fileName();
    auto relay = connectHost(sessionId, session, title, event != "SessionEnd");
    if (!relay) return 0;
    const auto result = handleHook(event, waitSeconds * 1000, input, *relay, [](const HookResult &r) {
        if (!r.out.isEmpty()) { std::fwrite(r.out.constData(), 1, size_t(r.out.size()), stdout); std::fflush(stdout); }
        if (!r.err.isEmpty()) { std::fwrite(r.err.constData(), 1, size_t(r.err.size()), stderr); std::fflush(stderr); }
    });
    return result.exitCode;
}

}
