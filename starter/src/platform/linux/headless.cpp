#include "platform/headless.h"
#include "process.h"
namespace pet::platform {
std::unique_ptr<ProcessServices> createProcessServices() { return std::make_unique<LinuxProcesses>(); }
bool desktopSessionAvailable(const QProcessEnvironment &environment) {
    return !environment.value("DISPLAY").isEmpty() || !environment.value("WAYLAND_DISPLAY").isEmpty();
}
void bootstrapGui() {
    // Preserve the Linux XWayland default; an explicit Qt platform wins.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
}
}
