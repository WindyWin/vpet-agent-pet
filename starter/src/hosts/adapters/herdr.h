#pragma once
#include "hosts/focus_service.h"
#include "platform/contracts/command.h"
#include "platform/contracts/process.h"

namespace pet::hosts::herdr {
constexpr auto id = "herdr";
// Target "<tab id>|<pane id>|<API socket path>", from HERDR_*. Tab and socket may be empty.
struct Target { QString tab, pane, socket; };
bool decode(const QString &target, Target &out);
// Tab focus moves attached clients; agent focus then picks the pane and marks it seen.
QVector<platform::Command> selectCommands(const Target &target);
// API socket used by a local herdr UI invocation; empty for other commands and remotes.
QString clientSocket(const QProcessEnvironment &environment, const QStringList &arguments);
Capture capture();
// Selects the tab and pane through the herdr CLI. Pane ancestors lead to the
// detached server, so live UI clients of the same socket are raised first, each
// after selecting the Konsole tab it runs in, before the session's own hints.
std::unique_ptr<Activation> activation(std::shared_ptr<platform::CommandRunner> commands,
                                       std::shared_ptr<const platform::ProcessServices> processes,
                                       const Registry &registry = Registry::builtin());
}
