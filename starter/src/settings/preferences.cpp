#include "preferences.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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
PreferencesStore::PreferencesStore(QString path) : path_(std::move(path)) {
    if (path_.isEmpty()) path_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/preferences.json";
}
Preferences PreferencesStore::load() {
    Preferences result;
    QFile file(path_);
    if (!file.exists()) return result;
    QJsonParseError parse;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) {
        writable_ = false; error_ = "Cannot read preferences; existing file preserved."; return result;
    }
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto object = document.object();
    auto integer = [](const QJsonValue &value, int low, int high) {
        const double n = value.toDouble(low - 1.0);
        return value.isDouble() && std::isfinite(n) && n == std::floor(n) && n >= low && n <= high;
    };
    if (parse.error != QJsonParseError::NoError || object["version"].toInt() != 1
        || !integer(object["size"], 160, 320) || !object["on_top"].isBool()
        || !integer(object["x"], -1000000, 1000000) || !integer(object["y"], -1000000, 1000000)
        // Notification keys were added in M5; files written earlier omit them.
        || (object.contains("muted") && !object["muted"].isBool()) || (object.contains("sound") && !object["sound"].isBool())) {
        writable_ = false; error_ = "Invalid preferences; using defaults and preserving the file."; return result;
    }
    result.size = object["size"].toInt();
    result.position = {object["x"].toInt(), object["y"].toInt()};
    result.hasPosition = true;
    result.onTop = object["on_top"].toBool();
    result.muted = object["muted"].toBool();
    result.sound = object["sound"].toBool();
    return result;
}
bool PreferencesStore::save(const Preferences &preferences) {
    if (!writable_) return false;
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
        error_ = "Cannot create preferences directory."; return false;
    }
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) { error_ = file.errorString(); return false; }
    const QJsonObject object{{"version", 1}, {"size", preferences.size}, {"on_top", preferences.onTop},
                             {"x", preferences.position.x()}, {"y", preferences.position.y()},
                             {"muted", preferences.muted}, {"sound", preferences.sound}};
    const auto bytes = QJsonDocument(object).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) { error_ = file.errorString(); return false; }
    error_.clear(); return true;
}
}
