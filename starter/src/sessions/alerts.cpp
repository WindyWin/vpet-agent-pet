#include "alerts.h"
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
    if (path.isEmpty()) return "Unknown project";
    const auto cleaned = cleanPath(path);
    const QFileInfo info(cleaned);
    const auto name = info.fileName().isEmpty() ? cleaned : info.fileName();
    const bool ambiguous = std::any_of(context.begin(), context.end(), [&](const Alert &a) {
        return !a.project.isEmpty() && cleanPath(a.project) != cleaned && QFileInfo(cleanPath(a.project)).fileName() == name;
    });
    const auto parent = QFileInfo(info.path()).fileName();
    return ambiguous && !parent.isEmpty() ? name + " (" + parent + ")" : name;
}
AlertText describe(const Alert &alert, const QVector<Alert> &context) {
    QString title = alert.kind == "error" ? "Tool error" : alert.kind == "turn_finished" ? "Turn finished"
                  : alert.reason == "approval" ? "Needs approval" : alert.reason == "input" ? "Needs input" : "Needs attention";
    if (alert.count > 1) title += QString(" (×%1)").arg(alert.count);
    return {title,
            projectName(alert.project, context) + " · " + providerName(alert.provider) + " · " + shortSessionId(alert.provider, alert.id, context),
            alert.project.isEmpty() ? "Project path unavailable for this session" : alert.project};
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
}
