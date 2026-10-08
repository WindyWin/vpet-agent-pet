#include "mood.h"
#include <climits>

namespace pet {
Mood::Mood(Player &player, QObject *parent) : QObject(parent), player_(player) {}
void Mood::setSetting(MoodSetting setting) {
    setting_ = setting;
    refresh(settled_ < 0 ? 0 : settled_);
}
void Mood::settle(qint64 now) {
    if (settled_ < 0 || score_ == neutral || now < settled_) { settled_ = qMax(settled_, now); return; }
    // Whole recovery steps only; the remainder carries over to the next call.
    const auto steps = (now - settled_) / recoveryMs;
    const auto distance = qAbs(score_ - neutral);
    if (steps >= distance) { score_ = neutral; settled_ = now; return; }
    score_ += score_ > neutral ? -int(steps) : int(steps);
    settled_ += steps * recoveryMs;
}
int Mood::score(qint64 now) { settle(now); return score_; }
void Mood::finished(qint64 now) {
    settle(now);
    streak_ = qMin(streak_ + 1, streakBonus);
    score_ = qMin(100, score_ + finishGain + streak_);
    if (lastFinished_ >= 0 && now - lastFinished_ > breakMs) sinceSnack_ = 0; // A break already happened.
    lastFinished_ = now;
    ++sinceSnack_;
    if (turns_ < INT_MAX) ++turns_;
    if (turns_ % milestoneTurns == 0) treat_ = "milestone";
    else if (sinceSnack_ >= snackTurns && treat_.isEmpty()) treat_ = "snack";
    if (treat_ == "snack") sinceSnack_ = 0;
    emit counted();
    refresh(now);
}
void Mood::failed(qint64 now) {
    settle(now);
    streak_ = 0;
    score_ = qMax(0, score_ - errorLoss);
    refresh(now);
}
void Mood::refresh(qint64 now) {
    settle(now);
    QString level;
    if (setting_ != MoodSetting::Off) {
        if (score_ >= (level_ == "happy" ? happyUntil : happyAt)) level = "happy";
        else if (setting_ == MoodSetting::Full && score_ <= (level_ == "poor" ? poorUntil : poorAt)) level = "poor";
    }
    level_ = level;
    player_.setMood(level_);
}
void Mood::keep(const QString &cue) {
    if (cue == "milestone" || (cue == "snack" && treat_.isEmpty())) treat_ = cue;
}
Mood::Celebration Mood::celebrate(const QString &occasion) {
    const bool occasional = treat_ != "milestone" && !player_.pool(occasion).isEmpty();
    auto cue = occasional ? occasion : treat_;
    auto pool = player_.pool(cue);
    if (!occasional) treat_.clear();
    if (pool.isEmpty()) { cue = "celebrate"; pool = player_.pool(cue); }
    if (pool.isEmpty()) return {"turn-finished", player_.stateFor("turn-finished")};
    return {cue, drawReaction(pool, random_)};
}
}
