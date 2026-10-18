#include "animation/catalog.h"
#include "animation/pet_library.h"
#include "animation/easter_eggs.h"
#include "animation/player.h"
#include "animation/stage.h"
#include "desktop/pet_picker.h"
#include "desktop/source_browser.h"
#include "packs/sources.h"
#include <QListWidget>
#include <QPointer>
#include "desktop/pet_window.h"
#include "settings/preferences.h"
#include <QAction>
#include <QCryptographicHash>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <functional>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QProcess>
#include <QResource>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QToolButton>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace {
void copyLocalPet(const QString &destination) {
    const QString fixture = PET_SOURCE "/tests/fixtures/local-pets/local-cat";
    QDirIterator it(fixture, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto source = it.next();
        const auto target = destination + "/local-cat/" + QDir(fixture).relativeFilePath(source);
        QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath())); QVERIFY(QFile::copy(source, target));
    }
}
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
        QStandardPaths::setTestModeEnabled(true); // The default pet store stays out of the real data folder.
        qputenv("CLAUDE_CONFIG_DIR", QFile::encodeName(clients.path() + "/claude"));
        qputenv("CODEX_HOME", QFile::encodeName(clients.path() + "/codex"));
        pet::EasterEggs::defaultClock = [] { return QDateTime(QDate(2026, 10, 7), QTime(12, 0)); };
    }
    void cuesComeFromTheirDataFile() {
        const auto &cues = pet::cues();
        QCOMPARE(cues.size(), 35);
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
        QVERIFY(pet::findCue("lunch")->reaction);
        QVERIFY(pet::findCue("movement-break")->reaction);
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
        QCOMPARE(QDir(library.root() + "/assets").entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList{"vpet"});
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
    void legacyCleanupPreservesInstalledPacksAndUnknownFiles() {
        QTemporaryDir data;
        const auto cache = data.path() + "/pets";
        const auto local = data.path() + "/packs/pets";
        copyLocalPet(local);
        QVERIFY(QDir().mkpath(cache));
        const QString blob = QString(64, 'a') + ".rcc";
        for (const auto &name : QStringList{blob, blob + ".partial", "yun.root", "keep.txt", "unknown.rcc"}) {
            QFile file(cache + "/" + name); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep");
        }
        pet::PetLibrary library(PET_INDEX, "/cleanup", cache, local);
        QCOMPARE(library.removeLegacyDownloads(), 3);
        QCOMPARE(library.removeLegacyDownloads(), 0);
        QVERIFY(QFile::exists(cache + "/keep.txt"));
        QVERIFY(QFile::exists(cache + "/unknown.rcc"));
        QVERIFY(QFile::exists(local + "/local-cat/pack.json"));
        QVERIFY(library.info("local-cat").error.isEmpty());
        // A linked legacy directory is never traversed or cleaned.
        const auto linked = data.path() + "/linked";
        if (QFile::link(cache, linked) && QFileInfo(linked).isSymLink()) {
            pet::PetLibrary other(PET_INDEX, "/cleanup-link", linked, local);
            QCOMPARE(other.removeLegacyDownloads(), 0);
        }
    }
    void retiredPetWithLocalReplacementIsPreserved() {
        QTemporaryDir data;
        copyLocalPet(data.path());
        QVERIFY(QDir(data.path()).rename("local-cat", "yun"));
        for (const auto *name : {"pet.json", "pack.json", "animations.json", "manifest.json"}) {
            const auto path = data.path() + "/yun/" + name;
            auto bytes = contents(path); bytes.replace("local-cat", "yun");
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bytes);
        }
        pet::PetLibrary library(PET_INDEX, "/replacement", {}, data.path());
        QVERIFY2(library.info("yun").error.isEmpty(), qPrintable(library.info("yun").error));
        QVERIFY(!library.retiredPet("yun"));
        QVERIFY(library.retiredPet("fat-cat"));
        QVERIFY(!library.retiredPet("local-cat"));
        QVERIFY(!library.retiredPet("vpet"));
    }
    void migrationSelectsVpetAndShowsGalleryOnce() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/preferences.json";
        pet::PreferencesStore store(path);
        pet::Preferences saved; saved.pet = "bocchi";
        QVERIFY(store.save(saved));
        {
            pet::PetWindow window(nullptr, path);
            window.migrateLegacyPet();
            QCOMPARE(window.pet(), QString("vpet"));
            QCOMPARE(store.load().pet, QString("vpet"));
            auto notices = window.findChildren<QMessageBox *>();
            QCOMPARE(notices.size(), 1);
            QVERIFY(notices.first()->text().contains("https://github.com/WindyWin/agent-pet-gallery/releases"));
            window.migrateLegacyPet();
            QCOMPARE(window.findChildren<QMessageBox *>().size(), 1);
        }
        pet::PetWindow again(nullptr, path);
        again.migrateLegacyPet();
        QVERIFY(again.findChildren<QMessageBox *>().isEmpty());
        saved.pet = "yun"; QVERIFY(store.save(saved));
        pet::PetWindow preview(nullptr, path, false);
        preview.migrateLegacyPet();
        QCOMPARE(store.load().pet, QString("yun"));
        QVERIFY(preview.findChildren<QMessageBox *>().isEmpty());
    }
    void localPetDiscoveryPlaybackAndTerms() {
        QTemporaryDir installed, store;
        copyLocalPet(installed.path());
        pet::PetLibrary library(PET_INDEX, "/local-play", store.path(), installed.path());
        const auto info = library.info("local-cat");
        QVERIFY2(info.error.isEmpty(), qPrintable(info.error));
        QCOMPARE(info.name, QString("Local Cat")); QCOMPARE(info.license, QString("Apache-2.0"));
        QVERIFY(!info.folder.isEmpty());
        QVERIFY(library.terms("local-cat").contains("Agent Pet test artwork"));
        const auto pets = library.pets();
        QVERIFY(std::any_of(pets.begin(), pets.end(), [](const pet::PetInfo &p) { return p.id == "local-cat" && p.error.isEmpty(); }));
        QString error;
        QVERIFY2(library.activate("local-cat", &error), qPrintable(error));
        pet::Player player(nullptr, library.catalog());
        QVERIFY(player.select("idle", true)); QVERIFY(!player.pixmap().isNull());
        QVERIFY(library.catalog().sequences.first().first().path.startsWith(installed.path()));
        library.removeLegacyDownloads();
        QVERIFY(QFile::exists(installed.filePath("local-cat/pack.json")));
        pet::PetLibrary updated(PET_INDEX, "/local-updated", store.path(), installed.path());
        QVERIFY2(updated.activate("local-cat", &error), qPrintable(error));
    }
    void localDisplaySnapshotDoesNotAuthorizeActivation() {
        QTemporaryDir installed, store;
        copyLocalPet(installed.path());
        pet::PetLibrary library(PET_INDEX, "/local-snapshot", store.path(), installed.path());
        QFile frame(installed.filePath("local-cat/idle/_000_100.png"));
        QVERIFY(frame.open(QIODevice::WriteOnly)); frame.write("changed"); frame.close();
        // Discovery does not touch artwork, including already invalid frames.
        library.refreshDisplaySnapshot();
        QVERIFY(library.info("local-cat").error.isEmpty());
        // Display calls reuse the snapshot, but it never authorizes loading changed artwork.
        for (int i = 0; i < 3; ++i) {
            QVERIFY(library.info("local-cat").error.isEmpty());
            const auto pets = library.pets();
            QVERIFY(std::any_of(pets.begin(), pets.end(), [](const pet::PetInfo &pet) {
                return pet.id == "local-cat" && pet.error.isEmpty();
            }));
        }
        QString error;
        QVERIFY(!library.activate("local-cat", &error)); QVERIFY(!error.isEmpty());
        QFile metadata(installed.filePath("local-cat/pack.json"));
        QVERIFY(metadata.open(QIODevice::WriteOnly)); metadata.write("{}"); metadata.close();
        QVERIFY(library.info("local-cat").error.isEmpty());
        library.refreshDisplaySnapshot();
        QVERIFY(!library.info("local-cat").error.isEmpty());
    }
    void localDisplayDiscoveryIsBounded() {
        QTemporaryDir installed, store;
        for (int i = 0; i < 257; ++i) QVERIFY(QDir().mkpath(installed.filePath("pack-" + QString::number(i))));
        pet::PetLibrary library(PET_INDEX, "/local-display-limit", store.path(), installed.path());
        library.refreshDisplaySnapshot();
        const auto pets = library.pets();
        QCOMPARE(std::count_if(pets.begin(), pets.end(), [](const pet::PetInfo &pet) { return !pet.folder.isEmpty(); }), 256);
        // Metadata and preview paths are bounded independently of activation's full-artwork checks.
        QTemporaryDir valid;
        copyLocalPet(valid.path());
        pet::PetLibrary metadata(PET_INDEX, "/local-display-size", store.path(), valid.path());
        QFile preview(valid.filePath("local-cat/preview.png"));
        QVERIFY(preview.open(QIODevice::WriteOnly)); QVERIFY(preview.resize(4 * 1024 * 1024 + 1)); preview.close();
        metadata.refreshDisplaySnapshot();
        QVERIFY(metadata.info("local-cat").error.contains("oversized"));
        QFile pack(valid.filePath("local-cat/pack.json"));
        QVERIFY(pack.open(QIODevice::WriteOnly)); QVERIFY(pack.resize(64 * 1024 + 1)); pack.close();
        metadata.refreshDisplaySnapshot();
        QVERIFY(metadata.info("local-cat").error.contains("metadata"));
    }
    void pluginsExtendLocalPets() {
        QTemporaryDir installed, store, plugins; copyLocalPet(installed.path());
        const QString fixture = PET_SOURCE "/tests/fixtures/plugins/example";
        QDirIterator it(fixture, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const auto path = it.next(), target = plugins.filePath("example/" + QDir(fixture).relativeFilePath(path));
            QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath())); QVERIFY(QFile::copy(path, target));
        }
        const auto path = plugins.filePath("example/plugin.json");
        auto metadata = QJsonDocument::fromJson(contents(path)).object(); metadata["pet"] = "local-cat";
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(metadata).toJson()); file.close();
        pet::PetLibrary library(PET_INDEX, "/local-plugins", store.path(), installed.path());
        library.setPlugins(plugins.path(), {"example"});
        QString error; QVERIFY2(library.activate("local-cat", &error), qPrintable(error));
        QVERIFY(library.catalog().animations.contains("example.heart"));
        QCOMPARE(library.plugins().first().status, pet::PluginPack::Applied);
    }
    void localPetValidation_data() {
        QTest::addColumn<QString>("fault"); QTest::addColumn<QString>("message");
        QTest::newRow("manifest") << "manifest" << "checksum";
        QTest::newRow("missing-frame") << "missing-frame" << "Missing PNG";
        QTest::newRow("schema") << "schema" << "schema";
        QTest::newRow("minimum") << "minimum" << "requires Agent Pet";
        QTest::newRow("minimum-overflow") << "minimum-overflow" << "minAppVersion";
        QTest::newRow("identity") << "identity" << "agree";
        QTest::newRow("contract") << "contract" << "cue";
        QTest::newRow("path") << "path" << "Invalid frame path";
        QTest::newRow("terms") << "terms" << "terms";
        QTest::newRow("code") << "code" << "Unsupported";
        QTest::newRow("large-file") << "large-file" << "4 MiB";
        QTest::newRow("large-side") << "large-side" << "2048";
        QTest::newRow("large-preview") << "large-preview" << "512";
        QTest::newRow("catalog-limit") << "catalog-limit" << "JSON size";
        QTest::newRow("frame-limit") << "frame-limit" << "10000";
        QTest::newRow("version") << "version" << "version";
        QTest::newRow("unknown-field") << "unknown-field" << "metadata";
    }
    void localPetValidation() {
        QFETCH(QString, fault); QFETCH(QString, message);
        QTemporaryDir installed, store; copyLocalPet(installed.path());
        const QDir folder(installed.filePath("local-cat"));
        auto edit = [&](const QString &name, const std::function<void(QJsonObject &)> &change) {
            auto o = QJsonDocument::fromJson(contents(folder.filePath(name))).object(); change(o);
            QFile f(folder.filePath(name)); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QJsonDocument(o).toJson());
        };
        if (fault == "manifest") edit("manifest.json", [](QJsonObject &o) {
            auto files = o["files"].toArray(); auto frame = files[0].toObject(); frame["sha256"] = QString(64, '0'); files[0] = frame; o["files"] = files;
        });
        if (fault == "missing-frame") QVERIFY(QFile::remove(folder.filePath("idle/_000_100.png")));
        if (fault == "schema") edit("pack.json", [](QJsonObject &o) { o["schema"] = 99; });
        if (fault == "minimum") edit("pack.json", [](QJsonObject &o) { o["minAppVersion"] = "999.0.0"; });
        if (fault == "minimum-overflow") edit("pack.json", [](QJsonObject &o) { o["minAppVersion"] = "99999999999999999999999.0.0"; });
        if (fault == "identity") edit("pet.json", [](QJsonObject &o) { o["name"] = "Different"; });
        if (fault == "contract") edit("animations.json", [](QJsonObject &o) {
            auto cues = o["cues"].toObject(); cues["thinking"] = "absent"; o["cues"] = cues;
        });
        if (fault == "path") edit("animations.json", [](QJsonObject &o) { o = withFrame(o, "assets/local-cat/../outside.png"); });
        if (fault == "terms") QVERIFY(QFile::remove(folder.filePath("licenses/TERMS.txt")));
        if (fault == "code") { QFile f(folder.filePath("run.js")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("code"); }
        if (fault == "large-file") { QFile f(folder.filePath("idle/_000_100.png")); QVERIFY(f.open(QIODevice::ReadWrite)); QVERIFY(f.resize(4 * 1024 * 1024 + 1)); }
        if (fault == "large-side" || fault == "large-preview") {
            QImage image(fault == "large-side" ? 2049 : 513, 1, QImage::Format_ARGB32); image.fill(Qt::red);
            QVERIFY(image.save(folder.filePath(fault == "large-side" ? "idle/extra.png" : "preview.png")));
        }
        if (fault == "catalog-limit") { QFile f(folder.filePath("animations.json")); QVERIFY(f.open(QIODevice::ReadWrite)); QVERIFY(f.resize(8 * 1024 * 1024 + 1)); }
        if (fault == "frame-limit") edit("animations.json", [](QJsonObject &o) {
            auto original = o["sequences"].toArray()[0].toObject(); QJsonArray frames;
            for (int i = 0; i < 1000; ++i) frames.append(original["frames"].toArray()[0]);
            original["frames"] = frames; original["duration_ms"] = 100000; QJsonArray sequences;
            for (int i = 0; i < 11; ++i) { original["path"] = QString("seq%1").arg(i); sequences.append(original); }
            o["sequences"] = sequences;
        });
        if (fault == "version") edit("pack.json", [](QJsonObject &o) { o["version"] = "1.0.0-01"; });
        if (fault == "unknown-field") edit("pack.json", [](QJsonObject &o) { o["trusted"] = true; });
        pet::PetLibrary library(PET_INDEX, "/local-invalid", store.path(), installed.path());
        const auto info = library.info("local-cat");
        QVERIFY2(info.error.contains(message), qPrintable(info.error)); QVERIFY(info.preview.isEmpty());
        const auto pets = library.pets();
        QVERIFY(std::any_of(pets.begin(), pets.end(), [](const pet::PetInfo &p) { return p.id == "local-cat" && !p.error.isEmpty(); }));
        QString error; QVERIFY(!library.activate("local-cat", &error)); QVERIFY(error.contains(message));
        QVERIFY(library.active().isEmpty()); QVERIFY(library.terms("local-cat").isEmpty());
        QVERIFY2(library.activate("vpet", &error), qPrintable(error)); QCOMPARE(library.active(), QString("vpet"));
    }
    void localPetAcceptsFrameLimitAndCountsUnreferencedBytes() {
        QTemporaryDir installed, store; copyLocalPet(installed.path());
        const QDir folder(installed.filePath("local-cat"));
        auto catalog = QJsonDocument::fromJson(contents(folder.filePath("animations.json"))).object();
        auto sequence = catalog["sequences"].toArray()[0].toObject(); const auto original = sequence["frames"].toArray();
        QJsonArray frames, sequences;
        for (int i = 0; i < 1000; ++i) frames.append(original[i % 2]);
        sequence["frames"] = frames; sequence["duration_ms"] = 100000;
        for (int i = 0; i < 10; ++i) {
            sequence["path"] = i == 0 ? QString("idle") : QString("extra%1").arg(i); sequences.append(sequence);
        }
        catalog["sequences"] = sequences;
        QFile file(folder.filePath("animations.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(catalog).toJson()); file.close();
        pet::PetLibrary library(PET_INDEX, "/local-limits", store.path(), installed.path());
        QVERIFY2(library.info("local-cat").error.isEmpty(), qPrintable(library.info("local-cat").error));
        // Sparse padding keeps this test small on disk. Unreferenced PNGs still count toward the budget.
        const auto png = contents(folder.filePath("idle/_000_100.png"));
        for (int i = 0; i < 128; ++i) {
            QFile extra(folder.filePath(QString("idle/extra%1.png").arg(i))); QVERIFY(extra.open(QIODevice::WriteOnly));
            extra.write(png); QVERIFY(extra.resize(4 * 1024 * 1024));
        }
        QVERIFY(library.info("local-cat").error.contains("512 MiB"));
    }
    void localPetGoneAndReservedId() {
        QTemporaryDir installed, store; copyLocalPet(installed.path());
        pet::PetLibrary library(PET_INDEX, "/local-gone", store.path(), installed.path());
        QVERIFY(QDir(installed.filePath("local-cat")).removeRecursively());
        QString error; QVERIFY(!library.activate("local-cat", &error)); QVERIFY(error.contains("Unknown pet"));
        QVERIFY(library.activate("vpet", &error));
        copyLocalPet(installed.path());
        QVERIFY(QDir(installed.path()).rename("local-cat", "vpet"));
        const auto pets = library.pets();
        int rejected = 0;
        for (const auto &pet : pets) if (pet.id == "vpet" && !pet.folder.isEmpty()) { QVERIFY(!pet.error.isEmpty()); ++rejected; }
        QCOMPARE(rejected, 1); QVERIFY(library.info("vpet").folder.isEmpty());
    }
    void localPetLinksAndCaseCollisions() {
        QTemporaryDir installed, store; copyLocalPet(installed.path());
        const auto target = installed.filePath("local-cat/idle/_000_100.png");
        const auto link = installed.filePath("local-cat/idle/link.png");
        if (!QFile::link(target, link) || !QFileInfo(link).isSymLink()) QSKIP("Host does not support file symlinks.");
        pet::PetLibrary library(PET_INDEX, "/local-links", store.path(), installed.path());
        QVERIFY(library.info("local-cat").error.contains("Unsafe"));
        QVERIFY(QFile::remove(link));
        const auto upper = installed.filePath("local-cat/idle/UPPER.png");
        QVERIFY(QFile::copy(target, upper));
        if (!QFile::copy(target, installed.filePath("local-cat/idle/upper.png"))) QSKIP("Case-insensitive filesystem.");
        QVERIFY(library.info("local-cat").error.contains("duplicate"));
    }
    void pickerRejectsInvalidLocalPets() {
        pet::PetInfo valid{"vpet", "VPet", "Artist", {}, {}, {}};
        pet::PetInfo invalid{"bad", "Bad pet", "Other artist", {}, {}, {}};
        invalid.folder = "/packs/bad"; invalid.license = "CC0-1.0"; invalid.error = "Missing PNG frame";
        QWidget page; pet::PetPicker picker({invalid, valid}, "bad", "vpet", &page);
        QCOMPARE(picker.selected(), QString("vpet"));
        QSignalSpy chosen(&picker, &pet::PetPicker::chosen);
        for (auto *tile : picker.findChildren<QToolButton *>()) if (tile->property("pet") == "bad") {
            QVERIFY(!tile->isEnabled()); QVERIFY(tile->text().contains("Local"));
            QVERIFY(tile->toolTip().contains("CC0-1.0")); QVERIFY(tile->toolTip().contains("Missing PNG")); tile->click();
        }
        QCOMPARE(chosen.size(), 0);
        QVERIFY(picker.findChild<QLabel *>()->text().contains("Missing PNG"));
        for (auto *tile : picker.findChildren<QToolButton *>()) if (tile->property("pet") == "bad") QVERIFY(!tile->isEnabled());
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
        QVERIFY(window.findChild<QPushButton *>("openPetsFolder"));
        // One bundled pet leaves nothing to choose; the picker-level cases cover that.
        QCOMPARE(window.findChild<pet::PetPicker *>() != nullptr, pet::PetLibrary::shared().pets().size() > 1);
    }
    void sourceBrowserRemovalDoesNotDependOnOpeningTab_data() {
        QTest::addColumn<int>("firstTab");
        QTest::newRow("pet-first") << 0;
        QTest::newRow("plugin-first") << 1;
    }
    void sourceBrowserRemovalDoesNotDependOnOpeningTab() {
        QFETCH(int, firstTab);
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        pet::PetWindow window(nullptr, path);
        window.setPet("mini"); window.setPlugins({"example", "keep"});
        window.showSettings();
        QList<QPushButton *> browse;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == "Browse pack sources…") browse.append(button);
        QCOMPARE(browse.size(), 2);
        browse[firstTab]->click();
        auto *browser = window.findChild<pet::SourceBrowser *>(); QVERIFY(browser);
        browse[1 - firstTab]->click();
        QCOMPARE(window.findChildren<pet::SourceBrowser *>().size(), 1);
        emit browser->packRemoved("pet", "mini");
        QCOMPARE(window.pet(), QString("mini"));
        QCOMPARE(pet::PreferencesStore(path).load().pet, QString("mini"));
        emit browser->packRemoved("plugin", "example");
        QCOMPARE(window.plugins(), (QStringList{"example", "keep"}));
        QCOMPARE(pet::PreferencesStore(path).load().plugins, (QStringList{"example", "keep"}));
    }
    void sourceBrowserSurvivesSettingsAndIsReused() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json", false);
        auto browse = [&] {
            for (auto *button : window.findChildren<QPushButton *>())
                if (button->text() == "Browse pack sources…") { button->click(); return true; }
            return false;
        };
        window.showSettings(); QVERIFY(browse());
        QPointer<pet::SourceBrowser> browser = window.findChild<pet::SourceBrowser *>();
        QVERIFY(browser); QCOMPARE(browser->parentWidget(), &window);
        QVERIFY(browse()); QCOMPARE(window.findChildren<pet::SourceBrowser *>().size(), 1);
        QPointer<QDialog> settings;
        for (auto *dialog : window.findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly))
            if (dialog != browser.data()) settings = dialog;
        QVERIFY(settings); settings->close(); QTRY_VERIFY(settings.isNull());
        QVERIFY(browser); QVERIFY(browser->isVisible());
        window.showSettings(); QVERIFY(browse());
        QCOMPARE(window.findChildren<pet::SourceBrowser *>().size(), 1);
        browser->reject(); QTRY_VERIFY(browser.isNull());
    }
    void sourceBrowserCanBeDestroyedDuringTermsConsent() {
        QTemporaryDir directory;
        const auto archive = directory.path() + "/cat.agentpet";
        QProcess zip;
        zip.start(QString(PYTHON), {"-c", "import pathlib,sys,zipfile\np=pathlib.Path(sys.argv[1])\nwith zipfile.ZipFile(sys.argv[2],'w') as z:\n for f in sorted(p.rglob('*')):\n  if f.is_file(): z.write(f,f.relative_to(p).as_posix())",
            QString(PET_SOURCE) + "/tests/fixtures/local-pets/local-cat", archive});
        QVERIFY(zip.waitForFinished()); QCOMPARE(zip.exitCode(), 0);
        QPointer<pet::SourceBrowser> browser = new pet::SourceBrowser(nullptr, directory.path() + "/sources");
        const auto downloads = browser->findChildren<pet::packs::Download *>(); QVERIFY(!downloads.isEmpty());
        QTimer destroy; destroy.setSingleShot(true); bool sawTerms = false;
        connect(&destroy, &QTimer::timeout, this, [&] {
            for (auto *dialog : browser->findChildren<QDialog *>())
                sawTerms = sawTerms || dialog->windowTitle() == "Review pack terms";
            delete browser.data();
        });
        destroy.start(0);
        // completed() calls received() synchronously; deletion occurs inside consent.exec().
        emit downloads.first()->completed(archive);
        QVERIFY(sawTerms); QVERIFY(browser.isNull());
    }
    void sourceBrowserShowsCachedPacksWithoutRefreshing() {
        QTemporaryDir directory;
        pet::packs::SourceStore store(directory.path()); QString error;
        const QJsonObject entry{{"kind", "pet"}, {"id", "example-cat"}, {"version", "1.0.0"},
            {"name", "Example cat"}, {"author", "Artist"}, {"license", "CC0-1.0"}, {"schema", 2},
            {"minAppVersion", "0.17.0"}, {"description", "Example"},
            {"download", "https://example.invalid/cat.agentpet"}, {"preview", "https://example.invalid/cat.png"},
            {"bytes", 100}, {"sha256", QString(64, 'a')}};
        QVERIFY(store.cache(pet::packs::officialSource(), QJsonDocument(QJsonObject{
            {"schema_version", 1}, {"packs", QJsonArray{entry}}}).toJson(), error));
        QPointer<pet::SourceBrowser> browser = new pet::SourceBrowser(nullptr, directory.path());
        browser->show();
        auto *list = browser->findChild<QListWidget *>(); QVERIFY(list); QCOMPARE(list->count(), 1);
        QVERIFY(list->item(0)->text().contains("Example cat")); QCOMPARE(list->currentRow(), -1);
        for (auto *button : browser->findChildren<QPushButton *>())
            if (button->text() == "Install…" || button->text() == "Cancel download" || button->text() == "Remove source")
                QVERIFY(!button->isEnabled());
        browser->reject(); QTRY_VERIFY(browser.isNull());
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
        auto *selector = about->findChild<QComboBox *>("petTerms");
        QVERIFY(selector);
        QVERIFY(QMetaObject::invokeMethod(selector, "currentIndexChanged", Q_ARG(int, selector->currentIndex())));
        for (const auto *path : {":/licenses/QT-NOTICE.txt", ":/NOTICE", ":/THIRD_PARTY_NOTICES.md",
                                 ":/LICENSE", ":/licenses/LGPL-3.0.txt", ":/licenses/GPL-3.0.txt"}) {
            QFile document(path); QVERIFY(document.open(QIODevice::ReadOnly));
            const auto text = QString::fromUtf8(document.readAll()).replace("\r\n", "\n").trimmed();
            QVERIFY(!text.isEmpty()); QVERIFY2(terms->toPlainText().contains(text), path);
        }
    }
};
QTEST_MAIN(PetsTests)
#include "pets_tests.moc"
