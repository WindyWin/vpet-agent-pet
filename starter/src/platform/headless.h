#pragma once
#include "platform/contracts/process.h"
#include <memory>
namespace pet::platform {
std::unique_ptr<ProcessServices> createProcessServices();
// Called before GUI initialization, after display-free command dispatch.
void bootstrapGui();
bool desktopSessionAvailable(const QProcessEnvironment &environment);
}
