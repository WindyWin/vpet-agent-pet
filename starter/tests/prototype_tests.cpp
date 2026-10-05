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
#include <memory>

namespace {
// Scripted random draws; one that is missing counts as unexpected, so a test also pins how many are made.
struct Draws {
    QList<int> values; int unexpected = 0;
    pet::Random random() {
        return [this](int bound) {
            if (values.isEmpty()) { ++unexpected; return 0; }
            return qMin(values.takeFirst(), bound - 1);
        };
    }
};
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
// Plays a fidget or any other state until the pet is back at idle.
void playOut(pet::Player &player) {
    for (int i = 0; i < 12 && player.state() != "idle"; ++i) finishSequence(player);
}
bool loads(const QJsonObject &catalog) {
    QTemporaryDir directory; fixture(directory.path()); // The images the catalog's sequences point at.
    writeCatalog(directory.path(), catalog);
    return pet::Player(nullptr, directory.path()).valid();
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
        QCOMPARE(player.states().size(), 11 + player.fidgets().size()); // The states plus their fidgets.
        QVERIFY(!player.fidgets().isEmpty());
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
            if (player.isFidget(state)) { // Plays itself out and hands back to idle.
                for (int pass = 0; pass < 5 && player.state() == state; ++pass) checkSequence();
                QCOMPARE(player.state(), QString("idle"));
            } else if (player.state() == state && player.phase() == "loop" && state != "idle") {
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
        prefs.size = 300; prefs.position = {-1800, 100}; prefs.hasPosition = true; prefs.onTop = false; QVERIFY(store.save(prefs));
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

    void variantsAreDrawnByWeight() {
        pet::Player player; player.setPaused(true);
        QCOMPARE(player.sequence(), QString("Default/Nomal/1")); // The first idle is always the catalog's own.
        Draws draws; player.setRandom(draws.random());
        // Idle weighs 2, 1, 1: draws 0 and 1 are the catalog entry, 2 and 3 are the variants.
        const QList<QPair<int, QString>> expected{{0, "Default/Nomal/1"}, {1, "Default/Nomal/1"},
                                                  {2, "Default/Nomal/2"}, {3, "Default/Nomal/3"}};
        for (const auto &[draw, sequence] : expected) {
            draws.values = {draw};
            player.select("thinking", true); player.select("idle", true);
            QCOMPARE(player.sequence(), sequence);
        }
        // An idle loop may change variant on each pass, and says so.
        QSignalSpy looped(&player, &pet::Player::looped);
        draws.values = {2}; finishSequence(player);
        QCOMPARE(player.sequence(), QString("Default/Nomal/2")); QCOMPARE(player.frameIndex(), 0);
        QCOMPARE(looped.size(), 1); QCOMPARE(looped.last().first().toString(), QString("idle"));
        // Variants off plays only the catalog's entry and draws nothing.
        player.setVariants(false); draws.values = {3}; // Turning them off also ends a variant already playing.
        finishSequence(player); QCOMPARE(player.sequence(), QString("Default/Nomal/1"));
        player.select("thinking", true); player.select("idle", true);
        QCOMPARE(player.sequence(), QString("Default/Nomal/1")); QCOMPARE(draws.values.size(), 1);
        // Phased fidgets vary their middle part.
        player.setVariants(true); draws.values = {1};
        player.select("fidget_aside", true); QCOMPARE(player.sequence(), QString("IDEL/aside/Nomal/A"));
        finishSequence(player); QCOMPARE(player.phase(), QString("loop"));
        QCOMPARE(player.sequence(), QString("IDEL/aside/Nomal/B_2"));
        QCOMPARE(draws.unexpected, 0);
    }
    void fidgetsEndThemselvesAndYieldAtOnce() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        QSignalSpy entered(&player, &pet::Player::entered);
        // The loop part of "aside" plays twice, then its end, then idle.
        player.select("fidget_aside", true); finishSequence(player);
        QCOMPARE(player.phase(), QString("loop")); finishSequence(player);
        QCOMPARE(player.phase(), QString("loop")); finishSequence(player);
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("IDEL/aside/Nomal/C"));
        finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        QCOMPARE(entered.last().first().toString(), QString("idle"));
        // One-shot fidgets simply finish.
        player.select("fidget_yawn", true); QCOMPARE(player.phase(), QString("once"));
        finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        // Real work replaces a fidget immediately, even without the urgent flag.
        player.select("fidget_squat", true); finishSequence(player); QCOMPARE(player.phase(), QString("loop"));
        QVERIFY(player.select("thinking"));
        QCOMPARE(player.state(), QString("thinking")); QCOMPARE(player.phase(), QString("start"));
        // An error or a drag during a fidget returns to idle, not to the fidget.
        player.select("idle", true); player.select("fidget_squat", true); player.select("tool_error");
        QCOMPARE(player.state(), QString("tool_error")); finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        player.select("fidget_aside", true); player.beginDrag(); player.endDrag();
        playOut(player); QCOMPARE(player.state(), QString("idle"));
    }
    void ambientFidgetsFollowTheIdleClock() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        pet::Ambient ambient(player);
        QCOMPARE(ambient.level(), pet::AmbientLevel::Subtle);
        QCOMPARE(pet::Ambient::gapSeconds(pet::AmbientLevel::Subtle), (QPair<int, int>{45, 90}));
        QCOMPARE(pet::Ambient::gapSeconds(pet::AmbientLevel::Lively), (QPair<int, int>{15, 25}));
        Draws draws; qint64 now = 1000000; ambient.setRandom(draws.random()); ambient.setClock([&] { return now; });
        // A gap draw of 0 is the shortest wait, 45 s; the rare roll (not 0) and the pick follow when it is due.
        draws.values = {0}; player.select("thinking", true); player.select("idle", true);
        QCOMPARE(ambient.idleFor(), qint64(0));
        now += 44999; finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        now += 1; draws.values = {5, 0}; finishSequence(player);
        QCOMPARE(player.state(), QString("fidget_aside")); QVERIFY(ambient.resting()); QVERIFY(draws.values.isEmpty());
        // Under two idle minutes only "aside" is eligible. After the fidget the idle clock keeps counting
        // and the next fidget waits for a new gap.
        draws.values = {10}; playOut(player);
        QVERIFY(!ambient.resting()); QCOMPARE(ambient.idleFor(), qint64(45000)); QVERIFY(draws.values.isEmpty());
        finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        // Past two minutes the longer-wait fidgets join in, and the last one never repeats at once:
        // yawn 3, boring 2 and squat 2 remain, so a draw of 3 is "boring".
        now += 130000; draws.values = {5, 3}; finishSequence(player);
        QCOMPARE(player.state(), QString("fidget_boring"));
        // A rare roll of 0 draws from the rare pool instead.
        draws.values = {0}; playOut(player); now += 100000; draws.values = {0, 0}; finishSequence(player);
        QCOMPARE(player.state(), QString("fidget_meow"));
        // Real activity ends the idle clock; idling again starts a fresh one.
        draws.values = {0}; playOut(player); player.select("thinking", true);
        QCOMPARE(ambient.idleFor(), qint64(-1)); QVERIFY(!ambient.resting());
        draws.values = {0}; player.select("idle", true); QCOMPARE(ambient.idleFor(), qint64(0));
        // After ten quiet minutes it dozes off, and wakes through the usual end of its sleep.
        // Just short of that a fidget is still due, not a nap; exactly then the nap wins.
        now += 599999; draws.values = {5, 0}; finishSequence(player); QCOMPARE(player.state(), QString("fidget_aside"));
        draws.values = {0}; playOut(player);
        now += 1; finishSequence(player);
        QCOMPARE(player.state(), QString("sleeping")); QVERIFY(ambient.resting());
        player.select("thinking"); playOut(player);
        for (int i = 0; i < 12 && player.state() != "thinking"; ++i) finishSequence(player);
        QCOMPARE(player.state(), QString("thinking")); QVERIFY(!ambient.resting());
        QCOMPARE(draws.unexpected, 0);
        // Lively waits 15 to 25 seconds.
        ambient.setLevel(pet::AmbientLevel::Lively);
        draws.values = {0}; player.select("idle", true);
        now += 14999; finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        now += 1; draws.values = {5, 0}; finishSequence(player); QCOMPARE(player.state(), QString("fidget_aside"));
        QCOMPARE(draws.unexpected, 0);
    }
    void ambientOffIsQuiet() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        pet::Ambient ambient(player); Draws draws; qint64 now = 0;
        ambient.setRandom(draws.random()); ambient.setClock([&] { return now; });
        ambient.setLevel(pet::AmbientLevel::Off); QVERIFY(!player.variants());
        player.select("thinking", true); player.select("idle", true);
        now += 3 * 3600 * 1000; finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        QCOMPARE(draws.unexpected, 0); QVERIFY(!ambient.resting());
        ambient.setLevel(pet::AmbientLevel::Subtle); QVERIFY(player.variants());
    }
    void ambientDoesNotFightTheMonitor() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window); auto &player = window.player(); player.setPaused(true);
        Draws draws; qint64 clock = 1000000;
        window.ambient().setRandom(draws.random()); window.ambient().setClock([&] { return clock; });
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString kind) {
            ++seq; return pet::Event{"claude", "s1", QString::number(seq), kind, {}, {}, "/work/abc-web", {}, now + seq, {}};
        };
        QVERIFY(monitor.apply(event("prompt"), now + seq));
        QCOMPARE(player.requestedState(), QString("thinking"));
        draws.values = {0}; QVERIFY(monitor.apply(event("interrupt"), now + seq)); // The aggregate is idle again.
        playOut(player); QCOMPARE(player.state(), QString("idle"));
        // A fidget survives the monitor's periodic update, which would otherwise restore plain idle.
        clock += 45000; draws.values = {5, 0}; finishSequence(player);
        QCOMPARE(player.state(), QString("fidget_aside"));
        monitor.update(now + seq + 1); QCOMPARE(player.state(), QString("fidget_aside"));
        // So does a nap, until something real happens; then it wakes through its end.
        draws.values = {0}; playOut(player);
        clock += 700000; finishSequence(player); QCOMPARE(player.state(), QString("sleeping"));
        monitor.update(now + seq + 2); QCOMPARE(player.state(), QString("sleeping"));
        QVERIFY(monitor.apply(event("prompt"), now + seq));
        QCOMPARE(player.requestedState(), QString("thinking")); QCOMPARE(player.phase(), QString("end"));
        QCOMPARE(draws.unexpected, 0);
    }
    void ambientPreference() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path); QCOMPARE(window.ambientLevel(), int(pet::Preferences::AmbientSubtle));
            window.showSettings(); auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
            QComboBox *combo = nullptr;
            for (auto *box : dialog->findChildren<QComboBox*>()) if (box->accessibleName() == "Idle animation") combo = box;
            QVERIFY(combo); QCOMPARE(combo->count(), 3); QCOMPARE(combo->currentIndex(), 1);
            combo->setCurrentIndex(2); QCOMPARE(window.ambient().level(), pet::AmbientLevel::Lively);
            QVERIFY(window.savePreferences()); dialog->close();
        }
        QCOMPARE(pet::PreferencesStore(path).load().ambient, int(pet::Preferences::AmbientLively));
        pet::PetWindow restored(nullptr, path); QCOMPARE(restored.ambient().level(), pet::AmbientLevel::Lively);
        restored.setAmbientLevel(0); QVERIFY(!restored.player().variants()); // Off also means the plain idle loop.
        restored.setAmbientLevel(99); QCOMPARE(restored.ambientLevel(), 2);
        // Older files have no key; a bad value is refused like any other and preserved.
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true})"); file.close();
        pet::PreferencesStore legacy(path); QCOMPARE(legacy.load().ambient, int(pet::Preferences::AmbientSubtle));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true,"ambient":7})"); file.close();
        pet::PreferencesStore invalid(path); QCOMPARE(invalid.load().ambient, int(pet::Preferences::AmbientSubtle));
        QVERIFY(!invalid.save(pet::Preferences{}));
    }
    void malformedVariantsAndFidgets() {
        QTemporaryDir fixtures; auto base = fixture(fixtures.path()); // "idle" and "work" sequences exist.
        auto with = [&](const QString &key, const QJsonValue &value) { auto catalog = base; catalog[key] = value; return catalog; };
        auto variant = [](QJsonArray sequences, int weight) { return QJsonObject{{"sequences", sequences}, {"weight", weight}}; };
        auto playback = [&](const QString &state, const QJsonObject &policy) {
            auto catalog = base; auto all = catalog["playback"].toObject(); all[state] = policy;
            catalog["playback"] = all; return catalog;
        };
        // A well-formed variant, weight and one-shot fidget load.
        QVERIFY(loads(with("variants", QJsonObject{{"idle", QJsonArray{variant({"work"}, 3)}}})));
        auto fidgeting = playback("working", QJsonObject{{"mode", "once"}, {"after", "idle"}});
        auto ambient = [](QJsonArray fidgets, int sleep = 600) {
            return QJsonObject{{"sleep_after_s", sleep}, {"fidgets", fidgets}};
        };
        auto fidget = [](QString state, int weight = 1) { return QJsonObject{{"state", state}, {"weight", weight}}; };
        fidgeting["ambient"] = ambient({fidget("working")}); QVERIFY(loads(fidgeting));
        QVERIFY(loads(playback("idle", QJsonObject{{"mode", "loop"}, {"after", "idle"}, {"weight", 5}})));
        // Each of these is refused.
        QVERIFY(!loads(with("variants", QJsonObject{{"nobody", QJsonArray{variant({"work"}, 1)}}})));
        QVERIFY(!loads(with("variants", QJsonObject{{"idle", QJsonArray{}}})));
        QVERIFY(!loads(with("variants", QJsonObject{{"idle", QJsonArray{variant({"missing"}, 1)}}})));
        QVERIFY(!loads(with("variants", QJsonObject{{"idle", QJsonArray{variant({"work", "idle"}, 1)}}})));
        QVERIFY(!loads(with("variants", QJsonObject{{"idle", QJsonArray{variant({"work"}, 0)}}})));
        QVERIFY(!loads(playback("idle", QJsonObject{{"mode", "loop"}, {"after", "idle"}, {"weight", 0}})));
        QVERIFY(!loads(playback("idle", QJsonObject{{"mode", "loop"}, {"after", "idle"}, {"loops", 2}}))); // Phased only.
        for (const auto &broken : {ambient({fidget("nobody")}), ambient({fidget("idle")}), ambient({fidget("working", 0)}),
                                   ambient({fidget("working"), fidget("working")}), ambient({fidget("working")}, 30)}) {
            auto catalog = fidgeting; catalog["ambient"] = broken; QVERIFY(!loads(catalog));
        }
        // A looping state never ends by itself, so it cannot be a fidget.
        auto looping = base; looping["ambient"] = ambient({fidget("working")}); QVERIFY(!loads(looping));
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
        auto *integrations = dialog->findChild<QGroupBox*>("integrations"); QVERIFY(integrations); // Setup and coverage.
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
    void trayHideAndStatus() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window);
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString session, QString kind, QString reason = {}) {
            ++seq; return pet::Event{"claude", session, QString::number(seq), kind, {}, {}, "/work/" + session, {}, now + seq, reason};
        };
        window.setTrayAvailable(false); // Nothing could show it again: hiding is refused.
        window.setPetHidden(true); QVERIFY(!window.petHidden()); QVERIFY(window.isVisible());
        window.setTrayAvailable(true);
        QCOMPARE(window.statusText(), "Agent Pet — idle"); QVERIFY(!window.trayAlert());
        QVERIFY(monitor.apply(event("one", "prompt"), now + seq));
        QCOMPARE(window.statusText(), "Agent Pet — 1 session");
        QVERIFY(monitor.apply(event("two", "attention", "approval"), now + seq));
        QCOMPARE(window.statusText(), "Agent Pet — 2 sessions · 1 needs attention"); QVERIFY(window.trayAlert());
        QVERIFY(monitor.bubble().isVisible());
        QSignalSpy changed(&window, &pet::PetWindow::presenceChanged);
        monitor.toggleSessions(); QVERIFY(monitor.sessionList().isVisible());
        window.setPetHidden(true);
        QVERIFY(window.petHidden()); QVERIFY(!window.isVisible()); QCOMPARE(changed.size(), 1);
        QVERIFY(!monitor.bubble().isVisible()); QVERIFY(!monitor.sessionList().isVisible());
        QVERIFY(monitor.active()); // Monitoring continues while hidden.
        // New alerts go to the tray instead of a bubble.
        QVERIFY(monitor.apply(event("three", "error"), now + seq)); QVERIFY(!monitor.bubble().isVisible());
        QCOMPARE(window.statusText(), "Agent Pet — 3 sessions · 1 needs attention · 1 tool error");
        window.setOnTop(false); window.setClickThrough(true); QVERIFY(!window.isVisible()); // Flag changes keep it hidden.
        window.setClickThrough(false);
        // A user-hidden pet stays hidden when sessions start.
        QVERIFY(monitor.apply(event("four", "session_start"), now + seq)); QVERIFY(window.petHidden());
        window.setPetHidden(false);
        QVERIFY(window.isVisible()); QVERIFY(monitor.bubble().isVisible()); QCOMPARE(changed.size(), 2);
        window.setPetHidden(true); window.recover(); QVERIFY(!window.petHidden()); QVERIFY(window.isVisible());
        for (const auto *session : {"one", "two", "three", "four"}) QVERIFY(monitor.apply(event(session, "session_end"), now + seq));
        QCOMPARE(window.statusText(), "Agent Pet — idle"); QVERIFY(!window.trayAlert());
        window.setPetHidden(true); window.requestQuit(); QVERIFY(!monitor.active()); // Quits at once while hidden.
    }
    void idlePolicyHidesAndQuits() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        pet::PetWindow window(nullptr, path); window.show();
        pet::Monitor monitor(window);
        window.setTrayAvailable(true);
        const qint64 now = QDateTime::currentMSecsSinceEpoch(), grace = pet::Presence::idleGraceMs; qint64 seq = 0;
        auto event = [&](QString kind) { ++seq; return pet::Event{"claude", "s", QString::number(seq), kind, {}, {}, "/work/s", {}, now + seq}; };
        window.setWhenIdle(pet::IdlePolicy::Hide);
        QCOMPARE(pet::PreferencesStore(path).load().whenIdle, pet::IdlePolicy::Hide);
        monitor.update(now + 3 * grace); QVERIFY(!window.petHidden()); // Never saw a session.
        QVERIFY(monitor.apply(event("session_start"), now + seq));
        QVERIFY(monitor.apply(event("session_end"), now + seq));
        const qint64 deadline = window.presence().idleDeadline();
        QVERIFY(deadline > now); QVERIFY(deadline <= now + seq + grace);
        monitor.update(deadline - 1); QVERIFY(!window.petHidden());
        monitor.update(deadline); QVERIFY(window.petHidden()); QVERIFY(!window.isVisible());
        QVERIFY(monitor.apply(event("session_start"), now + seq)); // Auto-hidden: back with the next session.
        QVERIFY(!window.petHidden()); QVERIFY(window.isVisible());
        // `agent-pet autostart` changed the file meanwhile: read before acting and kept on save.
        auto external = pet::PreferencesStore(path).load();
        external.autostart = true; external.whenIdle = pet::IdlePolicy::Quit;
        QVERIFY(pet::PreferencesStore(path).save(external));
        window.setPetSize(200); QVERIFY(window.savePreferences());
        auto saved = pet::PreferencesStore(path).load();
        QVERIFY(saved.autostart); QCOMPARE(saved.whenIdle, pet::IdlePolicy::Quit); QCOMPARE(saved.size, 200);
        QVERIFY(window.autostart());
        window.setWhenIdle(pet::IdlePolicy::Hide);
        external = pet::PreferencesStore(path).load(); external.whenIdle = pet::IdlePolicy::Quit;
        QVERIFY(pet::PreferencesStore(path).save(external));
        QCOMPARE(window.whenIdle(), pet::IdlePolicy::Hide);
        QVERIFY(monitor.apply(event("session_end"), now + seq));
        QVERIFY(window.presence().idleDeadline());
        monitor.update(window.presence().idleDeadline()); // The file now says quit.
        QVERIFY(window.quitting()); QVERIFY(!monitor.active());
    }
    void startupSettingsGroup() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        pet::PetWindow window(nullptr, path); window.show();
        window.setTrayAvailable(false);
        window.showSettings();
        auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
        auto *startup = dialog->findChild<QGroupBox*>("startup"); QVERIFY(startup);
        auto *autostart = startup->findChild<QCheckBox*>(); QVERIFY(autostart); QVERIFY(!autostart->isChecked());
        auto *idle = startup->findChild<QComboBox*>(); QVERIFY(idle); QCOMPARE(idle->count(), 3);
        autostart->setChecked(true); idle->setCurrentIndex(2);
        QVERIFY(window.autostart()); QCOMPARE(window.whenIdle(), pet::IdlePolicy::Quit);
        const auto saved = pet::PreferencesStore(path).load();
        QVERIFY(saved.autostart); QCOMPARE(saved.whenIdle, pet::IdlePolicy::Quit);
        dialog->close(); QCoreApplication::processEvents();
    }
};
QTEST_MAIN(PrototypeTests)
#include "prototype_tests.moc"
