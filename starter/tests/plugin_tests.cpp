#include "animation/catalog.h"
#include "animation/pet_library.h"
#include "animation/player.h"
#include "animation/plugins.h"
#include "desktop/pet_window.h"
#include "desktop/plugin_list.h"
#include "settings/preferences.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

namespace {
void writeJson(const QString &path, const QJsonObject &object) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(object).toJson());
}
QJsonObject readJson(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}
// The catalog of pet "test": every state cue's default state on one idle frame, plus a reaction (`cheer`, the whole
// celebrate pool), a fidget and happy mood art for idle, so packs have something to join and to replace.
QJsonObject baseCatalog() {
    const QJsonObject frame{{"path", "assets/test/idle/_000_100.png"}, {"duration_ms", 100}};
    QJsonObject states, playback;
    for (const auto &cue : pet::cues()) {
        if (cue.reaction) continue;
        QJsonArray sequences;
        for (int phase = 0; phase < (cue.mode == "phased" ? 3 : 1); ++phase) sequences.append("idle");
        states[cue.state] = sequences;
        playback[cue.state] = QJsonObject{{"mode", cue.mode}, {"after", cue.after}};
    }
    for (const auto *state : {"cheer", "fidget"}) {
        states[state] = QJsonArray{"idle"};
        playback[state] = QJsonObject{{"mode", "once"}, {"after", "idle"}};
    }
    const QJsonObject choice{{"weight", 1}, {"sequences", QJsonArray{"idle"}}};
    return {{"schema_version", pet::catalogSchema}, {"asset_root", "assets/test"}, {"states", states}, {"playback", playback},
            {"sequences", QJsonArray{QJsonObject{{"path", "idle"}, {"duration_ms", 100}, {"frames", QJsonArray{frame}}}}},
            {"cues", QJsonObject{{"celebrate", QJsonArray{QJsonObject{{"state", "cheer"}, {"weight", 1}}}}}},
            {"ambient", QJsonObject{{"fidgets", QJsonArray{QJsonObject{{"state", "fidget"}, {"weight", 1}}}}}},
            {"moods", QJsonObject{{"happy", QJsonObject{{"idle", QJsonArray{choice}}}}}}};
}
// A sequence of a pack: `frames` frames of 100 ms at frames/<name>/<n>.png.
QJsonObject sequence(const QString &name, int frames = 1) {
    QJsonArray list;
    for (int n = 0; n < frames; ++n) list.append(QJsonObject{{"path", QString("frames/%1/%2.png").arg(name).arg(n)}, {"duration_ms", 100}});
    return {{"path", name}, {"duration_ms", 100 * frames}, {"frames", list}};
}
QJsonObject once(const QString &after = "idle") { return {{"mode", "once"}, {"after", after}}; }
// A fragment with one new one-shot state "<id>.dance" on sequence "dance".
QJsonObject danceFragment(const QString &id) {
    return {{"schema_version", pet::catalogSchema}, {"sequences", QJsonArray{sequence("dance", 2)}},
            {"states", QJsonObject{{id + ".dance", QJsonArray{"dance"}}}}, {"playback", QJsonObject{{id + ".dance", once()}}}};
}
// Writes pack `id` into `folder`: plugin.json (fields in `info` replace the defaults; a null value removes one), the
// fragment, and a 16x16 PNG for every frame the fragment names.
void writePack(const QString &folder, const QString &id, const QJsonObject &fragment, const QJsonObject &info = {}) {
    QJsonObject plugin{{"schema_version", 1}, {"id", id}, {"name", "Pack " + id}, {"version", "1.0.0"},
                       {"author", "Tests"}, {"license", "CC0-1.0"}, {"pet", "test"}};
    for (auto it = info.begin(); it != info.end(); ++it) {
        if (it.value().isNull()) plugin.remove(it.key());
        else plugin[it.key()] = it.value();
    }
    const QDir pack(folder + "/" + id);
    writeJson(pack.filePath("plugin.json"), plugin);
    writeJson(pack.filePath("animations.json"), fragment);
    for (const auto &entry : fragment["sequences"].toArray())
        for (const auto &frame : entry.toObject()["frames"].toArray()) {
            const auto path = pack.filePath(frame.toObject()["path"].toString());
            QDir().mkpath(QFileInfo(path).absolutePath());
            QImage image(16, 16, QImage::Format_ARGB32);
            image.fill(Qt::red);
            image.save(path, "PNG");
        }
}
const pet::PluginPack *find(const QVector<pet::PluginPack> &packs, const QString &id) {
    const auto found = std::find_if(packs.begin(), packs.end(), [&](const pet::PluginPack &pack) { return pack.id == id; });
    return found == packs.end() ? nullptr : &*found;
}
}

