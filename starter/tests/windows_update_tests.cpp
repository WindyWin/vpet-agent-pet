#include "updates/release.h"
#include "platform/contracts/update_layout.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>
using namespace pet::updates;
class WindowsUpdateTests : public QObject {
    Q_OBJECT
    static QJsonObject asset(const QString &name, const QString &digest = "sha256:" + QString(64, 'a')) {
        return {{"name", name}, {"state", "uploaded"}, {"size", 1234}, {"digest", digest},
                {"browser_download_url", "https://github.com/WindyWin/vpet-agent-pet/releases/download/v99.1.0/" + name}};
    }
    static QJsonObject release(const QJsonArray &assets) {
        return {{"tag_name", "v99.1.0"}, {"draft", false}, {"prerelease", false}, {"assets", assets}};
    }
private slots:
    void prefersSetupToPortableZip() {
        Release parsed; QString error;
        const QString setup = "agent-pet-99.1.0-windows-x86_64-setup.exe";
        QVERIFY(parseRelease(release({asset("agent-pet-99.1.0-windows-x86_64.zip"), asset(setup)}), "x86_64", parsed, error));
        QCOMPARE(parsed.download.fileName(), setup);
        QVERIFY(!parsed.digest.isEmpty());
        QVERIFY(parsed.componentsDownload.isEmpty());
    }
    void rejectsPortableOnlyAndWrongArchitecture() {
        Release parsed; QString error;
        QVERIFY(!parseRelease(release({asset("agent-pet-99.1.0-windows-x86_64.zip")}), "x86_64", parsed, error));
        QVERIFY(!parseRelease(release({asset("agent-pet-99.1.0-windows-arm64-setup.exe")}), "x86_64", parsed, error));
    }
    void unverifiedSetupAllowsNotificationsOnly() {
        Release parsed; QString error;
        QVERIFY(parseRelease(release({asset("agent-pet-99.1.0-windows-x86_64-setup.exe", "")}), "x86_64", parsed, error));
        QVERIFY(parsed.digest.isEmpty());
        QCOMPARE(parsed.version, QString("99.1.0"));
    }
    void rejectsForeignDownloadUrl() {
        auto foreign = asset("agent-pet-99.1.0-windows-x86_64-setup.exe");
        foreign["browser_download_url"] = "https://example.com/setup.exe";
        Release parsed; QString error;
        QVERIFY(!parseRelease(release({foreign}), "x86_64", parsed, error));
    }
};
QTEST_GUILESS_MAIN(WindowsUpdateTests)
#include "windows_update_tests.moc"
