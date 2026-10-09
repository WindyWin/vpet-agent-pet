#include "monitor.h"
#include "ipc/session_store.h"
#include "i18n/contexts.h"
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
    connect(&note_, &NoteBubble::later, this, &Monitor::putOff);
    connect(&note_, &NoteBubble::skipped, this, &Monitor::skipped);
    // A reminder on screen is said again in the new language; anything else was a passing remark and goes.
    connect(&note_, &NoteBubble::outdated, this, [this] {
        if (!reminder_.isEmpty()) ask(reminder_);
    });
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
    connect(&window_.stage(), &Stage::outcome, this, &Monitor::outcome);
    connect(&window_, &PetWindow::presenceChanged, this, [this] {
        if (window_.petHidden()) { list_.hide(); note_.hide(); dropReminder(); }
        refreshAlerts();
    });
}
QString Monitor::bedtimeNote() { return Pet::tr("It's getting late. Maybe finish up and get some sleep?"); }
// Session states are cues (src/animation/cues.json): each aggregate state asks the pet to show the cue of that name.
// These are the Urgent class: they interrupt whatever the pet is doing and keep reminders and surprises away.
static bool urgent(const QString &cue) { return cue == "attention" || cue == "exhausted" || cue == "error"; }
// How long a due reminder may wait for calm before it is asked for again.
static constexpr qint64 reminderWaitMs = 10 * 60 * 1000;
static const char *const wellnessReminders[] = {"eyes", "water"};
static const char *const clockReminders[] = {"monday", "lunch", "leave-work", "sleep"};
// These ask to be confirmed: Done, Later or Skip today, and come back when ignored. Monday blues and bedtime are only said.
bool Monitor::confirmable(const QString &reminder) const {
    return reminder == "eyes" || reminder == "water" || reminder == "lunch" || reminder == "leave-work";
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
// A custom event is a named occurrence from a script (docs/events.md). It is a reaction, not session state: it never
// reaches the sessions, the recap or the wellness timers, so it cannot change what the sessions show.
bool Monitor::apply(const Event &event, qint64 now) {
    if (active_ && event.kind == "custom") return react("custom:" + event.name, event.timestamp, now);
    if (!active_ || !sessions_.apply(event, now)) return false;
    now_ = now;
    const auto it = sessions_.records().find(event.provider + QChar(0x1f) + event.session);
    const Session *session = it == sessions_.records().end() ? nullptr : &*it;
    if (recap_.record(event, session, QDateTime::fromMSecsSinceEpoch(now).date())) recapTimer_.start();
    // Only accepted events count: duplicates and stale callbacks of an interrupted turn are dropped above.
    if (event.kind == "turn_finished" && !event.waiting) {
        window_.mood().finished(now);
        lastTurnMs_ = sessions_.records().value(event.provider + QChar(0x1f) + event.session).lastTurnMs;
    }
    else if (failure(event, session)) window_.mood().failed(now);
    // Only a prompt shows the user is there; nothing counts behind a locked screen.
    if (!(locked && locked())) window_.wellness().activity(now, event.kind == "prompt");
    checkpointSessions();
    // A snooze "until this turn finishes" ends with the last turn going on, not with whichever session finishes first.
    if (event.kind == "turn_finished" && !event.waiting) {
        const auto &records = sessions_.records();
        const bool running = std::any_of(records.begin(), records.end(), [](const Session &s) {
            return s.state == "working" || s.state == "reading" || s.state == "thinking";
        });
        if (!running) window_.snooze().turnFinished();
    }
    observed_ = true; update(now);
    // The hook saw a destructive command start: the pet jumps, then shows the work going on. The runtime keeps
    // it away while a session waits on the user, is out of quota or just failed, or the pet is hidden.
    if (event.kind == "tool_start" && event.risky) window_.eggs().surprise("danger");
    // Work going on deep into the night earns one gentle note.
    if (event.kind == "turn_finished" && !event.waiting && !quiet() && !window_.petHidden() && window_.eggs().bedtime())
        say(bedtimeNote());
    // Background work that is still running is not a finished turn.
    if (!(event.kind == "turn_finished" && event.waiting)) react(event.kind, event.timestamp, now);
    return true;
}
// What a plugin pack says should happen now (events.json). Only an event from the last minute is worth a reaction.
// The reaction is a Surprise: it plays unless a session needs the user, the pet is held, hidden or busy with something
// bigger, and then it is let go; the trigger rests only once a reaction actually played.
bool Monitor::react(const QString &trigger, qint64 stamp, qint64 now) {
    constexpr qint64 recentMs = 60 * 1000;
    if (stamp < now - recentMs || stamp > now + recentMs) return false;
    auto &rules = window_.player().rules();
    const auto reaction = rules.pick(trigger, now, random);
    if (!reaction) return false;
    using namespace behavior;
    const auto submitted = window_.stage().runtime().submit({"plugin", trigger, "plugin-event", Policy::Surprise, Lifetime::OneShot,
                                                              0, {}, reaction->state, reaction->say});
    if (submitted != Submission::Admitted) return false;
    rules.commit(*reaction, now);
    return true;
}
void Monitor::update(qint64 now) {
    if (!active_) return;
    now_ = now;
    sessions_.expire(now);
    // A reminder that faded, or was hidden, without an answer is asked again later; one that lost its bubble to
    // something else is only put off.
    if ((!reminder_.isEmpty() || restLeft_ > 0) && window_.snooze().activeAt(now)) { // Snoozed meanwhile.
        note_.hide(); rest_.stop(); restLeft_ = 0; released(false);
    }
    else if (!reminder_.isEmpty() && !note_.isVisible() && restLeft_ == 0) released(true);
    nudges_.settle(reminder_, now);
    window_.setSnoozeShown(window_.snooze().activeAt(now));
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
    refreshAlerts();
    if (list_.isVisible()) list_.present(sessionRows(sessions_, now));
    int errors = 0;
    for (const auto &alert : sessions_.pending()) errors += alert.kind == "error";
    window_.setStatus(topLevelSessions(sessions_), sessions_.unresolvedAttention(), errors);
    window_.updatePresence(sessions_.records().size(), now); // May hide, show or quit the pet.
    if (!active_) return;
    window_.mood().refresh(now);
    withdrawReminders(now); // Before the context and the tick, which can start waiting work.
    syncBehavior(now);
    auto &runtime = window_.stage().runtime();
    runtime.tick();
    remind();
    remindWellness(now);
    if (!observed_) return;
    using namespace behavior;
    const auto cue = sessions_.aggregate(now);
    const auto *shape = findCue(cue);
    const bool moment = shape && shape->mode == "once"; // turn-finished, error: shown once, never replayed.
    const auto submitted = runtime.submit({"session", "activity", cue, urgent(cue) ? Policy::Urgent : Policy::Activity,
                                           moment ? Lifetime::Moment : Lifetime::Persistent});
    // A turn that just finished is celebrated in one of several ways, or with a treat when one is due; the
    // celebration takes the turn-finished state's place.
    if (submitted != Submission::Admitted || cue != "turn-finished") return;
    const auto celebration = window_.mood().celebrate(window_.eggs().celebration(lastTurnMs_));
    if (celebration.cue == "turn-finished") return; // No pool for any of them: the cue's own state shows.
    // A snack already says "have a drink": once it plays, the water reminder need not repeat it.
    const auto effects = celebration.cue == "snack" ? QStringList{"water"} : QStringList{};
    const auto celebrated = runtime.submit({"mood", "celebration", celebration.cue, Policy::Celebration, Lifetime::OneShot,
                                            runtime.now() + EasterEggs::surpriseMs, effects, celebration.state, celebration.say});
    if (celebrated == Submission::Rejected || celebrated == Submission::Duplicate) window_.mood().keep(celebration.cue);
}
void Monitor::say(const QString &text, const QString &details, int ms) {
    reminder_.clear(); // Whatever the note said before is gone.
    note_.say(text, window_.figure(), window_.screenAreas(), details, ms);
}
// Everything that decides whether a reminder would get in the way: the user is there, nothing needs them,
// nothing else is being said, and the pet is in view and free. A locked screen sees no pointer movement, so the
// user is never present behind it.
void Monitor::syncBehavior(qint64 now) {
    window_.stage().update([&](behavior::Context &c) {
        c.visible = !window_.petHidden();
        c.moving = window_.walking() || window_.sliding() || window_.flying();
        c.present = window_.wellness().present(now) && !(locked && locked());
        c.muted = window_.quiet(now);
        c.speaking = bubble_.isVisible() || note_.isVisible() || restLeft_ > 0;
        c.attention = sessions_.unresolvedAttention();
    });
}
// What the runtime did with a reminder, a treat or a rule's reaction. A reminder shows its note when it is admitted, so
// art and note come together, and a reminder that never got its turn says nothing; so does a reaction's remark.
void Monitor::outcome(const behavior::Intent &intent, behavior::Outcome outcome) {
    using behavior::Outcome;
    if (!active_) return;
    if (outcome == Outcome::Started && intent.effects.contains("water")) window_.wellness().given("water", now_);
    if (outcome == Outcome::Started && intent.source == "mood" && intent.cue == "birthday") window_.eggs().cheered();
    if (intent.source == "mood" && (outcome == Outcome::Dropped || outcome == Outcome::Expired)) window_.mood().keep(intent.cue);
    if (outcome != Outcome::Admitted) return;
    // A rule's remark comes with its reaction, or alone for a rule without art. Reminders take none: they say their note.
    if (!intent.remark.isEmpty() && !quiet()) say(intent.remark);
    if (intent.source == "wellness" || intent.source == "clock") {
        if (confirmable(intent.key)) { nudges_.shown(intent.key); ask(intent.key); return; }
        window_.eggs().reminded(intent.key);
        say(EasterEggs::reminderNote(intent.key, window_.eggs().reminderSchedule()));
    }
}
// A reminder that is no longer due (given, taken some other way, turned off, a break, quiet hours) stops waiting.
void Monitor::withdrawReminders(qint64 now) {
    auto &runtime = window_.stage().runtime();
    const auto clock = window_.eggs().dueReminders();
    for (const auto *reminder : clockReminders) if (!clock.contains(reminder)) { runtime.withdraw("clock", reminder); nudges_.forget(reminder); }
    auto wellness = window_.wellness().due(now); // Empty behind a locked screen: locking reset both timers.
    if (Wellness::quietAt(window_.eggs().now())) wellness.clear();
    for (const auto *reminder : wellnessReminders) if (wellness != reminder) { runtime.withdraw("wellness", reminder); nudges_.forget(reminder); }
}
// An eye break or a sip of water, once its stretch of active time is up. A due reminder waits for calm; in
// quiet hours it is let go, so the morning does not start with one.
void Monitor::remindWellness(qint64 now) {
    if (!active_) return; // The presence update may have just quit.
    auto &wellness = window_.wellness();
    auto &runtime = window_.stage().runtime();
    auto due = wellness.due(now);
    // Quiet hours and a reminder skipped for today let the interval pass without a word, so the other one can come.
    if (!due.isEmpty() && (Wellness::quietAt(window_.eggs().now()) || nudges_.skippedOn(due, window_.eggs().now().date()))) {
        wellness.given(due, now); due.clear();
    }
    if (due.isEmpty() || (locked && locked()) || nudges_.held(due, now)) return;
    using namespace behavior;
    runtime.submit({"wellness", due, due == "eyes" ? "eye-break" : "water", Policy::Reminder, Lifetime::OneShot,
                    runtime.now() + reminderWaitMs});
}
// Shows a reminder as a question. The go-home reminder sums up the day, when there was agent work to sum up. The wording
// is made here so that a language change can say it again.
void Monitor::ask(const QString &reminder) {
    const bool wellness = reminder == "eyes" || reminder == "water";
    auto text = wellness ? Wellness::note(reminder) : EasterEggs::reminderNote(reminder, window_.eggs().reminderSchedule());
    QString details;
    const auto today = recap_.day(QDate::currentDate());
    if (reminder == "leave-work" && window_.recapEnabled() && today.turns) {
        text += "\n" + Recap::summary(today); details = Recap::breakdown(today);
    }
    reminder_ = reminder;
    note_.ask(text, window_.figure(), window_.screenAreas(), details, int(Nudges::askMs));
}
// A reminder is given when it was answered, skipped, or asked as often as it will be: its interval starts over, or the
// clock reminder is done for today.
void Monitor::give(const QString &reminder) {
    nudges_.forget(reminder);
    if (reminder == "eyes" || reminder == "water") window_.wellness().given(reminder, now_);
    else window_.eggs().reminded(reminder);
}
// The reminder on screen went away without an answer. Ignored ones come back, until they have been asked enough.
void Monitor::released(bool ignored) {
    const auto reminder = std::exchange(reminder_, QString());
    if (reminder.isEmpty()) return;
    if (ignored ? nudges_.ignored(reminder, now_) : (nudges_.later(reminder, now_), false)) give(reminder);
}
// A click on the reminder, or Done, means it was done: a sip or a meal earns a happy reaction at once, an eye break
// after its countdown.
void Monitor::answered() {
    if (restLeft_ > 0) { rest_.stop(); restLeft_ = 0; return; } // Clicking the countdown away ends it.
    const auto reminder = std::exchange(reminder_, QString());
    if (reminder.isEmpty() || !confirmable(reminder)) return;
    give(reminder);
    if (reminder != "eyes") window_.eggs().surprise("reminder-done", true);
    if (reminder != "eyes" || !active_) return;
    restLeft_ = Wellness::eyeRestSeconds + 1;
    rest();
    rest_.start();
}
void Monitor::putOff() { released(false); }
// Skip today: a clock reminder is given for the day anyway; a wellness one is let go until tomorrow.
void Monitor::skipped() {
    const auto reminder = std::exchange(reminder_, QString());
    if (reminder.isEmpty()) return;
    give(reminder);
    nudges_.skipDay(reminder, window_.eggs().now().date());
}
void Monitor::dropReminder() {
    if (reminder_.isEmpty() && restLeft_ == 0) return; // The note may be saying something else.
    note_.hide(); rest_.stop(); restLeft_ = 0;
    released(false); // Not the user's doing: it is asked again later.
}
void Monitor::rest() {
    if (locked && locked()) { dropReminder(); return; }
    if (!active_ || window_.petHidden() || --restLeft_ < 0) { rest_.stop(); restLeft_ = 0; return; }
    if (restLeft_ > 0) {
        //: A countdown; %1 = seconds left
        note_.say(Pet::tr("Eyes on something far away… %1").arg(restLeft_), window_.figure(), window_.screenAreas(), {}, 1500);
        return;
    }
    rest_.stop();
    window_.eggs().surprise("reminder-done", true);
    say(Pet::tr("Nice! Your eyes thank you."), {}, 4000);
}
void Monitor::showRecap() {
    if (!active_) return;
    const auto today = recap_.day(QDate::currentDate());
    // A hidden pet has no bubble to speak from; the tray says it instead.
    if (window_.petHidden()) window_.showTrayMessage(Pet::tr("Today's recap"), Recap::breakdown(today));
    else say(Recap::summary(today), today.turns ? Recap::breakdown(today) : QString());
}
// Monday blues, lunch, the go-home nudge and bedtime: said once each day when calm, and kept for later while the pet is
// hidden or the user is away.
void Monitor::remind() {
    auto &runtime = window_.stage().runtime();
    using namespace behavior;
    // Each due reminder is submitted on its own, so one that stays blocked cannot hide an overlapping one.
    for (const auto &due : window_.eggs().dueReminders())
        if (!nudges_.held(due, now_)) runtime.submit({"clock", due, due, Policy::Reminder, Lifetime::OneShot, runtime.now() + reminderWaitMs});
}
bool Monitor::shown(const Alert &alert) const {
    const int level = window_.bubbles();
    return alert.kind == "attention" || ((alert.kind == "error" || alert.kind == "exhausted") && level >= Preferences::RequestsAndErrors) ||
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
    if (!alert || quiet() || !active_) { bubble_.hide(); return; }
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
    if (session.host.isNull()) return Monitor::tr("This session started before Agent Pet could see its terminal. Its next event will fix that.");
    QStringList text;
    if (result.activation == Outcome::Unsupported) text << Monitor::tr("This desktop session does not let Agent Pet raise windows.");
    else if (result.activation == Outcome::Skipped) text << Monitor::tr("Could not select this session's tab or pane. Check that its terminal is attached.");
    else text << Monitor::tr("Could not focus this session's window. Check that its terminal is attached.");
    return (text + result.requirements).join(' ');
}
bool Monitor::focusSession(const QString &key) {
    const auto it = sessions_.records().find(key);
    if (it == sessions_.records().end()) {
        QToolTip::showText(window_.figure().center(), tr("Could not focus this session's window. Check that its terminal is attached."));
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
    for (const auto *kind : {"attention", "exhausted", "error", "turn_finished"}) sessions_.dismiss(key, kind);
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
