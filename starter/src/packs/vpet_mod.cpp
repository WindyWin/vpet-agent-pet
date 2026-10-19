#include "vpet_mod.h"
#include "archive.h"
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <algorithm>
#include <stdexcept>

static void initVPetResources() {
    static const bool initialized = [] { Q_INIT_RESOURCE(vpet_conversion); return true; }();
    Q_UNUSED(initialized);
}
namespace pet::packs {
bool modPath(const QString &path) {
    static const QRegularExpression unsafe("[\\x00-\\x1f\\x7f<>:\"\\\\|?*]");
    static const QRegularExpression reserved("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption);
    if (path.isEmpty() || path.size() > 240) return false;
    for (const auto &part : path.split('/'))
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ')
            || unsafe.match(part).hasMatch() || reserved.match(part).hasMatch()) return false;
    return true;
}
namespace {
constexpr qint64 MiB = 1024 * 1024;
// Python sorts Unicode code points, whereas QString's default order is UTF-16.
bool unicodeLess(const QString &a, const QString &b) {
    const auto left = a.toUcs4(), right = b.toUcs4();
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
}
void require(bool ok, const QString &why) { if (!ok) throw std::runtime_error(why.toUtf8().constData()); }
bool ordinary(const QFileInfo &f) { return !f.isSymLink() && !f.isJunction(); }
QByteArray read(const QString &path, qint64 limit) {
    QFile f(path);
    require(ordinary(QFileInfo(path)) && f.open(QIODevice::ReadOnly) && f.size() <= limit, "Cannot read bounded regular file: " + path);
    const auto bytes = f.read(limit + 1);
    require(bytes.size() <= limit && f.error() == QFile::NoError, "File exceeds limit or could not be read: " + path);
    return bytes;
}
QString utf8(const QByteArray &bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8); const QString text = decoder(bytes);
    require(!decoder.hasError(), "Expected UTF-8 text."); return text;
}
void write(const QString &path, const QByteArray &bytes) {
    require(QDir().mkpath(QFileInfo(path).path()), "Cannot create conversion directory.");
    QFile f(path); require(f.open(QIODevice::WriteOnly | QIODevice::NewOnly) && f.write(bytes) == bytes.size() && f.flush(), "Cannot write converted file: " + path);
}
void json(const QString &path, const QJsonObject &value) { write(path, QJsonDocument(value).toJson()); }
QJsonObject lps(const QString &path) {
    QJsonObject records;
    for (const auto &raw : utf8(read(path, MiB)).split('\n')) {
        const auto line = raw.trimmed(); if (line.isEmpty() || line.startsWith("///")) continue;
        const auto parts = line.split(":|"); const auto first = parts[0].indexOf('#');
        const auto key = (first < 0 ? parts[0] : parts[0].left(first)).toLower();
        QJsonObject fields{{"value", first < 0 ? QString() : parts[0].mid(first + 1)}};
        for (int i = 1; i < parts.size(); ++i) { const auto at = parts[i].indexOf('#'); if (at >= 0) fields[parts[i].left(at).toLower()] = parts[i].mid(at + 1); }
        auto values = records[key].toArray(); values.append(fields); records[key] = values;
    }
    return records;
}
QJsonObject first(const QJsonObject &records, const QString &key) { return records[key].toArray().isEmpty() ? QJsonObject{} : records[key].toArray().first().toObject(); }
QStringList definitions(const QString &input) {
    const QFileInfo f(input); require(ordinary(f), "Linked mod paths are not supported.");
    if (f.isFile() && f.suffix() == "lps") return {f.absoluteFilePath()};
    require(f.isDir(), "Choose a mod folder, ZIP, or pet definition (.lps).");
    QStringList result;
    for (const auto &base : {input + "/pet", input}) {
        require(ordinary(QFileInfo(base)), "Linked mod directories are not supported.");
        for (const auto &entry : QDir(base).entryList({"*.lps"}, QDir::Files, QDir::Name)) result.append(base + '/' + entry);
        if (!result.isEmpty()) break;
    }
    if (result.isEmpty() && QFileInfo::exists(input + ".lps")) result.append(input + ".lps");
    require(result.size() <= 64, "Too many pet definitions.");
    return result;
}
struct Frame { QString name, prefix, file; qint64 index, duration; QByteArray hash; };
QVector<Frame> frames(const QStringList &paths) {
    static const QRegularExpression pattern("(?:^|_)([0-9]+)_([0-9]+)\\.png$");
    QVector<Frame> result; QMap<QPair<QString, qint64>, QByteArray> pictures; QSet<qint64> indices;
    for (const auto &path : paths) {
        auto name = QFileInfo(path).completeBaseName() + ".png"; const auto match = pattern.match(name);
        require(modPath(name) && match.hasMatch(), "Invalid frame filename: " + name);
        bool okIndex, okDuration; const auto index = match.captured(1).toLongLong(&okIndex), duration = match.captured(2).toLongLong(&okDuration);
        require(okIndex && okDuration && duration > 0 && duration <= 2147483647, "Invalid frame index or duration: " + name);
        const auto prefix = name.left(match.capturedStart()); const auto bytes = read(path, 4 * MiB);
        QImageReader reader(path, "png");
        // Only PNG is accepted. Avoid probing unrelated native image plugins
        // when conversion runs on a worker thread.
        reader.setAutoDetectImageFormat(false);
        const auto size = reader.size();
        require(size.width() > 0 && size.height() > 0 && size.width() <= 2048 && size.height() <= 2048, "PNG dimensions must be 1–2048 pixels: " + name);
        const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); const auto key = qMakePair(prefix, index);
        require(!pictures.contains(key) || pictures[key] == hash, "Conflicting duplicate frame index: " + name);
        pictures[key] = hash; indices.insert(index); result.append({name, prefix, path, index, duration, hash});
    }
    require(!result.isEmpty(), "Empty frame sequence."); const bool runs = indices.size() != pictures.size();
    std::sort(result.begin(), result.end(), [runs](const Frame &a, const Frame &b) {
        if (runs && a.prefix != b.prefix) return unicodeLess(a.prefix, b.prefix);
        return a.index != b.index ? a.index < b.index : unicodeLess(a.name, b.name);
    });
    return result;
}
void references(const QJsonValue &value, const QJsonObject &reasons, QJsonArray &paths) {
    if (value.isString() && reasons.contains(value.toString())) paths.append(value);
    else if (value.isArray()) for (const auto &item : value.toArray()) references(item, reasons, paths);
    else if (value.isObject()) { const auto object = value.toObject(); for (auto it = object.begin(); it != object.end(); ++it) references(it.value(), reasons, paths); }
}
}
bool convertVPetMod(const QString &input, const QString &output, const VPetOptions &options, QJsonObject &report, QString &error) {
    try {
        initVPetResources();
        require(QRegularExpression("^[a-z0-9-]{1,32}$").match(options.id).hasMatch() && options.id != "vpet" && portablePath(options.id), "Use a portable lowercase pet id (1–32 letters, digits or hyphens), other than vpet.");
        require(!QFileInfo::exists(output), "Conversion output already exists.");
        QTemporaryDir extracted; require(extracted.isValid(), "Cannot create temporary mod directory.");
        QString source = input;
        if (QFileInfo(input).suffix().compare("zip", Qt::CaseInsensitive) == 0) {
            require(extract(input, extracted.path(), error, true), error); source = extracted.path();
            // Common workshop exports wrap the mod in one directory.
            for (int depth = 0; depth < 8 && definitions(source).isEmpty(); ++depth) {
                const auto dirs = QDir(source).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                if (dirs.size() != 1) break; source += '/' + dirs[0];
            }
        }
        QStringList pets; QJsonObject records;
        for (const auto &path : definitions(source)) { const auto parsed = lps(path); if (parsed.contains("pet")) { pets.append(path); records = parsed; } }
        require(pets.size() == 1, "Expected one pet definition; choose pet/<name>.lps to select a pet.");
        const auto pet = first(records, "pet"); const auto relative = pet.value("path").toString(QFileInfo(pets[0]).completeBaseName());
        require(modPath(relative), "Unsafe graph path in pet definition.");
        QString graphs = QFileInfo(pets[0]).absolutePath();
        for (const auto &part : relative.split('/')) { graphs += '/' + part; require(ordinary(QFileInfo(graphs)) && QFileInfo(graphs).isDir(), "Graph directories must be regular local directories."); }
        auto name = pet["petname"].toString(); if (name.isEmpty()) name = pet["value"].toString(); if (name.isEmpty()) name = options.id;
        auto author = options.author; if (author.isEmpty()) author = pet["author"].toString(); if (author.isEmpty()) author = first(records, "author")["value"].toString();
        auto license = options.license; if (license.isEmpty()) license = pet["license"].toString(); if (license.isEmpty()) license = first(records, "license")["value"].toString();
        require(!author.isEmpty() && !license.isEmpty() && !options.terms.isEmpty(), "Supply author and licence if absent from the mod, and its licence text with --terms.");
        auto terms = utf8(read(options.terms, MiB)); terms.replace("\r\n", "\n"); terms.replace('\r', '\n');
        require(!terms.trimmed().isEmpty() && terms.toUtf8().size() <= MiB, "Terms must contain UTF-8 text, at most 1 MiB.");
        QMap<QString, QStringList> available; QSet<QString> names; qint64 total = 0; int entries = 0;
        QDirIterator scan(graphs, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (scan.hasNext()) {
            scan.next(); const auto f = scan.fileInfo();
            require(++entries <= 20000 && ordinary(f), "Too many mod entries, or linked mod content.");
            if (!f.isFile() || f.suffix().compare("png", Qt::CaseInsensitive)) continue;
            total += f.size(); require(f.size() <= 4 * MiB && total <= 512 * MiB, "Mod PNG byte limit exceeded.");
            const auto rel = QDir(graphs).relativeFilePath(f.filePath()); const auto normalized = rel.left(rel.lastIndexOf('.')) + ".png";
            require(modPath(normalized) && !names.contains(normalized), "Unsafe or duplicate PNG filename: " + rel); names.insert(normalized);
            auto parent = QFileInfo(rel).path(); available[parent].append(f.filePath());
        }
        auto availablePaths = available.keys(); std::sort(availablePaths.begin(), availablePaths.end(), unicodeLess);
        QString idle = "Default/Nomal/1";
        if (!available.contains(idle)) { idle.clear(); for (const auto &path : availablePaths) if (path == "Default" || path.startsWith("Default/")) { idle = path; break; } }
        require(!idle.isEmpty(), "Mod has no Default idle PNG sequence.");
        auto catalog = QJsonDocument::fromJson(read(":/vpet-conversion/assets/vpet/animations.json", 8 * MiB)).object();
        require(!catalog.isEmpty(), "Missing conversion template.");
        QJsonObject mapping, reasons, sequenceReport; QSet<QString> used;
        for (const auto &value : catalog["sequences"].toArray()) {
            const auto path = value.toObject()["path"].toString(); auto normal = path; normal.replace(QRegularExpression("Happy|PoorCondition|Ill"), "Nomal");
            const auto chosen = available.contains(path) ? path : available.contains(normal) ? normal : idle;
            const QString reason = chosen == path ? "exact" : chosen == normal ? "normal" : "idle";
            mapping[path] = chosen; reasons[path] = reason; used.insert(chosen); sequenceReport[path] = QJsonObject{{"source", chosen}, {"kind", reason}};
        }
        QJsonArray files; QMap<QString, QJsonArray> loaded; QString preview; qint64 copied = 0;
        auto paths = used.values(); std::sort(paths.begin(), paths.end(), unicodeLess); int sequenceIndex = 0;
        for (const auto &path : paths) {
            // The reference rejects nested directories inside a selected sequence.
            for (auto it = available.begin(); it != available.end(); ++it) require(!it.key().startsWith(path + '/'), "Nested PNG sequence: " + path);
            int frameIndex = 0;
            for (const auto &frame : frames(available[path])) {
                const auto relative = QString("frames/s%1/f%2.png").arg(sequenceIndex, 4, 10, QLatin1Char('0')).arg(frameIndex++, 5, 10, QLatin1Char('0'));
                const auto bytes = read(frame.file, 4 * MiB);
                require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) == frame.hash, "Mod frame changed during conversion.");
                write(output + '/' + relative, bytes); copied += bytes.size();
                const auto asset = "assets/" + options.id + '/' + relative;
                files.append(QJsonObject{{"path", asset}, {"bytes", bytes.size()}, {"sha256", QString::fromLatin1(frame.hash.toHex())}});
                loaded[path].append(QJsonObject{{"path", asset}, {"duration_ms", frame.duration}});
                if (path == idle && preview.isEmpty()) preview = output + '/' + relative;
            }
            ++sequenceIndex;
        }
        catalog["asset_root"] = "assets/" + options.id; QJsonArray sequences;
        for (const auto &value : catalog["sequences"].toArray()) {
            auto seq = value.toObject(); const auto items = loaded[mapping[seq["path"].toString()].toString()]; qint64 duration = 0;
            for (const auto &frame : items) duration += frame.toObject()["duration_ms"].toInteger();
            require(duration <= 2147483647, "Sequence duration exceeds supported limit."); seq["frames"] = items; seq["duration_ms"] = duration; sequences.append(seq);
        }
        catalog["sequences"] = sequences;
        auto touch = catalog["touch"].toObject(); QJsonArray regions;
        auto integer = [](const QJsonObject &object, const QString &key) { bool ok; const auto value = object[key].toString().toInt(&ok); require(ok, "Invalid touch coordinate: " + key); return value; };
        for (const auto &pair : {qMakePair(QString("pinch"), QString("pinch")), qMakePair(QString("touchhead"), QString("touch_head")), qMakePair(QString("touchbody"), QString("touch_body"))}) {
            if (!records.contains(pair.first)) continue; const auto fields = first(records, pair.first); QJsonArray rect;
            for (const auto &key : {"px", "py", "sw", "sh"}) rect.append(integer(fields, key));
            regions.append(QJsonObject{{"state", pair.second}, {"rect", rect}});
        }
        touch["regions"] = regions;
        if (records.contains("side")) { auto edge = touch["edge"].toObject(); for (const auto &side : {"left", "right"}) { auto field = edge[side].toObject(); field["at"] = integer(first(records, "side"), side); edge[side] = field; } touch["edge"] = edge; }
        else touch.remove("edge"); catalog["touch"] = touch;
        auto states = catalog["states"].toObject(); auto moves = catalog["moves"].toObject(); QJsonArray kept;
        for (const auto &move : moves["list"].toArray()) { bool exact = true; for (const auto &path : states[move.toObject()["state"].toString()].toArray()) if (reasons[path.toString()] != "exact") exact = false; if (exact) kept.append(move); }
        if (kept.isEmpty()) catalog.remove("moves"); else { moves["list"] = kept; catalog["moves"] = moves; }
        QImageReader reader(preview, "png");
        reader.setAutoDetectImageFormat(false);
        const auto size = reader.size();
        if (size.width() > 512 || size.height() > 512) { auto image = reader.read(); require(!image.isNull() && image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(output + "/preview.png", "PNG"), "Cannot resize preview."); }
        else write(output + "/preview.png", read(preview, 4 * MiB));
        write(output + "/licenses/TERMS.txt", terms.toUtf8());
        json(output + "/pack.json", {{"kind", "pet"}, {"id", options.id}, {"name", name}, {"author", author}, {"license", license}, {"version", "1.0.0"}, {"schema", 2}, {"minAppVersion", "0.17.0"}, {"terms", "licenses/TERMS.txt"}, {"source", "vpet-mod"}});
        json(output + "/pet.json", {{"schema_version", 1}, {"id", options.id}, {"name", name}, {"author", author}, {"terms", "TERMS.txt"}, {"preview", "preview.png"}});
        json(output + "/animations.json", catalog); json(output + "/manifest.json", {{"schema_version", 1}, {"file_count", files.size()}, {"total_bytes", copied}, {"files", files}});
        auto addVariants = [&](const QJsonObject &variants) { for (auto it = variants.begin(); it != variants.end(); ++it) { auto paths = states[it.key()].toArray(); for (const auto &v : it.value().toArray()) for (const auto &p : v.toObject()["sequences"].toArray()) paths.append(p); states[it.key()] = paths; } };
        addVariants(catalog["variants"].toObject()); const auto moods = catalog["moods"].toObject(); for (auto it = moods.begin(); it != moods.end(); ++it) addVariants(it.value().toObject());
        const auto activities = catalog["activity"].toObject(); for (auto it = activities.begin(); it != activities.end(); ++it) { auto paths = states[it.key()].toArray(); references(it.value(), reasons, paths); states[it.key()] = paths; }
        const auto contracts = QJsonDocument::fromJson(read(":/vpet-conversion/src/animation/cues.json", MiB)).object()["cues"].toObject();
        const auto mappedCues = catalog.value("cues").toObject(); QJsonObject cues;
        for (auto it = contracts.begin(); it != contracts.end(); ++it) {
            // operator[] on a temporary object returns a dangling QJsonValueRef.
            const QJsonValue target = mappedCues.contains(it.key()) ? mappedCues.value(it.key()) : it.value().toObject().value("state");
            QJsonArray targets;
            if (target.isString()) targets.append(target); else for (const auto &v : target.toArray()) targets.append(v.toObject()["state"]);
            bool exact = true; int count = 0; for (const auto &state : targets) for (const auto &p : states[state.toString()].toArray()) { ++count; if (reasons[p.toString()] != "exact") exact = false; }
            cues[it.key()] = count && exact ? "real" : "fallback";
        }
        QJsonArray unused; for (const auto &path : availablePaths) if (!used.contains(path)) unused.append(path);
        report = {{"id", options.id}, {"tags", records.value("tag").toArray()}, {"raise_points", records.value("raisepoint").toArray()}, {"cues", cues}, {"sequences", sequenceReport}, {"unused_folders", unused},
            {"notes", QJsonArray{"Raise points retained in report; the runtime has no raise-point field.", "Matching movement art uses template movement speeds and geometry (500-unit coordinates)."}}};
        return true;
    } catch (const std::exception &e) { error = QString::fromUtf8(e.what()); return false; }
}
}
