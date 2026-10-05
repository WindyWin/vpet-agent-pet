#pragma once
#include "animation/player.h"
#include <QPointF>
#include <QRect>

namespace pet::wander {
// How far each side of the window is from the same side of `area`, in units of a square artwork
// `scale` units wide. A side past the edge of the area is negative.
Sides distances(const QRect &window, const QRect &area, int scale);
// Whether a move may start: enough room where it needs room, and close enough where it needs a wall.
bool fits(const Move &move, const Sides &distances);
// Whether a move in progress still has the room it keeps; once it does not, it ends.
bool keeps(const Move &move, const Sides &distances);
// How far a move carries a window `side` pixels wide in `ms`, in pixels. A stalled event loop
// moves it no further than 100 ms would.
QPointF step(const Move &move, int side, int scale, qint64 ms);
}
