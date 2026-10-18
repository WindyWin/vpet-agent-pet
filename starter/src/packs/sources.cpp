#include "sources.h"
#include "i18n/contexts.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QVersionNumber>
#include <cmath>

namespace pet::packs {
namespace {
const QRegularExpression version("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*)?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?$");
const QRegularExpression release("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$");
const QRegularExpression digestPattern("^[0-9a-f]{64}$");
bool id(const QString &value) {
    static const QRegularExpression pattern("^[a-z0-9-]{1,32}$");
    static const QRegularExpression reserved("^(con|prn|aux|nul|com[0-9]|lpt[0-9])$");
    return pattern.match(value).hasMatch() && !reserved.match(value).hasMatch();
}
bool write(const QString &path, const QByteArray &bytes, QString &error) {
    QSaveFile file(path);
    if (!QDir().mkpath(QFileInfo(path).path()) || !file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit()) { error = PackSources::tr("Cannot save pack sources or catalog cache."); return false; }
    return true;
}
}
QString officialSource() { return "https://github.com/WindyWin/agent-pet-gallery/releases/latest/download/index.json"; }
bool httpsUrl(const QUrl &url) {
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty()
        && url.userInfo().isEmpty() && !url.hasFragment() && url.toString().size() <= 2048;
}
QString Entry::key() const { return metadata["kind"].toString() + '/' + metadata["id"].toString(); }
bool newerPackVersion(const QString &candidate, const QString &installed) {
    if (!version.match(candidate).hasMatch() || !version.match(installed).hasMatch()) return false;
    auto split = [](QString v) { return v.section('+', 0, 0).split('-'); };
    const auto a = split(candidate), b = split(installed);
    const auto ac = a[0].split('.'), bc = b[0].split('.');
    for (int i = 0; i < 3; ++i) {
        if (ac[i].size() != bc[i].size()) return ac[i].size() > bc[i].size();
        if (ac[i] != bc[i]) return ac[i] > bc[i];
    }
    if (a.size() == 1 || b.size() == 1) return a.size() == 1 && b.size() != 1;
    const auto ap = candidate.section('+', 0, 0).mid(a[0].size() + 1).split('.');
    const auto bp = installed.section('+', 0, 0).mid(b[0].size() + 1).split('.');
    const QRegularExpression digits("^[0-9]+$");
    for (int i = 0; i < qMin(ap.size(), bp.size()); ++i) {
        if (ap[i] == bp[i]) continue;
        const bool an = digits.match(ap[i]).hasMatch(), bn = digits.match(bp[i]).hasMatch();
        if (an != bn) return !an;
        if (an && ap[i].size() != bp[i].size()) return ap[i].size() > bp[i].size();
        return ap[i] > bp[i];
    }
    return ap.size() > bp.size();
}
bool parseIndex(const QByteArray &bytes, QVector<Entry> &entries, QString &error) {
    auto fail = [&] { error = PackSources::tr("Malformed or unsupported pack catalog."); return false; };
    if (bytes.size() > MaxIndex) return fail();
    const auto document = QJsonDocument::fromJson(bytes);
    const auto root = document.object();
    if (!document.isObject() || root.size() != 2 || root["schema_version"].toDouble(-1) != 1 || !root["packs"].isArray()) return fail();
    const auto packs = root["packs"].toArray(); if (packs.size() > 1000) return fail();
    QVector<Entry> parsed; QSet<QString> keys;
    const QSet<QString> fields{"kind", "id", "version", "name", "description", "author", "license", "preview", "download", "bytes", "sha256", "schema", "minAppVersion", "pet", "setup"};
    for (const auto &value : packs) {
        if (!value.isObject()) return fail();
        const auto o = value.toObject(); Entry e;
        for (auto i = o.begin(); i != o.end(); ++i) if (!fields.contains(i.key())) return fail();
        for (const auto &field : {"kind", "id", "version", "name", "description", "author", "license", "preview", "download", "sha256", "minAppVersion"}) {
            const QString f(field); const int limit = f == "description" ? 4096 : (f == "preview" || f == "download") ? 2048 : f == "author" ? 128 : 64;
            if (!o[f].isString() || o[f].toString().trimmed().isEmpty() || o[f].toString().size() > limit) return fail();
        }
        const auto kind = o["kind"].toString();
        if ((kind != "pet" && kind != "plugin") || !id(o["id"].toString()) || o["id"] == "vpet"
            || !version.match(o["version"].toString()).hasMatch() || o["version"].toString().size() > 32
            || !release.match(o["minAppVersion"].toString()).hasMatch() || o["minAppVersion"].toString().size() > 32) return fail();
        if (kind == "plugin" ? !id(o["pet"].toString()) : o.contains("pet")) return fail();
        if (o.contains("setup") && (!o["setup"].isString() || o["setup"].toString().size() > 4096)) return fail();
        const double size = o["bytes"].toDouble(-1), schema = o["schema"].toDouble(-1);
        if (size < 1 || size > MaxDownload || std::floor(size) != size || schema < 1 || schema > 1000 || std::floor(schema) != schema) return fail();
        e.bytes = qint64(size); e.preview = QUrl(o["preview"].toString()); e.download = QUrl(o["download"].toString());
        e.digest = o["sha256"].toString(); e.description = o["description"].toString(); e.setup = o["setup"].toString();
        if (!httpsUrl(e.preview) || !httpsUrl(e.download) || !digestPattern.match(e.digest).hasMatch()) return fail();
        for (const auto &f : {"kind", "id", "version", "name", "author", "license", "schema", "minAppVersion"}) e.metadata[f] = o[f];
        if (kind == "plugin") e.metadata["pet"] = o["pet"];
        if (keys.contains(e.key())) return fail();
        keys.insert(e.key()); parsed.append(e);
    }
    entries = parsed; error.clear(); return true;
}
QString compatibility(const Entry &e, const QString &appVersion, const QStringList &pets) {
    if (e.metadata["schema"].toInt() != 2) return PackSources::tr("Unsupported catalog schema.");
    if (newerPackVersion(e.metadata["minAppVersion"].toString(), appVersion)) return PackSources::tr("Requires Agent Pet %1 or newer.").arg(e.metadata["minAppVersion"].toString());
    if (e.metadata["kind"] == "plugin" && !pets.contains(e.metadata["pet"].toString())) return PackSources::tr("Install the target pet first: %1").arg(e.metadata["pet"].toString());
    return {};
}
bool matches(const Entry &e, const QJsonObject &metadata) {
    for (auto i = e.metadata.begin(); i != e.metadata.end(); ++i) if (metadata[i.key()] != i.value()) return false;
    return true;
}
SourceStore::SourceStore(QString folder) : folder_(std::move(folder)) {}
QString SourceStore::cachePath(const QString &url) const {
    return folder_ + '/' + QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".json";
}
QStringList SourceStore::urls(QString &error) const {
    error.clear(); QStringList urls{officialSource()}; QFile file(folder_ + "/sources.json");
    if (!file.exists()) return urls;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024) { error = PackSources::tr("Cannot read pack sources."); return urls; }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray() || doc.array().size() > 32) { error = PackSources::tr("Cannot read pack sources."); return urls; }
    for (const auto &v : doc.array()) {
        const auto u = v.toString();
        if (!httpsUrl(QUrl(u))) { error = PackSources::tr("Cannot read pack sources."); return {officialSource()}; }
        if (!urls.contains(u)) urls.append(u);
    }
    return urls;
}
bool SourceStore::save(const QStringList &urls, QString &error) {
    return write(folder_ + "/sources.json", QJsonDocument(QJsonArray::fromStringList(urls)).toJson(), error);
}
bool SourceStore::add(const QString &url, QString &error) {
    if (!httpsUrl(QUrl(url))) { error = PackSources::tr("Enter an HTTPS URL without credentials or a fragment."); return false; }
    QDir().mkpath(folder_); QLockFile lock(folder_ + "/sources.lock");
    if (!lock.tryLock()) { error = PackSources::tr("Another source change is in progress."); return false; }
    auto list = urls(error); if (!error.isEmpty()) return false;
    if (list.contains(url)) return true;
    if (list.size() >= 32) { error = PackSources::tr("At most 32 sources are supported."); return false; }
    list.append(url); return save(list, error);
}
bool SourceStore::remove(const QString &url, QString &error) {
    if (url == officialSource()) { error = PackSources::tr("The official source cannot be removed."); return false; }
    QDir().mkpath(folder_); QLockFile lock(folder_ + "/sources.lock");
    if (!lock.tryLock()) { error = PackSources::tr("Another source change is in progress."); return false; }
    auto list = urls(error); if (!error.isEmpty()) return false;
    list.removeAll(url); return save(list, error);
}
QVector<Entry> SourceStore::cached(const QString &url, QString &error) const {
    error.clear(); QVector<Entry> entries; QFile file(cachePath(url)); if (!file.exists()) return entries;
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxIndex) { error = PackSources::tr("Cannot read cached catalog."); return entries; }
    parseIndex(file.readAll(), entries, error); return entries;
}
bool SourceStore::cache(const QString &url, const QByteArray &bytes, QString &error) {
    QVector<Entry> entries; return parseIndex(bytes, entries, error) && write(cachePath(url), bytes, error);
}
Download::Download(QNetworkAccessManager *network, QObject *parent) : QObject(parent), network_(network) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] { stop(PackSources::tr("Download timed out.")); });
}
Download::~Download() { if (reply_) { reply_->disconnect(this); reply_->abort(); reply_->deleteLater(); } }
void Download::stop(const QString &error) { if (reply_) { failure_ = error; if (!reply_->isFinished()) reply_->abort(); } }
void Download::cancel() { stop(PackSources::tr("Download cancelled. Installed packs are unchanged.")); }
void Download::start(const QUrl &url, qint64 limit, qint64 expected, const QString &digest) {
    if (busy()) return;
    if (!httpsUrl(url) || limit < 1 || limit > MaxDownload || (expected != -1 && (expected < 1 || expected > limit))
        || (!digest.isEmpty() && (!digestPattern.match(digest).hasMatch() || expected < 1))) { emit failed(PackSources::tr("Invalid download URL, size or digest.")); return; }
    file_ = std::make_unique<QTemporaryFile>();
    if (!file_->open()) { emit failed(PackSources::tr("Cannot create a temporary download.")); return; }
    limit_ = limit; expected_ = expected; digest_ = digest; failure_.clear();
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setRawHeader("Accept-Encoding", "identity"); request.setTransferTimeout(30000); request.setMaximumRedirectsAllowed(5);
    reply_ = network_->get(request); reply_->setReadBufferSize(128 * 1024);
    connect(reply_, &QNetworkReply::redirected, this, [this](const QUrl &target) {
        if (!httpsUrl(target)) stop(PackSources::tr("Download redirected to an unsafe URL."));
        else reply_->redirectAllowed();
    });
    connect(reply_, &QNetworkReply::metaDataChanged, this, [this] {
        if (reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) return;
        const auto length = reply_->header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid() && (length.toLongLong() > limit_ || (expected_ > 0 && length.toLongLong() != expected_)))
            stop(PackSources::tr("Download size does not match the catalog or exceeds the limit."));
    });
    connect(reply_, &QIODevice::readyRead, this, &Download::drain);
    connect(reply_, &QNetworkReply::finished, this, [this] {
        if (failure_.isEmpty()) drain();
        timer_.stop(); auto *reply = reply_.data(); reply_ = nullptr;
        QString error = failure_;
        if (error.isEmpty() && (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200))
            error = PackSources::tr("Download failed: %1").arg(reply->errorString());
        if (error.isEmpty() && (file_->size() == 0 || (expected_ > 0 && file_->size() != expected_))) error = PackSources::tr("Download size does not match the catalog or exceeds the limit.");
        if (error.isEmpty() && !digest_.isEmpty()) {
            file_->seek(0); QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(file_.get()) || hash.result().toHex() != digest_.toLatin1()) error = PackSources::tr("Download SHA-256 does not match the catalog.");
        }
        auto file = std::move(file_); file->flush(); reply->deleteLater();
        if (error.isEmpty()) emit completed(file->fileName()); else emit failed(error);
    });
    timer_.start(10 * 60 * 1000);
}
void Download::drain() {
    if (!reply_ || !failure_.isEmpty() || reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) return;
    while (reply_->bytesAvailable() > 0) {
        const auto bytes = reply_->read(64 * 1024);
        if (bytes.isEmpty()) break;
        if (file_->size() + bytes.size() > limit_ || (expected_ > 0 && file_->size() + bytes.size() > expected_)) { stop(PackSources::tr("Download size does not match the catalog or exceeds the limit.")); return; }
        if (file_->write(bytes) != bytes.size()) { stop(PackSources::tr("Cannot save downloaded data.")); return; }
    }
    emit progress(file_->size(), expected_);
}
}
