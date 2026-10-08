#include "player.h"
#include "pet_library.h"
#include <QDebug>
#include <QImageReader>
#include <algorithm>
#include <utility>

namespace pet {
Player::Player(QObject *parent) : QObject(parent) {
    auto &library = PetLibrary::shared();
    QString error;
    if (library.active().isEmpty() && !library.activate("vpet", &error)) {
        qWarning().noquote() << "Cannot load pet vpet:" << error;
        error = tr("Cannot load the artwork pack. Reinstall Agent Pet to restore it.");
    }
    open(library.catalog(), error);
}
Player::Player(QObject *parent, const QString &root, const QString &pet) : QObject(parent) {
    QString error;
    const auto catalog = Catalog::load(root, pet, &error);
    open(catalog, error);
}
Player::Player(QObject *parent, const Catalog &catalog) : QObject(parent) {
    open(catalog, "Empty animation catalog.");
}
void Player::open(const Catalog &catalog, const QString &error) {
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &Player::advance);
    if (!catalog.valid()) { fail(error); return; }
    catalog_ = catalog;
    // The first idle is always the catalog's own entry; variants start with the second pass.
    variants_ = false; enter("idle"); variants_ = true;
}
const Move *Player::move(const QString &state) const {
    const auto found = catalog_.moves.constFind(state);
    return found == catalog_.moves.constEnd() ? nullptr : &*found;
}
const ActivityArt *Player::activity(const QString &state) const {
    const auto found = catalog_.activity.constFind(state);
    return found == catalog_.activity.constEnd() ? nullptr : &*found;
}
void Player::finish() {
    if (stopped_ || held_ || catalog_.animations.value(state_).mode != "phased" || phase_ == 2) return;
    beginEnd();
}
QString Player::phase() const {
    if (stopped_) return "stopped";
    if (!catalog_.animations.contains(state_)) return "unavailable";
    if (decoration_ == Decoration::Handover) return "handover";
    if (decoration_ != Decoration::None) return "linger";
    if (catalog_.animations[state_].mode != "phased") return catalog_.animations[state_].mode;
    return QStringList{"start", "loop", "end"}.at(phase_);
}
QString Player::resumeTarget() const {
    if (held_) return holdResume_;
    if (!pending_.isEmpty()) return pending_;
    // Decoration is never worth coming back to, nor is a reaction to being handled: a pet dragged
    // out of its hiding place stays out.
    if (isFidget(state_) || isTouch(state_)) return "idle";
    return catalog_.animations.value(state_).mode == "once" ? previous_ : state_;
}
bool Player::select(const QString &state, bool interrupt) {
    if (!catalog_.animations.contains(state)) {
        error_ = "Unknown animation state: " + state;
        emit failed(error_); return false;
    }
    if (held_ && state != state_) {
        holdResume_ = state;
        return true;
    }
    if (state == state_ && pending_.isEmpty() && !stopped_) return true;
    // A fidget is decoration: anything else replaces it at once instead of waiting out its exit.
    if (isFidget(state_)) interrupt = true;
    // Update a queued transition without restarting the outgoing exit sequence.
    if (!interrupt && catalog_.animations.value(state_).mode == "phased" && !stopped_) {
        if (decorate(state)) return true;
        pending_ = state;
        if (phase_ != 2) beginEnd();
        return true;
    }
    const auto resume = resumeTarget();
    if (catalog_.animations[state].mode == "once" && state != state_) previous_ = resume;
    pending_.clear();
    enter(state);
    return true;
}
bool Player::play(const QString &cue, bool interrupt) {
    const auto state = stateFor(cue);
    if (state.isEmpty()) {
        error_ = "Unknown cue: " + cue;
        emit failed(error_); return false;
    }
    return select(state, interrupt);
}
// A non-urgent change from an activity's loop phase that can stay at the desk; false plays the usual end.
bool Player::decorate(const QString &target) {
    const auto found = catalog_.activity.constFind(state_);
    if (!continuity_ || phase_ != 1 || found == catalog_.activity.constEnd()) return false;
    const auto &art = *found;
    if (decoration_ != Decoration::None && target == pending_) return true; // Already on its way there.
    if (decoration_ == Decoration::Handover) {
        // Changing course mid-swap: it lands first, then acts on the latest request from that desk.
        const auto landing = catalog_.activity.constFind(handoverTo_);
        const bool desk = target == handoverTo_ || (landing != catalog_.activity.constEnd()
            && (landing->handover.contains(target) || landing->linger.to == target));
        if (desk) pending_ = target;
        return desk;
    }
    const bool lingering = decoration_ == Decoration::LingerIn || decoration_ == Decoration::Linger
        || decoration_ == Decoration::LingerOut;
    if (decoration_ == Decoration::None && !art.linger.to.isEmpty() && target == art.linger.to) {
        pending_ = target; lingerMs_ = 0; lingerLast_.clear();
        decoration_ = art.linger.in.isEmpty() ? Decoration::Linger : Decoration::LingerIn;
        enterSequence(1, art.linger.in.isEmpty() ? drawLinger() : art.linger.in);
        return true;
    }
    if (lingering && (target == state_ || art.handover.contains(target))) {
        // Back to work, or on to the other prop; a playing out decides what follows it.
        pending_ = target == state_ ? QString() : target;
        if (decoration_ == Decoration::LingerOut) return true;
        if (art.linger.out.isEmpty()) leaveLinger();
        else { decoration_ = Decoration::LingerOut; enterSequence(1, art.linger.out); }
        return true;
    }
    if (decoration_ == Decoration::None && art.handover.contains(target)) {
        pending_ = handoverTo_ = target; decoration_ = Decoration::Handover;
        enterSequence(1, art.handover.value(target));
        return true;
    }
    return false;
}
// A handover or a linger sequence just ended.
void Player::decorationEnded() {
    const auto &art = catalog_.activity[state_];
    if (decoration_ == Decoration::Handover) {
        const auto target = std::exchange(handoverTo_, {}), next = std::exchange(pending_, {});
        enter(target, 1);
        if (next != target) select(next); // The request moved on while the props swapped.
        return;
    }
    if (decoration_ == Decoration::LingerIn || (decoration_ == Decoration::Linger && lingerMs_ < art.linger.maxMs)) {
        decoration_ = Decoration::Linger;
        enterSequence(1, drawLinger());
        return;
    }
    if (decoration_ == Decoration::Linger && !art.linger.out.isEmpty()) {
        decoration_ = Decoration::LingerOut;
        enterSequence(1, art.linger.out);
        return;
    }
    leaveLinger();
}
// After a linger: back to the loop, on to a handover, or the usual end and the pending state.
void Player::leaveLinger() {
    const auto &art = catalog_.activity[state_];
    if (pending_.isEmpty()) { decoration_ = Decoration::None; enterSequence(1); }
    else if (art.handover.contains(pending_)) {
        decoration_ = Decoration::Handover; handoverTo_ = pending_;
        enterSequence(1, art.handover.value(pending_));
    } else beginEnd();
}
// The end phase, or a reaction drawn for leaving to the pending state when the gate allows one.
void Player::beginEnd() {
    decoration_ = Decoration::None;
    QString farewell;
    const auto found = catalog_.activity.constFind(state_);
    if (found != catalog_.activity.constEnd() && reactionGate_)
        if (const auto pool = found->exit.value(pending_); !pool.isEmpty() && reactionGate_()) farewell = drawChoice(pool);
    enterSequence(2, farewell);
}
QString Player::drawChoice(const QVector<ActivityChoice> &pool) {
    int total = 0;
    for (const auto &choice : pool) total += choice.weight;
    int roll = pool.size() == 1 ? 0 : qBound(0, random_(total), total - 1);
    for (const auto &choice : pool) {
        if (roll < choice.weight) return choice.sequence;
        roll -= choice.weight;
    }
    return pool.first().sequence;
}
// Linger passes are drawn evenly, never the same one twice in a row unless it is the only one.
QString Player::drawLinger() {
    auto loop = catalog_.activity[state_].linger.loop;
    if (loop.size() > 1) loop.removeAll(lingerLast_);
    lingerLast_ = loop.size() == 1 ? loop.first() : loop.at(qBound(0, random_(int(loop.size())), int(loop.size()) - 1));
    return lingerLast_;
}
bool Player::vary(const QString &sequence) {
    const auto found = catalog_.activity.constFind(state_);
    if (found == catalog_.activity.constEnd() || stopped_ || phase_ != 1 || decoration_ != Decoration::None || index_ != 0
        || std::none_of(found->loops.begin(), found->loops.end(),
                        [&](const ActivityChoice &choice) { return choice.sequence == sequence; }))
        return false;
    sequence_ = sequence;
    display(); // In the same event-loop turn as the pass's first frame, so nothing is painted in between.
    return true;
}
void Player::hold(const QString &state) {
    if (!catalog_.animations.contains(state) || (held_ && state == state_)) return;
    const bool was = held_;
    if (!held_) holdResume_ = resumeTarget();
    pending_.clear();
    held_ = true;
    // Before the entry, which ends whatever showed: nothing waiting may start in its place while held.
    if (!was) emit heldChanged(true);
    enter(state);
}
void Player::release() {
    if (!held_) return;
    held_ = false;
    select(holdResume_);
    emit heldChanged(false);
}
QString Player::touchAt(QPointF point, int side) const {
    if (catalog_.touch.scale <= 0 || side <= 0) return {};
    const QPointF scaled(point.x() * catalog_.touch.scale / side, point.y() * catalog_.touch.scale / side);
    for (const auto &region : catalog_.touch.regions)
        if (QRectF(region.rect).contains(scaled)) return region.state;
    return {};
}
QStringList Player::choose(const QString &state) {
    const QVector<Choice> *art = &catalog_.animations[state].choices;
    if (const auto mood = catalog_.moods.constFind(mood_); mood != catalog_.moods.constEnd())
        if (const auto found = mood->constFind(state); found != mood->constEnd()) art = &*found;
    const auto &choices = *art;
    if (!variants_ || choices.size() == 1) return choices.first().sequences;
    int total = 0;
    for (const auto &choice : choices) total += choice.weight;
    int roll = qBound(0, random_(total), total - 1);
    for (const auto &choice : choices) {
        if (roll < choice.weight) return choice.sequences;
        roll -= choice.weight;
    }
    return choices.first().sequences;
}
void Player::enter(const QString &state, int phase) {
    const auto from = state_;
    state_ = state;
    stopped_ = false;
    chosen_ = choose(state);
    loopCount_ = 0;
    decoration_ = Decoration::None;
    welcome_.clear();
    // A welcome replaces the first loop pass when the pet comes from one of the listed states.
    const auto art = catalog_.activity.constFind(state);
    if (art != catalog_.activity.constEnd() && art->enterFrom.contains(from) && reactionGate_ && reactionGate_())
        welcome_ = drawChoice(art->enter);
    emit entered(state);
    enterSequence(phase, phase == 1 ? std::exchange(welcome_, {}) : QString());
}
void Player::enterSequence(int phase, const QString &sequence) {
    timer_.stop();
    phase_ = phase;
    sequence_ = sequence.isEmpty() ? chosen_.value(phase) : sequence;
    index_ = 0;
    pixmap_ = {};
    cache_.clear();
    display();
}
void Player::advance() {
    timer_.stop();
    if (stopped_ || !catalog_.sequences.contains(sequence_)) return;
    // Linger time is the frames shown, so a paused or hidden pet never runs it down.
    if (decoration_ == Decoration::LingerIn || decoration_ == Decoration::Linger) lingerMs_ += duration_;
    if (++index_ < catalog_.sequences[sequence_].size()) { display(); return; }
    if (decoration_ != Decoration::None) { decorationEnded(); return; }
    const auto &animation = catalog_.animations[state_];
    if (animation.mode == "phased") {
        if (phase_ == 0) { enterSequence(1, std::exchange(welcome_, {})); return; }
        if (phase_ == 1 && animation.loops > 0 && ++loopCount_ >= animation.loops) { enterSequence(2); return; }
        if (phase_ == 2) {
            const auto finished = state_;
            auto target = pending_.isEmpty() ? QString("idle") : pending_;
            pending_.clear();
            if (catalog_.animations[target].mode == "once") previous_ = state_;
            finishing_ = true; enter(target); finishing_ = false;
            emit completed(finished);
            return;
        }
    }
    if (animation.mode == "once") {
        const auto finished = state_;
        if (animation.after == "stop") {
            stopped_ = true; index_ = catalog_.sequences[sequence_].size() - 1; emit changed();
        } else {
            auto target = animation.after == "previous" ? previous_ : QString("idle");
            if (!catalog_.animations.contains(target) || catalog_.animations[target].mode == "once" || target == stateFor("drag")
                || isTouch(target)) target = "idle";
            finishing_ = true; enter(target); finishing_ = false;
        }
        emit completed(finished);
        return;
    }
    // Another pass of a loop. An idle loop may switch variant or mood here, between two identical first
    // frames, and with variants off this is where it returns to the catalog's own entry. A phased loop
    // returns to its own sequence: an alternate or a welcome lasts one pass.
    if (animation.mode == "loop") {
        chosen_ = choose(state_);
        sequence_ = chosen_.value(0);
    } else sequence_ = chosen_.value(phase_);
    index_ = 0;
    display();
    emit looped(state_); // Last: a listener may select a new state or vary this pass.
}
void Player::display() {
    const auto &frame = catalog_.sequences[sequence_].at(index_);
    auto *cached = cache_.object(frame.path);
    if (!cached) {
        QImageReader reader(frame.path);
        const auto dimensions = reader.size();
        if (!dimensions.isValid() || dimensions.width() > 2048 || dimensions.height() > 2048) {
            fail("Missing, invalid or oversized frame: " + frame.path); return;
        }
        reader.setScaledSize(dimensions.scaled(renderSize_, renderSize_, Qt::KeepAspectRatio));
        const auto image = reader.read();
        if (image.isNull()) { fail("Cannot decode frame: " + frame.path); return; }
        auto *decoded = new QPixmap(QPixmap::fromImage(image));
        const int cost = (decoded->width() * decoded->height() * 4 + 1023) / 1024;
        cache_.insert(frame.path, decoded, cost);
        cached = cache_.object(frame.path);
    }
    pixmap_ = *cached;
    duration_ = frame.durationMs;
    if (!paused_) timer_.start(duration_);
    emit changed();
}
void Player::fail(const QString &message) {
    timer_.stop();
    error_ = message;
    pixmap_ = {}; cache_.clear(); stopped_ = true;
    emit failed(error_);
    // A damaged activity can recover to idle. A damaged idle remains a visible UI fallback.
    if (state_ != "idle" && catalog_.animations.contains("idle")) {
        pending_.clear(); held_ = false; enter("idle");
    } else emit changed();
}
void Player::setRenderSize(int pixels) {
    pixels = qBound(160, pixels, 640);
    if (renderSize_ == pixels) return;
    renderSize_ = pixels; pixmap_ = {}; cache_.clear();
    if (!stopped_ && catalog_.sequences.contains(sequence_)) display();
}
void Player::setPaused(bool paused) {
    paused_ = paused;
    if (paused) timer_.stop();
    else if (!stopped_) timer_.start(duration_);
    emit changed();
}
}
