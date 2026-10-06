#include "activity.h"
#include <QDateTime>

namespace pet {
Activity::Activity(Player &player, QObject *parent)
    : QObject(parent), player_(player), clock_([] { return QDateTime::currentMSecsSinceEpoch(); }) {
    connect(&player_, &Player::entered, this, &Activity::entered);
    connect(&player_, &Player::looped, this, &Activity::looped);
    player_.setContinuity(style_ != ActivityStyle::Classic);
    player_.setReactionGate([this] { return react(); });
}
Activity::~Activity() { player_.setReactionGate({}); }
QPair<int, int> Activity::gapSeconds(ActivityStyle style) {
    return style == ActivityStyle::Playful ? QPair<int, int>{6, 12} : QPair<int, int>{10, 18};
}
void Activity::setStyle(ActivityStyle style) {
    if (style == style_) return;
    style_ = style;
    player_.setContinuity(style != ActivityStyle::Classic);
    if (nextDue_ >= 0) nextDue_ = style == ActivityStyle::Classic ? 0 : clock_() + gapMs(); // A new pace starts from now.
}
void Activity::setClock(std::function<qint64()> milliseconds) {
    if (milliseconds) clock_ = std::move(milliseconds);
}
qint64 Activity::gapMs() {
    const auto [low, high] = gapSeconds(style_);
    return qint64(low + qBound(0, random_(high - low + 1), high - low)) * 1000;
}
void Activity::entered(const QString &state) {
    // One clock runs from the first activity to the last, across the transitions between them.
    if (!player_.activity(state)) nextDue_ = -1;
    else if (nextDue_ < 0) nextDue_ = style_ == ActivityStyle::Classic ? 0 : clock_() + gapMs();
}
void Activity::looped(const QString &state) {
    const auto *art = player_.activity(state);
    if (style_ == ActivityStyle::Classic || !art || nextDue_ < 0 || clock_() < nextDue_) return;
    QVector<const ActivityChoice *> pool;
    for (const auto &loop : art->loops)
        if (!loop.playful || style_ == ActivityStyle::Playful) pool.append(&loop);
    if (pool.size() > 1) pool.removeIf([this](const ActivityChoice *loop) { return loop->sequence == last_; });
    if (!pool.isEmpty()) {
        int total = 0;
        for (const auto *loop : pool) total += loop->weight;
        int roll = qBound(0, random_(total), total - 1);
        const ActivityChoice *pick = pool.first();
        for (const auto *loop : pool) {
            if (roll < loop->weight) { pick = loop; break; }
            roll -= loop->weight;
        }
        if (player_.vary(pick->sequence)) last_ = pick->sequence;
    }
    nextDue_ = clock_() + gapMs();
}
bool Activity::react() {
    if (style_ != ActivityStyle::Playful || nextDue_ < 0 || clock_() < nextDue_) return false;
    nextDue_ = clock_() + gapMs();
    return true;
}
}
