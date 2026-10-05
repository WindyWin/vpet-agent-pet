#pragma once
#include "platform/desktop/window_match.h"
#include "sessions/state.h"

namespace pet {
// Brings an agent's terminal or editor forward: selects its Konsole tab, tmux pane
// or herdr pane, then activates its X11 window or uses KDE Plasma 6's KWin API.
namespace hostFocus {
// False when the window could not be raised, even if its tab/pane was selected.
bool focus(const Session &session);
// True when the session's window is the active X11 window (the user is already there).
bool active(const Session &session);
QVector<platform::WindowInfo> windows();
}
}
