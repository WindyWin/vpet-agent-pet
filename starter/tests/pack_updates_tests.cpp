#include "packs/updater.h"
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstring>
using namespace pet::packs;
namespace {
bool copyTree(const QString &source, const QString &target) {
    if (!QDir().mkpath(target)) return false;
    QDirIterator it(source, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); const auto to = target + '/' + QDir(source).relativeFilePath(it.filePath());
        if (it.fileInfo().isDir()) { if (!QDir().mkpath(to)) return false; }
        else if (!QFile::copy(it.filePath(), to)) return false;
    } return true;
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QJsonObject read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return QJsonDocument::fromJson(f.readAll()).object(); }
const QString source = "https://example.test/index.json";
std::unique_ptr<Candidate> candidate(const QString &data, QString &error, QString version = "1.0.0") {
    auto c = std::make_unique<Candidate>(); c->staging = std::make_unique<QTemporaryDir>(data + "/stage-XXXXXX");
    c->folder = c->staging->path() + "/local-cat";
    if (!copyTree(QString(PET_SOURCE) + "/tests/fixtures/local-pets/local-cat", c->folder)) return {};
    auto m = read(c->folder + "/pack.json"); m["version"] = version;
    if (!write(c->folder + "/pack.json", QJsonDocument(m).toJson()) || !validate(c->folder, c->metadata, c->terms, error)) return {};
    return c;
}
bool seed(const QString &data, QString &error) {
    SourceStore store(data + "/sources"); if (!store.add(source, error)) return false;
    auto c = candidate(data, error); if (!c) return false;
    c->origin = installationOrigin(*c, source, QString(64, 'a'), data);
    return install(*c, error, data) && applyPending(error, data);
}
Entry archive(const QString &data, QString &error, QString version = "1.1.0", bool changedTerms = false) {
    auto c = candidate(data, error, version); if (!c) return {};
    if (changedTerms) write(c->folder + "/licenses/TERMS.txt", "Different terms\n");
    QProcess zip;
    zip.start(QString(PYTHON), {"-c", "import pathlib,sys,zipfile\np=pathlib.Path(sys.argv[1])\nwith zipfile.ZipFile(sys.argv[2],'w') as z:\n for f in sorted(p.rglob('*')):\n  if f.is_file(): z.write(f,f.relative_to(p).as_posix())", c->folder, data + "/update.agentpet"});
    if (!zip.waitForFinished() || zip.exitCode()) return {};
    QFile f(data + "/update.agentpet"); if (!f.open(QIODevice::ReadOnly)) return {};
    Entry e; e.metadata = c->metadata; e.bytes = f.size(); e.digest = QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
    e.download = QUrl("https://example.test/update.agentpet"); e.preview = QUrl("https://example.test/preview.png"); return e;
}
QByteArray catalog(const Entry &e) {
    auto m = e.metadata; m.remove("terms"); m["description"] = "Updated cat"; m["download"] = e.download.toString(); m["preview"] = e.preview.toString();
    m["bytes"] = double(e.bytes); m["sha256"] = e.digest;
    return QJsonDocument(QJsonObject{{"schema_version", 1}, {"packs", QJsonArray{m}}}).toJson();
}
class Reply : public QNetworkReply {
public:
    Reply(const QNetworkRequest &r, QByteArray bytes, bool wait, QObject *parent) : QNetworkReply(parent), bytes_(std::move(bytes)) {
        setRequest(r); setUrl(r.url()); open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this, wait] {
            if (isFinished()) return;
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); emit metaDataChanged();
            if (wait || isFinished()) return;
            ready_ = true; emit readyRead(); if (isFinished()) return;
            setFinished(true); emit finished();
        });
    }
    void abort() override { if (isFinished()) return; setError(OperationCanceledError, "Interrupted"); setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return (ready_ ? bytes_.size() - offset_ : 0) + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *buffer, qint64 max) override {
        if (!ready_) return 0;
        auto n = qMin(max, qint64(bytes_.size()) - offset_); if (!n) return -1;
        std::memcpy(buffer, bytes_.constData() + offset_, size_t(n)); offset_ += n; return n;
    }
