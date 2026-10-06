#include "monitor.h"
#include "ipc/session_store.h"
#include "session_playback.h"
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QToolTip>
#include <algorithm>
#include <utility>

namespace pet {
Monitor::Monitor(PetWindow &window, std::shared_ptr<hosts::FocusService> focus)
    : window_(window), focus_(std::move(focus)), recapStore_(window.recapPath()), recap_(recapStore_.load()) {
    if (focus_) {
        hostActive = [this](const Session &s) { return focus_->active(s.host, s.project); };
        bringForward = [this](const Session &s) { return focus_->focus(s.host, s.project, s.provider); };
    }
    pointer = [] { return QCursor::pos(); };
    lastPointer_ = pointer();
    rest_.setInterval(1000);
    connect(&rest_, &QTimer::timeout, this, &Monitor::rest);
    connect(&note_, &NoteBubble::clicked, this, &Monitor::answered);
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
    connect(&window_, &PetWindow::recapRequested, this, &Monitor::showRecap);
    recapTimer_.setSingleShot(true); recapTimer_.setInterval(2000);
    connect(&recapTimer_, &QTimer::timeout, this, [this] { recapStore_.save(recap_); });
    connect(&window_, &PetWindow::quitRequested, this, &Monitor::stop);
    connect(&window_, &PetWindow::presenceChanged, this, [this] {
        if (window_.petHidden()) { list_.hide(); note_.hide(); dropReminder(); }
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
void Monitor::restoreSessions(const QString &path) {
    sessionPath_ = path;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (loadSessions(path, sessions_, now)) {
        observed_ = !sessions_.records().isEmpty();
        update(now);
    }
}
bool Monitor::checkpointSessions() {
    return sessionPath_.isEmpty() || saveSessions(sessionPath_, sessions_);
}
bool Monitor::apply(const Event &event, qint64 now) {
    if (!active_ || !sessions_.apply(event, now)) return false;
    {
        const auto it = sessions_.records().find(event.provider + QChar(0x1f) + event.session);
        const Session *session = it == sessions_.records().end() ? nullptr : &*it;
        if (recap_.record(event, session, QDateTime::fromMSecsSinceEpoch(now).date())) recapTimer_.start();
    }
    // Only accepted events count: duplicates and stale callbacks of an interrupted turn are dropped above.
    if (event.kind == "turn_finished") {
        window_.mood().finished(now);
        lastTurnMs_ = sessions_.records().value(event.provider + QChar(0x1f) + event.session).lastTurnMs;
    }
    else if (event.kind == "error") window_.mood().failed(now);
    // Only a prompt shows the user is there; nothing counts behind a locked screen.
    if (!(locked && locked())) window_.wellness().activity(now, event.kind == "prompt");
    checkpointSessions();
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
    // A locked screen is a break: both timers start over, nothing counts until it is unlocked, and a
    // reminder or countdown on screen goes away. The pointer is followed, so a move behind the lock does
    // not count as the user coming back.
    if (locked && locked()) {
        window_.wellness().reset(); dropReminder();
        if (pointer) lastPointer_ = pointer();
    }
    else if (pointer) {
        const auto position = pointer();
        if (position != lastPointer_) { lastPointer_ = position; window_.wellness().activity(now); }
    }
    remind();
    refreshAlerts();
    if (list_.isVisible()) list_.present(sessionRows(sessions_, now));
    int errors = 0;
    for (const auto &alert : sessions_.pending()) errors += alert.kind == "error";
    window_.setStatus(topLevelSessions(sessions_), sessions_.unresolvedAttention(), errors);
    window_.updatePresence(sessions_.records().size(), now); // May hide, show or quit the pet.
    window_.mood().refresh(now);
    remindWellness(now);
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
        const auto chosen = celebrate ? window_.mood().celebrate(window_.eggs().celebration(lastTurnMs_)) : animation;
        window_.player().select(chosen, state == "attention" || state == "error");
        // A snack that played already says "have a drink"; the water reminder need not repeat it.
        const auto snacks = window_.player().reactions("snack");
        if (celebrate && std::any_of(snacks.begin(), snacks.end(), [&](const auto &r) { return r.state == chosen; }))
            window_.wellness().given("water", now);
        lastAggregate_ = state;
    }
}
void Monitor::say(const QString &text, const QString &details, int ms) {
    reminder_.clear(); // Whatever the note said before is gone.
    note_.say(text, window_.figure(), window_.screenAreas(), details, ms);
}
// The user is there, nothing needs them, nothing else is being said and the pet is free: a reminder will
// not get in the way. A locked screen sees no pointer movement, so it holds reminders too.
bool Monitor::calm(qint64 now) const {
    const auto state = sessions_.aggregate(now);
    return window_.wellness().present(now) && !window_.petHidden() && !window_.muted() && !bubble_.isVisible() && !note_.isVisible() && restLeft_ == 0 &&
           sessions_.unresolvedAttention() == 0 && state != "attention" && state != "error" &&
           !window_.player().held() && !window_.eggs().surprising() && !window_.walking() && !window_.flying();
}
// An eye break or a sip of water, once its stretch of active time is up. A due reminder waits for calm; in
// quiet hours it is let go, so the morning does not start with one.
void Monitor::remindWellness(qint64 now) {
    if (!active_ || (locked && locked())) return; // The presence update may have just quit.
    auto &wellness = window_.wellness();
    const auto due = wellness.due(now);
    if (due.isEmpty()) return;
    if (Wellness::quietAt(window_.eggs().now())) { wellness.given(due, now); return; }
    if (!calm(now)) return;
    wellness.given(due, now); // Ignored, it fades and comes back after the next interval.
    window_.eggs().surprise(due == "eyes" ? "eye_break" : "water", true);
    say(Wellness::note(due));
    reminder_ = due;
}
// A click on the reminder means it was done: a sip earns a happy reaction at once, an eye break after its countdown.
void Monitor::answered() {
    if (restLeft_ > 0) { rest_.stop(); restLeft_ = 0; return; } // Clicking the countdown away ends it.
    const auto reminder = std::exchange(reminder_, QString());
    if (reminder == "water") window_.eggs().surprise("reminder_done", true);
    if (reminder != "eyes" || !active_) return;
    restLeft_ = Wellness::eyeRestSeconds + 1;
    rest();
    rest_.start();
}
void Monitor::dropReminder() {
    if (reminder_.isEmpty() && restLeft_ == 0) return; // The note may be saying something else.
    note_.hide(); reminder_.clear(); rest_.stop(); restLeft_ = 0;
}
void Monitor::rest() {
    if (locked && locked()) { dropReminder(); return; }
    if (!active_ || window_.petHidden() || --restLeft_ < 0) { rest_.stop(); restLeft_ = 0; return; }
    if (restLeft_ > 0) {
        note_.say(QString("Eyes on something far away… %1").arg(restLeft_), window_.figure(), window_.screenAreas(), {}, 1500);
        return;
    }
    rest_.stop();
    window_.eggs().surprise("reminder_done", true);
    say("Nice! Your eyes thank you.", {}, 4000);
}
void Monitor::showRecap() {
    if (!active_) return;
    const auto today = recap_.day(QDate::currentDate());
    // A hidden pet has no bubble to speak from; the tray says it instead.
    if (window_.petHidden()) window_.showTrayMessage("Today's recap", Recap::breakdown(today));
    else say(Recap::summary(today), today.turns ? Recap::breakdown(today) : QString());
}
// Monday blues, the go-home nudge and bedtime: said once each day, and kept for later while the pet is hidden.
void Monitor::remind() {
    if (window_.petHidden()) return;
    const auto reminder = window_.eggs().reminder();
    if (reminder.isEmpty()) return;
    window_.eggs().surprise(reminder);
    if (window_.muted()) return;
    // The go-home nudge sums up the day, when there was agent work to sum up.
    const auto today = recap_.day(QDate::currentDate());
    if (reminder == "leave_work" && window_.recapEnabled() && today.turns)
        say(EasterEggs::reminderNote(reminder) + "\n" + Recap::summary(today), Recap::breakdown(today));
    else say(EasterEggs::reminderNote(reminder));
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
void Monitor::dismiss() { queue_.dismiss(sessions_); checkpointSessions(); refreshAlerts(); }
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
    checkpointSessions();
    refreshAlerts();
    return true;
}
void Monitor::focusCurrent() {
    if (const auto *alert = queue_.current()) focusSession(QString(alert->session)); // A copy: the queue re-syncs.
}
void Monitor::stop() {
    checkpointSessions();
    active_ = false; timer_.stop(); rest_.stop(); restLeft_ = 0; receiver_.reset(); bubble_.hide(); note_.hide(); list_.hide();
    if (recapTimer_.isActive()) { recapTimer_.stop(); recapStore_.save(recap_); }
}
}
