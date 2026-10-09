#pragma once
#include <QDateTime>
#include <QHash>
#include <QString>

namespace pet {
// Snooze (#34): a way to silence the pet for a while without touching settings. While it is on, no alert
// bubble, sound, reminder or remark shows; the attention badge and the animations keep following the sessions.
// A snooze lasts for a time, until the agent's turn finishes, or until tomorrow morning; it lives in memory only
// (a restart clears it). Time is passed in.
class Snooze {
public:
    static constexpr int minuteChoices[] = {15, 30, 60};
    static constexpr int tomorrowHour = 6; // "Until tomorrow" ends with the quiet hours.
    // When "until tomorrow" ends, counted from the local time `local`.
    static qint64 tomorrowMs(const QDateTime &local);
    void start(qint64 now, qint64 ms) { until_ = now + ms; untilTurn_ = false; }
    void startUntilTurnEnds() { until_ = 0; untilTurn_ = true; }
    void resume() { until_ = 0; untilTurn_ = false; }
    // A turn finished: a snooze waiting for it is over.
    void turnFinished() { untilTurn_ = false; }
    bool activeAt(qint64 now) const { return untilTurn_ || now < until_; }
    bool untilTurnEnds() const { return untilTurn_; }
    // Milliseconds left of a timed snooze; 0 for none, or for one that waits for the turn to end.
    qint64 remaining(qint64 now) const { return !untilTurn_ && now < until_ ? until_ - now : 0; }
private:
    qint64 until_ = 0;
    bool untilTurn_ = false;
};

// A reminder that asks to be confirmed (water, eyes, lunch, go home) and is not forgotten when ignored: it stays on
// screen for `askMs`, and unless the user answers it comes back after `laterMs`, up to `maxAsks` times in all; "Later"
// asks again after `laterMs` without using one up. Holding is bookkeeping only: the caller decides what "given" means.
class Nudges {
public:
    static constexpr qint64 askMs = 60 * 1000, laterMs = 10 * 60 * 1000;
    static constexpr int maxAsks = 3;
    // Not to be submitted again yet: it is on screen, or waiting for its next turn.
    bool held(const QString &key, qint64 now) const;
    void shown(const QString &key); // Its bubble went up; counts as one ask.
    void later(const QString &key, qint64 now); // Taken down without an answer, or put off: asks again in `laterMs`.
    // The user let it fade. True once it has used up its asks, and the caller should treat it as given.
    bool ignored(const QString &key, qint64 now);
    void forget(const QString &key) { entries_.remove(key); } // Answered, skipped, or no longer due.
    // Only `showing` is on screen (empty for none): anything else marked as shown has been replaced.
    void settle(const QString &showing, qint64 now);
    int asks(const QString &key) const { return entries_.value(key).asks; }
private:
    struct Entry { int asks = 0; bool showing = false; qint64 notBefore = 0; };
    QHash<QString, Entry> entries_;
};
}
