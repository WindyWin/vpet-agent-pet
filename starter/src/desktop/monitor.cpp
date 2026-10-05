#include "monitor.h"
#include "session_playback.h"
#include <QApplication>
#include <QDateTime>
#include <QToolTip>
#include <algorithm>

namespace pet {
Monitor::Monitor(PetWindow &window, std::shared_ptr<hosts::FocusService> focus) : window_(window), focus_(std::move(focus)) {
    if (focus_) {
        hostActive = [this](const Session &s) { return focus_->active(s.host, s.project); };
        bringForward = [this](const Session &s) { return focus_->focus(s.host, s.project); };
    }
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
        if (window_.petHidden()) { list_.hide(); note_.hide(); }
        refreshAlerts();
    });
}
const QString Monitor::bedtimeNote = "It's getting late. Maybe finish up and get some sleep?";
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
    // Only accepted events count: duplicates and stale callbacks of an interrupted turn are dropped above.
    if (event.kind == "turn_finished") {
        window_.mood().finished(now);
        lastTurnMs_ = sessions_.records().value(event.provider + QChar(0x1f) + event.session).lastTurnMs;
    }
    else if (event.kind == "error") window_.mood().failed(now);
    observed_ = true; update(now);
    // The hook saw a destructive command start: the pet jumps, then shows the work going on. A session
    // waiting on the user, or a fresh error, matters more.
    const auto aggregate = sessions_.aggregate(now);
    if (event.kind == "tool_start" && event.risky && !window_.petHidden() && aggregate != "attention" && aggregate != "error")
        window_.eggs().surprise("danger");
    // Work going on deep into the night earns one gentle note.
    if (event.kind == "turn_finished" && !window_.muted() && !window_.petHidden() && window_.eggs().bedtime())
        say(bedtimeNote);
    return true;
}
void Monitor::update(qint64 now) {
    if (!active_) return;
    sessions_.expire(now);
    remind();
    refreshAlerts();
    if (list_.isVisible()) list_.present(sessionRows(sessions_, now));
    int errors = 0;
    for (const auto &alert : sessions_.pending()) errors += alert.kind == "error";
    window_.setStatus(topLevelSessions(sessions_), sessions_.unresolvedAttention(), errors);
    window_.updatePresence(sessions_.records().size(), now); // May hide, show or quit the pet.
    window_.mood().refresh(now);
    if (!active_ || !observed_ || window_.player().requestedState() == "closing") return;
    const auto state = sessions_.aggregate(now);
    const auto animation = sessionAnimation(state);
    // A fidget, an ambient nap or a reaction to the user, such as hiding at a screen edge, is how an idle pet
    // looks; leave it until something real happens. A reaction counts from when it is requested, because
    // the drag's own end plays first.
    const bool resting = window_.ambient().resting() || window_.player().isTouch(window_.player().requestedState());
    const auto showing = animation == "idle" && resting ? animation : window_.player().requestedState();
    // A surprise, such as a startled jump or a dance, plays out unless a session needs the user.
    if (window_.eggs().surprising() && state != "attention" && state != "error") return;
    // While the user holds the pet, or it is falling, the player keeps the latest request for afterwards.
    if (state != lastAggregate_ || (!window_.player().held() && state != "error" && state != "turn-finished" &&
                                    showing != animation)) {
        // A turn that just finished is celebrated in one of several ways, or with a treat when one is due.
        const bool celebrate = state == "turn-finished" && lastAggregate_ != state;
        window_.player().select(celebrate ? window_.mood().celebrate(window_.eggs().celebration(lastTurnMs_)) : animation,
                                state == "attention" || state == "error");
        lastAggregate_ = state;
    }
}
void Monitor::say(const QString &text) { note_.say(text, window_.figure(), window_.screenAreas()); }
// Monday blues, the go-home nudge and bedtime: said once each day, and kept for later while the pet is hidden.
void Monitor::remind() {
    if (window_.petHidden()) return;
    const auto reminder = window_.eggs().reminder();
    if (reminder.isEmpty()) return;
    window_.eggs().surprise(reminder);
    if (!window_.muted()) say(EasterEggs::reminderNote(reminder));
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
        if (!session.host.isNull() && hostActive && hostActive(session) == platform::ActiveState::Active) sessions_.dismiss(alert.session, alert.kind);
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
// Tooltip shown when Open did not raise the session's window.
static QString focusFailure(const Session &session, const hosts::FocusResult &result) {
    using platform::Outcome;
    if (session.host.isNull()) return "This session started before Agent Pet could see its terminal. Its next event will fix that.";
    QStringList text;
    if (result.activation == Outcome::Unsupported) text << "This desktop session does not let Agent Pet raise windows.";
    else if (result.activation == Outcome::Skipped) text << "Could not select this session's tab or pane. Check that its terminal is attached.";
    else text << "Could not focus this session's window. Check that its terminal is attached.";
    return (text + result.requirements).join(' ');
}
bool Monitor::focusSession(const QString &key) {
    const auto it = sessions_.records().find(key);
    if (it == sessions_.records().end()) {
        QToolTip::showText(window_.figure().center(), "Could not focus this session's window. Check that its terminal is attached.");
        return false;
    }
    // A copy: focusing can process events, and new hook events may change the session map.
    const Session session = *it;
    hosts::FocusResult result;
    result.activation = platform::Outcome::Unsupported;
    if (!session.host.isNull() && bringForward) result = bringForward(session);
    if (!result.raised()) {
        QToolTip::showText(window_.figure().center(), focusFailure(session, result));
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
    active_ = false; timer_.stop(); receiver_.reset(); bubble_.hide(); note_.hide(); list_.hide();
}
}
