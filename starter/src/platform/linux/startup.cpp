#include "platform/contracts/startup.h"
#include "i18n/contexts.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace pet {
QString loginEntryPath(const QString &directory) {
    const auto base = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart" : directory;
    return base + "/agent-pet.desktop";
}
bool loginStartEnabled(const QString &directory) { return QFile::exists(loginEntryPath(directory)); }
// Desktop Entry Exec quoting: double quotes, with \ " ` $ escaped, then each backslash doubled at string level.
static QString desktopExec(const QString &executable) {
    QString quoted;
    for (const QChar c : executable) {
        if (c == '%') quoted += "%%";
        else if (c == '\\' || c == '"' || c == '`' || c == '$') quoted += QString("\\\\") + c;
        else quoted += c;
    }
    return "\"" + quoted + "\"";
}
bool setLoginStart(bool enabled, const QString &executable, QString *error, const QString &directory) {
    const auto path = loginEntryPath(directory);
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!enabled) return !QFile::exists(path) || QFile::remove(path) || fail(Startup::tr("Cannot remove %1").arg(path));
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return fail(Startup::tr("Cannot create %1").arg(QFileInfo(path).absolutePath()));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    const auto bytes = ("[Desktop Entry]\nType=Application\nName=Agent Pet\nComment=Start the Agent Pet desktop companion at login\n"
                        "Exec=" + desktopExec(executable) + "\nIcon=agent-pet\nTerminal=false\nX-GNOME-Autostart-enabled=true\n").toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) return fail(file.errorString());
    return true;
}
}
