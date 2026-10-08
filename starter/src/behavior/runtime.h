#pragma once
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <optional>

// The behavior runtime (issue #67): one place that decides which of the pet's competing behaviors may
// show. Producers (session activity, lifecycle, mood, easter eggs, wellness, ambient, touch) submit
// semantic intents; the runtime admits, defers, interrupts and expires them against the policy table in
// docs/superpowers/specs/2026-10-08-behavior-runtime-design.md and hands one cue at a time to
// presentation, which maps it to the active pet's animation (#64, docs/adr/0029-cues.md) and reports back
// how it went.
// Qt Core only and timer-free: the monitor's tick drives time, and tests drive it by hand
// (tests/behavior_tests.cpp). animation/stage.h connects it to the Player.
namespace pet::behavior {

// Centrally defined policy classes, from the highest rank down. Producers pick the class that says what
// their intent is; they never pick a number. A higher class may interrupt a lower one where the policy
// table allows it.
enum class Policy {
    Shutdown,    // quit, annoyed (then quit-angry): cancels everything else and always finishes
    Urgent,      // session activity that needs the user: attention, exhausted, error
    Surprise,    // danger, konami, reminder-done: a reaction to something that just happened
    Reminder,    // eye-break, water, monday, leave-work, sleep: art with a note, shown only when calm
    Celebration, // how a finished turn is celebrated: celebrate, snack, milestone, long-turn, birthday, friday-evening
    Startup,     // start: the pet arriving, until it is done or any session activity arrives
    Activity,    // other session activity: working, reading, thinking, turn-finished, waiting, idle, inactive
    Ambient,     // nap, occasion fidgets (may20, birthday, late-night), the pet's fidgets and moves, edge hiding
};

enum class Lifetime {
    // Valid until withdrawn or replaced by a submission with the same identity, and shown again whenever
    // presentation comes back to it: session activity that loops (working, attention, idle, …) and rests.
    Persistent,
    // Session activity whose cue plays once (turn-finished, error): current until replaced, so it still
    // governs admission, but presented at most once. Coming back to it after a reaction or a hold shows
    // nothing new; presentation has already moved on by itself. A Celebration that starts while it is
    // current and unshown stands in for it.
    Moment,
    // Plays once, then gives presentation back. Ends on completion, interruption, unavailability or timeout.
    OneShot,
};

struct Intent {
    QString source;   // The producer: "session", "lifecycle", "mood", "eggs", "wellness", "ambient", "touch"
    QString key;      // Stable within the source; source + key is the intent's identity for deduplication.
    // A cue from src/animation/cues.json, never an animation name. Ambient and touch intents may instead
    // name a state from the pet's own ambient, move or touch sections (fidgets, moves, edges), which
    // belong to each pet rather than to the cue vocabulary.
    QString cue;
    Policy policy = Policy::Activity;
    Lifetime lifetime = Lifetime::OneShot;
    // A one-shot that cannot run at once may wait until this time (ms, the runtime's clock); 0 or a past
    // time means now or never.
    qint64 expiresAt = 0;
    // Semantic side effects, such as "water" for a snack that offers a drink. Reported with Started only,
    // so a denied or unavailable reaction never counts as shown.
    QStringList effects;
    // The state a producer already drew from the cue's pool, handed to presentation as is. The runtime never
    // reads it; empty lets presentation resolve the cue itself.
    QString state;
};

// What submit() decided at once.
enum class Submission {
    Admitted,  // Claimed presentation now (or, for session activity or a rest, became the current one).
    Deferred,  // Waiting for its turn, until it expires.
    Duplicate, // The same identity is already waiting or showing, or a persistent intent did not change.
    Rejected,  // Not allowed now and not allowed to wait.
};

// Everything that happens to an intent instance, in order, through `outcome`. An instance reports
// Admitted at most once and exactly one terminal outcome (anything but Admitted and Started) after it,
// or Expired/Dropped without ever being admitted.
enum class Outcome {
    Admitted,    // It may run: a reminder shows its note now, and its interval starts over.
    Started,     // Presentation began playing it; its effects apply.
    Completed,   // It played to its end.
    Interrupted, // Something with the right to interrupt took over, or a persistent intent was withdrawn.
    Unavailable, // The active pet has nothing to show for this optional cue.
    TimedOut,    // Presentation gave no terminal feedback in time.
    Expired,     // It waited past `expiresAt` without being admitted.
    Dropped,     // Cancelled unseen: shutdown, urgent activity, an obsolete celebration or a full queue.
};

// Feedback from presentation about the request it was handed.
enum class Feedback { Started, Completed, Interrupted, Unavailable };

// One cue for presentation to show. `instance` correlates feedback; a report for any other instance than
// the latest request is stale and ignored.
struct Request {
    quint64 instance = 0;
    QString cue, state; // `state`: the intent's own, when its producer drew one.
    // Cut what is showing instead of letting it finish its exit (Player::select's interrupt).
    bool interrupt = false;
};

// Shared state the runtime admits against. Producers stop asking each other; whoever owns a fact sets it here.
struct Context {
    bool visible = true;   // The pet is on screen, not hidden to the tray.
    bool handled = false;  // Held by the user (dragged or petted) or falling after a throw.
    bool moving = false;   // Walking, climbing or sliding to an edge.
    bool present = false;  // The user was seen recently enough to read a reminder.
    bool muted = false;    // Notes and bubbles are off.
    bool speaking = false; // An alert bubble or a note is showing.
    int attention = 0;     // Sessions with an unresolved request for the user.
};

class Runtime {
public:
    // Presentation that reports nothing within this time is taken to have finished.
    static constexpr qint64 oneShotTimeoutMs = 15000; // EasterEggs::surpriseMs today.
    static constexpr qint64 shutdownTimeoutMs = 10000; // annoyed, a pause to read the remark, then quit-angry.
    static constexpr int maxDeferred = 4;            // Waiting one-shots, all sources together.

