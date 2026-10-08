#pragma once
#include <QString>
namespace pet::platform {
// Only the current user's registered Inno Setup installation can be upgraded automatically.
QString registeredInstallDirectory();
bool registeredInstallation(const QString &prefix);
}
