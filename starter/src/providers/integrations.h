#pragma once
#include <QJsonObject>
#include <QStringList>
namespace pet {
QString hookCommand(const QString &executable, const QString &provider);
bool mergeIntegration(const QJsonObject &input, const QString &provider, const QString &executable,
                      bool enable, QJsonObject &output, int &owned, QString &error);
int integrationCommand(const QStringList &args);
}
