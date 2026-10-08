// The behavior runtime's policy (issue #67), written ahead of the runtime. Each test drives the runtime
// with a hand-set clock and a fake presentation that records requests and answers with feedback, so no
// desktop, Player or artwork is involved. The policy table these tests pin down is in
// docs/superpowers/specs/2026-10-08-behavior-runtime-design.md.
// Cues are #64's (src/animation/cues.json); "fidget" and "edge-left" stand for states from a pet's own
// ambient and touch sections, which are not cues.
#include "behavior/runtime.h"
#include <QTest>

using namespace pet::behavior;

namespace {
QString name(Outcome outcome) {
    switch (outcome) {
    case Outcome::Admitted: return "admitted";
    case Outcome::Started: return "started";
    case Outcome::Completed: return "completed";
    case Outcome::Interrupted: return "interrupted";
    case Outcome::Unavailable: return "unavailable";
    case Outcome::TimedOut: return "timed-out";
    case Outcome::Expired: return "expired";
    case Outcome::Dropped: return "dropped";
    }
    return "?";
}
bool urgent(const QString &cue) { return cue == "attention" || cue == "exhausted" || cue == "error"; }
// Session activity, as the monitor will submit Sessions' aggregate: cues of the "once" shape are moments.
Intent activity(const QString &cue) {
    const bool once = cue == "turn-finished" || cue == "error";
    return {"session", "activity", cue, urgent(cue) ? Policy::Urgent : Policy::Activity,
            once ? Lifetime::Moment : Lifetime::Persistent};
}
Intent oneShot(const QString &source, const QString &key, Policy policy, qint64 expiresAt = 0, QStringList effects = {}) {
    return {source, key, key, policy, Lifetime::OneShot, expiresAt, std::move(effects)};
}
Intent danger() { return oneShot("eggs", "danger", Policy::Surprise); }
Intent konami() { return oneShot("eggs", "konami", Policy::Surprise); }
Intent quit() { return {"lifecycle", "quit", "quit", Policy::Shutdown}; }

// A runtime with a hand-set clock and a recording presentation. The user is present and nothing else
// is going on, so a reminder is free to show unless a test says otherwise.
struct Rig {
    qint64 now = 1700000000000;
    Runtime runtime{[this] { return now; }};
    QVector<Request> requests;
    QHash<QString, QStringList> outcomes; // "source/key" → outcome names, in order.
    QStringList effects;                  // Effects of every instance that started, in order.
    int finished = 0;
    Rig() {
        runtime.present = [this](const Request &request) { requests.append(request); };
        runtime.outcome = [this](const Intent &intent, Outcome outcome) {
            outcomes[intent.source + "/" + intent.key].append(name(outcome));
            if (outcome == Outcome::Started) effects += intent.effects;
        };
        runtime.finished = [this] { ++finished; };
        Context context;
        context.present = true;
        runtime.setContext(context);
    }
    QString history(const QString &identity) const { return outcomes.value(identity).join(' '); }
    QString lastCue() const { return requests.isEmpty() ? QString() : requests.last().cue; }
    quint64 lastInstance() const { return requests.isEmpty() ? 0 : requests.last().instance; }
    // Answers the latest request.
    void answer(Feedback feedback) { runtime.report(lastInstance(), feedback); }
    void play() { answer(Feedback::Started); }
    void change(const std::function<void(Context &)> &edit) {
        auto context = runtime.context();
        edit(context);
        runtime.setContext(context);
    }
};
}

class BehaviorTests : public QObject {
    Q_OBJECT
private slots:
    // --- Session activity ---------------------------------------------------------------------------

    void activityIsPresentedOnceAndRepeatsAreDuplicates() {
        Rig rig;
        QCOMPARE(rig.runtime.submit(activity("working")), Submission::Admitted);
        QCOMPARE(rig.requests.size(), 1);
        QCOMPARE(rig.lastCue(), QString("working"));
        QVERIFY(!rig.requests.last().interrupt);
        // The monitor submits the aggregate on every tick; an unchanged one must not restart the animation.
        QCOMPARE(rig.runtime.submit(activity("working")), Submission::Duplicate);
        QCOMPARE(rig.requests.size(), 1);
        QCOMPARE(rig.runtime.activity(), QString("working"));
        QCOMPARE(rig.runtime.submit(activity("reading")), Submission::Admitted);
        QCOMPARE(rig.lastCue(), QString("reading"));
        QCOMPARE(rig.requests.size(), 2);
    }

