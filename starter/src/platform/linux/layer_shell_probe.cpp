#include "layer_shell_probe.h"
#include <wayland-client.h>
#include <cstring>

namespace pet::platform {
namespace {
void announced(void *data, wl_registry *, uint32_t, const char *interface, uint32_t) {
    if (std::strcmp(interface, "zwlr_layer_shell_v1") == 0) *static_cast<bool *>(data) = true;
}
void withdrawn(void *, wl_registry *, uint32_t) {}
const wl_registry_listener listener = {announced, withdrawn};
}
bool compositorHasLayerShell() {
    wl_display *display = wl_display_connect(nullptr); // WAYLAND_DISPLAY, as Qt will use it.
    if (!display) return false;
    bool found = false;
    if (wl_registry *registry = wl_display_get_registry(display)) {
        wl_registry_add_listener(registry, &listener, &found);
        wl_display_roundtrip(display); // The globals arrive in the first round trip.
        wl_registry_destroy(registry);
    }
    wl_display_disconnect(display);
    return found;
}
}
