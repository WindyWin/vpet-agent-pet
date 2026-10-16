# 0042 — Keep the pet's size when XWayland's scale changes

Status: Accepted.

## Context

On KDE Wayland the pet runs as an X11 client (`xcb` over XWayland). KWin gives
XWayland the highest scale of any output and publishes it as `Xft.dpi` on the root
window. Qt reads that once at start and never again. Adding a screen with a higher
scale (an Android tablet used as a monitor through `tablet-screen`, scale 1.75
against a 1x laptop panel) raised XWayland's scale while Qt's pixel ratio stayed
at 1, so the 160 px pet was drawn at 160 / 1.75 = 91 px until the screen went away.

## Decision

`platform::nativeDisplayScale()` reads `Xft.dpi / 96` from the root window's
`RESOURCE_MANAGER` property (XWayland sessions only; nullopt elsewhere).
`PetWindow` records the value at start, which is what Qt's ratio assumes, and on any
screen change (300 ms and 1.5 s later, the property lags RandR) resizes the window
by live / recorded. The size the user chose (`petSize()`, saved in preferences, shown
in Settings) is unchanged; only the window and its render size follow.

## Consequences

The pet keeps its apparent size when screens with other scales come and go. Menus,
bubbles and dialogs still use Qt's stale ratio and change size with the scale; a
full fix needs a restart or native Wayland. Plain X11, macOS and Windows are
unaffected.

## Validation

2026-10-16, Linux, KDE Wayland, Qt 6.11.2: a probe window and the pet kept 240
screen px while the tablet output's scale went 1.75 to 1 and back (X11 window size
420 to 240 to 420 px). All 17 CTest suites passed.
