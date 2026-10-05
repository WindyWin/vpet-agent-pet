#include "connection.h"
#include "platform/contracts/native_window.h"
#include <X11/Xlib.h>

namespace pet::platform {
std::optional<QPoint> nativeWindowOrigin(quintptr window) {
    auto *d = x11::display();
    if (!d || !window) return std::nullopt;
    auto *previous = XSetErrorHandler([](Display *, XErrorEvent *) { return 0; });
    int x = 0, y = 0; Window child;
    const bool ok = XTranslateCoordinates(d, Window(window), DefaultRootWindow(d), 0, 0, &x, &y, &child);
    XSync(d, False); XSetErrorHandler(previous);
    return ok ? std::optional<QPoint>(QPoint(x, y)) : std::nullopt;
}
std::optional<bool> nativeLeftButtonDown() {
    auto *d = x11::display();
    if (!d) return std::nullopt;
    Window root, child;
    int rootX, rootY, localX, localY;
    unsigned int mask;
    if (!XQueryPointer(d, DefaultRootWindow(d),
                       &root, &child, &rootX, &rootY, &localX, &localY, &mask)) return std::nullopt;
    return (mask & Button1Mask) != 0;
}
}
