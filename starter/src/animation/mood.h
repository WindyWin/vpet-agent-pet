#pragma once
#include "player.h"
#include <QObject>

namespace pet {
enum class MoodSetting { Off = 0, Cheerful = 1, Full = 2 };

// How the pet feels about recent agent activity. Finished turns raise a score, faster on a streak;
// tool errors lower it; with no news it drifts back to neutral. The score picks the catalog's
// "happy" or "poor" art for idle and fidgets, with a gap between entering and leaving a mood so it
// does not flicker. It also chooses how a finished turn is celebrated, including the occasional treat:
// a snack after a long productive stretch and a bigger celebration on every hundredth finished turn.
// Time is passed in, not read, so tests and the monitor share one clock.
class Mood : public QObject {
    Q_OBJECT
public:
    static constexpr int neutral = 50, happyAt = 70, happyUntil = 60, poorAt = 30, poorUntil = 40;
    // A finished turn adds `finishGain` plus one per turn of its streak, up to `streakBonus` more.
    static constexpr int finishGain = 4, streakBonus = 6, errorLoss = 12;
    static constexpr qint64 recoveryMs = 30000; // One point back toward neutral every 30 seconds.
    // A snack is due after this many finished turns without a break of `breakMs` between two of them.
    static constexpr int snackTurns = 20, milestoneTurns = 100;
    static constexpr qint64 breakMs = 30 * 60 * 1000;
    explicit Mood(Player &player, QObject *parent = nullptr);
    void setSetting(MoodSetting setting);
    MoodSetting setting() const { return setting_; }
    void finished(qint64 now); // A turn finished in any session.
    void failed(qint64 now);   // A tool failed in any session.
    void refresh(qint64 now);  // Lets the score recover and updates the mood shown.
    int score(qint64 now);
    QString level() const { return level_; } // "happy", "poor" or empty for neutral.
    // The state that celebrates a finished turn: a pending treat if one is due and the catalog has it,
    // otherwise one drawn from the "turn_finished" reaction. Falls back to "turn_finished".
    QString celebrate();
    QString treat() const { return treat_; } // "milestone", "snack" or empty.
    // Every finished turn ever seen, persisted so the hundredth survives restarts.
    int turns() const { return turns_; }
    void setTurns(int turns) { turns_ = qMax(0, turns); }
    void setRandom(Random random) { random_ = random ? std::move(random) : systemRandom(); }
signals:
    void counted(); // `turns` changed and is worth saving.
private:
    void settle(qint64 now);
    Player &player_;
    Random random_ = systemRandom();
    MoodSetting setting_ = MoodSetting::Full;
    QString level_, treat_;
    int score_ = neutral, streak_ = 0, sinceSnack_ = 0, turns_ = 0;
    qint64 settled_ = -1, lastFinished_ = -1;
};
}
