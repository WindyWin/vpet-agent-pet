#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace pet {
enum class IntegrationFormat { HookGroups, NamedHooks, JavaScriptPlugin };
struct Provider {
    QString id;
    QString name;
    IntegrationFormat format;
    QStringList events;
    QJsonObject (*normalize)(const QJsonObject &, qint64);
};
const QVector<Provider> &providers();
const Provider *findProvider(const QString &id);
}
