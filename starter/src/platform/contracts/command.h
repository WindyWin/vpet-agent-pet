#pragma once
#include <QMap>
#include <QStringList>

namespace pet::platform {
// An external program with an argument array. It never passes through a shell.
struct Command {
    QString program;
    QStringList arguments;
    QMap<QString, QString> environment; // Added to the inherited environment.
};
}
