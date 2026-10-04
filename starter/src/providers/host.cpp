#include "host.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

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
QJsonObject hostContext(const QProcessEnvironment &env, QVector<qint64> ancestors) {
    QJsonObject out;
    const auto value = [&](const char *name) { return env.value(name); };
    QString host, target;
    if (!value("HERDR_PANE_ID").isEmpty()) {
        host = "herdr";
        target = value("HERDR_TAB_ID") + "|" + value("HERDR_PANE_ID") + "|" + value("HERDR_SOCKET_PATH");
    } else if (!value("TMUX").isEmpty() && !value("TMUX_PANE").isEmpty()) {
        host = "tmux";
        target = value("TMUX").section(',', 0, 0) + "|" + value("TMUX_PANE");
    } else if (!value("KONSOLE_DBUS_SERVICE").isEmpty()) {
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
    if (matches("[1-9][0-9]{0,19}", value("WINDOWID"))) out["host_window"] = value("WINDOWID");
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
