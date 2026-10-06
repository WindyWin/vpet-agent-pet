#include "platform/contracts/update_layout.h"
#include <signal.h>
#include <unistd.h>
namespace pet::platform {
// One universal (arm64 + x86_64) application bundle.
QString updateArchitecture() { return "universal"; }
QString releaseArchive(const QString &version, const QString &architecture, const QString &component) {
    return "agent-pet-" + version + "-macos-" + architecture + (component.isEmpty() ? QString{} : '-' + component) + ".zip";
}
QString componentsManifest(const QString &version, const QString &architecture) {
    return "agent-pet-" + version + "-macos-" + architecture + "-components.json";
}
QStringList obsoleteComponentPatterns() { return {}; }
// Relative to the bundle's Contents directory.
QString applicationRelativePath() { return "MacOS/agent-pet"; }
QString updaterRelativePath() { return "MacOS/agent-pet-updater"; }
QString artworkRelativePath() { return "Resources/artwork.rcc"; }
QString artworkPackRelativePath(const QString &name) { return "Resources/" + name + ".rcc"; }
// Automatic installation is not implemented: releases are announced and downloaded manually.
QString managedInstallPrefix() { return {}; }
bool processRunning(qint64 pid) { return pid > 0 && kill(pid_t(pid), 0) == 0; }
}
