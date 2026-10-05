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
#include <tuple>

namespace pet {
namespace {
bool safePath(const QString &path) {
    return !path.isEmpty() && !QDir::isAbsolutePath(path) && !path.contains('\\')
        && !path.contains(':') && !path.split('/').contains("..")
        && !path.split('/').contains(".");
}
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
        if (!QResource::registerResource(artwork)) {
            fail("Cannot load the artwork pack. Reinstall Agent Pet to restore it."); return false;
        }
    }
    QFile file(QDir(root).filePath("assets/vpet/animations.json"));
    auto invalid = [this](const QString &reason) {
        sequences_.clear(); animations_.clear(); fail(reason); return false;
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
    return true;
}
QString Player::phase() const {
    if (stopped_) return "stopped";
    if (!animations_.contains(state_)) return "unavailable";
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
        pending_ = state;
        if (phase_ != 2) enterSequence(2);
        return true;
    }
    const auto resume = resumeTarget();
    if (animations_[state].mode == "once" && state != state_) previous_ = resume;
    pending_.clear();
    enter(state);
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
void Player::enter(const QString &state) {
    state_ = state;
    stopped_ = false;
    chosen_ = choose(state);
    loopCount_ = 0;
    emit entered(state);
    enterSequence(0);
}
void Player::enterSequence(int phase) {
    timer_.stop();
    phase_ = phase;
    sequence_ = chosen_.value(phase);
    index_ = 0;
    pixmap_ = {};
    cache_.clear();
    display();
}
void Player::advance() {
    timer_.stop();
    if (stopped_ || !sequences_.contains(sequence_)) return;
    if (++index_ < sequences_[sequence_].size()) { display(); return; }
    const auto &animation = animations_[state_];
    if (animation.mode == "phased") {
        if (phase_ == 0) { enterSequence(1); return; }
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
    // frames, and with variants off this is where it returns to the catalog's own entry.
    if (animation.mode == "loop") {
        chosen_ = choose(state_);
        sequence_ = chosen_.value(0);
    }
    index_ = 0;
    display();
    if (animation.mode == "loop") emit looped(state_); // Last: a listener may select a new state.
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
