#pragma once
#include <QProcessEnvironment>
#include <QStringList>
#include <functional>

namespace pet {
// Starts `executable arguments...` detached from the caller: double fork and setsid,
// working directory /, stdin/stdout/stderr on /dev/null and no other inherited
// descriptors, so an agent client waiting on the hook's pipes never waits on the pet.
// Returns false when the program could not be executed.
bool launchDetached(const QString &executable, const QStringList &arguments);
using Launcher = std::function<bool(const QString &executable, const QStringList &arguments)>;
// The hook's decision after its send failed because no pet is listening: launch the
// pet with the normalized event when this is a session start, autostart is enabled
// in the preferences at `preferencesPath` and a display is available.
// Returns true when `launch` was called and succeeded.
bool autostartPet(const QByteArray &event, const QString &kind, const QString &preferencesPath,
                  const QProcessEnvironment &environment, const QString &executable, const Launcher &launch);
// agent-pet autostart enable|disable|status [--when-idle keep|hide|quit]; no display needed.
int autostartCommand(const QStringList &args);
}
