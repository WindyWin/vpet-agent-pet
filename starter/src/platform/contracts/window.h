#pragma once
#include <QString>

namespace pet::platform {
// A native window owned by one desktop backend, such as {"x11", "<decimal id>"}.
// Shared code stores and forwards it; only the named backend interprets the ID.
struct WindowRef {
    QString backend, id;
    bool isNull() const { return backend.isEmpty() || id.isEmpty(); }
    bool operator==(const WindowRef &other) const { return backend == other.backend && id == other.id; }
};
constexpr auto x11Backend = "x11";
}
