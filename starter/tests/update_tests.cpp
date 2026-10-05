#include "updates/release.h"
#include "updates/installer.h"
#include "updates/controller.h"
#include <QTest>
#include <QSignalSpy>
#include <memory>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QSysInfo>
#include <QDateTime>
#include <cstring>
using namespace pet::updates;
static QJsonObject readState(const QTemporaryDir &dir) {
    QFile f(dir.filePath("state.json")); return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject{};
}
static void write(const QString &path, const QByteArray &data) { QFile f(path); if (f.open(QIODevice::WriteOnly)) f.write(data); }
static QJsonObject releaseObject(QByteArray package = "package") {
    const QString name = "agent-pet-99.1.0-linux-" + QSysInfo::buildCpuArchitecture() + ".tar.gz";
    return {{"tag_name", "v99.1.0"}, {"draft", false}, {"prerelease", false}, {"assets", QJsonArray{QJsonObject{
        {"name", name}, {"state", "uploaded"}, {"size", package.size()},
        {"digest", "sha256:" + QString::fromLatin1(QCryptographicHash::hash(package, QCryptographicHash::Sha256).toHex())},
        {"browser_download_url", "https://github.com/WindyWin/vpet-agent-pet/releases/download/v99.1.0/" + name}}}}};
}
class Reply : public QNetworkReply {
    QByteArray data_; qint64 offset_ = 0;
public:
    Reply(const QNetworkRequest &request, QByteArray data, bool fail, QObject *parent) : QNetworkReply(parent), data_(data) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this, fail] {
            if (isFinished()) return;
            if (fail) setError(QNetworkReply::ConnectionRefusedError, "offline");
            else { setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); emit readyRead(); }
            if (!isFinished()) { setFinished(true); emit finished(); }
        });
    }
    void abort() override { if (!isFinished()) { setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished(); } }
    qint64 bytesAvailable() const override { return data_.size() - offset_ + QNetworkReply::bytesAvailable(); }
    qint64 readData(char *buffer, qint64 max) override { const qint64 n = qMin(max, data_.size() - offset_); if (!n) return -1; std::memcpy(buffer, data_.constData() + offset_, n); offset_ += n; return n; }
};
class Network : public QNetworkAccessManager {
public:
    int requests = 0; bool fail = false; QByteArray package = "package", download = "package";
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++requests;
        return new Reply(request, request.url().host() == "api.github.com" ? QJsonDocument(releaseObject(package)).toJson() : download, fail, this);
    }
};
class UpdateTests : public QObject {
    Q_OBJECT
private slots:
    void versions() {
        QVERIFY(newer("0.10.0", "0.9.9")); QVERIFY(!newer("0.6.0", "0.6.0"));
        QVERIFY(!newer("0.5.9", "0.6.0")); QVERIFY(!newer("1.0.0-beta", "0.6.0"));
        QVERIFY(!newer("01.0.0", "0.6.0")); QVERIFY(!newer("1.0", "0.6.0"));
    }
    void releaseValidation() {
        Release release; QString error; auto object = releaseObject();
        QVERIFY(parseRelease(object, QSysInfo::buildCpuArchitecture(), release, error)); QCOMPARE(release.version, "99.1.0");
        QVERIFY(!parseRelease(object, "wrong-arch", release, error));
        object["draft"] = true; QVERIFY(!parseRelease(object, QSysInfo::buildCpuArchitecture(), release, error));
        object = releaseObject(); auto asset = object["assets"].toArray().first().toObject();
        asset["browser_download_url"] = "https://example.com/payload.tar.gz"; object["assets"] = QJsonArray{asset};
        QVERIFY(!parseRelease(object, QSysInfo::buildCpuArchitecture(), release, error));
        object = releaseObject(); asset = object["assets"].toArray().first().toObject(); asset.remove("digest"); object["assets"] = QJsonArray{asset};
        QVERIFY(parseRelease(object, QSysInfo::buildCpuArchitecture(), release, error)); QVERIFY(release.digest.isEmpty());
    }
    void checksum() {
        QTemporaryDir dir; QString error; const QString path = dir.filePath("package"); write(path, "package");
        Release release; QVERIFY(parseRelease(releaseObject(), QSysInfo::buildCpuArchitecture(), release, error));
        QVERIFY(verifiedArchive(path, release.digest, error)); write(path, "tampered"); QVERIFY(!verifiedArchive(path, release.digest, error));
        QVERIFY(!verifiedArchive(path, "", error));
    }
    void crashRecovery() {
        QTemporaryDir dir; const auto prefix = dir.filePath("app"), stage = prefix + ".update-test";
        QDir().mkpath(prefix); QDir().mkpath(stage); write(prefix + "/version", "old"); write(stage + "/version", "new");
        write(stage + "/.agent-pet-update-id", "token");
        const QByteArray journal = QJsonDocument(QJsonObject{{"stage", stage}, {"token", "token"}}).toJson();
        write(prefix + ".update-transaction.json", journal); QString error;
        QVERIFY(exchangeDirectories(prefix, stage, error)); QVERIFY(recoverInstallation(prefix, error));
        QFile current(prefix + "/version"); QVERIFY(current.open(QIODevice::ReadOnly)); QCOMPARE(current.readAll(), "old"); current.close();
        QVERIFY(!QFile::exists(stage)); QVERIFY(!QFile::exists(prefix + ".update-transaction.json"));
        QDir().mkpath(stage); write(prefix + ".update-transaction.json", journal);
        QVERIFY(recoverInstallation(prefix, error)); QVERIFY(QFile::exists(prefix + "/version"));
    }
    void notificationCheckThrottleAndSkip() {
        QTemporaryDir dir; Network network; Controller controller(nullptr, &network, {}, dir.path());
        controller.check(true); QTRY_VERIFY(controller.indicator().contains("99.1.0")); QCOMPARE(network.requests, 1);
        controller.check(); QCOMPARE(network.requests, 1);
        std::unique_ptr<QWidget> settings(controller.settings(nullptr));
        for (auto *button : settings->findChildren<QPushButton *>()) if (button->text() == "Skip this version") button->click();
        QCOMPARE(controller.indicator(), "Updates…");
        Controller reloaded(nullptr, &network, {}, dir.path()); QCOMPARE(reloaded.indicator(), "Updates…");
        controller.check(true); QTRY_COMPARE(network.requests, 2); QTRY_VERIFY(controller.indicator().contains("99.1.0"));
    }
    void offlineAndCorruptDownload() {
        QTemporaryDir dir; Network network; network.fail = true;
        Controller controller(nullptr, &network, "/managed", dir.path());
        std::unique_ptr<QWidget> settings(controller.settings(nullptr)); controller.check(true);
        QTRY_VERIFY(([&] { for (auto *label : settings->findChildren<QLabel *>()) if (label->text().contains("Could not check")) return true; return false; })());
        network.fail = false; network.download = "corrupt"; controller.check(true); QTRY_VERIFY(controller.indicator().contains("99.1.0"));
        controller.download(); QTRY_COMPARE(network.requests, 3);
        QTRY_VERIFY(([&] { for (auto *label : settings->findChildren<QLabel *>()) if (label->text().contains("checksum")) return true; return false; })());
        QVERIFY(!QFile::exists(dir.filePath("pending.json"))); QVERIFY(!QFile::exists(dir.filePath("package.tar.gz")));
    }
    void downloadWriteFailureAndCancellation() {
        QTemporaryDir dir; Network network;
        Controller controller(nullptr, &network, "/managed", dir.path());
        std::unique_ptr<QWidget> settings(controller.settings(nullptr));
        controller.check(true); QTRY_VERIFY(controller.indicator().contains("99.1.0"));
        // A directory at the destination forces an I/O failure without filling
        // the developer's disk. No failed download may become installable.
        QDir().mkpath(dir.filePath("package.tar.gz"));
        controller.download();
        QTRY_VERIFY(([&] { for (auto *label : settings->findChildren<QLabel *>())
            if (label->text().contains("Cannot save") || label->text().contains("failed")) return true; return false; })());
        QVERIFY(!QFile::exists(dir.filePath("pending.json")));
        QVERIFY(QDir(dir.filePath("package.tar.gz")).removeRecursively());
        controller.download();
        for (auto *button : settings->findChildren<QPushButton *>()) if (button->text().startsWith("Cancel")) button->click();
        QTest::qWait(10);
        QVERIFY(!QFile::exists(dir.filePath("pending.json")));
        QVERIFY(!QFile::exists(dir.filePath("package.tar.gz")));
    }
    void automaticDownloadAndSessionGuard() {
        QTemporaryDir dir; Network network;
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 1}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, "/managed", dir.path()); controller.check(true);
        QTRY_VERIFY(QFile::exists(dir.filePath("pending.json"))); QCOMPARE(network.requests, 2);
        QSignalSpy restart(&controller, &Controller::restartRequested); controller.sessionsActive = [] { return true; };
        controller.install(); QCOMPARE(restart.size(), 0);
        QVERIFY(controller.indicator().contains("ready"));
    }
    void fullyAutomaticWaitsForIdleSessions() {
        QTemporaryDir dir; Network network;
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 3}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, "/managed", dir.path());
        bool busy = true; controller.sessionsActive = [&busy] { return busy; };
        controller.check(true);
        QTRY_VERIFY(QFile::exists(dir.filePath("pending.json")));
        // Active sessions must never be interrupted by an automatic restart.
        QTest::qWait(50); QVERIFY(!readState(dir).contains("autoInstalled"));
        // Once idle, the install is attempted exactly once per version.
        busy = false; controller.autoInstall();
        QCOMPARE(readState(dir)["autoInstalled"].toString(), QString("99.1.0"));
    }
};
QTEST_MAIN(UpdateTests)
#include "update_tests.moc"
