#pragma once
#include <QString>
namespace pet::updates {
bool extractArchive(const QString &archive, const QString &destination, QString &error);
bool exchangeDirectories(const QString &first, const QString &second, QString &error);
bool recoverInstallation(const QString &prefix, QString &error);
}
