#pragma once
#include <QStringList>
namespace pet {
// Starts `executable arguments...` detached, without retaining the caller's
// input/output pipes or unrelated handles. POSIX systems use double fork and
// setsid, working directory / and standard streams on /dev/null.
// Returns false when the program could not be executed.
bool launchDetached(const QString &executable, const QStringList &arguments);
// The program that shows the pet: this executable, or on Windows the GUI program beside the
// console companion that runs hooks and commands.
QString petExecutable();
// Login registration for `executable`. Linux uses an XDG autostart entry, macOS a
// per-user launchd agent, Windows a value under the user's Run registry key; `directory`
// overrides its default per-user location (a registry key on Windows) for tests.
QString loginEntryPath(const QString &directory = {});
bool loginStartEnabled(const QString &directory = {});
bool setLoginStart(bool enabled, const QString &executable, QString *error = nullptr, const QString &directory = {});
}
