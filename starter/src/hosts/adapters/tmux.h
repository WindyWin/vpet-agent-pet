#pragma once
#include "hosts/focus_service.h"
#include "platform/contracts/command.h"
#include "platform/contracts/process.h"

namespace pet::hosts::tmux {
constexpr auto id = "tmux";
// Target "<server socket path>|<pane id>", from $TMUX and $TMUX_PANE. The socket may be empty.
struct Target { QString socket, pane; };
bool decode(const QString &target, Target &out);
// Selects the pane's window, then the pane.
QVector<platform::Command> selectCommands(const Target &target);
// Prints the PID of each client attached to the pane's session, one per line.
platform::Command listClients(const Target &target);
Capture capture();
// Selects the pane through the tmux CLI. The server is detached from any terminal,
// so the window to raise is found from the attached clients' ancestors.
std::unique_ptr<Activation> activation(std::shared_ptr<platform::CommandRunner> commands,
                                       std::shared_ptr<const platform::ProcessServices> processes);
}
