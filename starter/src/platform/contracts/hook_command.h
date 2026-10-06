#pragma once
#include <QJsonObject>
#include <QString>
namespace pet {
QString hookCommand(const QString &executable, const QString &provider);
bool ownedHookHandler(const QJsonObject &handler, const QString &provider);
}
