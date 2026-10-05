#include "connection.h"
#include <QGuiApplication>
#include <X11/Xlib.h>

namespace pet::platform::x11 {
Display *display() {
    if (QGuiApplication::platformName() != "xcb") return nullptr;
    struct Connection {
        Display *display = XOpenDisplay(nullptr);
        ~Connection() { if (display) XCloseDisplay(display); }
    };
    static Connection connection;
    return connection.display;
}
}
