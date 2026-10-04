#pragma once
#include <QPoint>
#include <optional>
typedef struct _XDisplay Display;
namespace pet {
// The app's private Xlib connection, or null off X11. Qt's own connection is untouched.
Display *x11Display();
// X11 native moves can consume the release before Qt sees it.
std::optional<bool> nativeLeftButtonDown();
// Where the display server shows a top-level window. Qt's cached position can be
// stale under reparenting window managers (seen with Openbox).
std::optional<QPoint> nativeWindowOrigin(unsigned long window);
}
