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
    QString componentsDigest;
    QUrl componentsDownload;
    qint64 componentsSize = 0;
    QJsonObject json() const;
};
bool newer(const QString &candidate, const QString &installed);
bool parseRelease(const QJsonObject &object, const QString &architecture, Release &release, QString &error);
// Only exact release-local or digest-named shared component URLs are allowed.
bool validComponentUrl(const QString &url, const QString &version, const QString &architecture,
                       const QString &name, const QString &archive, const QString &digest);
bool verifiedArchive(const QString &path, const QString &digest, QString &error);
bool verifiedFile(const QString &path, const QString &digest, qint64 limit, QString &error);
QString installedPrefix();
QString dataDirectory();
}
