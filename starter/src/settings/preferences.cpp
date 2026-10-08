#include "preferences.h"
#include "i18n/contexts.h"
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace pet {
QPoint Preferences::visiblePosition(QPoint position, QSize size, const QVector<QRect> &screens) {
    if (screens.isEmpty()) return position;
    const QRect window(position, size);
    QRect target = screens.first();
    qint64 largest = 0;
    for (const auto &screen : screens) {
        if (screen.contains(window)) return position;
        const auto intersection = screen.intersected(window);
        const qint64 area = qint64(intersection.width()) * intersection.height();
        if (area > largest) { largest = area; target = screen; }
    }
    if (largest == 0) position = target.bottomRight() - QPoint(size.width() + 23, size.height() + 23);
    return {qBound(target.left(), position.x(), qMax(target.left(), target.right() - size.width() + 1)),
            qBound(target.top(), position.y(), qMax(target.top(), target.bottom() - size.height() + 1))};
}
bool Preferences::validBirthday(const QString &monthDay) {
    // A leap year, so February 29 is a date.
    return monthDay.size() == 5 && QDate::fromString("2000-" + monthDay, "yyyy-MM-dd").isValid();
}
bool Preferences::validPet(const QString &id) {
    static const QRegularExpression pattern(QRegularExpression::anchoredPattern("[a-z0-9-]{1,32}"));
    return pattern.match(id).hasMatch();
}
PreferencesStore::PreferencesStore(QString path) : path_(std::move(path)) {
    if (path_.isEmpty()) path_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/preferences.json";
}
Preferences PreferencesStore::load() {
    Preferences result;
    QFile file(path_);
    if (!file.exists()) return result;
    QJsonParseError parse;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) {
        writable_ = false; error_ = Settings::tr("Cannot read preferences; existing file preserved."); return result;
    }
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto object = document.object();
    auto integer = [](const QJsonValue &value, int low, int high) {
        const double n = value.toDouble(low - 1.0);
        return value.isDouble() && std::isfinite(n) && n == std::floor(n) && n >= low && n <= high;
    };
    const auto validTime = [&object](const QString &key) {
        if (!object.contains(key)) return true;
        const auto value = object[key];
        const auto time = QTime::fromString(value.toString(), "HH:mm");
        return value.isString() && time.isValid() && time.toString("HH:mm") == value.toString();
    };
    // A file created by `agent-pet autostart` before the pet ever ran has no position yet.
    const bool position = object.contains("x") || object.contains("y");
    IdlePolicy whenIdle = IdlePolicy::Keep;
    if (parse.error != QJsonParseError::NoError || object["version"].toInt() != 1
        || !integer(object["size"], 160, 320) || !object["on_top"].isBool()
        || (position && (!integer(object["x"], -1000000, 1000000) || !integer(object["y"], -1000000, 1000000)))
        // Notification keys were added in M5 and startup keys later; earlier files omit them.
        || (object.contains("muted") && !object["muted"].isBool()) || (object.contains("sound") && !object["sound"].isBool())
        || (object.contains("bubbles") && !integer(object["bubbles"], 0, 2))
        || (object.contains("ambient") && !integer(object["ambient"], 0, 2))
        || (object.contains("activity") && !integer(object["activity"], 0, 2))
        || (object.contains("mood") && !integer(object["mood"], 0, 2))
        || (object.contains("turns") && !integer(object["turns"], 0, 2147483647))
        || (object.contains("touch") && !object["touch"].isBool())
        || (object.contains("wander") && !object["wander"].isBool())
        || (object.contains("easter_eggs") && !object["easter_eggs"].isBool())
        || (object.contains("birthday") && !Preferences::validBirthday(object["birthday"].toString()))
        || (object.contains("eye_minutes") && !integer(object["eye_minutes"], 0, 1440))
        || (object.contains("water_minutes") && !integer(object["water_minutes"], 0, 1440))
        || !validTime("lunch_time") || !validTime("monday_time") || !validTime("leave_work_time") || !validTime("sleep_time")
        || (object.contains("recap") && !object["recap"].isBool())
        || (object.contains("autostart") && !object["autostart"].isBool())
        || (object.contains("language") && !object["language"].isString())
        || (object.contains("pet") && !object["pet"].isString())
        || (object.contains("when_idle") && !parseIdlePolicy(object["when_idle"].toString(), whenIdle))) {
        writable_ = false; error_ = Settings::tr("Invalid preferences; using defaults and preserving the file."); return result;
    }
    result.size = object["size"].toInt();
    if (position) result.position = {object["x"].toInt(), object["y"].toInt()};
    result.hasPosition = position;
    result.onTop = object["on_top"].toBool();
    result.muted = object["muted"].toBool();
    result.sound = object["sound"].toBool();
    result.bubbles = object["bubbles"].toInt(Preferences::RequestsAndErrors);
    result.ambient = object["ambient"].toInt(Preferences::AmbientSubtle);
    result.activity = object["activity"].toInt(Preferences::ActivityPlayful);
    result.mood = object["mood"].toInt(Preferences::MoodFull);
    result.turns = object["turns"].toInt(0);
    result.touch = object["touch"].toBool(true);
    result.wander = object["wander"].toBool(true);
    result.easterEggs = object["easter_eggs"].toBool(true);
    result.birthday = object["birthday"].toString();
    result.eyeMinutes = object["eye_minutes"].toInt(result.eyeMinutes);
    result.waterMinutes = object["water_minutes"].toInt(result.waterMinutes);
    if (object.contains("lunch_time")) result.reminderSchedule.lunch = QTime::fromString(object["lunch_time"].toString(), "HH:mm");
    if (object.contains("monday_time")) result.reminderSchedule.monday = QTime::fromString(object["monday_time"].toString(), "HH:mm");
    if (object.contains("leave_work_time")) result.reminderSchedule.leaveWork = QTime::fromString(object["leave_work_time"].toString(), "HH:mm");
    if (object.contains("sleep_time")) result.reminderSchedule.sleep = QTime::fromString(object["sleep_time"].toString(), "HH:mm");
    result.recap = object["recap"].toBool(true);
    result.autostart = object["autostart"].toBool();
    result.whenIdle = whenIdle;
    const auto language = object["language"].toString();
    if (language == "en" || language == "vi") result.language = language;
    if (const auto pet = object["pet"].toString(); Preferences::validPet(pet)) result.pet = pet;
    return result;
}
bool PreferencesStore::save(const Preferences &preferences) {
    if (!writable_) return false;
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
        error_ = Settings::tr("Cannot create preferences directory."); return false;
    }
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) { error_ = file.errorString(); return false; }
    QJsonObject object{{"version", 1}, {"size", preferences.size}, {"on_top", preferences.onTop},
                       {"muted", preferences.muted}, {"sound", preferences.sound}, {"bubbles", preferences.bubbles}, {"ambient", preferences.ambient}, {"activity", preferences.activity},
                       {"mood", preferences.mood}, {"turns", preferences.turns}, {"touch", preferences.touch},
                       {"wander", preferences.wander}, {"easter_eggs", preferences.easterEggs}, {"recap", preferences.recap},
                       {"eye_minutes", preferences.eyeMinutes}, {"water_minutes", preferences.waterMinutes},
                       {"autostart", preferences.autostart}, {"when_idle", idlePolicyName(preferences.whenIdle)},
                       {"language", preferences.language},
                       {"lunch_time", preferences.reminderSchedule.lunch.toString("HH:mm")},
                       {"monday_time", preferences.reminderSchedule.monday.toString("HH:mm")},
                       {"leave_work_time", preferences.reminderSchedule.leaveWork.toString("HH:mm")},
                       {"sleep_time", preferences.reminderSchedule.sleep.toString("HH:mm")},
                       {"pet", Preferences::validPet(preferences.pet) ? preferences.pet : QString("vpet")}};
    if (Preferences::validBirthday(preferences.birthday)) object["birthday"] = preferences.birthday;
    if (preferences.hasPosition) { object["x"] = preferences.position.x(); object["y"] = preferences.position.y(); }
    const auto bytes = QJsonDocument(object).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) { error_ = file.errorString(); return false; }
    error_.clear(); return true;
}
}
