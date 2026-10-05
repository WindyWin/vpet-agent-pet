#include "desktop/monitor.h"
#include "desktop/pet_window.h"
#include "desktop/session_playback.h"
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
#include <QSignalSpy>
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
        QSet<QString> reactions;
        for (const auto *name : {"turn_finished", "snack", "milestone", "long_turn", "friday_evening", "may20", "birthday",
                                 "konami", "danger"})
            for (const auto &reaction : player.reactions(name)) reactions.insert(reaction.state);
        QCOMPARE(reactions.size(), 10);
        // Late at night the pet yawns more, with a fidget it already has.
        QCOMPARE(player.reactions("late_night").size(), 1); QVERIFY(player.isFidget(player.reactions("late_night").first().state));
        // And the touch reactions: three held presses, two falls and two edges.
        const auto &touch = player.touch();
        QSet<QString> touches{touch.fallLeft, touch.fallRight, touch.edgeLeft, touch.edgeRight};
        for (const auto &region : touch.regions) touches.insert(region.state);
        QCOMPARE(touches.size(), 7);
        for (const auto &state : touches) QVERIFY2(player.isTouch(state), qPrintable(state));
        QCOMPARE(player.states().size(), 11 + reactions.size() - 1 + player.fidgets().size() + touches.size());
        QVERIFY(!player.fidgets().isEmpty());
        auto checkSequence = [&] {
            const int count = player.frameCount();
            for (int i = 0; i < count; ++i) {
                QVERIFY2(!player.pixmap().isNull(), qPrintable(player.error()));
                QVERIFY(player.pixmap().width() <= 640); QVERIFY(player.pixmap().height() <= 640);
                QVERIFY(player.cacheKiB() <= pet::Player::cacheLimitKiB); player.advance();
            }
        };
        // Every state under every mood, so each mood's art is decoded too.
        for (const auto *mood : {"", "happy", "poor"}) for (const auto &state : player.states()) {
            player.setMood(mood);
            QVERIFY(player.select(state, true));
            checkSequence();
            if (player.isFidget(state) || reactions.contains(state)) { // Plays itself out and hands back to idle.
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
        // An edge counts once an eighth of the window is past an outer side of its screen.
        const QVector<QRect> one{QRect(0, 0, 1000, 800)};
        QCOMPARE(pushedEdge(QRect(-29, 100, 240, 240), one), Edge::None);
        QCOMPARE(pushedEdge(QRect(-30, 100, 240, 240), one), Edge::Left);
        QCOMPARE(pushedEdge(QRect(790, 100, 240, 240), one), Edge::Right);
        QCOMPARE(pushedEdge(QRect(400, -100, 240, 240), one), Edge::None); // Top and bottom do not hide.
        const QVector<QRect> two{QRect(0, 0, 1000, 800), QRect(1000, 0, 1000, 800)};
        QCOMPARE(pushedEdge(QRect(790, 100, 240, 240), two), Edge::None); // The next screen goes on.
        QCOMPARE(pushedEdge(QRect(1790, 100, 240, 240), two), Edge::Right);
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
        player.beginDrag(); window.move(area.left() - 60, area.top() + 50); window.letGo({});
        QVERIFY(!window.hiding()); QCOMPARE(window.pos().x(), area.left());
        playOut(player); QVERIFY(monitor.apply(event("interrupt"), now + seq)); playOut(player);
        QCOMPARE(player.state(), QString("idle"));
        // Idle, it hides there once the drag's end has played, and the monitor's periodic update leaves it hiding.
        player.beginDrag(); window.move(area.left() - 60, area.top() + 50); window.letGo({});
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
        player.beginDrag(); window.move(area.right() - window.width() + 60, area.top()); window.letGo({});
        QTRY_COMPARE_WITH_TIMEOUT(player.requestedState(), QString("edge_right"), 2000); finishSequence(player); QCOMPARE(window.edge(), pet::touch::Edge::Right);
        QCOMPARE(window.pos().x(), area.right() + 1 - qRound(281.0 * window.width() / 500));
        player.beginDrag(); QVERIFY(!window.hiding()); QCOMPARE(player.requestedState(), QString("dragging"));
        window.letGo({}); QCOMPARE(player.requestedState(), QString("idle")); playOut(player); QVERIFY(!window.hiding());
        // Recovering the position brings it out too, so nothing puts it back behind the edge later.
        player.beginDrag(); window.move(area.left() - 60, area.top()); window.letGo({});
        QTRY_COMPARE_WITH_TIMEOUT(player.requestedState(), QString("edge_left"), 2000); finishSequence(player);
        QVERIFY(window.hiding()); window.recover(); QVERIFY(!window.hiding()); QCOMPARE(player.state(), QString("idle"));
        window.constrainPosition(); QVERIFY(area.contains(window.geometry()));
        // Off, nothing falls or hides.
        window.setTouchEnabled(false);
        player.beginDrag(); window.letGo({-3000, 0}); QVERIFY(!window.flying());
        player.beginDrag(); window.move(area.left() - 60, area.top()); window.letGo({});
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
        // The Konami code completes on its last key, also after a false start.
        const QList<int> code{Qt::Key_Up, Qt::Key_Up, Qt::Key_Down, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right,
                              Qt::Key_Left, Qt::Key_Right, Qt::Key_B};
        QVERIFY(!eggs.key(Qt::Key_Up));
        for (const int key : code) QVERIFY(!eggs.key(key));
        QVERIFY(eggs.key(Qt::Key_A)); QVERIFY(!eggs.key(Qt::Key_A));
        // A surprise plays a pool at once; turned off, it plays nothing.
        Draws draws; eggs.setRandom(draws.random());
        QVERIFY(!eggs.surprise("danger")); QCOMPARE(player.state(), QString("idle"));
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
        local = QDateTime(QDate(2026, 10, 8), QTime(2, 0)); QToolTip::hideText();
        draws.values = {0}; QVERIFY(monitor.apply(event("turn_finished", now + 10), now + 10));
        QCOMPARE(QToolTip::text(), pet::Monitor::bedtimeNote); QVERIFY(!window.eggs().bedtime()); // Used for tonight.
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
