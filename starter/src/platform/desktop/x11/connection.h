#pragma once
typedef struct _XDisplay Display;

namespace pet::platform::x11 {
// The app's private Xlib connection, or null off X11. Qt's own connection is untouched.
Display *display();
}
