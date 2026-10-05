#pragma once
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <QVector>

namespace pet {
// Identifies the terminal or editor an agent runs in, so a click on its alert can
// bring it forward. Only identifiers are kept: no titles, commands or output.
//   host        konsole | herdr | tmux | vscode | terminal
//   host_pids   the hook's ancestor process IDs, nearest first (finds X11 windows)
//   host_window $WINDOWID when the terminal exports it
//   host_target konsole: "<D-Bus service>|<window path>|<session path>"
//               herdr:   "<tab id>|<pane id>|<socket path>"
//               tmux:    "<socket path>|<pane id>"
// Programs started from a terminal inherit its variables, so herdr, tmux and Konsole
// count only when one of the ancestor names (from processNames) is that program.
// Without names, as when /proc is unreadable, the variables are taken as they are.
QJsonObject hostContext(const QProcessEnvironment &environment, QVector<qint64> ancestors, const QStringList &names);
// Parent chain of a process from /proc, nearest first, excluding pid 1.
QVector<qint64> processAncestors(qint64 pid, int limit = 16);
// Kernel command names (/proc/<pid>/comm) of the processes, in order.
QStringList processNames(const QVector<qint64> &pids);

struct HostCommand {
    QString program;
    QStringList arguments;
    QMap<QString, QString> environment;
};
// External commands that select the session's tab or pane before the window is
// raised. Fields are validated; nothing passes through a shell.
QVector<HostCommand> hostCommands(const QString &host, const QString &target);
// API socket used by a local Herdr UI invocation; empty for commands/remotes.
QString herdrClientSocket(const QProcessEnvironment &environment, const QStringList &arguments);
struct KonsoleTarget { QString service, window; int session = -1; };
bool konsoleTarget(const QString &target, KonsoleTarget &out);

struct HostWindow { quint64 id = 0; qint64 pid = 0; QString title; };
// The window to raise: $WINDOWID when listed, else the windows of the nearest
// ancestor process, preferring the one whose title names the project (VS Code
// runs all of its windows from one process). Returns 0 when nothing matches.
quint64 chooseWindow(const QString &hostWindow, const QString &hostPids, const QString &project,
                     const QVector<HostWindow> &windows);
QString hostName(const QString &host);
}