class PluginTests : public QObject {
    Q_OBJECT
    QTemporaryDir base_;
    // Applies the packs in `folder` that `enabled` names to a fresh copy of the test pet's catalog.
    pet::Catalog apply(const QString &folder, const QStringList &enabled, QVector<pet::PluginPack> &packs, const QString &pet = "test") {
        QString error;
        auto source = pet::Catalog::read(base_.path(), "test", &error);
        if (!source.valid()) qWarning() << error;
        packs = pet::plugins::scan(folder, "1.0.0");
        pet::plugins::apply(source, pet, enabled, packs);
        return pet::Catalog::build(source, &error);
    }
    QString status(const QVector<pet::PluginPack> &packs, const QString &id) {
        const auto *pack = find(packs, id);
        if (!pack) return "missing";
        static const QStringList names{"invalid", "off", "other pet", "applied", "rejected"};
        return pack->error.isEmpty() ? names[pack->status] : names[pack->status] + ": " + pack->error;
    }
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true); // Preferences, pets and plugins stay out of the real data folder.
        writeJson(base_.path() + "/assets/test/animations.json", baseCatalog());
        QString error;
        const auto catalog = pet::Catalog::load(base_.path(), "test", &error);
        QVERIFY2(catalog.valid(), qPrintable(error));
        QCOMPARE(catalog.contractError(), QString());
    }
    void scanReadsPluginJson() {
        QTemporaryDir folder;
        writePack(folder.path(), "good", danceFragment("good"), {{"url", "https://example.com/good"}, {"min_app_version", "1.0"}});
        writePack(folder.path(), "wrong-id", danceFragment("wrong-id"), {{"id", "other"}});
        writePack(folder.path(), "no-license", danceFragment("no-license"), {{"license", QJsonValue::Null}});
        writePack(folder.path(), "unknown-key", danceFragment("unknown-key"), {{"scripts", "run.sh"}});
        writePack(folder.path(), "too-new", danceFragment("too-new"), {{"min_app_version", "1.0.1"}});
        writePack(folder.path(), "plain-url", danceFragment("plain-url"), {{"url", "http://example.com"}});
        writePack(folder.path(), "no-pet", danceFragment("no-pet"), {{"pet", "Not a pet"}});
        QDir().mkpath(folder.path() + "/Bad Name"); // Not an id: ignored.
        QDir().mkpath(folder.path() + "/empty");    // No plugin.json.
        const auto packs = pet::plugins::scan(folder.path(), "1.0.0");
        QStringList ids;
        for (const auto &pack : packs) ids.append(pack.id);
        QCOMPARE(ids, (QStringList{"empty", "good", "no-license", "no-pet", "plain-url", "too-new", "unknown-key", "wrong-id"}));
        const auto *good = find(packs, "good");
        QCOMPARE(good->status, pet::PluginPack::Off); QCOMPARE(good->error, QString());
        QCOMPARE(good->name, QString("Pack good")); QCOMPARE(good->version, QString("1.0.0"));
        QCOMPARE(good->author, QString("Tests")); QCOMPARE(good->license, QString("CC0-1.0"));
        QCOMPARE(good->pet, QString("test")); QCOMPARE(good->url, QString("https://example.com/good"));
        QCOMPARE(good->folder, folder.path() + "/good");
        QCOMPARE(status(packs, "empty"), QString("invalid: plugin.json is missing or too large."));
        QCOMPARE(status(packs, "wrong-id"), QString("invalid: plugin.json id must match its folder name, wrong-id."));
        QCOMPARE(status(packs, "no-license"), QString("invalid: plugin.json needs a license of up to 64 characters."));
        QCOMPARE(status(packs, "unknown-key"), QString("invalid: Unknown key in plugin.json: scripts"));
        QCOMPARE(status(packs, "too-new"), QString("invalid: Needs Agent Pet 1.0.1 or later."));
        QCOMPARE(status(packs, "plain-url"), QString("invalid: plugin.json url must start with https://."));
        QCOMPARE(status(packs, "no-pet"), QString("invalid: plugin.json needs the id of the pet it is for, such as vpet."));
        QCOMPARE(find(packs, "no-license")->name, QString("Pack no-license")); // Named as far as it reads.
        QVERIFY(pet::plugins::scan(folder.path() + "/nowhere", "1.0.0").isEmpty());
    }
    void packAddsStatesReactionsFidgetsAndVariants() {
        QTemporaryDir folder;
        auto fragment = danceFragment("party");
        auto sequences = fragment["sequences"].toArray(); sequences.append(sequence("sway")); fragment["sequences"] = sequences;
        fragment["cues"] = QJsonObject{{"celebrate", QJsonArray{QJsonObject{{"state", "party.dance"}, {"weight", 3}}}},
                                       {"snack", QJsonArray{QJsonObject{{"state", "cheer"}, {"weight", 1}}}}};
        fragment["ambient"] = QJsonObject{{"fidgets", QJsonArray{QJsonObject{{"state", "party.dance"}, {"weight", 2}}}}};
        fragment["variants"] = QJsonObject{{"idle", QJsonArray{QJsonObject{{"weight", 1}, {"sequences", QJsonArray{"sway"}}}}}};
        fragment["moods"] = QJsonObject{{"poor", QJsonObject{{"idle", QJsonArray{QJsonObject{{"weight", 1}, {"sequences", QJsonArray{"sway"}}}}}}}};
        writePack(folder.path(), "party", fragment);
        QVector<pet::PluginPack> packs;
        const auto catalog = apply(folder.path(), {"party"}, packs);
        QCOMPARE(status(packs, "party"), QString("applied"));
        QVERIFY(catalog.valid()); QCOMPARE(catalog.contractError(), QString());
        QCOMPARE(catalog.animations.value("party.dance").mode, QString("once"));
        QCOMPARE(catalog.animations.value("party.dance").choices.first().sequences, QStringList{"party:dance"});
        // Frames load from the pack folder, as files.
        const auto frames = catalog.sequences.value("party:dance");
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames.first().path, QFileInfo(folder.path() + "/party/frames/dance/0.png").canonicalFilePath());
        // The pack joins the pet's pool and adds one the pet lacked.
        const auto celebrate = catalog.pools.value("celebrate");
        QCOMPARE(celebrate.size(), 2);
        QCOMPARE(celebrate[0].state, QString("cheer")); QCOMPARE(celebrate[1].state, QString("party.dance"));
        QCOMPARE(celebrate[1].weight, 3);
        QCOMPARE(catalog.pools.value("snack").size(), 1);
        QVERIFY(catalog.fidgetStates.contains("fidget")); QVERIFY(catalog.fidgetStates.contains("party.dance"));
        QCOMPARE(catalog.animations.value("idle").choices.size(), 2); // Its own entry and the pack's variant.
        QCOMPARE(catalog.animations.value("idle").choices[1].sequences, QStringList{"party:sway"});
        QCOMPARE(catalog.moods.value("poor").value("idle").first().sequences, QStringList{"party:sway"});
        QCOMPARE(catalog.moods.value("happy").value("idle").first().sequences, QStringList{"idle"}); // Untouched.
    }
    void newNamesBelongToThePack() {
        QTemporaryDir folder;
        auto bare = danceFragment("party");
        bare["states"] = QJsonObject{{"dance", QJsonArray{"dance"}}}; bare["playback"] = QJsonObject{{"dance", once()}};
        writePack(folder.path(), "bare", bare);
        auto foreign = danceFragment("other");
        writePack(folder.path(), "foreign", foreign);
        auto odd = danceFragment("odd");
        odd["states"] = QJsonObject{{"odd.Big Dance", QJsonArray{"dance"}}}; odd["playback"] = QJsonObject{{"odd.Big Dance", once()}};
        writePack(folder.path(), "odd", odd);
        // Packs use their own sequences only, by their own names.
        auto borrowed = danceFragment("borrowed");
        borrowed["states"] = QJsonObject{{"borrowed.dance", QJsonArray{"idle"}}};
        writePack(folder.path(), "borrowed", borrowed);
        QVector<pet::PluginPack> packs;
        const auto catalog = apply(folder.path(), {"bare", "foreign", "odd", "borrowed"}, packs);
        QCOMPARE(status(packs, "bare"), QString("rejected: New states must be named bare.<name>: dance"));
        QCOMPARE(status(packs, "foreign"), QString("rejected: New states must be named foreign.<name>: other.dance"));
        QCOMPARE(status(packs, "odd"), QString("rejected: Invalid state name: odd.Big Dance"));
        QCOMPARE(status(packs, "borrowed"), QString("rejected: Unknown sequence for borrowed.dance"));
        QVERIFY(catalog.valid());
        QCOMPARE(catalog.sequences.keys(), QStringList{"idle"}); // Nothing of a rejected pack stays.
    }
    void replacingNeedsAnOverride() {
        QTemporaryDir folder;
        auto fragment = danceFragment("calm");
        auto sequences = fragment["sequences"].toArray(); sequences.append(sequence("think")); fragment["sequences"] = sequences;
        auto states = fragment["states"].toObject(); states["thinking"] = QJsonArray{"think", "think", "think"}; fragment["states"] = states;
        auto playback = fragment["playback"].toObject(); playback["thinking"] = QJsonObject{{"mode", "phased"}, {"after", "idle"}};
        fragment["playback"] = playback;
        writePack(folder.path(), "calm", fragment);
        QVector<pet::PluginPack> packs;
        apply(folder.path(), {"calm"}, packs);
        QCOMPARE(status(packs, "calm"), QString("rejected: Replacing states.thinking needs \"states.thinking\" in overrides."));
        fragment["overrides"] = QJsonArray{"states.thinking"};
        writePack(folder.path(), "calm", fragment);
        auto catalog = apply(folder.path(), {"calm"}, packs);
        QCOMPARE(status(packs, "calm"), QString("applied"));
        QCOMPARE(catalog.animations.value("thinking").choices.size(), 1);
        QCOMPARE(catalog.animations.value("thinking").choices.first().sequences, (QStringList{"calm:think", "calm:think", "calm:think"}));
        // A reaction pool is joined freely and replaced only as an override; so is a state cue's state.
        fragment["cues"] = QJsonObject{{"celebrate", QJsonArray{QJsonObject{{"state", "calm.dance"}, {"weight", 1}}}}};
        fragment["overrides"] = QJsonArray{"states.thinking", "cues.celebrate"};
        writePack(folder.path(), "calm", fragment);
        catalog = apply(folder.path(), {"calm"}, packs);
        QCOMPARE(status(packs, "calm"), QString("applied"));
        QCOMPARE(catalog.pools.value("celebrate").size(), 1);
        QCOMPARE(catalog.pools.value("celebrate").first().state, QString("calm.dance"));
        fragment["cues"] = QJsonObject{{"turn-finished", "calm.dance"}};
        fragment["overrides"] = QJsonArray{"states.thinking"};
        writePack(folder.path(), "calm", fragment);
        apply(folder.path(), {"calm"}, packs);
        QCOMPARE(status(packs, "calm"), QString("rejected: Replacing cues.turn-finished needs \"cues.turn-finished\" in overrides."));
        fragment["overrides"] = QJsonArray{"states.thinking", "cues.turn-finished"};
        writePack(folder.path(), "calm", fragment);
        catalog = apply(folder.path(), {"calm"}, packs);
        QCOMPARE(status(packs, "calm"), QString("applied"));
        QCOMPARE(catalog.stateFor("turn-finished"), QString("calm.dance"));
        // The pet's own mood art is replaced only as an override.
        fragment["moods"] = QJsonObject{{"happy", QJsonObject{{"idle", QJsonArray{QJsonObject{{"weight", 1}, {"sequences", QJsonArray{"dance"}}}}}}}};
        writePack(folder.path(), "calm", fragment);
        apply(folder.path(), {"calm"}, packs);
        QCOMPARE(status(packs, "calm"), QString("rejected: Replacing moods.happy.idle needs \"moods.happy.idle\" in overrides."));
        // An override that replaces nothing is refused as a likely typo.
        fragment.remove("moods");
        fragment["overrides"] = QJsonArray{"states.thinking", "cues.turn-finished", "states.thinkin"};
        writePack(folder.path(), "calm", fragment);
        apply(folder.path(), {"calm"}, packs);
        QCOMPARE(status(packs, "calm"), QString("rejected: Override states.thinkin replaces nothing in this pack."));
    }
    void replacedStatesDropTheirVariantsAndMoodArt() {
        QTemporaryDir folder;
        auto fragment = danceFragment("still");
        fragment["states"] = QJsonObject{{"idle", QJsonArray{"dance"}}};
        fragment["playback"] = QJsonObject{{"idle", QJsonObject{{"mode", "loop"}, {"after", "idle"}}}};
        fragment["overrides"] = QJsonArray{"states.idle"};
        writePack(folder.path(), "still", fragment);
        QVector<pet::PluginPack> packs;
        const auto catalog = apply(folder.path(), {"still"}, packs);
        QCOMPARE(status(packs, "still"), QString("applied"));
        QCOMPARE(catalog.animations.value("idle").choices.first().sequences, QStringList{"still:dance"});
        QVERIFY(!catalog.moods.value("happy").contains("idle"));
    }
    void packsCannotReplaceTheSameThing() {
        QTemporaryDir folder;
        for (const auto *id : {"first", "second"}) {
            auto fragment = danceFragment(id);
            fragment["cues"] = QJsonObject{{"celebrate", QJsonArray{QJsonObject{{"state", QString(id) + ".dance"}, {"weight", 1}}}}};
            fragment["overrides"] = QJsonArray{"cues.celebrate"};
            writePack(folder.path(), id, fragment);
        }
        // A third pack only joins the pool, which needs no claim.
        auto joining = danceFragment("third");
        joining["cues"] = QJsonObject{{"celebrate", QJsonArray{QJsonObject{{"state", "third.dance"}, {"weight", 1}}}}};
        writePack(folder.path(), "third", joining);
        QVector<pet::PluginPack> packs;
        const auto catalog = apply(folder.path(), {"first", "second", "third"}, packs);
        QCOMPARE(status(packs, "first"), QString("applied"));
        QCOMPARE(status(packs, "second"), QString("rejected: cues.celebrate is already replaced by plugin first."));
        QCOMPARE(status(packs, "third"), QString("applied"));
        QCOMPARE(catalog.pools.value("celebrate").size(), 2);
        QCOMPARE(catalog.pools.value("celebrate")[0].state, QString("first.dance"));
        QCOMPARE(catalog.pools.value("celebrate")[1].state, QString("third.dance"));
        QVERIFY(!catalog.animations.contains("second.dance"));
    }
    void invalidPacksAreSkippedAndTheRestLoad() {
        QTemporaryDir folder, outside;
        writePack(folder.path(), "good", danceFragment("good"));
        writePack(folder.path(), "missing", danceFragment("missing"));
        QVERIFY(QFile::remove(folder.path() + "/missing/frames/dance/1.png"));
        auto escape = danceFragment("escape");
        auto sequences = escape["sequences"].toArray();
        auto entry = sequences[0].toObject();
        entry["frames"] = QJsonArray{QJsonObject{{"path", "../good/frames/dance/0.png"}, {"duration_ms", 100}}};
        entry["duration_ms"] = 100;
        sequences[0] = entry; escape["sequences"] = sequences;
        writePack(folder.path(), "escape", escape);
        writePack(folder.path(), "linked", danceFragment("linked"));
        QImage(8, 8, QImage::Format_ARGB32).save(outside.path() + "/elsewhere.png", "PNG");
        QFile::remove(folder.path() + "/linked/frames/dance/0.png");
        const bool linked = QFile::link(outside.path() + "/elsewhere.png", folder.path() + "/linked/frames/dance/0.png");
        writePack(folder.path(), "not-png", danceFragment("not-png"));
        {
            QFile fake(folder.path() + "/not-png/frames/dance/0.png");
            QVERIFY(fake.open(QIODevice::WriteOnly)); fake.write("GIF89a, not a PNG at all");
        }
        writePack(folder.path(), "bitmap", danceFragment("bitmap")); // An image Qt reads, but not a PNG.
        QVERIFY(QImage(16, 16, QImage::Format_RGB32).save(folder.path() + "/bitmap/frames/dance/0.png", "BMP"));
        writePack(folder.path(), "huge", danceFragment("huge"));
        QImage(pet::plugins::maxFrameSide + 1, 1, QImage::Format_ARGB32).save(folder.path() + "/huge/frames/dance/0.png", "PNG");
        auto timing = danceFragment("timing");
        sequences = timing["sequences"].toArray(); entry = sequences[0].toObject(); entry["duration_ms"] = 999;
        sequences[0] = entry; timing["sequences"] = sequences;
        writePack(folder.path(), "timing", timing);
        auto touch = danceFragment("touch");
        touch["touch"] = QJsonObject{{"scale", 100}};
        writePack(folder.path(), "touch", touch);
        auto contract = danceFragment("contract");
        contract["states"] = QJsonObject{{"thinking", QJsonArray{"dance"}}};
        contract["playback"] = QJsonObject{{"thinking", once()}};
        contract["overrides"] = QJsonArray{"states.thinking"};
        writePack(folder.path(), "contract", contract);
        auto fidget = danceFragment("fidget");
        fidget["ambient"] = QJsonObject{{"sleep_after_s", 60}};
        writePack(folder.path(), "fidget", fidget);
        auto reaction = danceFragment("reaction");
        reaction["playback"] = QJsonObject{{"reaction.dance", QJsonObject{{"mode", "once"}, {"after", "previous"}}}};
        reaction["cues"] = QJsonObject{{"celebrate", QJsonArray{QJsonObject{{"state", "reaction.dance"}, {"weight", 1}}}}};
        writePack(folder.path(), "reaction", reaction);
        QStringList enabled;
        for (const auto &pack : pet::plugins::scan(folder.path(), "1.0.0")) enabled.append(pack.id);
        QVector<pet::PluginPack> packs;
        const auto catalog = apply(folder.path(), enabled, packs);
        QCOMPARE(status(packs, "good"), QString("applied"));
        QCOMPARE(status(packs, "missing"), QString("rejected: Missing frame frames/dance/1.png"));
        QCOMPARE(status(packs, "escape"), QString("rejected: Invalid frame path or duration in dance"));
        if (linked) QCOMPARE(status(packs, "linked"), QString("rejected: Missing frame frames/dance/0.png"));
        QCOMPARE(status(packs, "not-png"), QString("rejected: Not a PNG frame of at most 2048 pixels a side: frames/dance/0.png"));
        QCOMPARE(status(packs, "bitmap"), QString("rejected: Not a PNG frame of at most 2048 pixels a side: frames/dance/0.png"));
        QCOMPARE(status(packs, "huge"), QString("rejected: Not a PNG frame of at most 2048 pixels a side: frames/dance/0.png"));
        QCOMPARE(status(packs, "timing"), QString("rejected: Invalid sequence timing: dance"));
        QCOMPARE(status(packs, "touch"), QString("rejected: Plugins cannot set touch in animations.json."));
        QCOMPARE(status(packs, "contract"), QString("rejected: Cue thinking must play phased, then idle; state thinking does not"));
        QCOMPARE(status(packs, "fidget"), QString("rejected: Plugins can only add ambient fidgets."));
        QCOMPARE(status(packs, "reaction"), QString("rejected: Invalid reaction for cue celebrate: reaction.dance"));
        QVERIFY(catalog.valid()); QCOMPARE(catalog.contractError(), QString());
        QStringList states = catalog.animations.keys();
        states.erase(std::remove_if(states.begin(), states.end(), [](const QString &state) { return !state.contains('.'); }), states.end());
        QCOMPARE(states, QStringList{"good.dance"});
        QCOMPARE(catalog.pools.value("celebrate").size(), 1);
    }
    void packsLoadOnlyWhenEnabledForTheirPet() {
        QTemporaryDir folder;
        writePack(folder.path(), "chosen", danceFragment("chosen"));
        writePack(folder.path(), "unchosen", danceFragment("unchosen"));
        writePack(folder.path(), "elsewhere", danceFragment("elsewhere"), {{"pet", "vpet"}});
        writePack(folder.path(), "broken", danceFragment("broken"), {{"version", ""}});
        QVector<pet::PluginPack> packs;
        const auto catalog = apply(folder.path(), {"chosen", "elsewhere", "broken", "uninstalled"}, packs);
        QCOMPARE(status(packs, "chosen"), QString("applied"));
        QCOMPARE(status(packs, "unchosen"), QString("off"));
        QCOMPARE(status(packs, "elsewhere"), QString("other pet"));
        QCOMPARE(status(packs, "broken"), QString("invalid: plugin.json needs a version such as 1.0.0."));
        QVERIFY(catalog.animations.contains("chosen.dance"));
        QVERIFY(!catalog.animations.contains("unchosen.dance")); QVERIFY(!catalog.animations.contains("elsewhere.dance"));
    }
    void exampleFitsVpet() {
        // The pack docs/plugins.md describes, against the real VPet catalog.
        QString error;
        auto source = pet::Catalog::read(PET_SOURCE, "vpet", &error);
        QVERIFY2(source.valid(), qPrintable(error));
        auto packs = pet::plugins::scan(PET_SOURCE "/tests/fixtures/plugins", "1.0.0");
        pet::plugins::apply(source, "vpet", {"example"}, packs);
        QCOMPARE(status(packs, "example"), QString("applied"));
        const auto catalog = pet::Catalog::build(source, &error);
        QVERIFY2(catalog.valid(), qPrintable(error));
        QCOMPARE(catalog.contractError(), QString());
        QVERIFY(catalog.fidgetStates.contains("example.heart"));
        const auto celebrate = catalog.pools.value("celebrate");
        QVERIFY(std::any_of(celebrate.begin(), celebrate.end(), [](const pet::Reaction &reaction) { return reaction.state == "example.heart"; }));
        // A player draws the pack's frames from their files.
        pet::Player player(nullptr, catalog);
        QVERIFY(player.select("example.heart", true));
        QCOMPARE(player.state(), QString("example.heart"));
        QVERIFY(!player.pixmap().isNull());
        QCOMPARE(player.error(), QString());
    }
    void libraryMergesEnabledPacksWhenItActivates() {
        QTemporaryDir folder;
        auto fragment = danceFragment("wave");
        fragment["cues"] = QJsonObject{{"celebrate", QJsonArray{QJsonObject{{"state", "wave.dance"}, {"weight", 1}}}}};
        writePack(folder.path(), "wave", fragment, {{"pet", "mini"}});
        writePack(folder.path(), "bad", danceFragment("missing-prefix"), {{"pet", "mini"}});
        {
            pet::PetLibrary library(FIXTURE_INDEX, "/plugins-on");
            library.setPlugins(folder.path(), {"wave", "bad"});
            QCOMPARE(library.pluginFolder(), folder.path());
            QString error;
            QVERIFY2(library.activate("mini", &error), qPrintable(error)); // A bad pack never fails the pet.
            QVERIFY(library.catalog().animations.contains("wave.dance"));
            QCOMPARE(library.catalog().pools.value("celebrate").size(), 1);
            writePack(folder.path(), "later", danceFragment("later"), {{"pet", "mini"}});
            const auto packs = library.plugins();
            QCOMPARE(packs.size(), 3);
            QCOMPARE(status(packs, "wave"), QString("applied"));
            QVERIFY(status(packs, "bad").startsWith("rejected: New states must be named bad.<name>"));
            QCOMPARE(status(packs, "later"), QString("off")); // Installed after start: loads on the next one.
        }
        // Without setPlugins() nothing loads.
        pet::PetLibrary plain(FIXTURE_INDEX, "/plugins-off");
        QString error;
        QVERIFY2(plain.activate("mini", &error), qPrintable(error));
        QVERIFY(!plain.catalog().animations.contains("wave.dance"));
        QCOMPARE(plain.pluginFolder(), pet::plugins::defaultFolder());
    }
    void preferencesKeepEnabledPacks() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/preferences.json";
        pet::PreferencesStore store(path);
        pet::Preferences preferences;
        QVERIFY(preferences.plugins.isEmpty());
        preferences.plugins = {"party", "Bad Id", "party", "calm"};
        QVERIFY(store.save(preferences));
        QCOMPARE(readJson(path)["plugins"].toArray(), (QJsonArray{"party", "calm"}));
        QCOMPARE(pet::PreferencesStore(path).load().plugins, (QStringList{"party", "calm"}));
        // Only a value of the wrong type invalidates the file; ids that are not valid are dropped.
        auto object = readJson(path);
        QJsonArray many{"x y", 7};
        for (int n = 0; n < 40; ++n) many.append(QString("p%1").arg(n));
        object["plugins"] = many;
        writeJson(path, object);
        pet::PreferencesStore crowded(path);
        const auto loaded = crowded.load();
        QCOMPARE(crowded.error(), QString());
        QCOMPARE(loaded.plugins.size(), pet::Preferences::maxPlugins);
        QCOMPARE(loaded.plugins.first(), QString("p0"));
        object["plugins"] = "party";
        writeJson(path, object);
        pet::PreferencesStore wrong(path);
        QVERIFY(wrong.load().plugins.isEmpty());
        QVERIFY(!wrong.error().isEmpty());
        // Files from before plugins have none.
        object.remove("plugins");
        writeJson(path, object);
        QVERIFY(pet::PreferencesStore(path).load().plugins.isEmpty());
    }
    void listShowsPacksAndTheirFate() {
        pet::PluginPack loaded{"loaded", "Loaded pack", "1.0", "Ann", "CC-BY-4.0", {}, "vpet", {}, "/p/loaded",
                               pet::PluginPack::Applied, {}};
        pet::PluginPack rejected{"rejected", "Rejected pack", "2.0", "Bo", "MIT", {}, "vpet", {}, "/p/rejected",
                                 pet::PluginPack::Rejected, "Missing frame frames/a.png"};
        pet::PluginPack fresh{"fresh", "Fresh pack", "0.1", "Cy", "CC0-1.0", "https://example.com", "vpet", {}, "/p/fresh",
                              pet::PluginPack::Off, {}};
        pet::PluginPack other{"other", "Other pack", "1.0", "Di", "CC0-1.0", {}, "mini", {}, "/p/other", pet::PluginPack::Off, {}};
        pet::PluginPack invalid{"invalid", "invalid", {}, {}, {}, {}, {}, {}, "/p/invalid", pet::PluginPack::Invalid,
                                "plugin.json is missing or too large."};
        pet::PluginList list({loaded, rejected, fresh, other, invalid}, {"loaded", "rejected", "invalid"}, "vpet", "/p");
        auto *view = list.findChild<QListWidget *>();
        QVERIFY(view); QCOMPARE(view->count(), 5);
        auto line = [view](int row) { return view->item(row)->text(); };
        QCOMPARE(line(0), QString("Loaded pack 1.0\nby Ann · license: CC-BY-4.0\nLoaded"));
        QCOMPARE(line(1), QString("Rejected pack 2.0\nby Bo · license: MIT\nNot loaded: Missing frame frames/a.png"));
        QCOMPARE(line(2), QString("Fresh pack 0.1\nby Cy · license: CC0-1.0\nOff"));
        QCOMPARE(line(4), QString("invalid\nCannot be used: plugin.json is missing or too large."));
        QVERIFY(!(view->item(4)->flags() & Qt::ItemIsUserCheckable));
        QCOMPARE(view->item(4)->checkState(), Qt::Unchecked);
        QVERIFY(view->item(2)->toolTip().contains("https://example.com"));
        QSignalSpy changed(&list, &pet::PluginList::changed);
        view->item(0)->setCheckState(Qt::Unchecked);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.last().first().toStringList(), (QStringList{"rejected", "invalid"})); // An unusable pack's choice stays.
        QCOMPARE(line(0), QString("Loaded pack 1.0\nby Ann · license: CC-BY-4.0\nUnloads the next time Agent Pet starts"));
        view->item(2)->setCheckState(Qt::Checked);
        QCOMPARE(line(2), QString("Fresh pack 0.1\nby Cy · license: CC0-1.0\nLoads the next time Agent Pet starts"));
        view->item(3)->setCheckState(Qt::Checked);
        QCOMPARE(line(3), QString("Other pack 1.0\nby Di · license: CC0-1.0\nFor the pet mini: loads when it runs"));
        QCOMPARE(list.enabled(), (QStringList{"rejected", "invalid", "fresh", "other"}));
        QCOMPARE(changed.size(), 3);
        pet::PluginList none({}, {}, "vpet", "/p");
        QVERIFY(!none.findChild<QListWidget *>()->isVisibleTo(&none));
    }
    void windowSavesEnabledPacks() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path);
            QVERIFY(window.plugins().isEmpty());
            window.setPlugins({"party", "Not valid", "party"});
            QCOMPARE(window.plugins(), QStringList{"party"});
            window.setOnTop(false); // Any later save keeps the choice.
            window.showSettings();
            auto *tabs = window.findChild<QTabWidget *>();
            QVERIFY(tabs);
            QStringList titles;
            for (int tab = 0; tab < tabs->count(); ++tab) titles.append(tabs->tabText(tab));
            QVERIFY2(titles.contains("Plugins"), qPrintable(titles.join(", ")));
            QVERIFY(window.findChild<pet::PluginList *>());
        }
        QCOMPARE(pet::PreferencesStore(path).load().plugins, QStringList{"party"});
        pet::PetWindow again(nullptr, path);
        QCOMPARE(again.plugins(), QStringList{"party"});
    }
};
QTEST_MAIN(PluginTests)
#include "plugin_tests.moc"
