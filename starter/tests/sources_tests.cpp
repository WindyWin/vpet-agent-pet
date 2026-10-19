#include "packs/sources.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstring>
using namespace pet::packs;
namespace {
QJsonObject entry() {
    return {{"kind", "pet"}, {"id", "cat"}, {"version", "1.0.0"}, {"name", "Cat"},
        {"description", "A cat"}, {"author", "Artist"}, {"license", "CC0-1.0"}, {"schema", 2},
        {"minAppVersion", "0.17.0"}, {"preview", "https://example.test/cat.png"},
        {"download", "https://example.test/cat.agentpet"}, {"bytes", 3}, {"sha256", QString(64, 'a')}};
}
QByteArray index(QJsonObject pack = entry()) { return QJsonDocument(QJsonObject{{"schema_version", 1}, {"packs", QJsonArray{pack}}}).toJson(); }
struct Response { QByteArray body = "abc"; qint64 length = -1; int status = 200; bool wait = false; QUrl redirect; };
class Reply : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, Response response, QObject *parent) : QNetworkReply(parent), response_(std::move(response)) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            if (isFinished()) return;
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response_.status);
            if (response_.length >= 0) setHeader(QNetworkRequest::ContentLengthHeader, response_.length);
            emit metaDataChanged(); if (isFinished()) return;
            if (!response_.redirect.isEmpty()) { emit redirected(response_.redirect); return; }
            if (response_.wait) return;
            ready_ = true; emit readyRead(); if (isFinished()) return;
            setFinished(true); emit finished();
        });
    }
    void abort() override { if (isFinished()) return; setError(OperationCanceledError, "Cancelled"); setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return (ready_ ? response_.body.size() - offset_ : 0) + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *buffer, qint64 max) override {
        if (!ready_) return 0;
        const auto n = qMin(max, qint64(response_.body.size()) - offset_); if (!n) return -1;
        std::memcpy(buffer, response_.body.constData() + offset_, size_t(n)); offset_ += n; return n;
    }
