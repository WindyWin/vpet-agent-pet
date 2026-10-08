#include "animation/catalog.h"
#include "animation/pet_library.h"
#include "animation/easter_eggs.h"
#include "animation/player.h"
#include "desktop/pet_picker.h"
#include "desktop/pet_window.h"
#include "settings/preferences.h"
#include <QAction>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QProcess>
#include <QResource>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QToolButton>
#include <algorithm>
#include <utility>

namespace {
// A catalog for pet "test" in which every state cue's default state plays one idle frame with the cue's shape.
QJsonObject contractCatalog() {
    const QJsonObject frame{{"path", "assets/test/idle/_000_100.png"}, {"duration_ms", 100}};
    QJsonObject states, playback;
    for (const auto &cue : pet::cues()) {
        if (cue.reaction) continue;
        QJsonArray sequences;
        for (int phase = 0; phase < (cue.mode == "phased" ? 3 : 1); ++phase) sequences.append("idle");
        states[cue.state] = sequences;
        playback[cue.state] = QJsonObject{{"mode", cue.mode}, {"after", cue.after}};
    }
    return {{"schema_version", pet::catalogSchema}, {"asset_root", "assets/test"}, {"states", states}, {"playback", playback},
            {"sequences", QJsonArray{QJsonObject{{"path", "idle"}, {"duration_ms", 100}, {"frames", QJsonArray{frame}}}}}};
}
// Replaces one state's sequences and playback.
QJsonObject withState(QJsonObject catalog, const QString &state, const QJsonArray &sequences, const QJsonObject &policy) {
    auto states = catalog["states"].toObject(); auto playback = catalog["playback"].toObject();
    states[state] = sequences; playback[state] = policy;
    catalog["states"] = states; catalog["playback"] = playback;
    return catalog;
}
QJsonObject withFrame(QJsonObject catalog, const QString &path) {
    auto sequences = catalog["sequences"].toArray(); auto sequence = sequences[0].toObject();
    sequence["frames"] = QJsonArray{QJsonObject{{"path", path}, {"duration_ms", 100}}};
    sequences[0] = sequence; catalog["sequences"] = sequences;
    return catalog;
}
// Loads `catalog` as pet "test" from a temporary folder. Parsing never opens frames.
pet::Catalog load(const QJsonObject &catalog, QString *error = nullptr) {
    QTemporaryDir root;
    QDir().mkpath(root.path() + "/assets/test");
    QFile file(root.path() + "/assets/test/animations.json");
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QJsonDocument(catalog).toJson()); file.close();
    QString reason;
    auto loaded = pet::Catalog::load(root.path(), "test", &reason);
    if (error) *error = reason;
    return loaded;
}
QString sha256(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(); }
QByteArray contents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
// Recomputes pet `id`'s hash tree from the index mounted at `root` and the packs in `directory`, and compares
// it with the pet's packs.json. Empty when everything matches; otherwise what differs.
QString treeMismatch(const QString &root, const QString &id, const QString &directory) {
    const QDir pet(QDir(root).filePath("assets/" + id));
    const auto tree = QJsonDocument::fromJson(contents(pet.filePath("packs.json"))).object();
    const auto preview = QJsonDocument::fromJson(contents(pet.filePath("pet.json"))).object()["preview"].toString();
    const auto catalog = sha256(QString("pet.json %1\nanimations.json %2\n%3 %4\n")
        .arg(sha256(contents(pet.filePath("pet.json"))), sha256(contents(pet.filePath("animations.json"))),
             preview, sha256(contents(pet.filePath(preview)))).toUtf8());
    if (tree["catalog"].toString() != "sha256:" + catalog) return id + ": catalog digest";
    QString text = "agent-pet-pet-tree 1\ncatalog " + catalog + "\n", previous;
    const auto packs = tree["packs"].toArray();
    if (packs.isEmpty()) return id + ": no packs";
    for (const auto &value : packs) {
        const auto pack = value.toObject();
        const auto name = pack["name"].toString();
        const auto bytes = contents(QDir(directory).filePath(name + ".rcc"));
        // A pack's name is the SHA-256 of its frame folder's resource path, as the updater has always seen it.
        if (name != "artwork-" + sha256(("assets/" + id + "/" + pack["sequence"].toString()).toUtf8()))
            return id + ": name of " + pack["sequence"].toString();
        if (bytes.isEmpty() || pack["sha256"].toString() != sha256(bytes) || pack["bytes"].toInteger() != bytes.size())
            return id + ": leaf " + name;
        if (name <= previous) return id + ": packs out of order";
        previous = name;
        text += name + " " + sha256(bytes) + "\n";
    }
    if (tree["root"].toString() != "sha256:" + sha256(text.toUtf8())) return id + ": root digest";
    return {};
}
}
class PetsTests : public QObject {
    Q_OBJECT
    QTemporaryDir clients;
private slots:
    void initTestCase() {
        qputenv("CLAUDE_CONFIG_DIR", QFile::encodeName(clients.path() + "/claude"));
        qputenv("CODEX_HOME", QFile::encodeName(clients.path() + "/codex"));
        pet::EasterEggs::defaultClock = [] { return QDateTime(QDate(2026, 10, 7), QTime(12, 0)); };
    }
    void cuesComeFromTheirDataFile() {
        const auto &cues = pet::cues();
        QCOMPARE(cues.size(), 32);
        QVERIFY(std::is_sorted(cues.begin(), cues.end(), [](const pet::Cue &a, const pet::Cue &b) { return a.name < b.name; }));
        auto shape = [](const QString &name) {
            const auto *cue = pet::findCue(name);
            return cue ? cue->state + ":" + cue->mode + "/" + cue->after : QString();
        };
        QCOMPARE(shape("idle"), QString("idle:loop/idle")); QVERIFY(pet::findCue("idle")->fixed);
        QCOMPARE(shape("waiting"), QString("idle:loop/idle"));
        QCOMPARE(shape("start"), QString("starting:once/idle"));
        QCOMPARE(shape("error"), QString("tool_error:once/previous"));
        QCOMPARE(shape("quit-angry"), QString("closing_angry:once/stop"));
        QCOMPARE(shape("drag"), QString("dragging:phased/idle"));
        QCOMPARE(shape("exhausted"), QString("out_of_quota:loop/idle"));
        QVERIFY(pet::findCue("celebrate")->reaction); QVERIFY(pet::findCue("danger")->reaction);
        QVERIFY(!pet::findCue("nobody")); QVERIFY(!pet::findCue("turn_finished")); // Cue names use hyphens.
        // Every session aggregate is a state cue of the same name.
        for (const auto *state : {"attention", "exhausted", "error", "turn-finished", "working", "reading", "thinking",
                                  "waiting", "idle", "inactive"})
            QVERIFY2(pet::findCue(state) && !pet::findCue(state)->reaction, state);
    }
    void vpetMeetsTheContract() {
        QString error;
        const auto catalog = pet::Catalog::load(PET_SOURCE, "vpet", &error);
        QVERIFY2(catalog.valid(), qPrintable(error));
        QVERIFY2(catalog.contractError().isEmpty(), qPrintable(catalog.contractError()));
        QVERIFY(catalog.activity.contains("thinking")); QVERIFY(catalog.touch.scale > 0); QVERIFY(catalog.moveScale > 0);
        QCOMPARE(catalog.sequences.value("Default/Nomal/1").first().path,
                 QDir(PET_SOURCE).filePath("assets/vpet/vup/Default/Nomal/1/_000_250.png"));
    }
    void contractIsEnforced() {
        const auto catalog = contractCatalog();
        QVERIFY(load(catalog).valid()); QCOMPARE(load(catalog).contractError(), QString());
        auto missing = catalog;
        auto states = missing["states"].toObject(); states.remove("sleeping"); missing["states"] = states;
        auto playback = missing["playback"].toObject(); playback.remove("sleeping"); missing["playback"] = playback;
        const auto partial = load(missing);
        QVERIFY(partial.valid()); // Still a catalog a Player can play...
        QCOMPARE(partial.contractError(), QString("Cue inactive plays missing state sleeping")); // ...but not a pet the app can run.
        QCOMPARE(load(withState(catalog, "tool_error", {"idle"}, {{"mode", "once"}, {"after", "idle"}})).contractError(),
                 QString("Cue error must play once, then previous; state tool_error does not"));
        QVERIFY(load(withState(catalog, "thinking", {"idle"}, {{"mode", "once"}, {"after", "idle"}}))
                    .contractError().contains("thinking"));
        // A phased state cue ends when the app moves on, never after a count of loops.
        QVERIFY(load(withState(catalog, "reading", {"idle", "idle", "idle"}, {{"mode", "phased"}, {"after", "idle"}, {"loops", 2}}))
                    .contractError().contains("reading"));
    }
    void catalogsMapCues() {
        auto catalog = withState(contractCatalog(), "busy", {"idle", "idle", "idle"}, {{"mode", "phased"}, {"after", "idle"}});
        catalog = withState(catalog, "cheer", {"idle"}, {{"mode", "once"}, {"after", "idle"}});
        auto mapped = [&](const QJsonObject &cues) { auto copy = catalog; copy["cues"] = cues; return copy; };
        QString error;
        // A state cue plays the state the catalog names; the others keep their defaults.
        auto loaded = load(mapped({{"attention", "busy"}, {"celebrate", QJsonArray{QJsonObject{{"state", "cheer"}, {"weight", 2}}}}}), &error);
        QVERIFY2(loaded.valid(), qPrintable(error));
        QCOMPARE(loaded.stateFor("attention"), QString("busy")); QCOMPARE(loaded.stateFor("thinking"), QString("thinking"));
        QCOMPARE(loaded.stateFor("celebrate"), QString()); QCOMPARE(loaded.stateFor("nobody"), QString());
        QCOMPARE(loaded.pools.value("celebrate").size(), 1); QCOMPARE(loaded.pools.value("celebrate").first().weight, 2);
        QVERIFY(!loaded.pools.contains("snack")); // An unmapped reaction cue plays nothing.
        QCOMPARE(loaded.contractError(), QString());
        // Once mapped away, a default state need not exist.
        auto states = catalog["states"].toObject(); states.remove("needs_input"); catalog["states"] = states;
        auto playback = catalog["playback"].toObject(); playback.remove("needs_input"); catalog["playback"] = playback;
        QCOMPARE(load(mapped({{"attention", "busy"}})).contractError(), QString());
        QCOMPARE(load(catalog).contractError(), QString("Cue attention plays missing state needs_input"));
        // The mapped state must have the cue's shape, and the drag state is the drag cue's alone.
        QCOMPARE(load(mapped({{"attention", "cheer"}})).contractError(),
                 QString("Cue attention must play phased, then idle; state cheer does not"));
        QCOMPARE(load(mapped({{"attention", "busy"}, {"drag", "busy"}})).contractError(), QString("Cue attention shares the drag state busy"));
        // Load refuses names outside the vocabulary, unknown states, a fixed cue moved, and pools that never end.
        QVERIFY(!load(mapped({{"turn_finished", "cheer"}}), &error).valid()); QCOMPARE(error, QString("Unknown cue: turn_finished"));
        QVERIFY(!load(mapped({{"attention", "nobody"}}), &error).valid()); QCOMPARE(error, QString("Invalid state for cue attention"));
        QVERIFY(!load(mapped({{"attention", QJsonArray{"busy"}}})).valid());
        QVERIFY(!load(mapped({{"idle", "busy"}})).valid()); QVERIFY(load(mapped({{"idle", "idle"}})).valid());
        QVERIFY(!load(mapped({{"danger", QJsonArray{}}})).valid());
        QVERIFY(!load(mapped({{"danger", QJsonArray{QJsonObject{{"state", "busy"}, {"weight", 1}}}}})).valid());
        QVERIFY(!load(mapped({{"danger", "cheer"}})).valid());
        auto notObject = catalog; notObject["cues"] = QJsonArray{}; QVERIFY(!load(notObject).valid());
        // A catalog from before cues asks to be migrated.
        auto old = contractCatalog(); old["schema_version"] = 1;
        QVERIFY(!load(old, &error).valid());
        QCOMPARE(error, QString("Animation catalog schema 1 is out of date: run scripts/migrate_catalog.py."));
        old["schema_version"] = 3; QVERIFY(!load(old, &error).valid()); QCOMPARE(error, QString("Invalid animation catalog format."));
    }
    void framesStayInsideThePet() {
        QString error;
        auto outside = contractCatalog(); outside["asset_root"] = "assets/other";
        QVERIFY(!load(outside, &error).valid()); QVERIFY2(error.contains("Asset root"), qPrintable(error));
        auto escaping = contractCatalog(); escaping["asset_root"] = "assets/test/../other";
        QVERIFY(!load(escaping, &error).valid());
        auto narrower = contractCatalog(); narrower["asset_root"] = "assets/test/art"; // The idle frame is outside it.
        QVERIFY(!load(narrower, &error).valid()); QVERIFY2(error.contains("Invalid frame path"), qPrintable(error));
        // A frame at the pet folder's root would never be packed.
        QVERIFY(!load(withFrame(contractCatalog(), "assets/test/_000_100.png")).valid());
        QVERIFY(!load(withFrame(contractCatalog(), "assets/other/idle/_000_100.png")).valid());
        auto implicit = contractCatalog(); implicit.remove("asset_root"); // Defaults to the pet's folder.
        QVERIFY(load(implicit).valid());
        QVERIFY(!pet::Catalog::load(PET_SOURCE, "../assets", &error).valid());
        QVERIFY2(error.contains("Invalid pet identifier"), qPrintable(error));
        QVERIFY(!pet::Catalog::load(PET_SOURCE, "nosuch", &error).valid());
        QVERIFY(pet::validPetId("vpet")); QVERIFY(pet::validPetId("cat-2"));
        QVERIFY(!pet::validPetId("cat\n")); QVERIFY(!pet::validPetId(QString(32, 'a') + "\n")); // `$` would let these pass.
        QVERIFY(!pet::validPetId("Cat")); QVERIFY(!pet::validPetId("")); QVERIFY(!pet::validPetId(QString(33, 'a')));
    }
    void hashTreesMatchTheBuiltPacks() {
        // Every bundled pet folder is a pet, so adding one needs no edit here.
        QStringList bundled;
        const QDir assets(QString(PET_SOURCE) + "/assets");
        for (const auto &id : assets.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            if (QFileInfo::exists(assets.filePath(id + "/pet.json"))) bundled.append(id);
        QVERIFY(bundled.contains("vpet"));
        for (const auto &[index, pets] : {std::pair{QString(PET_INDEX), bundled},
                                          std::pair{QString(FIXTURE_INDEX), QStringList{"broken", "duo", "mini"}}}) {
            QVERIFY2(QResource::registerResource(index, "/tree"), qPrintable(index));
            const QString indexPath = index; // A lambda cannot capture a structured binding in C++17.
            const auto unregister = qScopeGuard([indexPath] { QResource::unregisterResource(indexPath, "/tree"); });
            QCOMPARE(QDir(":/tree/assets").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name), pets);
            for (const auto &id : pets) {
                const auto mismatch = treeMismatch(":/tree", id, QFileInfo(index).absolutePath());
                QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));
            }
            // Listing pets reads the index only: no frame of any pet is a resource yet.
            // (Any other bundled pet's frames are unknown here; the tree check above covers its packs.)
            const bool isVpet = pets.contains("vpet");
            QVERIFY(!QFile::exists(":/tree/assets/" + (isVpet ? QString("vpet") : pets.first()) + "/" + (isVpet ? "vup/Default/Nomal/1/_000_250.png" : "idle/_000_100.png")));
        }
        const auto duo = QJsonDocument::fromJson(contents(QFileInfo(FIXTURE_INDEX).absolutePath() + "/pets/duo/packs.json")).object();
        QCOMPARE(duo["packs"].toArray().size(), 2); // One pack per frame folder.
    }
    void libraryListsPetsWithoutTheirFrames() {
        pet::PetLibrary library(FIXTURE_INDEX, "/listing");
        QVERIFY2(library.error().isEmpty(), qPrintable(library.error()));
        QCOMPARE(library.root(), QString(":/listing"));
        const auto pets = library.pets();
        QCOMPARE(pets.size(), 3);
        QCOMPARE(pets[0].id, QString("broken")); QCOMPARE(pets[1].id, QString("duo")); QCOMPARE(pets[2].id, QString("mini"));
        QCOMPARE(pets[2].name, QString("Mini")); QCOMPARE(pets[2].author, QString("Agent Pet tests"));
        QCOMPARE(pets[2].terms, QString("LICENSE")); QVERIFY(pets[2].url.isEmpty());
        QCOMPARE(pets[2].preview, QString(":/listing/assets/mini/preview.png"));
        QVERIFY(!QPixmap(pets[2].preview).isNull()); // Previews come from the index.
        QVERIFY(!QFile::exists(":/listing/assets/mini/idle/_000_100.png")); // No pack is registered.
        QVERIFY(library.active().isEmpty()); QVERIFY(!library.catalog().valid());
        QVERIFY(library.info("nosuch").id.isEmpty()); QVERIFY(library.info("../x").id.isEmpty());
        pet::PetLibrary missing("/nonexistent/artwork.rcc", "/missing");
        QVERIFY(!missing.error().isEmpty()); QVERIFY(missing.pets().isEmpty());
        QString error; QVERIFY(!missing.activate("mini", &error)); QVERIFY(!error.isEmpty());
    }
    void activateRegistersOnlyThatPet() {
        pet::PetLibrary library(FIXTURE_INDEX, "/activate");
        QString error;
        QVERIFY2(library.activate("duo", &error), qPrintable(error));
        QCOMPARE(library.active(), QString("duo"));
        QVERIFY(QFile::exists(":/activate/assets/duo/idle/_000_100.png"));
        QVERIFY(QFile::exists(":/activate/assets/duo/wave/_000_50.png"));
        QVERIFY(!QFile::exists(":/activate/assets/mini/idle/_000_100.png"));
        QVERIFY(library.catalog().valid());
        QCOMPARE(library.catalog().sequences.value("wave").first().path, QString(":/activate/assets/duo/wave/_000_50.png"));
        QVERIFY(!library.activate("mini", &error)); QVERIFY2(error.contains("already active"), qPrintable(error));
        QVERIFY(!QFile::exists(":/activate/assets/mini/idle/_000_100.png"));
        QVERIFY(QFile::exists(":/activate/assets/duo/wave/_000_50.png")); // The active pet keeps its packs.
    }
    void failedActivationRollsBack() {
        // A copy of the fixture build that lacks duo's second pack.
        QTemporaryDir directory;
        const QDir built(QFileInfo(FIXTURE_INDEX).absolutePath());
        for (const auto &name : built.entryList({"*.rcc"}, QDir::Files))
            QVERIFY(QFile::copy(built.filePath(name), directory.filePath(name)));
        const auto tree = QJsonDocument::fromJson(contents(built.filePath("pets/duo/packs.json"))).object();
        const auto last = tree["packs"].toArray().last().toObject()["name"].toString();
        QVERIFY(QFile::remove(directory.filePath(last + ".rcc")));
        pet::PetLibrary library(directory.filePath("artwork.rcc"), "/rollback");
        QString error;
        QVERIFY(!library.activate("duo", &error)); QVERIFY2(error.contains(last), qPrintable(error));
        QVERIFY(library.active().isEmpty()); QVERIFY(!library.catalog().valid());
        QVERIFY(!QFile::exists(":/rollback/assets/duo/idle/_000_100.png"));
        QVERIFY(!QFile::exists(":/rollback/assets/duo/wave/_000_50.png"));
        QVERIFY2(library.activate("mini", &error), qPrintable(error)); // A failure leaves the library usable.
    }
    void incompletePacksAreRefused() {
        // A copy of the fixture build whose wave pack holds duo's idle frames instead: it registers, but lacks wave.
        QTemporaryDir directory;
        const QDir built(QFileInfo(FIXTURE_INDEX).absolutePath());
        for (const auto &name : built.entryList({"*.rcc"}, QDir::Files))
            QVERIFY(QFile::copy(built.filePath(name), directory.filePath(name)));
        QHash<QString, QString> packs;
        for (const auto &pack : QJsonDocument::fromJson(contents(built.filePath("pets/duo/packs.json"))).object()["packs"].toArray())
            packs.insert(pack.toObject()["sequence"].toString(), pack.toObject()["name"].toString() + ".rcc");
        QVERIFY(QFile::remove(directory.filePath(packs.value("wave"))));
        QVERIFY(QFile::copy(built.filePath(packs.value("idle")), directory.filePath(packs.value("wave"))));
        pet::PetLibrary library(directory.filePath("artwork.rcc"), "/mismatch");
        QString error;
        QVERIFY(!library.activate("duo", &error));
        QCOMPARE(error, QString("Missing frame assets/duo/wave/_000_50.png. Reinstall Agent Pet to restore it."));
        QVERIFY(library.active().isEmpty()); QVERIFY(!library.catalog().valid());
        QVERIFY(!QFile::exists(":/mismatch/assets/duo/idle/_000_100.png"));
        QVERIFY2(library.activate("mini", &error), qPrintable(error));

        // A wave pack with its first frame only, built here with rcc: every frame is checked, not just the first.
        QVERIFY(QFile::copy(PET_SOURCE "/tests/fixtures/pets/duo/wave/_000_50.png", directory.filePath("_000_50.png")));
        QFile qrc(directory.filePath("partial.qrc"));
        QVERIFY(qrc.open(QIODevice::WriteOnly));
        qrc.write(R"(<RCC><qresource prefix="/"><file alias="assets/duo/wave/_000_50.png">_000_50.png</file></qresource></RCC>)");
        qrc.close();
        QVERIFY(QFile::remove(directory.filePath(packs.value("wave"))));
        QCOMPARE(QProcess::execute(RCC, {"--binary", "-o", directory.filePath(packs.value("wave")), qrc.fileName()}), 0);
        pet::PetLibrary partial(directory.filePath("artwork.rcc"), "/partial");
        QVERIFY(!partial.activate("duo", &error));
        QCOMPARE(error, QString("Missing frame assets/duo/wave/_001_50.png. Reinstall Agent Pet to restore it."));
        QVERIFY(partial.active().isEmpty());
        QVERIFY(!QFile::exists(":/partial/assets/duo/wave/_000_50.png"));
    }
    void brokenPetIsRefused() {
        pet::PetLibrary library(FIXTURE_INDEX, "/broken");
        QString error;
        QVERIFY(!library.activate("broken", &error));
        QCOMPARE(error, QString("Cue inactive plays missing state sleeping"));
        QVERIFY(!QFile::exists(":/broken/assets/broken/idle/_000_100.png"));
        QVERIFY(!library.activate("nosuch", &error)); QVERIFY(!error.isEmpty());
        QVERIFY2(library.activate("mini", &error), qPrintable(error));
    }
    void everyCuePlaysOnEveryPet() {
        // mini is what new_pet.py scaffolds (one state per playback shape, every cue mapped onto them); duo keeps the
        // default states; VPet is the bundled pet.
        for (const auto &[index, id] : {std::pair{QString(FIXTURE_INDEX), QString("mini")}, std::pair{QString(FIXTURE_INDEX), QString("duo")},
                                        std::pair{QString(PET_INDEX), QString("vpet")}}) {
            pet::PetLibrary library(index, "/cues-" + id);
            QString error; QVERIFY2(library.activate(id, &error), qPrintable(id + ": " + error));
            pet::Player player(nullptr, library.catalog()); player.setPaused(true);
            QVERIFY2(player.valid(), qPrintable(player.error()));
            for (const auto &cue : pet::cues()) {
                const auto what = qPrintable(id + " " + cue.name + ": " + player.error());
                if (cue.reaction) {
                    for (const auto &reaction : player.pool(cue.name)) QVERIFY2(player.select(reaction.state, true), what);
                    continue;
                }
                QVERIFY2(player.play(cue.name, true), what);
                QCOMPARE(player.state(), player.stateFor(cue.name));
                QVERIFY2(!player.pixmap().isNull() && player.error().isEmpty(), what);
            }
            if (id == "mini") {
                QCOMPARE(player.stateFor("attention"), QString("busy")); QCOMPARE(player.stateFor("drag"), QString("held"));
                player.play("working", true); player.beginDrag(); QVERIFY(player.isDragging()); QCOMPARE(player.state(), QString("held"));
                player.endDrag(); QCOMPARE(player.requestedState(), QString("busy"));
            }
        }
    }
    void bundledPetsMeetTheContract() {
        pet::PetLibrary library(PET_INDEX, "/bundled");
        const auto pets = library.pets();
        QVERIFY(std::any_of(pets.begin(), pets.end(), [](const pet::PetInfo &pet) { return pet.id == "vpet"; }));
        for (const auto &pet : pets) {
            QString error;
            const auto catalog = pet::Catalog::load(library.root(), pet.id, &error);
            QVERIFY2(catalog.valid(), qPrintable(pet.id + ": " + error));
            QVERIFY2(catalog.contractError().isEmpty(), qPrintable(pet.id + ": " + catalog.contractError()));
        }
        const auto vpet = library.info("vpet");
        QCOMPARE(vpet.author, QString("VUP-Simulator team"));
        QCOMPARE(vpet.url, QString("https://github.com/LorisYounger/VPet"));
        QCOMPARE(vpet.terms, QString("VPET-ARTWORK-TERMS.md"));
        // The app's default player activates VPet in the shared library.
        pet::Player player; player.setPaused(true);
        QVERIFY2(player.valid(), qPrintable(player.error())); QVERIFY(!player.pixmap().isNull());
        QCOMPARE(pet::PetLibrary::shared().active(), QString("vpet"));
        QCOMPARE(pet::PetLibrary::shared().root(), QString(":/"));
    }
    void petPreference() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        pet::PreferencesStore store(path); pet::Preferences preferences;
        QCOMPARE(preferences.pet, QString("vpet"));
        preferences.pet = "cat-2"; QVERIFY(store.save(preferences));
        QCOMPARE(pet::PreferencesStore(path).load().pet, QString("cat-2")); // Unknown to this build, but kept.
        auto write = [&](const QByteArray &json) {
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(json);
        };
        write(R"({"version": 1, "size": 240, "on_top": true, "pet": "../Cat"})");
        pet::PreferencesStore invalidId(path);
        QCOMPARE(invalidId.load().pet, QString("vpet")); QVERIFY(invalidId.error().isEmpty()); // Reads as VPet.
        write(R"({"version": 1, "size": 240, "on_top": true, "pet": 3})");
        pet::PreferencesStore wrongType(path);
        QCOMPARE(wrongType.load().pet, QString("vpet")); QVERIFY(!wrongType.error().isEmpty()); // An invalid file.
        write(R"({"version": 1, "size": 240, "on_top": true})");
        QCOMPARE(pet::PreferencesStore(path).load().pet, QString("vpet")); // Files from earlier versions.
        QVERIFY(pet::Preferences::validPet("mini")); QVERIFY(!pet::Preferences::validPet("Mini"));
        QVERIFY(!pet::Preferences::validPet("")); QVERIFY(!pet::Preferences::validPet(QString(33, 'a')));
        QVERIFY(!pet::Preferences::validPet("cat\n")); // A trailing newline is not part of an id.
    }
    void windowSavesTheChosenPet() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path);
            QCOMPARE(window.pet(), QString("vpet"));
            window.setPet("mini");
            QCOMPARE(window.pet(), QString("mini"));
            QCOMPARE(pet::PetLibrary::shared().active(), QString("vpet")); // Applies on the next start.
            window.setPet("Not valid"); QCOMPARE(window.pet(), QString("mini"));
            window.setOnTop(false); // Any later save keeps the choice.
        }
        QCOMPARE(pet::PreferencesStore(path).load().pet, QString("mini"));
        pet::PetWindow again(nullptr, path);
        QCOMPARE(again.pet(), QString("mini"));
    }
    void pickerChoosesForTheNextStart() {
        // Previews that do not exist: a tile still shows the pet's name and works.
        const pet::PetInfo vpet{"vpet", "VUP", "VUP-Simulator team", {}, "VPET-ARTWORK-TERMS.md", ":/missing/vpet.png"};
        const pet::PetInfo mini{"mini", "Mini", "Agent Pet tests", {}, "LICENSE", ":/missing/mini.png"};
        // A parentless widget counts as hidden until shown, so the pickers sit in a stand-in Settings page.
        QWidget page;
        pet::PetPicker alone({vpet}, "vpet", "vpet", &page);
        QVERIFY(!alone.isVisibleTo(&page)); // Nothing to choose from.
        pet::PetPicker picker({mini, vpet}, "vpet", "vpet", &page);
        QVERIFY(picker.isVisibleTo(&page));
        QToolButton *miniTile = nullptr, *vpetTile = nullptr;
        for (auto *tile : picker.findChildren<QToolButton *>())
            (tile->property("pet").toString() == "mini" ? miniTile : vpetTile) = tile;
        QVERIFY(miniTile && vpetTile);
        QVERIFY(vpetTile->isChecked()); QVERIFY(!miniTile->isChecked());
        QCOMPARE(miniTile->accessibleName(), QString("Mini"));
        QCOMPARE(miniTile->toolTip(), QString("by Agent Pet tests"));
        QVERIFY(miniTile->text().contains("Mini"));
        auto *note = picker.findChild<QLabel *>();
        QVERIFY(note); QVERIFY(!note->isVisibleTo(&page));
        QSignalSpy chosen(&picker, &pet::PetPicker::chosen);
        miniTile->click();
        QCOMPARE(chosen.size(), 1); QCOMPARE(chosen.first().first().toString(), QString("mini"));
        QVERIFY(miniTile->isChecked()); QVERIFY(!vpetTile->isChecked()); // Exclusive.
        QCOMPARE(picker.selected(), QString("mini"));
        QVERIFY(note->isVisibleTo(&page));
        QCOMPARE(note->text(), QString("Mini will appear the next time Agent Pet starts."));
        vpetTile->click();
        QCOMPARE(chosen.size(), 2); QVERIFY(!note->isVisibleTo(&page)); // Back to the running pet.
        vpetTile->click();
        QCOMPARE(chosen.size(), 2); // The checked tile again changes nothing.
        // A saved choice this build does not know shows the running pet and saves nothing by itself.
        pet::PetPicker unknown({mini, vpet}, "cat", "vpet", &page);
        QCOMPARE(unknown.selected(), QString("vpet"));
        QSignalSpy quiet(&unknown, &pet::PetPicker::chosen);
        QCOMPARE(quiet.size(), 0);
    }
    void pickerShowsNamesAndAuthorsAsWritten() {
        const pet::PetInfo vpet{"vpet", "VUP", "VUP-Simulator team", {}, "VPET-ARTWORK-TERMS.md", {}};
        const pet::PetInfo odd{"odd", "Tom & <b>Jerry</b>", "<i>A</i>", {}, "LICENSE", {}};
        QWidget page;
        pet::PetPicker picker({odd, vpet}, "vpet", "vpet", &page);
        QToolButton *tile = nullptr;
        for (auto *candidate : picker.findChildren<QToolButton *>())
            if (candidate->property("pet").toString() == "odd") tile = candidate;
        QVERIFY(tile);
        QCOMPARE(tile->text(), QString("Tom && <b>Jerry</b>")); // `&&` shows one `&`, not a mnemonic.
        QCOMPARE(tile->accessibleName(), QString("Tom & <b>Jerry</b>"));
        QVERIFY(tile->toolTip().contains("&lt;i&gt;"));
        tile->click();
        QCOMPARE(tile->text(), QString("✓ Tom && <b>Jerry</b>"));
        auto *note = picker.findChild<QLabel *>();
        QVERIFY(note);
        QCOMPARE(note->textFormat(), Qt::PlainText);
        QVERIFY(note->text().contains("Tom & <b>Jerry</b>"));
    }
    void settingsShowThePickerOnlyWithSeveralPets() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json", false);
        window.showSettings();
        // One bundled pet leaves nothing to choose; the picker-level cases cover that.
        QCOMPARE(window.findChild<pet::PetPicker *>() != nullptr, pet::PetLibrary::shared().pets().size() > 1);
    }
    void aboutCreditsTheRunningPet() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json", false);
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == "About and artwork terms…") action->trigger();
        QDialog *about = nullptr;
        for (auto *dialog : window.findChildren<QDialog *>())
            if (dialog->windowTitle() == "About Agent Pet — artwork and terms") about = dialog;
        QVERIFY(about);
        bool credited = false;
        for (auto *label : about->findChildren<QLabel *>())
            credited = credited || label->text().contains(
                "Artwork: VUP-Simulator team, via <a href=\"https://github.com/LorisYounger/VPet\">github.com/LorisYounger/VPet</a>, "
                "under its own terms below.");
        QVERIFY(credited);
        auto *terms = about->findChild<QTextBrowser *>();
        QVERIFY(terms); QVERIFY(terms->toPlainText().contains("Animation copyright notice"));
        QVERIFY(!terms->toPlainText().contains("Artwork terms unavailable."));
    }
};
QTEST_MAIN(PetsTests)
#include "pets_tests.moc"
