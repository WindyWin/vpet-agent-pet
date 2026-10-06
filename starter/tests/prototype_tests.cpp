#include "animation/activity.h"
#include "desktop/monitor.h"
#include "desktop/pet_window.h"
#include "desktop/session_playback.h"
#include "i18n/language.h"
#include "version.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QGroupBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolTip>
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
// The reactions that only celebrate or surprise.
QSet<QString> celebrations(pet::Player &player) {
    QSet<QString> reactions;
    for (const auto *name : {"turn_finished", "snack", "milestone", "long_turn", "friday_evening", "may20", "birthday",
                             "konami", "danger"})
        for (const auto &reaction : player.reactions(name)) reactions.insert(reaction.state);
    return reactions;
}
// Every state under one mood, every frame decoded within the size and cache bounds, each state played out.
void playsEveryFrame(pet::Player &player, const char *mood) {
    const auto reactions = celebrations(player);
    auto checkSequence = [&] {
        const int count = player.frameCount();
        for (int i = 0; i < count; ++i) {
            QVERIFY2(!player.pixmap().isNull(), qPrintable(player.error()));
            QVERIFY(player.pixmap().width() <= 640); QVERIFY(player.pixmap().height() <= 640);
            QVERIFY(player.cacheKiB() <= pet::Player::cacheLimitKiB); player.advance();
        }
    };
    for (const auto &state : player.states()) {
        player.setMood(mood);
        QVERIFY(player.select(state, true));
        checkSequence();
        if (player.isFidget(state) || reactions.contains(state)) { // Plays itself out and hands back to idle.
            for (int pass = 0; pass < 10 && player.state() == state; ++pass) checkSequence();
            QCOMPARE(player.state(), QString("idle"));
        } else if (player.state() == state && player.phase() == "loop" && state != "idle") {
            checkSequence(); player.select("idle"); QCOMPARE(player.phase(), QString("end"));
            checkSequence(); QCOMPARE(player.state(), QString("idle"));
        }
        QVERIFY2(player.error().isEmpty(), qPrintable(player.error()));
    }
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
        // An ordinary Wednesday noon, so no special day or hour changes what a test expects.
        pet::EasterEggs::defaultClock = [] { return QDateTime(QDate(2026, 10, 7), QTime(12, 0)); };
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
        // The session states, the reactions that only celebrate or surprise, and the fidgets.
        const auto reactions = celebrations(player);
        QCOMPARE(reactions.size(), 10);
        // Late at night the pet yawns more, with a fidget it already has.
        QCOMPARE(player.reactions("late_night").size(), 1); QVERIFY(player.isFidget(player.reactions("late_night").first().state));
        // And the touch reactions: three held presses, two falls and two edges.
        const auto &touch = player.touch();
        QSet<QString> touches{touch.fallLeft, touch.fallRight, touch.edgeLeft, touch.edgeRight};
        for (const auto &region : touch.regions) touches.insert(region.state);
        QCOMPARE(touches.size(), 7);
        for (const auto &state : touches) QVERIFY2(player.isTouch(state), qPrintable(state));
        QCOMPARE(player.states().size(), 13 + reactions.size() - 1 + player.fidgets().size() + touches.size());
        QVERIFY(!player.fidgets().isEmpty());
        // Walks in each mood, crawls and climbs up and down both edges: all fidgets that move the window.
        int moves = 0;
        for (const auto &state : player.states())
            if (player.move(state)) { ++moves; QVERIFY2(player.isFidget(state), qPrintable(state)); }
        QCOMPARE(moves, 12); QCOMPARE(player.moveScale(), 500);
        playsEveryFrame(player, ""); if (QTest::currentTestFailed()) return;
        for (int i = 0; i < 50; ++i) {
            player.select(i % 2 ? "thinking" : "working", true);
            QVERIFY(player.cacheKiB() <= 1600);
        }
        player.setRenderSize(160); QVERIFY(player.cacheKiB() <= 100);
    }
    // Each mood's art is decoded too; separate functions so test shards can share the work.
    void everyHappyFrame() { pet::Player player; player.setPaused(true); player.setRenderSize(640); playsEveryFrame(player, "happy"); }
    void everyPoorFrame() { pet::Player player; player.setPaused(true); player.setRenderSize(640); playsEveryFrame(player, "poor"); }
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
    void languagePreference() {
        // The translator is application-wide: the other tests expect English whatever happens here.
        const auto english = qScopeGuard([] { pet::i18n::install(pet::i18n::Language::English); });
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path); window.show(); QCOMPARE(window.language(), QString("auto"));
            pet::Monitor monitor(window);
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            QVERIFY(monitor.apply(pet::Event{"claude", "a1b2", "1", "attention", {}, {}, "/work/abc-web", {}, now, "approval"}, now));
            QCOMPARE(monitor.bubble().title(), "Needs approval");
            const auto menuHas = [&window](const QString &text) {
                const auto actions = window.findChild<QMenu *>()->actions();
                return std::any_of(actions.begin(), actions.end(), [&](QAction *action) { return action->text() == text; });
            };
            const auto settingsTitled = [&window](const QString &title) -> QDialog * {
                for (auto *dialog : window.findChildren<QDialog *>()) if (dialog->isVisible() && dialog->windowTitle() == title) return dialog;
                return nullptr;
            };
            QVERIFY(menuHas("Settings…"));
            window.showSettings();
            auto *dialog = settingsTitled("Agent Pet settings"); QVERIFY(dialog);
            dialog->findChild<QTabWidget *>()->setCurrentIndex(1);
            QComboBox *combo = nullptr;
            for (auto *box : dialog->findChildren<QComboBox *>()) if (box->accessibleName() == "Language") combo = box;
            QVERIFY(combo); QCOMPARE(combo->count(), 3); QCOMPARE(combo->itemText(2), "Tiếng Việt");
            combo->setCurrentIndex(combo->findData("vi")); // Live.
            QCOMPARE(pet::i18n::installed(), pet::i18n::Language::Vietnamese);
            QTRY_VERIFY(menuHas("Cài đặt…"));
            QCOMPARE(window.statusText(), "Agent Pet — 1 phiên · 1 phiên cần chú ý");
            monitor.update(now); QCOMPARE(monitor.bubble().title(), "Cần phê duyệt");
            // Settings come back in Vietnamese, on the tab that was open.
            QTRY_VERIFY(settingsTitled("Cài đặt Agent Pet"));
            QCOMPARE(settingsTitled("Cài đặt Agent Pet")->findChild<QTabWidget *>()->currentIndex(), 1);
            auto *birthday = settingsTitled("Cài đặt Agent Pet")->findChild<QDateEdit *>();
            QVERIFY(birthday); QCOMPARE(birthday->text(), "1 tháng 1"); // Month names follow the pet, not the system.
            window.setLanguage("en");
            QTRY_VERIFY(menuHas("Settings…"));
            monitor.update(now); QCOMPARE(monitor.bubble().title(), "Needs approval");
            window.setLanguage("vi");
        }
        QCOMPARE(pet::PreferencesStore(path).load().language, QString("vi"));
        // An unknown value, perhaps from a newer version, reads as automatic without invalidating the file.
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true,"language":"klingon"})"); file.close();
        pet::PreferencesStore store(path); QCOMPARE(store.load().language, QString("auto")); QVERIFY(store.error().isEmpty());
    }
    void activityPreference() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path);
            QCOMPARE(window.activityStyle(), int(pet::Preferences::ActivityPlayful)); QVERIFY(window.player().continuity());
            window.showSettings(); auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
            QComboBox *combo = nullptr;
            for (auto *box : dialog->findChildren<QComboBox*>()) if (box->accessibleName() == "Active animation") combo = box;
            QVERIFY(combo); QCOMPARE(combo->count(), 3); QCOMPARE(combo->currentIndex(), 2);
            QVERIFY(combo->itemText(0).startsWith("Classic")); QVERIFY(combo->toolTip().contains("never delays alerts"));
            combo->setCurrentIndex(0); // Live.
            QCOMPARE(window.activity().style(), pet::ActivityStyle::Classic); QVERIFY(!window.player().continuity());
            QVERIFY(window.savePreferences()); dialog->close();
        }
        QCOMPARE(pet::PreferencesStore(path).load().activity, int(pet::Preferences::ActivityClassic));
        pet::PetWindow restored(nullptr, path); QCOMPARE(restored.activity().style(), pet::ActivityStyle::Classic);
        // Independent of the idle animation, both ways.
        restored.setAmbientLevel(0); QCOMPARE(restored.activityStyle(), 0);
        restored.setActivityStyle(1); QCOMPARE(restored.ambientLevel(), 0); QVERIFY(restored.player().continuity());
        restored.setActivityStyle(99); QCOMPARE(restored.activityStyle(), 2);
        // Older files have no key; a bad value is refused like any other and preserved.
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true})"); file.close();
        pet::PreferencesStore legacy(path); QCOMPARE(legacy.load().activity, int(pet::Preferences::ActivityPlayful));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true,"activity":3})"); file.close();
        pet::PreferencesStore invalid(path); QCOMPARE(invalid.load().activity, int(pet::Preferences::ActivityPlayful));
        QVERIFY(!invalid.save(pet::Preferences{}));
    }
    void monitorLeavesTheDeskAlone() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window); auto &player = window.player(); player.setPaused(true);
        player.setRandom([](int) { return 0; });
        window.activity().setRandom([](int) { return 0; }); window.activity().setClock([] { return qint64(0); }); // Never due.
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString kind, qint64 at, QString tool = {}, QString activity = {}) {
            ++seq; return pet::Event{"claude", "s1", QString::number(seq), kind, tool, {}, "/work/abc-web", activity, at, {}};
        };
        QVERIFY(monitor.apply(event("prompt", now), now));
        QVERIFY(monitor.apply(event("tool_start", now + 1, "t1", "reading"), now + 1));
        for (int i = 0; i < 6 && !(player.state() == "reading" && player.phase() == "loop"); ++i) finishSequence(player);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("loop"));
        // The tool ends; after the activity hold the aggregate is thinking, and the pet stays at its book.
        QVERIFY(monitor.apply(event("tool_end", now + 2, "t1"), now + 2));
        monitor.update(now + 5002);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("linger"));
        QCOMPARE(player.requestedState(), QString("thinking"));
        // The periodic update sees its request honoured and restarts nothing.
        player.advance(); const auto sequence = player.sequence(); const int frame = player.frameIndex();
        monitor.update(now + 6000);
        QCOMPARE(player.phase(), QString("linger")); QCOMPARE(player.sequence(), sequence); QCOMPARE(player.frameIndex(), frame);
        // The next tool picks the book back up: no end, no start.
        QVERIFY(monitor.apply(event("tool_start", now + 7000, "t2", "reading"), now + 7000));
        QCOMPARE(player.phase(), QString("loop")); QCOMPARE(player.sequence(), QString("WORK/Study/B_1_Nomal"));
        // A request for the user still cuts in at once.
        QVERIFY(monitor.apply(event("attention", now + 7001), now + 7001));
        QCOMPARE(player.state(), QString("needs_input"));
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
    void activitySectionIsShipped() {
        pet::Player player;
        QVERIFY(!player.activity("idle")); QVERIFY(!player.activity("needs_input"));
        const auto *thinking = player.activity("thinking"), *reading = player.activity("reading"),
                   *working = player.activity("working");
        QVERIFY(thinking && reading && working);
        QCOMPARE(thinking->loops.size(), 6); QCOMPARE(reading->loops.size(), 2); QCOMPARE(working->loops.size(), 3);
        QVERIFY(thinking->loops.at(4).playful); QVERIFY(!thinking->loops.at(0).playful);
        QCOMPARE(thinking->exit.value("working").first().sequence, QString("Think/Happy/C_2"));
        QCOMPARE(working->enterFrom, QStringList{"thinking"});
        QCOMPARE(reading->linger.to, QString("thinking")); QCOMPARE(reading->linger.maxMs, 8000);
        QVERIFY(reading->linger.in.isEmpty()); QCOMPARE(reading->linger.loop.size(), 2);
        QCOMPARE(working->linger.in, QString("WORK/Desk/ponder_in"));
        QCOMPARE(working->linger.out, QString("WORK/Desk/ponder_out"));
        QCOMPARE(reading->handover.value("working"), QString("WORK/Desk/reading_to_working"));
        QCOMPARE(working->handover.value("reading"), QString("WORK/Desk/working_to_reading"));
    }
    void malformedActivity() {
        QTemporaryDir fixtures; auto base = fixture(fixtures.path()); // "idle" and "work" sequences exist.
        auto states = base["states"].toObject(); auto playback = base["playback"].toObject();
        for (const auto *state : {"thinking", "reading", "working"}) {
            states[state] = QJsonArray{"work", "work", "work"};
            playback[state] = QJsonObject{{"mode", "phased"}, {"after", "idle"}};
        }
        states["blink"] = QJsonArray{"work"}; playback["blink"] = QJsonObject{{"mode", "once"}, {"after", "idle"}};
        base["states"] = states; base["playback"] = playback;
        auto choice = [](const QString &sequence, int weight = 1) { return QJsonObject{{"sequence", sequence}, {"weight", weight}}; };
        const QJsonObject good{
            {"thinking", QJsonObject{
                {"loops", QJsonArray{choice("work"), QJsonObject{{"sequence", "idle"}, {"weight", 2}, {"style", "playful"}}}},
                {"exit", QJsonObject{{"working", QJsonArray{choice("idle")}}}}}},
            {"reading", QJsonObject{
                {"linger", QJsonObject{{"to", "thinking"}, {"max_s", 8}, {"loop", QJsonArray{"work", "idle"}}}},
                {"handover", QJsonObject{{"working", "idle"}}}}},
            {"working", QJsonObject{
                {"enter", QJsonObject{{"from", QJsonArray{"thinking"}}, {"choices", QJsonArray{choice("idle")}}}},
                {"linger", QJsonObject{{"to", "thinking"}, {"max_s", 60}, {"in", "work"}, {"loop", QJsonArray{"idle"}}, {"out", "work"}}}}}};
        auto with = [&](const QJsonObject &activity) { auto catalog = base; catalog["activity"] = activity; return catalog; };
        auto broken = [&](const QString &state, const QString &key, const QJsonValue &value) {
            auto activity = good; auto entry = activity[state].toObject(); entry[key] = value; activity[state] = entry;
            return with(activity);
        };
        // Without the section, or with an empty entry, everything plays as before.
        QVERIFY(loads(base)); QVERIFY(loads(with(good))); QVERIFY(loads(with(QJsonObject{{"reading", QJsonObject{}}})));
        {
            QTemporaryDir directory; fixture(directory.path()); writeCatalog(directory.path(), base);
            pet::Player plain(nullptr, directory.path()); QVERIFY(plain.valid()); QVERIFY(!plain.activity("working"));
        }
        // Only phased states that end when asked can be decorated.
        QVERIFY(!loads(with(QJsonObject{{"idle", QJsonObject{}}})));
        QVERIFY(!loads(with(QJsonObject{{"blink", QJsonObject{}}})));
        QVERIFY(!loads(with(QJsonObject{{"nobody", QJsonObject{}}})));
        auto counted = with(good); auto policies = counted["playback"].toObject();
        policies["thinking"] = QJsonObject{{"mode", "phased"}, {"after", "idle"}, {"loops", 2}};
        counted["playback"] = policies; QVERIFY(!loads(counted));
        // Each broken part is refused.
        QVERIFY(!loads(broken("thinking", "loops", QJsonArray{})));
        QVERIFY(!loads(broken("thinking", "loops", QJsonArray{choice("missing")})));
        QVERIFY(!loads(broken("thinking", "loops", QJsonArray{choice("work", 0)})));
        QVERIFY(!loads(broken("thinking", "loops", QJsonArray{choice("work", 1001)})));
        QVERIFY(!loads(broken("thinking", "loops", QJsonArray{QJsonObject{{"sequence", "work"}, {"weight", 1}, {"style", "wild"}}})));
        QVERIFY(!loads(broken("thinking", "exit", QJsonObject{{"thinking", QJsonArray{choice("idle")}}}))); // Itself.
        QVERIFY(!loads(broken("thinking", "exit", QJsonObject{{"idle", QJsonArray{choice("idle")}}})));
        QVERIFY(!loads(broken("thinking", "exit", QJsonObject{{"working", QJsonArray{}}})));
        QVERIFY(!loads(broken("working", "enter", QJsonObject{{"from", QJsonArray{"idle"}}, {"choices", QJsonArray{choice("idle")}}})));
        QVERIFY(!loads(broken("working", "enter", QJsonObject{{"from", QJsonArray{"thinking"}}, {"choices", QJsonArray{}}})));
        QVERIFY(!loads(broken("working", "enter", QJsonObject{{"from", QJsonArray{}}, {"choices", QJsonArray{choice("idle")}}})));
        QVERIFY(!loads(broken("reading", "linger", QJsonObject{{"to", "idle"}, {"max_s", 8}, {"loop", QJsonArray{"work"}}})));
        QVERIFY(!loads(broken("reading", "linger", QJsonObject{{"to", "thinking"}, {"max_s", 0}, {"loop", QJsonArray{"work"}}})));
        QVERIFY(!loads(broken("reading", "linger", QJsonObject{{"to", "thinking"}, {"max_s", 61}, {"loop", QJsonArray{"work"}}})));
        QVERIFY(!loads(broken("reading", "linger", QJsonObject{{"to", "thinking"}, {"max_s", 8}, {"loop", QJsonArray{}}})));
        QVERIFY(!loads(broken("reading", "linger", QJsonObject{{"to", "thinking"}, {"max_s", 8}, {"in", "missing"}, {"loop", QJsonArray{"work"}}})));
        QVERIFY(!loads(broken("reading", "handover", QJsonObject{{"idle", "work"}})));
        QVERIFY(!loads(broken("reading", "handover", QJsonObject{{"working", "missing"}})));
        QVERIFY(!loads(broken("reading", "handover", QJsonObject{})));
    }
    void phasedLoopsAnnounceEachPass() {
        pet::Player player; player.setPaused(true);
        QSignalSpy looped(&player, &pet::Player::looped);
        player.select("reading", true);
        QVERIFY(!player.vary("WORK/Study/B_2_Nomal")); // Start phase.
        finishSequence(player); QCOMPARE(looped.size(), 0); // Its first pass is no new pass.
        finishSequence(player);
        QCOMPARE(looped.size(), 1); QCOMPARE(looped.last().first().toString(), QString("reading"));
        QVERIFY(!player.vary("WORK/WorkONE/B_2_Nomal")); // Another state's alternate.
        QVERIFY(!player.vary("WORK/Study/C_Nomal")); // Not an alternate at all.
        QVERIFY(player.vary("WORK/Study/B_2_Nomal"));
        QCOMPARE(player.sequence(), QString("WORK/Study/B_2_Nomal")); QCOMPARE(player.frameIndex(), 0);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("loop"));
        player.advance(); QVERIFY(!player.vary("WORK/Study/B_3_Nomal")); // Mid-pass.
        // An alternate lasts one pass.
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Study/B_1_Nomal")); QCOMPARE(looped.size(), 2);
        // A listener varies the pass it is told about.
        const auto connection = connect(&player, &pet::Player::looped, &player,
                                        [&] { QVERIFY(player.vary("WORK/Study/B_3_Nomal")); });
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Study/B_3_Nomal"));
        disconnect(connection);
    }
    void everyActivityAlternateDecodes() {
        pet::Player player; player.setPaused(true); player.setRenderSize(640);
        for (const auto *state : {"thinking", "reading", "working"})
            for (const auto &loop : player.activity(state)->loops) {
                player.select(state, true); finishSequence(player);
                QVERIFY2(player.vary(loop.sequence), qPrintable(loop.sequence));
                for (int i = 0, count = player.frameCount(); i < count; ++i) {
                    QVERIFY2(!player.pixmap().isNull(), qPrintable(player.error()));
                    QVERIFY(player.cacheKiB() <= pet::Player::cacheLimitKiB); player.advance();
                }
                QCOMPARE(player.state(), QString(state)); QVERIFY2(player.error().isEmpty(), qPrintable(player.error()));
            }
    }
    void lingerKeepsThePetAtItsDesk() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; }); player.setContinuity(true);
        player.select("reading", true); finishSequence(player);
        // A short thinking pause keeps the book open; the request counts as honoured.
        player.select("thinking");
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.requestedState(), QString("thinking"));
        QCOMPARE(player.phase(), QString("linger")); QCOMPARE(player.sequence(), QString("WORK/Study/B_4_Nomal"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Study/B_3_Nomal")); // Never twice in a row.
        player.advance(); player.select("thinking"); QCOMPARE(player.frameIndex(), 1); // The same request restarts nothing.
        // Paused, nothing is shown, so no linger time passes.
        QTest::qWait(30); QCOMPARE(player.frameIndex(), 1); QCOMPARE(player.phase(), QString("linger"));
        // Reading again resumes the base loop: no end, no start.
        QVERIFY(player.select("reading"));
        QCOMPARE(player.phase(), QString("loop")); QCOMPARE(player.sequence(), QString("WORK/Study/B_1_Nomal"));
        QCOMPARE(player.requestedState(), QString("reading"));
        // A long pause ends at the first pass boundary at or past max_s, counted in frames shown:
        // 1250 + 1500 + 1250 + 1500 + 1250 = 6750 ms, and the sixth pass reaches 8250 ms.
        player.select("thinking");
        for (int pass = 0; pass < 5; ++pass) { finishSequence(player); QCOMPARE(player.phase(), QString("linger")); }
        finishSequence(player);
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("WORK/Study/C_Nomal"));
        finishSequence(player); QCOMPARE(player.state(), QString("thinking")); QCOMPARE(player.phase(), QString("start"));
        // Working ponders chin in hand, with an in and an out.
        player.select("working", true); finishSequence(player);
        player.select("thinking");
        QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_in")); QCOMPARE(player.phase(), QString("linger"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_loop"));
        player.select("working");
        QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_out")); QCOMPARE(player.phase(), QString("linger"));
        QCOMPARE(player.requestedState(), QString("working"));
        finishSequence(player);
        QCOMPARE(player.phase(), QString("loop")); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal"));
        // 500 ms in, then 1000 ms passes: the eighth reaches 8500 ms, then out, end and thinking.
        player.select("thinking"); finishSequence(player);
        for (int pass = 0; pass < 7; ++pass) { finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_loop")); }
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_out"));
        finishSequence(player);
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("WORK/WorkONE/C_Nomal"));
        finishSequence(player); QCOMPARE(player.state(), QString("thinking"));
    }
    void handoverSwapsPropsAtTheDesk() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; }); player.setContinuity(true);
        QSignalSpy entered(&player, &pet::Player::entered);
        player.select("reading", true); finishSequence(player); entered.clear();
        player.select("working");
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("handover"));
        QCOMPARE(player.sequence(), QString("WORK/Desk/reading_to_working"));
        QCOMPARE(player.requestedState(), QString("working")); QCOMPARE(entered.size(), 0);
        // No end and no start: straight into working's loop.
        finishSequence(player);
        QCOMPARE(player.state(), QString("working")); QCOMPARE(player.phase(), QString("loop"));
        QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal")); QCOMPARE(entered.size(), 1);
        // From a linger too: the pen comes down first, then the props swap.
        player.select("thinking"); QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_in"));
        finishSequence(player); player.select("reading");
        QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_out")); QCOMPARE(player.requestedState(), QString("reading"));
        finishSequence(player);
        QCOMPARE(player.phase(), QString("handover")); QCOMPARE(player.sequence(), QString("WORK/Desk/working_to_reading"));
        finishSequence(player); QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("loop"));
        // Reading's linger has no out, so its handover starts at once.
        player.select("thinking"); player.select("working");
        QCOMPARE(player.sequence(), QString("WORK/Desk/reading_to_working"));
        // The same request again changes nothing; any other plays the usual end at once.
        player.select("working"); QCOMPARE(player.phase(), QString("handover"));
        player.select("idle");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("WORK/Study/C_Nomal"));
        QCOMPARE(player.requestedState(), QString("idle"));
    }
    void handoverLandsBeforeChangingCourse() {
        // Tool calls flip reading and working faster than a handover plays: the swap lands, then the pet acts
        // on the latest request from the desk instead of getting up.
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; }); player.setContinuity(true);
        player.select("reading", true); finishSequence(player);
        player.select("working"); player.advance();
        player.select("reading");
        QCOMPARE(player.phase(), QString("handover")); QCOMPARE(player.sequence(), QString("WORK/Desk/reading_to_working"));
        QCOMPARE(player.frameIndex(), 1); QCOMPARE(player.requestedState(), QString("reading"));
        finishSequence(player); // Lands in working, then hands straight back.
        QCOMPARE(player.state(), QString("working")); QCOMPARE(player.phase(), QString("handover"));
        QCOMPARE(player.sequence(), QString("WORK/Desk/working_to_reading")); QCOMPARE(player.requestedState(), QString("reading"));
        // Flipping back and forth while it plays: only the last request counts.
        player.select("working"); player.select("reading"); player.select("working");
        QCOMPARE(player.phase(), QString("handover")); QCOMPARE(player.requestedState(), QString("working"));
        finishSequence(player);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("handover"));
        QCOMPARE(player.sequence(), QString("WORK/Desk/reading_to_working"));
        finishSequence(player); QCOMPARE(player.state(), QString("working")); QCOMPARE(player.phase(), QString("loop"));
        // A thinking pause mid-swap lingers at the new desk once it lands.
        player.select("reading"); player.select("thinking");
        QCOMPARE(player.phase(), QString("handover")); QCOMPARE(player.requestedState(), QString("thinking"));
        finishSequence(player);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("linger"));
        QCOMPARE(player.sequence(), QString("WORK/Study/B_4_Nomal")); QCOMPARE(player.requestedState(), QString("thinking"));
        // Anything the desk cannot show still ends at once, mid-swap or not.
        player.select("working"); QCOMPARE(player.phase(), QString("handover"));
        player.select("turn_finished");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("WORK/Study/C_Nomal"));
        // Queued flip-backs never hold an urgent alert or a drag behind the swap.
        for (const auto *urgent : {"needs_input", "tool_error", "dragging"}) {
            player.select("reading", true); finishSequence(player);
            player.select("working"); player.select("reading");
            if (QString(urgent) == "dragging") player.beginDrag();
            else player.select(urgent, true);
            QCOMPARE(player.state(), QString(urgent));
            if (QString(urgent) == "dragging") player.endDrag();
        }
    }
    void reactionsAskTheGate() {
        pet::Player player; player.setPaused(true);
        int asked = 0; bool allow = false;
        player.setReactionGate([&] { ++asked; return allow; });
        player.select("thinking", true); QCOMPARE(asked, 0); // Nothing reacts to coming from idle.
        finishSequence(player);
        // Leaving for reading asks once; refused, the usual end plays, and reading has no welcome.
        player.select("reading"); QCOMPARE(asked, 1); QCOMPARE(player.sequence(), QString("Think/Nomal/C"));
        finishSequence(player); QCOMPARE(player.state(), QString("reading")); QCOMPARE(asked, 1);
        // Allowed: thinking leaves for working with a happy turn instead of its end...
        player.select("thinking", true); finishSequence(player);
        allow = true; player.select("working"); QCOMPARE(asked, 2);
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("Think/Happy/C_2"));
        // ...and working, entered from thinking, asks for its welcome: the first loop pass.
        finishSequence(player); QCOMPARE(asked, 3); QCOMPARE(player.sequence(), QString("WORK/WorkONE/A_Nomal"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/Happy/B"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal")); // One pass.
        // Transitions without art never ask.
        player.select("idle"); QCOMPARE(asked, 3); QCOMPARE(player.sequence(), QString("WORK/WorkONE/C_Nomal"));
        // An urgent select skips the end and its reaction, but the welcome still asks.
        player.select("thinking", true); finishSequence(player);
        player.select("working", true); QCOMPARE(asked, 4);
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/Happy/B"));
    }
    void decorationNeverDelaysAlerts() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; }); player.setContinuity(true);
        auto lingerInReading = [&] {
            player.select("reading", true); finishSequence(player); player.select("thinking");
            QCOMPARE(player.phase(), QString("linger"));
        };
        // An alternate pass ends at once, for an urgent change or not.
        player.select("reading", true); finishSequence(player); finishSequence(player);
        QVERIFY(player.vary("WORK/Study/B_2_Nomal")); player.advance();
        player.select("turn_finished");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("WORK/Study/C_Nomal"));
        player.select("reading", true); finishSequence(player); finishSequence(player);
        QVERIFY(player.vary("WORK/Study/B_2_Nomal"));
        player.select("needs_input", true);
        QCOMPARE(player.state(), QString("needs_input")); QCOMPARE(player.phase(), QString("start"));
        // So does a linger: an alert, an error, a drag, a finished turn or idle each act at once.
        lingerInReading(); player.select("needs_input", true); QCOMPARE(player.state(), QString("needs_input"));
        lingerInReading(); player.select("tool_error", true); QCOMPARE(player.state(), QString("tool_error"));
        lingerInReading(); player.beginDrag(); QCOMPARE(player.state(), QString("dragging"));
        player.endDrag(); QCOMPARE(player.requestedState(), QString("thinking")); // The request it was showing.
        lingerInReading(); player.select("turn_finished");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.requestedState(), QString("turn_finished"));
        lingerInReading(); player.select("idle"); QCOMPARE(player.sequence(), QString("WORK/Study/C_Nomal"));
        // Continuity off: a short thinking pause plays the usual end.
        player.setContinuity(false); player.select("reading", true); finishSequence(player);
        player.select("thinking"); QCOMPARE(player.phase(), QString("end"));
    }
    void decorationFollowsEveryRequest() {
        // Tool calls can flip the request many times a second; whatever the timing, the latest one wins.
        pet::Player player; player.setPaused(true); player.setContinuity(true);
        QRandomGenerator generator(47);
        player.setRandom([&](int bound) { return int(generator.bounded(bound)); });
        int asked = 0; player.setReactionGate([&] { return ++asked % 2 == 0; });
        // Mostly between activities, as tool calls flip it; now and then a finished turn or idle.
        const QStringList requests{"thinking", "reading", "working", "thinking", "reading", "working", "idle", "turn_finished"};
        QSet<QString> visited; // Every frame decodes full-size art, so the run is short but must reach each path.
        connect(&player, &pet::Player::changed, &player, [&] {
            visited << player.phase() << player.sequence();
        }); // Observe every displayed frame, including sequences crossed by a batch of advances.
        for (int step = 0; step < 600; ++step) {
            if (generator.bounded(10) < 4) {
                const auto request = requests.at(int(generator.bounded(int(requests.size()))));
                player.select(request);
                QCOMPARE(player.requestedState(), request);
            } else {
                for (int i = int(generator.bounded(1, 8)); i > 0; --i) player.advance();
            }
            QVERIFY2(player.error().isEmpty(), qPrintable(player.error()));
            visited << player.phase() << player.sequence();
        }
        // Desk continuity can avoid entering working for the entire random run. Finish with a
        // thinking -> working transition whose gate declines the exit and allows the welcome.
        asked = 0;
        player.select("thinking", true); finishSequence(player);
        player.select("working"); QCOMPARE(player.requestedState(), QString("working"));
        finishSequence(player); finishSequence(player);
        QCOMPARE(player.state(), QString("working"));
        QCOMPARE(player.sequence(), QString("WORK/WorkONE/Happy/B"));
        for (const auto *path : {"linger", "handover", "WORK/Desk/ponder_out", "WORK/Desk/working_to_reading",
                                 "WORK/Desk/reading_to_working", "Think/Happy/C_2", "WORK/WorkONE/Happy/B"})
            QVERIFY2(visited.contains(path), path);
        player.select("reading");
        for (int i = 0; i < 40 && !(player.state() == "reading" && player.phase() == "loop"); ++i) finishSequence(player);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("loop"));
    }
    void activityAlternatesOnePassAtATime() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        pet::Activity activity(player); Draws draws; qint64 now = 1000000;
        activity.setRandom(draws.random()); activity.setClock([&] { return now; });
        QCOMPARE(activity.style(), pet::ActivityStyle::Playful); QVERIFY(player.continuity());
        QCOMPARE(pet::Activity::gapSeconds(pet::ActivityStyle::Playful), (QPair<int, int>{6, 12}));
        QCOMPARE(pet::Activity::gapSeconds(pet::ActivityStyle::Subtle), (QPair<int, int>{10, 18}));
        // Entering thinking starts the clock: a gap draw of 0 is the shortest wait, 6 s.
        draws.values = {0}; player.select("thinking", true); finishSequence(player);
        QCOMPARE(player.sequence(), QString("Think/Nomal/B"));
        now += 5999; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B"));
        // Due: thinking's loops weigh 2+2+1+1+1+1; a roll of 2 is B_3. Then the next gap.
        now += 1; draws.values = {2, 0}; finishSequence(player);
        QCOMPARE(player.sequence(), QString("Think/Nomal/B_3")); QCOMPARE(player.frameIndex(), 0);
        QCOMPARE(player.state(), QString("thinking")); QCOMPARE(player.phase(), QString("loop"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B")); // One pass only.
        // Never the same alternate twice in a row: without B_3 (2,1,1,1,1) a roll of 2 is B_4.
        now += 6000; draws.values = {2, 0}; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B_4"));
        // Subtle waits 10 to 18 s and never draws the playful tier: the highest roll is B_5.
        draws.values = {0}; activity.setStyle(pet::ActivityStyle::Subtle); // A new pace starts from now.
        finishSequence(player); now += 9999; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B"));
        now += 1; draws.values = {99, 0}; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B_5"));
        // Playful reaches the happy loops with the same roll.
        draws.values = {0}; activity.setStyle(pet::ActivityStyle::Playful); finishSequence(player);
        now += 6000; draws.values = {99, 0}; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Happy/B_4"));
        // Classic varies nothing, ever, and draws nothing.
        activity.setStyle(pet::ActivityStyle::Classic); QVERIFY(!player.continuity());
        finishSequence(player); now += 3600000; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B"));
        QCOMPARE(draws.unexpected, 0);
    }
    void activityReactionsShareTheClock() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        pet::Activity activity(player); Draws draws; qint64 now = 1000000;
        activity.setRandom(draws.random()); activity.setClock([&] { return now; });
        draws.values = {0}; player.select("thinking", true); finishSequence(player); // Due in 6 s.
        // Not due: the usual end, and no welcome.
        player.select("working"); QCOMPARE(player.sequence(), QString("Think/Nomal/C"));
        finishSequence(player); finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal"));
        // Due: the exit reaction spends the opportunity (next gap drawn)...
        player.select("thinking", true); finishSequence(player);
        now += 6000; draws.values = {0}; player.select("working");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("Think/Happy/C_2"));
        // ...so working's welcome does not follow: one reaction per transition.
        finishSequence(player); finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal"));
        // Straight in from thinking, only the welcome can play, for one pass.
        player.select("thinking", true); finishSequence(player);
        now += 6000; draws.values = {0}; player.select("working", true);
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/Happy/B"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal"));
        // Subtle never reacts, however long it waits.
        draws.values = {0}; activity.setStyle(pet::ActivityStyle::Subtle);
        player.select("thinking", true); finishSequence(player);
        now += 3600000; player.select("working"); QCOMPARE(player.sequence(), QString("Think/Nomal/C"));
        finishSequence(player); finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal"));
        QCOMPARE(draws.unexpected, 0);
    }
    void classicActivityIsTodaysPlayback() {
        // The same requests show the same sequences with a Classic pace as with no pacing at all.
        auto script = [](pet::Player &player) {
            QStringList shown;
            auto note = [&] { shown << player.state() + " " + player.phase() + " " + player.sequence(); };
            auto step = [&](int passes) { for (int i = 0; i < passes; ++i) { finishSequence(player); note(); } };
            player.select("reading", true); note(); step(3);
            player.select("thinking"); note(); step(3);
            player.select("working"); note(); step(3);
            player.select("reading"); note(); step(3);
            player.select("working"); player.select("reading"); note(); step(3);
            return shown;
        };
        pet::Player plain; plain.setPaused(true); plain.setRandom([](int) { return 0; });
        pet::Player classic; classic.setPaused(true); classic.setRandom([](int) { return 0; });
        pet::Activity activity(classic); Draws draws; qint64 now = 0;
        activity.setRandom(draws.random()); activity.setClock([&] { return now += 60000; }); // Always due.
        activity.setStyle(pet::ActivityStyle::Classic);
        const auto expected = script(plain);
        QCOMPARE(script(classic), expected);
        QCOMPARE(draws.unexpected, 0);
        QVERIFY(expected.contains("reading end WORK/Study/C_Nomal"));
        QVERIFY(expected.contains("thinking start Think/Nomal/A"));
        // Playful, the same requests stay at the desk.
        pet::Player playful; playful.setPaused(true); playful.setRandom([](int) { return 0; });
        pet::Activity paced(playful); paced.setRandom([](int) { return 0; }); paced.setClock([] { return qint64(0); });
        const auto desk = script(playful);
        QVERIFY(desk.contains("reading linger WORK/Study/B_4_Nomal"));
        QVERIFY(desk.contains("reading handover WORK/Desk/reading_to_working"));
    }
    void activityStyleChangesMidDesk() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        pet::Activity activity(player); activity.setRandom([](int) { return 0; }); activity.setClock([] { return qint64(0); });
        player.select("reading", true); finishSequence(player); player.select("thinking");
        QCOMPARE(player.phase(), QString("linger"));
        // Switched to Classic mid-linger, the next change plays the usual end at once...
        activity.setStyle(pet::ActivityStyle::Classic); player.select("reading");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.requestedState(), QString("reading"));
        for (int i = 0; i < 4 && !(player.state() == "reading" && player.phase() == "loop"); ++i) finishSequence(player);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("loop"));
        // ...and a handover in progress still lands where it was going.
        activity.setStyle(pet::ActivityStyle::Playful); player.select("working");
        QCOMPARE(player.phase(), QString("handover"));
        activity.setStyle(pet::ActivityStyle::Classic); finishSequence(player);
        QCOMPARE(player.state(), QString("working")); QCOMPARE(player.phase(), QString("loop"));
    }
    void activityWithoutArtIsClassic() {
        // A catalog without the section: a Playful pace has nothing to vary or react with.
        QTemporaryDir directory; auto catalog = fixture(directory.path()); writeCatalog(directory.path(), catalog);
        pet::Player player(nullptr, directory.path()); player.setPaused(true);
        pet::Activity activity(player); Draws draws; qint64 now = 0;
        activity.setRandom(draws.random()); activity.setClock([&] { return now += 60000; });
        player.select("working", true);
        for (int i = 0; i < 5; ++i) { finishSequence(player); QCOMPARE(player.sequence(), QString("work")); }
        QCOMPARE(draws.unexpected, 0);
    }
    void moodArtReplacesChoices() {
        pet::Player player; player.setPaused(true); Draws draws; player.setRandom(draws.random());
        // A happy idle loop picks up its art on the next pass, without drawing among the plain variants.
        player.setMood("happy"); finishSequence(player);
        QCOMPARE(player.sequence(), QString("Default/Happy/1"));
        player.select("fidget_aside", true); QCOMPARE(player.sequence(), QString("IDEL/aside/Happy/A"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("IDEL/aside/Happy/B"));
        // A held state keeps what it drew; the new mood shows the next time it is entered.
        player.setMood("poor"); finishSequence(player); QCOMPARE(player.sequence(), QString("IDEL/aside/Happy/B"));
        player.select("fidget_yawn", true); QCOMPARE(player.sequence(), QString("IDEL/yawning/PoorCondition"));
        // States without mood art, and moods the catalog lacks, play the usual choices.
        draws.values = {0}; player.select("fidget_boring", true); QCOMPARE(player.sequence(), QString("IDEL/Boring/A_Nomal"));
        QVERIFY(!player.hasMood("happy", "fidget_boring")); QVERIFY(player.hasMood("poor", "idle"));
        player.setMood("ill"); draws.values = {2}; player.select("idle", true);
        QCOMPARE(player.sequence(), QString("Default/Nomal/2"));
        // With variants off a mood still shows, through its first choice.
        player.setVariants(false); player.setMood("poor"); finishSequence(player);
        QCOMPARE(player.sequence(), QString("Default/PoorCondition/1"));
        player.setMood({}); finishSequence(player); QCOMPARE(player.sequence(), QString("Default/Nomal/1"));
        QCOMPARE(draws.unexpected, 0); QVERIFY(draws.values.isEmpty());
    }
    void moodFollowsTurnsAndErrors() {
        pet::Player player; player.setPaused(true);
        pet::Mood mood(player); qint64 now = 1000000;
        QCOMPARE(mood.setting(), pet::MoodSetting::Full); QCOMPARE(mood.score(now), pet::Mood::neutral);
        // A streak gains more with each turn: 5, 6, 7, then 8 crosses into happy.
        for (int i = 0; i < 3; ++i) mood.finished(now);
        QCOMPARE(mood.score(now), 68); QCOMPARE(mood.level(), QString());
        mood.finished(now); QCOMPARE(mood.score(now), 76);
        QCOMPARE(mood.level(), QString("happy")); QCOMPARE(player.mood(), QString("happy"));
        // It fades a point every 30 s, and stays happy down to 60, below the 70 it took to get there.
        now += 16 * pet::Mood::recoveryMs + 29999; mood.refresh(now);
        QCOMPARE(mood.score(now), 60); QCOMPARE(mood.level(), QString("happy"));
        now += 1; mood.refresh(now); QCOMPARE(mood.score(now), 59); QCOMPARE(mood.level(), QString());
        QCOMPARE(player.mood(), QString());
        // Errors end a streak and cost 12 each; two in a row from neutral droop.
        now += 3600000; mood.refresh(now); QCOMPARE(mood.score(now), pet::Mood::neutral);
        mood.failed(now); QCOMPARE(mood.level(), QString()); mood.failed(now);
        QCOMPARE(mood.score(now), 26); QCOMPARE(mood.level(), QString("poor"));
        mood.finished(now); QCOMPARE(mood.score(now), 31); QCOMPARE(mood.level(), QString("poor")); // Back to a streak of one.
        // Poorly lasts until the score passes 40.
        now += 9 * pet::Mood::recoveryMs; mood.refresh(now); QCOMPARE(mood.score(now), 40); QCOMPARE(mood.level(), QString("poor"));
        now += pet::Mood::recoveryMs; mood.refresh(now); QCOMPARE(mood.level(), QString());
        // Cheerful never droops; Off is always neutral.
        mood.failed(now); mood.failed(now); QCOMPARE(mood.level(), QString("poor"));
        mood.setSetting(pet::MoodSetting::Cheerful); QCOMPARE(mood.level(), QString()); QCOMPARE(player.mood(), QString());
        for (int i = 0; i < 10; ++i) mood.finished(now);
        QCOMPARE(mood.score(now), 100); QCOMPARE(mood.level(), QString("happy"));
        mood.setSetting(pet::MoodSetting::Off); QCOMPARE(mood.level(), QString()); QCOMPARE(player.mood(), QString());
        mood.finished(now); QCOMPARE(mood.level(), QString());
    }
    void celebrationsAndTreats() {
        pet::Player player; player.setPaused(true);
        pet::Mood mood(player); Draws draws; mood.setRandom(draws.random()); qint64 now = 1000000;
        QSignalSpy counted(&mood, &pet::Mood::counted);
        // "turn_finished" weighs 2, the two cheers 1 each.
        const QList<QPair<int, QString>> expected{{0, "turn_finished"}, {1, "turn_finished"}, {2, "cheer_shining"}, {3, "cheer_shy"}};
        for (const auto &[draw, state] : expected) { draws.values = {draw}; QCOMPARE(mood.celebrate(), state); }
        // Twenty finished turns without a long break earn a snack, eaten by the next celebration.
        for (int i = 0; i < 19; ++i) { mood.finished(now); now += 60000; }
        QCOMPARE(mood.treat(), QString());
        mood.finished(now); QCOMPARE(mood.treat(), QString("snack"));
        draws.values = {1}; QCOMPARE(mood.celebrate(), QString("snack_thirsty")); QCOMPARE(mood.treat(), QString());
        // A break of more than half an hour starts the count again.
        for (int i = 0; i < 19; ++i) { mood.finished(now); now += 60000; }
        now += pet::Mood::breakMs; mood.finished(now); QCOMPARE(mood.treat(), QString());
        // Every hundredth turn is a milestone, and it outranks a snack.
        QCOMPARE(mood.turns(), 40); QCOMPARE(counted.size(), 40);
        mood.setTurns(98); mood.finished(now); QCOMPARE(mood.treat(), QString());
        mood.finished(now); QCOMPARE(mood.turns(), 100); QCOMPARE(mood.treat(), QString("milestone"));
        QCOMPARE(mood.celebrate(), QString("milestone")); // A pool of one draws nothing.
        QCOMPARE(draws.unexpected, 0);
    }
    void monitorFeedsTheMood() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window); auto &player = window.player(); player.setPaused(true);
        player.setRandom([](int) { return 0; }); window.ambient().setRandom([](int) { return 0; });
        Draws draws; window.mood().setRandom(draws.random());
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString kind, QString session = "s1") {
            ++seq; return pet::Event{"claude", session, QString::number(seq), kind, {}, {}, "/work/abc-web", {}, now + seq, {}};
        };
        QVERIFY(monitor.apply(event("prompt"), now + seq));
        draws.values = {2}; const auto finished = event("turn_finished");
        QVERIFY(monitor.apply(finished, now + seq));
        // Thinking plays its end first, then the drawn celebration.
        QCOMPARE(player.requestedState(), QString("cheer_shining")); QCOMPARE(window.mood().turns(), 1);
        finishSequence(player); QCOMPARE(player.state(), QString("cheer_shining"));
        // The periodic update leaves the celebration alone, and a duplicate event counts for nothing.
        monitor.update(now + seq); QCOMPARE(player.state(), QString("cheer_shining"));
        QVERIFY(!monitor.apply(finished, now + seq)); QCOMPARE(window.mood().turns(), 1);
        // Another session finishing while this one waits on the user raises the mood without a celebration.
        QVERIFY(monitor.apply(event("attention"), now + seq));
        QVERIFY(monitor.apply(event("prompt", "s2"), now + seq));
        QVERIFY(monitor.apply(event("turn_finished", "s2"), now + seq));
        QCOMPARE(window.mood().turns(), 2); QCOMPARE(player.requestedState(), QString("needs_input"));
        // Tool errors lower it; two after a neutral start droop the idle pet.
        const auto before = window.mood().score(now + seq);
        QVERIFY(monitor.apply(event("error"), now + seq)); QVERIFY(monitor.apply(event("error"), now + seq));
        QCOMPARE(window.mood().score(now + seq), before - 2 * pet::Mood::errorLoss);
        QCOMPARE(draws.unexpected, 0);
    }
    void moodPreference() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path); QCOMPARE(window.moodLevel(), int(pet::Preferences::MoodFull));
            window.showSettings(); auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
            QComboBox *combo = nullptr;
            for (auto *box : dialog->findChildren<QComboBox*>()) if (box->accessibleName() == "Mood") combo = box;
            QVERIFY(combo); QCOMPARE(combo->count(), 3); QCOMPARE(combo->currentIndex(), 2);
            combo->setCurrentIndex(1); QCOMPARE(window.mood().setting(), pet::MoodSetting::Cheerful);
            window.mood().finished(1000); window.mood().finished(2000); // Counted turns are saved too.
            QVERIFY(window.savePreferences()); dialog->close();
        }
        const auto saved = pet::PreferencesStore(path).load();
        QCOMPARE(saved.mood, int(pet::Preferences::MoodCheerful)); QCOMPARE(saved.turns, 2);
        pet::PetWindow restored(nullptr, path);
        QCOMPARE(restored.mood().setting(), pet::MoodSetting::Cheerful); QCOMPARE(restored.mood().turns(), 2);
        restored.setMoodLevel(99); QCOMPARE(restored.moodLevel(), 2);
        // Older files have neither key; bad values are refused like any other and preserved.
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true})"); file.close();
        const auto legacy = pet::PreferencesStore(path).load();
        QCOMPARE(legacy.mood, int(pet::Preferences::MoodFull)); QCOMPARE(legacy.turns, 0);
        for (const auto *broken : {R"({"version":1,"size":200,"on_top":true,"mood":3})",
                                   R"({"version":1,"size":200,"on_top":true,"turns":-1})",
                                   R"({"version":1,"size":200,"on_top":true,"turns":1.5})"}) {
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write(broken); file.close();
            pet::PreferencesStore invalid(path); QCOMPARE(invalid.load().turns, 0); QVERIFY(!invalid.save(pet::Preferences{}));
        }
    }
    void malformedMoodsAndReactions() {
        QTemporaryDir fixtures; auto base = fixture(fixtures.path()); // "idle" and "work" sequences exist.
        auto choice = [](QJsonArray sequences, int weight = 1) { return QJsonObject{{"sequences", sequences}, {"weight", weight}}; };
        auto moods = [&](QJsonObject value) { auto catalog = base; catalog["moods"] = value; return catalog; };
        QVERIFY(loads(moods({{"happy", QJsonObject{{"idle", QJsonArray{choice({"work"})}}}},
                             {"poor", QJsonObject{{"working", QJsonArray{choice({"idle"}, 2)}}}}})));
        QVERIFY(!loads(moods({{"ill", QJsonObject{{"idle", QJsonArray{choice({"work"})}}}}})));
        QVERIFY(!loads(moods({{"happy", QJsonObject{{"nobody", QJsonArray{choice({"work"})}}}}})));
        QVERIFY(!loads(moods({{"happy", QJsonObject{{"idle", QJsonArray{}}}}})));
        QVERIFY(!loads(moods({{"happy", QJsonObject{{"idle", QJsonArray{choice({"missing"})}}}}})));
        QVERIFY(!loads(moods({{"happy", QJsonObject{{"idle", QJsonArray{choice({"work", "idle"})}}}}})));
        QVERIFY(!loads(moods({{"happy", QJsonObject{{"idle", QJsonArray{choice({"work"}, 0)}}}}})));
        // A reaction plays states that end by themselves and return to idle.
        auto reacting = base; auto playback = reacting["playback"].toObject();
        playback["working"] = QJsonObject{{"mode", "once"}, {"after", "idle"}}; reacting["playback"] = playback;
        auto reactions = [&](QJsonArray pool, QJsonObject catalog) {
            catalog["reactions"] = QJsonObject{{"turn_finished", pool}}; return catalog;
        };
        auto reaction = [](QString state, int weight = 1) { return QJsonObject{{"state", state}, {"weight", weight}}; };
        QVERIFY(loads(reactions({reaction("working", 3)}, reacting)));
        QVERIFY(!loads(reactions({}, reacting)));
        QVERIFY(!loads(reactions({reaction("nobody")}, reacting)));
        QVERIFY(!loads(reactions({reaction("idle")}, reacting)));
        QVERIFY(!loads(reactions({reaction("working", 0)}, reacting)));
        QVERIFY(!loads(reactions({reaction("working")}, base))); // A loop never ends by itself.
    }
    void touchRegionsAndHolds() {
        pet::Player player; player.setPaused(true);
        // Hit boxes are in the artwork's 500-unit space and follow the pet's size.
        for (const int side : {160, 240, 320}) {
            auto at = [&](double x, double y) { return player.touchAt(QPointF(x * side / 500, y * side / 500), side); };
            QCOMPARE(at(250, 80), QString("touch_head"));
            QCOMPARE(at(170, 150), QString("pinch")); // The cheek sits inside the head and wins.
            QCOMPARE(at(250, 270), QString("touch_body"));
            QCOMPARE(at(250, 460), QString()); QCOMPARE(at(20, 20), QString()); // Feet and empty margins.
        }
        // A held touch keeps playing while sessions move on, then ends and shows the latest request.
        player.select("working", true); finishSequence(player);
        player.hold("touch_head"); QVERIFY(player.held()); QCOMPARE(player.sequence(), QString("Touch_Head/A_Nomal"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("Touch_Head/B_Nomal"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("Touch_Head/B_Nomal"));
        player.select("needs_input", true); QCOMPARE(player.state(), QString("touch_head"));
        QCOMPARE(player.requestedState(), QString("touch_head"));
        player.release(); QVERIFY(!player.held()); QCOMPARE(player.sequence(), QString("Touch_Head/C_Nomal"));
        finishSequence(player); QCOMPARE(player.state(), QString("needs_input"));
        // Moving while petting turns into a drag at once; the request from before the touch survives both.
        player.select("idle", true);
        player.hold("pinch"); player.beginDrag(); QCOMPARE(player.state(), QString("dragging")); QVERIFY(player.isDragging());
        player.select("thinking"); player.endDrag(); finishSequence(player); QCOMPARE(player.state(), QString("thinking"));
        // A reaction never comes back after a one-shot that returns to what was showing.
        player.select("edge_left", true); player.select("tool_error", true); finishSequence(player);
        QCOMPARE(player.state(), QString("idle"));
        player.release(); QVERIFY(!player.held()); // Releasing nothing is harmless.
    }
    void gestureGeometry() {
        using namespace pet::touch;
        // Speed over the last 120 ms of a drag; a pause before letting go reads as still.
        QCOMPARE(velocity({}), QPointF());
        QCOMPARE(velocity({{0, {0, 0}}, {40, {40, 0}}, {80, {80, -20}}}), QPointF(1000, -250));
        QCOMPARE(velocity({{0, {0, 0}}, {40, {400, 0}}, {200, {400, 0}}}), QPointF());
        QCOMPARE(velocity({{0, {0, 0}}, {40, {400, 0}}, {80, {400, 0}}, {120, {400, 0}}}), QPointF(0, 0));
        // An edge counts once a quarter of the window is past an outer side of its screen, where the artwork reaches it.
        const QVector<QRect> one{QRect(0, 0, 1000, 800)};
        QCOMPARE(pushedEdge(QRect(-59, 100, 240, 240), one), Edge::None);
        QCOMPARE(pushedEdge(QRect(-30, 100, 240, 240), one), Edge::None); // Artwork still clear of the edge.
        QCOMPARE(pushedEdge(QRect(-60, 100, 240, 240), one), Edge::Left);
        QCOMPARE(pushedEdge(QRect(819, 100, 240, 240), one), Edge::None);
        QCOMPARE(pushedEdge(QRect(820, 100, 240, 240), one), Edge::Right);
        QCOMPARE(pushedEdge(QRect(400, -100, 240, 240), one), Edge::None); // Top and bottom do not hide.
        const QVector<QRect> two{QRect(0, 0, 1000, 800), QRect(1000, 0, 1000, 800)};
        QCOMPARE(pushedEdge(QRect(820, 100, 240, 240), two), Edge::None); // The next screen goes on.
        QCOMPARE(pushedEdge(QRect(1820, 100, 240, 240), two), Edge::Right);
        QCOMPARE(pushedEdge(QRect(-60, 100, 240, 240), two), Edge::Left);
        // Hiding puts the screen edge through the artwork at the catalog's line, kept on-screen vertically.
        QCOMPARE(hidePosition(Edge::Left, QRect(-60, 700, 240, 240), one, 219, 500), QPoint(-105, 560));
        QCOMPARE(hidePosition(Edge::Right, QRect(800, 100, 240, 240), one, 281, 500), QPoint(1000 - 135, 100));
        QCOMPARE(hidePosition(Edge::Right, QRect(1800, 100, 240, 240), two, 281, 500), QPoint(2000 - 135, 100));
        // A thrown pet falls onto the bottom of its screen, bouncing off the sides on the way.
        Flight drop({100, 0}, {-2000, -500}, QRect(0, 0, 1000, 800), {240, 240});
        int steps = 0; bool bounced = false;
        while (drop.step(16)) { ++steps; bounced = bounced || drop.position().x() == 0; QVERIFY(drop.position().y() >= 0); }
        QVERIFY(drop.landed()); QVERIFY(bounced); QCOMPARE(drop.position().y(), 560); QVERIFY(steps < 100);
        QVERIFY(drop.position().x() >= 0); QVERIFY(!drop.step(16));
        // On the floor already it lands at once; one that cannot move still ends in time.
        Flight floor({300, 560}, {1500, 0}, QRect(0, 0, 1000, 800), {240, 240}); QVERIFY(!floor.step(16));
        Flight stuck({300, 0}, {0, 0}, QRect(0, 0, 1000, 100000), {240, 240});
        int ticks = 0; while (stuck.step(100)) ++ticks;
        QCOMPARE(ticks, int(Flight::maxMs / 100) - 1);
    }
    void nativePixelsMapToLogical() {
        // XWayland at 1.75: Qt keeps each screen's origin and scales the rest, so the X server's
        // 3391,875 is the pet's 3200,500 on a screen starting at 2945,0 (measured on KDE).
        QCOMPARE(pet::PetWindow::fromNative({3391, 875}, {2945, 0}, 1.75), QPoint(3200, 500));
        QCOMPARE(pet::PetWindow::fromNative({875, 488}, {0, 49}, 1.75), QPoint(500, 300));
        QCOMPARE(pet::PetWindow::fromNative({2770, 875}, {2945, 0}, 1.75), QPoint(2845, 500)); // Hiding past its left edge.
        QCOMPARE(pet::PetWindow::fromNative({400, 300}, {0, 0}, 1), QPoint(400, 300));
    }
    void windowTouchReactions() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window); auto &player = window.player(); player.setPaused(true);
        window.ambient().setLevel(pet::AmbientLevel::Off);
        const auto area = pet::touch::areaFor(window.geometry(), window.screenAreas());
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString kind) {
            ++seq; return pet::Event{"claude", "s1", QString::number(seq), kind, {}, {}, "/work/abc-web", {}, now + seq, {}};
        };
        playOut(player); QCOMPARE(player.state(), QString("idle"));
        // Thrown: it falls to the bottom of the screen while sessions wait, then lands and shows them.
        window.move(area.left() + 100, area.top());
        player.beginDrag(); window.letGo({1500, 0});
        QVERIFY(window.flying()); QCOMPARE(player.state(), QString("fall_right")); QVERIFY(player.held());
        QVERIFY(monitor.apply(event("prompt"), now + seq)); QCOMPARE(player.state(), QString("fall_right"));
        QTRY_VERIFY_WITH_TIMEOUT(!window.flying(), 4000);
        QCOMPARE(window.pos().y(), area.bottom() + 1 - window.height()); QVERIFY(window.pos().x() > area.left() + 100);
        QCOMPARE(player.sequence(), QString("MOVE/fall.right/C_Nomal"));
        finishSequence(player); QCOMPARE(player.state(), QString("thinking"));
        // Pushed past an edge while busy, it just comes back into view.
        player.beginDrag(); window.move(area.left() - window.width() / 4, area.top() + 50); window.letGo({});
        QVERIFY(!window.hiding()); QCOMPARE(window.pos().x(), area.left());
        playOut(player); QVERIFY(monitor.apply(event("interrupt"), now + seq)); playOut(player);
        QCOMPARE(player.state(), QString("idle"));
        // Idle, it hides there once the drag's end has played, and the monitor's periodic update leaves it hiding.
        player.beginDrag(); window.move(area.left() - window.width() / 4, area.top() + 50); window.letGo({});
        QCOMPARE(player.state(), QString("dragging")); QVERIFY(!window.hiding());
        // It slides to its hiding place first, and only then does the hide play.
        QVERIFY(window.sliding()); QCOMPARE(player.requestedState(), QString("idle"));
        QTRY_COMPARE_WITH_TIMEOUT(player.requestedState(), QString("edge_left"), 2000); QVERIFY(!window.sliding());
        monitor.update(now + seq); QCOMPARE(player.requestedState(), QString("edge_left"));
        finishSequence(player); QCOMPARE(player.state(), QString("edge_left")); QVERIFY(window.hiding());
        QCOMPARE(window.pos(), QPoint(area.left() - qRound(219.0 * window.width() / 500), area.top() + 50));
        monitor.update(now + seq); QCOMPARE(player.requestedState(), QString("edge_left"));
        window.constrainPosition(); QCOMPARE(window.pos().x(), area.left() - qRound(219.0 * window.width() / 500));
        // Session activity brings it out through its end, back into view.
        QVERIFY(monitor.apply(event("prompt"), now + seq)); QCOMPARE(player.phase(), QString("end"));
        QVERIFY(window.hiding()); finishSequence(player);
        QCOMPARE(player.state(), QString("thinking")); QVERIFY(!window.hiding()); QCOMPARE(window.pos().x(), area.left());
        playOut(player); QVERIFY(monitor.apply(event("interrupt"), now + seq)); playOut(player);
        // The right edge too; dragging it back out ends the hiding without fighting the drag.
        player.beginDrag(); window.move(area.right() + 1 - window.width() + window.width() / 4, area.top()); window.letGo({});
        QTRY_COMPARE_WITH_TIMEOUT(player.requestedState(), QString("edge_right"), 2000); finishSequence(player); QCOMPARE(window.edge(), pet::touch::Edge::Right);
        QCOMPARE(window.pos().x(), area.right() + 1 - qRound(281.0 * window.width() / 500));
        player.beginDrag(); QVERIFY(!window.hiding()); QCOMPARE(player.requestedState(), QString("dragging"));
        window.letGo({}); QCOMPARE(player.requestedState(), QString("idle")); playOut(player); QVERIFY(!window.hiding());
        // Recovering the position brings it out too, so nothing puts it back behind the edge later.
        player.beginDrag(); window.move(area.left() - window.width() / 4, area.top()); window.letGo({});
        QTRY_COMPARE_WITH_TIMEOUT(player.requestedState(), QString("edge_left"), 2000); finishSequence(player);
        QVERIFY(window.hiding()); window.recover(); QVERIFY(!window.hiding()); QCOMPARE(player.state(), QString("idle"));
        window.constrainPosition(); QVERIFY(area.contains(window.geometry()));
        // Off, nothing falls or hides.
        window.setTouchEnabled(false);
        player.beginDrag(); window.letGo({-3000, 0}); QVERIFY(!window.flying());
        player.beginDrag(); window.move(area.left() - window.width() / 4, area.top()); window.letGo({});
        QVERIFY(!window.sliding()); playOut(player); QVERIFY(!window.hiding()); QCOMPARE(player.state(), QString("idle"));
    }
    void holdToPet() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto &player = window.player(); player.setPaused(true); window.ambient().setLevel(pet::AmbientLevel::Off);
        playOut(player);
        QSignalSpy sessions(&window, &pet::PetWindow::sessionsRequested);
        const QPoint head(window.width() / 2, window.width() * 80 / 500);
        // A short press is still a click.
        QTest::mousePress(&window, Qt::LeftButton, {}, head); QTest::mouseRelease(&window, Qt::LeftButton, {}, head, 50);
        QTRY_COMPARE(sessions.size(), 1); QCOMPARE(player.state(), QString("idle"));
        // Held still, it pets; let go, it finishes and is not a click.
        QTest::mousePress(&window, Qt::LeftButton, {}, head);
        QTRY_COMPARE_WITH_TIMEOUT(player.state(), QString("touch_head"), 2000);
        QTest::mouseRelease(&window, Qt::LeftButton, {}, head);
        QTRY_VERIFY(!player.held()); QCOMPARE(player.phase(), QString("end"));
        QTest::qWait(300); QCOMPARE(sessions.size(), 1);
        finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        // Off the artwork, or with touch off, holding does nothing.
        window.setTouchEnabled(false);
        QTest::mousePress(&window, Qt::LeftButton, {}, head); QTest::qWait(700);
        QCOMPARE(player.state(), QString("idle")); QTest::mouseRelease(&window, Qt::LeftButton, {}, head);
    }
    void touchPatience() {
        using pet::touch::Patience;
        Patience patience;
        for (int i = 0; i < Patience::throwLimit - 1; ++i) QVERIFY(!patience.thrown(i * 100));
        QVERIFY(patience.thrown(400));
        patience.reset();
        for (int i = 0; i < 10; ++i) QVERIFY(!patience.thrown(i * Patience::throwWindowMs));
        patience.reset();
        QVERIFY(!patience.petting(true, 100));
        QVERIFY(!patience.petting(true, 100 + Patience::petLimitMs - 1));
        QVERIFY(patience.petting(true, 100 + Patience::petLimitMs));
        QVERIFY(!patience.petting(false, 20000));
        QVERIFY(!patience.petting(true, 20001));
        patience.reset();
        QVERIFY(!patience.petting(true, 50000));
        QVERIFY(!patience.dragging(true, 50000));
        QVERIFY(!patience.dragging(true, 50000 + Patience::dragLimitMs - 1));
        QVERIFY(patience.dragging(true, 50000 + Patience::dragLimitMs));
        QVERIFY(!patience.dragging(false, 70000));
        QVERIFY(!patience.dragging(true, 70001));
        patience.reset();
        QVERIFY(!patience.dragging(true, 100000));
    }
    void angryQuitAfterThrows() {
        pet::PetWindow window(nullptr, {}, false);
        pet::Monitor monitor(window);
        auto &player = window.player();
        player.setPaused(true);
        QSignalSpy quitting(&window, &pet::PetWindow::quitRequested);
        for (int i = 0; i < pet::touch::Patience::throwLimit; ++i) {
            player.beginDrag();
            window.letGo({1200, 0});
            QCOMPARE(window.quitting(), i == pet::touch::Patience::throwLimit - 1);
        }
        QCOMPARE(quitting.size(), 1);
        QVERIFY(!monitor.active());
        QVERIFY(!window.flying()); QVERIFY(!player.held());
        QCOMPARE(player.state(), QString("angry"));
        auto *note = window.findChild<pet::NoteBubble*>(); QVERIFY(note); QVERIFY(note->isVisible());
        QCOMPARE(note->text(), QString("Stop throwing me! I'm leaving!"));
        window.recover(); window.letGo({1200, 0});
        QCOMPARE(player.state(), QString("angry"));
        finishSequence(player);
        QCOMPARE(player.state(), QString("angry")); // The complaint stays readable before departure.
        QTRY_COMPARE_WITH_TIMEOUT(player.state(), QString("closing_angry"), 3000);
        QCOMPARE(player.sequence(), QString("Shutdown/PoorCondition"));
        finishSequence(player); QVERIFY(player.stopped());
        // Ordinary quit keeps its original animation, and disabled touch never counts throws.
        pet::PetWindow normal(nullptr, {}, false);
        normal.setTouchEnabled(false);
        for (int i = 0; i < 10; ++i) normal.letGo({1200, 0});
        QVERIFY(!normal.quitting());
        normal.requestQuit();
        QVERIFY(!normal.findChild<pet::NoteBubble*>()->isVisible());
        QCOMPARE(normal.player().state(), QString("closing"));
        QCOMPARE(normal.player().sequence(), QString("Shutdown/Nomal_1"));
    }
    void angryQuitAfterLongHold_data() {
        QTest::addColumn<bool>("drag");
        QTest::newRow("pet-for-eight-seconds") << false;
        QTest::newRow("drag-for-fifteen-seconds") << true;
    }
    void angryQuitAfterLongHold() {
        QFETCH(bool, drag);
        pet::PetWindow window(nullptr, {}, false); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto &player = window.player(); player.setPaused(true); playOut(player);
        QSignalSpy quitting(&window, &pet::PetWindow::quitRequested);
        const QPoint head(window.width() / 2, window.width() * 80 / 500);
        QTest::mousePress(&window, Qt::LeftButton, {}, head);
        if (drag) QTest::mouseMove(&window, head + QPoint(30, 0));
        QTRY_COMPARE_WITH_TIMEOUT(player.state(), drag ? QString("dragging") : QString("touch_head"), 2000);
        const auto limit = drag ? pet::touch::Patience::dragLimitMs : pet::touch::Patience::petLimitMs;
        QElapsedTimer elapsed; elapsed.start();
        QTRY_VERIFY_WITH_TIMEOUT(window.quitting(), limit + 1000);
        QVERIFY(elapsed.elapsed() >= limit - 150); // Dragging must not use the shorter petting deadline.
        QCOMPARE(player.state(), QString("angry"));
        auto *note = window.findChild<pet::NoteBubble*>(); QVERIFY(note); QVERIFY(note->isVisible());
        QCOMPARE(note->text(), drag ? QString("Put me down! I'm leaving!")
                                   : QString("Too much petting! I need a break. Bye!"));
        QCOMPARE(quitting.size(), 1); QVERIFY(!player.held());
        QTest::mouseRelease(&window, Qt::LeftButton, {}, head);
        QCOMPARE(player.state(), QString("angry"));
    }
    void touchPreference() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path); QVERIFY(window.touchEnabled());
            window.showSettings(); auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
            QCheckBox *box = nullptr;
            for (auto *check : dialog->findChildren<QCheckBox*>()) if (check->accessibleName() == "Touch reactions") box = check;
            QVERIFY(box); QVERIFY(box->isChecked());
            box->setChecked(false); QVERIFY(!window.touchEnabled());
            QVERIFY(window.savePreferences()); dialog->close();
        }
        QVERIFY(!pet::PreferencesStore(path).load().touch);
        pet::PetWindow restored(nullptr, path); QVERIFY(!restored.touchEnabled());
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true})"); file.close();
        QVERIFY(pet::PreferencesStore(path).load().touch); // Older files have no key.
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write(R"({"version":1,"size":200,"on_top":true,"touch":1})"); file.close();
        pet::PreferencesStore invalid(path); QVERIFY(invalid.load().touch); QVERIFY(!invalid.save(pet::Preferences{}));
    }
    void malformedTouch() {
        QTemporaryDir fixtures; auto base = fixture(fixtures.path()); // "idle" and "work" sequences exist.
        // A held reaction needs a phased state with a loop only a release ends.
        auto states = base["states"].toObject(); states["held"] = QJsonArray{"work", "work", "idle"};
        states["brief"] = QJsonArray{"work"}; base["states"] = states;
        auto playback = base["playback"].toObject();
        playback["held"] = QJsonObject{{"mode", "phased"}, {"after", "idle"}};
        playback["brief"] = QJsonObject{{"mode", "once"}, {"after", "idle"}}; base["playback"] = playback;
        auto with = [&](QJsonObject touch) { auto catalog = base; catalog["touch"] = touch; return catalog; };
        auto region = [](QString state, QJsonArray rect) { return QJsonObject{{"state", state}, {"rect", rect}}; };
        const QJsonObject good{{"scale", 500}, {"regions", QJsonArray{region("held", {10, 10, 100, 100})}},
                               {"fall", QJsonObject{{"left", "held"}}},
                               {"edge", QJsonObject{{"right", QJsonObject{{"state", "held"}, {"at", 250}}}}}};
        QVERIFY(loads(base)); QVERIFY(loads(with(good))); QVERIFY(loads(with({{"scale", 500}})));
        auto broken = [&](const QString &key, const QJsonValue &value) { auto touch = good; touch[key] = value; return with(touch); };
        QVERIFY(!loads(broken("scale", 0)));
        QVERIFY(!loads(broken("regions", QJsonArray{region("brief", {10, 10, 100, 100})}))); // Ends by itself.
        QVERIFY(!loads(broken("regions", QJsonArray{region("working", {10, 10, 100, 100})}))); // A loop has no end.
        QVERIFY(!loads(broken("regions", QJsonArray{region("nobody", {10, 10, 100, 100})})));
        QVERIFY(!loads(broken("regions", QJsonArray{region("held", {450, 10, 100, 100})}))); // Outside the artwork.
        QVERIFY(!loads(broken("regions", QJsonArray{region("held", {10, 10, 0, 100})})));
        QVERIFY(!loads(broken("regions", QJsonArray{region("held", {10, 10, 100})})));
        QVERIFY(!loads(broken("fall", QJsonObject{{"right", "idle"}})));
        QVERIFY(!loads(broken("edge", QJsonObject{{"left", QJsonObject{{"state", "held"}, {"at", 500}}}})));
        QVERIFY(!loads(broken("edge", QJsonObject{{"left", QJsonObject{{"state", "brief"}, {"at", 200}}}})));
    }
    void wanderGeometry() {
        using namespace pet::wander;
        const QRect area(0, 0, 1000, 800);
        // Distances are in the artwork's units: a 250-pixel window is 500 units wide, so a pixel is two.
        const auto d = distances(QRect(100, 50, 250, 250), area, 500);
        QCOMPARE(d.left, 200.0); QCOMPARE(d.top, 100.0); QCOMPARE(d.right, 1300.0); QCOMPARE(d.bottom, 1000.0);
        QCOMPARE(distances(QRect(-20, 0, 250, 250), area, 500).left, -40.0); // Past the edge.
        auto at = [&](int x, int y = 300) { return distances(QRect(x, y, 250, 250), area, 500); };
        // A walk starts with room on its side and stops once it is down to what it keeps.
        pet::Move walk; walk.speed = {-112, 0}; walk.room.left = 200; walk.keep.left = 100;
        QVERIFY(fits(walk, at(100))); QVERIFY(!fits(walk, at(99)));
        QVERIFY(keeps(walk, at(51))); QVERIFY(!keeps(walk, at(50)));
        // A climb starts at its wall, with room above it.
        pet::Move climb; climb.speed = {0, -80}; climb.near.left = 100; climb.room.top = 200; climb.keep.top = 100;
        QVERIFY(fits(climb, at(0))); QVERIFY(fits(climb, at(-60))); QVERIFY(fits(climb, at(49))); QVERIFY(!fits(climb, at(50)));
        QVERIFY(!fits(climb, at(0, 99))); QVERIFY(fits(climb, at(0, 100)));
        QVERIFY(keeps(climb, at(0, 51))); QVERIFY(!keeps(climb, at(0, 50)));
        // Steps follow the pet's size and the time passed, at most 100 ms of it.
        QCOMPARE(step(walk, 250, 500, 100), QPointF(-5.6, 0));
        QCOMPARE(step(walk, 500, 500, 50), QPointF(-5.6, 0));
        QCOMPARE(step(walk, 250, 500, 5000), QPointF(-5.6, 0));
        QCOMPARE(step(climb, 250, 500, 100), QPointF(0, -4));
        QCOMPARE(step(walk, 250, 0, 100), QPointF());
    }
    void movesAreFidgetsTheWindowAllows() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        // Ending a move early plays its end on the spot, then idle.
        player.select("walk_left", true); QCOMPARE(player.phase(), QString("start"));
        player.finish(); QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("MOVE/walk.left/C_Nomal"));
        player.finish(); QCOMPARE(player.phase(), QString("end")); // Already ending.
        finishSequence(player); QCOMPARE(player.state(), QString("idle"));
        player.finish(); QCOMPARE(player.state(), QString("idle")); // Nothing to end.
        // Otherwise it walks its loop as many times as upstream's distance says.
        player.select("walk_left", true); finishSequence(player);
        for (int pass = 0; pass < 7; ++pass) { QCOMPARE(player.phase(), QString("loop")); finishSequence(player); }
        QCOMPARE(player.phase(), QString("end")); player.select("idle", true);
        // The catalog's moves, ported from vup.lps.
        const auto *climb = player.move("climb_up_left"); QVERIFY(climb);
        QCOMPARE(climb->wall, QString("left")); QCOMPARE(climb->at, 145); QCOMPARE(climb->speed, QPointF(0, -80));
        QCOMPARE(climb->near.left, 100.0); QCOMPARE(climb->room.top, 200.0); QCOMPARE(climb->keep.top, 100.0);
        QCOMPARE(climb->room.left, -1.0);
        QCOMPARE(player.move("climb_down_right")->at, 315); QCOMPARE(player.move("climb_down_right")->speed, QPointF(0, 80));
        QCOMPARE(player.move("trot_right")->mood, QString("happy")); QCOMPARE(player.move("trudge_left")->mood, QString("poor"));
        QCOMPARE(player.move("walk_right")->speed, QPointF(112, 0)); QVERIFY(!player.move("fidget_aside"));
        pet::Ambient ambient(player); Draws draws; qint64 now = 1000000;
        ambient.setRandom(draws.random()); ambient.setClock([&] { return now; });
        // Without a window to ask, a long idle spell draws only fidgets that stay put: aside 3, yawn 3,
        // boring 2 and squat 2, so the highest draw is a squat.
        draws.values = {0}; player.select("thinking", true); player.select("idle", true);
        now += 300000; draws.values = {5, 10}; finishSequence(player); QCOMPARE(player.state(), QString("fidget_squat"));
        // The window is asked about each move, and those it allows join the draw: squat just played, so
        // aside 3, yawn 3, boring 2 and walk_right 2 remain.
        QStringList asked;
        ambient.setMoveGate([&](const pet::Move &move) { asked.append(move.state); return move.state == "walk_right"; });
        draws.values = {0}; playOut(player);
        now += 90000; draws.values = {5, 8}; finishSequence(player);
        QCOMPARE(player.state(), QString("walk_right")); QVERIFY(ambient.resting()); QCOMPARE(asked.size(), 12);
        // Before four idle minutes nobody is asked.
        asked.clear(); draws.values = {0}; player.select("thinking", true); player.select("idle", true);
        now += 45000; draws.values = {5, 0}; finishSequence(player);
        QCOMPARE(player.state(), QString("fidget_aside")); QVERIFY(asked.isEmpty());
        QCOMPARE(draws.unexpected, 0);
    }
    void windowWalksAndClimbs() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        pet::Monitor monitor(window); auto &player = window.player(); player.setPaused(true);
        window.ambient().setLevel(pet::AmbientLevel::Off); window.setPetSize(250); // A pixel is two artwork units.
        playOut(player);
        const auto area = pet::touch::areaFor(window.geometry(), window.screenAreas());
        const int floor = area.bottom() + 1 - 250;
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString kind) {
            ++seq; return pet::Event{"claude", "s1", QString::number(seq), kind, {}, {}, "/work/abc-web", {}, now + seq, {}};
        };
        // A walk needs room on its side and the mood it was drawn for; a climb needs its wall close by.
        const auto &walk = *player.move("walk_left"), &trot = *player.move("trot_left"), &climb = *player.move("climb_up_left");
        window.move(area.left() + 100, floor);
        QVERIFY(window.canWander(walk)); QVERIFY(!window.canWander(trot)); QVERIFY(!window.canWander(climb));
        player.setMood("happy"); QVERIFY(window.canWander(trot)); player.setMood({});
        window.move(area.left() + 99, floor); QVERIFY(!window.canWander(walk));
        window.move(area.left() + 49, floor); QVERIFY(window.canWander(climb));
        QVERIFY(!window.canWander(*player.move("climb_down_left"))); // Already at the bottom.
        window.setWanderEnabled(false); QVERIFY(!window.canWander(climb)); window.setWanderEnabled(true);
        // Walking: the start plays on the spot, the loop travels, and it stops short of the edge to play its end.
        window.move(area.left() + 60, floor);
        player.select("walk_left", true); QVERIFY(window.walking()); QCOMPARE(player.phase(), QString("start"));
        QTest::qWait(100); QCOMPARE(window.pos().x(), area.left() + 60);
        finishSequence(player); QCOMPARE(player.phase(), QString("loop"));
        player.setPaused(false);
        QTRY_COMPARE_WITH_TIMEOUT(player.phase(), QString("end"), 3000);
        player.setPaused(true);
        QVERIFY(window.pos().x() <= area.left() + 50); QVERIFY(window.pos().x() >= area.left() + 44); // At most one 100 ms step past.
        QCOMPARE(window.pos().y(), floor);
        finishSequence(player); QCOMPARE(player.state(), QString("idle")); QVERIFY(!window.walking());
        // A walk is how an idle pet looks: the monitor leaves it alone, and agent activity ends it at once.
        QVERIFY(monitor.apply(event("prompt"), now + seq)); QVERIFY(monitor.apply(event("interrupt"), now + seq));
        playOut(player); QCOMPARE(player.state(), QString("idle"));
        window.move(area.left() + 200, floor);
        player.select("walk_left", true); finishSequence(player); monitor.update(now + seq);
        QCOMPARE(player.state(), QString("walk_left")); QVERIFY(window.walking());
        QVERIFY(monitor.apply(event("prompt"), now + seq));
        QCOMPARE(player.state(), QString("thinking")); QVERIFY(!window.walking());
        playOut(player); QVERIFY(monitor.apply(event("interrupt"), now + seq)); playOut(player);
        // Climbing: it leaps onto its wall, where the screen edge cuts the artwork at the hands, climbs during
        // the loop, and steps back into view once something else shows.
        window.move(area.left() + 20, floor);
        player.select("climb_up_left", true); QVERIFY(window.walking()); QVERIFY(window.sliding());
        QTRY_VERIFY(!window.sliding()); QCOMPARE(window.pos(), QPoint(area.left() - qRound(145 * 250 / 500.0), floor));
        finishSequence(player); QCOMPARE(player.phase(), QString("loop"));
        player.setPaused(false);
        QTRY_VERIFY_WITH_TIMEOUT(window.pos().y() < floor - 10, 3000);
        player.setPaused(true);
        QCOMPARE(window.pos().x(), area.left() - qRound(145 * 250 / 500.0));
        monitor.update(now + seq); QCOMPARE(player.state(), QString("climb_up_left"));
        QVERIFY(monitor.apply(event("prompt"), now + seq));
        QCOMPARE(player.state(), QString("thinking")); QVERIFY(!window.walking()); QVERIFY(window.sliding());
        QTRY_VERIFY(!window.sliding()); QCOMPARE(window.pos().x(), area.left()); QVERIFY(window.pos().y() < floor - 10);
        playOut(player); QVERIFY(monitor.apply(event("interrupt"), now + seq)); playOut(player);
        // Recovering the position, or turning wandering off, stops a walk where it is.
        window.move(area.left() + 200, floor);
        player.select("walk_right", true); QVERIFY(window.walking()); window.recover();
        QVERIFY(!window.walking()); QCOMPARE(player.state(), QString("idle"));
        player.select("walk_right", true); window.setWanderEnabled(false);
        QVERIFY(!window.walking()); QCOMPARE(player.state(), QString("idle"));
    }
    void wanderPreference() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path); QVERIFY(window.wanderEnabled());
            window.showSettings(); auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
            QCheckBox *box = nullptr;
            for (auto *check : dialog->findChildren<QCheckBox*>()) if (check->accessibleName() == "Wandering") box = check;
            QVERIFY(box); QVERIFY(box->isChecked());
            box->setChecked(false); QVERIFY(!window.wanderEnabled());
            QVERIFY(window.savePreferences()); dialog->close();
        }
        QVERIFY(!pet::PreferencesStore(path).load().wander);
        pet::PetWindow restored(nullptr, path); QVERIFY(!restored.wanderEnabled());
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true})"); file.close();
        QVERIFY(pet::PreferencesStore(path).load().wander); // Older files have no key.
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write(R"({"version":1,"size":200,"on_top":true,"wander":"no"})"); file.close();
        pet::PreferencesStore invalid(path); QVERIFY(invalid.load().wander); QVERIFY(!invalid.save(pet::Preferences{}));
    }
    void malformedMoves() {
        QTemporaryDir fixtures; auto base = fixture(fixtures.path()); // "idle" and "work" sequences exist.
        // A move travels during the loop of a phased state that ends by itself.
        auto states = base["states"].toObject(); states["stroll"] = QJsonArray{"work", "work", "idle"};
        states["hop"] = QJsonArray{"work"}; base["states"] = states;
        auto playback = base["playback"].toObject();
        playback["stroll"] = QJsonObject{{"mode", "phased"}, {"after", "idle"}, {"loops", 3}};
        playback["hop"] = QJsonObject{{"mode", "once"}, {"after", "idle"}}; base["playback"] = playback;
        auto with = [&](QJsonArray list, int scale = 500) {
            auto catalog = base; catalog["moves"] = QJsonObject{{"scale", scale}, {"list", list}}; return catalog;
        };
        const QJsonObject good{{"state", "stroll"}, {"speed", QJsonArray{0, -80}}, {"mood", "happy"},
                               {"wall", QJsonObject{{"side", "left"}, {"at", 145}}}, {"near", QJsonObject{{"left", 100}}},
                               {"room", QJsonObject{{"top", 200}}}, {"keep", QJsonObject{{"top", 100}}}};
        QVERIFY(loads(base)); QVERIFY(loads(with({good}))); QVERIFY(loads(with({})));
        QVERIFY(loads(with({QJsonObject{{"state", "stroll"}, {"speed", QJsonArray{112, 0}}}}))); // Conditions are optional.
        auto broken = [&](const QString &key, const QJsonValue &value) { auto move = good; move[key] = value; return with({move}); };
        QVERIFY(!loads(with({good}, 0)));
        QVERIFY(!loads(with({good, good}))); // One move per state.
        QVERIFY(!loads(broken("state", "hop"))); // No loop to travel in.
        QVERIFY(!loads(broken("state", "working"))); // Never ends by itself.
        QVERIFY(!loads(broken("state", "nobody")));
        QVERIFY(!loads(broken("speed", QJsonArray{0, 0})));
        QVERIFY(!loads(broken("speed", QJsonArray{-112})));
        QVERIFY(!loads(broken("speed", QJsonArray{"fast", 0})));
        QVERIFY(!loads(broken("speed", QJsonArray{5000, 0})));
        QVERIFY(!loads(broken("mood", "sleepy")));
        QVERIFY(!loads(broken("wall", QJsonObject{{"side", "top"}, {"at", 145}})));
        QVERIFY(!loads(broken("wall", QJsonObject{{"side", "left"}, {"at", 500}})));
        QVERIFY(!loads(broken("room", QJsonObject{{"middle", 100}})));
        QVERIFY(!loads(broken("near", QJsonObject{{"left", -1}})));
        QVERIFY(!loads(broken("keep", QJsonObject{})));
        QVERIFY(!loads(broken("keep", 100)));
    }
    void easterEggOccasions() {
        auto at = [](int year, int month, int day, int hour, int minute = 0, const QString &birthday = {}) {
            return pet::EasterEggs::occasionsAt(QDateTime(QDate(year, month, day), QTime(hour, minute)), birthday);
        };
        QCOMPARE(at(2026, 10, 7, 12), QStringList());
        QCOMPARE(at(2026, 5, 20, 12), QStringList{"may20"});
        QCOMPARE(at(2026, 3, 14, 12, 0, "03-14"), QStringList{"birthday"});
        QCOMPARE(at(2026, 3, 15, 12, 0, "03-14"), QStringList());
        // A February 29 birthday is kept on the 28th in other years.
        QCOMPARE(at(2027, 2, 28, 12, 0, "02-29"), QStringList{"birthday"});
        QCOMPARE(at(2028, 2, 28, 12, 0, "02-29"), QStringList());
        QCOMPARE(at(2028, 2, 29, 12, 0, "02-29"), QStringList{"birthday"});
        // Late night runs from 01:00 to 05:00, Friday evening from 17:00 to midnight.
        QCOMPARE(at(2026, 10, 7, 0, 59), QStringList()); QCOMPARE(at(2026, 10, 7, 1, 0), QStringList{"late_night"});
        QCOMPARE(at(2026, 10, 7, 4, 59), QStringList{"late_night"}); QCOMPARE(at(2026, 10, 7, 5, 0), QStringList());
        QCOMPARE(at(2026, 10, 9, 16, 59), QStringList()); QCOMPARE(at(2026, 10, 9, 17, 0), QStringList{"friday_evening"});
        QCOMPARE(at(2026, 10, 9, 23, 59), QStringList{"friday_evening"}); QCOMPARE(at(2026, 10, 10, 0, 30), QStringList());
        QCOMPARE(at(2026, 5, 20, 2, 0, "05-20"), (QStringList{"may20", "birthday", "late_night"}));
        // A birthday is "MM-dd" of a real date.
        pet::Player player; player.setPaused(true); pet::EasterEggs eggs(player);
        for (const auto *bad : {"13-01", "02-30", "00-10", "2-1", "0101", "aa-bb", "2026-03-14"})
            QVERIFY2(!eggs.setBirthday(bad), bad);
        QVERIFY(eggs.setBirthday("02-29")); QVERIFY(!eggs.setBirthday("x")); QCOMPARE(eggs.birthday(), QString("02-29"));
        QVERIFY(eggs.setBirthday({})); QCOMPARE(eggs.birthday(), QString());
        // The bedtime note comes once a night, and only at night.
        QDateTime local(QDate(2026, 10, 7), QTime(23, 0)); eggs.setClock([&] { return local; });
        QVERIFY(!eggs.bedtime()); local = local.addSecs(3 * 3600); QVERIFY(eggs.bedtime()); QVERIFY(!eggs.bedtime());
        local = local.addDays(1); QVERIFY(eggs.bedtime());
        eggs.setEnabled(false); local = local.addDays(1); QVERIFY(!eggs.bedtime());
        // Clock reminders: Monday morning, weekdays from 16:45, and every evening from 22:00.
        auto due = [](int year, int month, int day, int hour, int minute) {
            return pet::EasterEggs::remindersAt(QDateTime(QDate(year, month, day), QTime(hour, minute)));
        };
        QCOMPARE(due(2026, 10, 5, 5, 59), QStringList()); QCOMPARE(due(2026, 10, 5, 6, 0), QStringList{"monday"});
        QCOMPARE(due(2026, 10, 5, 11, 59), QStringList{"monday"}); QCOMPARE(due(2026, 10, 6, 9, 0), QStringList());
        QCOMPARE(due(2026, 10, 7, 16, 44), QStringList()); QCOMPARE(due(2026, 10, 7, 16, 45), QStringList{"leave_work"});
        QCOMPARE(due(2026, 10, 7, 17, 59), QStringList{"leave_work"}); QCOMPARE(due(2026, 10, 7, 18, 0), QStringList());
        QCOMPARE(due(2026, 10, 10, 16, 45), QStringList()); // Saturday.
        QCOMPARE(due(2026, 10, 10, 21, 59), QStringList()); QCOMPARE(due(2026, 10, 10, 22, 0), QStringList{"sleep"});
        QCOMPARE(due(2026, 10, 7, 23, 59), QStringList{"sleep"}); QCOMPARE(due(2026, 10, 8, 0, 0), QStringList());
        // Each is given once a day, and not at all while the eggs are off.
        local = QDateTime(QDate(2026, 10, 5), QTime(9, 0));
        QCOMPARE(eggs.reminder(), QString()); // Off since the bedtime check above.
        eggs.setEnabled(true);
        QCOMPARE(eggs.reminder(), QString("monday")); QCOMPARE(eggs.reminder(), QString());
        local = QDateTime(QDate(2026, 10, 5), QTime(16, 50)); QCOMPARE(eggs.reminder(), QString("leave_work"));
        local = QDateTime(QDate(2026, 10, 5), QTime(22, 0)); QCOMPARE(eggs.reminder(), QString("sleep")); QCOMPARE(eggs.reminder(), QString());
        local = local.addDays(1); QCOMPARE(eggs.reminder(), QString("sleep"));
        for (const auto *name : {"monday", "leave_work", "sleep"}) QVERIFY(!pet::EasterEggs::reminderNote(name).isEmpty());
        // The Konami code completes on its last key, also after a false start.
        const QList<int> code{Qt::Key_Up, Qt::Key_Up, Qt::Key_Down, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right,
                              Qt::Key_Left, Qt::Key_Right, Qt::Key_B};
        QVERIFY(!eggs.key(Qt::Key_Up));
        for (const int key : code) QVERIFY(!eggs.key(key));
        QVERIFY(eggs.key(Qt::Key_A)); QVERIFY(!eggs.key(Qt::Key_A));
        // A surprise plays a pool at once; turned off, it plays nothing.
        Draws draws; eggs.setRandom(draws.random());
        eggs.setEnabled(false); QVERIFY(!eggs.surprise("danger")); QCOMPARE(player.state(), QString("idle"));
        eggs.setEnabled(true); QVERIFY(!eggs.surprise("nobody"));
        QVERIFY(eggs.surprise("danger")); QCOMPARE(player.state(), QString("startled")); QVERIFY(eggs.surprising());
        // It stops counting once something else shows, or after a while even if the player stalls.
        player.select("thinking", true); QVERIFY(!eggs.surprising());
        QVERIFY(eggs.surprise("danger")); local = local.addMSecs(pet::EasterEggs::surpriseMs); QVERIFY(!eggs.surprising());
        // Never over a drag.
        player.beginDrag(); QVERIFY(!eggs.surprise("danger")); player.endDrag();
        QCOMPARE(draws.unexpected, 0);
    }
    void easterEggFidgets() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        pet::Ambient ambient(player); pet::EasterEggs eggs(player); ambient.setEasterEggs(&eggs);
        Draws draws, eggDraws; qint64 now = 1000000;
        QDateTime local(QDate(2026, 5, 20), QTime(12, 0));
        ambient.setRandom(draws.random()); ambient.setClock([&] { return now; });
        eggs.setRandom(eggDraws.random()); eggs.setClock([&] { return local; });
        // On May 20 the first fidget greets with the day's own animation; a pool of one draws nothing.
        draws.values = {0}; player.select("thinking", true); player.select("idle", true);
        now += 45000; finishSequence(player);
        QCOMPARE(player.state(), QString("love_520")); QVERIFY(ambient.resting());
        // It plays out like a fidget, keeping the idle clock.
        draws.values = {0}; playOut(player); QVERIFY(!ambient.resting()); QCOMPARE(ambient.idleFor(), qint64(45000));
        // Later that day it comes back one fidget in four; otherwise an ordinary fidget plays.
        now += 45000; eggDraws.values = {1}; draws.values = {5, 0}; finishSequence(player);
        QCOMPARE(player.state(), QString("fidget_aside"));
        draws.values = {0}; playOut(player);
        now += 45000; eggDraws.values = {0}; finishSequence(player); QCOMPARE(player.state(), QString("love_520"));
        // An ordinary day asks nothing of the eggs.
        draws.values = {0}; playOut(player);
        local = QDateTime(QDate(2026, 5, 21), QTime(12, 0));
        now += 45000; draws.values = {5, 0}; finishSequence(player); QCOMPARE(player.state(), QString("fidget_aside"));
        // Late at night one fidget in two is a yawn, even before the usual two idle minutes for one.
        draws.values = {0}; playOut(player);
        local = QDateTime(QDate(2026, 5, 22), QTime(2, 30));
        now += 45000; eggDraws.values = {0}; finishSequence(player); QCOMPARE(player.state(), QString("fidget_yawn"));
        QVERIFY(ambient.resting());
        draws.values = {0}; playOut(player);
        now += 45000; eggDraws.values = {1}; draws.values = {5, 0}; finishSequence(player);
        QCOMPARE(player.state(), QString("fidget_aside"));
        // A birthday greets first, then May 20 when it falls on the same day.
        draws.values = {0}; playOut(player);
        QVERIFY(eggs.setBirthday("05-20")); local = QDateTime(QDate(2027, 5, 20), QTime(12, 0));
        now += 45000; finishSequence(player); QCOMPARE(player.state(), QString("birthday"));
        draws.values = {0}; playOut(player);
        now += 45000; finishSequence(player); QCOMPARE(player.state(), QString("love_520"));
        // Turned off, it is an ordinary day again.
        draws.values = {0}; playOut(player);
        eggs.setEnabled(false); local = QDateTime(QDate(2028, 5, 20), QTime(12, 0));
        now += 45000; draws.values = {5, 0}; finishSequence(player); QCOMPARE(player.state(), QString("fidget_aside"));
        QCOMPARE(draws.unexpected, 0); QCOMPARE(eggDraws.unexpected, 0);
    }
    void easterEggCelebrations() {
        pet::Player player; player.setPaused(true);
        pet::Mood mood(player); pet::EasterEggs eggs(player); Draws draws; mood.setRandom(draws.random());
        QDateTime local(QDate(2026, 10, 7), QTime(12, 0)); eggs.setClock([&] { return local; }); // A Wednesday.
        QCOMPARE(eggs.celebration(0), QString()); QCOMPARE(eggs.celebration(60000), QString());
        // A turn of fifteen minutes or more is celebrated bigger: the milestone weighs 2, a dance 1.
        QCOMPARE(eggs.celebration(pet::EasterEggs::longTurnMs - 1), QString());
        QCOMPARE(eggs.celebration(pet::EasterEggs::longTurnMs), QString("long_turn"));
        draws.values = {1}; QCOMPARE(mood.celebrate("long_turn"), QString("milestone"));
        draws.values = {2}; QCOMPARE(mood.celebrate("long_turn"), QString("dance"));
        // Friday evening dances.
        local = QDateTime(QDate(2026, 10, 9), QTime(17, 0));
        QCOMPARE(eggs.celebration(0), QString("friday_evening")); QCOMPARE(mood.celebrate("friday_evening"), QString("dance"));
        // A birthday celebrates its first finished turn; a long turn still comes first.
        QVERIFY(eggs.setBirthday("10-09"));
        QCOMPARE(eggs.celebration(pet::EasterEggs::longTurnMs), QString("long_turn"));
        QCOMPARE(eggs.celebration(0), QString("birthday")); QCOMPARE(eggs.celebration(0), QString("friday_evening"));
        QCOMPARE(mood.celebrate("birthday"), QString("birthday"));
        // A milestone outranks an occasion, and a snack waits behind one for the next turn.
        mood.setTurns(99); mood.finished(1000); QCOMPARE(mood.treat(), QString("milestone"));
        QCOMPARE(mood.celebrate("friday_evening"), QString("milestone")); QCOMPARE(mood.treat(), QString());
        for (int i = 0; i < pet::Mood::snackTurns && mood.treat().isEmpty(); ++i) mood.finished(2000);
        QCOMPARE(mood.treat(), QString("snack"));
        QCOMPARE(mood.celebrate("friday_evening"), QString("dance")); QCOMPARE(mood.treat(), QString("snack"));
        draws.values = {0}; QCOMPARE(mood.celebrate(), QString("snack_hungry")); QCOMPARE(mood.treat(), QString());
        // A pool the catalog lacks changes nothing, and neither do the eggs when turned off.
        draws.values = {0}; QCOMPARE(mood.celebrate("nobody"), QString("turn_finished"));
        eggs.setEnabled(false); QCOMPARE(eggs.celebration(pet::EasterEggs::longTurnMs), QString());
        QCOMPARE(draws.unexpected, 0);
    }
    void monitorEasterEggs() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window); auto &player = window.player(); player.setPaused(true);
        player.setRandom([](int) { return 0; }); window.ambient().setRandom([](int) { return 0; });
        Draws draws, eggDraws; window.mood().setRandom(draws.random()); window.eggs().setRandom(eggDraws.random());
        QDateTime local(QDate(2026, 10, 7), QTime(12, 0)); window.eggs().setClock([&] { return local; });
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString kind, qint64 at, QString session = "s1") {
            ++seq; return pet::Event{"claude", session, QString::number(seq), kind, {}, {}, "/work/abc-web", {}, at, {}};
        };
        auto tool = [&](QString id, qint64 at, bool risky) {
            auto e = event("tool_start", at); e.tool = id; e.risky = risky; return e;
        };
        // A turn that ran twenty minutes ends in a big celebration.
        const qint64 start = now - 20 * 60000;
        QVERIFY(monitor.apply(event("prompt", start), start));
        draws.values = {2}; QVERIFY(monitor.apply(event("turn_finished", now), now));
        QCOMPARE(player.requestedState(), QString("dance"));
        playOut(player); playOut(player); QCOMPARE(player.state(), QString("idle"));
        // A destructive command startles the pet; the periodic update lets it finish, then shows the work.
        QVERIFY(monitor.apply(event("prompt", now + 1), now + 1));
        QVERIFY(monitor.apply(tool("t1", now + 2, true), now + 2));
        QCOMPARE(player.state(), QString("startled")); QVERIFY(window.eggs().surprising());
        monitor.update(now + 3); QCOMPARE(player.state(), QString("startled"));
        playOut(player); monitor.update(now + 4); QCOMPARE(player.requestedState(), QString("working"));
        // An ordinary command does not.
        QVERIFY(monitor.apply(tool("t2", now + 5, false), now + 5)); QVERIFY(!window.eggs().surprising());
        // A request for the user cuts a surprise short, and one waiting elsewhere keeps the pet from jumping.
        QVERIFY(monitor.apply(tool("t3", now + 6, true), now + 6)); QCOMPARE(player.state(), QString("startled"));
        QVERIFY(monitor.apply(event("attention", now + 7, "s2"), now + 7));
        QCOMPARE(player.state(), QString("needs_input")); QVERIFY(!window.eggs().surprising());
        QVERIFY(monitor.apply(tool("t4", now + 8, true), now + 8)); QCOMPARE(player.state(), QString("needs_input"));
        QVERIFY(monitor.apply(event("session_end", now + 9, "s2"), now + 9));
        // A turn finishing late at night brings one bedtime note.
        local = QDateTime(QDate(2026, 10, 8), QTime(2, 0)); monitor.note().hide();
        draws.values = {0}; QVERIFY(monitor.apply(event("turn_finished", now + 10), now + 10));
        QVERIFY(monitor.note().isVisible()); QCOMPARE(monitor.note().text(), pet::Monitor::bedtimeNote()); QVERIFY(!QToolTip::isVisible());
        QVERIFY(!window.eggs().bedtime()); // Used for tonight.
        // The Konami code, typed on the pet, makes it dance, and the monitor lets it.
        local = QDateTime(QDate(2026, 10, 7), QTime(12, 0));
        for (int i = 0; i < 4; ++i) playOut(player);
        QCOMPARE(player.state(), QString("idle")); monitor.update(now + 13);
        eggDraws.values = {0};
        for (const int key : {Qt::Key_Up, Qt::Key_Up, Qt::Key_Down, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right,
                              Qt::Key_Left, Qt::Key_Right, Qt::Key_B, Qt::Key_A})
            QTest::keyClick(&window, Qt::Key(key));
        QCOMPARE(player.state(), QString("dance")); monitor.update(now + 14); QCOMPARE(player.state(), QString("dance"));
        QCOMPARE(draws.unexpected, 0); QCOMPARE(eggDraws.unexpected, 0);
    }
    void dailyRecap() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const auto today = QDateTime::fromMSecsSinceEpoch(now).date();
        {
            pet::PetWindow window(nullptr, path); window.show(); QVERIFY(window.recapEnabled());
            QCOMPARE(window.recapPath(), directory.path() + "/recap.json");
            pet::Monitor monitor(window); window.player().setPaused(true);
            QDateTime local(QDate(2026, 10, 7), QTime(12, 0)); window.eggs().setClock([&] { return local; });
            // Asked for before any work, the pet says so and has nothing more to show.
            emit window.recapRequested();
            QVERIFY(monitor.note().isVisible()); QCOMPARE(monitor.note().text(), QString("No agent work yet today."));
            QVERIFY(!monitor.note().hasDetails());
            QVERIFY(monitor.apply({"claude", "s1", "1", "prompt", {}, {}, "/work/abc-web", {}, now - 22 * 60000, {}}, now));
            QVERIFY(monitor.apply({"claude", "s1", "2", "turn_finished", {}, {}, {}, {}, now, {}}, now));
            const auto summary = pet::Recap::summary(monitor.recap().day(today));
            QCOMPARE(summary, QString("Today: 1 turn in abc-web · longest run 22 min"));
            // From the menu: the summary, then the per-project breakdown on a click.
            monitor.note().hide(); emit window.recapRequested();
            QCOMPARE(monitor.note().text(), summary); QVERIFY(monitor.note().hasDetails());
            QTest::mouseClick(&monitor.note(), Qt::LeftButton);
            QVERIFY(monitor.note().isVisible());
            QCOMPARE(monitor.note().text(), pet::Recap::breakdown(monitor.recap().day(today)));
            QTest::mouseClick(&monitor.note(), Qt::LeftButton); QVERIFY(!monitor.note().isVisible());
            // The go-home reminder sums up the day, unless that is turned off.
            local = QDateTime(QDate(2026, 10, 7), QTime(16, 50)); monitor.update(now + 1);
            QCOMPARE(monitor.note().text(), pet::EasterEggs::reminderNote("leave_work") + "\n" + summary);
            QVERIFY(monitor.note().hasDetails());
            window.setRecapEnabled(false); QVERIFY(window.savePreferences());
            local = QDateTime(QDate(2026, 10, 8), QTime(16, 50)); monitor.update(now + 2);
            QCOMPARE(monitor.note().text(), pet::EasterEggs::reminderNote("leave_work")); QVERIFY(!monitor.note().hasDetails());
            monitor.stop(); // Writes the counters now rather than after the short delay.
        }
        QVERIFY(!pet::PreferencesStore(path).load().recap);
        // The counters survive a restart; the setting is in the settings dialog.
        pet::PetWindow window(nullptr, path); QVERIFY(!window.recapEnabled());
        pet::Monitor monitor(window); QCOMPARE(monitor.recap().day(today).turns, 1);
        window.showSettings(); auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
        QCheckBox *box = nullptr;
        for (auto *check : dialog->findChildren<QCheckBox*>()) if (check->accessibleName() == "Daily recap") box = check;
        QVERIFY(box); QVERIFY(!box->isChecked()); box->setChecked(true); QVERIFY(window.recapEnabled());
        dialog->close();
        // Without persistence there is no recap file.
        QVERIFY(pet::PetWindow(nullptr, path, false).recapPath().isEmpty());
    }
    void easterEggPreference() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path);
            QVERIFY(window.easterEggsEnabled()); QCOMPARE(window.birthday(), QString());
            window.showSettings(); auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
            QCheckBox *eggs = nullptr, *birthday = nullptr; QDateEdit *date = nullptr;
            for (auto *box : dialog->findChildren<QCheckBox*>()) {
                if (box->accessibleName() == "Easter eggs") eggs = box;
                if (box->accessibleName() == "Birthday") birthday = box;
            }
            for (auto *edit : dialog->findChildren<QDateEdit*>()) if (edit->accessibleName() == "Birthday date") date = edit;
            QVERIFY(eggs && birthday && date);
            QVERIFY(eggs->isChecked()); QVERIFY(!birthday->isChecked()); QVERIFY(!date->isEnabled());
            birthday->setChecked(true); QVERIFY(date->isEnabled()); QCOMPARE(window.birthday(), QString("01-01"));
            date->setDate(QDate(2000, 2, 29)); QCOMPARE(window.birthday(), QString("02-29"));
            eggs->setChecked(false); QVERIFY(!window.eggs().enabled());
            QVERIFY(window.savePreferences()); dialog->close();
        }
        auto saved = pet::PreferencesStore(path).load();
        QVERIFY(!saved.easterEggs); QCOMPARE(saved.birthday, QString("02-29"));
        {
            pet::PetWindow restored(nullptr, path);
            QVERIFY(!restored.easterEggsEnabled()); QCOMPARE(restored.birthday(), QString("02-29"));
            restored.setBirthday("02-30"); QCOMPARE(restored.birthday(), QString("02-29")); // Refused.
            restored.setBirthday({}); restored.setEasterEggsEnabled(true); QVERIFY(restored.savePreferences());
        }
        saved = pet::PreferencesStore(path).load(); QVERIFY(saved.easterEggs); QCOMPARE(saved.birthday, QString());
        // Older files have neither key; bad values are refused like any other and preserved.
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true})"); file.close();
        const auto legacy = pet::PreferencesStore(path).load();
        QVERIFY(legacy.easterEggs); QCOMPARE(legacy.birthday, QString());
        for (const auto *broken : {R"({"version":1,"size":200,"on_top":true,"easter_eggs":1})",
                                   R"({"version":1,"size":200,"on_top":true,"birthday":"02-30"})",
                                   R"({"version":1,"size":200,"on_top":true,"birthday":229})",
                                   R"({"version":1,"size":200,"on_top":true,"birthday":""})"}) {
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write(broken); file.close();
            pet::PreferencesStore invalid(path); QCOMPARE(invalid.load().birthday, QString()); QVERIFY(!invalid.save(pet::Preferences{}));
        }
    }
    void wellnessTimers() {
        pet::Wellness wellness; const qint64 t0 = 1000000; const qint64 minute = 60000;
        QCOMPARE(wellness.eyeMinutes(), 20); QCOMPARE(wellness.waterMinutes(), 60); // On by default.
        QCOMPARE(wellness.due(t0), QString()); // Nothing seen yet.
        // Activity every half minute counts in full; the first one only starts the stretch.
        qint64 t = t0;
        for (; t < t0 + 20 * minute; t += 30000) wellness.activity(t);
        QCOMPARE(wellness.eyesActiveMs(t - 30000), 20 * minute - 30000);
        QCOMPARE(wellness.due(t - 30000), QString());
        QCOMPARE(wellness.due(t), QString("eyes")); QCOMPARE(wellness.due(t + 5000), QString("eyes")); // Waits until given.
        wellness.given("eyes", t); QCOMPARE(wellness.eyesActiveMs(t), qint64(0)); QCOMPARE(wellness.due(t), QString());
        // A gap of a few minutes counts one minute and pauses the rest; five minutes away is a break.
        QCOMPARE(wellness.waterActiveMs(t), 20 * minute);
        wellness.activity(t + 3 * minute); QCOMPARE(wellness.waterActiveMs(t + 3 * minute), 20 * minute + 30000); // Last seen at t - 30 s.
        QCOMPARE(wellness.waterActiveMs(t + 9 * minute), qint64(0)); QCOMPARE(wellness.due(t + 9 * minute), QString());
        wellness.activity(t + 9 * minute); QCOMPARE(wellness.waterActiveMs(t + 9 * minute), qint64(0));
        QCOMPARE(wellness.eyesActiveMs(t + 9 * minute), qint64(0));
        // Water comes after eyes when both are due; off means never.
        t += 9 * minute; wellness.setEyeMinutes(0);
        for (const qint64 end = t + 60 * minute; t <= end; t += 30000) wellness.activity(t);
        QCOMPARE(wellness.due(t), QString("water")); wellness.given("water", t); QCOMPARE(wellness.due(t), QString());
        wellness.setEyeMinutes(25); QCOMPARE(wellness.eyeMinutes(), 0); // Not a choice.
        wellness.setWaterMinutes(45); QCOMPARE(wellness.waterMinutes(), 45);
        // Quiet hours run from 22:00 to 06:00; each reminder has its words.
        QVERIFY(pet::Wellness::quietAt(QDateTime(QDate(2026, 10, 7), QTime(22, 0))));
        QVERIFY(pet::Wellness::quietAt(QDateTime(QDate(2026, 10, 7), QTime(5, 59))));
        QVERIFY(!pet::Wellness::quietAt(QDateTime(QDate(2026, 10, 7), QTime(6, 0))));
        QVERIFY(!pet::Wellness::quietAt(QDateTime(QDate(2026, 10, 7), QTime(21, 59))));
        QCOMPARE(pet::Wellness::note("eyes"), QString("Look at something far away for 20 seconds"));
        QCOMPARE(pet::Wellness::note("water"), QString("Time for some water 💧"));
        QCOMPARE(pet::Wellness::note("snack"), QString());
        // Agent events alone start nothing, and keep a stretch going only while the user was seen within
        // five minutes, as behind a locked screen.
        pet::Wellness agent; agent.activity(t0, false); QCOMPARE(agent.due(t0 + 30 * minute), QString());
        QVERIFY(!agent.present(t0));
        agent.activity(t0); QVERIFY(agent.present(t0)); QVERIFY(agent.present(t0 + 59000)); QVERIFY(!agent.present(t0 + minute));
        for (qint64 at = t0 + 30000; at <= t0 + 20 * minute; at += 30000) agent.activity(at, false);
        QCOMPARE(agent.eyesActiveMs(t0 + 4 * minute + 30000), 4 * minute + 30000);
        QCOMPARE(agent.eyesActiveMs(t0 + 20 * minute), qint64(0)); QCOMPARE(agent.due(t0 + 20 * minute), QString());
        // A reset (the screen was locked) also forgets the user: agent events alone do not restart it.
        pet::Wellness locked; locked.activity(t0); locked.activity(t0 + 30000); locked.reset();
        QVERIFY(!locked.present(t0 + 30000));
        locked.activity(t0 + minute, false); QCOMPARE(locked.eyesActiveMs(t0 + minute + 30000), qint64(0));
        locked.activity(t0 + 2 * minute); locked.activity(t0 + 2 * minute + 30000, false);
        QCOMPARE(locked.eyesActiveMs(t0 + 2 * minute + 30000), qint64(30000));
    }
    void monitorWellnessReminders() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window); auto &player = window.player(); player.setPaused(true);
        player.setRandom([](int) { return 0; }); window.ambient().setRandom([](int) { return 0; });
        window.ambient().setLevel(pet::AmbientLevel::Off);
        QDateTime local(QDate(2026, 10, 7), QTime(12, 0)); window.eggs().setClock([&] { return local; });
        QPoint pointer; monitor.pointer = [&] { return pointer; };
        monitor.setRestTickMs(10); // The twenty-second eye break, in a fifth of a second.
        const qint64 minute = 60000; qint64 t = QDateTime::currentMSecsSinceEpoch();
        // The user keeps moving the pointer; the monitor's updates notice.
        auto work = [&](qint64 minutes) {
            for (const qint64 end = t + minutes * minute; t < end;) { t += 30000; pointer += QPoint(1, 0); monitor.update(t); }
        };
        pointer = QPoint(1, 1); monitor.update(t); // Starts the stretch.
        work(19); QVERIFY(!monitor.note().isVisible());
        work(1);
        QVERIFY(monitor.note().isVisible()); QCOMPARE(monitor.note().text(), pet::Wellness::note("eyes"));
        QCOMPARE(monitor.reminder(), QString("eyes")); QCOMPARE(player.requestedState(), QString("fidget_yawn"));
        // Clicking it starts a twenty-second countdown, then the pet cheers.
        emit monitor.note().clicked(); QCOMPARE(monitor.restLeft(), 20); QVERIFY(monitor.note().isVisible());
        QVERIFY(monitor.note().text().endsWith("20")); QCOMPARE(monitor.reminder(), QString());
        playOut(player);
        QTRY_COMPARE_WITH_TIMEOUT(monitor.restLeft(), 0, 2500);
        QCOMPARE(player.requestedState(), QString("cheer_shy")); playOut(player);
        monitor.note().hide();
        // An approval waiting holds a due reminder until it is answered; then it comes.
        window.setEyeMinutes(0);
        work(39);
        qint64 seq = 0;
        auto event = [&](QString kind) { ++seq; return pet::Event{"claude", "w1", QString::number(seq), kind, {}, {}, "/work/abc-web", {}, t, {}}; };
        QVERIFY(monitor.apply(event("attention"), t));
        work(1); QVERIFY(!monitor.note().isVisible()); QVERIFY(window.wellness().due(t) == "water");
        emit monitor.bubble().dismissRequested(); work(1); QVERIFY(!monitor.note().isVisible()); // Still waiting on the user.
        QVERIFY(monitor.apply(event("prompt"), t)); QVERIFY(monitor.apply(event("session_end"), t));
        monitor.update(t);
        QVERIFY(monitor.note().isVisible()); QCOMPARE(monitor.note().text(), pet::Wellness::note("water"));
        QCOMPARE(player.requestedState(), QString("snack_thirsty"));
        emit monitor.note().clicked(); QCOMPARE(player.requestedState(), QString("cheer_shy")); QCOMPARE(monitor.restLeft(), 0);
        QCOMPARE(window.wellness().due(t), QString());
        // Ignored, it fades; nothing more until the next interval. Muted alerts hold it.
        playOut(player); playOut(player); monitor.note().hide();
        window.setMuted(true); work(61); QVERIFY(!monitor.note().isVisible()); QCOMPARE(window.wellness().due(t), QString("water"));
        // Unmuted while the user is away (no input for a minute and a half): it waits for them.
        t += 90000; window.setMuted(false); monitor.update(t); QVERIFY(!monitor.note().isVisible());
        pointer += QPoint(1, 0); monitor.update(t); QVERIFY(monitor.note().isVisible()); monitor.note().hide();
        // In quiet hours a due reminder is let go instead of waiting for the morning.
        local = QDateTime(QDate(2026, 10, 7), QTime(23, 0));
        // (The evening's own sleep note may show; the reminder does not.)
        work(61); QCOMPARE(monitor.reminder(), QString()); QCOMPARE(window.wellness().due(t), QString());
        monitor.note().hide();
        // Five minutes away starts the stretch over.
        local = QDateTime(QDate(2026, 10, 7), QTime(12, 0));
        work(50); t += 6 * minute; monitor.update(t); work(15); QVERIFY(!monitor.note().isVisible());
        // A locked screen counts nothing, from the pointer or from agents, and shows nothing.
        bool locked = false; monitor.locked = [&] { return locked; };
        work(40); const auto before = window.wellness().waterActiveMs(t);
        QVERIFY(before >= 40 * minute);
        locked = true; QVERIFY(monitor.apply(event("prompt"), t)); work(1);
        QCOMPARE(window.wellness().waterActiveMs(t), qint64(0)); // Locking is a break: both timers start over.
        QVERIFY(window.wellness().due(t).isEmpty()); QVERIFY(!monitor.note().isVisible());
        // Even a short lock: back after two minutes, the stretch starts from the first move.
        locked = false; work(1); QCOMPARE(window.wellness().waterActiveMs(t), qint64(30000));
        QVERIFY(!monitor.note().isVisible());
        // Locking takes away a reminder on screen, and ends a countdown without the cheer.
        playOut(player); window.setEyeMinutes(20); work(20); QCOMPARE(monitor.reminder(), QString("eyes"));
        locked = true; monitor.update(t); QCOMPARE(monitor.reminder(), QString()); QVERIFY(!monitor.note().isVisible());
        playOut(player); locked = false; work(21); QCOMPARE(monitor.reminder(), QString("eyes"));
        emit monitor.note().clicked(); QCOMPARE(monitor.restLeft(), 20); playOut(player);
        const auto playing = player.requestedState(); locked = true;
        QTRY_COMPARE_WITH_TIMEOUT(monitor.restLeft(), 0, 2500);
        QVERIFY(!monitor.note().isVisible()); QCOMPARE(player.requestedState(), playing);
        // A pointer moved behind the lock is not the user back: after unlock, only a new move counts.
        pointer += QPoint(5, 0); t += 30000; monitor.update(t); locked = false;
        t += 30000; monitor.update(t); QVERIFY(!window.wellness().present(t));
        pointer += QPoint(1, 0); t += 30000; monitor.update(t); QVERIFY(window.wellness().present(t));
    }
    void wellnessPreference() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path);
            window.showSettings(); auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
            auto *group = dialog->findChild<QGroupBox*>("reminders"); QVERIFY(group);
            QComboBox *eyes = nullptr, *water = nullptr;
            for (auto *combo : group->findChildren<QComboBox*>()) {
                if (combo->accessibleName() == "Eye break reminder") eyes = combo;
                if (combo->accessibleName() == "Water reminder") water = combo;
            }
            QVERIFY(eyes && water);
            QCOMPARE(eyes->currentData().toInt(), 20); QCOMPARE(water->currentData().toInt(), 60);
            QCOMPARE(eyes->count(), 4); QCOMPARE(water->count(), 4);
            eyes->setCurrentIndex(eyes->findData(45)); water->setCurrentIndex(water->findData(0));
            QCOMPARE(window.wellness().eyeMinutes(), 45); QCOMPARE(window.wellness().waterMinutes(), 0);
            QVERIFY(window.savePreferences()); dialog->close();
        }
        auto saved = pet::PreferencesStore(path).load(); QCOMPARE(saved.eyeMinutes, 45); QCOMPARE(saved.waterMinutes, 0);
        {
            pet::PetWindow restored(nullptr, path);
            QCOMPARE(restored.wellness().eyeMinutes(), 45); QCOMPARE(restored.wellness().waterMinutes(), 0);
            restored.setWaterMinutes(50); QCOMPARE(restored.wellness().waterMinutes(), 0); // Not a choice.
        }
        // Older files have neither key; an unknown interval falls back to the default; bad values are refused.
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true,"eye_minutes":25})"); file.close();
        QCOMPARE(pet::PreferencesStore(path).load().waterMinutes, 60);
        { pet::PetWindow window(nullptr, path); QCOMPARE(window.wellness().eyeMinutes(), 20); }
        for (const auto *broken : {R"({"version":1,"size":200,"on_top":true,"eye_minutes":true})",
                                   R"({"version":1,"size":200,"on_top":true,"water_minutes":-5})"}) {
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write(broken); file.close();
            pet::PreferencesStore invalid(path); QCOMPARE(invalid.load().eyeMinutes, 20); QVERIFY(!invalid.save(pet::Preferences{}));
        }
    }
    void focusSessionListAndQuietHosts() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window);
        QStringList focused, looking;
        monitor.bringForward = [&](const pet::Session &s) {
            focused << s.id;
            pet::hosts::FocusResult result;
            result.activation = s.host.adapter != "terminal" ? pet::platform::Outcome::Requested : pet::platform::Outcome::TargetNotFound;
            return result;
        };
        monitor.hostActive = [&](const pet::Session &s) {
            return looking.contains(s.id) ? pet::platform::ActiveState::Active : pet::platform::ActiveState::Inactive;
        };
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
    void focusOutcomesAndAlertPolicy() {
        using pet::platform::Outcome; using pet::platform::ActiveState;
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        window.setBubbles(pet::Preferences::AllAlerts);
        pet::Monitor monitor(window);
        pet::hosts::FocusResult outcome;
        QStringList asked;
        auto active = ActiveState::Unknown;
        monitor.bringForward = [&](const pet::Session &) { return outcome; };
        monitor.hostActive = [&](const pet::Session &s) { asked << s.id; return active; };
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString session, QString kind, QString host, QString reason = {}) {
            ++seq; pet::Event e{"claude", session, QString::number(seq), kind, {}, {}, "/work/" + session, {}, now + seq, reason};
            e.host = host; return e;
        };
        const auto key = [](const QString &id) { return QString("claude") + QChar(0x1f) + id; };
        const auto alerts = [&](const QString &id) {
            int count = 0;
            for (const auto &alert : monitor.sessions().pending()) count += alert.session == key(id);
            return count;
        };
        // A session without host metadata is never checked for an active window.
        QVERIFY(monitor.apply(event("old", "error", {}), now + seq));
        QVERIFY(!asked.contains("old"));
        QToolTip::hideText();
        QVERIFY(!monitor.focusSession(key("old")));
        QCOMPARE(QToolTip::text(), QString("This session started before Agent Pet could see its terminal. Its next event will fix that."));
        // Unknown observation, as for a multiplexer pane, never suppresses an alert; only Active does.
        QVERIFY(monitor.apply(event("pane", "error", "tmux"), now + seq)); QCOMPARE(alerts("pane"), 1);
        active = ActiveState::Inactive;
        QVERIFY(monitor.apply(event("pane", "turn_finished", "tmux"), now + seq)); QCOMPARE(alerts("pane"), 2);
        active = ActiveState::Active;
        QVERIFY(monitor.apply(event("seen", "error", "vscode"), now + seq)); QCOMPARE(alerts("seen"), 0);
        active = ActiveState::Unknown;
        // A host that cannot be raised keeps every alert and explains what to check.
        QVERIFY(monitor.apply(event("web", "attention", "konsole", "input"), now + seq));
        QVERIFY(monitor.apply(event("web", "turn_finished", "konsole"), now + seq)); // Answers the request.
        QVERIFY(monitor.apply(event("web", "error", "konsole"), now + seq));
        QVERIFY(asked.contains("web"));
        const auto failure = [&](Outcome selection, Outcome activation, QStringList requirements = {}) {
            outcome = {selection, activation, {}, requirements};
            QToolTip::hideText();
            const bool focused = monitor.focusSession(key("web"));
            return focused ? QString("focused") : QToolTip::text();
        };
        // The window was searched for: the X11 lookup failed and KWin was unavailable, as on a non-KDE desktop.
        QCOMPARE(failure(Outcome::Confirmed, Outcome::TargetNotFound, {"Wayland focus requires KDE Plasma 6."}),
                 QString("Could not focus this session's window. Check that its terminal is attached. "
                         "Wayland focus requires KDE Plasma 6."));
        QCOMPARE(failure(Outcome::Failed, Outcome::Failed),
                 QString("Could not focus this session's window. Check that its terminal is attached."));
        // A multiplexer stopped at its pane selection; no window was tried.
        QCOMPARE(failure(Outcome::TimedOut, Outcome::Skipped),
                 QString("Could not select this session's tab or pane. Check that its terminal is attached."));
        // No backend could act in this session at all.
        QCOMPARE(failure(Outcome::Unsupported, Outcome::Unsupported, {"Wayland focus requires KDE Plasma 6."}),
                 QString("This desktop session does not let Agent Pet raise windows. Wayland focus requires KDE Plasma 6."));
        // Selecting the tab alone is not focus.
        QCOMPARE(failure(Outcome::Confirmed, Outcome::MissingTarget), QString("Could not focus this session's window. "
                                                                              "Check that its terminal is attached."));
        QCOMPARE(alerts("web"), 2);
        // Once raised, whether requested or confirmed, its error and finished-turn alerts go; other sessions' stay.
        QCOMPARE(failure(Outcome::Failed, Outcome::Requested), QString("focused"));
        QCOMPARE(alerts("web"), 0); QCOMPARE(alerts("pane"), 2);
        QVERIFY(monitor.apply(event("web", "error", "konsole"), now + seq));
        QCOMPARE(failure(Outcome::Unsupported, Outcome::Confirmed), QString("focused")); QCOMPARE(alerts("web"), 0);
        QVERIFY(!monitor.focusSession(key("missing"))); // An unknown session is not focused.
        // Without a focus service, nothing is raised or reported active.
        pet::Monitor bare(window);
        QVERIFY(!bare.hostActive); QVERIFY(!bare.bringForward);
        QVERIFY(bare.apply(event("lone", "error", "konsole"), now + seq));
        QToolTip::hideText(); QVERIFY(!bare.focusSession(key("lone")));
        QCOMPARE(QToolTip::text(), QString("This desktop session does not let Agent Pet raise windows."));
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
// QTEST_MAIN, plus "--shard K/N": run every Nth test function from the Kth on, so CTest can run the suite
// as parallel processes. The slowest functions are dealt first, so no shard gets two of them; every function
// runs in exactly one shard either way. initTestCase and cleanupTestCase run in each shard.
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    PrototypeTests tests;
    QTEST_SET_MAIN_SOURCE_PATH
    QStringList arguments;
    for (int i = 0; i < argc; ++i) arguments << QString::fromLocal8Bit(argv[i]);
    if (const auto at = arguments.indexOf("--shard"); at > 0) {
        const auto parts = arguments.value(at + 1).split('/');
        const int shard = parts.value(0).toInt(), shards = parts.value(1).toInt();
        if (parts.size() != 2 || shard < 1 || shard > shards) { qCritical("--shard expects K/N"); return 2; }
        arguments.remove(at, 2);
        QStringList functions;
        const auto *meta = tests.metaObject();
        for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
            const auto method = meta->method(i); const QByteArray name = method.name();
            if (method.methodType() != QMetaMethod::Slot || method.parameterCount() > 0 || name.endsWith("_data") ||
                name == "initTestCase" || name == "cleanupTestCase" || name == "init" || name == "cleanup") continue;
            functions << QString::fromLatin1(name);
        }
        int slow = 0;
        for (const auto *name : {"decorationFollowsEveryRequest", "everyIncludedFrameAndCacheBound", "everyHappyFrame", "everyPoorFrame"})
            if (const auto at = functions.indexOf(name); at >= 0) functions.move(at, slow++);
        // QTest runs every function when none is named, so a shard left without any runs nothing instead.
        if (shard > functions.size()) { qInfo("Shard %d/%d has no test functions", shard, shards); return 0; }
        for (int index = shard - 1; index < functions.size(); index += shards) arguments << functions.at(index);
    }
    return QTest::qExec(&tests, arguments);
}
#include "prototype_tests.moc"
