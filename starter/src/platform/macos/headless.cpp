#include "platform/headless.h"
#include "process.h"
namespace pet::platform {
std::unique_ptr<ProcessServices> createProcessServices() { return std::make_unique<MacProcesses>(); }
bool desktopSessionAvailable(const QProcessEnvironment &environment) {
    // Every login session has the window server; a remote shell cannot show the pet.
    return environment.value("SSH_CONNECTION").isEmpty() && environment.value("SSH_TTY").isEmpty();
}
void bootstrapGui() {}
}
