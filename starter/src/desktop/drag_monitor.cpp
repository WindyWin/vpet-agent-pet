#include "drag_monitor.h"
#include <QGuiApplication>
#include <X11/Xlib.h>
namespace pet {
std::optional<bool> nativeLeftButtonDown() {
    if (QGuiApplication::platformName() != "xcb") return std::nullopt;
    struct Connection {
        Display *display = XOpenDisplay(nullptr);
        ~Connection() { if (display) XCloseDisplay(display); }
    };
    static Connection connection;
    if (!connection.display) return std::nullopt;
    Window root, child;
    int rootX, rootY, localX, localY;
    unsigned int mask;
    if (!XQueryPointer(connection.display, DefaultRootWindow(connection.display),
                       &root, &child, &rootX, &rootY, &localX, &localY, &mask)) return std::nullopt;
    return (mask & Button1Mask) != 0;
}
}
