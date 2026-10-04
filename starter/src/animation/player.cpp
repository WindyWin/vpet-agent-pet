#include "player.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

namespace pet {
Player::Player(QObject *parent) : QObject(parent) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] {
        index_ = (index_ + 1) % frames_.size();
        display();
    });
    select("idle");
}
void Player::select(const QString &state) {
    if (state != "idle" && state != "thinking")
        throw std::invalid_argument("M1 supports idle and thinking only");
    QFile catalog(":/assets/vpet/animations.json");
    if (!catalog.open(QIODevice::ReadOnly)) throw std::runtime_error("Missing animation catalog");
    const auto root = QJsonDocument::fromJson(catalog.readAll()).object();
    const auto paths = root["states"].toObject()[state].toArray();
    const QString sequence = paths.at(state == "thinking" ? 1 : 0).toString();
    QVector<Frame> selected;
    for (const auto &entry : root["sequences"].toArray()) {
        const auto object = entry.toObject();
        if (object["path"].toString() != sequence) continue;
        for (const auto &value : object["frames"].toArray()) {
            const auto frame = value.toObject();
            const QString path = ":/" + frame["path"].toString();
            const int duration = frame["duration_ms"].toInt();
            if (duration <= 0 || !QFile::exists(path))
                throw std::runtime_error("Invalid bundled animation frame");
            selected.append({path, duration});
        }
    }
    if (selected.isEmpty()) throw std::runtime_error("Empty animation sequence");
    timer_.stop();
    frames_ = selected;
    state_ = state;
    index_ = 0;
    display();
}
void Player::display() {
    QPixmap next(frames_.at(index_).path);
    if (next.isNull()) throw std::runtime_error("Cannot decode bundled frame");
    pixmap_ = next;
    timer_.start(frames_.at(index_).durationMs);
    emit changed();
}
}
