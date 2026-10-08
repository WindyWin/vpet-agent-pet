#pragma once
#include "behavior/runtime.h"
#include "player.h"
#include "settings/preferences.h"
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <functional>

namespace pet {
class Stage;
// Surprises tied to the calendar, the clock and a few rare events. Each one is a reaction cue that the pet maps
// to a pool of states, so a pet without that pool skips it:
//   may20, birthday  on that day the first ambient fidget greets with it, and later ones now and then
//   late-night       from 01:00 to 05:00, extra yawning among the fidgets and a bedtime note once a night
//   friday-evening   from Friday 17:00, how a finished turn is celebrated
//   long-turn        a turn that ran for `longTurnMs` or more is celebrated bigger
//   birthday         also celebrates the first finished turn of the birthday
//   danger           a hook saw a destructive shell command start
//   konami           the Konami code typed while the pet has focus
//   monday           configured Monday time: the pet is tired and down about the week ahead (once a day)
//   leave-work       configured weekday time (default 16:45): time to get ready to go home (once a day)
//   sleep            configured daily time (default 22:00): time to go to sleep (once a day)
// Local time comes from a replaceable clock, so tests can visit any date.
class EasterEggs : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 longTurnMs = 15 * 60 * 1000;
    // A surprise holds off session animation for at most this long, even if the player stalls: the behavior
    // runtime's bound on any reaction.
    static constexpr qint64 surpriseMs = behavior::Runtime::oneShotTimeoutMs;
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
    // Which of "may20", "birthday", "late-night" and "friday-evening" hold at this local time. A
    // February 29 birthday is kept on February 28 in other years.
    static QStringList occasionsAt(const QDateTime &local, const QString &birthday);
    QStringList occasions() const { return occasionsAt(clock_(), birthday_); }
    // A state for the ambient scheduler to play instead of an ordinary fidget, or empty.
    QString fidget();
    // The reaction cue to celebrate a finished turn of `turnMs` with (0 when unknown), or empty. The birthday is
    // offered until `cheered()` says its cheer played.
    QString celebration(qint64 turnMs);
    void cheered() { cheeredOn_ = clock_().date(); }
    // Surprises go through the behavior runtime. Unset, none play.
    void setStage(Stage *stage) { stage_ = stage; }
    // Plays a reaction cue now, such as "danger" or "konami", as a Surprise: it plays out unless a session needs
    // the user. False when skipped: turned off (unless `evenWhenOff`, for a cheer the user earned elsewhere), no
    // pool for the cue, or the runtime refused it (the pet is held, hidden, quitting or busy with something bigger).
    bool surprise(const QString &cue, bool evenWhenOff = false);
    bool surprising() const; // One of these surprises is what the runtime shows.
    bool bedtime(); // True once a night, the first time it is asked late at night.
    // Which of "monday", "leave-work" and "sleep" are due at this local time, whether or not already given.
    static QStringList remindersAt(const QDateTime &local, const ReminderSchedule &schedule = {});
    void setReminderSchedule(const ReminderSchedule &schedule) {
        if (schedule.monday.isValid() && schedule.leaveWork.isValid() && schedule.sleep.isValid()) schedule_ = schedule;
    }
    ReminderSchedule reminderSchedule() const { return schedule_; }
    // What the pet says for a reminder; empty for an unknown one.
    static QString reminderNote(const QString &reminder, const ReminderSchedule &schedule = {});
    // The first reminder that is due and not yet given today; empty for none, or when turned off. A reminder
    // is given once a day, or not at all if the pet was not running when it was due.
    QString dueReminder() const;
    void reminded(const QString &reminder); // Given today: it is not due again until tomorrow.
    QString reminder(); // dueReminder(), marked given.
    bool key(int key); // Feeds a key press; true when it completes the Konami code.
    void setClock(std::function<QDateTime()> clock) { if (clock) clock_ = std::move(clock); }
    QDateTime now() const { return clock_(); } // Local time, from the replaceable clock.
    // Replaceable for tests. A pool of one draws nothing; see `fidget` for the other draws.
    void setRandom(Random random) { random_ = random ? std::move(random) : systemRandom(); }
private:
    QString draw(const QString &cue);
    Player &player_;
    Stage *stage_ = nullptr;
    Random random_ = systemRandom();
    std::function<QDateTime()> clock_ = defaultClock;
    bool enabled_ = true;
    ReminderSchedule schedule_;
    QString birthday_;
    QDate greetedOn_, cheeredOn_, bedtimeOn_;
    QHash<QString, QDate> reminded_; // The day each reminder was last given.
    QSet<QString> greeted_; //Occasions that already greeted on `greetedOn_`.
    QVector<int> keys_;
};
}
