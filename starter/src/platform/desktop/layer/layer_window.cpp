#include "platform/contracts/native_window.h"
#include <QGuiApplication>
#include <QWindow>
#ifdef AGENT_PET_HAVE_WAYLAND
#include <LayerShellQt/Window>
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
#else
bool layerShellActive() { return false; }
void layerAttach(QWindow *, bool, bool) {}
void layerSetScreen(QWindow *, QScreen *) {}
void layerPlace(QWindow *, QPoint) {}
#endif
}
