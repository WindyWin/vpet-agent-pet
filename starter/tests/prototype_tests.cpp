#include "desktop/monitor.h"
#include "desktop/pet_window.h"
#include "desktop/session_playback.h"
#include "version.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QGroupBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
void finishSequence(pet::Player &player) {
    const int remaining = player.frameCount() - player.frameIndex();
    for (int i = 0; i < remaining; ++i) player.advance();
}
QJsonObject fixture(const QString &root) {
    QDir().mkpath(root + "/assets/vpet/vup");
    QImage image(16, 16, QImage::Format_ARGB32); image.fill(Qt::transparent);
    image.save(root + "/assets/vpet/vup/idle.png"); image.save(root + "/assets/vpet/vup/work.png");
    auto sequence = [](const QString &name) {
        return QJsonObject{{"path", name}, {"duration_ms", 25},
            {"frames", QJsonArray{QJsonObject{{"path", "assets/vpet/vup/" + name + ".png"}, {"duration_ms", 25}}}}};
    };
    return {{"schema_version", 1}, {"states", QJsonObject{{"idle", QJsonArray{"idle"}}, {"working", QJsonArray{"work"}}}},
            {"playback", QJsonObject{{"idle", QJsonObject{{"mode", "loop"}, {"after", "idle"}}},
                                     {"working", QJsonObject{{"mode", "loop"}, {"after", "idle"}}}}},
            {"sequences", QJsonArray{sequence("idle"), sequence("work")}}};
}
void writeCatalog(const QString &root, const QJsonObject &catalog) {
    QFile file(root + "/assets/vpet/animations.json");
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(catalog).toJson());
}
}
class PrototypeTests : public QObject {
    Q_OBJECT
    QTemporaryDir clients;
private slots:
    void initTestCase() {
        // Settings inspect integration files; keep them away from the real client configuration.
        qputenv("CLAUDE_CONFIG_DIR", QFile::encodeName(clients.path() + "/claude"));
        qputenv("CODEX_HOME", QFile::encodeName(clients.path() + "/codex"));
    }
    void releaseMetadataIsEmbedded() {
        // The About view and release packages rely on these embedded resources.
        QVERIFY(QString(AGENT_PET_VERSION).count('.') == 2);
        for (const auto *path : {":/LICENSE", ":/NOTICE", ":/THIRD_PARTY_NOTICES.md", ":/licenses/VPET-ARTWORK-TERMS.md"}) {
            QFile file(path);
            QVERIFY2(file.open(QIODevice::ReadOnly) && file.size() > 0, path);
        }
    }
    void sessionAnimationMapping() {
        pet::Player player;
        const QStringList states{"attention", "error", "turn-finished", "working", "reading", "thinking", "idle", "inactive"};
        for (const auto &state : states) {
            const auto animation = pet::sessionAnimation(state);
            QVERIFY2(player.select(animation, true), qPrintable(player.error()));
            QCOMPARE(player.state(), animation);
            player.beginDrag(); player.select(animation); player.endDrag();
            QCOMPARE(player.requestedState(), animation);
        }
    }

