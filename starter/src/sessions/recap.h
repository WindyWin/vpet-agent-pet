#pragma once
#include "state.h"
#include <QDate>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

namespace pet {
// What the agents did on one local day. Only counts and project folder names: never prompts,
// tools or commands.
struct RecapDay {
    QDate date;
    int turns = 0, errors = 0, approvals = 0;
    int longWaits = 0; // Approvals answered after `Recap::longWaitMs` or more.
    qint64 longestTurnMs = 0, longestWaitMs = 0;
    QMap<QString, int> projects; // Finished turns per project folder name.
};
// Daily counters for the pet's recap, built from the events `Sessions` accepted. Turns, errors and
// approval requests count on the day they arrive; a wait counts on the day it is answered.
class Recap {
public:
    static constexpr int keepDays = 14, maxProjects = 64;
    static constexpr qint64 longWaitMs = 10 * 60 * 1000;
    /// Counts an accepted event using its post-event session, or null after it ends.
    /// Returns true when counters are updated, even for a day too old to retain.
    bool record(const Event &event, const Session *session, const QDate &day);
    /// Returns counters for date, or zero counters carrying that date when it is absent.
    RecapDay day(const QDate &date) const; // Empty counters for a day without any.
    /// Returns retained days in ascending date order, with at most keepDays entries.
    const QVector<RecapDay> &days() const { return days_; }
    /// Formats today's counts and notable durations as a compact speech-bubble summary.
    static QString summary(const RecapDay &day);
    /// Lists turns per project, busiest first, then errors, approvals and the longest run.
    static QString breakdown(const RecapDay &day);
    /// Serializes retained daily counters as version 1 JSON; pending approval waits are omitted.
    QJsonObject toJson() const;
    /// Replaces recap with validated version 1 JSON; returns false and leaves it unchanged on failure.
    static bool fromJson(const QJsonObject &object, Recap &recap); // False leaves `recap` unchanged.
private:
    /// Finds or inserts a retained day, using a scratch day if date predates a full history.
    RecapDay &at(const QDate &date);
    QVector<RecapDay> days_; // Oldest first, at most `keepDays`.
    RecapDay discarded_;
    QMap<QString, qint64> waiting_; // Session key → when its approval request arrived. In memory only.
};
// recap.json beside preferences.json, written atomically. Only the pet writes it.
class RecapStore {
public:
    /// Uses path for recap storage; an empty path disables loading and saving.
    explicit RecapStore(QString path) : path_(std::move(path)) {}
    /// Returns stored counters, or an empty recap if the file is unavailable, oversized or invalid.
    Recap load() const; // Empty when missing or invalid; the next save replaces an invalid file.
    /// Atomically writes recap, creating parent directories; returns false if disabled or unsuccessful.
    bool save(const Recap &recap) const;
    /// Returns the configured recap file path, which may be empty to disable persistence.
    QString path() const { return path_; }
private:
    QString path_;
};
}
