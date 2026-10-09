#include "stage.h"
#include <QDateTime>
#include <utility>

namespace pet {
using behavior::Feedback;

Stage::Stage(Player &player, std::function<qint64()> clock, QObject *parent)
    : QObject(parent), player_(player),
      runtime_(clock ? std::move(clock) : std::function<qint64()>([] { return QDateTime::currentMSecsSinceEpoch(); })) {
    runtime_.present = [this](const behavior::Request &request) { present(request); };
    runtime_.outcome = [this](const behavior::Intent &intent, behavior::Outcome what) { emit outcome(intent, what); };
    runtime_.finished = [this] { emit finished(); };
    pause_.setSingleShot(true);
    pause_.setInterval(annoyedPauseMs);
    connect(&pause_, &QTimer::timeout, this, [this] {
        cue_ = "quit-angry";
        if (!show(player_.stateFor(cue_), true)) report(Feedback::Unavailable);
    });
    connect(&player_, &Player::entered, this, &Stage::entered);
    connect(&player_, &Player::completed, this, &Stage::completed);
    connect(&player_, &Player::heldChanged, this, [this](bool held) { update([held](behavior::Context &c) { c.handled = held; }); });
    update([this](behavior::Context &c) { c.handled = player_.held(); });
}
void Stage::update(const std::function<void(behavior::Context &)> &change) {
    auto context = runtime_.context();
    change(context);
    runtime_.setContext(context);
}
void Stage::report(Feedback feedback) {
    const auto instance = std::exchange(instance_, 0);
    pause_.stop();
    if (instance) runtime_.report(instance, feedback);
}
bool Stage::show(const QString &state, bool interrupt) {
    if (state.isEmpty()) return false;
    state_ = state;
    started_ = false;
    const auto instance = instance_;
    if (!player_.select(state, interrupt)) return false;
    // Already showing it, so no entry will say so.
    if (instance_ == instance && !started_ && !player_.held() && player_.state() == state && player_.requestedState() == state) {
        started_ = true;
        runtime_.report(instance, Feedback::Started);
    }
    return true;
}
void Stage::present(const behavior::Request &request) {
    later_.reset();
    if (entering_) {
        // The player is still entering a state; it hears the request as soon as it is done.
        later_ = request;
        QTimer::singleShot(0, this, [this] { if (auto request = std::exchange(later_, std::nullopt)) present(*request); });
        return;
    }
    pause_.stop();
    instance_ = request.instance;
    cue_ = request.cue;
    auto state = request.state;
    if (state.isEmpty()) state = player_.stateFor(request.cue);
    // A reminder names its trigger: the pet's rule table chooses its art (a pet without lunch art shares its snack or
    // cheer, as triggers.json says).
    if (state.isEmpty())
        if (const auto reaction = player_.rules().draw(request.cue, random_)) state = reaction->state;
    if (state.isEmpty() && player_.states().contains(request.cue)) state = request.cue;
    // An idle pet's own decoration (a fidget, a walk, a touch reaction such as hiding at an edge) already shows
    // idle, wherever it came from: it is left to finish.
    const auto showing = player_.requestedState();
    if (state == player_.stateFor("idle") && showing != state
        && (player_.isFidget(showing) || player_.isTouch(showing))) {
        state_ = showing;
        started_ = true;
        return;
    }
    if (request.cue == "annoyed") {
        if (show(state, request.interrupt)) return;
        // Pestered into leaving, the pet can still storm off without its complaint.
        cue_ = "quit-angry";
        state = player_.stateFor(cue_);
    }
    if (!show(state, request.interrupt)) report(Feedback::Unavailable);
}
void Stage::entered(const QString &state) {
    if (!instance_) return;
    if (state == state_) {
        if (!started_) { started_ = true; runtime_.report(instance_, Feedback::Started); }
        return;
    }
    if (player_.finishing()) return; // It ended by itself; `completed` follows.
    // Something else took over. Whatever the runtime shows next waits until the player has entered it.
    entering_ = true;
    report(Feedback::Interrupted);
    entering_ = false;
}
void Stage::completed(const QString &state) {
    if (!instance_ || state != state_) return;
    if (cue_ == "annoyed") { pause_.start(); return; } // Then quit-angry, after a moment to read the remark.
    report(Feedback::Completed);
}
}
