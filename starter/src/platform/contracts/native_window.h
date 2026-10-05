#pragma once
#include <QPoint>
#include <QtGlobal>
#include <optional>

namespace pet::platform {
// Optional native queries for the pet's own window; nullopt where the platform has
// none, and callers keep their Qt fallbacks.
// Whether the left button is down. X11 native moves can consume the release before Qt sees it.
std::optional<bool> nativeLeftButtonDown();
// Where the display server shows a top-level window. Qt's cached position can be
// stale under reparenting window managers (seen with Openbox).
std::optional<QPoint> nativeWindowOrigin(quintptr window);
}
