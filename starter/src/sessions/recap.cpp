#include "recap.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace pet {
static QString projectFolder(const QString &path) {
    if (path.isEmpty()) return "Unknown project";
    const auto cleaned = QDir::cleanPath(path);
    const auto name = QFileInfo(cleaned).fileName();
    return name.isEmpty() ? cleaned : name;
}
static QString plural(qint64 n, const QString &one, const QString &many) { return QString("%1 %2").arg(n).arg(n == 1 ? one : many); }
static QString duration(qint64 ms) {
    const qint64 minutes = ms / 60000;
    if (minutes < 60) return QString("%1 min").arg(minutes);
    return minutes % 60 ? QString("%1 h %2 min").arg(minutes / 60).arg(minutes % 60) : QString("%1 h").arg(minutes / 60);
}
RecapDay &Recap::at(const QDate &date) {
    auto it = std::find_if(days_.begin(), days_.end(), [&](const RecapDay &d) { return d.date == date; });
    if (it != days_.end()) return *it;
    // A clock set back before every kept day counts into a day that is never kept.
    if (days_.size() >= keepDays && date < days_.first().date) { discarded_ = {}; discarded_.date = date; return discarded_; }
    if (days_.size() >= keepDays) days_.removeFirst();
    RecapDay day; day.date = date;
    return *days_.insert(std::upper_bound(days_.begin(), days_.end(), date,
                                          [](const QDate &d, const RecapDay &r) { return d < r.date; }), day);
}
RecapDay Recap::day(const QDate &date) const {
    for (const auto &d : days_) if (d.date == date) return d;
    RecapDay empty; empty.date = date; return empty;
}
bool Recap::record(const Event &e, const Session *s, const QDate &date) {
    const auto k = e.provider + QChar(0x1f) + e.session;
    bool changed = false;
    // An approval is answered once its session moves on; one that ends with the session was not.
    if (waiting_.contains(k) && (!s || s->state != "attention")) {
        const qint64 waited = std::max<qint64>(0, e.timestamp - waiting_.take(k));
        if (s) {
            auto &d = at(date);
            d.longWaits += waited >= longWaitMs; d.longestWaitMs = std::max(d.longestWaitMs, waited);
            changed = true;
        }
    }
    if (!s) return changed;
    // Questions for the user ("input") are not approvals.
    if (e.kind == "attention" && e.reason != "input" && !waiting_.contains(k)) {
        if (waiting_.size() >= Sessions::maxSessions)
            waiting_.erase(std::min_element(waiting_.begin(), waiting_.end()));
        waiting_[k] = e.timestamp; ++at(date).approvals;
        return true;
    }
    if (e.kind == "error") { ++at(date).errors; return true; }
    if (e.kind == "turn_finished") {
        auto &d = at(date);
        ++d.turns; d.longestTurnMs = std::max(d.longestTurnMs, s->lastTurnMs);
        const auto project = projectFolder(s->project);
        if (d.projects.contains(project) || d.projects.size() < maxProjects) ++d.projects[project];
        return true;
    }
    return changed;
}
QString Recap::summary(const RecapDay &d) {
    if (d.turns == 0 && d.errors == 0 && d.approvals == 0) return "No agent work yet today.";
    QString text = "Today: " + plural(d.turns, "turn", "turns");
    if (d.projects.size() == 1) text += " in " + d.projects.firstKey();
    else if (d.projects.size() > 1) text += " across " + plural(d.projects.size(), "project", "projects");
    QStringList parts{text};
    if (d.errors) parts << plural(d.errors, "error", "errors");
    if (d.longWaits) parts << plural(d.longWaits, "approval", "approvals") + " waited 10+ min";
    else if (d.approvals) parts << plural(d.approvals, "approval", "approvals");
    if (d.longestTurnMs >= 60000) parts << "longest run " + duration(d.longestTurnMs);
    return parts.join(" · ");
}
QString Recap::breakdown(const RecapDay &d) {
    QVector<QPair<QString, int>> projects;
    for (auto it = d.projects.begin(); it != d.projects.end(); ++it) projects.append({it.key(), it.value()});
    std::stable_sort(projects.begin(), projects.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    QStringList lines{"Today's recap"};
    for (const auto &[name, turns] : projects) lines << name + " · " + plural(turns, "turn", "turns");
    if (projects.isEmpty()) lines << "No finished turns yet.";
    QStringList totals;
    if (d.errors) totals << plural(d.errors, "error", "errors");
    if (d.approvals) totals << plural(d.approvals, "approval", "approvals") +
                               (d.longestWaitMs >= 60000 ? ", longest wait " + duration(d.longestWaitMs) : QString());
    if (d.longestTurnMs >= 60000) totals << "longest run " + duration(d.longestTurnMs);
    if (!totals.isEmpty()) lines << totals.join(" · ");
    return lines.join('\n');
}
QJsonObject Recap::toJson() const {
    QJsonArray days;
    for (const auto &d : days_) {
        QJsonObject projects;
        for (auto it = d.projects.begin(); it != d.projects.end(); ++it) projects[it.key()] = it.value();
        days.append(QJsonObject{{"date", d.date.toString(Qt::ISODate)}, {"turns", d.turns}, {"errors", d.errors},
                                {"approvals", d.approvals}, {"long_waits", d.longWaits},
                                {"longest_turn_ms", double(d.longestTurnMs)}, {"longest_wait_ms", double(d.longestWaitMs)},
                                {"projects", projects}});
    }
    return {{"version", 1}, {"days", days}};
}
bool Recap::fromJson(const QJsonObject &object, Recap &recap) {
    auto count = [](const QJsonValue &value, double high, qint64 &out) {
        const double n = value.toDouble(-1);
        if (!value.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < 0 || n > high) return false;
        out = qint64(n); return true;
    };
    if (object["version"].toInt() != 1 || !object["days"].isArray()) return false;
    const auto days = object["days"].toArray();
    if (days.size() > keepDays) return false;
    Recap result;
    for (const auto &value : days) {
        const auto o = value.toObject();
        RecapDay d; d.date = QDate::fromString(o["date"].toString(), Qt::ISODate);
        qint64 turns, errors, approvals, longWaits;
        if (!value.isObject() || !d.date.isValid() || !o["projects"].isObject() ||
            (!result.days_.isEmpty() && d.date <= result.days_.last().date) ||
            !count(o["turns"], 2147483647, turns) || !count(o["errors"], 2147483647, errors) ||
            !count(o["approvals"], 2147483647, approvals) || !count(o["long_waits"], 2147483647, longWaits) ||
            !count(o["longest_turn_ms"], 9007199254740991.0, d.longestTurnMs) ||
            !count(o["longest_wait_ms"], 9007199254740991.0, d.longestWaitMs))
            return false;
        d.turns = int(turns); d.errors = int(errors); d.approvals = int(approvals); d.longWaits = int(longWaits);
        const auto projects = o["projects"].toObject();
        if (projects.size() > maxProjects) return false;
        for (auto it = projects.begin(); it != projects.end(); ++it) {
            qint64 n;
            if (it.key().isEmpty() || it.key().size() > 256 || !count(it.value(), 2147483647, n)) return false;
            d.projects[it.key()] = int(n);
        }
        result.days_.append(d);
    }
    recap = result;
    return true;
}
Recap RecapStore::load() const {
    Recap recap;
    QFile file(path_);
    if (path_.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > 1048576) return recap;
    Recap::fromJson(QJsonDocument::fromJson(file.readAll()).object(), recap);
    return recap;
}
bool RecapStore::save(const Recap &recap) const {
    if (path_.isEmpty() || !QDir().mkpath(QFileInfo(path_).absolutePath())) return false;
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(recap.toJson()).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}
}