private:
    Response response_; qint64 offset_ = 0; bool ready_ = false;
};
class Network : public QNetworkAccessManager {
public:
    Response next; int requests = 0;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &r, QIODevice *) override { ++requests; return new Reply(r, next, this); }
};
}
class SourcesTests : public QObject {
    Q_OBJECT
private slots:
    void catalogAndCompatibility() {
        QVector<Entry> entries; QString error; QVERIFY(parseIndex(index(), entries, error)); QCOMPARE(entries.size(), 1);
        QVERIFY(compatibility(entries[0], "0.17.1", {"vpet"}).isEmpty());
        QVERIFY(!compatibility(entries[0], "0.16.0", {"vpet"}).isEmpty());
        auto meta = entries[0].metadata; QVERIFY(matches(entries[0], meta)); meta["id"] = "other"; QVERIFY(!matches(entries[0], meta));
        auto plugin = entry(); plugin["kind"] = "plugin"; plugin["pet"] = "cat";
        QVERIFY(parseIndex(index(plugin), entries, error)); QVERIFY(!compatibility(entries[0], "0.17.1", {"vpet"}).isEmpty());
        QVERIFY(compatibility(entries[0], "0.17.1", {"cat"}).isEmpty());
        auto future = entry(); future["schema"] = 3; QVERIFY(parseIndex(index(future), entries, error));
        QVERIFY(!compatibility(entries[0], "0.17.1", {"vpet"}).isEmpty());
    }
    void malformed_data() {
        QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value");
        QTest::newRow("http") << QString("download") << QJsonValue("http://example.test/cat");
        QTest::newRow("credentials") << QString("preview") << QJsonValue("https://user:pass@example.test/cat");
        QTest::newRow("file") << QString("download") << QJsonValue("file:///tmp/cat");
        QTest::newRow("oversize") << QString("bytes") << QJsonValue(double(MaxDownload + 1));
        QTest::newRow("fraction") << QString("bytes") << QJsonValue(3.5);
        QTest::newRow("missing") << QString("bytes") << QJsonValue();
        QTest::newRow("digest") << QString("sha256") << QJsonValue("abc");
        QTest::newRow("traversal") << QString("id") << QJsonValue("../cat");
        QTest::newRow("reserved") << QString("id") << QJsonValue("vpet");
        QTest::newRow("device") << QString("id") << QJsonValue("con");
        QTest::newRow("unknown") << QString("script") << QJsonValue("run.sh");
        QTest::newRow("bad-version") << QString("version") << QJsonValue("1.0.0-01");
    }
    void malformed() {
        QFETCH(QString, field); QFETCH(QJsonValue, value); auto o = entry(); o[field] = value;
        QVector<Entry> entries; QString error; QVERIFY(!parseIndex(index(o), entries, error)); QVERIFY(!error.isEmpty()); QVERIFY(entries.isEmpty());
    }
    void limitsAndDuplicates() {
        QVector<Entry> entries; QString error;
        QVERIFY(!parseIndex(QByteArray(MaxIndex + 1, 'x'), entries, error));
        QVERIFY(!parseIndex("{", entries, error));
        QVERIFY(!parseIndex(QJsonDocument(QJsonObject{{"schema_version", 1}, {"packs", QJsonArray{entry(), entry()}}}).toJson(), entries, error));
    }
    void versions() {
        QVERIFY(newerPackVersion("1.1.0", "1.0.9")); QVERIFY(!newerPackVersion("1.0.0", "2.0.0"));
        QVERIFY(newerPackVersion("1.0.0", "1.0.0-rc.1")); QVERIFY(!newerPackVersion("1.0.0-beta", "1.0.0"));
        QVERIFY(newerPackVersion("1.0.0-beta.11", "1.0.0-beta.2"));
        QVERIFY(newerPackVersion("1.0.0-beta-2", "1.0.0-beta-1"));
        QVERIFY(!newerPackVersion("1.0.0+new", "1.0.0+old"));
    }
    void sourcesAndOfflineCache() {
        QTemporaryDir dir; SourceStore store(dir.path()); QString error; const auto url = QString("https://example.test/index.json");
        QCOMPARE(store.urls(error), QStringList{officialSource()}); QVERIFY(store.add(url, error));
        QVERIFY(store.cache(url, index(), error)); QVERIFY(!store.cache(url, "malformed", error));
        SourceStore offline(dir.path()); QCOMPARE(offline.cached(url, error).size(), 1); QVERIFY(error.isEmpty());
        QVERIFY(!store.add("http://example.test", error)); QVERIFY(!store.remove(officialSource(), error));
        QVERIFY(store.remove(url, error)); QCOMPARE(store.urls(error), QStringList{officialSource()});
        // Removing or losing a source leaves its valid cache and all pack storage untouched.
        QCOMPARE(offline.cached(url, error).size(), 1);
    }
    void download_data() {
        QTest::addColumn<QByteArray>("body"); QTest::addColumn<int>("expected"); QTest::addColumn<int>("limit");
        QTest::addColumn<QString>("digest"); QTest::addColumn<bool>("success");
        const auto hash = QString::fromLatin1(QCryptographicHash::hash("abc", QCryptographicHash::Sha256).toHex());
        QTest::newRow("verified") << QByteArray("abc") << 3 << 4 << hash << true;
        QTest::newRow("unpinned") << QByteArray("abc") << -1 << 4 << QString() << true;
        QTest::newRow("bad-digest") << QByteArray("abc") << 3 << 4 << QString(64, 'a') << false;
        QTest::newRow("oversize-stream") << QByteArray("abcde") << -1 << 4 << QString() << false;
        QTest::newRow("oversize-expected") << QByteArray("abcd") << 3 << 4 << hash << false;
        QTest::newRow("truncated") << QByteArray("ab") << 3 << 4 << hash << false;
        QTest::newRow("empty") << QByteArray() << -1 << 4 << QString() << false;
    }
    void download() {
        QFETCH(QByteArray, body); QFETCH(int, expected); QFETCH(int, limit); QFETCH(QString, digest); QFETCH(bool, success);
        Network network; network.next.body = body; Download transfer(&network); QSignalSpy good(&transfer, &Download::completed), bad(&transfer, &Download::failed);
        QString temporary; QByteArray received;
        connect(&transfer, &Download::completed, this, [&](const QString &path) { temporary = path; QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly)); received = f.readAll(); });
        transfer.start(QUrl("https://example.test/pack"), limit, expected, digest);
        QTRY_COMPARE(good.size() + bad.size(), 1); QCOMPARE(good.size(), success ? 1 : 0); QVERIFY(!transfer.busy());
        if (success) { QCOMPARE(received, body); QVERIFY(!QFileInfo::exists(temporary)); }
    }
    void rejectedHeadersAndRedirects() {
        Network network; Download transfer(&network); QSignalSpy bad(&transfer, &Download::failed), good(&transfer, &Download::completed);
        network.next.length = 100; transfer.start(QUrl("https://example.test/pack"), 10); QTRY_COMPARE(bad.size(), 1);
        network.next = {}; network.next.redirect = QUrl("http://example.test/insecure"); network.next.status = 302;
        transfer.start(QUrl("https://example.test/pack"), 10); QTRY_COMPARE(bad.size(), 2);
        network.next = {}; network.next.status = 404;
        transfer.start(QUrl("https://example.test/pack"), 10); QTRY_COMPARE(bad.size(), 3); QCOMPARE(good.size(), 0);
    }
    void cancelAndRetry() {
        Network network; network.next.wait = true; Download transfer(&network); QSignalSpy good(&transfer, &Download::completed), bad(&transfer, &Download::failed);
        transfer.start(QUrl("https://example.test/pack"), 10); transfer.cancel(); QTRY_COMPARE(bad.size(), 1); QVERIFY(!transfer.busy());
        network.next = {}; transfer.start(QUrl("https://example.test/pack"), 10); QTRY_COMPARE(good.size(), 1);
        transfer.start(QUrl("file:///tmp/pack"), 10); QCOMPARE(bad.size(), 2); QCOMPARE(network.requests, 2);
    }
};
QTEST_GUILESS_MAIN(SourcesTests)
#include "sources_tests.moc"
