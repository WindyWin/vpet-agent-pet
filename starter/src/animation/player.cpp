#include "player.h"
#include "pet_library.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
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
    setRules(library.eventRules().rules());
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
    rules_ = EventRules(catalog_.pools);
    // The first idle is always the catalog's own entry; variants start with the second pass.
    variants_ = false; enter("idle"); variants_ = true;
}
void Player::setRules(const QVector<EventRule> &packs) {
    rules_ = EventRules(catalog_.pools);
    rules_.add(packs);
}
const Move *Player::move(const QString &state) const {
    const auto found = catalog_.moves.constFind(state);
    return found == catalog_.moves.constEnd() ? nullptr : &*found;
}
const ActivityArt *Player::activity(const QString &state) const {
    if ((state == state_ && bedtimeArt_) || (bedtime_ && hasBedtime(state))) return nullptr;
    const auto found = catalog_.activity.constFind(state);
    return found == catalog_.activity.constEnd() ? nullptr : &*found;
}
void Player::setBedtime(bool enabled) {
    if (bedtime_ == enabled) return;
    bedtime_ = enabled;
    // Leave the current desk using the art it entered with, then enter the new version. A queued
    // transition or one-shot keeps its original art and will pick up the schedule at its next entry.
    if (hasBedtime(state_) && !held_ && !stopped_ && pending_.isEmpty()
        && catalog_.animations.value(state_).mode == "phased" && phase_ != 2) {
        pending_ = state_;
        beginEnd();
    }
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
// A catalog may restrict desk transitions to specific poses. A sustained performance can
// begin standing and only land at the desk when the complete sequence has played.
bool Player::seated() const {
    if (bedtimeArt_) return false;
    const auto found = catalog_.activity.constFind(state_);
    if (found == catalog_.activity.constEnd()) return false;
    const auto &visual = found->continuity;
    return phase_ == 1 && (visual.sources.isEmpty() || seated_ || decoration_ != Decoration::None);
}
QString Player::handover(const QString &target, const QString &fallback) const {
    return catalog_.activity.value(state_).continuity.overrides.value(sequence_).value(target, fallback);
}
// A non-urgent change from an activity's loop phase that can stay at the desk; false plays the usual end.
bool Player::decorate(const QString &target) {
    if (bedtimeArt_ || (bedtime_ && hasBedtime(target))) return false;
    const auto found = catalog_.activity.constFind(state_);
    if (!continuity_ || found == catalog_.activity.constEnd() || phase_ == 2
        || (phase_ != 1 && found->continuity.sources.isEmpty())) return false;
    const auto &art = *found;
    if (decoration_ != Decoration::None && target == pending_) return true; // Already on its way there.
    if (decoration_ == Decoration::Handover) {
        // Finish the current prop swap, then act on the latest request from its landing pose.
        const auto landing = catalog_.activity.constFind(handoverTo_);
        const auto destination = catalog_.activity.constFind(target);
        const bool desk = target == handoverTo_ || (landing != catalog_.activity.constEnd()
            && (landing->handover.contains(target) || landing->linger.to == target
                || (sustainedEnabled_ && destination != catalog_.activity.constEnd()
                    && destination->sustainedResume.contains(handoverTo_))));
        if (desk) pending_ = target;
        return desk;
    }
    const auto landing = catalog_.activity.constFind(target);
    const bool resume = sustainedEnabled_ && landing != catalog_.activity.constEnd()
        && landing->sustainedResume.contains(state_);
    if (!seated()) {
        // Don't cut an entrance or a standing performance into a seated prop swap.
        // Keep only the latest desk request and honour it once this clip lands.
        if (art.continuity.landings.contains(sequence_)
            && (target == state_ || art.handover.contains(target) || resume || art.linger.to == target)) {
            pending_ = target == state_ ? QString() : target;
            return true;
        }
        return false;
    }
    if (resume && decoration_ == Decoration::None) {
        const auto sequence = handover(target, landing->sustainedResume.value(state_));
        pending_.clear(); enter(target, 1, sequence);
        return true;
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
        enterSequence(1, handover(target, art.handover.value(target)));
        return true;
    }
    return false;
}
// A handover or a linger sequence just ended.
void Player::decorationEnded() {
    const auto &art = catalog_.activity[state_];
    if (decoration_ == Decoration::Handover) {
        const auto target = std::exchange(handoverTo_, {}), next = std::exchange(pending_, {});
        const auto loop = catalog_.activity.value(target).continuity.landings.value(sequence_);
        enter(target, 1, loop);
        if (!next.isEmpty() && next != target) select(next); // The request moved on while the props swapped.
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
    const bool atDesk = seated();
    decoration_ = Decoration::None;
    QString farewell;
    const auto found = catalog_.activity.constFind(state_);
    if (!bedtimeArt_ && found != catalog_.activity.constEnd() && reactionGate_)
        if (const auto pool = found->exit.value(pending_); !pool.isEmpty() && reactionGate_()) farewell = drawChoice(pool);
    if (atDesk && found != catalog_.activity.constEnd() && !found->continuity.exit.isEmpty())
        farewell = found->continuity.exit;
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
    if (bedtimeArt_) return false;
    const auto found = catalog_.activity.constFind(state_);
    if (found == catalog_.activity.constEnd() || !catalog_.sequences.contains(sequence)
        || stopped_ || phase_ != 1 || decoration_ != Decoration::None || index_ != 0
        || (sequence != found->sustained && sequence != found->sustainedLoop && std::none_of(found->loops.begin(), found->loops.end(),
                        [&](const ActivityChoice &choice) { return choice.sequence == sequence; })))
        return false;
    sequence_ = sequence;
    if (!found->continuity.sources.isEmpty()) seated_ = found->continuity.sources.contains(sequence);
    sustainedActive_ = sequence == found->sustained || sequence == found->sustainedLoop;
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
QString Player::touchAt(QPointF point, int side, bool tapOnly) const {
    if (catalog_.touch.scale <= 0 || side <= 0) return {};
    const QPointF scaled(point.x() * catalog_.touch.scale / side, point.y() * catalog_.touch.scale / side);
    for (const auto &region : catalog_.touch.regions)
        if (QRectF(region.rect).contains(scaled)) return !tapOnly || region.tap ? region.state : QString();
    return {};
}
QStringList Player::choose(const QString &state) {
    const QVector<Choice> *art = &catalog_.animations[state].choices;
    if (const auto mood = catalog_.moods.constFind(mood_); mood != catalog_.moods.constEnd())
        if (const auto found = mood->constFind(state); found != mood->constEnd()) art = &*found;
    if (bedtime_)
        if (const auto found = catalog_.bedtime.constFind(state); found != catalog_.bedtime.constEnd()) art = &*found;
    const auto &choices = *art;
    if (variants_ && idleStep_.state == state && art == &catalog_.animations[state].choices)
        return idleStep_.sequences;
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
void Player::setIdleStep(const IdleStep &step, bool refreshIdle) {
    if (idleStep_.state == step.state && idleStep_.sequences == step.sequences && idleStep_.repeat == step.repeat) return;
    const bool wasActive = !idleStep_.state.isEmpty();
    idleStep_ = step;
    if (state_ == step.state || (wasActive && step.state.isEmpty() && state_ == "idle")) {
        chosen_ = choose(state_);
        if (refreshIdle && state_ == "idle" && index_ == 0) {
            sequence_ = chosen_.value(0);
            display();
        }
    }
}
void Player::enter(const QString &state, int phase, const QString &sequence) {
    const auto from = state_;
    const bool fromBedtimeArt = bedtimeArt_;
    const bool atDesk = !catalog_.activity.value(from).continuity.sources.isEmpty() && seated();
    seated_ = !sequence.isEmpty() && atDesk;
    state_ = state;
    stopped_ = false;
    chosen_ = choose(state);
    bedtimeArt_ = bedtime_ && hasBedtime(state);
    loopCount_ = 0;
    decoration_ = Decoration::None;
    welcome_.clear();
    sustainedActive_ = false;
    // A welcome replaces the first loop pass when the pet comes from one of the listed states.
    const auto art = catalog_.activity.constFind(state);
    if (!bedtimeArt_ && !fromBedtimeArt && art != catalog_.activity.constEnd() && art->enterFrom.contains(from) && reactionGate_ && reactionGate_())
        welcome_ = drawChoice(art->enter);
    if (!bedtimeArt_ && !fromBedtimeArt && !sequence.isEmpty()) {
        welcome_ = sequence;
        sustainedActive_ = art != catalog_.activity.constEnd() && !art->sustainedLoop.isEmpty()
            && (art->continuity.landings.contains(sequence) || art->sustainedResume.values().contains(sequence));
    }
    emit entered(state);
    enterSequence(phase, phase == 1 ? std::exchange(welcome_, {}) : QString());
}
void Player::enterSequence(int phase, const QString &sequence) {
    timer_.stop();
    phase_ = phase;
    sequence_ = sequence.isEmpty() ? chosen_.value(phase) : sequence;
    index_ = 0;
    const auto art = catalog_.activity.constFind(state_);
    if (art != catalog_.activity.constEnd() && !art->continuity.sources.isEmpty()) {
        if (phase != 1 || sequence_ == art->sustained) seated_ = false;
        else if (art->continuity.sources.contains(sequence_)) seated_ = true;
        else if (!art->continuity.landings.contains(sequence_)) seated_ = false;
    }
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
        const auto landing = catalog_.activity.value(state_).continuity.landings.value(sequence_);
        if (phase_ <= 1 && !landing.isEmpty() && (continuity_ || phase_ == 1)) {
            const bool newPass = phase_ == 1;
            const auto art = catalog_.activity.constFind(state_);
            if (art != catalog_.activity.constEnd() && landing == art->sustainedLoop) {
                chosen_[1] = landing;
                sustainedActive_ = true;
            }
            const auto next = std::exchange(pending_, {});
            enterSequence(1, !newPass && !welcome_.isEmpty() ? std::exchange(welcome_, {}) : landing);
            if (!next.isEmpty() && next != state_) select(next);
            else if (newPass) emit looped(state_);
            return;
        }
        if (phase_ == 0) { enterSequence(1, std::exchange(welcome_, {})); return; }
        const int loops = idleStep_.state == state_ && variants_ ? idleStep_.repeat : animation.loops;
        if (phase_ == 1 && loops > 0 && ++loopCount_ >= loops) { enterSequence(2); return; }
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
        bedtimeArt_ = bedtime_ && hasBedtime(state_);
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
        QFile file(frame.path);
        QFileInfo pathInfo(frame.path);
        auto confined = [&] {
            if (frame.root.isEmpty()) return true;
            const auto root = QFileInfo(frame.root).canonicalFilePath();
            const auto path = pathInfo.canonicalFilePath();
            if (root.isEmpty() || path.isEmpty()) return false;
            const auto relative = QDir(root).relativeFilePath(path);
            return safeAssetPath(relative) && relative != ".." && !relative.startsWith("../");
        };
        if (pathInfo.isSymLink() || pathInfo.isJunction() || !confined()
            || !file.open(QIODevice::ReadOnly) || file.size() > 4LL * 1024 * 1024) {
            fail("Missing, linked or oversized frame: " + frame.path); return;
        }
        // Decode from the open file handle after rechecking the current path and byte limit. Replacing the path
        // after open cannot redirect this read to another file.
        pathInfo.refresh();
        if (!pathInfo.exists() || pathInfo.isSymLink() || pathInfo.isJunction() || !confined()) {
            fail("Missing or linked frame: " + frame.path); return;
        }
        QImageReader reader(&file);
        const auto dimensions = reader.size();
        if (reader.format() != "png" || !dimensions.isValid() || dimensions.width() > 2048 || dimensions.height() > 2048) {
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
        const bool was = std::exchange(held_, false);
        pending_.clear(); enter("idle");
        if (was) emit heldChanged(false); // After the recovery, so nothing re-enters the player before it is done.
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
