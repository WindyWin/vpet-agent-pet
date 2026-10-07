#include "platform/headless.h"
#include "process.h"
namespace pet::platform {
std::unique_ptr<ProcessServices> createProcessServices() { return std::make_unique<WindowsProcesses>(); }
bool desktopSessionAvailable(const QProcessEnvironment &environment) {
    // Every interactive logon has a desktop; an OpenSSH session cannot show the pet.
    return environment.value("SSH_CONNECTION").isEmpty() && environment.value("SSH_CLIENT").isEmpty();
}
void bootstrapGui() {}
}
