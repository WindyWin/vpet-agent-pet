#include "plugins.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QVersionNumber>

namespace pet::plugins {
namespace {
QJsonObject readObject(const QString &path, qint64 limit, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
        *error = QFileInfo(path).fileName() + " is missing or too large.";
        return {};
    }
    QJsonParseError parse{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) *error = "Invalid " + QFileInfo(path).fileName() + " format.";
    return document.object();
}
PluginPack readInfo(const QString &folder, const QString &id, const QString &appVersion) {
    PluginPack pack;
    pack.id = pack.name = id;
    pack.folder = folder;
    auto invalid = [&pack](const QString &reason) {
        pack.status = PluginPack::Invalid;
        pack.error = reason;
        return pack;
    };
    QString error;
    const auto object = readObject(folder + "/plugin.json", 64 * 1024, &error);
    if (!error.isEmpty()) return invalid(error);
    static const QSet<QString> keys{"schema_version", "id", "name", "version", "author", "license", "pet", "url", "min_app_version"};
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!keys.contains(it.key())) return invalid("Unknown key in plugin.json: " + it.key());
    auto text = [&object](const char *key, int limit) {
        const auto value = object.value(key).toString();
        return value.size() <= limit ? value : QString();
    };
    // Shown as far as it reads, so Settings can name the pack whatever is wrong with it.
    if (const auto name = text("name", 64); !name.isEmpty()) pack.name = name;
    pack.version = text("version", 32); pack.author = text("author", 128); pack.license = text("license", 64);
    pack.pet = text("pet", 32); pack.url = text("url", 256); pack.minApp = text("min_app_version", 32);
    static const QRegularExpression version(QRegularExpression::anchoredPattern("[0-9A-Za-z.+-]{1,32}"));
    if (object.value("schema_version").toInt() != 1) return invalid("plugin.json must have schema_version 1.");
    if (object.value("id").toString() != id) return invalid("plugin.json id must match its folder name, " + id + ".");
    if (object.value("name").toString().isEmpty() || pack.name != object.value("name").toString())
        return invalid("plugin.json needs a name of up to 64 characters.");
    if (!version.match(pack.version).hasMatch()) return invalid("plugin.json needs a version such as 1.0.0.");
    if (pack.author.isEmpty()) return invalid("plugin.json needs an author of up to 128 characters.");
    if (pack.license.isEmpty()) return invalid("plugin.json needs a license of up to 64 characters.");
    if (!validPetId(pack.pet)) return invalid("plugin.json needs the id of the pet it is for, such as vpet.");
    if (object.contains("url") && !pack.url.startsWith("https://")) return invalid("plugin.json url must start with https://.");
    if (object.contains("min_app_version")) {
        qsizetype end = 0;
        const auto minimum = QVersionNumber::fromString(pack.minApp, &end);
        if (minimum.isNull() || end != pack.minApp.size()) return invalid("Invalid min_app_version in plugin.json.");
        if (const auto running = QVersionNumber::fromString(appVersion); !running.isNull() && running < minimum)
            return invalid("Needs Agent Pet " + pack.minApp + " or later.");
    }
    return pack;
}
// Merges one pack's fragment into `source`. `claims` maps each replaced name ("states.<name>", "cues.<name>",
// "moods.<mood>.<state>") to the pack that replaced it, so two packs cannot replace the same thing.
bool merge(CatalogSource &source, const PluginPack &pack, QHash<QString, QString> &claims, QString *error) {
    auto fail = [error](const QString &reason) {
        *error = reason;
        return false;
    };
    const auto fragment = readObject(pack.folder + "/animations.json", 1024 * 1024, error);
    if (!error->isEmpty()) return false;
    // Touch, moves and activity art depend on a pet's geometry and timing too closely to patch from outside.
    static const QSet<QString> sections{"schema_version", "overrides", "sequences", "states", "playback", "variants", "moods", "cues", "ambient"};
    for (auto it = fragment.begin(); it != fragment.end(); ++it) {
        if (!sections.contains(it.key())) return fail("Plugins cannot set " + it.key() + " in animations.json.");
        if (it.key() != "schema_version" && it.key() != "overrides" && it.key() != "sequences" && !it.value().isObject())
            return fail("Invalid " + it.key() + " section.");
    }
    if (fragment.value("schema_version").toInt() != catalogSchema)
        return fail(QString("animations.json must have schema_version %1.").arg(catalogSchema));
    QSet<QString> overrides;
    if (fragment.contains("overrides") && !fragment.value("overrides").isArray()) return fail("Invalid overrides section.");
    for (const auto &value : fragment.value("overrides").toArray()) {
        if (!value.isString() || value.toString().isEmpty()) return fail("Invalid override.");
        overrides.insert(value.toString());
    }
    // Replacing anything the pet (or an earlier pack) has is allowed only when the pack says so.
    auto claim = [&](const QString &what) {
        if (!overrides.contains(what)) return fail(QString("Replacing %1 needs \"%1\" in overrides.").arg(what));
        if (const auto owner = claims.value(what); !owner.isEmpty() && owner != pack.id)
            return fail(QString("%1 is already replaced by plugin %2.").arg(what, owner));
        claims.insert(what, pack.id);
        return true;
    };
    // Sequences: frames are PNG files inside the pack folder. Their ids become "<pack>:<path>", which no pet's
    // sequence can be (a pet's paths never hold ':'), so packs never share or shadow a sequence.
    const auto root = QFileInfo(pack.folder).canonicalFilePath();
    const QDir folder(root);
    QHash<QString, QString> local; // The fragment's sequence name -> its id in the merged catalog.
    int frameCount = 0;
    qint64 bytes = 0;
    if (fragment.contains("sequences") && !fragment.value("sequences").isArray()) return fail("Invalid sequences section.");
    for (const auto &entry : fragment.value("sequences").toArray()) {
        const auto object = entry.toObject();
        const auto name = object.value("path").toString();
        if (!safeAssetPath(name) || local.contains(name)) return fail("Invalid or duplicate sequence identifier: " + name);
        QVector<Frame> frames;
        int total = 0;
        for (const auto &value : object.value("frames").toArray()) {
            const auto frame = value.toObject();
            const auto relative = frame.value("path").toString();
            const auto duration = frame.value("duration_ms").toInt(-1);
            if (!safeAssetPath(relative) || !relative.endsWith(".png") || duration < 1 || duration > 60000)
                return fail("Invalid frame path or duration in " + name);
            // A link may point anywhere: only what resolves inside the pack folder counts.
            const QFileInfo file(folder.filePath(relative));
            const auto real = file.canonicalFilePath();
            if (real.isEmpty() || !file.isFile() || !real.startsWith(root + "/")) return fail("Missing frame " + relative);
            if (file.size() > maxFrameBytes) return fail("Frame larger than 4 MiB: " + relative);
            bytes += file.size();
            if (bytes > maxPackBytes) return fail("Frames larger than 64 MiB in all.");
            if (++frameCount > maxFrames) return fail(QString("More than %1 frames.").arg(maxFrames));
            // The header only: whole frames are decoded when they play, as the pet's own are.
            const auto size = QImageReader(real).size();
            if (QImageReader::imageFormat(real) != "png" || size.width() < 1 || size.height() < 1 || size.width() > maxFrameSide
                || size.height() > maxFrameSide)
                return fail(QString("Not a PNG frame of at most %1 pixels a side: %2").arg(maxFrameSide).arg(relative));
            frames.append({real, duration});
            total += duration;
            if (frames.size() > 1000) return fail("Too many frames in " + name);
        }
        if (frames.isEmpty() || total != object.value("duration_ms").toInt()) return fail("Invalid sequence timing: " + name);
        local.insert(name, pack.id + ":" + name);
        source.sequences.insert(pack.id + ":" + name, frames);
    }
    // Packs name sequences locally and only their own: rewritten to their merged ids.
    auto sequenceIds = [&local](const QJsonValue &value, QJsonArray &ids) {
        if (!value.isArray()) return false;
        for (const auto &name : value.toArray()) {
            if (!local.contains(name.toString())) return false;
            ids.append(local.value(name.toString()));
        }
        return true;
    };
    auto choices = [&](const QJsonValue &value, QJsonArray &rewritten) {
        if (!value.isArray() || value.toArray().isEmpty()) return false;
        for (const auto &item : value.toArray()) {
            QJsonArray ids;
            if (!item.isObject() || !sequenceIds(item.toObject().value("sequences"), ids)) return false;
            rewritten.append(QJsonObject{{"weight", item.toObject().value("weight")}, {"sequences", ids}});
        }
        return true;
    };
    auto document = source.document;
    auto states = document.value("states").toObject(), playback = document.value("playback").toObject();
    auto variants = document.value("variants").toObject(), moods = document.value("moods").toObject();
    auto cues = document.value("cues").toObject();
    // States: new ones are named "<pack>.<name>"; any other must be the pet's, replaced as a whole with its variants
    // and mood art, since those must keep the shape of the state they vary.
    static const QRegularExpression ownName(QRegularExpression::anchoredPattern("[a-z0-9-]{1,32}\\.[a-z0-9_-]{1,64}"));
    const auto ownStates = fragment.value("states").toObject(), ownPlayback = fragment.value("playback").toObject();
    for (auto it = ownPlayback.begin(); it != ownPlayback.end(); ++it)
        if (!ownStates.contains(it.key())) return fail("Playback for a state the pack does not define: " + it.key());
    for (auto it = ownStates.begin(); it != ownStates.end(); ++it) {
        const auto &name = it.key();
        if (name.startsWith(pack.id + ".")) {
            if (!ownName.match(name).hasMatch()) return fail("Invalid state name: " + name);
        } else {
            if (!states.contains(name)) return fail(QString("New states must be named %1.<name>: %2").arg(pack.id, name));
            if (!claim("states." + name)) return false;
            variants.remove(name);
            for (const auto &mood : moods.keys()) {
                auto art = moods.value(mood).toObject();
                art.remove(name);
                moods[mood] = art;
            }
        }
        QJsonArray ids;
        if (!sequenceIds(it.value(), ids)) return fail("Unknown sequence for " + name);
        if (!ownPlayback.value(name).isObject()) return fail("Missing playback for " + name);
        states[name] = ids;
        playback[name] = ownPlayback.value(name);
    }
    // Variants add ways to play any state.
    const auto ownVariants = fragment.value("variants").toObject();
    for (auto it = ownVariants.begin(); it != ownVariants.end(); ++it) {
        if (!states.contains(it.key())) return fail("Variants for an unknown state: " + it.key());
        auto list = variants.value(it.key()).toArray();
        QJsonArray added;
        if (!choices(it.value(), added)) return fail("Invalid variants for " + it.key());
        for (const auto &choice : std::as_const(added)) list.append(choice);
        variants[it.key()] = list;
    }
    // Mood art replaces a state's choices in that mood; replacing the pet's own mood art is an override.
    const auto ownMoods = fragment.value("moods").toObject();
    for (auto mood = ownMoods.begin(); mood != ownMoods.end(); ++mood) {
        if ((mood.key() != "happy" && mood.key() != "poor") || !mood.value().isObject()) return fail("Unknown mood: " + mood.key());
        auto art = moods.value(mood.key()).toObject();
        const auto ownArt = mood.value().toObject();
        for (auto it = ownArt.begin(); it != ownArt.end(); ++it) {
            if (!states.contains(it.key())) return fail("Mood art for an unknown state: " + it.key());
            if (art.contains(it.key()) && !claim("moods." + mood.key() + "." + it.key())) return false;
            QJsonArray rewritten;
            if (!choices(it.value(), rewritten)) return fail("Invalid mood art for " + it.key());
            art[it.key()] = rewritten;
        }
        moods[mood.key()] = art;
    }
    // Cues: a reaction pool gains the pack's reactions, or is replaced by them as an override. Mapping a state cue
    // to another state is always an override.
    const auto ownCues = fragment.value("cues").toObject();
    for (auto it = ownCues.begin(); it != ownCues.end(); ++it) {
        const auto *cue = findCue(it.key());
        if (!cue) return fail("Unknown cue: " + it.key());
        if (!cue->reaction) {
            if (!claim("cues." + it.key())) return false;
            cues[it.key()] = it.value();
            continue;
        }
        if (!it.value().isArray() || it.value().toArray().isEmpty()) return fail("Empty pool for cue " + it.key());
        auto pool = cues.value(it.key()).toArray();
        if (overrides.contains("cues." + it.key())) {
            if (!claim("cues." + it.key())) return false;
            pool = {};
        }
        for (const auto &reaction : it.value().toArray()) pool.append(reaction);
        cues[it.key()] = pool;
    }
    // Ambient: more fidgets only; the sleep delay stays the pet's.
    const auto ownAmbient = fragment.value("ambient").toObject();
    for (auto it = ownAmbient.begin(); it != ownAmbient.end(); ++it)
        if (it.key() != "fidgets" || !it.value().isArray()) return fail("Plugins can only add ambient fidgets.");
    if (ownAmbient.contains("fidgets")) {
        auto ambient = document.value("ambient").toObject();
        auto fidgets = ambient.value("fidgets").toArray();
        for (const auto &fidget : ownAmbient.value("fidgets").toArray()) fidgets.append(fidget);
        ambient["fidgets"] = fidgets;
        document["ambient"] = ambient;
    }
    // An override that replaces nothing is most likely a typo.
    for (const auto &what : std::as_const(overrides))
        if (claims.value(what) != pack.id) return fail("Override " + what + " replaces nothing in this pack.");
    document["states"] = states; document["playback"] = playback; document["variants"] = variants;
    document["moods"] = moods; document["cues"] = cues;
    source.document = document;
    return true;
}
}
QString defaultFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins";
}
QVector<PluginPack> scan(const QString &folder, const QString &appVersion) {
    QVector<PluginPack> packs;
    const QDir root(folder);
    for (const auto &id : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!validPetId(id)) {
            qWarning().noquote() << "Ignoring plugin folder" << root.filePath(id) << "(names are a-z, 0-9 and -)";
            continue;
        }
        if (packs.size() == maxPacks) {
            qWarning().noquote() << "Ignoring plugin folders after the first" << maxPacks << "in" << folder;
            break;
        }
        packs.append(readInfo(root.filePath(id), id, appVersion));
    }
    return packs;
}
void apply(CatalogSource &source, const QString &pet, const QStringList &enabled, QVector<PluginPack> &packs) {
    QHash<QString, QString> claims;
    for (auto &pack : packs) {
        if (pack.status == PluginPack::Invalid) {
            if (enabled.contains(pack.id)) qWarning().noquote() << "Plugin" << pack.id << "cannot be used:" << pack.error;
            continue;
        }
        if (!enabled.contains(pack.id)) { pack.status = PluginPack::Off; continue; }
        if (pack.pet != pet) { pack.status = PluginPack::OtherPet; continue; }
        auto candidate = source;
        auto candidateClaims = claims;
        QString error;
        if (merge(candidate, pack, candidateClaims, &error)) {
            const auto catalog = Catalog::build(candidate, &error);
            if (catalog.valid()) error = catalog.contractError();
            else if (error.isEmpty()) error = "Invalid animation catalog.";
        }
        if (!error.isEmpty()) {
            pack.status = PluginPack::Rejected;
            pack.error = error;
            qWarning().noquote() << "Plugin" << pack.id << "is not loaded:" << error;
            continue;
        }
        source = candidate;
        claims = candidateClaims;
        pack.status = PluginPack::Applied;
        pack.error.clear();
        qInfo().noquote() << "Plugin" << pack.id << pack.version << "loaded";
    }
}
}
