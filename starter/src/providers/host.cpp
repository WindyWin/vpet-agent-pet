#include "host.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

namespace pet {
static bool matches(const char *pattern, const QString &value) {
    return QRegularExpression(QRegularExpression::anchoredPattern(pattern)).match(value).hasMatch();
}
QVector<qint64> processAncestors(qint64 pid, int limit) {
    QVector<qint64> chain;
    while (pid > 1 && chain.size() < limit && !chain.contains(pid)) {
        chain.append(pid);
        QFile stat(QString("/proc/%1/stat").arg(pid));
        if (!stat.open(QIODevice::ReadOnly)) break;
        // "pid (comm) state ppid ..."; comm may contain spaces and parentheses.
        const auto line = QString::fromUtf8(stat.read(4096));
        const auto fields = line.mid(line.lastIndexOf(')') + 2).split(' ');
        pid = fields.value(1).toLongLong();
    }
    return chain;
}
QStringList processNames(const QVector<qint64> &pids) {
    QStringList names;
    for (const auto pid : pids) {
        QFile comm(QString("/proc/%1/comm").arg(pid));
        names << (comm.open(QIODevice::ReadOnly) ? QString::fromUtf8(comm.read(64)).trimmed() : QString());
    }
    return names;
}
QJsonObject hostContext(const QProcessEnvironment &env, QVector<qint64> ancestors, const QStringList &names) {
    QJsonObject out;
    const auto value = [&](const char *name) { return env.value(name); };
    // VS Code launched from a herdr pane passes HERDR_PANE_ID on to its own terminals, for example.
    const auto under = [&](const char *program) {
        return names.isEmpty() || std::any_of(names.begin(), names.end(), [&](const QString &name) {
            return name == program || name.startsWith(QString(program) + ":"); // tmux renames itself "tmux: server".
        });
    };
    QString host, target;
    if (!value("HERDR_PANE_ID").isEmpty() && under("herdr")) {
        host = "herdr";
        target = value("HERDR_TAB_ID") + "|" + value("HERDR_PANE_ID") + "|" + value("HERDR_SOCKET_PATH");
    } else if (!value("TMUX").isEmpty() && !value("TMUX_PANE").isEmpty() && under("tmux")) {
        host = "tmux";
        target = value("TMUX").section(',', 0, 0) + "|" + value("TMUX_PANE");
    } else if (!value("KONSOLE_DBUS_SERVICE").isEmpty() && under("konsole")) {
        host = "konsole";
        target = value("KONSOLE_DBUS_SERVICE") + "|" + value("KONSOLE_DBUS_WINDOW") + "|" + value("KONSOLE_DBUS_SESSION");
    } else if (value("TERM_PROGRAM") == "vscode") host = "vscode";
    else if (!ancestors.isEmpty()) host = "terminal";
    if (host.isEmpty()) return out;
    out["host"] = host;
    if (!target.isEmpty() && target.size() <= 256) out["host_target"] = target;
    QStringList pids;
    for (const auto pid : ancestors.mid(0, 16)) pids << QString::number(pid);
    if (!pids.isEmpty()) out["host_pids"] = pids.join(',');
    // VS Code sets no WINDOWID; one in its terminals belongs to the terminal that launched it.
    if (host != "vscode" && matches("[1-9][0-9]{0,19}", value("WINDOWID"))) out["host_window"] = value("WINDOWID");
    return out;
}
bool konsoleTarget(const QString &target, KonsoleTarget &out) {
    const auto parts = target.split('|');
    if (parts.size() != 3 || !matches(R"((:[0-9]+\.[0-9]+|[A-Za-z_][A-Za-z0-9_-]*(\.[A-Za-z_][A-Za-z0-9_-]*)+))", parts[0]) ||
        !matches("/Windows/[0-9]{1,6}", parts[1]) || !matches("/Sessions/[0-9]{1,6}", parts[2])) return false;
    out = {parts[0], parts[1], parts[2].section('/', -1).toInt()};
    return true;
}
QVector<HostCommand> hostCommands(const QString &host, const QString &target) {
    const auto parts = target.split('|');
    if (host == "tmux" && parts.size() == 2 && matches("%[0-9]{1,6}", parts[1]) &&
        (parts[0].isEmpty() || QDir::isAbsolutePath(parts[0]))) {
        QStringList socket;
        if (!parts[0].isEmpty()) socket << "-S" << parts[0];
        return {{"tmux", socket + QStringList{"select-window", "-t", parts[1]}, {}},
                {"tmux", socket + QStringList{"select-pane", "-t", parts[1]}, {}}};
    }
    if (host == "herdr" && parts.size() == 3 && matches("[A-Za-z0-9_:.-]{1,64}", parts[1]) &&
        (parts[0].isEmpty() || matches("[A-Za-z0-9_:.-]{1,64}", parts[0])) &&
        (parts[2].isEmpty() || QDir::isAbsolutePath(parts[2]))) {
        QMap<QString, QString> env;
        if (!parts[2].isEmpty()) env["HERDR_SOCKET_PATH"] = parts[2];
        QVector<HostCommand> commands;
        // Tab focus moves attached clients; agent focus then picks the pane and marks it seen.
        if (!parts[0].isEmpty()) commands.append({"herdr", {"tab", "focus", parts[0]}, env});
        commands.append({"herdr", {"agent", "focus", parts[1]}, env});
        return commands;
    }
    return {};
}
QString herdrClientSocket(const QProcessEnvironment &env, const QStringList &arguments) {
    QString session = env.value("HERDR_SESSION");
    bool explicitSession = false;
    if (arguments.size() == 3 && arguments[0] == "session" && arguments[1] == "attach") {
        session = arguments[2];
        explicitSession = true;
    } else {
        for (int i = 0; i < arguments.size(); ++i) {
            const auto &arg = arguments[i];
            if (arg == "--session" && i + 1 < arguments.size()) {
                session = arguments[++i]; explicitSession = true;
            } else if (arg.startsWith("--session=")) {
                session = arg.mid(10); explicitSession = true;
            } else if (arg != "--handoff" && arg != "--no-session") return {};
        }
    }
    if (!session.isEmpty() && (!matches("[A-Za-z0-9_.-]{1,64}", session) || session == "." || session == "..")) return {};
    if (!explicitSession && !env.value("HERDR_SOCKET_PATH").isEmpty())
        return QDir::cleanPath(env.value("HERDR_SOCKET_PATH"));
    // A legacy client-only override cannot safely identify an API session.
    if (!explicitSession && !env.value("HERDR_CLIENT_SOCKET_PATH").isEmpty()) return {};
    auto config = env.value("XDG_CONFIG_HOME");
    if (config.isEmpty()) {
        if (env.value("HOME").isEmpty()) return {};
        config = env.value("HOME") + "/.config";
    }
    return QDir::cleanPath(config + "/herdr/" +
        (session.isEmpty() || session == "default" ? QString() : "sessions/" + session + "/") + "herdr.sock");
}
quint64 chooseWindow(const QString &hostWindow, const QString &hostPids, const QString &project,
                     const QVector<HostWindow> &windows) {
    const auto wanted = hostWindow.toULongLong();
    for (const auto &w : windows) if (wanted && w.id == wanted) return w.id;
    const auto name = QFileInfo(QDir::cleanPath(project)).fileName();
    for (const auto &pid : hostPids.split(',', Qt::SkipEmptyParts)) {
        QVector<HostWindow> owned;
        for (const auto &w : windows) if (w.pid == pid.toLongLong()) owned.append(w);
        if (owned.isEmpty()) continue;
        if (!name.isEmpty())
            for (const auto &w : owned) if (w.title.contains(name, Qt::CaseInsensitive)) return w.id;
        return owned.first().id;
    }
    return 0;
}
QString hostName(const QString &host) {
    return host == "konsole" ? "Konsole" : host == "herdr" ? "herdr" : host == "tmux" ? "tmux"
         : host == "vscode" ? "VS Code" : host == "terminal" ? "Terminal" : QString();
}
}
