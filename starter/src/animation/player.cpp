#include "player.h"
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QCoreApplication>
#include <QResource>
#include <QRegularExpression>
#include <algorithm>
#include <tuple>
#include <utility>

namespace pet {
namespace {
bool safePath(const QString &path) {
    return !path.isEmpty() && !QDir::isAbsolutePath(path) && !path.contains('\\')
        && !path.contains(':') && !path.split('/').contains("..")
        && !path.split('/').contains(".");
}
}
QString drawReaction(const QVector<Reaction> &pool, const Random &random) {
    if (pool.isEmpty()) return {};
    int total = 0;
    for (const auto &reaction : pool) total += reaction.weight;
    int roll = pool.size() == 1 ? 0 : qBound(0, random(total), total - 1);
    for (const auto &reaction : pool) {
        if (roll < reaction.weight) return reaction.state;
        roll -= reaction.weight;
    }
    return pool.first().state;
}
Player::Player(QObject *parent, const QString &root) : QObject(parent) {
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &Player::advance);
    // The first idle is always the catalog's own entry; variants start with the second pass.
    if (load(root)) { variants_ = false; enter("idle"); variants_ = true; }
}
bool Player::load(const QString &root) {
    if (root == ":/" && !QFile::exists(":/assets/vpet/animations.json")) {
        const QDir executable(QCoreApplication::applicationDirPath());
        // Installed bundles keep it under share; local builds put it beside the executable.
        const auto installed = executable.filePath("../share/agent-pet/artwork.rcc");
        const auto artwork = QFile::exists(installed) ? installed : executable.filePath("artwork.rcc");
        QStringList registered;
        auto registerPack = [&](const QString &path) {
            if (!QResource::registerResource(path)) return false;
            registered.append(path); return true;
        };
        auto missingPack = [&] {
            for (const auto &path : registered) QResource::unregisterResource(path);
            fail("Cannot load the artwork pack. Reinstall Agent Pet to restore it."); return false;
        };
        if (!registerPack(artwork)) return missingPack();
        QFile index(":/assets/vpet/packs.json");
        // Legacy bundles contain all frames in artwork.rcc and have no index.
        if (index.exists()) {
            if (!index.open(QIODevice::ReadOnly) || index.size() > 1024 * 1024) return missingPack();
            const auto packs = QJsonDocument::fromJson(index.readAll());
            if (!packs.isArray() || packs.array().isEmpty() || packs.array().size() > 4093) return missingPack();
            const QDir directory(QFileInfo(artwork).absolutePath());
            QSet<QString> seen;
            for (const auto &value : packs.array()) {
                const auto name = value.toString();
                if (!QRegularExpression("^artwork-[0-9a-f]{64}\\.rcc$").match(name).hasMatch()
                    || seen.contains(name) || !registerPack(directory.filePath(name))) return missingPack();
                seen.insert(name);
            }
        }
    }
    QFile file(QDir(root).filePath("assets/vpet/animations.json"));
    auto invalid = [this](const QString &reason) {
        sequences_.clear(); animations_.clear(); activity_.clear(); fail(reason); return false;
    };
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return invalid("Animation catalog is missing or too large.");
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto catalog = document.object();
    if (parse.error != QJsonParseError::NoError || catalog["schema_version"].toInt() != 1)
        return invalid("Invalid animation catalog format.");
    for (const auto &entry : catalog["sequences"].toArray()) {
        const auto object = entry.toObject();
        const auto id = object["path"].toString();
        if (!safePath(id) || sequences_.contains(id)) return invalid("Invalid or duplicate sequence identifier.");
        QVector<Frame> frames;
        int total = 0;
        for (const auto &value : object["frames"].toArray()) {
            const auto frame = value.toObject();
            const auto relative = frame["path"].toString();
            const auto duration = frame["duration_ms"].toInt(-1);
            if (!safePath(relative) || !relative.startsWith("assets/vpet/vup/") || duration < 1 || duration > 60000)
                return invalid("Invalid frame path or duration in " + id);
            frames.append({QDir(root).filePath(relative), duration});
            total += duration;
            if (frames.size() > 1000) return invalid("Too many frames in " + id);
        }
        if (frames.isEmpty() || total != object["duration_ms"].toInt()) return invalid("Invalid sequence timing: " + id);
        sequences_.insert(id, frames);
    }
    const auto states = catalog["states"].toObject();
    const auto playback = catalog["playback"].toObject();
    constexpr int maxWeight = 1000;
    for (auto it = states.begin(); it != states.end(); ++it) {
        Choice primary;
        for (const auto &value : it.value().toArray()) {
            auto id = value.toString();
            if (!sequences_.contains(id)) return invalid("Unknown sequence for " + it.key());
            primary.sequences.append(id);
        }
        const auto policy = playback[it.key()].toObject();
        Animation animation;
        animation.mode = policy["mode"].toString();
        animation.after = policy["after"].toString();
        // `weight` ranks the entry against the state's variants; `loops` ends a phased state by
        // itself after that many passes of its loop phase.
        primary.weight = policy.contains("weight") ? policy["weight"].toInt(0) : 1;
        animation.loops = policy["loops"].toInt(0);
        if ((animation.mode != "phased" && animation.mode != "loop" && animation.mode != "once")
            || primary.sequences.size() != (animation.mode == "phased" ? 3 : 1)
            || !QStringList{"idle", "previous", "stop"}.contains(animation.after)
            || primary.weight < 1 || primary.weight > maxWeight
            || (policy.contains("loops") && (animation.mode != "phased" || animation.loops < 1 || animation.loops > 100)))
            return invalid("Invalid playback policy for " + it.key());
        animation.choices.append(primary);
        animations_.insert(it.key(), animation);
    }
    if (!animations_.contains("idle") || animations_["idle"].mode != "loop") return invalid("Missing idle loop.");
    const auto variants = catalog["variants"].toObject();
    for (auto it = variants.begin(); it != variants.end(); ++it) {
        if (!animations_.contains(it.key()) || it.value().toArray().isEmpty())
            return invalid("Variants for an unknown or empty state: " + it.key());
        auto &animation = animations_[it.key()];
        const int phases = animation.choices.first().sequences.size();
        for (const auto &value : it.value().toArray()) {
            Choice choice;
            choice.weight = value.toObject()["weight"].toInt(0);
            for (const auto &id : value.toObject()["sequences"].toArray()) {
                if (!sequences_.contains(id.toString())) return invalid("Unknown variant sequence for " + it.key());
                choice.sequences.append(id.toString());
            }
            if (choice.sequences.size() != phases || choice.weight < 1 || choice.weight > maxWeight)
                return invalid("Invalid variant for " + it.key());
            animation.choices.append(choice);
        }
    }
    // Mood art: per mood, states whose choices it replaces. Each choice keeps the state's shape.
    const auto moods = catalog["moods"].toObject();
    for (auto mood = moods.begin(); mood != moods.end(); ++mood) {
        if (mood.key() != "happy" && mood.key() != "poor") return invalid("Unknown mood: " + mood.key());
        const auto states = mood.value().toObject();
        for (auto it = states.begin(); it != states.end(); ++it) {
            if (!animations_.contains(it.key()) || it.value().toArray().isEmpty())
                return invalid("Mood art for an unknown or empty state: " + it.key());
            const int phases = animations_[it.key()].choices.first().sequences.size();
            QVector<Choice> choices;
            for (const auto &value : it.value().toArray()) {
                Choice choice;
                choice.weight = value.toObject()["weight"].toInt(0);
                for (const auto &id : value.toObject()["sequences"].toArray()) {
                    if (!sequences_.contains(id.toString())) return invalid("Unknown mood sequence for " + it.key());
                    choice.sequences.append(id.toString());
                }
                if (choice.sequences.size() != phases || choice.weight < 1 || choice.weight > maxWeight)
                    return invalid("Invalid mood choice for " + it.key());
                choices.append(choice);
            }
            moods_[mood.key()].insert(it.key(), choices);
        }
    }
    // A fidget or a reaction must end by itself and hand back to idle, so nothing has to wait for it.
    auto endsItself = [this](const QString &state) {
        const auto found = animations_.constFind(state);
        return state != "idle" && found != animations_.constEnd() && found->after == "idle"
            && (found->mode == "once" || (found->mode == "phased" && found->loops > 0));
    };
    const auto reactions = catalog["reactions"].toObject();
    for (auto it = reactions.begin(); it != reactions.end(); ++it) {
        if (it.value().toArray().isEmpty()) return invalid("Empty reaction: " + it.key());
        for (const auto &value : it.value().toArray()) {
            const auto object = value.toObject();
            const Reaction reaction{object["state"].toString(), object["weight"].toInt(0)};
            if (!endsItself(reaction.state) || reaction.weight < 1 || reaction.weight > maxWeight)
                return invalid("Invalid reaction for " + it.key() + ": " + reaction.state);
            reactions_[it.key()].append(reaction);
        }
    }
    const auto ambient = catalog["ambient"].toObject();
    if (ambient.contains("sleep_after_s")) {
        sleepAfterS_ = ambient["sleep_after_s"].toInt(0);
        if (sleepAfterS_ < 60 || sleepAfterS_ > 86400) return invalid("Invalid ambient sleep delay.");
    }
    for (const auto &value : ambient["fidgets"].toArray()) {
        const auto object = value.toObject();
        Fidget fidget{object["state"].toString(), object["weight"].toInt(0), object["min_idle_s"].toInt(0),
                      object["rare"].toBool()};
        if (!endsItself(fidget.state) || fidgetStates_.contains(fidget.state)
            || fidget.weight < 1 || fidget.weight > maxWeight || fidget.minIdleS < 0 || fidget.minIdleS > 86400)
            return invalid("Invalid ambient fidget: " + fidget.state);
        fidgets_.append(fidget);
        fidgetStates_.insert(fidget.state);
    }
    // Touch reactions are held while the user (or a fall) keeps them, so each holds a loop phase that
    // only a release ends. Every part is optional; a catalog without the section has no reactions.
    if (catalog.contains("touch")) {
        const auto touch = catalog["touch"].toObject();
        touch_.scale = touch["scale"].toInt(0);
        if (touch_.scale < 10 || touch_.scale > 10000) return invalid("Invalid touch scale.");
        auto heldState = [this](const QJsonValue &value, QString &state) {
            state = value.toString();
            const auto found = animations_.constFind(state);
            if (found == animations_.constEnd() || found->mode != "phased" || found->loops > 0 || fidgetStates_.contains(state)
                || state == "idle" || state == "dragging") return false;
            touchStates_.insert(state);
            return true;
        };
        for (const auto &value : touch["regions"].toArray()) {
            const auto object = value.toObject();
            const auto rect = object["rect"].toArray();
            TouchRegion region;
            if (!heldState(object["state"], region.state) || rect.size() != 4) return invalid("Invalid touch region.");
            region.rect = QRect(rect[0].toInt(-1), rect[1].toInt(-1), rect[2].toInt(0), rect[3].toInt(0));
            if (region.rect.left() < 0 || region.rect.top() < 0 || region.rect.width() < 1 || region.rect.height() < 1
                || region.rect.right() >= touch_.scale || region.rect.bottom() >= touch_.scale)
                return invalid("Touch region outside the artwork: " + region.state);
            touch_.regions.append(region);
        }
        const auto fall = touch["fall"].toObject(), edge = touch["edge"].toObject();
        if ((fall.contains("left") && !heldState(fall["left"], touch_.fallLeft))
            || (fall.contains("right") && !heldState(fall["right"], touch_.fallRight)))
            return invalid("Invalid fall reaction.");
        for (auto [side, state, at] : {std::tuple{QString("left"), &touch_.edgeLeft, &touch_.edgeLeftAt},
                                       std::tuple{QString("right"), &touch_.edgeRight, &touch_.edgeRightAt}}) {
            if (!edge.contains(side)) continue;
            const auto object = edge[side].toObject();
            *at = object["at"].toInt(0);
            if (!heldState(object["state"], *state) || *at < 1 || *at >= touch_.scale)
                return invalid("Invalid edge reaction: " + side);
        }
    }
    // Moves carry the window across the screen during their loop phase, which a count must end, like
    // a fidget's. Every part is optional; a catalog without the section never moves on its own.
    if (catalog.contains("moves")) {
        const auto moves = catalog["moves"].toObject();
        moveScale_ = moves["scale"].toInt(0);
        if (moveScale_ < 10 || moveScale_ > 10000) return invalid("Invalid move scale.");
        auto sides = [this](const QJsonValue &value, Sides &sides) {
            if (value.isUndefined()) return true;
            const auto object = value.toObject();
            if (!value.isObject() || object.isEmpty()) return false;
            for (auto it = object.begin(); it != object.end(); ++it) {
                const double distance = it.value().toDouble(-1);
                if (!it.value().isDouble() || distance < 0 || distance > 10.0 * moveScale_) return false;
                if (it.key() == "left") sides.left = distance;
                else if (it.key() == "top") sides.top = distance;
                else if (it.key() == "right") sides.right = distance;
                else if (it.key() == "bottom") sides.bottom = distance;
                else return false;
            }
            return true;
        };
        for (const auto &value : moves["list"].toArray()) {
            const auto object = value.toObject();
            const auto speed = object["speed"].toArray();
            const auto wall = object["wall"].toObject();
            Move move;
            move.state = object["state"].toString();
            move.mood = object["mood"].toString();
            move.wall = wall["side"].toString();
            move.at = wall["at"].toInt(0);
            if (speed.size() == 2) move.speed = {speed[0].toDouble(), speed[1].toDouble()};
            const double limit = 4.0 * moveScale_;
            if (!endsItself(move.state) || animations_[move.state].mode != "phased" || moves_.contains(move.state)
                || speed.size() != 2 || !speed[0].isDouble() || !speed[1].isDouble() || move.speed.isNull()
                || qAbs(move.speed.x()) > limit || qAbs(move.speed.y()) > limit
                || (object.contains("mood") && move.mood != "happy" && move.mood != "poor")
                || (object.contains("wall") && ((move.wall != "left" && move.wall != "right") || move.at < 1 || move.at >= moveScale_))
                || !sides(object.value("room"), move.room) || !sides(object.value("near"), move.near)
                || !sides(object.value("keep"), move.keep))
                return invalid("Invalid move: " + move.state);
            moves_.insert(move.state, move);
        }
    }
    // Activity decoration: alternate loops, reactions and desk continuity for states that end only when
    // asked. Every part is optional; a state without an entry plays only its own sequences.
    if (catalog.contains("activity")) {
        const auto activity = catalog["activity"].toObject();
        auto decorated = [&](const QString &state) {
            const auto found = animations_.constFind(state);
            return found != animations_.constEnd() && found->mode == "phased" && found->loops == 0 && state != "idle"
                && state != "dragging" && !fidgetStates_.contains(state) && !touchStates_.contains(state);
        };
        auto pool = [&](const QJsonValue &value, QVector<ActivityChoice> &choices, bool styled) {
            if (!value.isArray() || value.toArray().isEmpty()) return false;
            for (const auto &item : value.toArray()) {
                const auto object = item.toObject();
                const auto style = object.value("style");
                const ActivityChoice choice{object["sequence"].toString(), object["weight"].toInt(0), style == "playful"};
                if (!sequences_.contains(choice.sequence) || choice.weight < 1 || choice.weight > maxWeight
                    || (!style.isUndefined() && (!styled || (style != "subtle" && style != "playful"))))
                    return false;
                choices.append(choice);
            }
            return true;
        };
        if (!catalog["activity"].isObject()) return invalid("Invalid activity section.");
        for (auto it = activity.begin(); it != activity.end(); ++it) {
            const auto object = it.value().toObject();
            auto other = [&](const QString &state) { return state != it.key() && activity.contains(state); };
            ActivityArt art;
            bool ok = decorated(it.key()) && it.value().isObject();
            if (ok && object.contains("loops")) ok = pool(object["loops"], art.loops, true);
            if (ok && object.contains("enter")) {
                const auto enter = object["enter"].toObject();
                ok = !enter["from"].toArray().isEmpty() && pool(enter["choices"], art.enter, false);
                for (const auto &state : enter["from"].toArray()) {
                    ok = ok && other(state.toString());
                    art.enterFrom.append(state.toString());
                }
            }
            if (ok && object.contains("exit")) {
                const auto exit = object["exit"].toObject();
                ok = !exit.isEmpty();
                for (auto target = exit.begin(); ok && target != exit.end(); ++target)
                    ok = other(target.key()) && pool(target.value(), art.exit[target.key()], false);
            }
            if (ok && object.contains("linger")) {
                const auto linger = object["linger"].toObject();
                const int maxS = linger["max_s"].toInt(0);
                art.linger = {linger["to"].toString(), maxS * 1000, linger["in"].toString(), linger["out"].toString(), {}};
                ok = other(art.linger.to) && maxS >= 1 && maxS <= 60 && !linger["loop"].toArray().isEmpty()
                    && (!linger.contains("in") || sequences_.contains(art.linger.in))
                    && (!linger.contains("out") || sequences_.contains(art.linger.out));
                for (const auto &id : linger["loop"].toArray()) {
                    ok = ok && sequences_.contains(id.toString());
                    art.linger.loop.append(id.toString());
                }
            }
            if (ok && object.contains("handover")) {
                const auto handover = object["handover"].toObject();
                ok = !handover.isEmpty();
                for (auto target = handover.begin(); ok && target != handover.end(); ++target) {
                    ok = other(target.key()) && sequences_.contains(target.value().toString());
                    art.handover.insert(target.key(), target.value().toString());
                }
            }
            if (!ok) return invalid("Invalid activity for " + it.key());
            activity_.insert(it.key(), art);
        }
    }
    return true;
}
const Move *Player::move(const QString &state) const {
    const auto found = moves_.constFind(state);
    return found == moves_.constEnd() ? nullptr : &*found;
}
const ActivityArt *Player::activity(const QString &state) const {
    const auto found = activity_.constFind(state);
    return found == activity_.constEnd() ? nullptr : &*found;
}
void Player::finish() {
    if (stopped_ || held_ || animations_.value(state_).mode != "phased" || phase_ == 2) return;
    beginEnd();
}
QString Player::phase() const {
    if (stopped_) return "stopped";
    if (!animations_.contains(state_)) return "unavailable";
    if (decoration_ == Decoration::Handover) return "handover";
    if (decoration_ != Decoration::None) return "linger";
    if (animations_[state_].mode != "phased") return animations_[state_].mode;
    return QStringList{"start", "loop", "end"}.at(phase_);
}
QString Player::resumeTarget() const {
    if (held_) return holdResume_;
    if (!pending_.isEmpty()) return pending_;
    // Decoration is never worth coming back to, nor is a reaction to being handled: a pet dragged
    // out of its hiding place stays out.
    if (isFidget(state_) || isTouch(state_)) return "idle";
    return animations_.value(state_).mode == "once" ? previous_ : state_;
}
bool Player::select(const QString &state, bool interrupt) {
    if (!animations_.contains(state)) {
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
    if (!interrupt && animations_.value(state_).mode == "phased" && !stopped_) {
        if (decorate(state)) return true;
        pending_ = state;
        if (phase_ != 2) beginEnd();
        return true;
    }
    const auto resume = resumeTarget();
    if (animations_[state].mode == "once" && state != state_) previous_ = resume;
    pending_.clear();
    enter(state);
    return true;
}
// A non-urgent change from an activity's loop phase that can stay at the desk; false plays the usual end.
bool Player::decorate(const QString &target) {
    const auto found = activity_.constFind(state_);
    if (!continuity_ || phase_ != 1 || found == activity_.constEnd()) return false;
    const auto &art = *found;
    if (decoration_ != Decoration::None && target == pending_) return true; // Already on its way there.
    if (decoration_ == Decoration::Handover) {
        // Changing course mid-swap: it lands first, then acts on the latest request from that desk.
        const auto landing = activity_.constFind(handoverTo_);
        const bool desk = target == handoverTo_ || (landing != activity_.constEnd()
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
    const auto &art = activity_[state_];
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
    const auto &art = activity_[state_];
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
    const auto found = activity_.constFind(state_);
    if (found != activity_.constEnd() && reactionGate_)
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
    auto loop = activity_[state_].linger.loop;
    if (loop.size() > 1) loop.removeAll(lingerLast_);
    lingerLast_ = loop.size() == 1 ? loop.first() : loop.at(qBound(0, random_(int(loop.size())), int(loop.size()) - 1));
    return lingerLast_;
}
bool Player::vary(const QString &sequence) {
    const auto found = activity_.constFind(state_);
    if (found == activity_.constEnd() || stopped_ || phase_ != 1 || decoration_ != Decoration::None || index_ != 0
        || std::none_of(found->loops.begin(), found->loops.end(),
                        [&](const ActivityChoice &choice) { return choice.sequence == sequence; }))
        return false;
    sequence_ = sequence;
    display(); // In the same event-loop turn as the pass's first frame, so nothing is painted in between.
    return true;
}
void Player::hold(const QString &state) {
    if (!animations_.contains(state) || (held_ && state == state_)) return;
    if (!held_) holdResume_ = resumeTarget();
    pending_.clear();
    held_ = true;
    enter(state);
}
void Player::release() {
    if (!held_) return;
    held_ = false;
    select(holdResume_);
}
QString Player::touchAt(QPointF point, int side) const {
    if (touch_.scale <= 0 || side <= 0) return {};
    const QPointF scaled(point.x() * touch_.scale / side, point.y() * touch_.scale / side);
    for (const auto &region : touch_.regions)
        if (QRectF(region.rect).contains(scaled)) return region.state;
    return {};
}
QStringList Player::choose(const QString &state) {
    const QVector<Choice> *art = &animations_[state].choices;
    if (const auto mood = moods_.constFind(mood_); mood != moods_.constEnd())
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
    const auto art = activity_.constFind(state);
    if (art != activity_.constEnd() && art->enterFrom.contains(from) && reactionGate_ && reactionGate_())
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
    if (stopped_ || !sequences_.contains(sequence_)) return;
    // Linger time is the frames shown, so a paused or hidden pet never runs it down.
    if (decoration_ == Decoration::LingerIn || decoration_ == Decoration::Linger) lingerMs_ += duration_;
    if (++index_ < sequences_[sequence_].size()) { display(); return; }
    if (decoration_ != Decoration::None) { decorationEnded(); return; }
    const auto &animation = animations_[state_];
    if (animation.mode == "phased") {
        if (phase_ == 0) { enterSequence(1, std::exchange(welcome_, {})); return; }
        if (phase_ == 1 && animation.loops > 0 && ++loopCount_ >= animation.loops) { enterSequence(2); return; }
        if (phase_ == 2) {
            auto target = pending_.isEmpty() ? QString("idle") : pending_;
            pending_.clear();
            if (animations_[target].mode == "once") previous_ = state_;
            enter(target); return;
        }
    }
    if (animation.mode == "once") {
        const auto finished = state_;
        if (animation.after == "stop") {
            stopped_ = true; index_ = sequences_[sequence_].size() - 1; emit changed();
        } else {
            auto target = animation.after == "previous" ? previous_ : QString("idle");
            if (!animations_.contains(target) || animations_[target].mode == "once" || target == "dragging"
                || isTouch(target)) target = "idle";
            enter(target);
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
    const auto &frame = sequences_[sequence_].at(index_);
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
    if (state_ != "idle" && animations_.contains("idle")) {
        pending_.clear(); held_ = false; enter("idle");
    } else emit changed();
}
void Player::setRenderSize(int pixels) {
    pixels = qBound(160, pixels, 640);
    if (renderSize_ == pixels) return;
    renderSize_ = pixels; pixmap_ = {}; cache_.clear();
    if (!stopped_ && sequences_.contains(sequence_)) display();
}
void Player::setPaused(bool paused) {
    paused_ = paused;
    if (paused) timer_.stop();
    else if (!stopped_) timer_.start(duration_);
    emit changed();
}
}