private:
    QByteArray bytes_; qint64 offset_ = 0; bool ready_ = false;
};
class Network : public QNetworkAccessManager {
public:
    QHash<QString, QByteArray> responses; QStringList requested; bool waitArchive = false;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &r, QIODevice *) override {
        requested.append(r.url().toString());
        return new Reply(r, responses.value(r.url().toString()), waitArchive && r.url().path().endsWith("agentpet"), this);
    }
};
}
class PackUpdatesTests : public QObject {
    Q_OBJECT
private slots:
    void failedSourceRemovalPreservesRevision_data() {
        QTest::addColumn<bool>("existingRevision");
        QTest::newRow("initial revision") << false;
        QTest::newRow("recorded revision") << true;
    }
    void failedSourceRemovalPreservesRevision() {
        QFETCH(bool, existingRevision);
        QTemporaryDir data; QString error; QVERIFY2(seed(data.path(), error), qPrintable(error));
        SourceStore store(data.path() + "/sources");
        const auto revision = data.path() + "/sources/" + QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".revision";
        if (existingRevision) {
            QVERIFY(store.remove(source, error)); QVERIFY(store.add(source, error));
            auto c = candidate(data.path(), error); QVERIFY(c);
            c->origin = installationOrigin(*c, source, QString(64, 'a'), data.path());
            QVERIFY(install(*c, error, data.path())); QVERIFY(applyPending(error, data.path()));
        }
        Updater updater(data.path()); QVERIFY(updater.setAutomatic(source, true, error));
        const auto before = sourceRevision(source, data.path());
        const auto sources = data.path() + "/sources/sources.json";
        const auto permissions = QFile::permissions(sources);
        QVERIFY(QFile::setPermissions(sources, QFileDevice::ReadOwner | QFileDevice::ReadUser));
        const bool removed = store.remove(source, error);
        QVERIFY(QFile::setPermissions(sources, permissions));
        QVERIFY(!removed); QVERIFY(!error.isEmpty());
        QCOMPARE(sourceRevision(source, data.path()), before);
        QCOMPARE(QFileInfo::exists(revision), existingRevision);
        QVERIFY(!packOrigin("pet", "local-cat", data.path()).isEmpty());
        QVERIFY(updater.automatic(source));
        QVERIFY(store.urls(error).contains(source));
    }
    void originsAndRestartRecovery() {
        QTemporaryDir data; QString error; QVERIFY2(seed(data.path(), error), qPrintable(error));
        QCOMPARE(packOrigin("pet", "local-cat", data.path())["source"].toString(), source);
        auto c = candidate(data.path(), error, "1.1.0"); QVERIFY(c);
        c->origin = installationOrigin(*c, source, QString(64, 'b'), data.path());
        QVERIFY(install(*c, error, data.path()));
        QCOMPARE(packOrigin("pet", "local-cat", data.path())["version"].toString(), QString("1.0.0"));
        const auto job = data.path() + "/pack-pending/pet-local-cat", target = data.path() + "/packs/pets/local-cat";
        // Crash after replacing artwork, before recording its new origin.
        QVERIFY(QDir().rename(target, job + "/previous")); QVERIFY(QDir().rename(job + "/payload/local-cat", target));
        QVERIFY(packOrigin("pet", "local-cat", data.path()).isEmpty());
        QVERIFY2(applyPending(error, data.path()), qPrintable(error));
        QCOMPARE(packOrigin("pet", "local-cat", data.path())["version"].toString(), QString("1.1.0"));
        // A local replacement clears its old online origin.
        c = candidate(data.path(), error, "1.2.0"); QVERIFY(c); QVERIFY(install(*c, error, data.path())); QVERIFY(applyPending(error, data.path()));
        QVERIFY(packOrigin("pet", "local-cat", data.path()).isEmpty());
        QVERIFY(remove("pet", "local-cat", error, data.path())); QVERIFY(applyPending(error, data.path()));
        QVERIFY(!QFileInfo::exists(data.path() + "/pack-origins/pet-local-cat.json"));
    }
    void originWriteFailureRestoresWorkingPayload() {
        QTemporaryDir data; QString error; QVERIFY(seed(data.path(), error));
        auto c = candidate(data.path(), error, "1.1.0"); QVERIFY(c);
        c->origin = installationOrigin(*c, source, QString(64, 'b'), data.path()); QVERIFY(install(*c, error, data.path()));
        const auto record = data.path() + "/pack-origins/pet-local-cat.json";
        QVERIFY(QFile::rename(record, record + ".previous")); QVERIFY(QDir().mkpath(record));
        QVERIFY(!applyPending(error, data.path()));
        QCOMPARE(read(data.path() + "/packs/pets/local-cat/pack.json")["version"].toString(), QString("1.0.0"));
        QVERIFY(QDir(record).removeRecursively()); QVERIFY(QFile::rename(record + ".previous", record));
        QCOMPARE(packOrigin("pet", "local-cat", data.path())["version"].toString(), QString("1.0.0"));
        QVERIFY2(applyPending(error, data.path()), qPrintable(error));
        QCOMPARE(packOrigin("pet", "local-cat", data.path())["version"].toString(), QString("1.1.0"));
    }
    void automaticStageAndFailures() {
        QTemporaryDir data; QString error; QVERIFY(seed(data.path(), error));
        Updater u(data.path()); u.checksEnabled = [] { return true; };
        const auto rev = sourceRevision(source, data.path()); const auto e = archive(data.path(), error); QVERIFY(e.bytes > 0);
        const auto path = data.path() + "/update.agentpet";
        QVERIFY(!u.stage(e, source, rev, path, error)); // opt in required
        QVERIFY(u.setAutomatic(source, true, error));
        auto broken = e; broken.digest = QString(64, '0'); QVERIFY(!u.stage(broken, source, rev, path, error));
        broken = e; broken.bytes++; QVERIFY(!u.stage(broken, source, rev, path, error));
        broken = e; broken.metadata["minAppVersion"] = "999.0.0"; QVERIFY(!u.stage(broken, source, rev, path, error));
        const auto terms = archive(data.path(), error, "1.1.0", true); QVERIFY(!u.stage(terms, source, rev, path, error)); QVERIFY(error.contains("terms"));
        const auto valid = archive(data.path(), error); QVERIFY2(u.stage(valid, source, rev, path, error), qPrintable(error));
        QVERIFY2(applyPending(error, data.path()), qPrintable(error));
        QCOMPARE(packOrigin("pet", "local-cat", data.path())["version"].toString(), QString("1.1.0"));
        QVERIFY(!u.stage(valid, source, rev, path, error)); // never equal/downgrade
    }
    void removalRevokesStagedWorkAndConsent() {
        QTemporaryDir data; QString error; QVERIFY(seed(data.path(), error));
        Updater u(data.path()); u.checksEnabled = [] { return true; }; QVERIFY(u.setAutomatic(source, true, error));
        const auto rev = sourceRevision(source, data.path()), target = data.path() + "/packs/pets/local-cat/pack.json";
        const auto e = archive(data.path(), error); QVERIFY(u.stage(e, source, rev, data.path() + "/update.agentpet", error));
        SourceStore store(data.path() + "/sources"); QVERIFY(store.remove(source, error));
        QVERIFY(packOrigin("pet", "local-cat", data.path()).isEmpty()); QVERIFY(!u.automatic(source));
        QVERIFY(applyPending(error, data.path())); QCOMPARE(read(target)["version"].toString(), QString("1.0.0"));
        QVERIFY(store.add(source, error)); QVERIFY(!u.automatic(source)); QVERIFY(packOrigin("pet", "local-cat", data.path()).isEmpty());
        QVERIFY(!u.stage(e, source, rev, data.path() + "/update.agentpet", error));
    }
    void scheduledChecksAndInterruptedDownloads() {
        QTemporaryDir data; QString error; QVERIFY(seed(data.path(), error));
        const auto e = archive(data.path(), error); QVERIFY(e.bytes > 0);
        QFile f(data.path() + "/update.agentpet"); QVERIFY(f.open(QIODevice::ReadOnly));
        Network n; n.responses[source] = catalog(e); n.responses[e.download.toString()] = f.readAll();
        Updater u(data.path(), &n); bool enabled = false; u.checksEnabled = [&] { return enabled; };
        u.check(100000); QCOMPARE(n.requested.size(), 0); enabled = true;
        u.check(100000); QTRY_COMPARE(n.requested.size(), 1); QTRY_VERIFY(!u.busy());
        QCOMPARE(n.requested.size(), 1); // default notification only
        SourceStore store(data.path() + "/sources"); QVERIFY(!store.cached(source, error).isEmpty());
        u.check(100001); QCOMPARE(n.requested.size(), 1);
        QVERIFY(u.setAutomatic(source, true, error)); n.waitArchive = true;
        u.check(200000); QTRY_COMPARE(n.requested.size(), 3); QVERIFY(u.busy());
        enabled = false; u.policyChanged(); QVERIFY(!u.busy());
        QVERIFY(!QFileInfo::exists(data.path() + "/pack-pending/pet-local-cat"));
        QCOMPARE(packOrigin("pet", "local-cat", data.path())["version"].toString(), QString("1.0.0"));
        enabled = true; n.waitArchive = false; u.check(300000);
        QTRY_VERIFY(QFileInfo::exists(data.path() + "/pack-pending/pet-local-cat"));
        QVERIFY(applyPending(error, data.path())); QCOMPARE(packOrigin("pet", "local-cat", data.path())["version"].toString(), QString("1.1.0"));
    }
    void localPacksAreNeverInferredFromCatalogs() {
        QTemporaryDir data; QString error; auto c = candidate(data.path(), error); QVERIFY(c);
        QVERIFY(install(*c, error, data.path())); QVERIFY(applyPending(error, data.path()));
        SourceStore store(data.path() + "/sources"); QVERIFY(store.add(source, error));
        const auto e = archive(data.path(), error); QVERIFY(store.cache(source, catalog(e), error));
        Network n; Updater u(data.path(), &n); u.checksEnabled = [] { return true; };
        QVERIFY(u.setAutomatic(source, true, error)); u.check(100000); QCOMPARE(n.requested.size(), 0);
        QVERIFY(!u.stage(e, source, sourceRevision(source, data.path()), data.path() + "/update.agentpet", error));
        QVERIFY(packOrigin("pet", "local-cat", data.path()).isEmpty());
    }
    void offlineCacheAndPersistentThrottle() {
        QTemporaryDir data; QString error; QVERIFY(seed(data.path(), error));
        const auto e = archive(data.path(), error); SourceStore store(data.path() + "/sources");
        QVERIFY(store.cache(source, catalog(e), error)); Network n; n.responses[source] = "malformed";
        {
            Updater u(data.path(), &n); u.checksEnabled = [] { return true; };
            QVERIFY(u.setAutomatic(source, true, error)); u.check(100000); QTRY_VERIFY(!u.busy());
            QCOMPARE(n.requested.size(), 1); QCOMPARE(store.cached(source, error).size(), 1);
            QVERIFY(!QFileInfo::exists(data.path() + "/pack-pending/pet-local-cat"));
        }
        Updater restarted(data.path(), &n); restarted.checksEnabled = [] { return true; };
        QVERIFY(restarted.automatic(source)); restarted.check(100001); QCOMPARE(n.requested.size(), 1);
        QCOMPARE(packOrigin("pet", "local-cat", data.path())["version"].toString(), QString("1.0.0"));
        QVERIFY(write(data.path() + "/sources/updates.json", "broken"));
        QVERIFY(!restarted.automatic(source)); restarted.check(200000); QCOMPARE(n.requested.size(), 1);
        QVERIFY(!restarted.setAutomatic(source, true, error));
    }
    void incompatibleCatalogIsVisibleWithoutDownload() {
        QTemporaryDir data; QString error; QVERIFY(seed(data.path(), error));
        auto e = archive(data.path(), error); e.metadata["minAppVersion"] = "999.0.0";
        Network n; n.responses[source] = catalog(e); Updater u(data.path(), &n); u.checksEnabled = [] { return true; };
        QVERIFY(u.setAutomatic(source, true, error)); u.check(100000); QTRY_COMPARE(n.requested.size(), 1); QTRY_VERIFY(!u.busy());
        QCOMPARE(n.requested.size(), 1); SourceStore store(data.path() + "/sources");
        const auto cached = store.cached(source, error); QCOMPARE(cached.size(), 1);
        QVERIFY(!compatibility(cached[0], "0.17.1", {}).isEmpty());
    }
};
QTEST_GUILESS_MAIN(PackUpdatesTests)
#include "pack_updates_tests.moc"
