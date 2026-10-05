#include "generic.h"

namespace pet::hosts {
Capture vscode::capture() {
    return {id, "VS Code",
            [](const QProcessEnvironment &env, const QVector<qint64> &, QString &) { return env.value("TERM_PROGRAM") == "vscode"; },
            {}, {}, false};
}
Capture terminal::capture() {
    return {id, "Terminal",
            [](const QProcessEnvironment &, const QVector<qint64> &ancestors, QString &) { return !ancestors.isEmpty(); },
            {}};
}
}
