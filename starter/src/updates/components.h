#pragma once
#include <QList>
#include <QString>
namespace pet::updates {
constexpr qint64 MaxComponentsManifest = 4 * 1024 * 1024;
struct ComponentFile {
    QString path, digest;
    qint64 size = 0;
    bool executable = false;
};
struct Component {
    QString name, archive, digest;
    qint64 size = 0;
    QList<ComponentFile> files;
};
struct Components {
    QList<Component> entries;
};
bool readComponents(const QString &path, const QString &digest, const QString &version,
                    const QString &architecture, Components &result, QString &error);
bool componentMatches(const Component &component, const QString &prefix);
bool componentsAvailable(const Components &components, const QString &prefix, const QString &cache);
bool assembleComponents(const Components &components, const QString &prefix, const QString &cache,
                        const QString &destination, QString &error);
}
