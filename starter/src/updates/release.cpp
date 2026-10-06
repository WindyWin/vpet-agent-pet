#include "release.h"
#include "i18n/contexts.h"
#include "components.h"
#include "platform/contracts/update_layout.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>
namespace pet::updates {
static const QRegularExpression versionPattern("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$");
bool newer(const QString &candidate, const QString &installed) {
    return versionPattern.match(candidate).hasMatch() && versionPattern.match(installed).hasMatch()
        && QVersionNumber::compare(QVersionNumber::fromString(candidate), QVersionNumber::fromString(installed)) > 0;
}
QJsonObject Release::json() const {
    return {{"version", version}, {"digest", digest}, {"page", page.toString()}, {"download", download.toString()}, {"size", size}};
}
bool parseRelease(const QJsonObject &object, const QString &architecture, Release &release, QString &error) {
    release = {};
    QString tag = object["tag_name"].toString();
    QString version = tag.startsWith('v') ? tag.mid(1) : tag;
    if (!object.contains("draft") || !object.contains("prerelease") || object["draft"].toBool(true)
        || object["prerelease"].toBool(true) || !versionPattern.match(version).hasMatch()) {
        error = Updater::tr("No supported stable release was returned."); return false;
    }
    const QString base = "https://github.com/WindyWin/vpet-agent-pet/releases/";
    release.version = version;
    release.page = QUrl(base + "tag/" + tag);
    const QString name = platform::releaseArchive(version, architecture);
    const QString componentsName = platform::componentsManifest(version, architecture);
    for (const auto &entry : object["assets"].toArray()) {
        const auto asset = entry.toObject();
        if (asset["name"].toString() == componentsName && asset["state"].toString() == "uploaded"
            && asset["browser_download_url"].toString() == base + "download/" + tag + '/' + componentsName
            && asset["size"].toInteger() > 0 && asset["size"].toInteger() <= MaxComponentsManifest
            && QRegularExpression("^sha256:[0-9a-f]{64}$").match(asset["digest"].toString()).hasMatch()) {
            release.componentsDigest = asset["digest"].toString();
            release.componentsDownload = QUrl(asset["browser_download_url"].toString());
            release.componentsSize = asset["size"].toInteger();
        }
    }
    for (const auto &entry : object["assets"].toArray()) {
        const auto asset = entry.toObject();
        if (asset["name"].toString() != name) continue;
        const QUrl url(asset["browser_download_url"].toString());
        const QString expected = base + "download/" + tag + "/" + name;
        if (url.toString() != expected || asset["state"].toString() != "uploaded") continue;
        release.size = asset["size"].toInteger();
        release.digest = asset["digest"].toString();
        if (release.size <= 0 || release.size > MaxArchive) continue;
        release.download = url;
        // Missing digests still allow release notifications and manual download.
        if (!QRegularExpression("^sha256:[0-9a-f]{64}$").match(release.digest).hasMatch()) release.digest.clear();
        return true;
    }
    error = Updater::tr("This release has no compatible package for this system."); return false;
}
bool verifiedArchive(const QString &path, const QString &digest, QString &error) {
    if (QFileInfo(path).size() <= 0) { error = Updater::tr("Cannot read update package."); return false; }
    return verifiedFile(path, digest, MaxArchive, error);
}
bool verifiedFile(const QString &path, const QString &digest, qint64 limit, QString &error) {
    if (!QRegularExpression("^sha256:[0-9a-f]{64}$").match(digest).hasMatch()) { error = Updater::tr("Release has no SHA-256 digest."); return false; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) { error = Updater::tr("Cannot read update package."); return false; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result().toHex() != digest.mid(7).toLatin1()) { error = Updater::tr("Update checksum does not match the release."); return false; }
    return true;
}
QString dataDirectory() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/updates"; }
QString installedPrefix() { return platform::managedInstallPrefix(); }
}
