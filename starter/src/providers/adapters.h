#pragma once
#include <QJsonObject>
#include <QStringList>
namespace pet {
// Empty result means unsupported or invalid input. Never forwards content fields.
QJsonObject normalizeHook(const QString &provider, const QJsonObject &input, qint64 now);
QStringList hookEvents(const QString &provider);
}
