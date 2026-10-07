#include "platform/contracts/update_layout.h"
#include <QSysInfo>
#include <windows.h>
namespace pet::platform {
QString updateArchitecture() { return QSysInfo::buildCpuArchitecture(); }
QString releaseArchive(const QString &version, const QString &architecture, const QString &component) {
    return "agent-pet-" + version + "-windows-" + architecture + (component.isEmpty() ? QString{} : '-' + component) + ".zip";
}
QString componentsManifest(const QString &version, const QString &architecture) {
    return "agent-pet-" + version + "-windows-" + architecture + "-components.json";
}
QStringList obsoleteComponentPatterns() { return {}; }
// Relative to the installation directory, which holds the executables and artwork side by side.
QString applicationRelativePath() { return "agent-pet.exe"; }
QString updaterRelativePath() { return "agent-pet-updater.exe"; }
QString artworkRelativePath() { return "artwork.rcc"; }
QString artworkPackRelativePath(const QString &name) { return name + ".rcc"; }
// Automatic installation is not implemented: releases are announced and installed with the setup program.
QString managedInstallPrefix() { return {}; }
bool processRunning(qint64 pid) {
    const HANDLE process = pid > 0 ? OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid)) : nullptr;
    if (!process) return false;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}
}
