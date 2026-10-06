#pragma once
#include <QDateTime>
#include <QString>

namespace pet {
// Wellness reminders: rest your eyes (the 20-20-20 rule) and drink some water, each after its own
// stretch of active time. Activity is what the monitor sees: the user's own (pointer movement, a
// prompt) and the agent's other events. Agent events keep a stretch going only while the user was seen
// within `breakMs`, so an agent working on behind a locked screen does not run the timers. Gaps between activities count up to `graceMs`, so a pause to read or think still
// counts while a longer one pauses the timers; a gap of `breakMs` or more is a real break and starts
// both over. Only the intervals are preferences; the timers live in memory. Time is passed in.
class Wellness {
public:
    static constexpr qint64 graceMs = 60 * 1000, breakMs = 5 * 60 * 1000;
    // Interval choices in minutes, as offered in settings; 0 is off. The second is the default.
    static constexpr int eyeChoices[] = {0, 20, 30, 45}, waterChoices[] = {0, 45, 60, 90};
    static constexpr int defaultEyeMinutes = 20, defaultWaterMinutes = 60;
    // Local hours with no reminders: the late-evening bedtime note and late-night yawns cover them.
    static constexpr int quietFrom = 22, quietUntil = 6;
    static constexpr int eyeRestSeconds = 20; // The countdown after the user takes the eye break.
    static bool validEyeMinutes(int minutes);
    static bool validWaterMinutes(int minutes);
    static bool quietAt(const QDateTime &local);
    // What the pet says for "eyes" or "water"; empty for anything else.
    static QString note(const QString &reminder);
    void setEyeMinutes(int minutes) { if (validEyeMinutes(minutes)) eyeMinutes_ = minutes; }
    void setWaterMinutes(int minutes) { if (validWaterMinutes(minutes)) waterMinutes_ = minutes; }
    int eyeMinutes() const { return eyeMinutes_; }
    int waterMinutes() const { return waterMinutes_; }
    void activity(qint64 now, bool user = true);
    // The user was seen within `graceMs`: someone is there to read a reminder.
    bool present(qint64 now) const { return lastUser_ >= 0 && now >= lastUser_ && now - lastUser_ < graceMs; }
    // Active time counted toward each reminder, up to `now`; 0 once a real break has begun.
    qint64 eyesActiveMs(qint64 now) const { return counted(eyes_, now); }
    qint64 waterActiveMs(qint64 now) const { return counted(water_, now); }
    // "eyes" or "water" when one is due, eyes first; empty for none. Due stays due until `given`.
    QString due(qint64 now) const;
    // The reminder was given (shown, or answered some other way); its timer starts over.
    void given(const QString &reminder, qint64 now);
    // A break seen some other way, such as a locked screen: both timers start over at the next activity.
    void reset() { last_ = -1; eyes_ = water_ = 0; }
private:
    qint64 pending(qint64 now) const;
    qint64 counted(qint64 base, qint64 now) const;
    int eyeMinutes_ = defaultEyeMinutes, waterMinutes_ = defaultWaterMinutes;
    qint64 last_ = -1, lastUser_ = -1, eyes_ = 0, water_ = 0;
};
}
