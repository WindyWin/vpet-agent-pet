#pragma once
#include "platform/contracts/startup.h"
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <functional>

namespace pet {
using Launcher = std::function<bool(const QString &executable, const QStringList &arguments)>;
// The hook's decision after its send failed because no pet is listening: launch the
// pet with the normalized event when this is a session start, autostart is enabled
// in the preferences at `preferencesPath` and a display is available.
// Returns true when `launch` was called and succeeded.
bool autostartPet(const QByteArray &event, const QString &kind, const QString &preferencesPath,
                  const QProcessEnvironment &environment, const QString &executable, const Launcher &launch);
// agent-pet autostart enable|disable|status [--when-idle keep|hide|quit]
// agent-pet autostart login enable|disable|status; no display needed.
int autostartCommand(const QStringList &args);
}
