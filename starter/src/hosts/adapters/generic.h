#pragma once
#include "hosts/registry.h"

namespace pet::hosts {
// Hosts recognized without a target: their window is found from process hints.
namespace vscode { constexpr auto id = "vscode"; Capture capture(); }
// Any other terminal, recognized by the hook having ancestors at all; registered last.
namespace terminal { constexpr auto id = "terminal"; Capture capture(); }
}
