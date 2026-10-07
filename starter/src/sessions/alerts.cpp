#include "alerts.h"
#include "i18n/contexts.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace pet {
QString providerName(const QString &provider) {
    return provider == "claude" ? "Claude Code" : provider == "codex" ? "Codex" : provider;
}
QString shortSessionId(const QString &provider, const QString &id, const QVector<Alert> &context) {
    for (int length = std::min<int>(4, id.size()); length < id.size(); ++length) {
        const auto prefix = id.left(length);
        if (std::none_of(context.begin(), context.end(), [&](const Alert &a) {
                return a.provider == provider && a.id != id && a.id.startsWith(prefix); }))
            return prefix;
    }
    return id;
}
static QString cleanPath(const QString &path) {
    const auto cleaned = QDir::cleanPath(path);
    return cleaned.size() > 1 && cleaned.endsWith('/') ? cleaned.chopped(1) : cleaned;
}
QString projectName(const QString &path, const QVector<Alert> &context) {
    if (path.isEmpty()) return Alerts::tr("Unknown project");
    const auto cleaned = cleanPath(path);
    const QFileInfo info(cleaned);
    const auto name = info.fileName().isEmpty() ? cleaned : info.fileName();
    const bool ambiguous = std::any_of(context.begin(), context.end(), [&](const Alert &a) {
        return !a.project.isEmpty() && cleanPath(a.project) != cleaned && QFileInfo(cleanPath(a.project)).fileName() == name;
    });
    const auto parent = QFileInfo(info.path()).fileName();
    return ambiguous && !parent.isEmpty() ? name + " (" + parent + ")" : name;
}
QString alertTitle(const Alert &alert) {
    if (alert.kind == "exhausted") return alert.reason == "billing" ? Alerts::tr("Out of credits") : Alerts::tr("Usage limit reached");
    if (alert.kind == "error") return alert.reason == "turn" ? Alerts::tr("Turn failed") : Alerts::tr("Tool error");
    return alert.kind == "turn_finished" ? Alerts::tr("Turn finished")
         : alert.reason == "approval" ? Alerts::tr("Needs approval") : alert.reason == "input" ? Alerts::tr("Needs input")
         : Alerts::tr("Needs attention");
}
AlertText describe(const Alert &alert, const QVector<Alert> &context) {
    QString title = alertTitle(alert);
    if (alert.count > 1) title += QString(" (×%1)").arg(alert.count);
    const auto name = projectName(alert.project, context);
    return {title, name + " · " + providerName(alert.provider) + " · " + shortSessionId(alert.provider, alert.id, context),
            alert.project.isEmpty() ? Alerts::tr("Project path unavailable for this session") : alert.project, name};
}
int AlertQueue::index() const {
    for (int i = 0; i < pending_.size(); ++i)
        if (pending_[i].session == session_ && pending_[i].kind == kind_) return i;
    return -1;
}
void AlertQueue::sync(const QVector<Alert> &pending) {
    pending_ = pending;
    if (pending_.isEmpty()) { session_.clear(); kind_.clear(); return; }
    const int i = index();
    // Pending is sorted by priority. A newly raised alert that outranks the shown
    // one takes over; otherwise the user's Next position is kept.
    const auto preempt = std::find_if(pending_.begin(), pending_.end(), [&](const Alert &a) {
        return i >= 0 && a.serial > serial_ && rank(a.kind) < rank(pending_[i].kind);
    });
    if (i < 0) { session_ = pending_.first().session; kind_ = pending_.first().kind; }
    else if (preempt != pending_.end()) { session_ = preempt->session; kind_ = preempt->kind; }
    for (const auto &a : pending_) serial_ = std::max(serial_, a.serial);
}
const Alert *AlertQueue::current() const {
    const int i = index();
    return i < 0 ? nullptr : &pending_[i];
}
void AlertQueue::next() {
    if (pending_.isEmpty()) return;
    const auto &alert = pending_[(std::max(index(), 0) + 1) % pending_.size()];
    session_ = alert.session; kind_ = alert.kind;
}
void AlertQueue::dismiss(Sessions &sessions) {
    const int i = index();
    if (i < 0) return;
    sessions.dismiss(session_, kind_);
    // Show the alert that followed the dismissed one, keeping the user's place.
    const auto following = pending_.size() > 1 ? pending_[(i + 1) % pending_.size()] : Alert{};
    session_ = following.session; kind_ = following.kind;
    sync(sessions.pending());
}
static int stateRank(const QString &state) {
    static const QStringList order{"attention", "exhausted", "error", "working", "reading", "thinking", "turn-finished", "idle", "inactive"};
    const int i = order.indexOf(state);
    return i < 0 ? order.size() : i;
}
static QString statusText(const Session &s, qint64 now) {
    if (s.state == "attention") return alertTitle({{}, "attention", {}, {}, {}, s.reason});
    if (s.state == "exhausted") return alertTitle({{}, "exhausted", {}, {}, {}, s.reason});
    if (s.state == "error") return s.reason == "turn" ? Alerts::tr("Turn failed") : Alerts::tr("Tool error");
    if (s.state == "working") return Alerts::tr("Working");
    if (s.state == "reading") return Alerts::tr("Reading");
    if (s.state == "thinking") return Alerts::tr("Thinking");
    if (s.state == "turn-finished") return Alerts::tr("Finished");
    if (s.state == "inactive") return Alerts::tr("Stopped");
    const qint64 minutes = std::max<qint64>(0, now - s.seen) / 60000;
    return minutes < 1 ? Alerts::tr("Idle")
                       //: %1 = minutes since the session's last event
                       : Alerts::tr("Idle · %1 min").arg(minutes);
}
QVector<SessionRow> sessionRows(const Sessions &sessions, qint64 now, const hosts::Registry &hosts) {
    const auto &records = sessions.records();
    const auto parentOf = [&](const Session &s) {
        const auto key = s.provider + QChar(0x1f) + s.parent;
        return !s.parent.isEmpty() && records.contains(key) ? key : QString();
    };
    // One pseudo-alert per listed session keeps short IDs and names unambiguous.
    QVector<Alert> context;
    for (auto it = records.begin(); it != records.end(); ++it)
        if (parentOf(*it).isEmpty()) context.append({it.key(), {}, it->project, it->provider, it->id, {}});
    QVector<SessionRow> rows;
    QMap<QString, int> index;
    for (auto it = records.begin(); it != records.end(); ++it) {
        if (!parentOf(*it).isEmpty()) continue;
        QStringList detail{providerName(it->provider), shortSessionId(it->provider, it->id, context)};
        if (!hosts.label(it->host.adapter).isEmpty()) detail << hosts.label(it->host.adapter);
        index[it.key()] = rows.size();
        rows.append({it.key(), projectName(it->project, context), detail.join(" · "), statusText(*it, now), it->state,
                     it->project.isEmpty() ? Alerts::tr("Project path unavailable for this session") : it->project});
    }
    for (auto it = records.begin(); it != records.end(); ++it) {
        const auto parent = parentOf(*it);
        if (parent.isEmpty()) continue;
        auto &row = rows[index[parent]];
        ++row.children;
        // A busy or blocked subagent makes its parent row busy or blocked.
        if (stateRank(it->state) < stateRank(row.state) && stateRank(it->state) < stateRank("turn-finished")) {
            row.state = it->state; row.status = statusText(*it, now);
        }
    }
    for (auto &row : rows)
        if (row.children) row.detail += " · " + (row.children == 1 ? Alerts::tr("1 subagent") : Alerts::tr("%1 subagents").arg(row.children));
    std::stable_sort(rows.begin(), rows.end(), [&](const SessionRow &a, const SessionRow &b) {
        const int ra = stateRank(a.state), rb = stateRank(b.state);
        return ra != rb ? ra < rb : records.value(a.key).seen > records.value(b.key).seen;
    });
    return rows;
}
}
