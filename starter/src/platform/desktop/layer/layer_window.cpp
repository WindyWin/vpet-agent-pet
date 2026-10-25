#include "platform/contracts/native_window.h"
#include <QGuiApplication>
#include <QWindow>
#include <QObject>
#ifdef AGENT_PET_HAVE_WAYLAND
#include <LayerShellQt/Window>
#include <QtGui/qguiapplication_platform.h>
#include <wayland-client.h>
#endif

namespace pet::platform {
#ifdef AGENT_PET_HAVE_WAYLAND
// bootstrapGui() chose layer-shell for this run (or the user set AGENT_PET_LAYER_SHELL=1 with QT_QPA_PLATFORM=wayland).
bool layerShellActive() {
    return QGuiApplication::platformName() == "wayland" && qEnvironmentVariableIntValue("AGENT_PET_LAYER_SHELL") == 1;
}
void layerAttach(QWindow *window, bool aboveWindows, bool takesKeyboard) {
    if (!window || !layerShellActive()) return;
    using Layer = LayerShellQt::Window;
    auto *layer = Layer::get(window);
    layer->setLayer(aboveWindows ? Layer::LayerTop : Layer::LayerBottom);
    layer->setAnchors(Layer::Anchors(Layer::AnchorTop | Layer::AnchorLeft));
    layer->setExclusiveZone(-1); // Never moved aside for panels, never reserving room.
    layer->setScope("agent-pet");
    // The pet's shortcuts (Esc, Space, Ctrl+,) work once it is clicked; typing elsewhere is never taken.
    layer->setKeyboardInteractivity(takesKeyboard ? Layer::KeyboardInteractivityOnDemand : Layer::KeyboardInteractivityNone);
    layer->setScreen(window->screen());
}
void layerSetScreen(QWindow *window, QScreen *screen) {
    if (!window || !screen || !layerShellActive()) return;
    window->setScreen(screen);
    LayerShellQt::Window::get(window)->setScreen(screen);
}
void layerPlace(QWindow *window, QPoint offset) {
    if (!window || !layerShellActive()) return;
    LayerShellQt::Window::get(window)->setMargins(QMargins(offset.x(), offset.y(), 0, 0));
}
void layerSync(QObject *context, std::function<void()> ready) {
#if QT_CONFIG(wayland)
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (layerShellActive() && native && native->display()) {
        // Owned by the caller's context, so closing a window cancels its outstanding callback.
        struct Sync : QObject {
            wl_callback *callback = nullptr;
            std::function<void()> ready;
            explicit Sync(QObject *parent) : QObject(parent) {}
            ~Sync() override { if (callback) wl_callback_destroy(callback); }
            static void done(void *data, wl_callback *, uint32_t) {
                auto *sync = static_cast<Sync *>(data);
                // Drain already queued input before allowing the next move.
                QMetaObject::invokeMethod(sync->parent(), std::move(sync->ready), Qt::QueuedConnection);
                delete sync;
            }
        };
        auto *sync = new Sync(context);
        sync->ready = std::move(ready);
        sync->callback = wl_display_sync(native->display());
        static const wl_callback_listener listener{Sync::done};
        wl_callback_add_listener(sync->callback, &listener, sync);
        wl_display_flush(native->display());
        return;
    }
#endif
    ready();
}
#else
bool layerShellActive() { return false; }
void layerAttach(QWindow *, bool, bool) {}
void layerSetScreen(QWindow *, QScreen *) {}
void layerPlace(QWindow *, QPoint) {}
void layerSync(QObject *, std::function<void()> ready) { ready(); }
#endif
}
