#include "monitor.h"
#include "host_focus.h"
#include "session_playback.h"
#include <QApplication>
#include <QDateTime>
#include <QToolTip>
#include <algorithm>

namespace pet {
Monitor::Monitor(PetWindow &window) : hostActive(hostFocus::active), bringForward(hostFocus::focus), window_(window) {
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, [this] { update(QDateTime::currentMSecsSinceEpoch()); });
    timer_.start();
    connect(&bubble_, &AlertBubble::focusRequested, this, &Monitor::focusCurrent);
    connect(&bubble_, &AlertBubble::listRequested, this, &Monitor::toggleSessions);
    connect(&bubble_, &AlertBubble::dismissRequested, this, &Monitor::dismiss);
    connect(&list_, &SessionList::focusRequested, this, &Monitor::focusSession);
    connect(&window_, &PetWindow::moved, this, [this] {
        if (bubble_.isVisible()) bubble_.place(window_.figure(), window_.screenAreas());
        if (list_.isVisible()) list_.place(window_.figure(), window_.screenAreas());
    });
    connect(&window_, &PetWindow::notificationsChanged, this, &Monitor::refreshAlerts);
    connect(&window_, &PetWindow::sessionsRequested, this, &Monitor::toggleSessions);
    connect(&window_, &PetWindow::quitRequested, this, &Monitor::stop);
    connect(&window_, &PetWindow::presenceChanged, this, [this] {
        if (window_.petHidden()) list_.hide();
        refreshAlerts();
    });
}
// Subagents fold into their parent, as in the running-sessions list.
static int topLevelSessions(const Sessions &sessions) {
    const auto &records = sessions.records();
    return int(std::count_if(records.begin(), records.end(), [&](const Session &s) {
        return s.parent.isEmpty() || !records.contains(s.provider + QChar(0x1f) + s.parent);
    }));
}
void Monitor::listen(std::unique_ptr<Receiver> receiver) {
    receiver->received = [this](const Event &event) { apply(event, QDateTime::currentMSecsSinceEpoch()); };
    receiver_ = std::move(receiver);
}
bool Monitor::apply(const Event &event, qint64 now) {
    if (!active_ || !sessions_.apply(event, now)) return false;
    observed_ = true; update(now);
    return true;
}
void Monitor::update(qint64 now) {
    if (!active_) return;
    sessions_.expire(now);
    refreshAlerts();
    if (list_.isVisible()) list_.present(sessionRows(sessions_, now));
    int errors = 0;
    for (const auto &alert : sessions_.pending()) errors += alert.kind == "error";
    window_.setStatus(topLevelSessions(sessions_), sessions_.unresolvedAttention(), errors);
    window_.updatePresence(sessions_.records().size(), now); // May hide, show or quit the pet.
    if (!active_ || !observed_ || window_.player().requestedState() == "closing") return;
    const auto state = sessions_.aggregate(now);
    const auto animation = sessionAnimation(state);
    if (state != lastAggregate_ || (!window_.player().isDragging() && state != "error" && state != "turn-finished" &&
                                    window_.player().requestedState() != animation)) {
        window_.player().select(animation, state == "attention" || state == "error");
        lastAggregate_ = state;
    }
}
bool Monitor::shown(const Alert &alert) const {
    const int level = window_.bubbles();
    return alert.kind == "attention" || (alert.kind == "error" && level >= Preferences::RequestsAndErrors) ||
           level >= Preferences::AllAlerts;
}
void Monitor::refreshAlerts() {
    window_.setAttention(sessions_.unresolvedAttention());
    // A new alert for the window the user is already looking at is not news.
    bool raised = false;
    quint64 newest = heard_;
    for (const auto &alert : sessions_.pending()) {
        if (alert.serial <= heard_) continue;
        newest = std::max(newest, alert.serial);
        const auto session = sessions_.records().value(alert.session);
        if (!session.host.isEmpty() && hostActive && hostActive(session)) sessions_.dismiss(alert.session, alert.kind);
        else raised = raised || shown(alert);
    }
    heard_ = newest;
    QVector<Alert> visible;
    for (const auto &alert : sessions_.pending()) if (shown(alert)) visible.append(alert);
    queue_.sync(visible);
    const auto *alert = queue_.current();
    if (!alert || window_.muted() || !active_) { bubble_.hide(); return; }
    if (raised && window_.sound()) QApplication::beep();
    // A hidden pet keeps its alerts in the tray icon and tooltip instead of a bubble.
    if (window_.petHidden()) { bubble_.hide(); return; }
    bubble_.present(describe(*alert, visible), alert->kind, queue_.more());
    bubble_.place(window_.figure(), window_.screenAreas());
    if (!bubble_.isVisible()) bubble_.show();
}
void Monitor::dismiss() { queue_.dismiss(sessions_); refreshAlerts(); }
void Monitor::toggleSessions() {
    if (!active_) return;
    if (list_.isVisible()) { list_.hide(); return; }
    list_.present(sessionRows(sessions_, QDateTime::currentMSecsSinceEpoch()));
    list_.place(window_.figure(), window_.screenAreas());
    list_.show();
}
bool Monitor::focusSession(const QString &key) {
    const auto it = sessions_.records().find(key);
    if (it == sessions_.records().end() || !bringForward || !bringForward(*it)) {
        QToolTip::showText(window_.figure().center(), it != sessions_.records().end() && it->host.isEmpty()
            ? "This session started before Agent Pet could see its terminal. Its next event will fix that."
            : "Could not focus this session's window. Check that its terminal is attached. Wayland focus requires KDE Plasma 6.");
        return false;
    }
    // Going there answers its bubbles; a pending request keeps the badge until it resolves.
    for (const auto *kind : {"attention", "error", "turn_finished"}) sessions_.dismiss(key, kind);
    refreshAlerts();
    return true;
}
void Monitor::focusCurrent() {
    if (const auto *alert = queue_.current()) focusSession(QString(alert->session)); // A copy: the queue re-syncs.
}
void Monitor::stop() {
    active_ = false; timer_.stop(); receiver_.reset(); bubble_.hide(); list_.hide();
}
}
