#pragma once
#include "providers/host.h"
#include "sessions/state.h"

namespace pet {
// Brings an agent's terminal or editor forward: selects its Konsole tab, tmux pane
// or herdr pane, then activates its X11 window. Native Wayland cannot raise other
// applications' windows, so there only the tab/pane selection happens.
namespace hostFocus {
// False when nothing could be selected or raised for this session.
bool focus(const Session &session);
// True when the session's window is the active X11 window (the user is already there).
bool active(const Session &session);
QVector<HostWindow> windows();
}
}
