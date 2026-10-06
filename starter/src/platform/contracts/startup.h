#pragma once
#include <QStringList>
namespace pet {
// Starts `executable arguments...` detached, without retaining the caller's
// input/output pipes or unrelated handles. POSIX systems use double fork and
// setsid, working directory / and standard streams on /dev/null.
// Returns false when the program could not be executed.
bool launchDetached(const QString &executable, const QStringList &arguments);
// Login registration for `executable`. Linux uses an XDG autostart entry, macOS a
// per-user launchd agent; `directory` overrides its default per-user location for tests.
QString loginEntryPath(const QString &directory = {});
bool loginStartEnabled(const QString &directory = {});
bool setLoginStart(bool enabled, const QString &executable, QString *error = nullptr, const QString &directory = {});
}
