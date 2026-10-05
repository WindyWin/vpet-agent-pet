#pragma once
#include "outcome.h"
#include "window.h"
#include <QVector>

namespace pet::platform {
// The window to act on: a backend-owned reference when one is known, process hints
// (nearest first) and the project, whose name picks among one process's windows.
struct WindowRequest {
    WindowRef window;
    QVector<qint64> pids;
    QString project;
};
// Raises and observes native windows for one desktop protocol, such as X11 or KWin.
class DesktopBackend {
public:
    virtual ~DesktopBackend() = default;
    virtual QString id() const = 0;
    // What the user needs when this backend is Unsupported, such as "Wayland focus requires KDE Plasma 6."
    virtual QString requirement() const { return {}; }
    // Unsupported when this backend cannot act in the current session.
    virtual Outcome activate(const WindowRequest &request) = 0;
    // Whether the requested window is the active one.
    virtual ActiveState active(const WindowRequest &) { return ActiveState::Unknown; }
};
}
