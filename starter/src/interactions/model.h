#pragma once
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

// Provider-neutral records of the direct-interaction protocol (docs/design/agent-interactions-spec.md).
// Qt Core only: no widgets, sockets or provider code.
namespace pet::interactions {

constexpr int protocolVersion = 1;

struct Limits {
    static constexpr int frameBytes = 64 * 1024;
    static constexpr int conversations = 64;
    static constexpr int messages = 200;
    static constexpr int pendingRequests = 16;
    static constexpr int textBytes = 16 * 1024;
    static constexpr int options = 20;
    static constexpr int labelChars = 256;
    static constexpr int commands = 100;
    static constexpr qint64 contentBytes = 16 * 1024 * 1024;
};

// Advertised separately; an unadvertised action is refused, never inferred from status support.
enum Capability : unsigned {
    Messages = 1, SingleChoice = 2, CustomAnswer = 4, FollowUp = 8, Reconcile = 16, IdempotentCommands = 32,
};
unsigned capabilitiesFromNames(const QStringList &names, QString &error);
QStringList capabilityNames(unsigned capabilities);

// (provider, session_id, conversation_epoch): the epoch changes when an identity is reused for a new
// conversation, and stays the same across a reconnect.
struct ConversationKey {
    QString provider, sessionId, epoch;
    bool valid() const { return !provider.isEmpty() && !sessionId.isEmpty() && !epoch.isEmpty(); }
    QString id() const { return provider + QChar(0x1f) + sessionId + QChar(0x1f) + epoch; }
    bool sameSession(const ConversationKey &o) const { return provider == o.provider && sessionId == o.sessionId; }
    bool operator==(const ConversationKey &o) const { return provider == o.provider && sessionId == o.sessionId && epoch == o.epoch; }
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, ConversationKey &key, QString &error);
};
inline size_t qHash(const ConversationKey &key, size_t seed = 0) { return qHash(key.id(), seed); }

enum class Role { Agent, User };

struct Message {
    QString messageId;
    qint64 sequence = 0;
    Role role = Role::Agent;
    QString text;
    qint64 createdAt = 0;
    bool read = false; // Local display state; never sent by a bridge.
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Message &message, QString &error);
};

struct Option {
    QString id, label;
};

enum class RequestState { Pending, Submitting, Answered, Cancelled, Expired, Replaced, Unknown };
const char *requestStateName(RequestState state);
bool requestStateFromName(const QString &name, RequestState &state);
inline bool terminal(RequestState state) {
    return state == RequestState::Answered || state == RequestState::Cancelled || state == RequestState::Expired
        || state == RequestState::Replaced;
}

struct InputRequest {
    QString requestId;
    int revision = 1;
    QString question;
    QVector<Option> options;
    bool customAllowed = false; // A request without options is a free-text question and must allow it.
    RequestState state = RequestState::Pending;
    qint64 expiresAt = 0;       // Authoritative expiry from the provider; 0 for none.
    QString replacedBy;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, InputRequest &request, QString &error);
    qint64 bytes() const;
};

enum class Action { AnswerRequest, SendFollowUp };

struct Command {
    QString commandId;
    ConversationKey conversation;
    Action action = Action::SendFollowUp;
    QString requestId;
    int requestRevision = 0;
    QString optionId; // Exactly one of optionId and text answers a request.
    QString text;     // Custom answer, or the follow-up text.
    QString source;   // "desktop", "telegram", …
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Command &command, QString &error);
    // Everything but the source: a repeated command ID must carry exactly this.
    QByteArray canonical() const;
};

enum class Outcome { Pending, Accepted, Rejected, Unknown };
const char *outcomeName(Outcome outcome);
bool outcomeFromName(const QString &name, Outcome &outcome);

struct CommandResult {
    QString commandId;
    Outcome outcome = Outcome::Rejected;
    QString reason; // Normalized: see reason::*.
    QString receipt;
    QJsonObject toJson() const;
};

// Normalized rejection reasons.
namespace reason {
inline const char *invalid = "invalid_command";
inline const char *commandIdReused = "command_id_reused";
inline const char *unknownConversation = "unknown_conversation";
inline const char *disconnected = "disconnected";
inline const char *ended = "conversation_ended";
inline const char *unsupported = "unsupported_action";
inline const char *tooLarge = "content_too_large";
inline const char *unknownRequest = "unknown_request";
inline const char *staleRevision = "stale_revision";
inline const char *invalidOption = "invalid_option";
inline const char *customNotAllowed = "custom_answer_not_allowed";
inline const char *alreadySubmitting = "already_submitting";
inline const char *alreadyAnswered = "already_answered";
inline const char *requestClosed = "request_closed";
inline const char *deliveryUnconfirmed = "delivery_unconfirmed";
inline const char *notReady = "not_ready";
inline const char *awaitingAnswer = "awaiting_answer";
inline const char *timedOut = "delivery_timed_out";
inline const char *capacity = "capacity";
}

struct CommandRecord {
    Command command;
    QByteArray canonical;
    CommandResult result;
    qint64 dispatchedAt = 0;
    qint64 order = 0;
};

// One conversation as the UI renders it. Owned and mutated only by the Service.
struct Conversation {
    ConversationKey key;
    QString title;               // Project or label supplied by the bridge.
    unsigned capabilities = 0;
    enum class Link { Connected, Disconnected, Ended } link = Link::Disconnected;
    bool ready = false;          // Bridge says a new turn may start.
    qint64 watermark = 0;        // Highest message sequence seen.
    bool historyGap = false;     // History could not be replayed after a reconnect.
    bool capacityExceeded = false;
    bool unsupportedContent = false;
    QVector<Message> messages;   // Ordered by sequence.
    QHash<QString, InputRequest> requests;
    QStringList requestOrder;    // Request IDs, oldest first.
    QHash<QString, CommandRecord> commands;
    QString followUpInFlight;    // Command ID, or empty.
    QString followUpUnknown;     // Unconfirmed follow-up command ID until a reconcile snapshot arrives.
    QHash<QString, QString> drafts; // Keyed by request ID; "" is the follow-up composer.
    qint64 lastActivity = 0;
    int connection = -1;         // Owning bridge connection while connected.
    QString generation;

    bool has(unsigned capability) const { return (capabilities & capability) == capability; }
    int pendingCount() const;
    int unreadCount() const;
    bool needsAttention() const;
    bool protectedFromEviction() const;
    qint64 bytes() const;
};

// Number of UTF-8 bytes of a string.
inline int utf8Size(const QString &s) { return s.toUtf8().size(); }

}