    void urgentActivityInterruptsASurprise() {
        Rig rig;
        rig.runtime.submit(activity("working"));
        QCOMPARE(rig.runtime.submit(danger()), Submission::Admitted);
        QCOMPARE(rig.lastCue(), QString("danger"));
        QVERIFY(rig.requests.last().interrupt);
        const auto surprise = rig.lastInstance();
        rig.play();
        QCOMPARE(rig.runtime.submit(activity("attention")), Submission::Admitted);
        QCOMPARE(rig.lastCue(), QString("attention"));
        QVERIFY(rig.requests.last().interrupt);
        QCOMPARE(rig.history("eggs/danger"), QString("admitted started interrupted"));
        // The cut-short surprise's late completion belongs to an old instance and changes nothing.
        rig.runtime.report(surprise, Feedback::Completed);
        QCOMPARE(rig.history("eggs/danger"), QString("admitted started interrupted"));
        QCOMPARE(rig.runtime.currentCue(), QString("attention"));
    }

    void urgentActivityKeepsDiscretionaryReactionsAway() {
        Rig rig;
        rig.runtime.submit(activity("error"));
        QCOMPARE(rig.runtime.submit(danger()), Submission::Rejected);
        QCOMPARE(rig.runtime.submit(oneShot("mood", "snack", Policy::Celebration, rig.now + 5000)), Submission::Rejected);
        QCOMPARE(rig.runtime.submit(oneShot("ambient", "fidget", Policy::Ambient)), Submission::Rejected);
        // A reminder is not lost to an error; it waits until nothing needs the user.
        QCOMPARE(rig.runtime.submit(oneShot("wellness", "eye-break", Policy::Reminder, rig.now + 60000)), Submission::Deferred);
        QCOMPARE(rig.history("wellness/eye-break"), QString());
        QCOMPARE(rig.lastCue(), QString("error"));
        rig.runtime.submit(activity("working"));
        QCOMPARE(rig.history("wellness/eye-break"), QString("admitted"));
        QCOMPARE(rig.lastCue(), QString("eye-break"));
    }

