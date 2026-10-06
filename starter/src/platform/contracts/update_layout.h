#pragma once
#include <QStringList>
namespace pet::platform {
QString updateArchitecture();
QString releaseArchive(const QString &version, const QString &architecture, const QString &component = {});
QString componentsManifest(const QString &version, const QString &architecture);
QStringList obsoleteComponentPatterns();
QString applicationRelativePath();
QString updaterRelativePath();
QString artworkRelativePath();
QString artworkPackRelativePath(const QString &name);
QString managedInstallPrefix();
bool processRunning(qint64 pid);
}
