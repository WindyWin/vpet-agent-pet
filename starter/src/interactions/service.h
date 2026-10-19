#pragma once
#include <QJsonObject>
#include <functional>
#include <map>
#include "interactions/model.h"

namespace pet::interactions {

// One observable change. UIs re-read the conversation (or the result) from the service.
struct Change {
    enum class Kind { Conversation, CommandResult } kind = Kind::Conversation;
    ConversationKey key;
    CommandResult result; // For Kind::CommandResult.
};

// The interaction service: owns conversation identity, the request state machine, command arbitration
// and the bounded store, for every interface (desktop, and later Telegram). It speaks to provider
// bridges through opaque connections (attach / receive / detach) and never touches sockets, widgets
// or timers itself; call expire() periodically. Single-threaded.
class Service {
public:
    using Clock = std::function<qint64()>;
    using Send = std::function<bool(const QByteArray &payload)>;

    // Tunable bounds; defaults are the specification's limits.
    struct Budget {
        int conversations = Limits::conversations;
        int messages = Limits::messages;
        int pendingRequests = Limits::pendingRequests;
        int commands = Limits::commands;
        qint64 contentBytes = Limits::contentBytes;
    };

    explicit Service(Clock clock = {});
    Budget budget;
    int commandTimeoutMs = 15000;

    // Provider bridge side.
    void attach(int connection, Send send);
    void receive(int connection, const QByteArray &payload);
    void detach(int connection);

    // Interface side. Validates, reserves the request and dispatches. The result is Pending while the
    // bridge works; its final outcome arrives as a Change of kind CommandResult.
    CommandResult submit(const Command &command);
    void expire();

    const Conversation *find(const ConversationKey &key) const;
    QList<ConversationKey> keys() const; // Most recent activity first.
    int attentionCount() const;          // Sessions with an open question, not events.
    qint64 contentBytes() const;

    void setDraft(const ConversationKey &key, const QString &requestId, const QString &text);
    QString draft(const ConversationKey &key, const QString &requestId) const;
    void markRead(const ConversationKey &key, const QString &messageId);

    int subscribe(std::function<void(const Change &)> observer);
    void unsubscribe(int id);

private:
    struct Connection {
        Send send;
        bool greeted = false;
        QString provider, generation;
    };

    qint64 now() const { return clock_(); }
    Conversation *mutableFind(const ConversationKey &key);
    Conversation *owned(int connection, const QJsonObject &frame, QString &error);
    bool sendTo(int connection, const QJsonObject &frame);
    void error(int connection, const QString &code, const QString &message, const QJsonObject &extra = {});
    void changed(const Conversation &conversation);
    void emitResult(const Conversation &conversation, const CommandResult &result);

    void hello(int connection, const QJsonObject &frame);
    bool registerConversation(int connection, const QJsonObject &entry, ConversationKey &key);
    void onMessage(int connection, const QJsonObject &frame);
    void onRequest(int connection, const QJsonObject &frame);
    void onRequestState(int connection, const QJsonObject &frame);
    void onReady(int connection, const QJsonObject &frame);
    void onEnded(int connection, const QJsonObject &frame);
    void onCommandResult(int connection, const QJsonObject &frame);
    void onSnapshot(int connection, const QJsonObject &frame);

    bool addMessage(Conversation &c, Message message, bool trackGap);
    bool storeRequest(Conversation &c, InputRequest request, bool fromSnapshot);
    void settleRequest(Conversation &c, InputRequest &request, RequestState state, const QString &replacedBy = {});
    void applyResult(Conversation &c, CommandRecord &record, Outcome outcome, const QString &reason,
                     const QString &receipt = {}, bool stillAnswerable = true);
    void endConversation(Conversation &c);
    void disconnectConversation(Conversation &c, const QString &why);
    bool makeRoom(const Conversation *keep, qint64 incoming);
    bool evictConversation(const Conversation *keep);
    bool evictMessage(bool readOnly);
    void trimMessages(Conversation &c);
    void trimRequests(Conversation &c);

    Clock clock_;
    std::map<QString, Conversation> conversations_;
    std::map<int, Connection> connections_;
    std::map<int, std::function<void(const Change &)>> observers_;
    int nextObserver_ = 1;
    qint64 order_ = 0;
};

}
