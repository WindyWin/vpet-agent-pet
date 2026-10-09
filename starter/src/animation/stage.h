#pragma once
#include "behavior/runtime.h"
#include "player.h"
#include <QObject>
#include <QTimer>
#include <functional>
#include <optional>

namespace pet {
// Shows what the behavior runtime chose on a Player and reports back how it went (docs/adr/0031-behavior-runtime.md).
// It owns the runtime: producers submit through runtime() and hear back through outcome() and finished().
//
// A request plays its producer's drawn state, else the state a state cue maps to, else what the pet's rule table
// draws for a trigger of that name (a reminder's art), else a state of the pet's own (a fidget, a move, an edge);
// with none of these it is unavailable.
// The state's entry starts it; its end, or a state ending by itself into the next one, completes it; any other
// state entered interrupts it. The pet being held or let go is the runtime's `handled` context.
class Stage : public QObject {
    Q_OBJECT
public:
    // Time to read the pet's complaint between the `annoyed` and `quit-angry` cues.
    static constexpr int annoyedPauseMs = 2250;
    // `clock` is the runtime's, in milliseconds; unset, the system clock.
    explicit Stage(Player &player, std::function<qint64()> clock = {}, QObject *parent = nullptr);
    behavior::Runtime &runtime() { return runtime_; }
    // Changes the shared context; the runtime acts on it at once.
    void update(const std::function<void(behavior::Context &)> &change);
    // Draws from the rule table when the producer did not; replaceable for tests.
    void setRandom(Random random) { random_ = random ? std::move(random) : systemRandom(); }
signals:
    void outcome(const pet::behavior::Intent &intent, pet::behavior::Outcome outcome);
    void finished(); // Shutdown is done; the app may quit.
private:
    void present(const behavior::Request &request);
    bool show(const QString &state, bool interrupt);
    void entered(const QString &state);
    void completed(const QString &state);
    void report(behavior::Feedback feedback);
    Player &player_;
    behavior::Runtime runtime_;
    Random random_ = systemRandom();
    QTimer pause_;
    quint64 instance_ = 0; // The latest request, until it is done; 0 for none.
    QString cue_, state_;  // Its cue and the state it selected.
    bool started_ = false, entering_ = false; // `entering_`: inside the player's `entered`, which must not select.
    std::optional<behavior::Request> later_;  // A request made while entering, presented right after.
};
}
