#include "ambient.h"
#include "stage.h"
#include <QDateTime>

namespace pet {
Ambient::Ambient(Player &player, QObject *parent)
    : QObject(parent), player_(player), clock_([] { return QDateTime::currentMSecsSinceEpoch(); }) {
    connect(&player_, &Player::entered, this, &Ambient::entered);
    connect(&player_, &Player::looped, this, &Ambient::looped);
    player_.setVariants(level_ != AmbientLevel::Off);
}
QPair<int, int> Ambient::gapSeconds(AmbientLevel level) {
    return level == AmbientLevel::Lively ? QPair<int, int>{15, 25} : QPair<int, int>{45, 90};
}
void Ambient::setLevel(AmbientLevel level) {
    if (level == level_) return;
    level_ = level;
    player_.setVariants(level != AmbientLevel::Off);
    if (idleSince_ >= 0 && level_ != AmbientLevel::Off) nextDue_ = clock_() + gapMs(); // A new pace starts from now.
}
void Ambient::setClock(std::function<qint64()> milliseconds) {
    if (milliseconds) clock_ = std::move(milliseconds);
}
qint64 Ambient::gapMs() {
    const auto [low, high] = gapSeconds(level_);
    return qint64(low + qBound(0, random_(high - low + 1), high - low)) * 1000;
}
void Ambient::entered(const QString &state) {
    if (state == "idle") {
        // Idle after a fidget keeps the clock, so the tiers and the nap still count up.
        fidgeting_ = asleep_ = false; special_.clear();
        if (idleSince_ < 0) idleSince_ = clock_();
        if (level_ != AmbientLevel::Off) nextDue_ = clock_() + gapMs();
    } else if (player_.isFidget(state) || (!special_.isEmpty() && state == special_)) {
        fidgeting_ = true; asleep_ = false;
    } else {
        // Anything else is real activity, or a nap this class chose to start.
        idleSince_ = -1; fidgeting_ = false; special_.clear();
        asleep_ = napping_ && state == player_.stateFor("nap");
    }
}
void Ambient::looped(const QString &state) {
    using namespace behavior;
    if (state != "idle" || !stage_ || level_ == AmbientLevel::Off || idleSince_ < 0 || player_.held() || player_.stopped())
        return;
    const auto now = clock_(), idle = now - idleSince_;
    if (player_.sleepAfterS() > 0 && idle >= qint64(player_.sleepAfterS()) * 1000
        && player_.states().contains(player_.stateFor("nap"))) {
        // A rest: it lasts until session activity, or anything else, wakes the pet.
        napping_ = true;
        stage_->runtime().submit({"ambient", "nap", "nap", Policy::Ambient, Lifetime::Persistent});
        napping_ = false;
        return;
    }
    if (now < nextDue_) return;
    const auto special = eggs_ ? eggs_->fidget() : QString();
    const auto fidget = special.isEmpty() ? pick(idle) : special;
    special_ = special; // Before submitting: entering it must count as a fidget.
    // Fidgets are the pet's own states, not cues; the runtime decides whether one may play now.
    if (fidget.isEmpty()
        || stage_->runtime().submit({"ambient", "fidget", fidget, Policy::Ambient, Lifetime::OneShot, 0, {}, fidget}) != Submission::Admitted) {
        special_.clear(); nextDue_ = now + gapMs(); return;
    }
    last_ = fidget;
}
QString Ambient::pick(qint64 idleMs) {
    QVector<const Fidget *> common, rare;
    for (const auto &fidget : player_.fidgets()) {
        const auto *move = player_.move(fidget.state);
        if (idleMs < qint64(fidget.minIdleS) * 1000 || (move && !(moveGate_ && moveGate_(*move)))) continue;
        (fidget.rare ? rare : common).append(&fidget);
    }
    // Never the same fidget twice in a row, unless it is all there is.
    for (auto *pool : {&common, &rare})
        if (pool->size() > 1) pool->removeIf([this](const Fidget *fidget) { return fidget->state == last_; });
    const bool wantRare = !rare.isEmpty() && random_(rareOneIn) == 0;
    const auto &pool = wantRare ? rare : common;
    if (pool.isEmpty()) return {};
    int total = 0;
    for (const auto *fidget : pool) total += fidget->weight;
    int roll = qBound(0, random_(total), total - 1);
    for (const auto *fidget : pool) {
        if (roll < fidget->weight) return fidget->state;
        roll -= fidget->weight;
    }
    return pool.first()->state;
}
}
