#include "platform/headless.h"
#include "process.h"
#ifdef AGENT_PET_HAVE_WAYLAND
#include "layer_shell_probe.h"
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#endif
namespace pet::platform {
std::unique_ptr<ProcessServices> createProcessServices() { return std::make_unique<LinuxProcesses>(); }
bool desktopSessionAvailable(const QProcessEnvironment &environment) {
    return !environment.value("DISPLAY").isEmpty() || !environment.value("WAYLAND_DISPLAY").isEmpty();
}
#ifdef AGENT_PET_HAVE_WAYLAND
// Native Wayland leaves window placement to the compositor, so the pet places itself on a layer-shell
// surface (docs/adr/0043-native-wayland-layer-shell.md). That needs the compositor's support and Qt's Wayland plugin.
static bool nativeWaylandUsable() {
    if (qEnvironmentVariable("AGENT_PET_PLATFORM").toLower() == "xcb") return false;
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) return false;
    // A packaged build (bin/qt.conf points at ../plugins) must find the plugins it ships, not the system's.
    const QDir bin = QFileInfo(QFileInfo("/proc/self/exe").symLinkTarget()).dir();
    const bool bundled = bin.exists("qt.conf");
    const QDir plugins(bundled ? bin.filePath("../plugins") : QLibraryInfo::path(QLibraryInfo::PluginsPath));
    // Qt 6.10 ships one plugin; earlier 6.x ship the generic and EGL ones. Each answers to QT_QPA_PLATFORM=wayland.
    const bool plugin = plugins.exists("platforms/libqwayland.so") || plugins.exists("platforms/libqwayland-generic.so")
                        || plugins.exists("platforms/libqwayland-egl.so");
    if (!plugin) return false;
    return compositorHasLayerShell();
}
#endif
void bootstrapGui() {
    // An explicit Qt platform wins.
    if (!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) return;
#ifdef AGENT_PET_HAVE_WAYLAND
    if (nativeWaylandUsable()) {
        qputenv("QT_QPA_PLATFORM", "wayland");
        // Only the pet's own windows become layer surfaces; menus and dialogs stay ordinary xdg windows.
        qputenv("AGENT_PET_LAYER_SHELL", "1");
        return;
    }
#endif
    // Otherwise keep the XWayland default.
    if (!qEnvironmentVariableIsEmpty("DISPLAY")) qputenv("QT_QPA_PLATFORM", "xcb");
}
}
