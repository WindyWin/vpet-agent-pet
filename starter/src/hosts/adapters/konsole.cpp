#include "konsole.h"
#include "pattern.h"

namespace pet::hosts::konsole {
bool decode(const QString &target, Target &out) {
    const auto parts = target.split('|');
    if (parts.size() != 3 || !fullMatch(R"((:[0-9]+\.[0-9]+|[A-Za-z_][A-Za-z0-9_-]*(\.[A-Za-z_][A-Za-z0-9_-]*)+))", parts[0]) ||
        !fullMatch("/Windows/[0-9]{1,6}", parts[1]) || !fullMatch("/Sessions/[0-9]{1,6}", parts[2])) return false;
    out = {parts[0], parts[1], parts[2].section('/', -1).toInt()};
    return true;
}
QString targetOf(const QProcessEnvironment &env) {
    return env.value("KONSOLE_DBUS_SERVICE") + "|" + env.value("KONSOLE_DBUS_WINDOW") + "|" + env.value("KONSOLE_DBUS_SESSION");
}
Capture capture() {
    return {id, "Konsole",
            [](const QProcessEnvironment &env, const QVector<qint64> &, QString &target) {
                if (env.value("KONSOLE_DBUS_SERVICE").isEmpty()) return false;
                target = targetOf(env);
                return true;
            },
            [](const QString &target) { Target decoded; return decode(target, decoded); }};
}
}
