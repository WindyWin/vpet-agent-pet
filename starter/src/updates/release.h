#pragma once
#include <QJsonObject>
#include <QString>
#include <QUrl>
namespace pet::updates {
constexpr qint64 MaxArchive = 512LL * 1024 * 1024;
struct Release {
    QString version, digest;
    QUrl page, download;
    qint64 size = 0;
    QJsonObject json() const;
};
bool newer(const QString &candidate, const QString &installed);
bool parseRelease(const QJsonObject &object, const QString &architecture, Release &release, QString &error);
bool verifiedArchive(const QString &path, const QString &digest, QString &error);
QString installedPrefix();
QString dataDirectory();
}