    void playbackUsesBundledTiming() {
        pet::Player player;
        QVERIFY(!player.pixmap().isNull()); QVERIFY(player.pixmap().hasAlphaChannel());
        QCOMPARE(player.frameDuration(), 250);
        QTRY_COMPARE_WITH_TIMEOUT(player.frameIndex(), 1, 450);
        QCOMPARE(player.frameDuration(), 125);
        player.setPaused(true); QVERIFY(player.select("thinking"));
        QCOMPARE(player.phase(), QString("start")); QCOMPARE(player.sequence(), QString("Think/Nomal/A"));
        finishSequence(player);
        QCOMPARE(player.phase(), QString("loop")); QCOMPARE(player.sequence(), QString("Think/Nomal/B"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B"));
        QVERIFY(player.select("idle")); QCOMPARE(player.sequence(), QString("Think/Nomal/C"));
        finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        QVERIFY(!player.select("unknown")); QCOMPARE(player.state(), QString("idle"));
    }
    void interruptionAndLatestRequest() {
        pet::Player player; player.setPaused(true);
        player.select("reading"); finishSequence(player); player.select("working");
        QCOMPARE(player.phase(), QString("end"));
        player.advance(); const int frame = player.frameIndex(); player.select("thinking");
        QCOMPARE(player.frameIndex(), frame);
        finishSequence(player); QCOMPARE(player.state(), QString("thinking"));
        player.select("needs_input", true); QCOMPARE(player.state(), QString("needs_input"));
        QCOMPARE(player.phase(), QString("start")); player.select("idle", true); QCOMPARE(player.state(), QString("idle"));
    }
    void oneShotsAndDragResume() {
        pet::Player player; player.setPaused(true); QSignalSpy complete(&player, &pet::Player::completed);
        player.select("starting", true); finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        QCOMPARE(complete.last().first().toString(), QString("starting"));
        player.select("working", true); finishSequence(player);
        player.select("tool_error", true); finishSequence(player); QCOMPARE(player.state(), QString("working"));
        player.beginDrag(); QCOMPARE(player.sequence(), QString("Raise/Raised_Static/A_Nomal"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("Raise/Raised_Dynamic/Nomal/1"));
        player.endDrag(); QCOMPARE(player.sequence(), QString("Raise/Raised_Static/C_Nomal"));
        finishSequence(player); QCOMPARE(player.state(), QString("working"));
        player.beginDrag(); player.select("needs_input", true); player.endDrag(); finishSequence(player);
        QCOMPARE(player.state(), QString("needs_input"));
        player.select("turn_finished", true); finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        player.select("closing", true); finishSequence(player); QVERIFY(player.stopped());
        QCOMPARE(complete.last().first().toString(), QString("closing"));
        player.select("idle", true); QVERIFY(!player.stopped());
    }
    void everyIncludedFrameAndCacheBound() {
        pet::Player player; player.setPaused(true); player.setRenderSize(640);
        QCOMPARE(player.states().size(), 11);
        auto checkSequence = [&] {
            const int count = player.frameCount();
            for (int i = 0; i < count; ++i) {
                QVERIFY2(!player.pixmap().isNull(), qPrintable(player.error()));
                QVERIFY(player.pixmap().width() <= 640); QVERIFY(player.pixmap().height() <= 640);
                QVERIFY(player.cacheKiB() <= pet::Player::cacheLimitKiB); player.advance();
            }
        };
        for (const auto &state : player.states()) {
            QVERIFY(player.select(state, true));
            checkSequence();
            if (player.state() == state && player.phase() == "loop" && state != "idle") {
                checkSequence(); player.select("idle"); QCOMPARE(player.phase(), QString("end"));
                checkSequence(); QCOMPARE(player.state(), QString("idle"));
            }
            QVERIFY2(player.error().isEmpty(), qPrintable(player.error()));
        }
        for (int i = 0; i < 50; ++i) {
            player.select(i % 2 ? "thinking" : "working", true);
            QVERIFY(player.cacheKiB() <= 1600);
        }
        player.setRenderSize(160); QVERIFY(player.cacheKiB() <= 100);
    }
    void brokenResourcesRecover() {
        QTemporaryDir directory; auto catalog = fixture(directory.path()); writeCatalog(directory.path(), catalog);
        pet::Player player(nullptr, directory.path()); player.setPaused(true); QVERIFY(player.valid());
        QFile broken(directory.path() + "/assets/vpet/vup/work.png");
        QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("not a PNG"); broken.close();
        QSignalSpy failed(&player, &pet::Player::failed); player.select("working", true);
        QCOMPARE(failed.size(), 1); QCOMPARE(player.state(), QString("idle")); QVERIFY(!player.pixmap().isNull());
        QVERIFY(QFile::remove(directory.path() + "/assets/vpet/vup/idle.png"));
        pet::Player noIdle(nullptr, directory.path()); QVERIFY(noIdle.stopped());
        QVERIFY(noIdle.pixmap().isNull()); QVERIFY(!noIdle.error().isEmpty());
    }
    void malformedCatalogs() {
        QTemporaryDir directory; pet::Player absent(nullptr, directory.path());
        QVERIFY(!absent.valid()); QVERIFY(!absent.error().isEmpty());
        auto catalog = fixture(directory.path()); auto sequences = catalog["sequences"].toArray();
        auto entry = sequences[0].toObject();
        entry["frames"] = QJsonArray{QJsonObject{{"path", "../escape.png"}, {"duration_ms", 25}}};
        sequences[0] = entry; catalog["sequences"] = sequences; writeCatalog(directory.path(), catalog);
        pet::Player traversal(nullptr, directory.path()); QVERIFY(!traversal.valid());
        catalog = fixture(directory.path()); sequences = catalog["sequences"].toArray();
        entry = sequences[0].toObject(); entry["duration_ms"] = -1; sequences[0] = entry;
        catalog["sequences"] = sequences; writeCatalog(directory.path(), catalog);
        pet::Player timing(nullptr, directory.path()); QVERIFY(!timing.valid());
        QFile file(directory.path() + "/assets/vpet/animations.json");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("invalid json"); file.close();
        pet::Player invalid(nullptr, directory.path()); QVERIFY(!invalid.valid());
    }
    void preferencesAndMonitorRecovery() {
        QTemporaryDir directory; const auto path = directory.path() + "/data/preferences.json";
        pet::PreferencesStore store(path); pet::Preferences prefs;
        prefs.size = 300; prefs.position = {-1800, 100}; prefs.onTop = false; QVERIFY(store.save(prefs));
        pet::PreferencesStore reloaded(path); const auto result = reloaded.load();
        QCOMPARE(result.size, 300); QCOMPARE(result.position, prefs.position); QVERIFY(!result.onTop);
        const QVector<QRect> dual{{0, 0, 1920, 1080}, {-1920, 0, 1920, 1080}};
        QCOMPARE(pet::Preferences::visiblePosition(prefs.position, {300, 300}, dual), prefs.position);
        const auto recovered = pet::Preferences::visiblePosition(prefs.position, {300, 300}, {dual.first()});
        QVERIFY(dual.first().contains(QRect(recovered, QSize(300, 300))));
        QCOMPARE(pet::Preferences::visiblePosition({1900, 1000}, {300, 300}, {dual.first()}), QPoint(1620, 780));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("broken"); file.close();
        pet::PreferencesStore corrupt(path); QCOMPARE(corrupt.load().size, 240); QVERIFY(!corrupt.save(prefs));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("broken"));
    }
    void controlsPersistAndDialogsKeepRunning() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json"; QPoint saved;
        {
            pet::PetWindow window(nullptr, path); window.show();
            QVERIFY(window.testAttribute(Qt::WA_TranslucentBackground));
            QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
            window.setPetSize(300); window.setOnTop(false); window.setClickThrough(true);
            QVERIFY(window.windowFlags().testFlag(Qt::WindowTransparentForInput));
            window.recover(); QVERIFY(!window.clickThrough()); saved = window.pos(); QVERIFY(window.savePreferences());
            window.showSettings(); window.showPreview(); window.player().setPaused(true);
            for (auto *dialog : window.findChildren<QDialog*>()) dialog->close();
            QVERIFY(!window.player().paused()); QVERIFY(window.isVisible());
        }
        pet::PetWindow restored(nullptr, path); QCOMPARE(restored.width(), 300); QCOMPARE(restored.pos(), saved);
        QVERIFY(!restored.windowFlags().testFlag(Qt::WindowStaysOnTopHint)); QVERIFY(!restored.clickThrough());
        restored.setPetSize(1); QCOMPARE(restored.width(), 160);
    }
    void notificationPreferences() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        QFile legacy(path); QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(R"({"version":1,"size":200,"on_top":true,"x":10,"y":20})"); legacy.close();
        pet::PreferencesStore store(path); auto prefs = store.load();
        QCOMPARE(prefs.size, 200); QVERIFY(!prefs.muted); QVERIFY(!prefs.sound);
        prefs.muted = true; prefs.sound = true; QVERIFY(store.save(prefs));
        const auto reloaded = pet::PreferencesStore(path).load(); QVERIFY(reloaded.muted); QVERIFY(reloaded.sound);
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(R"({"version":1,"size":200,"on_top":true,"x":10,"y":20,"muted":"yes"})"); legacy.close();
        pet::PreferencesStore invalid(path); QVERIFY(!invalid.load().muted); QVERIFY(!invalid.save(prefs));
    }
    void focusSessionListAndQuietHosts() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window);
        QStringList focused, looking;
        monitor.bringForward = [&](const pet::Session &s) { focused << s.id; return s.host != "terminal"; };
        monitor.hostActive = [&](const pet::Session &s) { return looking.contains(s.id); };
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString session, QString kind, QString host = "konsole", QString reason = {}) {
            ++seq; pet::Event e{"claude", session, QString::number(seq), kind, {}, {}, "/work/" + session, {}, now + seq, reason};
            e.host = host; e.hostPids = "4242"; return e;
        };
        // Clicking the bubble brings the session forward and clears the bubble, not the request.
        QVERIFY(monitor.apply(event("web", "attention", "konsole", "approval"), now + seq));
        QVERIFY(monitor.bubble().isVisible());
        emit monitor.bubble().focusRequested();
        QCOMPARE(focused, QStringList{"web"}); QVERIFY(!monitor.bubble().isVisible()); QCOMPARE(window.attention(), 1);
        // No bubble for the window the user is already looking at.
        looking << "api";
        QVERIFY(monitor.apply(event("api", "error", "vscode"), now + seq));
        QVERIFY(!monitor.bubble().isVisible());
        QVERIFY(monitor.apply(event("cli", "error", "terminal"), now + seq));
        QVERIFY(monitor.bubble().isVisible());
        QVERIFY(!monitor.focusSession(monitor.queue().current()->session)); // Unfindable window: stays.
        QVERIFY(monitor.bubble().isVisible());
        // A plain click on the pet opens the session list; most urgent first.
        QSignalSpy clicks(&window, &pet::PetWindow::sessionsRequested);
        QTest::mousePress(&window, Qt::LeftButton, {}, window.rect().center());
        QTest::mouseRelease(&window, Qt::LeftButton, {}, window.rect().center());
        QTRY_COMPARE(clicks.size(), 1);
        QVERIFY(monitor.sessionList().isVisible());
        QCOMPARE(monitor.sessionList().rowCount(), 3);
        QVERIFY(monitor.sessionList().rowText(0).startsWith("web — Needs approval\nClaude Code · web · Konsole"));
        QVERIFY(monitor.sessionList().rowText(1).contains(" — Tool error"));
        focused.clear(); monitor.sessionList().activateRow(0);
        QCOMPARE(focused, QStringList{"web"}); QVERIFY(!monitor.sessionList().isVisible());
        monitor.toggleSessions(); QVERIFY(monitor.sessionList().isVisible());
        monitor.toggleSessions(); QVERIFY(!monitor.sessionList().isVisible());
        // Reports fade on their own; requests stay.
        monitor.update(now + seq + pet::Sessions::errorAlertMs + 1000);
        QVERIFY(!monitor.bubble().isVisible()); QCOMPARE(window.attention(), 1);
        window.requestQuit(); QVERIFY(!monitor.sessionList().isVisible());
    }
    void bubblePlacementNearEdges() {
        const QRect screen(0, 0, 1920, 1080); const QSize bubble(300, 90);
        auto inside = [&](QPoint p) { return screen.contains(QRect(p, bubble)); };
        const QRect middle(800, 400, 240, 240), right(1680, 400, 240, 240), corner(1680, 0, 240, 240);
        QCOMPARE(pet::AlertBubble::placement(middle, bubble, screen), QPoint(1048, 448));
        const auto left = pet::AlertBubble::placement(right, bubble, screen);
        QCOMPARE(left, QPoint(1372, 448)); QVERIFY(inside(left));
        QVERIFY(inside(pet::AlertBubble::placement(corner, bubble, screen)));
        const QRect wide(0, 400, 1900, 240); // No side fits: above.
        QCOMPARE(pet::AlertBubble::placement(wide, bubble, screen).y(), 400 - 8 - 90);
        QVERIFY(inside(pet::AlertBubble::placement({-500, -500, 240, 240}, bubble, screen)));
    }
    void monitorAlertsBadgeAndQuit() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window);
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString session, QString kind, QString reason = {}) {
            ++seq; return pet::Event{"claude", session, QString::number(seq), kind, {}, {}, "/work/abc-web", {}, now + seq, reason};
        };
        // Default bubbles: requests and errors. A finished turn only animates the pet.
        QVERIFY(monitor.apply(event("b72c9", "turn_finished"), now + seq));
        QVERIFY(!monitor.bubble().isVisible());
        window.setBubbles(pet::Preferences::AllAlerts);
        QVERIFY(monitor.bubble().isVisible()); QCOMPARE(monitor.bubble().title(), "Turn finished");
        QVERIFY(monitor.apply(event("a1b2", "attention", "approval"), now + seq));
        QCOMPARE(monitor.bubble().title(), "Needs approval");
        QCOMPARE(monitor.bubble().label(), "abc-web · Claude Code · a1b2");
        QCOMPARE(monitor.bubble().footer(), "+1"); QCOMPARE(window.attention(), 1);
        QCOMPARE(window.player().requestedState(), "needs_input");
        // The bubble is one compact line beside the character, without covering it.
        QVERIFY(!monitor.bubble().geometry().intersects(window.figure()));
        QVERIFY(monitor.bubble().height() < 48);

        window.setMuted(true); QVERIFY(!monitor.bubble().isVisible()); QCOMPARE(window.attention(), 1);
        window.setMuted(false); QVERIFY(monitor.bubble().isVisible());
        emit monitor.bubble().dismissRequested();
        QCOMPARE(monitor.bubble().title(), "Turn finished"); QCOMPARE(monitor.bubble().footer(), QString());
        QCOMPARE(window.attention(), 1); // Dismissal never resolves the request.
        emit monitor.bubble().dismissRequested(); QVERIFY(!monitor.bubble().isVisible());
        QCOMPARE(monitor.sessions().aggregate(now), "attention");
        window.setBubbles(pet::Preferences::RequestsAndErrors);

        window.showSettings();
        auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
        auto *integrations = dialog->findChild<QGroupBox*>(); QVERIFY(integrations); // Setup and coverage.
        int notEnabled = 0;
        for (auto *label : integrations->findChildren<QLabel*>()) notEnabled += label->text().startsWith("Not enabled\n");
        QCOMPARE(notEnabled, 2);
        auto boxes = dialog->findChildren<QCheckBox*>(); QVERIFY(boxes.size() >= 3);
        QVERIFY(!dialog->findChildren<QComboBox*>().isEmpty()); // Bubble level.
        dialog->close(); QCoreApplication::processEvents();
        QVERIFY(monitor.active());
        QVERIFY(monitor.apply(event("a1b2", "prompt"), now + seq)); QCOMPARE(window.attention(), 0);
        QVERIFY(monitor.apply(event("c3d4", "error"), now + seq)); QVERIFY(monitor.bubble().isVisible());

        window.requestQuit();
        QVERIFY(!monitor.active()); QVERIFY(!monitor.bubble().isVisible());
        QVERIFY(!monitor.apply(event("c3d4", "attention"), now + seq));
    }
};
QTEST_MAIN(PrototypeTests)
#include "prototype_tests.moc"
