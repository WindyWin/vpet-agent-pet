#pragma once
#include "hosts/registry.h"
#include "platform/contracts/command.h"

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
}
