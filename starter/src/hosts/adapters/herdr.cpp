#include "herdr.h"
#include "pattern.h"
#include <QDir>

namespace pet::hosts::herdr {
bool decode(const QString &target, Target &out) {
    const auto parts = target.split('|');
    if (parts.size() != 3 || !fullMatch("[A-Za-z0-9_:.-]{1,64}", parts[1]) ||
        (!parts[0].isEmpty() && !fullMatch("[A-Za-z0-9_:.-]{1,64}", parts[0])) ||
        (!parts[2].isEmpty() && !QDir::isAbsolutePath(parts[2]))) return false;
    out = {parts[0], parts[1], parts[2]};
    return true;
}
QVector<platform::Command> selectCommands(const Target &target) {
    QMap<QString, QString> env;
    if (!target.socket.isEmpty()) env["HERDR_SOCKET_PATH"] = target.socket;
    QVector<platform::Command> commands;
    if (!target.tab.isEmpty()) commands.append({"herdr", {"tab", "focus", target.tab}, env});
    commands.append({"herdr", {"agent", "focus", target.pane}, env});
    return commands;
}
QString clientSocket(const QProcessEnvironment &env, const QStringList &arguments) {
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
    if (!session.isEmpty() && (!fullMatch("[A-Za-z0-9_.-]{1,64}", session) || session == "." || session == "..")) return {};
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
Capture capture() {
    return {id, "herdr",
            [](const QProcessEnvironment &env, const QVector<qint64> &, QString &target) {
                if (env.value("HERDR_PANE_ID").isEmpty()) return false;
                target = env.value("HERDR_TAB_ID") + "|" + env.value("HERDR_PANE_ID") + "|" + env.value("HERDR_SOCKET_PATH");
                return true;
            },
            [](const QString &target) { Target decoded; return decode(target, decoded); },
            "herdr"};
}
}
