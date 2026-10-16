#pragma once
namespace pet::platform {
// Whether the Wayland compositor in WAYLAND_DISPLAY offers wlr-layer-shell (KDE, Sway, Hyprland; not GNOME).
// A throwaway connection that reads the registry once; false when there is no compositor to ask.
bool compositorHasLayerShell();
}
