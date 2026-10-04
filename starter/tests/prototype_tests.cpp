#include "desktop/pet_window.h"
#include "desktop/session_playback.h"
#include <QApplication>
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
private slots:
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
};
QTEST_MAIN(PrototypeTests)
#include "prototype_tests.moc"
