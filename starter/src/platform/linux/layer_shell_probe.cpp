#include "layer_shell_probe.h"
#include <wayland-client.h>
#include <cstring>
#include <poll.h>

namespace pet::platform {
namespace {
void announced(void *data, wl_registry *, uint32_t, const char *interface, uint32_t) {
    if (std::strcmp(interface, "zwlr_layer_shell_v1") == 0) *static_cast<bool *>(data) = true;
}
void withdrawn(void *, wl_registry *, uint32_t) {}
const wl_registry_listener listener = {announced, withdrawn};
void synced(void *data, wl_callback *, uint32_t) { *static_cast<bool *>(data) = true; }
const wl_callback_listener syncListener = {synced};
constexpr int timeoutMs = 500;
// A round trip that gives up: a stuck compositor must not hold up the pet's start.
bool roundtrip(wl_display *display) {
    bool done = false;
    wl_callback *callback = wl_display_sync(display);
    if (!callback) return false;
    wl_callback_add_listener(callback, &syncListener, &done);
    while (!done) {
        while (wl_display_prepare_read(display) != 0)
            if (wl_display_dispatch_pending(display) < 0) { wl_callback_destroy(callback); return false; }
        if (wl_display_flush(display) < 0) { wl_display_cancel_read(display); wl_callback_destroy(callback); return false; }
        pollfd waiting{wl_display_get_fd(display), POLLIN, 0};
        const int ready = poll(&waiting, 1, timeoutMs);
        if (ready <= 0 || wl_display_read_events(display) < 0) {
            if (ready <= 0) wl_display_cancel_read(display);
            wl_callback_destroy(callback);
            return false;
        }
        if (wl_display_dispatch_pending(display) < 0) { wl_callback_destroy(callback); return false; }
    }
    wl_callback_destroy(callback);
    return true;
}
}
bool compositorHasLayerShell() {
    wl_display *display = wl_display_connect(nullptr); // WAYLAND_DISPLAY, as Qt will use it.
    if (!display) return false;
    bool found = false;
    if (wl_registry *registry = wl_display_get_registry(display)) {
        wl_registry_add_listener(registry, &listener, &found);
        roundtrip(display); // The globals arrive in the first round trip.
        wl_registry_destroy(registry);
    }
    wl_display_disconnect(display);
    return found;
}
}
