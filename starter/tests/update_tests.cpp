#include "updates/release.h"
#include "updates/installer.h"
#include "updates/controller.h"
#include "updates/components.h"
#include "sessions/state.h"
#include "i18n/language.h"
#include <QTest>
#include <QDialog>
#include <QScopeGuard>
#include <QSignalSpy>
#include <memory>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QProgressBar>
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
            else {
                setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); emit readyRead();
                emit downloadProgress(data_.size() / 2, data_.size()); emit downloadProgress(data_.size(), data_.size());
            }
            if (!isFinished()) { setFinished(true); emit finished(); }
        });
    }
    void abort() override { if (!isFinished()) { setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished(); } }
    qint64 bytesAvailable() const override { return data_.size() - offset_ + QNetworkReply::bytesAvailable(); }
    qint64 readData(char *buffer, qint64 max) override { const qint64 n = qMin(max, data_.size() - offset_); if (!n) return -1; std::memcpy(buffer, data_.constData() + offset_, n); offset_ += n; return n; }
};
static QString digest(const QByteArray &bytes) {
    return "sha256:" + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
static QJsonObject componentManifest() {
    QJsonArray components;
    const QMap<QString, QStringList> groups{
        {"app", {"bin/agent-pet", "bin/agent-pet-updater"}},
        {"runtime", {"lib/libtest.so"}}, {"artwork", {"share/agent-pet/artwork.rcc"}}};
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QJsonArray files;
        for (const auto &path : it.value())
            files.append(QJsonObject{{"path", path}, {"size", 3}, {"digest", digest("new")}, {"executable", false}});
        components.append(QJsonObject{{"name", it.key()}, {"files", files}, {"size", 7}, {"digest", digest("package")},
            {"archive", "agent-pet-99.1.0-linux-" + QSysInfo::buildCpuArchitecture() + '-' + it.key() + ".tar.gz"}});
    }
    return {{"format", 1}, {"version", "99.1.0"}, {"architecture", QSysInfo::buildCpuArchitecture()}, {"components", components}};
}
static QJsonObject withComponents(QJsonObject release, const QByteArray &manifest) {
    const QString name = "agent-pet-99.1.0-linux-" + QSysInfo::buildCpuArchitecture() + "-components.json";
    auto assets = release["assets"].toArray();
    assets.append(QJsonObject{{"name", name}, {"state", "uploaded"}, {"size", manifest.size()}, {"digest", digest(manifest)},
        {"browser_download_url", "https://github.com/WindyWin/vpet-agent-pet/releases/download/v99.1.0/" + name}});
    release["assets"] = assets; return release;
}
static void installFiles(const QString &prefix, const QJsonObject &manifest) {
    for (const auto &component : manifest["components"].toArray()) {
        for (const auto &value : component.toObject()["files"].toArray()) {
            const QString path = prefix + '/' + value.toObject()["path"].toString();
            QDir().mkpath(QFileInfo(path).absolutePath()); write(path, "new");
        }
    }
}
class Network : public QNetworkAccessManager {
public:
    int requests = 0; bool fail = false; QByteArray package = "package", download = "package";
    QByteArray manifest; QStringList urls; QString failSuffix;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++requests; urls.append(request.url().fileName());
        auto release = releaseObject(package);
        if (!manifest.isEmpty()) release = withComponents(release, manifest);
        const auto bytes = request.url().host() == "api.github.com" ? QJsonDocument(release).toJson()
            : request.url().fileName().endsWith("-components.json") ? manifest : download;
        return new Reply(request, bytes, fail || (!failSuffix.isEmpty() && request.url().fileName().endsWith(failSuffix)), this);
    }
};
// Collects what the dialog shows after every change: progress, status text and whether the bar is visible.
struct Shown { QList<int> percents; QStringList texts; QList<bool> bars; };
static void follow(Controller &controller, QWidget *settings, Shown &shown) {
    QObject::connect(&controller, &Controller::changed, settings, [&controller, settings, &shown] {
        shown.percents << controller.progress();
        for (auto *label : settings->findChildren<QLabel *>()) if (label->text().startsWith("Downloading")) shown.texts << label->text();
        shown.bars << !settings->findChild<QProgressBar *>()->isHidden();
    });
}
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
    void componentMetadataAndValidation() {
        QTemporaryDir dir; QString error; Components components;
        const auto object = componentManifest();
        const auto bytes = QJsonDocument(object).toJson();
        const auto path = dir.filePath("components.json"); write(path, bytes);
        QVERIFY(readComponents(path, digest(bytes), "99.1.0", QSysInfo::buildCpuArchitecture(), components, error));
        QVERIFY(!readComponents(path, digest(bytes), "99.2.0", QSysInfo::buildCpuArchitecture(), components, error));
        QVERIFY(!readComponents(path, digest(bytes), "99.1.0", "wrong-arch", components, error));
        QVERIFY(!readComponents(path, digest("tampered"), "99.1.0", QSysInfo::buildCpuArchitecture(), components, error));
        for (const auto &unsafe : {"../escape", "/absolute", "bin/../escape", ".agent-pet-install", "bin/agent-pet", "bin", "bin//empty"}) {
            auto malformed = object;
            auto entries = malformed["components"].toArray(); auto entry = entries[0].toObject();
            auto files = entry["files"].toArray(); auto content = files[0].toObject(); content["path"] = unsafe;
            files.append(content); entry["files"] = files; entries[0] = entry; malformed["components"] = entries;
            const auto invalid = QJsonDocument(malformed).toJson(); write(path, invalid);
            QVERIFY2(!readComponents(path, digest(invalid), "99.1.0", QSysInfo::buildCpuArchitecture(), components, error), unsafe);
        }
        Release release;
        auto metadata = withComponents(releaseObject(), bytes);
        QVERIFY(parseRelease(metadata, QSysInfo::buildCpuArchitecture(), release, error));
        QCOMPARE(release.componentsDigest, digest(bytes));
        auto assets = metadata["assets"].toArray(); auto manifestAsset = assets.last().toObject();
        manifestAsset["browser_download_url"] = "https://example.com/components.json";
        assets[assets.size() - 1] = manifestAsset; metadata["assets"] = assets;
        QVERIFY(parseRelease(metadata, QSysInfo::buildCpuArchitecture(), release, error));
        QVERIFY(release.componentsDownload.isEmpty()); // Full archive remains usable by old/new clients.
    }
    void downloadsOnlyChangedComponents() {
        QTemporaryDir dir, installed; Network network;
        const auto manifest = componentManifest(); network.manifest = QJsonDocument(manifest).toJson();
        installFiles(installed.path(), manifest);
        write(installed.filePath("bin/agent-pet"), "old");
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 1}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, installed.path(), dir.path()); controller.check(true);
        QTRY_VERIFY(QFile::exists(dir.filePath("pending.json")));
        QCOMPARE(network.requests, 3); // Release metadata, target manifest, app only.
        QVERIFY(network.urls.last().endsWith("-app.tar.gz"));
        QVERIFY(!network.urls.contains("agent-pet-99.1.0-linux-" + QSysInfo::buildCpuArchitecture() + ".tar.gz"));
        QVERIFY(!QFile::exists(dir.filePath("package.tar.gz")));
        Controller reloaded(nullptr, &network, installed.path(), dir.path());
        QVERIFY(reloaded.indicator().contains("ready"));
        // A damaged reused component invalidates readiness after restart.
        write(installed.filePath("share/agent-pet/artwork.rcc"), "bad");
        Controller damaged(nullptr, &network, installed.path(), dir.path());
        QVERIFY(!damaged.indicator().contains("ready"));
        damaged.download(); QTRY_VERIFY(damaged.indicator().contains("ready"));
        QCOMPARE(network.requests, 5); // Cached app survives; only manifest + artwork fetched.
        QVERIFY(network.urls.last().endsWith("-artwork.tar.gz"));
    }
    void downloadsOnlyChangedArtworkPack() {
        QTemporaryDir dir, installed; Network network;
        auto manifest = componentManifest();
        manifest["format"] = 2;
        auto entries = manifest["components"].toArray();
        const QString changed = "artwork-" + QString(64, 'b');
        for (const auto &name : {QString("artwork-") + QString(64, 'a'), changed}) {
            auto entry = entries.last().toObject();
            entry["name"] = name;
            entry["archive"] = "agent-pet-99.1.0-linux-" + QSysInfo::buildCpuArchitecture() + '-' + name + ".tar.gz";
            auto file = entry["files"].toArray().first().toObject();
            file["path"] = "share/agent-pet/" + name + ".rcc";
            entry["files"] = QJsonArray{file}; entries.append(entry);
        }
        manifest["components"] = entries;
        network.manifest = QJsonDocument(manifest).toJson();
        installFiles(installed.path(), manifest);
        write(installed.filePath("share/agent-pet/" + changed + ".rcc"), "old");
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 1}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, installed.path(), dir.path()); controller.check(true);
        QTRY_VERIFY(QFile::exists(dir.filePath("pending.json")));
        QCOMPARE(network.requests, 3); // Metadata, manifest, and exactly one changed sequence.
        QVERIFY(network.urls.last().endsWith('-' + changed + ".tar.gz"));
        QVERIFY(!QFile::exists(dir.filePath("package.tar.gz")));
        // Duplicate components must be rejected in the variable-length format too.
        entries[entries.size() - 1] = entries.first().toObject(); manifest["components"] = entries;
        const auto bytes = QJsonDocument(manifest).toJson();
        const auto path = dir.filePath("invalid.json"); write(path, bytes);
        Components components; QString error;
        QVERIFY(!readComponents(path, digest(bytes), "99.1.0", QSysInfo::buildCpuArchitecture(), components, error));
    }
    void componentDownloadCancellationAndRetry() {
        QTemporaryDir dir, installed; Network network;
        const auto manifest = componentManifest(); network.manifest = QJsonDocument(manifest).toJson();
        installFiles(installed.path(), manifest); write(installed.filePath("bin/agent-pet"), "old");
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 0}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, installed.path(), dir.path());
        std::unique_ptr<QWidget> settings(controller.settings(nullptr));
        controller.check(true); QTRY_VERIFY(controller.indicator().contains("99.1.0"));
        controller.download();
        for (auto *button : settings->findChildren<QPushButton *>()) if (button->text().startsWith("Cancel")) button->click();
        QTest::qWait(10); QVERIFY(!QFile::exists(dir.filePath("pending.json")));
        network.download = "corrupt"; controller.download();
        QTRY_VERIFY(([&] { for (auto *label : settings->findChildren<QLabel *>()) if (label->text().contains("checksum")) return true; return false; })());
        QVERIFY(!QFile::exists(dir.filePath("pending.json")));
        network.download = "package"; controller.download(); QTRY_VERIFY(controller.indicator().contains("ready"));
    }
    void componentFailureFallsBackToFullPackage() {
        for (const bool unsupported : {false, true}) {
            QTemporaryDir dir, installed; Network network;
            auto manifest = componentManifest();
            installFiles(installed.path(), manifest); write(installed.filePath("bin/agent-pet"), "old");
            if (unsupported) manifest["format"] = 99;
            else network.failSuffix = "-app.tar.gz";
            network.manifest = QJsonDocument(manifest).toJson();
            write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 1}, {"enabled", true}}).toJson());
            Controller controller(nullptr, &network, installed.path(), dir.path()); controller.check(true);
            QTRY_VERIFY(controller.indicator().contains("ready"));
            QVERIFY(QFile::exists(dir.filePath("package.tar.gz")));
            QCOMPARE(network.urls.last(), "agent-pet-99.1.0-linux-" + QSysInfo::buildCpuArchitecture() + ".tar.gz");
            QFile pending(dir.filePath("pending.json")); QVERIFY(pending.open(QIODevice::ReadOnly));
            QVERIFY(!QJsonDocument::fromJson(pending.readAll()).object().contains("kind"));
            QVERIFY(!QFile::exists(dir.filePath("components.json")));
        }
    }
    void progressSpansAllComponents() {
        QTemporaryDir dir, installed; Network network;
        const auto manifest = componentManifest(); network.manifest = QJsonDocument(manifest).toJson();
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 1}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, installed.path(), dir.path());
        std::unique_ptr<QWidget> settings(controller.settings(nullptr));
        Shown shown; follow(controller, settings.get(), shown);
        controller.check(true);
        QTRY_VERIFY(QFile::exists(dir.filePath("pending.json")));
        int last = 0;
        for (const int percent : shown.percents) if (percent >= 0) { QVERIFY2(percent >= last, "progress went backwards"); last = percent; }
        QCOMPARE(last, 100);
        QVERIFY(shown.percents.contains(-1)); // The manifest is fetched before the total is known.
        for (const auto &text : {"file 1 of 3", "file 2 of 3", "file 3 of 3"})
            QVERIFY2(shown.texts.join('\n').contains(text), text);
        QVERIFY(shown.bars.contains(true));
        QVERIFY(!shown.bars.last()); // Hidden again once the download is done.
        QCOMPARE(controller.progress(), -1);
    }
    void progressCountsOnlyMissingComponents() {
        QTemporaryDir dir, installed; Network network;
        const auto manifest = componentManifest(); network.manifest = QJsonDocument(manifest).toJson();
        installFiles(installed.path(), manifest); write(installed.filePath("bin/agent-pet"), "old");
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 1}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, installed.path(), dir.path());
        std::unique_ptr<QWidget> settings(controller.settings(nullptr));
        Shown shown; follow(controller, settings.get(), shown);
        controller.check(true);
        QTRY_VERIFY(QFile::exists(dir.filePath("pending.json")));
        const auto text = shown.texts.join('\n');
        QVERIFY(!text.contains("file")); // One component left: a plain percentage.
        QVERIFY(text.contains("100%"));
    }
    void progressRestartsForFullPackageFallback() {
        QTemporaryDir dir, installed; Network network;
        const auto manifest = componentManifest(); network.manifest = QJsonDocument(manifest).toJson();
        network.failSuffix = "-runtime.tar.gz";
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 1}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, installed.path(), dir.path());
        std::unique_ptr<QWidget> settings(controller.settings(nullptr));
        Shown shown; follow(controller, settings.get(), shown);
        controller.check(true);
        QTRY_VERIFY(QFile::exists(dir.filePath("pending.json")));
        QVERIFY(shown.texts.join('\n').contains("Downloading full update"));
        QCOMPARE(shown.percents.last() , -1);
    }
    void componentScanCanBeCancelled() {
        QTemporaryDir dir, installed; Network network;
        const auto manifest = componentManifest(); network.manifest = QJsonDocument(manifest).toJson();
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 0}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, installed.path(), dir.path());
        std::unique_ptr<QWidget> settings(controller.settings(nullptr));
        controller.check(true); QTRY_VERIFY(controller.indicator().contains("99.1.0"));
        QPushButton *cancel = nullptr;
        for (auto *button : settings->findChildren<QPushButton *>()) if (button->text().startsWith("Cancel")) cancel = button;
        QVERIFY(cancel);
        bool clicked = false;
        // Click Cancel on the next event-loop turn after the scan starts, while it is still checking files.
        connect(&controller, &Controller::changed, settings.get(), [&] {
            for (auto *label : settings->findChildren<QLabel *>())
                if (!clicked && label->text() == "Checking installed files…") {
                    clicked = true; QVERIFY(cancel->isEnabled()); QTimer::singleShot(0, cancel, &QPushButton::click);
                }
        });
        const int before = network.requests;
        controller.download();
        QTRY_VERIFY(clicked); QTest::qWait(50);
        QCOMPARE(network.requests, before + 1); // The manifest only: no component was fetched.
        QVERIFY(!QFile::exists(dir.filePath("pending.json")));
        QCOMPARE(controller.progress(), -1);
        QVERIFY(settings->findChild<QProgressBar *>()->isHidden());
        QVERIFY(!cancel->isEnabled());
        controller.download(); QTRY_VERIFY(controller.indicator().contains("ready")); // Retrying still works.
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
    void dialogFollowsLanguage() {
        const auto english = qScopeGuard([] { pet::i18n::install(pet::i18n::Language::English); });
        QTemporaryDir dir; Network network; Controller controller(nullptr, &network, {}, dir.path());
        QWidget parent; controller.showSettings(&parent);
        const auto titled = [&parent](const QString &title) {
            for (auto *dialog : parent.findChildren<QDialog *>()) if (dialog->isVisible() && dialog->windowTitle() == title) return true;
            return false;
        };
        QVERIFY(titled("Agent Pet updates"));
        QVERIFY(pet::i18n::install(pet::i18n::Language::Vietnamese));
        controller.retranslate(&parent);
        QTRY_VERIFY(titled("Cập nhật Agent Pet")); QVERIFY(!titled("Agent Pet updates"));
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
    void automaticDownloadAndCheckpointFailure() {
        QTemporaryDir dir; Network network;
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 1}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, "/managed", dir.path()); controller.check(true);
        QTRY_VERIFY(QFile::exists(dir.filePath("pending.json"))); QCOMPARE(network.requests, 2);
        QSignalSpy restart(&controller, &Controller::restartRequested); controller.prepareRestart = [] { return false; };
        controller.install(); QCOMPARE(restart.size(), 0);
        QVERIFY(controller.indicator().contains("ready"));
    }
    void automaticRestartPreservesPendingRequest() {
        QTemporaryDir dir; Network network;
        const auto prefix = dir.filePath("install");
        QVERIFY(QDir().mkpath(prefix + "/bin"));
        const auto helper = prefix + "/bin/agent-pet-updater";
        write(helper, "#!/bin/sh\nexit 0\n");
        QVERIFY(QFile::setPermissions(helper, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 3}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, prefix, dir.path());
        pet::Sessions sessions;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        pet::Event request{"claude", "session", "approval", "attention"}; request.timestamp = now;
        QVERIFY(sessions.apply(request, now));
        QByteArray checkpoint;
        controller.prepareRestart = [&] { checkpoint = sessions.checkpoint(); return true; };
        QSignalSpy restart(&controller, &Controller::restartRequested);
        controller.check(true);
        QTRY_COMPARE(restart.size(), 1);
        pet::Sessions restored;
        QVERIFY(restored.restore(checkpoint, now + 1000, [](const pet::Session &) { return true; }));
        QCOMPARE(restored.unresolvedAttention(), 1);
        QCOMPARE(restored.pending().size(), 1);
        controller.autoInstall(); QCOMPARE(restart.size(), 1);
    }
    void fullyAutomaticRequiresCheckpoint() {
        QTemporaryDir dir; Network network;
        write(dir.filePath("state.json"), QJsonDocument(QJsonObject{{"format", 1}, {"mode", 3}, {"enabled", true}}).toJson());
        Controller controller(nullptr, &network, "/managed", dir.path());
        bool saved = false; controller.prepareRestart = [&saved] { return saved; };
        controller.check(true);
        QTRY_VERIFY(QFile::exists(dir.filePath("pending.json")));
        // A failed checkpoint must prevent a restart.
        QTest::qWait(50); QVERIFY(!readState(dir).contains("autoInstalled"));
        // Once saved, the install is attempted exactly once per version.
        saved = true; controller.autoInstall();
        QCOMPARE(readState(dir)["autoInstalled"].toString(), QString("99.1.0"));
    }
};
QTEST_MAIN(UpdateTests)
#include "update_tests.moc"
