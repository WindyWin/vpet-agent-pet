#pragma once
#include "player.h"
#include <QDateTime>
#include <QObject>
#include <QSet>
#include <functional>

namespace pet {
// Surprises tied to the calendar, the clock and a few rare events. Each one plays a reaction pool from
// the catalog, so a catalog without that pool skips it:
//   may20, birthday  on that day the first ambient fidget greets with it, and later ones now and then
//   late_night       from 01:00 to 05:00, extra yawning among the fidgets and a bedtime note once a night
//   friday_evening   from Friday 17:00, how a finished turn is celebrated
//   long_turn        a turn that ran for `longTurnMs` or more is celebrated bigger
//   birthday         also celebrates the first finished turn of the birthday
//   danger           a hook saw a destructive shell command start
//   konami           the Konami code typed while the pet has focus
// Local time comes from a replaceable clock, so tests can visit any date.
class EasterEggs : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 longTurnMs = 15 * 60 * 1000;
    // A surprise holds off session animation for at most this long, even if the player stalls.
    static constexpr qint64 surpriseMs = 15000;
    // On a special day, one later fidget in this many is the day's own; at night, one in this many yawns.
    static constexpr int occasionOneIn = 4, lateNightOneIn = 2;
    static constexpr int lateNightFrom = 1, lateNightUntil = 5, fridayEveningFrom = 17; // Hours, local time.
    // The clock every new instance starts with; tests replace it to run at an ordinary time.
    inline static std::function<QDateTime()> defaultClock = [] { return QDateTime::currentDateTime(); };
    explicit EasterEggs(Player &player, QObject *parent = nullptr);
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }
    // "MM-dd", or empty for none; anything else is refused.
    bool setBirthday(const QString &monthDay);
    QString birthday() const { return birthday_; }
    // Which of "may20", "birthday", "late_night" and "friday_evening" hold at this local time. A
    // February 29 birthday is kept on February 28 in other years.
    static QStringList occasionsAt(const QDateTime &local, const QString &birthday);
    QStringList occasions() const { return occasionsAt(clock_(), birthday_); }
    // A state for the ambient scheduler to play instead of an ordinary fidget, or empty.
    QString fidget();
    // The reaction pool to celebrate a finished turn of `turnMs` with (0 when unknown), or empty.
    QString celebration(qint64 turnMs);
    // Plays a pool now, such as "danger" or "konami". It plays out unless a session needs the user.
    // False when skipped: turned off, no such pool, or the pet is held or stopped.
    bool surprise(const QString &pool);
    bool surprising() const; // A surprise is still what the pet shows.
    bool bedtime(); // True once a night, the first time it is asked late at night.
    bool key(int key); // Feeds a key press; true when it completes the Konami code.
    void setClock(std::function<QDateTime()> clock) { if (clock) clock_ = std::move(clock); }
    // Replaceable for tests. A pool of one draws nothing; see `fidget` for the other draws.
    void setRandom(Random random) { random_ = random ? std::move(random) : systemRandom(); }
private:
    QString draw(const QString &pool);
    Player &player_;
    Random random_ = systemRandom();
    std::function<QDateTime()> clock_ = defaultClock;
    bool enabled_ = true;
    QString birthday_, surprise_;
    qint64 surpriseUntil_ = 0;
    QDate greetedOn_, cheeredOn_, bedtimeOn_;
    QSet<QString> greeted_; // Occasions that already greeted on `greetedOn_`.
    QVector<int> keys_;
};
}
