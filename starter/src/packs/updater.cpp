#include "updater.h"
#include "animation/pet_library.h"
#include "version.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>

namespace pet::packs {
namespace {
QPointer<Updater> live;
QJsonObject read(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly) || f.size() > 128 * 1024) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
bool write(const QString &path, const QJsonObject &o, QString &error) {
    QSaveFile f(path); const auto b = QJsonDocument(o).toJson();
    if (f.open(QIODevice::WriteOnly) && f.write(b) == b.size() && f.commit()) return true;
    error = Updater::tr("Cannot save pack update settings."); return false;
}
QStringList pets() {
    QStringList result;
    for (const auto &p : PetLibrary::shared().pets()) if (p.error.isEmpty()) result.append(p.id);
    return result;
}
}
Updater::Updater(QString data, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), data_(std::move(data)), store_(data_ + "/sources"), transfer_(network ? network : &owned_, this) {
    live = this;
    connect(&transfer_, &Download::completed, this, &Updater::received);
    connect(&transfer_, &Download::progress, this, &Updater::progress);
    connect(&transfer_, &Download::failed, this, [this](const QString &error) {
        status(error);
        if (stopped_) return;
        const auto generation = generation_;
        QTimer::singleShot(0, this, [this, generation] { if (generation != generation_) return; if (catalog_) nextSource(); else nextPack(); });
    });
}
Updater::~Updater() { stopped_ = true; transfer_.cancel(); }
Updater *Updater::active() { return live.data(); }
void Updater::status(const QString &text) { message_ = text; emit changed(); }
void Updater::start() {
    QTimer::singleShot(15000, this, [this] { check(); });
    auto *timer = new QTimer(this); timer->setInterval(60 * 60 * 1000);
    connect(timer, &QTimer::timeout, this, [this] { check(); }); timer->start();
}
bool Updater::automatic(const QString &source) const {
    const auto state = read(data_ + "/sources/updates.json");
    const auto revision = sourceRevision(source, data_);
    return state["format"].toInt() == 1 && !revision.isEmpty() && state["automatic"].toObject()[source].isString()
        && state["automatic"].toObject()[source].toString() == revision;
}
bool Updater::setAutomatic(const QString &source, bool value, QString &error) {
    QString reason; if (!store_.urls(reason).contains(source) || !reason.isEmpty()) { error = tr("Source is unavailable."); return false; }
    QDir().mkpath(data_ + "/sources"); QLockFile lock(data_ + "/sources/updates.lock");
    if (!lock.tryLock()) { error = tr("Another pack update settings change is in progress."); return false; }
    auto state = read(data_ + "/sources/updates.json");
    if (QFile::exists(data_ + "/sources/updates.json") && state["format"].toInt() != 1) { error = tr("Cannot read pack update settings."); return false; }
    auto consent = state["automatic"].toObject();
    if (value && sourceRevision(source, data_).isEmpty()) { error = tr("Source is unavailable."); return false; }
    if (value) consent[source] = sourceRevision(source, data_); else consent.remove(source);
    state["format"] = 1; state["automatic"] = consent;
    if (!write(data_ + "/sources/updates.json", state, error)) return false;
    if (!value && source == source_) cancel();
    emit changed(); return true;
}
void Updater::cancel() {
    stopped_ = true; ++generation_; sources_.clear(); queue_.clear(); transfer_.cancel();
    emit changed();
}
void Updater::policyChanged() {
    QString error;
    if (!checksEnabled() || (!source_.isEmpty() && (!store_.urls(error).contains(source_)
        || revision_ != sourceRevision(source_, data_)))) cancel();
}
void Updater::check(qint64 now) {
    if (transfer_.busy() || !checksEnabled()) return;
    const auto path = data_ + "/sources/updates.json";
    QDir().mkpath(data_ + "/sources"); QLockFile lock(data_ + "/sources/updates.lock"); if (!lock.tryLock()) return;
    auto state = read(path);
    if (QFile::exists(path) && state["format"].toInt() != 1) { status(tr("Cannot read pack update settings.")); return; }
    if (!now) now = QDateTime::currentSecsSinceEpoch();
    const auto last = state["attempt"].toInteger();
    if (last > 0 && last <= now && now - last < 86400) return;
    state["format"] = 1; state["attempt"] = now; QString error;
    if (!write(path, state, error)) { status(error); return; }
    const auto configured = store_.urls(error); if (!error.isEmpty()) { status(error); return; }
    sources_.clear();
    // Only contact sources belonging to installed, app-recorded origins.
    const QDir records(data_ + "/pack-origins");
    for (const auto &name : records.entryList({"*.json"}, QDir::Files)) {
        const auto stem = name.chopped(5); const int split = stem.indexOf('-');
        const auto o = packOrigin(stem.left(split), stem.mid(split + 1), data_);
        const auto source = o["source"].toString();
        if (configured.contains(source) && !sources_.contains(source)) sources_.append(source);
    }
    stopped_ = false; ++generation_; nextSource();
}
void Updater::nextSource() {
    if (stopped_ || !checksEnabled()) return;
    queue_.clear();
    if (sources_.isEmpty()) { emit changed(); return; }
    source_ = sources_.takeFirst(); revision_ = sourceRevision(source_, data_); catalog_ = true;
    QString error;
    if (!store_.urls(error).contains(source_) || !error.isEmpty()) { nextSource(); return; }
    status(tr("Checking installed packs for updates…")); transfer_.start(QUrl(source_), MaxIndex); emit changed();
}
void Updater::received(const QString &path) {
    if (stopped_) return;
    policyChanged(); if (stopped_) return;
    QString error;
    if (catalog_) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly) || !store_.cache(source_, f.readAll(), error)) { status(error); nextSource(); return; }
        const auto entries = store_.cached(source_, error);
        for (const auto &e : entries) {
            const auto old = packOrigin(e.metadata["kind"].toString(), e.metadata["id"].toString(), data_);
            if (old["source"].toString() != source_ || !newerPackVersion(e.metadata["version"].toString(), old["version"].toString())) continue;
            status(tr("Update available: %1 %2").arg(e.metadata["name"].toString(), e.metadata["version"].toString()));
            if (automatic(source_) && compatibility(e, AGENT_PET_VERSION, pets()).isEmpty()) queue_.append(e);
        }
        emit changed(); nextPack(); return;
    }
    if (!stage(current_, source_, revision_, path, error)) status(error);
    else status(tr("Pack update staged. Restart Agent Pet to apply it."));
    nextPack();
}
void Updater::nextPack() {
    if (stopped_ || !checksEnabled()) return;
    if (queue_.isEmpty()) { nextSource(); return; }
    current_ = queue_.takeFirst(); catalog_ = false;
    const auto m = current_.metadata;
    if (!automatic(source_) || QFileInfo::exists(data_ + "/pack-pending/" + m["kind"].toString() + '-' + m["id"].toString())) { nextPack(); return; }
    transfer_.start(current_.download, MaxDownload, current_.bytes, current_.digest); emit changed();
}
bool Updater::sameConsent(const QJsonObject &old, const QJsonObject &next) {
    for (const auto &key : {"source", "revision", "license", "author", "terms"})
        if (!old[key].isString() || old[key].toString().isEmpty() || old[key] != next[key]) return false;
    return true;
}
bool Updater::stage(const Entry &entry, const QString &source, const QString &revision, const QString &archive, QString &error) {
    QString reason;
    if (!checksEnabled() || !automatic(source) || revision != sourceRevision(source, data_)
        || !store_.urls(reason).contains(source) || !reason.isEmpty()) { error = tr("Automatic update consent is no longer valid."); return false; }
    const auto old = packOrigin(entry.metadata["kind"].toString(), entry.metadata["id"].toString(), data_);
    if (old["source"].toString() != source || !newerPackVersion(entry.metadata["version"].toString(), old["version"].toString())) { error = tr("The installed pack or its origin changed."); return false; }
    // Verify again here so callers cannot bypass transport verification.
    QFile file(archive); QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || file.size() != entry.bytes || file.size() > MaxDownload
        || !hash.addData(&file) || hash.result().toHex() != entry.digest.toLatin1()) { error = tr("Downloaded pack failed integrity verification."); return false; }
    auto c = inspect(archive, error, data_); if (!c) return false;
    if (!matches(entry, c->metadata)) { error = tr("Archive metadata does not match the catalog."); return false; }
    c->origin = installationOrigin(*c, source, entry.digest, data_);
    if (!sameConsent(old, c->origin)) { error = tr("Updated pack terms or origin require manual acceptance."); return false; }
    c->automatic = true; return install(*c, error, data_);
}
}
