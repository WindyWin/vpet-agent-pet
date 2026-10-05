#include "x11_desktop.h"
#include "connection.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace pet::platform::x11 {
static QVector<unsigned long> property(Display *d, Window w, const char *name, Atom type, bool *present = nullptr) {
    Atom actual; int format; unsigned long count = 0, after; unsigned char *data = nullptr;
    QVector<unsigned long> values;
    if (XGetWindowProperty(d, w, XInternAtom(d, name, False), 0, 4096, False, type, &actual, &format, &count, &after, &data) == Success &&
        data && format == 32)
        for (unsigned long i = 0; i < count; ++i) values.append(reinterpret_cast<unsigned long *>(data)[i]);
    if (present) *present = actual != None;
    if (data) XFree(data);
    return values;
}
static QString title(Display *d, Window w) {
    Atom actual; int format; unsigned long count = 0, after; unsigned char *data = nullptr;
    QString text;
    if (XGetWindowProperty(d, w, XInternAtom(d, "_NET_WM_NAME", False), 0, 1024, False, XInternAtom(d, "UTF8_STRING", False),
                           &actual, &format, &count, &after, &data) == Success && data && format == 8)
        text = QString::fromUtf8(reinterpret_cast<const char *>(data), int(count));
    if (data) XFree(data);
    return text;
}
// Windows can close between listing and reading them; ignore those errors on this
// private connection instead of letting Xlib's default handler exit the app.
struct IgnoreErrors {
    Display *display;
    int (*previous)(Display *, XErrorEvent *);
    explicit IgnoreErrors(Display *d) : display(d), previous(XSetErrorHandler([](Display *, XErrorEvent *) { return 0; })) {}
    ~IgnoreErrors() { XSync(display, False); XSetErrorHandler(previous); }
};
QVector<WindowInfo> X11Desktop::windows() {
    QVector<WindowInfo> result;
    auto *d = display();
    if (!d) return result;
    const IgnoreErrors guard(d);
    for (const auto id : property(d, DefaultRootWindow(d), "_NET_CLIENT_LIST", XA_WINDOW)) {
        const auto pid = property(d, id, "_NET_WM_PID", XA_CARDINAL);
        result.append({QString::number(id), pid.isEmpty() ? 0 : qint64(pid.first()), title(d, id)});
    }
    return result;
}
static QString preferred(const WindowRequest &request) {
    return request.window.backend == x11Backend ? request.window.id : QString();
}
Outcome X11Desktop::activate(const WindowRequest &request) {
    auto *d = display();
    if (!d) return Outcome::Unsupported;
    if (preferred(request).isEmpty() && request.pids.isEmpty()) return Outcome::MissingTarget;
    const auto id = matchWindow(preferred(request), request.pids, request.project, windows()).toULongLong();
    if (!id) return Outcome::TargetNotFound;
    const IgnoreErrors guard(d);
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = Window(id);
    event.xclient.message_type = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 2; // Source: pager. Window managers honor it like a taskbar click.
    event.xclient.data.l[1] = CurrentTime;
    return XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &event) != 0
        ? Outcome::Requested : Outcome::Failed;
}
ActiveState X11Desktop::active(const WindowRequest &request) {
    auto *d = display();
    if (!d) return ActiveState::Unknown;
    bool supported = false;
    const auto current = property(d, DefaultRootWindow(d), "_NET_ACTIVE_WINDOW", XA_WINDOW, &supported);
    if (!supported) return ActiveState::Unknown; // The window manager does not publish it.
    if (current.isEmpty() || !current.first()) return ActiveState::Inactive;
    return matchWindow(preferred(request), request.pids, request.project, windows()) == QString::number(current.first())
        ? ActiveState::Active : ActiveState::Inactive;
}
}
#undef Bool
