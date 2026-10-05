#include "state.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
#include <algorithm>

namespace pet {
bool Event::parse(const QByteArray &data, Event &e, QString &error) {
    if (data.size() > 8192) { error = "Event exceeds 8192 bytes"; return false; }
    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { error = "Expected a JSON object"; return false; }
    const auto o = doc.object();
    const QSet<QString> fields{"version", "provider", "session_id", "event_id", "kind", "timestamp_ms", "tool_id", "parent_id", "project_path", "activity", "reason",
                              "host", "host_pids", "host_window", "host_target", "risky"};
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!fields.contains(it.key())) { error = "Unknown event field"; return false; }
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
         o.value("host_window").toString(), o.value("host_target").toString(), o.value("risky").toBool()};
    const QSet<QString> kinds{"session_start", "prompt", "tool_start", "tool_end", "attention", "error", "turn_finished", "interrupt", "session_end"};
    if ((e.provider != "claude" && e.provider != "codex") || e.session.isEmpty() || e.id.isEmpty() || !kinds.contains(e.kind) ||
        ((e.kind == "tool_start" || e.kind == "tool_end") && e.tool.isEmpty()) ||
        (!e.activity.isEmpty() && e.activity != "reading" && e.activity != "working") ||
        (!e.reason.isEmpty() && (e.kind != "attention" || (e.reason != "approval" && e.reason != "input"))) ||
        (o.contains("risky") && e.kind != "tool_start")) {
        error = "Missing identity or unsupported event kind/activity"; return false;
    }
    static const QSet<QString> hosts{"konsole", "herdr", "tmux", "vscode", "terminal"};
    static const QRegularExpression pids("^[1-9][0-9]{0,9}(,[1-9][0-9]{0,9}){0,15}$"), window("^[1-9][0-9]{0,19}$");
    if ((!e.host.isEmpty() && !hosts.contains(e.host)) || (!e.hostPids.isEmpty() && !pids.match(e.hostPids).hasMatch()) ||
        (!e.hostWindow.isEmpty() && !window.match(e.hostWindow).hasMatch())) {
        error = "Invalid host identification"; return false;
    }
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
    if (!e.host.isEmpty()) { s.host = e.host; s.hostPids = e.hostPids; s.hostWindow = e.hostWindow; s.hostTarget = e.hostTarget; }
    const bool held = s.activityUntil != 0; // Showing a finished tool's activity.
    if (e.kind != "session_start" && e.kind != "tool_end") s.activityUntil = 0;
    if (e.kind == "session_start") { /* Metadata refresh only for existing sessions. */ }
    else if (e.kind == "prompt") { s.tools.clear(); s.state = "thinking"; s.interrupted = false; s.turnStarted = e.timestamp; }
    else if (e.kind == "tool_start") { s.tools[e.tool] = e.activity.isEmpty() ? "working" : e.activity; s.state = toolState(s); }
    else if (e.kind == "tool_end") {
        s.tools.remove(e.tool);
        // A tool that was waiting on an answer has run, so the user answered.
        if (s.state == "attention" && s.attentionTools.contains(e.tool)) s.state = toolState(s);
        else if (s.state != "attention" && s.state != "inactive" && s.state != "turn-finished" && (isNew || s.state != "idle")) {
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
        if (s.state != "attention") {
            if (!e.tool.isEmpty()) s.resume = toolState(s);
            else if (s.state != "error") s.resume = held ? "thinking" : s.state;
            s.state = "error"; s.reactionUntil = now + 4000;
        }
    }
    else if (e.kind == "turn_finished") {
        s.tools.clear(); s.state = "turn-finished"; s.reactionUntil = now + 4000;
        s.lastTurnMs = s.turnStarted ? e.timestamp - s.turnStarted : 0; s.turnStarted = 0;
    }
    else if (e.kind == "interrupt") { s.tools.clear(); s.state = "idle"; s.interrupted = true; s.turnStarted = 0; }
    if (s.state != "attention") { s.reason.clear(); s.attentionTools.clear(); dismiss(k, "attention"); }
    if (e.kind == "attention" || e.kind == "error" || e.kind == "turn_finished") {
        const qint64 expires = e.kind == "turn_finished" ? now + finishedAlertMs : e.kind == "error" ? now + errorAlertMs : 0;
        auto it = std::find_if(alerts_.begin(), alerts_.end(), [&](const Alert &a) { return a.session == k && a.kind == e.kind; });
        if (it != alerts_.end()) {
            it->count = std::min(it->count + 1, 1000000); it->serial = ++serial_;
            if (!s.project.isEmpty()) it->project = s.project;
            if (!e.reason.isEmpty()) it->reason = e.reason;
            it->expires = expires;
        } else {
            if (alerts_.size() == maxAlerts) alerts_.removeFirst();
            alerts_.append({k, e.kind, s.project, e.provider, e.session, e.reason, now, 1, ++serial_, expires});
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
                if (it->state == "error") it->state = it->tools.isEmpty() ? it->resume : toolState(*it);
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
    const QStringList priority{"attention", "error", "turn-finished", "working", "reading", "thinking", "idle", "inactive"};
    for (const auto &state : priority) for (const auto &s : sessions_) if (s.state == state) return state;
    return "idle";
}
QVector<Alert> Sessions::pending() const {
    auto result = alerts_;
    auto rank = [](const QString &kind) { return kind == "attention" ? 0 : kind == "error" ? 1 : 2; };
    std::stable_sort(result.begin(), result.end(), [&](const Alert &a, const Alert &b) { return rank(a.kind) < rank(b.kind); });
    return result;
}
int Sessions::unresolvedAttention() const {
    return int(std::count_if(sessions_.begin(), sessions_.end(), [](const Session &s) { return s.state == "attention"; }));
}
void Sessions::dismiss(const QString &session, const QString &kind) {
    alerts_.erase(std::remove_if(alerts_.begin(), alerts_.end(), [&](const Alert &a) { return a.session == session && a.kind == kind; }), alerts_.end());
}
}
