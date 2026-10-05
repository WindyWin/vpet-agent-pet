#pragma once
#include "hosts/focus_service.h"
#include "platform/contracts/command.h"

namespace pet::hosts {
// Runs a multiplexer's selection commands in order, stopping at the first that does not succeed.
inline Outcome runSelection(platform::CommandRunner &runner, const QVector<platform::Command> &commands) {
    for (const auto &command : commands)
        if (const auto outcome = runner.run(command); !platform::succeeded(outcome)) return outcome;
    return Outcome::Confirmed;
}
}
