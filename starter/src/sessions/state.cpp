#include "state.h"
#include <QJsonDocument>
#include <QDataStream>
#include <cmath>
#include <algorithm>

namespace pet {
// A versioned private checkpoint preserves in-flight tools, dismissed alerts and event deduplication.
QByteArray Sessions::checkpoint() const {
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_6_5);
    out << quint32(1) << quint32(sessions_.size());
    for (const auto &s : sessions_) {
        out << s.provider << s.id << s.parent << s.project << s.state << s.resume << s.reason
            << s.attentionTools << s.interrupted << s.host.adapter << s.host.pids
            << s.host.window.backend << s.host.window.id << s.host.target << s.tools
            << s.timestamp << s.seen << s.reactionUntil << s.activityUntil << s.turnStarted << s.lastTurnMs;
    }
    out << ended_ << events_ << serial_ << quint32(alerts_.size());
    for (const auto &a : alerts_)
        out << a.session << a.kind << a.project << a.provider << a.id << a.reason << a.created << a.count << a.serial << a.expires;
    return data;
}
bool Sessions::restore(const QByteArray &data, qint64 now, const std::function<bool(const Session &)> &running) {
    if (data.size() > 8 * 1024 * 1024 || !running) return false;
    QDataStream in(data); in.setVersion(QDataStream::Qt_6_5);
    quint32 version = 0, count = 0; in >> version >> count;
    if (version != 1 || count > maxSessions) return false;
    Sessions restored;
    for (quint32 i = 0; i < count; ++i) {
        Session s;
        in >> s.provider >> s.id >> s.parent >> s.project >> s.state >> s.resume >> s.reason
           >> s.attentionTools >> s.interrupted >> s.host.adapter >> s.host.pids
           >> s.host.window.backend >> s.host.window.id >> s.host.target >> s.tools
           >> s.timestamp >> s.seen >> s.reactionUntil >> s.activityUntil >> s.turnStarted >> s.lastTurnMs;
        if (in.status() != QDataStream::Ok || s.tools.size() > maxTools || s.attentionTools.size() > maxTools) return false;
        if (s.seen <= now && now - s.seen < expiryMs && running(s))
            restored.sessions_.insert(s.provider + QChar(0x1f) + s.id, s);
    }
    in >> restored.ended_ >> restored.events_ >> restored.serial_ >> count;
    if (count > maxAlerts || restored.ended_.size() > maxSessions || restored.events_.size() > maxEvents) return false;
    for (quint32 i = 0; i < count; ++i) {
        Alert a;
        in >> a.session >> a.kind >> a.project >> a.provider >> a.id >> a.reason >> a.created >> a.count >> a.serial >> a.expires;
        if (restored.sessions_.contains(a.session)) restored.alerts_.append(a);
    }
    if (in.status() != QDataStream::Ok || !in.atEnd()) return false;
    restored.expire(now);
    *this = std::move(restored);
    return true;
}
bool Event::parse(const QByteArray &data, Event &e, QString &error, const hosts::Registry &hosts) {
    if (data.size() > 8192) { error = "Event exceeds 8192 bytes"; return false; }
    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { error = "Expected a JSON object"; return false; }
    const auto o = doc.object();
    const QSet<QString> fields{"version", "provider", "session_id", "event_id", "kind", "timestamp_ms", "tool_id", "parent_id", "project_path", "activity", "reason",
                              "host", "host_pids", "host_window", "host_target", "risky", "waiting"};
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!fields.contains(it.key())) { error = "Unknown event field"; return false; }
        if (it.key() == "waiting") {
            if (!it.value().isBool() || !it.value().toBool()) { error = "Invalid waiting flag"; return false; }
            continue;
        }
        if (it.key() == "risky") {
            if (!it.value().isBool()) { error = "Invalid risky flag"; return false; }
            continue;
        }
        if (it.key() != "version" && it.key() != "timestamp_ms" &&
            (!it.value().isString() || it.value().toString().size() > (it.key() == "project_path" ? 2048 : 256))) {
            error = "Invalid string field"; return false;
        }
        if (it.value().isString()) for (const auto ch : it.value().toString())
            if (ch.category() == QChar::Other_Control) { error = "Control character in field"; return false; }
    }
    const double stamp = o.value("timestamp_ms").toDouble(-1);
    if (o.value("version").toDouble() != 1 || stamp < 1 || stamp > 9007199254740991.0 || std::floor(stamp) != stamp) {
        error = "Unsupported version or timestamp"; return false;
    }
    e = {o.value("provider").toString(), o.value("session_id").toString(), o.value("event_id").toString(),
         o.value("kind").toString(), o.value("tool_id").toString(), o.value("parent_id").toString(),
         o.value("project_path").toString(), o.value("activity").toString(), static_cast<qint64>(stamp),
         o.value("reason").toString(), o.value("host").toString(), o.value("host_pids").toString(),
         o.value("host_window").toString(), o.value("host_target").toString(), o.value("risky").toBool(), o.value("waiting").toBool()};
    const QSet<QString> kinds{"session_start", "prompt", "tool_start", "tool_end", "attention", "error", "turn_finished", "turn_failed",
                              "interrupt", "session_end"};
    const bool reasonValid = e.reason.isEmpty() ||
        (e.kind == "attention" && (e.reason == "approval" || e.reason == "input")) ||
        (e.kind == "turn_failed" && (e.reason == "limit" || e.reason == "billing"));
    if ((e.provider != "claude" && e.provider != "codex") || e.session.isEmpty() || e.id.isEmpty() || !kinds.contains(e.kind) ||
        ((e.kind == "tool_start" || e.kind == "tool_end") && e.tool.isEmpty()) ||
        (!e.activity.isEmpty() && e.activity != "reading" && e.activity != "working") ||
        !reasonValid || (e.kind == "turn_failed" && !e.tool.isEmpty()) ||
        (o.contains("risky") && e.kind != "tool_start") ||
        (o.contains("waiting") && e.kind != "turn_finished")) {
        error = "Missing identity or unsupported event kind/activity"; return false;
    }
    if (!hosts.validV1(e.host, e.hostPids, e.hostWindow)) { error = "Invalid host identification"; return false; }
    return true;
}
static QString key(const QString &provider, const QString &id) { return provider + QChar(0x1f) + id; }
static QString toolState(const Session &s) {
    return s.tools.values().contains("working") ? "working" : s.tools.isEmpty() ? "thinking" : "reading";
}
template<class T> static void trimOldest(QMap<QString, T> &map, int limit) {
    while (map.size() > limit) {
        auto oldest = map.begin();
        for (auto it = map.begin(); it != map.end(); ++it) if (it.value() < oldest.value()) oldest = it;
        map.erase(oldest);
    }
}
bool Sessions::apply(const Event &e, qint64 now) {
    expire(now);
    const auto k = key(e.provider, e.session), eventKey = k + QChar(0x1f) + e.id;
    if (e.timestamp > now + 60000 || e.timestamp < now - expiryMs || events_.contains(eventKey)) return false;
    if (ended_.contains(k) && e.timestamp <= ended_.value(k)) return false;
    if (sessions_.contains(k)) {
        const auto &existing = sessions_[k];
        if (e.timestamp < existing.timestamp) return false;
        if (e.timestamp == existing.timestamp && existing.state == "turn-finished" &&
            (e.kind == "tool_start" || e.kind == "tool_end")) return false;
        // Late callbacks of an interrupted turn must not revive it; only a new prompt does.
        if (existing.interrupted && (e.kind == "tool_start" || e.kind == "tool_end" || e.kind == "error")) return false;
    }
    events_[eventKey] = now;
    trimOldest(events_, maxEvents);
    if (e.kind == "session_end") {
        sessions_.remove(k); ended_[k] = e.timestamp; trimOldest(ended_, maxSessions);
        alerts_.erase(std::remove_if(alerts_.begin(), alerts_.end(), [&](const Alert &a) { return a.session == k; }), alerts_.end());
        return true;
    }
    if (!sessions_.contains(k) && sessions_.size() >= maxSessions) return false;
    const bool isNew = !sessions_.contains(k);
    auto &s = sessions_[k];
    if (e.kind == "tool_start" && !s.tools.contains(e.tool) && s.tools.size() >= maxTools) return false;
    s.provider = e.provider; s.id = e.session; s.timestamp = e.timestamp; s.seen = now;
    if (!e.parent.isEmpty()) s.parent = e.parent;
    if (!e.project.isEmpty()) s.project = e.project;
    if (!e.host.isEmpty()) s.host = hosts::fromV1(e.host, e.hostPids, e.hostWindow, e.hostTarget);
    const bool ignored = (e.kind == "attention" && (s.state == "exhausted" ||
                         (s.state == "waiting" && e.reason == "input"))) ||
                         (e.kind == "error" && s.state == "waiting" && !s.tools.contains(e.tool));
    const bool held = s.activityUntil != 0; // Showing a finished tool's activity.
    if (e.kind != "session_start" && e.kind != "tool_end") s.activityUntil = 0;
    if (e.kind == "session_start") { /* Metadata refresh only for existing sessions. */ }
    else if (ignored) { /* Background waiting ignores idle prompts and unmatched errors. */ }
    else if (e.kind == "prompt") { s.tools.clear(); s.state = "thinking"; s.interrupted = false; s.turnStarted = e.timestamp; }
    else if (e.kind == "tool_start") { s.tools[e.tool] = e.activity.isEmpty() ? "working" : e.activity; s.state = toolState(s); }
    else if (e.kind == "tool_end") {
        s.tools.remove(e.tool);
        // A tool that was waiting on an answer has run, so the user answered. A request that named no
        // tool and found none started (Codex apply_patch or MCP, which can skip PreToolUse) is answered
        // by the first tool to finish.
        if (s.state == "attention" && (s.attentionTools.isEmpty() || s.attentionTools.contains(e.tool))) s.state = toolState(s);
        else if (s.state != "attention" && s.state != "exhausted" && s.state != "inactive" && s.state != "turn-finished" && s.state != "waiting" &&
                 (isNew || s.state != "idle")) {
            const auto next = toolState(s);
            if (next == "thinking" && (s.state == "working" || s.state == "reading")) s.activityUntil = now + activityHoldMs;
            else s.state = next;
        }
    }
    else if (e.kind == "attention") { s.state = "attention"; s.reason = e.reason;
        // Codex permission requests carry no tool identity: the pending tools are those already started.
        s.attentionTools = e.tool.isEmpty() ? QSet<QString>(s.tools.keyBegin(), s.tools.keyEnd()) : QSet<QString>{e.tool}; }
    else if (e.kind == "error") {
        if (!e.tool.isEmpty()) s.tools.remove(e.tool);
        if (s.state != "attention" && s.state != "exhausted") {
            if (!e.tool.isEmpty()) s.resume = toolState(s);
            else if (s.state != "error") s.resume = held ? "thinking" : s.state;
            s.state = "error"; s.reason.clear(); s.reactionUntil = now + 4000;
        }
    }
    else if (e.kind == "turn_finished") {
        s.tools.clear(); s.state = e.waiting ? "waiting" : "turn-finished";
        s.reactionUntil = e.waiting ? 0 : now + 4000;
        if (!e.waiting) {
            s.lastTurnMs = s.turnStarted ? e.timestamp - s.turnStarted : 0; s.turnStarted = 0;
        }
    }
    else if (e.kind == "interrupt") { s.tools.clear(); s.state = "idle"; s.interrupted = true; s.turnStarted = 0; }
    else if (e.kind == "turn_failed") {
        // The turn is over. Out of quota, nothing changes until work resumes: a retry, or an auto-resume's
        // first tool or finish. Any other failure reacts like an error, then rests.
        s.tools.clear(); s.turnStarted = 0;
        if (!e.reason.isEmpty()) { s.state = "exhausted"; s.reason = e.reason; }
        else { s.state = "error"; s.reason = "turn"; s.resume = "idle"; s.reactionUntil = now + 4000; }
    }
    if (s.state != "attention") {
        if (s.state != "exhausted" && s.state != "error") s.reason.clear();
        s.attentionTools.clear(); dismiss(k, "attention");
    }
    if (s.state != "exhausted") dismiss(k, "exhausted");
    if (!ignored && (e.kind == "attention" || e.kind == "error" ||
                     (e.kind == "turn_finished" && !e.waiting) || e.kind == "turn_failed")) {
        // A failed turn is reported as a quota stop, or as an error titled by the reason "turn".
        const QString kind = e.kind != "turn_failed" ? e.kind : e.reason.isEmpty() ? "error" : "exhausted";
        const QString reason = e.kind == "turn_failed" && e.reason.isEmpty() ? "turn" : e.reason;
        const qint64 expires = kind == "turn_finished" ? now + finishedAlertMs : kind == "error" ? now + errorAlertMs : 0;
        auto it = std::find_if(alerts_.begin(), alerts_.end(), [&](const Alert &a) { return a.session == k && a.kind == kind && (kind != "error" || a.reason == reason); });
        if (it != alerts_.end()) {
            it->count = std::min(it->count + 1, 1000000); it->serial = ++serial_;
            if (!s.project.isEmpty()) it->project = s.project;
            if (!reason.isEmpty() || kind == "error") it->reason = reason;
            it->expires = expires;
        } else {
            if (alerts_.size() == maxAlerts) alerts_.removeFirst();
            alerts_.append({k, kind, s.project, e.provider, e.session, reason, now, 1, ++serial_, expires});
        }
    }
    return true;
}
void Sessions::expire(qint64 now) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now - it->seen >= expiryMs) {
            ended_[it.key()] = it->timestamp;
            const auto k = it.key();
            alerts_.erase(std::remove_if(alerts_.begin(), alerts_.end(), [&](const Alert &a) { return a.session == k; }), alerts_.end());
            it = sessions_.erase(it);
        } else {
            if (it->activityUntil && now >= it->activityUntil) {
                if (it->tools.isEmpty() && (it->state == "working" || it->state == "reading")) it->state = "thinking";
                it->activityUntil = 0;
            }
            if (it->reactionUntil && now >= it->reactionUntil) {
                if (it->state == "error") { it->state = it->tools.isEmpty() ? it->resume : toolState(*it); it->reason.clear(); }
                else if (it->state == "turn-finished") it->state = "idle";
                it->reactionUntil = 0;
            }
            ++it;
        }
    }
    alerts_.erase(std::remove_if(alerts_.begin(), alerts_.end(), [&](const Alert &a) { return a.expires && now >= a.expires; }), alerts_.end());
    trimOldest(ended_, maxSessions);
    for (auto it = events_.begin(); it != events_.end();) if (now - it.value() >= expiryMs) it = events_.erase(it); else ++it;
}
QString Sessions::aggregate(qint64) const {
    const QStringList priority{"attention", "exhausted", "error", "turn-finished", "working", "reading", "thinking", "waiting", "idle", "inactive"};
    for (const auto &state : priority) for (const auto &s : sessions_) if (s.state == state) return state;
    return "idle";
}
QVector<Alert> Sessions::pending() const {
    auto result = alerts_;
    std::stable_sort(result.begin(), result.end(), [&](const Alert &a, const Alert &b) { return alertRank(a.kind) < alertRank(b.kind); });
    return result;
}
int Sessions::unresolvedAttention() const {
    return int(std::count_if(sessions_.begin(), sessions_.end(), [](const Session &s) { return s.state == "attention"; }));
}
void Sessions::dismiss(const QString &session, const QString &kind) {
    alerts_.erase(std::remove_if(alerts_.begin(), alerts_.end(), [&](const Alert &a) { return a.session == session && a.kind == kind; }), alerts_.end());
}
}
