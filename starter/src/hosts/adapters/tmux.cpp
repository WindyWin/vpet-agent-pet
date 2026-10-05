#include "tmux.h"
#include "pattern.h"
#include <QDir>

namespace pet::hosts::tmux {
bool decode(const QString &target, Target &out) {
    const auto parts = target.split('|');
    if (parts.size() != 2 || !fullMatch("%[0-9]{1,6}", parts[1]) || (!parts[0].isEmpty() && !QDir::isAbsolutePath(parts[0])))
        return false;
    out = {parts[0], parts[1]};
    return true;
}
static QStringList server(const Target &target) {
    return target.socket.isEmpty() ? QStringList() : QStringList{"-S", target.socket};
}
QVector<platform::Command> selectCommands(const Target &target) {
    return {{"tmux", server(target) + QStringList{"select-window", "-t", target.pane}, {}},
            {"tmux", server(target) + QStringList{"select-pane", "-t", target.pane}, {}}};
}
platform::Command listClients(const Target &target) {
    return {"tmux", server(target) + QStringList{"list-clients", "-t", target.pane, "-F", "#{client_pid}"}, {}};
}
Capture capture() {
    return {id, "tmux",
            [](const QProcessEnvironment &env, const QVector<qint64> &, QString &target) {
                if (env.value("TMUX").isEmpty() || env.value("TMUX_PANE").isEmpty()) return false;
                target = env.value("TMUX").section(',', 0, 0) + "|" + env.value("TMUX_PANE");
                return true;
            },
            [](const QString &target) { Target decoded; return decode(target, decoded); }};
}
}
