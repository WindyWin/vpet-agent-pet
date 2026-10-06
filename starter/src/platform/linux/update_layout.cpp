#include "platform/contracts/update_layout.h"
#include "version.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>
#include <signal.h>
#include <unistd.h>
namespace pet::platform {
QString updateArchitecture() { return QSysInfo::buildCpuArchitecture(); }
QString releaseArchive(const QString &version, const QString &architecture, const QString &component) {
    return "agent-pet-" + version + "-linux-" + architecture
        + (component.isEmpty() ? QString{} : '-' + component) + ".tar.gz";
}
QString componentsManifest(const QString &version, const QString &architecture) {
    return "agent-pet-" + version + "-linux-" + architecture + "-components.json";
}
QStringList obsoleteComponentPatterns() {
    return {releaseArchive("*", "*", "app"), releaseArchive("*", "*", "runtime"), releaseArchive("*", "*", "artwork")};
}
QString applicationRelativePath() { return "bin/agent-pet"; }
QString updaterRelativePath() { return "bin/agent-pet-updater"; }
QString artworkRelativePath() { return "share/agent-pet/artwork.rcc"; }
QString artworkPackRelativePath(const QString &name) { return "share/agent-pet/" + name + ".rcc"; }
bool processRunning(qint64 pid) { return pid > 0 && kill(pid_t(pid), 0) == 0; }
QString managedInstallPrefix() {
    if (QStringLiteral(AGENT_PET_REVISION) == "local") return {};
    const QString prefix = QFileInfo(QCoreApplication::applicationFilePath()).absoluteDir().absoluteFilePath("..");
    const QString canonical = QFileInfo(prefix).canonicalFilePath();
    QFile receipt(canonical + "/.agent-pet-install");
    if (!receipt.open(QIODevice::ReadOnly) || receipt.size() > 65536 || !QFileInfo(canonical).isWritable()) return {};
    if (!receipt.readAll().split('\n').contains(("prefix=" + canonical).toUtf8())) return {};
    if (!QFileInfo(canonical + "/" + updaterRelativePath()).isExecutable()) return {};
    return canonical;
}
}
