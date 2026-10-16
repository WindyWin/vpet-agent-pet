#include "local_pet.h"
#include "version.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QVersionNumber>

namespace pet::localPets {
namespace {
bool portable(const QString &path) {
    if (!safeAssetPath(path)) return false;
    static const QRegularExpression reserved("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression forbidden("[\\x00-\\x1f\\x7f\"&'<>;\\\\\\[\\]:*?|]");
    for (const auto &part : path.split('/'))
        if (part.isEmpty() || part.startsWith('.') || part.endsWith('.') || part.endsWith(' ')
            || reserved.match(part).hasMatch() || forbidden.match(part).hasMatch()) return false;
    return true;
}
QJsonObject object(const QString &path, qint64 limit) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool text(const QJsonObject &o, const QString &key, int limit) {
    return o.value(key).isString() && !o.value(key).toString().trimmed().isEmpty() && o.value(key).toString().size() <= limit;
}
bool keys(const QJsonObject &o, const QSet<QString> &allowed) {
    for (auto it = o.begin(); it != o.end(); ++it) if (!allowed.contains(it.key())) return false;
    return true;
}
}
PetInfo read(const QString &folder, CatalogSource *source) {
    PetInfo pet;
    pet.id = QFileInfo(folder).fileName(); pet.name = pet.id; pet.folder = folder;
    auto fail = [&](const QString &reason) { pet.error = reason; pet.preview.clear(); return pet; };
    if (!validPetId(pet.id) || !portable(pet.id)) return fail("Invalid pet folder identifier.");
    if (pet.id == "vpet") return fail("The built-in pet id vpet is reserved.");
    if (QFileInfo(folder).isSymLink() || QFileInfo(folder).isJunction() || !QFileInfo(folder).isDir()) return fail("Pet folder must be a directory, not a link.");
    const QDir dir(folder);
    // Check every entry before reading metadata or following references, including unreferenced files.
    QSet<QString> paths, folded;
    qint64 bytes = 0, pngBytes = 0, jsonBytes = 0, termsBytes = 0;
    int entries = 0;
    QDirIterator it(folder, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next(); const auto file = it.fileInfo();
        const auto path = dir.relativeFilePath(file.absoluteFilePath());
        if (++entries > maxEntries) return fail("More than 20000 entries in the pet folder.");
        if (!portable(path) || file.isSymLink() || file.isJunction() || (!file.isDir() && !file.isFile())) return fail("Unsafe pack entry: " + path);
        if (folded.contains(path.toCaseFolded())) return fail("Case-insensitive duplicate pack entry: " + path);
        folded.insert(path.toCaseFolded());
        if (file.isDir()) {
            static const QRegularExpression directoryName(QRegularExpression::anchoredPattern("[A-Za-z0-9_-][A-Za-z0-9._-]*"));
            if (!directoryName.match(file.fileName()).hasMatch()) return fail("Invalid frame folder name: " + path);
            continue;
        }
        paths.insert(path); bytes += file.size();
        if (bytes > maxTotalBytes) return fail("Pet folder exceeds 544 MiB.");
        const auto suffix = file.suffix();
        if (suffix == "png") {
            if (file.size() > plugins::maxFrameBytes) return fail("PNG larger than 4 MiB: " + path);
            pngBytes += file.size();
            if (pngBytes > maxPngBytes) return fail("Pet PNGs exceed 512 MiB.");
            QImageReader reader(file.absoluteFilePath());
            const auto size = reader.size();
            if (reader.format() != "png" || !size.isValid() || size.isEmpty()
                || size.width() > 2048 || size.height() > 2048) return fail("Invalid PNG or dimensions above 2048 pixels: " + path);
        } else if (suffix == "json") {
            qint64 limit = 8LL * 1024 * 1024;
            if (QStringList{"pack.json", "pet.json", "plugin.json", "events.json"}.contains(path)) limit = 64 * 1024;
            else if (path != "animations.json") jsonBytes += file.size();
            if (file.size() > limit || jsonBytes > 8 * 1024 * 1024) return fail("JSON size limit exceeded: " + path);
            if (object(file.absoluteFilePath(), limit).isEmpty()) return fail("Invalid JSON object: " + path);
        } else if (suffix == "txt" || suffix == "md") {
            termsBytes += file.size();
            if (termsBytes > 1024 * 1024) return fail("Terms exceed 1 MiB.");
            QFile contents(file.absoluteFilePath());
            if (!contents.open(QIODevice::ReadOnly)) return fail("Cannot read terms: " + path);
            QStringDecoder decoder(QStringDecoder::Utf8); const QString decoded = decoder(contents.readAll());
            if (decoder.hasError()) return fail("Terms must be UTF-8: " + path);
        } else return fail("Unsupported pack file: " + path);
    }
    const auto pack = object(dir.filePath("pack.json"), 64 * 1024);
    if (!keys(pack, {"kind", "id", "version", "schema", "minAppVersion", "name", "author", "license", "terms", "source"})
        || pack.value("kind").toString() != "pet" || pack.value("id").toString() != pet.id
        || !text(pack, "name", 64) || !text(pack, "author", 128) || !text(pack, "license", 64)
        || !text(pack, "version", 32) || !text(pack, "minAppVersion", 32)
        || (pack.contains("source") && !text(pack, "source", 256))) return fail("Invalid pack.json metadata.");
    pet.name = pack.value("name").toString(); pet.author = pack.value("author").toString();
    pet.license = pack.value("license").toString(); pet.version = pack.value("version").toString();
    static const QRegularExpression semver(QRegularExpression::anchoredPattern(
        "(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?"));
    static const QRegularExpression release(QRegularExpression::anchoredPattern("(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"));
    const auto minimum = pack.value("minAppVersion").toString();
    if (!semver.match(pet.version).hasMatch() || !release.match(minimum).hasMatch()) return fail("Invalid pack version or minAppVersion.");
    qsizetype suffix = 0;
    const auto app = QVersionNumber::fromString(AGENT_PET_VERSION), required = QVersionNumber::fromString(minimum, &suffix);
    if (required.segmentCount() != 3 || suffix != minimum.size()) return fail("Invalid minAppVersion.");
    if (required > app || (required == app && QString(AGENT_PET_VERSION).contains('-')))
        return fail("Pack requires Agent Pet " + minimum + " or newer.");
    if (pack.value("schema").toDouble(-1) != catalogSchema) return fail("Unsupported pack catalog schema; use scripts/migrate_catalog.py for older catalogs.");
    const auto terms = pack.value("terms").toString();
    if (!portable(terms) || !paths.contains(terms) || !(terms.endsWith(".txt") || terms.endsWith(".md"))
        || QFileInfo(dir.filePath(terms)).size() == 0) return fail("Missing or invalid pack terms.");
    const auto metadata = object(dir.filePath("pet.json"), 64 * 1024);
    if (!keys(metadata, {"schema_version", "id", "name", "author", "url", "terms", "preview"})
        || metadata.value("schema_version").toDouble(-1) != 1
        || metadata.value("id") != pack.value("id") || metadata.value("name") != pack.value("name")
        || metadata.value("author") != pack.value("author")
        || "licenses/" + metadata.value("terms").toString() != terms)
        return fail("pet.json must agree with pack.json identity and terms.");
    if (metadata.contains("url") && (!text(metadata, "url", 256) || !metadata.value("url").toString().startsWith("https://")))
        return fail("Invalid pet credit URL.");
    pet.url = metadata.value("url").toString(); pet.terms = dir.filePath(terms);
    if (metadata.value("preview").toString() != "preview.png" || !paths.contains("preview.png")) return fail("Missing preview.png.");
    pet.preview = dir.filePath("preview.png");
    const auto previewSize = QImageReader(pet.preview).size();
    if (previewSize.width() > 512 || previewSize.height() > 512) return fail("Preview exceeds 512 pixels.");
    QString reason;
    auto catalogSource = Catalog::read({}, pet.id, &reason, folder);
    if (!catalogSource.valid()) return fail(reason);
    QSet<QString> frames;
    int count = 0;
    for (const auto &sequence : catalogSource.sequences)
        for (const auto &frame : sequence) {
            if (++count > maxFrames) return fail("More than 10000 frame references.");
            const auto relative = dir.relativeFilePath(frame.path);
            if (!paths.contains(relative) || !relative.endsWith(".png")) return fail("Missing PNG frame: " + relative);
            frames.insert("assets/" + pet.id + "/" + relative);
        }
    const auto manifest = object(dir.filePath("manifest.json"), 8 * 1024 * 1024);
    if (manifest.value("schema_version").toDouble(-1) != 1 || !manifest.value("files").isArray()) return fail("Invalid frame manifest.");
    QSet<QString> listed; qint64 total = 0;
    for (const auto &value : manifest.value("files").toArray()) {
        const auto entry = value.toObject(); const auto path = entry.value("path").toString();
        if (!frames.contains(path) || listed.contains(path)) return fail("Manifest has an unknown or duplicate frame: " + path);
        listed.insert(path);
        QFile frame(dir.filePath(path.mid(QString("assets/" + pet.id + "/").size())));
        if (!frame.open(QIODevice::ReadOnly) || entry.value("bytes").toDouble(-1) != frame.size()) return fail("Frame size mismatch: " + path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&frame) || hash.result().toHex() != entry.value("sha256").toString().toLatin1()) return fail("Frame checksum mismatch: " + path);
        total += frame.size();
    }
    if (listed != frames || manifest.value("file_count").toDouble(-1) != listed.size()
        || manifest.value("total_bytes").toDouble(-1) != total) return fail("Incomplete or inconsistent frame manifest.");
    const auto catalog = Catalog::build(catalogSource, &reason);
    if (!catalog.valid()) return fail(reason);
    if (const auto error = catalog.contractError(); !error.isEmpty()) return fail(error);
    if (source) *source = catalogSource;
    return pet;
}
}
