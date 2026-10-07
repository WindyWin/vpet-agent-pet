#include "animation/catalog.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QResource>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <utility>

namespace {
// A catalog for pet "test" in which every core state plays one idle frame with its contract shape.
QJsonObject contractCatalog() {
    const QJsonObject frame{{"path", "assets/test/idle/_000_100.png"}, {"duration_ms", 100}};
    QJsonObject states, playback;
    for (const auto &core : pet::coreStates()) {
        QJsonArray sequences;
        for (int phase = 0; phase < (core.mode == "phased" ? 3 : 1); ++phase) sequences.append("idle");
        states[core.name] = sequences;
        playback[core.name] = QJsonObject{{"mode", core.mode}, {"after", core.after}};
    }
    return {{"schema_version", 1}, {"asset_root", "assets/test"}, {"states", states}, {"playback", playback},
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
private slots:
    void contractComesFromItsDataFile() {
        const auto &states = pet::coreStates();
        QCOMPARE(states.size(), 15);
        auto shape = [&](const QString &name) {
            for (const auto &state : states) if (state.name == name) return state.mode + "/" + state.after;
            return QString();
        };
        QCOMPARE(shape("idle"), QString("loop/idle"));
        QCOMPARE(shape("starting"), QString("once/idle"));
        QCOMPARE(shape("tool_error"), QString("once/previous"));
        QCOMPARE(shape("closing_angry"), QString("once/stop"));
        QCOMPARE(shape("dragging"), QString("phased/idle"));
        QCOMPARE(shape("out_of_quota"), QString("loop/idle"));
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
        QCOMPARE(partial.contractError(), QString("Missing core state: sleeping")); // ...but not a pet the app can run.
        QCOMPARE(load(withState(catalog, "tool_error", {"idle"}, {{"mode", "once"}, {"after", "idle"}})).contractError(),
                 QString("Core state tool_error must play once, then previous"));
        QVERIFY(load(withState(catalog, "thinking", {"idle"}, {{"mode", "once"}, {"after", "idle"}}))
                    .contractError().contains("thinking"));
        // A phased core state ends when the app moves on, never after a count of loops.
        QVERIFY(load(withState(catalog, "waiting", {"idle", "idle", "idle"}, {{"mode", "phased"}, {"after", "idle"}, {"loops", 2}}))
                    .contractError().contains("waiting"));
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
        for (const auto &[index, pets] : {std::pair{QString(PET_INDEX), QStringList{"vpet"}},
                                          std::pair{QString(FIXTURE_INDEX), QStringList{"broken", "duo", "mini"}}}) {
            QVERIFY2(QResource::registerResource(index, "/tree"), qPrintable(index));
            const auto unregister = qScopeGuard([&] { QResource::unregisterResource(index, "/tree"); });
            QCOMPARE(QDir(":/tree/assets").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name), pets);
            for (const auto &id : pets) {
                const auto mismatch = treeMismatch(":/tree", id, QFileInfo(index).absolutePath());
                QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));
            }
            // Listing pets reads the index only: no frame of any pet is a resource yet.
            QVERIFY(!QFile::exists(":/tree/assets/" + pets.first() + "/" + (pets.first() == "vpet" ? "vup/Default/Nomal/1/_000_250.png" : "idle/_000_100.png")));
        }
        const auto duo = QJsonDocument::fromJson(contents(QFileInfo(FIXTURE_INDEX).absolutePath() + "/pets/duo/packs.json")).object();
        QCOMPARE(duo["packs"].toArray().size(), 2); // One pack per frame folder.
    }
};
QTEST_MAIN(PetsTests)
#include "pets_tests.moc"
