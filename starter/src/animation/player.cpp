#include "player.h"
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

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
    if (load(root)) enter("idle");
}
bool Player::load(const QString &root) {
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
    for (auto it = states.begin(); it != states.end(); ++it) {
        Animation animation;
        for (const auto &value : it.value().toArray()) {
            auto id = value.toString();
            if (!sequences_.contains(id)) return invalid("Unknown sequence for " + it.key());
            animation.sequences.append(id);
        }
        const auto policy = playback[it.key()].toObject();
        animation.mode = policy["mode"].toString();
        animation.after = policy["after"].toString();
        if ((animation.mode != "phased" && animation.mode != "loop" && animation.mode != "once")
            || animation.sequences.size() != (animation.mode == "phased" ? 3 : 1)
            || !QStringList{"idle", "previous", "stop"}.contains(animation.after))
            return invalid("Invalid playback policy for " + it.key());
        animations_.insert(it.key(), animation);
    }
    if (!animations_.contains("idle") || animations_["idle"].mode != "loop") return invalid("Missing idle loop.");
    return true;
}
QString Player::phase() const {
    if (stopped_) return "stopped";
    if (!animations_.contains(state_)) return "unavailable";
    if (animations_[state_].mode != "phased") return animations_[state_].mode;
    return QStringList{"start", "loop", "end"}.at(phase_);
}
QString Player::resumeTarget() const {
    if (dragActive_) return dragResume_;
    if (!pending_.isEmpty()) return pending_;
    return animations_.value(state_).mode == "once" ? previous_ : state_;
}
bool Player::select(const QString &state, bool interrupt) {
    if (!animations_.contains(state)) {
        error_ = "Unknown animation state: " + state;
        emit failed(error_); return false;
    }
    if (dragActive_ && state != "dragging") {
        dragResume_ = state;
        return true;
    }
    if (state == state_ && pending_.isEmpty() && !stopped_) return true;
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
void Player::beginDrag() {
    if (dragActive_ || !animations_.contains("dragging")) return;
    dragResume_ = resumeTarget();
    pending_.clear();
    dragActive_ = true;
    enter("dragging");
}
void Player::endDrag() {
    if (!dragActive_) return;
    dragActive_ = false;
    select(dragResume_);
}
void Player::enter(const QString &state) {
    state_ = state;
    stopped_ = false;
    enterSequence(0);
}
void Player::enterSequence(int phase) {
    timer_.stop();
    phase_ = phase;
    sequence_ = animations_.value(state_).sequences.value(phase);
    index_ = 0;
    pixmap_ = {};
    cache_.clear();
    display();
}
void Player::advance() {
    timer_.stop();
    if (stopped_ || !sequences_.contains(sequence_)) return;
    if (++index_ < sequences_[sequence_].size()) { display(); return; }
    const auto animation = animations_[state_];
    if (animation.mode == "phased") {
        if (phase_ == 0) { enterSequence(1); return; }
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
            if (!animations_.contains(target) || animations_[target].mode == "once" || target == "dragging") target = "idle";
            enter(target);
        }
        emit completed(finished);
        return;
    }
    index_ = 0;
    display();
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
        pending_.clear(); dragActive_ = false; enter("idle");
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
