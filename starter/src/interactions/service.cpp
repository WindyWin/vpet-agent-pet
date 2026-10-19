#include "interactions/service.h"
#include <cmath>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace pet::interactions {

namespace {
QString shortReason(const QString &reason) { return reason.left(64); }
bool hasOpenRequest(const Conversation &c) {
    for (const auto &r : c.requests)
        if (r.state == RequestState::Pending || r.state == RequestState::Submitting || r.state == RequestState::Unknown) return true;
    return false;
}
}

Service::Service(Clock clock) : clock_(clock ? std::move(clock) : Clock([] { return QDateTime::currentMSecsSinceEpoch(); })) {}

// ---- Connections ----------------------------------------------------------------------------------

void Service::attach(int connection, Send send) {
    connections_[connection] = Connection{std::move(send), false, {}, {}};
}

void Service::detach(int connection) {
    for (auto &entry : conversations_) {
        auto &c = entry.second;
        if (c.connection == connection) disconnectConversation(c, reason::disconnected);
    }
    connections_.erase(connection);
}

void Service::disconnectConversation(Conversation &c, const QString &why) {
    c.connection = -1;
    c.ready = false;
    if (c.link == Conversation::Link::Connected) c.link = Conversation::Link::Disconnected;
    // A command that was in flight has an unknown outcome: it may have reached the agent.
    for (auto &record : c.commands)
        if (record.result.outcome == Outcome::Pending) {
            applyResult(c, record, Outcome::Unknown, why);
        }
    changed(c);
}

bool Service::sendTo(int connection, const QJsonObject &frame) {
    const auto it = connections_.find(connection);
    if (it == connections_.end() || !it->second.send) return false;
    return it->second.send(QJsonDocument(frame).toJson(QJsonDocument::Compact));
}

void Service::error(int connection, const QString &code, const QString &message, const QJsonObject &extra) {
    QJsonObject frame = extra;
    frame["version"] = protocolVersion;
    frame["type"] = "error";
    frame["code"] = code;
    frame["message"] = message.left(200);
    sendTo(connection, frame);
}

