#include "host_focus.h"
#include "drag_monitor.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace pet::hostFocus {
static Display *display() { return x11Display(); }
static QVector<unsigned long> property(Display *d, Window w, const char *name, Atom type) {
    Atom actual; int format; unsigned long count = 0, after; unsigned char *data = nullptr;
    QVector<unsigned long> values;
    if (XGetWindowProperty(d, w, XInternAtom(d, name, False), 0, 4096, False, type, &actual, &format, &count, &after, &data) == Success &&
        data && format == 32)
        for (unsigned long i = 0; i < count; ++i) values.append(reinterpret_cast<unsigned long *>(data)[i]);
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
QVector<HostWindow> windows() {
    QVector<HostWindow> result;
    auto *d = display();
    if (!d) return result;
    const IgnoreErrors guard(d);
    for (const auto id : property(d, DefaultRootWindow(d), "_NET_CLIENT_LIST", XA_WINDOW)) {
        const auto pid = property(d, id, "_NET_WM_PID", XA_CARDINAL);
        result.append({quint64(id), pid.isEmpty() ? 0 : qint64(pid.first()), title(d, id)});
    }
    return result;
}
static quint64 activeWindow() {
    auto *d = display();
    if (!d) return 0;
    const auto active = property(d, DefaultRootWindow(d), "_NET_ACTIVE_WINDOW", XA_WINDOW);
    return active.isEmpty() ? 0 : active.first();
}
static bool activate(quint64 id) {
    auto *d = display();
    if (!d || !id) return false;
    const IgnoreErrors guard(d);
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = Window(id);
    event.xclient.message_type = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 2; // Source: pager. Window managers honor it like a taskbar click.
    event.xclient.data.l[1] = CurrentTime;
    XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    return true;
}
static QString executable(const QString &program) {
    // Desktop launchers often lack user tool directories on PATH.
    auto path = QStandardPaths::findExecutable(program);
    if (path.isEmpty())
        path = QStandardPaths::findExecutable(program, {QDir::homePath() + "/.local/bin", QDir::homePath() + "/.cargo/bin",
                                                        "/usr/local/bin", "/opt/homebrew/bin"});
    return path;
}
static bool run(const HostCommand &command, QByteArray *output = nullptr) {
    const auto program = executable(command.program);
    if (program.isEmpty()) return false;
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    for (auto it = command.environment.begin(); it != command.environment.end(); ++it) env.insert(it.key(), it.value());
    process.setProcessEnvironment(env);
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(program, command.arguments);
    // A click waits at most this long for a hung multiplexer.
    if (!process.waitForFinished(1500)) { process.kill(); process.waitForFinished(200); return false; }
    if (output) *output = process.readAllStandardOutput();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
static QString searchPids(const Session &s) {
    QStringList pids;
    if (s.host == "tmux") {
        // The tmux server is detached from any terminal; its clients' parents are.
        auto commands = hostCommands(s.host, s.hostTarget);
        if (!commands.isEmpty()) {
            auto list = commands.first();
            list.arguments = list.arguments.mid(0, list.arguments.indexOf("select-window"));
            list.arguments << "list-clients" << "-t" << s.hostTarget.section('|', 1) << "-F" << "#{client_pid}";
            QByteArray output;
            if (run(list, &output))
                for (const auto &line : output.split('\n'))
                    for (const auto pid : processAncestors(line.trimmed().toLongLong())) pids << QString::number(pid);
        }
    }
    if (!s.hostPids.isEmpty()) pids << s.hostPids;
    return pids.join(',');
}
bool focus(const Session &s) {
    bool selected = false;
    KonsoleTarget konsole;
    if (s.host == "konsole" && konsoleTarget(s.hostTarget, konsole)) {
        QDBusInterface window(konsole.service, konsole.window, "org.kde.konsole.Window", QDBusConnection::sessionBus());
        window.setTimeout(1000);
        selected = window.isValid() && window.call("setCurrentSession", konsole.session).type() != QDBusMessage::ErrorMessage;
    }
    for (const auto &command : hostCommands(s.host, s.hostTarget)) selected = run(command) || selected;
    const auto window = chooseWindow(s.hostWindow, searchPids(s), s.project, windows());
    return activate(window) || selected;
}
bool active(const Session &s) {
    const auto current = activeWindow();
    return current && s.host != "tmux" && s.host != "herdr" && chooseWindow(s.hostWindow, s.hostPids, s.project, windows()) == current;
}
}
