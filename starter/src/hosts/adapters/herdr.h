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
// Verifies or finds a session's pane at Open time from `herdr agent list`:
//   - an agent whose `agent_session` is the session is authoritative;
//   - a captured pane is kept when herdr shows it running this provider in this
//     project (its tab refreshed), and is stale when it shows another session there;
//   - otherwise the one agent of that provider whose cwd is the project, never the
//     focused one of several: several are Ambiguous.
// Providers in `sharedDaemonProviders` run hooks from a daemon that inherits the
// environment of whichever pane started it, so their captured pane is only one
// candidate among the others, and its process hints are dropped. `socket` is the
// API socket to ask for a bare terminal (a captured pane asks its own); empty
// means herdr's default.
std::unique_ptr<Locator> locator(std::shared_ptr<platform::CommandRunner> commands, QString socket,
                                 QStringList sharedDaemonProviders = {"codex"});
// Selects the tab and pane through the herdr CLI. Pane ancestors lead to the
// detached server, so live UI clients of the same socket are raised first, each
// after selecting the Konsole tab it runs in, before the session's own hints.
std::unique_ptr<Activation> activation(std::shared_ptr<platform::CommandRunner> commands,
                                       std::shared_ptr<const platform::ProcessServices> processes,
                                       const Registry &registry = Registry::builtin());
}