void Service::receive(int connection, const QByteArray &payload) {
    const auto it = connections_.find(connection);
    if (it == connections_.end()) return;
    if (payload.size() > Limits::frameBytes) { error(connection, "frame_too_large", "frame exceeds the limit"); return; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(payload, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) { error(connection, "invalid_frame", "frame is not a JSON object"); return; }
    const auto frame = document.object();
    const auto type = frame.value("type").toString();
    if (type == "hello") { hello(connection, frame); return; }
    if (!it->second.greeted) { error(connection, "no_hello", "send hello first"); return; }
    if (frame.value("version").toInt(-1) != protocolVersion) { error(connection, "unsupported_version", "unsupported protocol version"); return; }
    if (type == "conversation_open") {
        ConversationKey key;
        // A refusal (an ended identity, capacity, a provider mismatch) has already been answered with an error; a resync
        // would make the bridge believe the conversation is registered.
        if (registerConversation(connection, frame, key))
            sendTo(connection, {{"version", protocolVersion}, {"type", "resync"}, {"conversation", key.toJson()}});
    }
    else if (type == "message") onMessage(connection, frame);
    else if (type == "request") onRequest(connection, frame);
    else if (type == "request_state") onRequestState(connection, frame);
    else if (type == "ready") onReady(connection, frame);
    else if (type == "conversation_ended") onEnded(connection, frame);
    else if (type == "command_result") onCommandResult(connection, frame);
    else if (type == "snapshot") onSnapshot(connection, frame);
    else error(connection, "unknown_type", "unknown frame type");
}

void Service::hello(int connection, const QJsonObject &frame) {
    auto &link = connections_[connection];
    if (link.greeted) { error(connection, "duplicate_hello", "already greeted"); return; }
    const auto versions = frame.value("versions").toArray();
    bool supported = frame.value("version").toInt(-1) == protocolVersion;
    for (const auto &v : versions) supported = supported || v.toInt(-1) == protocolVersion;
    if (!supported) {
        error(connection, "unsupported_version", "no common protocol version", {{"supported", QJsonArray{protocolVersion}}});
        return;
    }
    const auto provider = frame.value("provider").toString();
    const auto generation = frame.value("generation").toString();
    if (provider.isEmpty() || provider.toUtf8().size() > 64 || generation.isEmpty() || generation.toUtf8().size() > 128) {
        error(connection, "invalid_hello", "provider and generation are required");
        return;
    }
    link.greeted = true;
    link.provider = provider;
    link.generation = generation;
    QJsonArray resync, refused;
    for (const auto &value : frame.value("conversations").toArray()) {
        ConversationKey key;
        if (registerConversation(connection, value.toObject(), key)) resync.append(key.toJson());
        else if (key.valid()) refused.append(key.toJson());
    }
    QJsonObject welcome{{"version", protocolVersion}, {"type", "welcome"}, {"resync", resync}};
    if (!refused.isEmpty()) welcome["refused"] = refused;
    sendTo(connection, welcome);
}

bool Service::registerConversation(int connection, const QJsonObject &entry, ConversationKey &key) {
    QString err;
    if (!entry.value("conversation").isObject() || !ConversationKey::fromJson(entry.value("conversation").toObject(), key, err)) {
        if (err.isEmpty()) err = "missing conversation";
        key = {};
        error(connection, "invalid_conversation", err);
        return false;
    }
    const auto &link = connections_[connection];
    if (key.provider != link.provider) { error(connection, "invalid_conversation", "provider differs from the handshake"); return false; }
    QStringList names;
    for (const auto &n : entry.value("capabilities").toArray()) names << n.toString();
    const auto capabilities = capabilitiesFromNames(names, err);
    if (!err.isEmpty()) { error(connection, "invalid_conversation", err); return false; }
    // Another epoch of the same session means the identity was reused: the old conversation is over.
    for (auto &other : conversations_)
        if (other.second.key.sameSession(key) && !(other.second.key == key) && other.second.link != Conversation::Link::Ended)
            endConversation(other.second);
    auto found = conversations_.find(key.id());
    if (found == conversations_.end()) {
        while (int(conversations_.size()) >= budget.conversations)
            if (!evictConversation(nullptr)) {
                error(connection, reason::capacity, "too many conversations", {{"conversation", key.toJson()}});
                return false;
            }
        found = conversations_.emplace(key.id(), Conversation{}).first;
        found->second.key = key;
    } else if (found->second.link == Conversation::Link::Ended) {
        error(connection, reason::ended, "conversation has ended", {{"conversation", key.toJson()}});
        return false;
    }
    auto &c = found->second;
    // A reconnect takes the conversation over; the previous connection loses it.
    c.connection = connection;
    c.generation = link.generation;
    c.link = Conversation::Link::Connected;
    c.capabilities = capabilities;
    c.title = entry.value("title").toString().left(128);
    c.ready = entry.value("ready").toBool(false);
    c.lastActivity = now();
    changed(c);
    return true;
}

Conversation *Service::mutableFind(const ConversationKey &key) {
    const auto it = conversations_.find(key.id());
    return it == conversations_.end() ? nullptr : &it->second;
}
const Conversation *Service::find(const ConversationKey &key) const {
    const auto it = conversations_.find(key.id());
    return it == conversations_.end() ? nullptr : &it->second;
}

Conversation *Service::owned(int connection, const QJsonObject &frame, QString &err) {
    ConversationKey key;
    if (!frame.value("conversation").isObject() || !ConversationKey::fromJson(frame.value("conversation").toObject(), key, err)) {
        if (err.isEmpty()) err = "missing conversation";
        error(connection, "invalid_conversation", err);
        return nullptr;
    }
    auto *c = mutableFind(key);
    if (!c || c->connection != connection) {
        err = reason::unknownConversation;
        error(connection, reason::unknownConversation, "conversation is not registered by this connection", {{"conversation", key.toJson()}});
        return nullptr;
    }
    return c;
}

void Service::changed(const Conversation &c) {
    Change change;
    change.key = c.key;
    const auto copy = observers_; // An observer may unsubscribe.
    for (const auto &entry : copy) entry.second(change);
}
void Service::emitResult(const Conversation &c, const CommandResult &result) {
    Change change;
    change.kind = Change::Kind::CommandResult;
    change.key = c.key;
    change.result = result;
    const auto copy = observers_;
    for (const auto &entry : copy) entry.second(change);
}
int Service::subscribe(std::function<void(const Change &)> observer) {
    observers_[nextObserver_] = std::move(observer);
    return nextObserver_++;
}
void Service::unsubscribe(int id) { observers_.erase(id); }

// ---- Store and bounds -----------------------------------------------------------------------------

qint64 Service::contentBytes() const {
    qint64 n = 0;
    for (const auto &entry : conversations_) n += entry.second.bytes();
    return n;
}

QList<ConversationKey> Service::keys() const {
    QList<const Conversation *> list;
    for (const auto &entry : conversations_) list << &entry.second;
    std::stable_sort(list.begin(), list.end(), [](auto *a, auto *b) { return a->lastActivity > b->lastActivity; });
    QList<ConversationKey> keys;
    for (const auto *c : list) keys << c->key;
    return keys;
}

int Service::attentionCount() const {
    int n = 0;
    for (const auto &entry : conversations_) n += entry.second.needsAttention();
    return n;
}

bool Service::evictMessage(bool readOnly) {
    Conversation *best = nullptr;
    int index = -1;
    for (auto &entry : conversations_) {
        auto &c = entry.second;
        int candidate = -1;
        for (int i = 0; i < c.messages.size(); ++i)
            if (!readOnly || c.messages[i].read) { candidate = i; break; }
        if (candidate >= 0 && (!best || c.lastActivity < best->lastActivity)) { best = &c; index = candidate; }
    }
    if (!best) return false;
    best->messages.remove(index);
    return true;
}

bool Service::evictConversation(const Conversation *keep) {
    Conversation *best = nullptr;
    for (auto &entry : conversations_) {
        auto &c = entry.second;
        if (&c == keep || c.link == Conversation::Link::Connected || c.protectedFromEviction()) continue;
        if (!best || c.lastActivity < best->lastActivity) best = &c;
    }
    if (!best) return false;
    conversations_.erase(best->key.id());
    return true;
}

// Evicts oldest read messages, then inactive conversations, then unread messages. Open requests,
// in-flight commands and drafts are never evicted.
bool Service::makeRoom(const Conversation *keep, qint64 incoming) {
    while (contentBytes() + incoming > budget.contentBytes)
        if (!evictMessage(true) && !evictConversation(keep) && !evictMessage(false)) return false;
    return true;
}

void Service::trimMessages(Conversation &c) {
    while (c.messages.size() > budget.messages) {
        int index = 0;
        for (int i = 0; i < c.messages.size(); ++i)
            if (c.messages[i].read) { index = i; break; }
        c.messages.remove(index);
    }
}

void Service::trimRequests(Conversation &c) {
    // Answered cards stay as history, bounded; open requests are never dropped.
    while (c.requestOrder.size() > Limits::pendingRequests * 4) {
        QString victim;
        for (const auto &id : c.requestOrder)
            if (terminal(c.requests.value(id).state)) { victim = id; break; }
        if (victim.isEmpty()) return;
        c.requestOrder.removeOne(victim);
        c.requests.remove(victim);
    }
}

void Service::setDraft(const ConversationKey &key, const QString &requestId, const QString &text) {
    auto *c = mutableFind(key);
    if (!c) return;
    const auto clipped = text.left(Limits::textBytes);
    if (clipped.isEmpty()) c->drafts.remove(requestId);
    else c->drafts[requestId] = clipped;
}
QString Service::draft(const ConversationKey &key, const QString &requestId) const {
    const auto *c = find(key);
    return c ? c->drafts.value(requestId) : QString();
}

void Service::markRead(const ConversationKey &key, const QString &messageId) {
    auto *c = mutableFind(key);
    if (!c) return;
    bool any = false;
    for (auto &m : c->messages)
        if (m.messageId == messageId && !m.read) { m.read = true; any = true; }
    if (any) changed(*c);
}

// ---- Bridge frames --------------------------------------------------------------------------------

bool Service::addMessage(Conversation &c, Message message, bool trackGap) {
    for (const auto &m : c.messages)
        if (m.messageId == message.messageId) return false; // A repeated delivery changes nothing.
    message.read = message.role == Role::User || message.history;
    if (!makeRoom(&c, utf8Size(message.text) + 64)) { c.capacityExceeded = true; return false; }
    if (trackGap && c.watermark > 0 && message.sequence > c.watermark + 1) c.historyGap = true;
    int at = c.messages.size();
    while (at > 0 && c.messages[at - 1].sequence > message.sequence) --at;
    c.watermark = std::max(c.watermark, message.sequence);
    c.messages.insert(at, message);
    trimMessages(c);
    c.lastActivity = now();
    return true;
}

void Service::onMessage(int connection, const QJsonObject &frame) {
    QString err;
    auto *c = owned(connection, frame, err);
    if (!c) return;
    Message message;
    if (!frame.value("message").isObject() || !Message::fromJson(frame.value("message").toObject(), message, err)) {
        error(connection, err == reason::tooLarge ? reason::tooLarge : "invalid_message", err.isEmpty() ? "missing message" : err);
        return;
    }
    if (!c->has(Messages)) { error(connection, reason::unsupported, "conversation did not advertise messages"); return; }
    if (addMessage(*c, message, true)) changed(*c);
    else if (c->capacityExceeded) {
        error(connection, reason::capacity, "message could not be stored", {{"message_id", message.messageId}});
        changed(*c);
    }
}

void Service::settleRequest(Conversation &c, InputRequest &request, RequestState state, const QString &replacedBy) {
    request.state = state;
    if (state == RequestState::Replaced) request.replacedBy = replacedBy;
    c.lastActivity = now();
}

bool Service::storeRequest(Conversation &c, InputRequest request, bool fromSnapshot) {
    auto it = c.requests.find(request.requestId);
    if (it != c.requests.end()) {
        auto &existing = *it;
        if (request.revision < existing.revision) return false;
        if (terminal(existing.state)) return false; // An ended question never reopens.
        if (request.revision == existing.revision) {
            if (terminal(request.state)) { settleRequest(c, existing, request.state, request.replacedBy); return true; }
            // The same revision restated. A snapshot's "pending" resolves an unconfirmed delivery;
            // a submitting request stays reserved for its command.
            if (fromSnapshot && existing.state == RequestState::Unknown) { existing.state = RequestState::Pending; return true; }
            return false;
        }
        // A newer revision replaces the question wholesale; an answer in flight for the old one no longer matches.
        const auto delta = request.bytes() - existing.bytes();
        if (delta > 0 && !makeRoom(&c, delta)) { c.capacityExceeded = true; return false; }
        existing = request;
        c.lastActivity = now();
        return true;
    }
    if (!terminal(request.state)) {
        int open = 0;
        for (const auto &r : c.requests) open += !terminal(r.state);
        if (open >= budget.pendingRequests) { c.capacityExceeded = true; return false; }
    }
    if (!makeRoom(&c, request.bytes())) { c.capacityExceeded = true; return false; }
    c.requestOrder << request.requestId;
    c.requests.insert(request.requestId, request);
    trimRequests(c);
    c.lastActivity = now();
    return true;
}

void Service::onRequest(int connection, const QJsonObject &frame) {
    QString err;
    auto *c = owned(connection, frame, err);
    if (!c) return;
    InputRequest request;
    const auto requestId = frame.value("request").toObject().value("request_id").toString();
    if (!frame.value("request").isObject() || !InputRequest::fromJson(frame.value("request").toObject(), request, err)) {
        // A request is never truncated into a different question: it is refused whole.
        const bool large = err == reason::tooLarge;
        if (large) { c->unsupportedContent = true; changed(*c); }
        error(connection, large ? "unsupported_content" : "invalid_request", err.isEmpty() ? "missing request" : err, {{"request_id", requestId}});
        return;
    }
    if (storeRequest(*c, request, false)) changed(*c);
    else if (c->capacityExceeded && !c->requests.contains(request.requestId)) {
        error(connection, reason::capacity, "request could not be stored", {{"request_id", request.requestId}});
        changed(*c);
    }
}

void Service::onRequestState(int connection, const QJsonObject &frame) {
    QString err;
    auto *c = owned(connection, frame, err);
    if (!c) return;
    RequestState state;
    const auto id = frame.value("request_id").toString();
    if (!requestStateFromName(frame.value("state").toString(), state) || !terminal(state) || id.isEmpty()) {
        error(connection, "invalid_request_state", "state must be answered, cancelled, expired or replaced");
        return;
    }
    auto it = c->requests.find(id);
    if (it == c->requests.end() || terminal(it->state)) return;
    settleRequest(*c, *it, state, frame.value("replaced_by").toString());
    changed(*c);
}

void Service::onReady(int connection, const QJsonObject &frame) {
    QString err;
    auto *c = owned(connection, frame, err);
    if (!c) return;
    c->ready = frame.value("ready").toBool(false);
    c->lastActivity = now();
    changed(*c);
}

void Service::endConversation(Conversation &c) {
    c.link = Conversation::Link::Ended;
    c.ready = false;
    c.connection = -1;
    for (auto &r : c.requests)
        if (r.state == RequestState::Pending) r.state = RequestState::Cancelled;
    for (auto &record : c.commands)
        if (record.result.outcome == Outcome::Pending) applyResult(c, record, Outcome::Unknown, reason::ended);
    for (auto &r : c.requests)
        if (r.state == RequestState::Submitting || r.state == RequestState::Unknown) r.state = RequestState::Cancelled;
    changed(c);
}

bool Service::release(const ConversationKey &key) {
    auto *c = mutableFind(key);
    if (!c || c->link == Conversation::Link::Ended) return false;
    if (c->connection >= 0) sendTo(c->connection, {{"version", protocolVersion}, {"type", "release"}, {"conversation", key.toJson()}});
    endConversation(*c);
    return true;
}

void Service::onEnded(int connection, const QJsonObject &frame) {
    QString err;
    auto *c = owned(connection, frame, err);
    if (!c) return;
    endConversation(*c);
}

void Service::applyResult(Conversation &c, CommandRecord &record, Outcome outcome, const QString &why,
                          const QString &receipt, bool stillAnswerable) {
    record.result = CommandResult{record.command.commandId, outcome, shortReason(why), receipt.left(128)};
    const auto &command = record.command;
    const bool answer = command.action == Action::AnswerRequest;
    auto request = c.requests.find(command.requestId);
    const bool matching = answer && request != c.requests.end() && request->revision == command.requestRevision;
    const bool open = matching && (request->state == RequestState::Submitting || request->state == RequestState::Unknown);
    switch (outcome) {
    case Outcome::Accepted:
        if (matching && !terminal(request->state)) settleRequest(c, *request, RequestState::Answered);
        if (!answer) {
            if (c.followUpInFlight == command.commandId) c.followUpInFlight.clear();
            if (c.followUpUnknown == command.commandId) c.followUpUnknown.clear();
            c.ready = false; // The agent is working until its bridge says otherwise.
        }
        break;
    case Outcome::Rejected:
        if (open) {
            if (stillAnswerable) request->state = RequestState::Pending;
            else settleRequest(c, *request, why == reason::alreadyAnswered ? RequestState::Answered : RequestState::Cancelled);
        }
        if (!answer) {
            if (c.followUpInFlight == command.commandId) { c.followUpInFlight.clear(); c.ready = why != reason::notReady; }
            if (c.followUpUnknown == command.commandId) c.followUpUnknown.clear();
        }
        break;
    case Outcome::Unknown:
        if (open && request->state == RequestState::Submitting) request->state = RequestState::Unknown;
        if (!answer && c.followUpInFlight == command.commandId) { c.followUpInFlight.clear(); c.followUpUnknown = command.commandId; }
        break;
    case Outcome::Pending: break;
    }
    c.lastActivity = now();
    emitResult(c, record.result);
}

void Service::onCommandResult(int connection, const QJsonObject &frame) {
    const auto id = frame.value("command_id").toString();
    Outcome outcome;
    if (!outcomeFromName(frame.value("outcome").toString(), outcome) || outcome == Outcome::Pending || id.isEmpty()) {
        error(connection, "invalid_result", "outcome must be accepted, rejected or unknown");
        return;
    }
    for (auto &entry : conversations_) {
        auto &c = entry.second;
        if (c.connection != connection) continue;
        auto it = c.commands.find(id);
        if (it == c.commands.end()) continue;
        const auto current = it->result.outcome;
        // A definitive outcome is final; an unknown one may still be settled by a late result.
        if (current == Outcome::Accepted || current == Outcome::Rejected) return;
        if (current == Outcome::Unknown && outcome == Outcome::Unknown) return;
        applyResult(c, *it, outcome, frame.value("reason").toString(), frame.value("receipt").toString(),
                    frame.value("still_answerable").toBool(true));
        changed(c);
        return;
    }
}

void Service::onSnapshot(int connection, const QJsonObject &frame) {
    QString err;
    auto *c = owned(connection, frame, err);
    if (!c) return;
    qint64 watermark = 0, first = 0;
    const auto number = [&](const char *name, qint64 &out) {
        const auto v = frame.value(name);
        if (v.isUndefined() || v.isNull()) return true;
        if (!v.isDouble() || !(v.toDouble() >= -9007199254740992.0 && v.toDouble() <= 9007199254740992.0 && v.toDouble() == std::trunc(v.toDouble()))) return false;
        out = qint64(v.toDouble());
        return true;
    };
    if (!number("watermark", watermark) || !number("first_sequence", first)) { error(connection, "invalid_snapshot", "bad sequence"); return; }
    // Validate everything before applying anything: a bad snapshot changes nothing.
    QVector<Message> messages;
    QVector<InputRequest> requests;
    for (const auto &v : frame.value("messages").toArray()) {
        Message m;
        if (!v.isObject() || !Message::fromJson(v.toObject(), m, err)) { error(connection, "invalid_snapshot", err); return; }
        messages << m;
    }
    for (const auto &v : frame.value("requests").toArray()) {
        InputRequest r;
        if (!v.isObject() || !InputRequest::fromJson(v.toObject(), r, err)) {
            if (err == reason::tooLarge) { c->unsupportedContent = true; continue; }
            error(connection, "invalid_snapshot", err);
            return;
        }
        requests << r;
    }
    if (frame.contains("capabilities")) {
        QStringList names;
        for (const auto &n : frame.value("capabilities").toArray()) names << n.toString();
        const auto capabilities = capabilitiesFromNames(names, err);
        if (!err.isEmpty()) { error(connection, "invalid_snapshot", err); return; }
        c->capabilities = capabilities;
    }
    if (first > 0 && c->watermark > 0 && first > c->watermark + 1) c->historyGap = true;
    std::sort(messages.begin(), messages.end(), [](const Message &a, const Message &b) { return a.sequence < b.sequence; });
    for (const auto &m : messages) addMessage(*c, m, false);
    c->watermark = std::max({c->watermark, watermark});

    QStringList listed;
    for (const auto &r : requests) {
        listed << r.requestId;
        storeRequest(*c, r, true);
    }
    // A provider that no longer lists a pending question has closed it.
    for (auto it = c->requests.begin(); it != c->requests.end(); ++it)
        if (!listed.contains(it.key())) {
            if (it->state == RequestState::Pending) settleRequest(*c, *it, RequestState::Expired);
            else if (it->state == RequestState::Unknown) settleRequest(*c, *it, RequestState::Expired); // Command stays unknown.
        }

    for (const auto &v : frame.value("command_outcomes").toArray()) {
        const auto o = v.toObject();
        Outcome outcome;
        auto record = c->commands.find(o.value("command_id").toString());
        if (record == c->commands.end() || !outcomeFromName(o.value("outcome").toString(), outcome)) continue;
        if (outcome == Outcome::Pending || outcome == Outcome::Unknown) continue;
        const auto current = record->result.outcome;
        if (current != Outcome::Pending && current != Outcome::Unknown) continue;
        applyResult(*c, *record, outcome, o.value("reason").toString(), o.value("receipt").toString(), o.value("still_answerable").toBool(true));
    }
    // A reconcile pass happened: an unconfirmed follow-up no longer blocks the composer.
    c->followUpUnknown.clear();
    if (frame.contains("ready")) c->ready = frame.value("ready").toBool() && c->followUpInFlight.isEmpty();
    c->lastActivity = now();
    changed(*c);
}

// ---- Commands -------------------------------------------------------------------------------------

CommandResult Service::submit(const Command &command) {
    const auto reject = [&](const char *why) { return CommandResult{command.commandId, Outcome::Rejected, why, {}}; };
    if (command.commandId.isEmpty() || !command.conversation.valid()) return reject(reason::invalid);
    auto *c = mutableFind(command.conversation);
    if (!c) return reject(reason::unknownConversation);
    const auto canonical = command.canonical();
    const auto known = c->commands.constFind(command.commandId);
    if (known != c->commands.constEnd())
        return known->canonical == canonical ? known->result : reject(reason::commandIdReused);
    if (c->link == Conversation::Link::Ended) return reject(reason::ended);
    if (c->link == Conversation::Link::Disconnected || c->connection < 0) return reject(reason::disconnected);

    const bool answer = command.action == Action::AnswerRequest;
    if (utf8Size(command.text) > Limits::textBytes) return reject(reason::tooLarge);
    InputRequest *request = nullptr;
    if (answer) {
        if (command.optionId.isEmpty() == command.text.isEmpty()) return reject(reason::invalid);
        if (!command.optionId.isEmpty() && !c->has(SingleChoice)) return reject(reason::unsupported);
        if (!command.text.isEmpty() && !c->has(CustomAnswer)) return reject(reason::unsupported);
        auto it = c->requests.find(command.requestId);
        if (it == c->requests.end()) return reject(reason::unknownRequest);
        request = &*it;
        if (request->revision != command.requestRevision) return reject(reason::staleRevision);
        if (request->state == RequestState::Pending && request->expiresAt && now() >= request->expiresAt) {
            settleRequest(*c, *request, RequestState::Expired);
            changed(*c);
        }
        switch (request->state) {
        case RequestState::Pending: break;
        case RequestState::Submitting: return reject(reason::alreadySubmitting);
        case RequestState::Answered: return reject(reason::alreadyAnswered);
        case RequestState::Unknown: return reject(reason::deliveryUnconfirmed);
        default: return reject(reason::requestClosed);
        }
        if (!command.optionId.isEmpty()) {
            const bool listed = std::any_of(request->options.begin(), request->options.end(),
                                            [&](const Option &o) { return o.id == command.optionId; });
            if (!listed) return reject(reason::invalidOption);
        } else if (!request->customAllowed) return reject(reason::customNotAllowed);
    } else {
        if (!command.optionId.isEmpty() || command.text.trimmed().isEmpty()) return reject(reason::invalid);
        if (!c->has(FollowUp)) return reject(reason::unsupported);
        if (hasOpenRequest(*c)) return reject(reason::awaitingAnswer);
        if (!c->followUpInFlight.isEmpty()) return reject(reason::alreadySubmitting);
        if (!c->followUpUnknown.isEmpty()) return reject(reason::deliveryUnconfirmed);
        if (!c->ready) return reject(reason::notReady);
    }

    if (int(c->commands.size()) >= budget.commands) {
        // Forget the oldest settled command; one still in flight or unconfirmed is never forgotten.
        QString victim;
        qint64 oldest = 0;
        for (auto it = c->commands.begin(); it != c->commands.end(); ++it) {
            const auto o = it->result.outcome;
            if ((o == Outcome::Accepted || o == Outcome::Rejected) && (victim.isEmpty() || it->order < oldest)) { victim = it.key(); oldest = it->order; }
        }
        if (victim.isEmpty()) return reject(reason::capacity);
        c->commands.remove(victim);
    }

    CommandRecord record;
    record.command = command;
    record.canonical = canonical;
    record.result = CommandResult{command.commandId, Outcome::Pending, {}, {}};
    record.dispatchedAt = now();
    record.order = ++order_;
    const bool wasReady = c->ready;
    if (answer) request->state = RequestState::Submitting;
    else { c->followUpInFlight = command.commandId; c->ready = false; }
    if (!sendTo(c->connection, command.toJson())) {
        // Nothing was written, so nothing can have been delivered.
        if (answer) request->state = RequestState::Pending;
        else { c->followUpInFlight.clear(); c->ready = wasReady && c->connection >= 0; }
        return reject(reason::disconnected);
    }
    c->commands.insert(command.commandId, record);
    c->lastActivity = now();
    changed(*c);
    return record.result;
}

void Service::expire() {
    const auto time = now();
    for (auto &entry : conversations_) {
        auto &c = entry.second;
        bool touched = false;
        for (auto &r : c.requests)
            if (r.state == RequestState::Pending && r.expiresAt && time >= r.expiresAt) { settleRequest(c, r, RequestState::Expired); touched = true; }
        for (auto &record : c.commands)
            if (record.result.outcome == Outcome::Pending && time - record.dispatchedAt >= commandTimeoutMs) {
                applyResult(c, record, Outcome::Unknown, reason::timedOut);
                touched = true;
            }
        if (touched) changed(c);
    }
}

}
