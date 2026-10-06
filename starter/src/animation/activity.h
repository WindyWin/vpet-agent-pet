#pragma once
#include "player.h"
#include <QObject>
#include <functional>

namespace pet {
enum class ActivityStyle { Classic = 0, Subtle = 1, Playful = 2 };

// Paces the decoration of thinking, reading and working: now and then a loop pass plays one of the
// state's alternates, and in Playful the same opportunity may go to a reaction to a real transition
// instead, so it reacts at most once per transition and not to every tool call. Desk continuity
// (handover, linger) is the player's; this class only switches it on. It runs on the player's loop
// passes, so a paused or hidden pet does nothing. The art comes from the catalog's "activity" section.
class Activity : public QObject {
    Q_OBJECT
public:
    explicit Activity(Player &player, QObject *parent = nullptr);
    ~Activity() override;
    void setStyle(ActivityStyle style);
    ActivityStyle style() const { return style_; }
    // Replaceable for tests. The draws are: a gap when an activity starts, on a style change and after each
    // alternate or reaction; per alternate, the weighted pick before its gap.
    void setRandom(Random random) { random_ = random ? std::move(random) : systemRandom(); }
    void setClock(std::function<qint64()> milliseconds);
    // The shortest and longest wait between opportunities, in seconds.
    static QPair<int, int> gapSeconds(ActivityStyle style);
private:
    void entered(const QString &state);
    void looped(const QString &state);
    bool react();
    qint64 gapMs();
    Player &player_;
    ActivityStyle style_ = ActivityStyle::Playful;
    Random random_ = systemRandom();
    std::function<qint64()> clock_;
    qint64 nextDue_ = -1; // -1 outside an activity state.
    QString last_; // The previous alternate, not drawn twice in a row.
};
}
