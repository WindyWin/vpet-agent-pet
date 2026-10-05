#pragma once
#include "hosts/focus_service.h"
#include <memory>

namespace pet::platform {
// This build's native services, assembled once by the application. Register a new
// desktop backend or host activation in the platform's implementation of these.
std::unique_ptr<hosts::FocusService> createFocusService();
}
