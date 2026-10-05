#pragma once
#include "player.h"
#include <QObject>
#include <functional>

namespace pet {
enum class AmbientLevel { Off = 0, Subtle = 1, Lively = 2 };

// Plays a random fidget now and then while the pet idles, and lets it doze off after a long quiet
// spell. It runs on the player's idle loop, so a paused or hidden pet does nothing, and any other
// state ends the idle clock. Fidgets and their tiers come from the catalog's "ambient" section.
class Ambient : public QObject {
    Q_OBJECT
public:
    // One fidget in this many draws comes from the rare pool.
    static constexpr int rareOneIn = 30;
    explicit Ambient(Player &player, QObject *parent = nullptr);
    void setLevel(AmbientLevel level);
    AmbientLevel level() const { return level_; }
    // A fidget or an ambient nap is showing; session-driven animation should leave it alone.
    bool resting() const { return fidgeting_ || asleep_; }
    // Milliseconds since the pet last did anything but idle, or -1 while it is busy.
    qint64 idleFor() const { return idleSince_ < 0 ? -1 : clock_() - idleSince_; }
    // Replaceable for tests. Per fidget the draws are: the gap (when idle starts or a fidget ends),
    // then the rare roll (only when a rare fidget exists), then the weighted pick.
    void setRandom(Random random) { random_ = random ? std::move(random) : systemRandom(); }
    void setClock(std::function<qint64()> milliseconds);
    // The shortest and longest wait between fidgets, in seconds.
    static QPair<int, int> gapSeconds(AmbientLevel level);
private:
    void entered(const QString &state);
    void looped(const QString &state);
    qint64 gapMs();
    QString pick(qint64 idleMs);
    Player &player_;
    AmbientLevel level_ = AmbientLevel::Subtle;
    Random random_ = systemRandom();
    std::function<qint64()> clock_;
    qint64 idleSince_ = -1, nextDue_ = 0;
    QString last_;
    bool fidgeting_ = false, asleep_ = false, napping_ = false;
};
}
