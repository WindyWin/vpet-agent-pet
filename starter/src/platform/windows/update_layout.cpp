#include "platform/contracts/update_layout.h"
#include <QSysInfo>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include "version.h"
#include "installed_directory.h"
#include <windows.h>
namespace pet::platform {
QString updateArchitecture() { return QSysInfo::buildCpuArchitecture(); }
QString releaseArchive(const QString &version, const QString &architecture, const QString &component) {
    return "agent-pet-" + version + "-windows-" + architecture + (component.isEmpty() ? "-setup.exe" : '-' + component + ".zip");
}
QString componentsManifest(const QString &version, const QString &architecture) {
    Q_UNUSED(version); Q_UNUSED(architecture);
    return {}; // Windows upgrades use Setup, which maintains the uninstall log.
}
QStringList obsoleteComponentPatterns() { return {}; }
// Relative to the installation directory, which holds the executables and artwork side by side.
QString applicationRelativePath() { return "agent-pet.exe"; }
QString updaterRelativePath() { return "agent-pet-updater.exe"; }
QString artworkRelativePath() { return "artwork.rcc"; }
QString artworkPackRelativePath(const QString &name) { return name + ".rcc"; }
bool registeredInstallation(const QString &prefix) {
    QSettings uninstall("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{8441EED6-7EE4-4267-AADE-8840188C4202}_is1", QSettings::NativeFormat);
    const QString registered = QFileInfo(uninstall.value("Inno Setup: App Path").toString()).canonicalFilePath();
    return !prefix.isEmpty() && !registered.isEmpty()
        && QString::compare(prefix, registered, Qt::CaseInsensitive) == 0
        && QFileInfo(prefix + "/agent-pet.exe").isFile()
        && QFileInfo(prefix + "/unins000.exe").isFile()
        && QFileInfo(prefix + "/unins000.dat").isFile();
}
QString managedInstallPrefix() {
    if (QStringLiteral(AGENT_PET_REVISION) == "local") return {};
    const QString prefix = QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
    if (!registeredInstallation(prefix) || !QFileInfo(prefix).isWritable()
        || !QFileInfo(QFileInfo(prefix).absolutePath()).isWritable()
        || !QFileInfo(prefix + '/' + updaterRelativePath()).isFile()) return {};
    return prefix;
}
bool processRunning(qint64 pid) {
    const HANDLE process = pid > 0 ? OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid)) : nullptr;
    if (!process) return false;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}
}