    // `clock` gives milliseconds; tests drive it by hand.
    explicit Runtime(std::function<qint64()> clock);

    Submission submit(const Intent &intent);
    // Ends a persistent intent, or a one-shot that is still waiting. Its outcome is Interrupted when it was
    // showing and Dropped when it was waiting; unknown identities are ignored.
    void withdraw(const QString &source, const QString &key);
    // Re-evaluates admission, so a waiting reminder can start once things calm down, and the latest activity
    // shows once the user lets go of the pet.
    void setContext(const Context &context);
    Context context() const { return context_; }
    // Feedback on the latest request. For a one-shot it ends or starts the instance. For session activity,
    // Interrupted or Completed means presentation moved off it on its own (a developer selection, a phased
    // state ending): it is shown again on the next tick, unless it is a moment. A rest that is interrupted ends.
    void report(quint64 instance, Feedback feedback);
    // Expires waiting intents, times out silent presentation and shows the activity again where presentation
    // left it; the monitor calls it on its timer.
    void tick();
    qint64 now() const { return clock_(); }

    // The latest session activity cue, whether or not it is what shows; empty before the first.
    QString activity() const;
    // The instance that holds presentation, if any (a one-shot, a rest or the activity).
    std::optional<quint64> current() const;
    QString currentCue() const;
    // The one-shot holding presentation, or null.
    const Intent *showing() const { return showing_ ? &showing_->intent : nullptr; }
    // Whether submit() would admit this intent now, without submitting it.
    bool admits(const Intent &intent) const;
    int deferred() const { return int(waiting_.size()); }
    bool closing() const { return closing_; } // Shutdown was submitted.

    // Hands a cue to presentation. It may report() before it returns, as for an optional cue the pet has
    // no pool for. Unset, nothing is presented, and every request times out.
    std::function<void(const Request &)> present;
    std::function<void(const Intent &, Outcome)> outcome;
    // Shutdown is complete: its presentation finished, failed, timed out or was not needed (hidden pet).
    // Called exactly once.
    std::function<void()> finished;

private:
    struct Entry {
        Intent intent;
        quint64 instance = 0, sequence = 0; // `sequence`: submission order, the tie-breaker.
        qint64 requestedAt = -1;            // When presentation was handed it; -1 before that.
        bool started = false;
    };
    // Whether the class and the context let an intent run now, let it wait, or refuse it.
    enum class Gate { Now, Wait, Never };
    Gate gate(const Intent &intent) const;
    bool outranks(const Intent &intent) const; // Nothing shows, or it ranks strictly above what does.
    bool urgent() const;
    bool idleLike() const;
    void tell(const Intent &intent, Outcome outcome);
    Entry make(const Intent &intent);
    void admit(Entry entry);             // Takes presentation, interrupting the one-shot showing.
    void request(Entry &entry, bool interrupt);
    void end(Outcome outcome);           // Ends the showing one-shot and gives presentation back.
    void resolve(bool returning = false); // Admits waiting work, else shows the rest or the activity.
    void finish();
    std::function<qint64()> clock_;
    Context context_;
    std::optional<Entry> activity_, rest_, showing_; // `showing_`: the one-shot holding presentation.
    QVector<Entry> waiting_;
    quint64 nextInstance_ = 1, nextSequence_ = 1, presented_ = 0; // `presented_`: the latest request's instance.
    bool closing_ = false, finished_ = false, momentShown_ = false; // `momentShown_`: the activity, if a moment.
};
}
