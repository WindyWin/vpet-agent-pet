# 0008. Window behavior and recovery

- Status: Accepted
- Date: 2026-10-04

## Context

The pet must stay above other windows without getting in the way, and every
interaction needs a recovery path where compositors differ: no tray, mouse releases
consumed by a system move, refused positioning or raising.

## Decision

The pet requests a borderless, translucent, always-on-top tool window. Left drag
uses `QWindow::startSystemMove`, with coordinate movement as a fallback. A press that
leaves the pet in place is a click under 500 ms and a touch from then on (see
[touch reactions](0013-touch-reactions.md)); a throw and hiding at an edge move the window
from the application, which native Wayland may refuse. Right-click
opens controls. Walking and climbing move the window from the application too (see
[walking](0016-walking.md)) and are off under native Wayland. Space switches preview state, Menu opens controls and Escape quits
while the pet has focus. These are local shortcuts, not global desktop bindings.

The pet and tray share one menu. Its top level keeps the everyday actions (Show pet,
running sessions, today's recap, mute, always on top, Settings, Quit) plus an update
entry only while an update is waiting; previews, click-through, recovery, Updates… and
About sit under More. Settings is three tabs (General with alerts and reminders, Pet
with idle behavior, Startup and agents), with the status line, Updates, Quit and Close
below them, so the dialog fits small screens.

Click-through remains a 15-second lease. A single-shot
timer restores input whether or not a tray exists. The tray menu (under More) can immediately
recover input and position, or quit. The context menu also quits. Closing the pet
quits; closing About does not. Position recovery moves to the primary screen's lower right. Saved positions
are clamped to an available monitor on startup and layout changes.

When DISPLAY is set, the default backend is `xcb`, including under Wayland through
XWayland. Set `QT_QPA_PLATFORM=wayland` explicitly for native Wayland experiments
using a development Qt installation. The M2 package includes xcb and offscreen
plugins only; it does not claim native Wayland support.

Qt documents compositor requirements for [translucent windows](https://doc.qt.io/qt-6/qwidget.html#creating-translucent-windows).
Native Wayland cannot be assumed to honor application positioning, raising, or
always-on-top hints. Native move requests depend on a real input event and the
compositor. See [QWindow movement](https://doc.qt.io/qt-6/qwindow.html#startSystemMove)
and [window flags](https://doc.qt.io/qt-6/qt.html#WindowType-enum).

## Consequences

- Click-through cannot trap the user: it always expires.
- Features that move the window from the application (throws, edge hiding, walking)
  may not work under native Wayland and are disabled or bounded there.
- Desktop behavior is accepted by hand with the checklist below.

## Validation

### Manual acceptance checklist

Run on a composited X11 session and separately on KDE/GNOME Wayland with XWayland:

1. Launch the copied package from a directory with spaces; check clear space around
   the sprite against light and dark desktop backgrounds.
2. Drag across the screen and across monitors. Switch Idle / Thinking and all sizes.
3. Toggle Always on top; verify stacking against another application.
4. Enable click-through over another application; verify that application receives
   clicks. Wait 15 seconds and verify the pet receives input again.
5. Try immediate recovery and Quit from the tray, if available. Also verify recovery
   with a desktop lacking a tray. Right-click Quit and focused Escape must terminate.
6. Close About and verify animation continues. Record desktop/version and results.

Native Wayland tests are exploratory and must be recorded separately. No hooks,
integration registrations or user settings are changed by the prototype.
