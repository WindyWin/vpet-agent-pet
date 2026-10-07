#include "platform/contracts/startup.h"
#include "i18n/contexts.h"
#include <QDir>
#include <QSettings>

// A value under the user's Run key, which Explorer starts once at sign-in. The location is a
// registry key, which tests replace with their own.
namespace pet {
static constexpr auto runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr auto valueName = "Agent Pet";
static QString key(const QString &directory) { return directory.isEmpty() ? QString(runKey) : directory; }
QString loginEntryPath(const QString &directory) { return key(directory) + '\\' + valueName; }
bool loginStartEnabled(const QString &directory) {
    return QSettings(key(directory), QSettings::NativeFormat).contains(valueName);
}
bool setLoginStart(bool enabled, const QString &executable, QString *error, const QString &directory) {
    QSettings settings(key(directory), QSettings::NativeFormat);
    if (enabled) settings.setValue(valueName, '"' + QDir::toNativeSeparators(executable) + '"');
    else settings.remove(valueName);
    settings.sync();
    if (settings.status() == QSettings::NoError) return true;
    if (error) *error = Startup::tr("Cannot change %1").arg(loginEntryPath(directory));
    return false;
}
}
