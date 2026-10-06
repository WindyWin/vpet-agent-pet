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
    // Counts an accepted event; `session` is its record after the event, or null once it ended.
    // True when a counter changed.
    bool record(const Event &event, const Session *session, const QDate &day);
    RecapDay day(const QDate &date) const; // Empty counters for a day without any.
    const QVector<RecapDay> &days() const { return days_; }
    // "Today: 38 turns across 3 projects · 2 approvals waited 10+ min · longest run 22 min"
    static QString summary(const RecapDay &day);
    // A few lines: turns per project, busiest first, then errors, approvals and the longest run.
    static QString breakdown(const RecapDay &day);
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Recap &recap); // False leaves `recap` unchanged.
private:
    RecapDay &at(const QDate &date);
    QVector<RecapDay> days_; // Oldest first, at most `keepDays`.
    RecapDay discarded_;
    QMap<QString, qint64> waiting_; // Session key → when its approval request arrived. In memory only.
};
// recap.json beside preferences.json, written atomically. Only the pet writes it.
class RecapStore {
public:
    explicit RecapStore(QString path) : path_(std::move(path)) {}
    Recap load() const; // Empty when missing or invalid; the next save replaces an invalid file.
    bool save(const Recap &recap) const;
    QString path() const { return path_; }
private:
    QString path_;
};
}
