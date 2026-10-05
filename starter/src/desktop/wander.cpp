#include "wander.h"
#include <array>
#include <utility>

namespace pet::wander {
Sides distances(const QRect &window, const QRect &area, int scale) {
    const double units = window.width() > 0 && scale > 0 ? double(scale) / window.width() : 0;
    return {(window.left() - area.left()) * units, (window.top() - area.top()) * units,
            (area.right() - window.right()) * units, (area.bottom() - window.bottom()) * units};
}
static std::array<std::pair<double, double>, 4> pairs(const Sides &condition, const Sides &distances) {
    return {{{condition.left, distances.left}, {condition.top, distances.top},
             {condition.right, distances.right}, {condition.bottom, distances.bottom}}};
}
bool fits(const Move &move, const Sides &distances) {
    for (const auto &[room, distance] : pairs(move.room, distances)) if (room >= 0 && distance < room) return false;
    for (const auto &[near, distance] : pairs(move.near, distances)) if (near >= 0 && distance >= near) return false;
    return true;
}
bool keeps(const Move &move, const Sides &distances) {
    for (const auto &[keep, distance] : pairs(move.keep, distances)) if (keep >= 0 && distance <= keep) return false;
    return true;
}
QPointF step(const Move &move, int side, int scale, qint64 ms) {
    if (scale <= 0) return {};
    ms = qBound(qint64(0), ms, qint64(100));
    return move.speed * (double(side) / scale) * (ms / 1000.0);
}
}
