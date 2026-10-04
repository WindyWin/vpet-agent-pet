#pragma once
#include <optional>
namespace pet {
// X11 native moves can consume the release before Qt sees it.
std::optional<bool> nativeLeftButtonDown();
}
