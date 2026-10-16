#pragma once
#include <QPoint>
#include <QtGlobal>
#include <optional>

class QScreen;
class QWindow;

namespace pet::platform {
// Optional native queries for the pet's own window; nullopt where the platform has
// none, and callers keep their Qt fallbacks.
// Whether the left button is down. X11 native moves can consume the release before Qt sees it.
std::optional<bool> nativeLeftButtonDown();
// Where the display server shows a top-level window. Qt's cached position can be
// stale under reparenting window managers (seen with Openbox).
std::optional<QPoint> nativeWindowOrigin(quintptr window);
// The scale XWayland currently applies to X11 clients (Xft.dpi / 96 on the root window); nullopt
// off XWayland. Qt reads it once at start, so the compositor changing it later (a screen with a
// higher scale appears) leaves Qt's pixel ratio stale and every window shrinks or grows.
std::optional<qreal> nativeDisplayScale();
// Native Wayland offers no way to place a window, so the pet is a wlr-layer-shell surface anchored to
// its screen's top left and moved by changing its margins (docs/adr/0043-native-wayland-layer-shell.md).
// Whether the running platform is that; every other call is meaningful only when it is.
bool layerShellActive();
// Make the window's surface a layer surface (before it is shown, and again after a flag change recreates it).
// aboveWindows picks the top layer; off, the bottom one, behind every window. takesKeyboard lets a click give it
// keyboard focus.
void layerAttach(QWindow *window, bool aboveWindows, bool takesKeyboard);
// Bind the surface to a screen; takes effect when the window is next shown.
void layerSetScreen(QWindow *window, QScreen *screen);
// Where the surface sits, in pixels from its screen's top left. May be negative or past the edge.
void layerPlace(QWindow *window, QPoint offset);
}
