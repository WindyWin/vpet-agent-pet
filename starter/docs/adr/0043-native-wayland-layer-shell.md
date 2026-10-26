# 0043 — Native Wayland through layer-shell

Status: Accepted; drag across screens needs hands-on confirmation.

## Context

Native Wayland lets a client neither place its window nor stay above others, which the pet
needs for dragging, throwing, hiding at an edge and walking. So the pet ran as an X11 client
through XWayland ([0008](0008-window-behavior.md)), where the compositor rescales it
whenever another screen's scale changes ([0042](0042-xwayland-scale-follow.md)).

wlr-layer-shell (KDE Plasma, Sway, Hyprland; not GNOME) gives a surface that the client
anchors to a screen corner and moves by changing its margins, on a chosen layer, without
the compositor deciding where it goes.

## Decision

`bootstrapGui()` (Linux) chooses native Wayland when no `QT_QPA_PLATFORM` is set,
`AGENT_PET_PLATFORM` is not `xcb`, `WAYLAND_DISPLAY` is set, Qt's Wayland plugin is present
(in the bundle's own `plugins/` for a packaged build, so a package never loads the
system's) and a throwaway registry probe (`layer_shell_probe.cpp`, libwayland-client) finds
`zwlr_layer_shell_v1`. It then sets `AGENT_PET_LAYER_SHELL=1`. Everything else keeps `xcb`.
The whole path is compiled only when CMake finds LayerShellQt 6.6+ and libwayland-client
(`AGENT_PET_WAYLAND`, default ON).

Only the pet's own windows are layer surfaces: `PetWindow`, `AlertBubble`, `NoteBubble`.
LayerShellQt's `Window::get` switches that one window to layer-shell, so menus, the session
list and dialogs stay ordinary xdg windows (menus and popups attach to the pet's surface).
Setting `QT_WAYLAND_SHELL_INTEGRATION=layer-shell` instead makes every window a full-screen
layer surface and was rejected after trying it.

`desktop/overlay.{h,cpp}` is the one place that moves such a window: `place()` is
`QWidget::move` and, under layer-shell, also binds the surface to the screen under the
window's centre (recreating it when that changes) and sets the top-left margin relative to
that screen. Qt's own idea of the window's position is kept equal to the real one, which
popup placement and mouse coordinates depend on. `PetWindow::relocate()` replaces its
`move()` calls. The platform contract (`native_window.h`: `layerShellActive`, `layerAttach`,
`layerSetScreen`, `layerPlace`) hides LayerShellQt; other platforms stub it.

- "Always on top" maps to the top layer, off to the bottom one. Keyboard focus is
  on-demand for the pet (its shortcuts) and none for bubbles.
- No compositor move exists, so the pet drags itself (the existing fallback drag). Pointer
  events sent before the compositor applies a new margin carry coordinates for the old place
  and made the drag run away. After each move the pet ignores pointer motion until Qt
  has handled its update request and a Wayland `wl_display.sync` barrier acknowledges
  the preceding requests. The barrier is queued after paint/commit; an update request
  alone runs before Qt paints and does not acknowledge compositor placement. There is
  no elapsed-time bypass that could accept stale coordinates. An unchanged target
  sends no further move. Each placement has a generation, so an old acknowledgement
  cannot unlock a later placement or a recreated surface.
- A pet window made before the surface exists can end up without its alpha channel (drawn
  on black); `overlay::prepare` recreates it once when the format lacks alpha.
- Wandering, throws and edge hiding are enabled under layer-shell.

## Consequences

On KDE the pet is a native Wayland client with a correct scale, no XWayland resize bug and
its movement features. GNOME, and every build or package without LayerShellQt or the Wayland
plugin, are unchanged. Release packages still bundle only `libqxcb.so`; shipping the Wayland
plugin, LayerShellQt and libwayland needs its own packaging work.

Qt tracks a screen's scale change itself on native Wayland, so [0042](0042-xwayland-scale-follow.md)
does not apply. Layer surfaces are not listed by taskbars, so the pet is not in the window
list. Cross-screen drag recreates the surface when the window centre crosses; whether the
drag carries on seamlessly depends on the compositor.

## Validation

2026-10-22: replaced the update-request-only drag gate and its 50 ms bypass with an
asynchronous compositor barrier. The pending callback belongs to the pet window and
is cancelled on destruction. This follows the ordering guarantee of
[`wl_display.sync`](https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_display-request-sync).
Dragging still requires a hold longer than 50 ms and travel of at least 8 logical
pixels; petting still requires a 300 ms hold. Built the app and prototype tests with
Qt 6.11.2; `holdToPet`, `touchRegionsAndHolds`, `windowTouchReactions` and
`oneShotsAndDragResume` passed offscreen (6 results including setup/cleanup).
Restarted the local app for a manual check; native drag smoothness still needs the
user's confirmation.

2026-10-16, Arch Linux, KDE Plasma 6 Wayland, Qt 6.11.2, LayerShellQt 6.7.5: the pet started
natively, was transparent, 160 px, in the right place on the laptop output; its alert bubble
placed itself beside it; the right-click menu opened as a popup of the surface and closed
on an outside click. A synthetic (uinput) drag of 500 px followed the pointer exactly (grab
offset kept to the pixel). A drag across the two outputs ended with the pet on the other
one, but a stall of about 100 ms mid-drag was seen and not explained, so cross-screen drag
and throw/edge/walk on real hardware remain to be checked by hand. All 17 CTest suites pass
with layer-shell enabled; with `-DAGENT_PET_WAYLAND=OFF` the library targets build.
