#pragma once
// A scriptable provider bridge for the interaction service: it speaks the wire protocol through an
// in-memory connection, records what the service sends it, and answers commands only when the test
// says so (a real bridge is asynchronous), so races, timeouts and disconnects are deterministic.
#include "interactions/service.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace pet::interactions::testing {

inline ConversationKey key(const QString &session, const QString &epoch = "e1", const QString &provider = "fake") {
    return {provider, session, epoch};
}

class FakeBridge {
public:
    FakeBridge(Service &service, int connection, QString provider = "fake", QString generation = "g1")
        : service_(service), connection_(connection), provider_(std::move(provider)), generation_(std::move(generation)) {}

    unsigned capabilities = Messages | SingleChoice | CustomAnswer | FollowUp | Reconcile | IdempotentCommands;
    QVector<QJsonObject> inbox;  // Every frame the service sent.
    QVector<Command> commands;   // Commands received, in order.
    bool up = false;

    void connect() {
        up = true;
        service_.attach(connection_, [this](const QByteArray &payload) {
            if (!up) return false;
            const auto frame = QJsonDocument::fromJson(payload).object();
            inbox.append(frame);
            if (frame.value("type") == "command") {
                Command command;
                QString error;
                if (Command::fromJson(frame, command, error)) commands.append(command);
            }
            return true;
        });
    }
    void disconnect() {
        up = false;
        service_.detach(connection_);
    }

    QJsonObject entry(const ConversationKey &k, bool ready, const QString &title = "api-web") const {
        return {{"conversation", k.toJson()}, {"title", title}, {"ready", ready},
                {"capabilities", QJsonArray::fromStringList(capabilityNames(capabilities))}};
    }
    void hello(const QVector<ConversationKey> &keys, bool ready = true, const QString &title = "api-web") {
        QJsonArray list;
        for (const auto &k : keys) list.append(entry(k, ready, title));
        send({{"type", "hello"}, {"version", protocolVersion}, {"provider", provider_}, {"generation", generation_}, {"conversations", list}});
    }
    // Registers another conversation on this connection after the handshake.
    void open(const ConversationKey &k, bool ready = true, const QString &title = "api-web") {
        auto frame = entry(k, ready, title);
        frame["type"] = "conversation_open";
        send(frame);
    }
    void open(const QVector<ConversationKey> &keys, bool ready = true, const QString &title = "api-web") {
        for (const auto &k : keys) open(k, ready, title);
    }
    void send(QJsonObject frame) {
        if (!frame.contains("version")) frame["version"] = protocolVersion;
        service_.receive(connection_, QJsonDocument(frame).toJson(QJsonDocument::Compact));
    }
    void sendRaw(const QByteArray &payload) { service_.receive(connection_, payload); }

    void message(const ConversationKey &k, qint64 sequence, const QString &id, const QString &text, const QString &role = "agent") {
        send({{"type", "message"}, {"conversation", k.toJson()},
              {"message", QJsonObject{{"message_id", id}, {"sequence", double(sequence)}, {"role", role}, {"text", text}, {"created_at", 1000.0 + sequence}}}});
    }
    static QJsonObject requestJson(const QString &id, int revision, const QString &question, const QStringList &options,
                                   bool custom = false, const QString &state = "pending") {
        QJsonArray list;
        for (const auto &o : options) list.append(QJsonObject{{"id", o}, {"label", o.toUpper()}});
        return {{"request_id", id}, {"revision", revision}, {"question", question}, {"options", list}, {"custom_answer", custom}, {"state", state}};
    }
    void request(const ConversationKey &k, const QString &id, int revision, const QString &question, const QStringList &options,
                 bool custom = false) {
        send({{"type", "request"}, {"conversation", k.toJson()}, {"request", requestJson(id, revision, question, options, custom)}});
    }
    void requestState(const ConversationKey &k, const QString &id, const QString &state) {
        send({{"type", "request_state"}, {"conversation", k.toJson()}, {"request_id", id}, {"state", state}});
    }
    void ready(const ConversationKey &k, bool value) { send({{"type", "ready"}, {"conversation", k.toJson()}, {"ready", value}}); }
    void ended(const ConversationKey &k) { send({{"type", "conversation_ended"}, {"conversation", k.toJson()}}); }

    void result(const QString &commandId, const QString &outcome, const QString &reason = {}, bool stillAnswerable = true) {
        QJsonObject frame{{"type", "command_result"}, {"command_id", commandId}, {"outcome", outcome}, {"still_answerable", stillAnswerable}};
        if (!reason.isEmpty()) frame["reason"] = reason;
        send(frame);
    }
    void accept(const Command &c) { result(c.commandId, "accepted"); }

    void snapshot(const ConversationKey &k, qint64 watermark, const QJsonArray &requests = {}, const QJsonArray &messages = {},
                  const QJsonArray &outcomes = {}, qint64 firstSequence = 0, bool ready = true) {
        QJsonObject frame{{"type", "snapshot"}, {"conversation", k.toJson()}, {"watermark", double(watermark)}, {"requests", requests},
                          {"messages", messages}, {"command_outcomes", outcomes}, {"ready", ready}};
        if (firstSequence) frame["first_sequence"] = double(firstSequence);
        send(frame);
    }

    QJsonObject lastError() const {
        for (int i = inbox.size() - 1; i >= 0; --i)
            if (inbox[i].value("type") == "error") return inbox[i];
        return {};
    }
    QStringList errorCodes() const {
        QStringList codes;
        for (const auto &f : inbox)
            if (f.value("type") == "error") codes << f.value("code").toString();
        return codes;
    }
    QJsonObject lastOfType(const QString &type) const {
        for (int i = inbox.size() - 1; i >= 0; --i)
            if (inbox[i].value("type") == type) return inbox[i];
        return {};
    }

private:
    Service &service_;
    int connection_;
    QString provider_, generation_;
};

inline Command answer(const ConversationKey &k, const QString &commandId, const QString &requestId, int revision, const QString &option,
                      const QString &source = "desktop") {
    Command c;
    c.commandId = commandId; c.conversation = k; c.action = Action::AnswerRequest; c.requestId = requestId;
    c.requestRevision = revision; c.optionId = option; c.source = source;
    return c;
}
inline Command customAnswer(const ConversationKey &k, const QString &commandId, const QString &requestId, int revision, const QString &text) {
    Command c = answer(k, commandId, requestId, revision, {});
    c.text = text;
    return c;
}
inline Command followUp(const ConversationKey &k, const QString &commandId, const QString &text, const QString &source = "desktop") {
    Command c;
    c.commandId = commandId; c.conversation = k; c.action = Action::SendFollowUp; c.text = text; c.source = source;
    return c;
}

}
