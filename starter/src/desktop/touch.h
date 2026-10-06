#pragma once
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QVector>

namespace pet::touch {
// A press held this long without moving the pet pets it; a shorter one is a click.
constexpr int holdMs = 500;
// Let go while moving at least this fast (pixels per second) and the pet is thrown and falls.
constexpr double throwSpeed = 900;
// A short burst of throws, or one uninterrupted long pet, makes the pet leave.
class Patience {
public:
    static constexpr int throwLimit = 5;
    static constexpr qint64 throwWindowMs = 30000, petLimitMs = 8000, dragLimitMs = 15000;
    bool thrown(qint64 now) {
        while (!throws_.isEmpty() && now - throws_.first() >= throwWindowMs) throws_.removeFirst();
        throws_.append(now);
        return throws_.size() >= throwLimit;
    }
    bool petting(bool active, qint64 now) {
        if (!active) { petSince_ = -1; return false; }
        if (petSince_ < 0) petSince_ = now;
        return now - petSince_ >= petLimitMs;
    }
    bool dragging(bool active, qint64 now) {
        if (!active) { dragSince_ = -1; return false; }
        if (dragSince_ < 0) dragSince_ = now;
        return now - dragSince_ >= dragLimitMs;
    }
    void reset() { throws_.clear(); petSince_ = dragSince_ = -1; }
private:
    QVector<qint64> throws_;
    qint64 petSince_ = -1, dragSince_ = -1;
};
// Window positions polled during a drag, with milliseconds since the press.
struct Sample { qint64 ms; QPoint position; };
// How fast the window was moving when let go: over the last `spanMs` of samples (two or three polls),
// so a pet that was brought to rest before letting go reads as still. Zero without two samples.
QPointF velocity(const QVector<Sample> &samples, qint64 spanMs = 80);

enum class Edge { None, Left, Right };
// The screen area holding most of the window, or the first area when it overlaps none.
QRect areaFor(const QRect &window, const QVector<QRect> &areas);
// The side of its screen a window was pushed past, by a quarter of its width or more: the visible
// artwork, the middle half of the window, has then reached the edge. Only an outer
// edge counts: past the boundary between two side-by-side screens the pet is simply moving on.
Edge pushedEdge(const QRect &window, const QVector<QRect> &areas);
// Where the window sits while hiding at `edge`: the screen edge cuts the artwork at `at` of `scale`
// units, and the window stays within the area vertically.
QPoint hidePosition(Edge edge, const QRect &window, const QVector<QRect> &areas, int at, int scale);

// A thrown pet: it keeps its horizontal speed, bouncing softly off the sides of its screen, and falls
// until it lands on the bottom of the area. A flight that cannot land, because the window could not
// be moved, ends after `maxMs` all the same.
class Flight {
public:
    static constexpr double gravity = 2400, maxSpeed = 3000; // pixels per second (squared)
    static constexpr qint64 maxMs = 2500;
    Flight(QPoint start, QPointF velocity, const QRect &area, QSize size);
    bool step(qint64 ms); // Advances; false once it has landed.
    QPoint position() const { return position_.toPoint(); }
    bool landed() const { return landed_; }
private:
    QPointF position_, velocity_;
    QRect area_;
    QSize size_;
    qint64 elapsed_ = 0;
    bool landed_ = false;
};
}
