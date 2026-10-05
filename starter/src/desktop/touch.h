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
