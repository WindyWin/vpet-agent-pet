#include "interactions/bridge_launcher.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using pet::interactions::BridgeLauncher;

class BridgeLauncherTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString fakeNode(const QString &body) {
        const auto path = dir.filePath("node");
        QFile file(path);
        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        file.write("#!/bin/sh\n" + body.toUtf8() + "\n");
        file.close();
        file.setPermissions(QFile::permissions(path) | QFile::ExeOwner);
        return path;
    }
    QString folder() {
        const auto bridges = dir.filePath("bridges");
        QDir().mkpath(bridges);
        for (const auto &name : BridgeLauncher::scripts()) { QFile f(QDir(bridges).filePath(name)); f.open(QIODevice::WriteOnly); }
        return bridges;
    }
    int lines(const QString &name) {
        QFile file(dir.filePath(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
    }
private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }
    void folderIsFoundBesideTheApplication() {
        QTemporaryDir app;
        QDir(app.path()).mkpath("bin");
        QDir(app.path()).mkpath("share/agent-pet/bridges");
        QFile f(app.filePath("share/agent-pet/bridges/opencode-bridge.mjs")); f.open(QIODevice::WriteOnly); f.close();
        QCOMPARE(BridgeLauncher::bridgeFolder(app.filePath("bin")), QDir::cleanPath(app.filePath("share/agent-pet/bridges")));
        QVERIFY(BridgeLauncher::bridgeFolder(QDir::tempPath() + "/agent-pet-no-such-folder").isEmpty());
    }
    void withoutNodeNothingStarts() {
        BridgeLauncher launcher;
        launcher.node = "/nonexistent/node"; launcher.folder = folder();
        QVERIFY(!launcher.start());
        QVERIFY(!launcher.running());
        QVERIFY(!launcher.status().isEmpty());
    }
    void startsEachBridgeWithTheEndpointAndStopsThem() {
        QFile::remove(dir.filePath("started"));
        BridgeLauncher launcher;
        launcher.node = fakeNode("echo \"$@\" >> '" + dir.filePath("started") + "'\nexec sleep 30");
        launcher.folder = folder(); launcher.endpoint = "/tmp/pet-endpoint";
        QVERIFY(launcher.start());
        QTRY_COMPARE(lines("started"), BridgeLauncher::scripts().size());
        QFile file(dir.filePath("started")); file.open(QIODevice::ReadOnly);
        const auto text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains("opencode-bridge.mjs --endpoint /tmp/pet-endpoint"));
        QVERIFY(launcher.running());
        launcher.stop();
        QVERIFY(!launcher.running());
    }
    void anExitedBridgeIsStartedAgain() {
        QFile::remove(dir.filePath("runs"));
        BridgeLauncher launcher;
        launcher.node = fakeNode("echo run >> '" + dir.filePath("runs") + "'");
        launcher.folder = folder(); launcher.retryMs = 50;
        QVERIFY(launcher.start());
        QTRY_VERIFY_WITH_TIMEOUT(lines("runs") >= 2 * BridgeLauncher::scripts().size() + 1, 5000);
        launcher.stop();
        const int after = lines("runs");
        QTest::qWait(300);
        QCOMPARE(lines("runs"), after);
    }
};
QTEST_MAIN(BridgeLauncherTests)
#include "bridge_launcher_tests.moc"
