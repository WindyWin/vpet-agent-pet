#pragma once
#include "platform/contracts/command.h"

namespace pet::platform {
// Runs host CLIs such as tmux and herdr with QProcess. Desktop launchers often lack
// user tool directories on PATH, so common install locations are searched too.
class LinuxCommandRunner : public CommandRunner {
public:
    static constexpr int timeoutMs = 1500; // A click waits at most this long for a hung multiplexer.
    Outcome run(const Command &command, QByteArray *output = nullptr) override;
};
}
