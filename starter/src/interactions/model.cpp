#include "interactions/model.h"
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace pet::interactions {

namespace {
struct CapabilityName { const char *name; Capability value; };
const CapabilityName capabilityTable[] = {
    {"messages", Messages}, {"single_choice", SingleChoice}, {"custom_answer", CustomAnswer},
    {"follow_up", FollowUp}, {"reconcile", Reconcile}, {"idempotent_commands", IdempotentCommands},
};

bool str(const QJsonObject &o, const char *name, QString &out, QString &error, bool required = true, int maxBytes = 512) {
    const auto v = o.value(name);
    if (v.isUndefined() || v.isNull()) {
        if (required) error = QString("missing %1").arg(name);
        return !required;
    }
    if (!v.isString()) { error = QString("%1 must be a string").arg(name); return false; }
    out = v.toString();
    if (utf8Size(out) > maxBytes) { error = QString("%1 too long").arg(name); return false; }
    if (required && out.isEmpty()) { error = QString("%1 is empty").arg(name); return false; }
    return true;
}
// Every integer up to 2^53 is exact in a double; checking first keeps the cast defined for hostile input.
bool integral(double d) { return d >= -9007199254740992.0 && d <= 9007199254740992.0 && d == std::trunc(d); }
bool whole(const QJsonObject &o, const char *name, qint64 &out, QString &error, bool required = true) {
    const auto v = o.value(name);
    if (v.isUndefined() || v.isNull()) {
        if (required) error = QString("missing %1").arg(name);
        return !required;
    }
    if (!v.isDouble() || !integral(v.toDouble())) { error = QString("%1 must be an integer").arg(name); return false; }
    out = qint64(v.toDouble());
    return true;
}
}

unsigned capabilitiesFromNames(const QStringList &names, QString &error) {
    unsigned bits = 0;
    for (const auto &name : names) {
        bool found = false;
        for (const auto &entry : capabilityTable)
            if (name == entry.name) { bits |= entry.value; found = true; }
        if (!found) { error = "unknown capability " + name.left(40); return 0; }
    }
    return bits;
}
QStringList capabilityNames(unsigned capabilities) {
    QStringList names;
    for (const auto &entry : capabilityTable)
        if (capabilities & entry.value) names << entry.name;
    return names;
}

QJsonObject ConversationKey::toJson() const {
    return {{"provider", provider}, {"session_id", sessionId}, {"conversation_epoch", epoch}};
}
bool ConversationKey::fromJson(const QJsonObject &o, ConversationKey &key, QString &error) {
    return str(o, "provider", key.provider, error, true, 64) && str(o, "session_id", key.sessionId, error, true, 256)
        && str(o, "conversation_epoch", key.epoch, error, true, 128);
}

QJsonObject Message::toJson() const {
    return {{"message_id", messageId}, {"sequence", double(sequence)}, {"role", role == Role::Agent ? "agent" : "user"},
            {"text", text}, {"created_at", double(createdAt)}, {"history", history}};
}
bool Message::fromJson(const QJsonObject &o, Message &m, QString &error) {
    QString role;
    if (!str(o, "message_id", m.messageId, error, true, 256) || !whole(o, "sequence", m.sequence, error)) return false;
    if (m.sequence <= 0) { error = "sequence must be positive"; return false; }
    if (!str(o, "role", role, error)) return false;
    if (role == "agent") m.role = Role::Agent;
    else if (role == "user") m.role = Role::User;
    else { error = "unknown role"; return false; }
    // A message may legitimately be empty (a tool-only turn), but never larger than the limit.
    const auto v = o.value("text");
    if (!v.isString()) { error = "text must be a string"; return false; }
    m.text = v.toString();
    if (utf8Size(m.text) > Limits::textBytes) { error = QString(reason::tooLarge); return false; }
    const auto history = o.value("history");
    if (!history.isUndefined() && !history.isBool()) { error = "history must be a boolean"; return false; }
    m.history = history.toBool(false);
    return whole(o, "created_at", m.createdAt, error, false);
}

const char *requestStateName(RequestState state) {
    switch (state) {
    case RequestState::Pending: return "pending";
    case RequestState::Submitting: return "submitting";
    case RequestState::Answered: return "answered";
    case RequestState::Cancelled: return "cancelled";
    case RequestState::Expired: return "expired";
    case RequestState::Replaced: return "replaced";
    case RequestState::Unknown: return "unknown";
    }
    return "?";
}
bool requestStateFromName(const QString &name, RequestState &state) {
    for (auto s : {RequestState::Pending, RequestState::Submitting, RequestState::Answered, RequestState::Cancelled,
                   RequestState::Expired, RequestState::Replaced, RequestState::Unknown})
        if (name == requestStateName(s)) { state = s; return true; }
    return false;
}

QJsonObject InputRequest::toJson() const {
    QJsonArray list;
    for (const auto &o : options) list.append(QJsonObject{{"id", o.id}, {"label", o.label}});
    QJsonObject o{{"request_id", requestId}, {"revision", revision}, {"question", question}, {"options", list},
                  {"custom_answer", customAllowed}, {"state", requestStateName(state)}};
    if (expiresAt) o["expires_at"] = double(expiresAt);
    if (!replacedBy.isEmpty()) o["replaced_by"] = replacedBy;
    return o;
}
bool InputRequest::fromJson(const QJsonObject &o, InputRequest &r, QString &error) {
    qint64 revision = 0;
    if (!str(o, "request_id", r.requestId, error, true, 256) || !whole(o, "revision", revision, error)) return false;
    if (revision <= 0 || revision > 1000000) { error = "invalid revision"; return false; }
    r.revision = int(revision);
    const auto q = o.value("question");
    if (!q.isString() || q.toString().isEmpty()) { error = "question is required"; return false; }
    r.question = q.toString();
    if (utf8Size(r.question) > Limits::textBytes) { error = reason::tooLarge; return false; }
    const auto list = o.value("options");
    if (!list.isUndefined() && !list.isArray()) { error = "options must be an array"; return false; }
    if (list.toArray().size() > Limits::options) { error = reason::tooLarge; return false; }
    QStringList seen;
    r.options.clear();
    for (const auto &item : list.toArray()) {
        if (!item.isObject()) { error = "option must be an object"; return false; }
        Option option;
        if (!str(item.toObject(), "id", option.id, error, true, 256) || !str(item.toObject(), "label", option.label, error, true, 4096)) return false;
        if (option.label.size() > Limits::labelChars) { error = reason::tooLarge; return false; }
        if (seen.contains(option.id)) { error = "duplicate option id"; return false; }
        seen << option.id;
        r.options.append(option);
    }
    const auto custom = o.value("custom_answer");
    if (!custom.isUndefined() && !custom.isBool()) { error = "custom_answer must be a boolean"; return false; }
    r.customAllowed = custom.toBool(false);
    if (r.options.isEmpty() && !r.customAllowed) { error = "a request needs options or a custom answer"; return false; }
    QString state = o.value("state").toString("pending");
    if (!requestStateFromName(state, r.state) || r.state == RequestState::Submitting || r.state == RequestState::Unknown) {
        error = "invalid request state"; return false;
    }
    qint64 expires = 0;
    if (!whole(o, "expires_at", expires, error, false)) return false;
    r.expiresAt = expires;
    return str(o, "replaced_by", r.replacedBy, error, false, 256);
}
qint64 InputRequest::bytes() const {
    qint64 n = utf8Size(question) + utf8Size(requestId) + 64;
    for (const auto &o : options) n += utf8Size(o.id) + utf8Size(o.label);
    return n;
}

QJsonObject Command::toJson() const {
    QJsonObject payload;
    if (!optionId.isEmpty()) payload["option_id"] = optionId;
    if (!text.isEmpty() || optionId.isEmpty()) payload["text"] = text;
    QJsonObject o{{"version", protocolVersion}, {"type", "command"}, {"command_id", commandId},
                  {"conversation", conversation.toJson()},
                  {"action", action == Action::AnswerRequest ? "answer_request" : "send_follow_up"},
                  {"payload", payload}, {"source", source}};
    if (action == Action::AnswerRequest) { o["request_id"] = requestId; o["request_revision"] = requestRevision; }
    return o;
}
bool Command::fromJson(const QJsonObject &o, Command &c, QString &error) {
    QString action;
    if (!str(o, "command_id", c.commandId, error, true, 128) || !str(o, "action", action, error)
        || !str(o, "source", c.source, error, false, 32)) return false;
    if (!o.value("conversation").isObject() || !ConversationKey::fromJson(o.value("conversation").toObject(), c.conversation, error)) {
        if (error.isEmpty()) error = "missing conversation";
        return false;
    }
    const auto payload = o.value("payload");
    if (!payload.isObject()) { error = "missing payload"; return false; }
    const auto body = payload.toObject();
    if (!str(body, "option_id", c.optionId, error, false, 256) || !str(body, "text", c.text, error, false, Limits::textBytes)) return false;
    if (action == "answer_request") {
        c.action = Action::AnswerRequest;
        qint64 revision = 0;
        if (!str(o, "request_id", c.requestId, error, true, 256) || !whole(o, "request_revision", revision, error)) return false;
        c.requestRevision = int(revision);
        if (c.optionId.isEmpty() == c.text.isEmpty()) { error = "an answer carries exactly one of option_id and text"; return false; }
    } else if (action == "send_follow_up") {
        c.action = Action::SendFollowUp;
        if (!c.optionId.isEmpty() || c.text.trimmed().isEmpty()) { error = "a follow-up carries only non-empty text"; return false; }
    } else { error = "unknown action"; return false; }
    return true;
}
QByteArray Command::canonical() const {
    QJsonObject o = toJson();
    o.remove("source");
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

const char *outcomeName(Outcome outcome) {
    switch (outcome) {
    case Outcome::Pending: return "pending";
    case Outcome::Accepted: return "accepted";
    case Outcome::Rejected: return "rejected";
    case Outcome::Unknown: return "unknown";
    }
    return "?";
}
bool outcomeFromName(const QString &name, Outcome &outcome) {
    for (auto o : {Outcome::Pending, Outcome::Accepted, Outcome::Rejected, Outcome::Unknown})
        if (name == outcomeName(o)) { outcome = o; return true; }
    return false;
}
QJsonObject CommandResult::toJson() const {
    QJsonObject o{{"version", protocolVersion}, {"type", "command_result"}, {"command_id", commandId}, {"outcome", outcomeName(outcome)}};
    if (!reason.isEmpty()) o["reason"] = reason;
    if (!receipt.isEmpty()) o["receipt"] = receipt;
    return o;
}

int Conversation::pendingCount() const {
    int n = 0;
    for (const auto &r : requests) n += r.state == RequestState::Pending;
    return n;
}
int Conversation::unreadCount() const {
    int n = 0;
    for (const auto &m : messages) n += !m.read && m.role == Role::Agent;
    return n;
}
bool Conversation::needsAttention() const {
    if (link == Link::Ended) return false;
    for (const auto &r : requests)
        if (r.state == RequestState::Pending || r.state == RequestState::Unknown) return true;
    return false;
}
bool Conversation::protectedFromEviction() const {
    if (!followUpInFlight.isEmpty()) return true;
    for (const auto &r : requests)
        if (!terminal(r.state)) return true;
    for (auto it = drafts.begin(); it != drafts.end(); ++it)
        if (!it.value().isEmpty()) return true;
    return false;
}
qint64 Conversation::bytes() const {
    qint64 n = 0;
    for (const auto &m : messages) n += utf8Size(m.text) + utf8Size(m.messageId) + 32;
    for (const auto &r : requests) n += r.bytes();
    for (auto it = drafts.begin(); it != drafts.end(); ++it) n += utf8Size(it.value());
    for (const auto &c : commands) n += c.canonical.size();
    return n;
}

}