    void activityChangesDuringAReactionShowTheLatestAfterIt() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit(konami());
        rig.play();
        const auto before = rig.requests.size();
        // Not urgent: recorded, but the surprise plays out.
        QCOMPARE(rig.runtime.submit(activity("working")), Submission::Admitted);
        rig.runtime.submit(activity("thinking"));
        QCOMPARE(rig.requests.size(), before);
        QCOMPARE(rig.runtime.activity(), QString("thinking"));
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.history("eggs/konami"), QString("admitted started completed"));
        // Back to what is current now; "working" was obsolete and is not replayed.
        QCOMPARE(rig.requests.size(), before + 1);
        QCOMPARE(rig.lastCue(), QString("thinking"));
        QVERIFY(!rig.requests.last().interrupt);
    }

    // --- Handling (drag, petting, fall) -------------------------------------------------------------

    void handlingHoldsPresentationUntilRelease() {
        Rig rig;
        rig.runtime.submit(activity("working"));
        const auto before = rig.requests.size();
        rig.change([](Context &c) { c.handled = true; });
        rig.runtime.submit(activity("thinking"));
        rig.runtime.submit(activity("attention"));
        rig.runtime.submit(activity("reading"));
        QCOMPARE(rig.requests.size(), before);
        QCOMPARE(rig.runtime.activity(), QString("reading"));
        rig.change([](Context &c) { c.handled = false; });
        // One request, for the activity current at release.
        QCOMPARE(rig.requests.size(), before + 1);
        QCOMPARE(rig.lastCue(), QString("reading"));
    }

    void handlingRefusesSurprisesAndHoldsReminders() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.change([](Context &c) { c.handled = true; });
        QCOMPARE(rig.runtime.submit(danger()), Submission::Rejected);
        QCOMPARE(rig.runtime.submit(oneShot("ambient", "fidget", Policy::Ambient)), Submission::Rejected);
        QCOMPARE(rig.runtime.submit(oneShot("wellness", "water", Policy::Reminder, rig.now + 60000)), Submission::Deferred);
        QCOMPARE(rig.history("wellness/water"), QString());
        rig.change([](Context &c) { c.handled = false; });
        QCOMPARE(rig.history("wellness/water"), QString("admitted"));
        QCOMPARE(rig.lastCue(), QString("water"));
    }

    void shutdownTakesOverWhileHandled() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.change([](Context &c) { c.handled = true; });
        QCOMPARE(rig.runtime.submit(quit()), Submission::Admitted);
        QCOMPARE(rig.lastCue(), QString("quit"));
        QVERIFY(rig.requests.last().interrupt);
    }

    // --- Competition between one-shots --------------------------------------------------------------

    void higherRankInterruptsALowerReaction() {
        Rig rig;
        rig.runtime.submit(activity("turn-finished"));
        QCOMPARE(rig.runtime.submit(oneShot("mood", "snack", Policy::Celebration)), Submission::Admitted);
        rig.play();
        QCOMPARE(rig.runtime.submit(danger()), Submission::Admitted);
        QCOMPARE(rig.lastCue(), QString("danger"));
        QCOMPARE(rig.history("mood/snack"), QString("admitted started interrupted"));
    }

    void equalOrLowerRankWaitsOrIsRejected() {
        Rig rig;
        rig.runtime.submit(activity("turn-finished"));
        rig.runtime.submit(danger());
        rig.play();
        // Equal rank never interrupts; with no time to wait it is refused.
        QCOMPARE(rig.runtime.submit(konami()), Submission::Rejected);
        QCOMPARE(rig.runtime.submit(oneShot("mood", "milestone", Policy::Celebration, rig.now + 5000)), Submission::Deferred);
        QCOMPARE(rig.runtime.deferred(), 1);
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.lastCue(), QString("milestone"));
        QCOMPARE(rig.history("mood/milestone"), QString("admitted"));
        QCOMPARE(rig.runtime.deferred(), 0);
    }

    void waitingIntentsGoByRankThenSubmissionOrder() {
        Rig rig;
        rig.runtime.submit(activity("turn-finished"));
        rig.runtime.submit(danger());
        rig.play();
        const auto later = rig.now + 10000;
        rig.runtime.submit(oneShot("mood", "snack", Policy::Celebration, later));
        rig.runtime.submit(oneShot("eggs", "birthday", Policy::Celebration, later));
        rig.runtime.submit(oneShot("wellness", "water", Policy::Reminder, later)); // Last, but ranks higher.
        QCOMPARE(rig.runtime.deferred(), 3);
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.lastCue(), QString("water"));
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.lastCue(), QString("snack")); // Equal rank: submitted first, shown first.
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.lastCue(), QString("birthday"));
        const auto requests = rig.requests.size();
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.requests.size(), requests); // The finished turn was shown before the surprise.
    }

    void sameIdentityIsNotRestartedOrQueuedTwice() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit(danger());
        rig.play();
        const auto before = rig.requests.size();
        QCOMPARE(rig.runtime.submit(danger()), Submission::Duplicate);
        QCOMPARE(rig.requests.size(), before);
        QCOMPARE(rig.runtime.submit(oneShot("wellness", "eye-break", Policy::Reminder, rig.now + 60000)), Submission::Deferred);
        QCOMPARE(rig.runtime.submit(oneShot("wellness", "eye-break", Policy::Reminder, rig.now + 90000)), Submission::Duplicate);
        QCOMPARE(rig.runtime.deferred(), 1);
        QCOMPARE(rig.history("eggs/danger"), QString("admitted started"));
    }

    void waitingIntentsExpire() {
        Rig rig;
        rig.runtime.submit(activity("turn-finished"));
        rig.runtime.submit(danger());
        rig.play();
        rig.runtime.submit(oneShot("mood", "snack", Policy::Celebration, rig.now + 5000));
        rig.now += 5000;
        rig.runtime.tick();
        QCOMPARE(rig.history("mood/snack"), QString("expired"));
        QCOMPARE(rig.runtime.deferred(), 0);
        const auto requests = rig.requests.size();
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.requests.size(), requests);
        QCOMPARE(rig.runtime.currentCue(), QString("turn-finished"));
    }

    void waitingIsBounded() {
        Rig rig;
        rig.runtime.submit(activity("turn-finished"));
        rig.runtime.submit(danger());
        rig.play();
        for (int i = 0; i < Runtime::maxDeferred; ++i)
            QCOMPARE(rig.runtime.submit(oneShot("mood", QString("treat-%1").arg(i), Policy::Celebration, rig.now + 5000)),
                     Submission::Deferred);
        QCOMPARE(rig.runtime.submit(oneShot("mood", "one-too-many", Policy::Celebration, rig.now + 5000)), Submission::Rejected);
        QCOMPARE(rig.runtime.deferred(), Runtime::maxDeferred);
    }

    void obsoleteCelebrationIsDroppedWhenActivityMovesOn() {
        Rig rig;
        rig.runtime.submit(activity("turn-finished"));
        rig.runtime.submit(danger());
        rig.play();
        rig.runtime.submit(oneShot("mood", "snack", Policy::Celebration, rig.now + 10000));
        rig.runtime.submit(activity("working")); // A new prompt: the finished turn is old news.
        QCOMPARE(rig.history("mood/snack"), QString("dropped"));
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.lastCue(), QString("working"));
    }

    // --- Moments: activity that plays once ---------------------------------------------------------

    void celebrationTakesTheFinishedTurnsPlace() {
        Rig rig;
        rig.runtime.submit(activity("working"));
        // The monitor submits the finished turn, then how Mood celebrates it, in the same tick. Player
        // replaces a "once" state at once, so the turn-finished state never shows.
        rig.runtime.submit(activity("turn-finished"));
        QCOMPARE(rig.lastCue(), QString("turn-finished"));
        QCOMPARE(rig.runtime.submit(oneShot("mood", "celebrate", Policy::Celebration)), Submission::Admitted);
        QCOMPARE(rig.lastCue(), QString("celebrate"));
        QVERIFY(!rig.requests.last().interrupt);
        rig.play();
        const auto requests = rig.requests.size();
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.requests.size(), requests); // Not celebrated twice.
        QCOMPARE(rig.runtime.activity(), QString("turn-finished"));
    }

    void momentDuringAReactionShowsAfterIt() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit(konami());
        rig.play();
        const auto requests = rig.requests.size();
        rig.runtime.submit(activity("turn-finished"));
        QCOMPARE(rig.requests.size(), requests);
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.lastCue(), QString("turn-finished"));
    }

    void waitingCelebrationStandsInForTheMoment() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit(konami());
        rig.play();
        rig.runtime.submit(activity("turn-finished"));
        QCOMPARE(rig.runtime.submit(oneShot("mood", "snack", Policy::Celebration, rig.now + 10000)), Submission::Deferred);
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.lastCue(), QString("snack"));
        rig.play();
        const auto requests = rig.requests.size();
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.requests.size(), requests);
    }

    void unavailableCelebrationLeavesTheMomentToShow() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit(konami());
        rig.play();
        rig.runtime.submit(activity("turn-finished"));
        rig.runtime.submit(oneShot("mood", "milestone", Policy::Celebration, rig.now + 10000));
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.lastCue(), QString("milestone"));
        rig.answer(Feedback::Unavailable); // Never started, so it stood in for nothing.
        QCOMPARE(rig.lastCue(), QString("turn-finished"));
    }

    void momentIsShownOnceAcrossHandling() {
        Rig rig;
        rig.runtime.submit(activity("working"));
        rig.change([](Context &c) { c.handled = true; });
        rig.runtime.submit(activity("error"));
        rig.change([](Context &c) { c.handled = false; });
        QCOMPARE(rig.lastCue(), QString("error"));
        const auto requests = rig.requests.size();
        // Picked up and put down again while the error is still the aggregate: it is not replayed.
        rig.change([](Context &c) { c.handled = true; });
        rig.change([](Context &c) { c.handled = false; });
        QCOMPARE(rig.requests.size(), requests);
        QCOMPARE(rig.runtime.activity(), QString("error"));
    }

    void presentationMayAnswerBeforeItReturns() {
        Rig rig;
        rig.runtime.present = [&rig](const Request &request) {
            rig.requests.append(request);
            if (request.cue == "may20") rig.runtime.report(request.instance, Feedback::Unavailable); // No pool.
        };
        rig.runtime.submit(activity("idle"));
        QCOMPARE(rig.runtime.submit(oneShot("eggs", "may20", Policy::Ambient)), Submission::Admitted);
        QCOMPARE(rig.history("eggs/may20"), QString("admitted unavailable"));
        QCOMPARE(rig.runtime.currentCue(), QString("idle"));
        QCOMPARE(rig.runtime.submit(konami()), Submission::Admitted);
        QCOMPARE(rig.lastCue(), QString("konami"));
    }

    // --- Presentation feedback ----------------------------------------------------------------------

    void unavailableOptionalCueReleasesItsClaim() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        QCOMPARE(rig.runtime.submit(oneShot("eggs", "may20", Policy::Ambient)), Submission::Admitted);
        rig.answer(Feedback::Unavailable);
        QCOMPARE(rig.history("eggs/may20"), QString("admitted unavailable"));
        QCOMPARE(rig.runtime.currentCue(), QString("idle"));
        // Nothing is stuck: the next reaction is admitted at once.
        QCOMPARE(rig.runtime.submit(konami()), Submission::Admitted);
    }

    void staleFeedbackCannotFinishTheReplacement() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit(oneShot("wellness", "water", Policy::Reminder, rig.now + 60000));
        const auto reminder = rig.lastInstance();
        rig.play();
        rig.runtime.submit(danger()); // A surprise outranks a reminder's art.
        QCOMPARE(rig.history("wellness/water"), QString("admitted started interrupted"));
        rig.play();
        rig.runtime.report(reminder, Feedback::Completed);
        rig.runtime.report(reminder, Feedback::Unavailable);
        QCOMPARE(rig.runtime.currentCue(), QString("danger"));
        QCOMPARE(rig.history("eggs/danger"), QString("admitted started"));
        QCOMPARE(rig.history("wellness/water"), QString("admitted started interrupted"));
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.history("eggs/danger"), QString("admitted started completed"));
        QCOMPARE(rig.lastCue(), QString("idle"));
    }

    void silentPresentationTimesOut() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit(danger()); // Never answered, as with a broken pet or a stalled player.
        rig.now += Runtime::oneShotTimeoutMs - 1;
        rig.runtime.tick();
        QCOMPARE(rig.runtime.currentCue(), QString("danger"));
        rig.now += 1;
        rig.runtime.tick();
        QCOMPARE(rig.history("eggs/danger"), QString("admitted timed-out"));
        QCOMPARE(rig.lastCue(), QString("idle"));
        QCOMPARE(rig.runtime.submit(konami()), Submission::Admitted);
    }

    void effectsApplyOnlyWhenTheReactionStarts() {
        Rig rig;
        rig.runtime.submit(activity("turn-finished"));
        rig.runtime.submit(oneShot("mood", "snack", Policy::Celebration, 0, {"water"}));
        QVERIFY(rig.effects.isEmpty()); // Admitted is not shown.
        rig.play();
        QCOMPARE(rig.effects, QStringList{"water"});
        rig.answer(Feedback::Completed);
        rig.effects.clear();
        // A treat the pet has no art for never offered the drink.
        rig.runtime.submit(oneShot("mood", "milestone", Policy::Celebration, 0, {"water"}));
        rig.answer(Feedback::Unavailable);
        QVERIFY(rig.effects.isEmpty());
        // Nor did one that was refused.
        rig.runtime.submit(activity("attention"));
        rig.runtime.submit(oneShot("mood", "snack-again", Policy::Celebration, 0, {"water"}));
        QVERIFY(rig.effects.isEmpty());
    }

    // --- Reminders: art and note admitted together --------------------------------------------------

    void reminderWaitsForCalm_data() {
        QTest::addColumn<int>("condition");
        QTest::newRow("user away") << 0;
        QTest::newRow("hidden") << 1;
        QTest::newRow("muted") << 2;
        QTest::newRow("speaking") << 3;
        QTest::newRow("attention pending") << 4;
        QTest::newRow("handled") << 5;
        QTest::newRow("moving") << 6;
    }
    void reminderWaitsForCalm() {
        QFETCH(int, condition);
        Rig rig;
        rig.runtime.submit(activity("working"));
        auto busy = [condition](Context &c) {
            switch (condition) {
            case 0: c.present = false; break;
            case 1: c.visible = false; break;
            case 2: c.muted = true; break;
            case 3: c.speaking = true; break;
            case 4: c.attention = 1; break;
            case 5: c.handled = true; break;
            case 6: c.moving = true; break;
            }
        };
        rig.change(busy);
        QCOMPARE(rig.runtime.submit(oneShot("wellness", "eye-break", Policy::Reminder, rig.now + 60000)), Submission::Deferred);
        // No Admitted means the monitor shows no note either.
        QCOMPARE(rig.history("wellness/eye-break"), QString());
        rig.change([](Context &c) { c = Context(); c.present = true; });
        QCOMPARE(rig.history("wellness/eye-break"), QString("admitted"));
        QCOMPARE(rig.lastCue(), QString("eye-break"));
        QVERIFY(rig.requests.last().interrupt);
    }

    void quietHoursLetRemindersGo() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.change([](Context &c) { c.quiet = true; });
        QCOMPARE(rig.runtime.submit(oneShot("wellness", "water", Policy::Reminder, rig.now + 60000)), Submission::Rejected);
        QCOMPARE(rig.history("wellness/water"), QString());
        QCOMPARE(rig.lastCue(), QString("idle"));
    }

    void reminderKeepsItsNoteWhenTheArtIsMissing() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        QCOMPARE(rig.runtime.submit(oneShot("eggs", "leave-work", Policy::Reminder, rig.now + 60000)), Submission::Admitted);
        rig.answer(Feedback::Unavailable);
        // Admitted came first, so the note was shown and the reminder counts as given; the art alone failed.
        QCOMPARE(rig.history("eggs/leave-work"), QString("admitted unavailable"));
        QCOMPARE(rig.runtime.currentCue(), QString("idle"));
    }

    void reminderThatNeverGetsItsTurnShowsNoNote() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.change([](Context &c) { c.speaking = true; });
        rig.runtime.submit(oneShot("wellness", "water", Policy::Reminder, rig.now + 60000));
        rig.now += 60000;
        rig.runtime.tick();
        rig.change([](Context &c) { c.speaking = false; });
        QCOMPARE(rig.history("wellness/water"), QString("expired"));
        QCOMPARE(rig.lastCue(), QString("idle"));
    }

    void reminderInterruptsAFidget() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit(oneShot("ambient", "fidget", Policy::Ambient));
        rig.play();
        QCOMPARE(rig.runtime.submit(oneShot("wellness", "eye-break", Policy::Reminder, rig.now + 60000)), Submission::Admitted);
        QCOMPARE(rig.history("ambient/fidget"), QString("admitted started interrupted"));
        QCOMPARE(rig.lastCue(), QString("eye-break"));
    }

    // --- Ambient rest -------------------------------------------------------------------------------

    void ambientRunsOnlyWhenIdleAndInView() {
        Rig rig;
        rig.runtime.submit(activity("working"));
        QCOMPARE(rig.runtime.submit(oneShot("ambient", "fidget", Policy::Ambient)), Submission::Rejected);
        rig.runtime.submit(activity("waiting")); // Waiting looks idle.
        QCOMPARE(rig.runtime.submit(oneShot("ambient", "fidget", Policy::Ambient)), Submission::Admitted);
        rig.answer(Feedback::Completed);
        rig.change([](Context &c) { c.visible = false; });
        QCOMPARE(rig.runtime.submit(oneShot("ambient", "fidget", Policy::Ambient)), Submission::Rejected);
    }

    void ambientNeverInterruptsAReaction() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit(konami());
        QCOMPARE(rig.runtime.submit(oneShot("ambient", "fidget", Policy::Ambient)), Submission::Rejected);
        QCOMPARE(rig.lastCue(), QString("konami"));
    }

    void restEndsWithActivityAndIsNotReplayed() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        const Intent nap{"ambient", "nap", "nap", Policy::Ambient, Lifetime::Persistent};
        QCOMPARE(rig.runtime.submit(nap), Submission::Admitted);
        QCOMPARE(rig.lastCue(), QString("nap"));
        rig.runtime.submit(activity("working"));
        QCOMPARE(rig.history("ambient/nap"), QString("admitted interrupted"));
        QCOMPARE(rig.lastCue(), QString("working"));
        rig.runtime.submit(activity("idle"));
        QCOMPARE(rig.lastCue(), QString("idle"));
    }

    void withdrawnRestReturnsToActivity() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit({"touch", "edge", "edge-left", Policy::Ambient, Lifetime::Persistent});
        QCOMPARE(rig.lastCue(), QString("edge-left"));
        rig.runtime.withdraw("touch", "edge"); // Dragged back out from behind the screen edge.
        QCOMPARE(rig.history("touch/edge"), QString("admitted interrupted"));
        QCOMPARE(rig.lastCue(), QString("idle"));
        rig.runtime.withdraw("touch", "edge"); // Unknown now: ignored.
        QCOMPARE(rig.history("touch/edge"), QString("admitted interrupted"));
    }

    // --- Lifecycle ----------------------------------------------------------------------------------

    void startupYieldsOnlyToUrgentActivity() {
        Rig rig;
        QCOMPARE(rig.runtime.submit({"lifecycle", "start", "start", Policy::Startup}), Submission::Admitted);
        QVERIFY(rig.requests.last().interrupt);
        rig.play();
        rig.runtime.submit(activity("working"));
        QCOMPARE(rig.runtime.submit(danger()), Submission::Rejected);
        QCOMPARE(rig.lastCue(), QString("start"));
        rig.runtime.submit(activity("attention")); // A request restored from before an update.
        QCOMPARE(rig.lastCue(), QString("attention"));
        QCOMPARE(rig.history("lifecycle/start"), QString("admitted started interrupted"));
    }

    void startupCompletesIntoTheLatestActivity() {
        Rig rig;
        rig.runtime.submit({"lifecycle", "start", "start", Policy::Startup});
        rig.play();
        rig.runtime.submit(activity("working"));
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.lastCue(), QString("working"));
    }

    void shutdownCancelsEverythingAndFinishesOnce() {
        Rig rig;
        rig.runtime.submit(activity("turn-finished"));
        rig.runtime.submit(danger());
        rig.play();
        rig.runtime.submit(oneShot("mood", "snack", Policy::Celebration, rig.now + 5000));
        QCOMPARE(rig.runtime.submit(quit()), Submission::Admitted);
        QVERIFY(rig.runtime.closing());
        QCOMPARE(rig.history("eggs/danger"), QString("admitted started interrupted"));
        QCOMPARE(rig.history("mood/snack"), QString("dropped"));
        QCOMPARE(rig.runtime.deferred(), 0);
        QCOMPARE(rig.lastCue(), QString("quit"));
        QVERIFY(rig.requests.last().interrupt);
        const auto requests = rig.requests.size();
        // Nothing else gets in, not even urgent activity or a second quit.
        QCOMPARE(rig.runtime.submit(activity("attention")), Submission::Rejected);
        QCOMPARE(rig.runtime.submit(konami()), Submission::Rejected);
        QCOMPARE(rig.runtime.submit({"lifecycle", "quit", "annoyed", Policy::Shutdown}), Submission::Duplicate);
        QCOMPARE(rig.requests.size(), requests);
        QCOMPARE(rig.finished, 0);
        rig.play();
        rig.answer(Feedback::Completed);
        QCOMPARE(rig.finished, 1);
        rig.answer(Feedback::Completed);
        rig.runtime.tick();
        QCOMPARE(rig.finished, 1);
    }

    void shutdownFinishesWhenItsCueIsUnavailable() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit({"lifecycle", "quit", "annoyed", Policy::Shutdown});
        rig.answer(Feedback::Unavailable);
        QCOMPARE(rig.finished, 1);
    }

    void shutdownFinishesWithoutFeedback() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.runtime.submit(quit());
        rig.play(); // Started, then nothing: the closing animation stalled.
        rig.now += Runtime::shutdownTimeoutMs - 1;
        rig.runtime.tick();
        QCOMPARE(rig.finished, 0);
        rig.now += 1;
        rig.runtime.tick();
        QCOMPARE(rig.finished, 1);
        QCOMPARE(rig.history("lifecycle/quit"), QString("admitted started timed-out"));
    }

    void shutdownOfAHiddenPetFinishesAtOnce() {
        Rig rig;
        rig.runtime.submit(activity("idle"));
        rig.change([](Context &c) { c.visible = false; });
        const auto requests = rig.requests.size();
        rig.runtime.submit(quit());
        QCOMPARE(rig.finished, 1); // No one would see the closing animation.
        QCOMPARE(rig.requests.size(), requests);
    }

    void shutdownFinishesWithoutAPresentation() {
        Rig rig;
        rig.runtime.present = nullptr; // Nobody to hand the cue to: the request times out like any other.
        rig.runtime.submit(quit());
        QCOMPARE(rig.finished, 0);
        rig.now += Runtime::shutdownTimeoutMs;
        rig.runtime.tick();
        QCOMPARE(rig.finished, 1);
    }
};
QTEST_GUILESS_MAIN(BehaviorTests)
#include "behavior_tests.moc"
