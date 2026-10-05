#pragma once
#include "outcome.h"
#include <QMap>
#include <QStringList>

namespace pet::platform {
// An external program with an argument array. It never passes through a shell.
struct Command {
    QString program;
    QStringList arguments;
    QMap<QString, QString> environment; // Added to the inherited environment.
};
class CommandRunner {
public:
    virtual ~CommandRunner() = default;
    // Runs to completion within the runner's bound: Confirmed on exit status 0, Unsupported
    // when the program is not installed, TimedOut or Failed otherwise.
    virtual Outcome run(const Command &command, QByteArray *output = nullptr) = 0;
};
}
