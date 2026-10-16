#include "connection.h"
#include "platform/contracts/native_window.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <QByteArray>
#include <QList>
#include <QtGlobal>

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
std::optional<qreal> nativeDisplayScale() {
    // Only XWayland rescales X11 clients under them; on a plain X session Xft.dpi is the user's font choice.
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) return std::nullopt;
    auto *d = x11::display();
    if (!d) return std::nullopt;
    // Read the property itself: XResourceManagerString() is a copy taken when the connection opened.
    Atom type; int format; unsigned long count = 0, after = 0; unsigned char *data = nullptr;
    auto *previous = XSetErrorHandler([](Display *, XErrorEvent *) { return 0; });
    const int status = XGetWindowProperty(d, DefaultRootWindow(d), XA_RESOURCE_MANAGER, 0, 65536, False, XA_STRING,
                                          &type, &format, &count, &after, &data);
    XSync(d, False); XSetErrorHandler(previous);
    std::optional<qreal> scale;
    if (status == Success && data) {
        for (const auto &line : QByteArray(reinterpret_cast<const char *>(data), int(count)).split('\n')) {
            if (!line.startsWith("Xft.dpi:")) continue;
            bool ok = false;
            const double dpi = line.mid(8).trimmed().toDouble(&ok);
            if (ok && dpi >= 48 && dpi <= 960) scale = dpi / 96.0;
        }
    }
    if (data) XFree(data);
    return scale;
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
