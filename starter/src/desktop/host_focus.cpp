#include "host_focus.h"
#include "drag_monitor.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace pet::hostFocus {
class FocusResult : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.agentpet.FocusResult")
public:
    bool received = false, focused = false;
    QEventLoop loop;
public slots:
    void complete(bool success) { received = true; focused = success; loop.quit(); }
};
static bool activateKWin(const QString &pids, const QString &project) {
    // Plasma 6 supports native Wayland windows through its scripting API:
    // https://develop.kde.org/docs/plasma/kwin/api/
    // Load a temporary script per click and wait for its actual focus result.
    auto bus = QDBusConnection::sessionBus();
    QDBusInterface scripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", bus);
    scripting.setTimeout(1000);
    if (!scripting.isValid()) return false;
    QJsonArray ancestors;
    for (const auto &pid : pids.split(',')) if (pid.toLongLong() > 1) ancestors.append(pid.toDouble());
    if (ancestors.isEmpty()) return false;
    FocusResult result;
    const QString path = "/AgentPetFocus";
    if (!bus.registerObject(path, &result, QDBusConnection::ExportAllSlots)) return false;
    const auto json = [](const QJsonArray &value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); };
    QTemporaryFile file(QDir::tempPath() + "/agent-pet-focus-XXXXXX.js");
    bool success = false;
    if (file.open()) {
        QByteArray script = "const pids = " + json(ancestors) + ";\nconst params = " +
            json({QFileInfo(project).fileName(), bus.baseService(), path}) + R"JS(;
let chosen = null;
const windows = workspace.windowList();
for (const pid of pids) {
    const matches = windows.filter(w => w.pid === pid && w.normalWindow);
    if (!matches.length) continue;
    const named = matches.filter(w => params[0] && w.caption.toLowerCase().includes(params[0].toLowerCase()));
    if (matches.length === 1) chosen = matches[0];
    else if (named.length === 1) chosen = named[0];
    break;
}
if (chosen) {
    chosen.minimized = false;
    if (chosen.desktops.length) workspace.currentDesktop = chosen.desktops[0];
    workspace.activeWindow = chosen;
}
callDBus(params[1], params[2], "org.agentpet.FocusResult", "complete",
         chosen !== null && workspace.activeWindow === chosen);
)JS";
        if (file.write(script) == script.size() && file.flush()) {
            const auto name = QFileInfo(file.fileName()).fileName();
            QDBusReply<int> loaded = scripting.call("loadScript", file.fileName(), name);
            if (loaded.isValid() && loaded.value() >= 0) {
                QDBusInterface runner("org.kde.KWin", QString("/Scripting/Script%1").arg(loaded.value()), "org.kde.kwin.Script", bus);
                runner.setTimeout(1000);
                if (runner.call("run").type() != QDBusMessage::ErrorMessage && !result.received) {
                    QTimer::singleShot(1500, &result.loop, &QEventLoop::quit);
                    result.loop.exec(QEventLoop::ExcludeUserInputEvents);
                }
                success = result.received && result.focused;
                scripting.call("unloadScript", name);
            }
        }
    }
    bus.unregisterObject(path);
    return success;
}
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
    return XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &event) != 0;
}
static QString executable(const QString &program) {
    // Desktop launchers often lack user tool directories on PATH.
    auto path = QStandardPaths::findExecutable(program);
    if (path.isEmpty())
        path = QStandardPaths::findExecutable(program, {QDir::homePath() + "/.local/bin", QDir::homePath() + "/.cargo/bin",
                                                        QDir::homePath() + "/.linuxbrew/bin", "/home/linuxbrew/.linuxbrew/bin",
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
static bool selectKonsole(const QString &target) {
    KonsoleTarget konsole;
    if (konsoleTarget(target, konsole)) {
        QDBusInterface window(konsole.service, konsole.window, "org.kde.konsole.Window", QDBusConnection::sessionBus());
        window.setTimeout(1000);
        return window.isValid() && window.call("setCurrentSession", konsole.session).type() != QDBusMessage::ErrorMessage;
    }
    return false;
}
static bool focusHerdrClient(const Session &s, const QVector<HostWindow> &available) {
    const auto socket = s.hostTarget.section('|', 2, 2);
    if (socket.isEmpty()) return false;
    // Pane ancestors lead to the detached server. Find a live UI client for this
    // socket instead, using its current terminal metadata (also after reattach).
    for (const auto &entry : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto pid = entry.toLongLong();
        if (pid <= 1) continue;
        const auto base = "/proc/" + entry;
        if (QFileInfo(QFileInfo(base + "/exe").symLinkTarget()).fileName() != "herdr") continue;
        if (!QFileInfo(base + "/fd/0").symLinkTarget().startsWith("/dev/pts/")) continue;
        QFile cmdline(base + "/cmdline"), environ(base + "/environ");
        if (!cmdline.open(QIODevice::ReadOnly) || !environ.open(QIODevice::ReadOnly)) continue;
        auto args = QString::fromLocal8Bit(cmdline.readAll()).split(QChar(0), Qt::SkipEmptyParts);
        if (args.isEmpty()) continue;
        args.removeFirst();
        QProcessEnvironment env;
        for (const auto &pair : environ.readAll().split('\0')) {
            const auto equals = pair.indexOf('=');
            if (equals > 0) env.insert(QString::fromLocal8Bit(pair.left(equals)), QString::fromLocal8Bit(pair.mid(equals + 1)));
        }
        if (herdrClientSocket(env, args) != QDir::cleanPath(socket)) continue;
        const auto ancestors = processAncestors(pid);
        const auto context = hostContext(env, ancestors, processNames(ancestors));
        const auto window = chooseWindow(context["host_window"].toString(), context["host_pids"].toString(), s.project, available);
        selectKonsole(env.value("KONSOLE_DBUS_SERVICE") + "|" + env.value("KONSOLE_DBUS_WINDOW") + "|" + env.value("KONSOLE_DBUS_SESSION"));
        if (activate(window)) return true;
        if (activateKWin(context["host_pids"].toString(), s.project)) return true;
    }
    return false;
}
bool focus(const Session &source) {
    // KWin callbacks process events; retain a copy if new hook events update the session map.
    const Session s = source;
    if (s.host == "konsole") selectKonsole(s.hostTarget);
    for (const auto &command : hostCommands(s.host, s.hostTarget)) if (!run(command)) return false;
    const auto available = windows();
    if (s.host == "herdr" && focusHerdrClient(s, available)) return true;
    const auto window = chooseWindow(s.hostWindow, searchPids(s), s.project, available);
    return activate(window) || activateKWin(searchPids(s), s.project);
}
bool active(const Session &s) {
    const auto current = activeWindow();
    return current && s.host != "tmux" && s.host != "herdr" && chooseWindow(s.hostWindow, s.hostPids, s.project, windows()) == current;
}
}
#undef Bool
#include "host_focus.moc"
