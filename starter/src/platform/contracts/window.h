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
constexpr auto windowsBackend = "windows";
// The backend whose window IDs protocol v1 carries as `host_window`. A hook and its pet
// share one desktop: an HWND on Windows, an X11 window everywhere else.
#ifdef Q_OS_WIN
constexpr auto nativeWindowBackend = windowsBackend;
#else
constexpr auto nativeWindowBackend = x11Backend;
#endif
}
