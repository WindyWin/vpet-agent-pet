#include "touch.h"
#include <QtMath>

namespace pet::touch {
QPointF velocity(const QVector<Sample> &samples, qint64 spanMs) {
    if (samples.size() < 2) return {};
    const auto &last = samples.last();
    // The oldest sample still within the span, so a pause just before letting go reads as still.
    int first = samples.size() - 2;
    while (first > 0 && last.ms - samples[first - 1].ms <= spanMs) --first;
    const auto elapsed = last.ms - samples[first].ms;
    if (elapsed <= 0 || elapsed > spanMs) return {};
    return QPointF(last.position - samples[first].position) * (1000.0 / elapsed);
}
QRect areaFor(const QRect &window, const QVector<QRect> &areas) {
    if (areas.isEmpty()) return window;
    QRect best = areas.first();
    qint64 largest = 0;
    for (const auto &area : areas) {
        const auto overlap = area.intersected(window);
        const qint64 size = qint64(overlap.width()) * overlap.height();
        if (size > largest) { largest = size; best = area; }
    }
    return best;
}
static bool outer(const QPoint &beyond, const QVector<QRect> &areas) {
    for (const auto &area : areas) if (area.contains(beyond)) return false;
    return true;
}
Edge pushedEdge(const QRect &window, const QVector<QRect> &areas) {
    if (areas.isEmpty()) return Edge::None;
    const auto area = areaFor(window, areas);
    const int margin = qMax(1, window.width() / 8), middle = window.center().y();
    if (window.left() <= area.left() - margin && outer({area.left() - 1, middle}, areas)) return Edge::Left;
    if (window.right() >= area.right() + margin && outer({area.right() + 1, middle}, areas)) return Edge::Right;
    return Edge::None;
}
QPoint hidePosition(Edge edge, const QRect &window, const QVector<QRect> &areas, int at, int scale) {
    const auto area = areaFor(window, areas);
    const int cut = scale > 0 ? qRound(double(at) * window.width() / scale) : window.width() / 2;
    const int x = edge == Edge::Left ? area.left() - cut : edge == Edge::Right ? area.right() + 1 - cut : window.x();
    const int y = qBound(area.top(), window.y(), qMax(area.top(), area.bottom() - window.height() + 1));
    return {x, y};
}
Flight::Flight(QPoint start, QPointF velocity, const QRect &area, QSize size)
    : position_(start), area_(area), size_(size) {
    const auto speed = qSqrt(QPointF::dotProduct(velocity, velocity));
    velocity_ = speed > maxSpeed ? velocity * (maxSpeed / speed) : velocity;
}
bool Flight::step(qint64 ms) {
    if (landed_) return false;
    ms = qBound(qint64(0), ms, qint64(100)); // A stalled event loop must not teleport the pet.
    elapsed_ += ms;
    const double seconds = ms / 1000.0;
    velocity_.ry() += gravity * seconds;
    position_ += velocity_ * seconds;
    const double left = area_.left(), right = area_.right() + 1 - size_.width();
    const double top = area_.top(), bottom = area_.bottom() + 1 - size_.height();
    if (position_.x() < left || position_.x() > right) {
        position_.rx() = qBound(left, position_.x(), qMax(left, right));
        velocity_.rx() *= -0.5;
    }
    if (position_.y() < top) { position_.ry() = top; velocity_.ry() = 0; }
    if (position_.y() >= bottom || elapsed_ >= maxMs) {
        position_.ry() = qMin(position_.y(), qMax(top, bottom));
        landed_ = true;
    }
    return !landed_;
}
}
