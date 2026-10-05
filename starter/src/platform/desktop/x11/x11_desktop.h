#pragma once
#include "platform/contracts/desktop.h"
#include "platform/desktop/window_match.h"

namespace pet::platform::x11 {
// EWMH window activation and observation over the app's private Xlib connection.
// Unsupported off X11/XWayland. Activation is Requested: the window manager decides.
class X11Desktop : public DesktopBackend {
public:
    QString id() const override { return x11Backend; }
    Outcome activate(const WindowRequest &request) override;
    ActiveState active(const WindowRequest &request) override;
    // Managed top-level windows with their _NET_WM_PID and title.
    static QVector<WindowInfo> windows();
};
}
