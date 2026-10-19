#include "installer.h"
#include "archive.h"
#include "animation/local_pet.h"
#include "version.h"
#include "settings/preferences.h"
#include <QDirIterator>
#include <QCryptographicHash>
#include <QUrl>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QVersionNumber>
#include <QRegularExpression>
#include <QUuid>
#include <cstdio>

namespace pet::packs {
namespace {
constexpr qint64 MiB = 1024 * 1024;
QString root(const QString &data) { return data.isEmpty() ? dataFolder() : data; }
QString parent(const QString &data, const QString &kind) { return data + (kind == "pet" ? "/packs/pets" : "/plugins"); }
QJsonObject object(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly) || f.size() > 8 * MiB) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
bool identity(const QString &kind, const QString &id) {
    return (kind == "pet" || kind == "plugin") && validPetId(id) && portablePath(id) && id != "vpet";
}
bool ordinary(const QString &path) { const QFileInfo f(path); return !f.isSymLink() && !f.isJunction(); }
bool prepare(const QString &data, QString &error) {
    // Refuse redirected managed directories; never traverse links while installing/removing.
    for (const auto &suffix : {QString(), QString("/packs"), QString("/packs/pets"), QString("/plugins"), QString("/pack-pending"), QString("/pack-origins")}) {
        const auto path = data + suffix;
        if (!ordinary(path) || !QDir().mkpath(path)) { error = "Cannot create a safe pack storage directory: " + path; return false; }
    }
    return true;
}
bool clearRemovedPluginSelection(const QString &data, const QString &id, QString &error) {
    PreferencesStore store(data + "/preferences.json");
    auto preferences = store.load();
    if (!store.error().isEmpty()) { error = "Cannot read plugin selection: " + store.error(); return false; }
    if (!preferences.plugins.removeAll(id)) return true;
    if (!store.save(preferences)) { error = "Cannot save plugin selection: " + store.error(); return false; }
    return true;
}
bool queue(const QString &data, const QString &kind, const QString &id, const QString &folder, QString &error,
           const QJsonObject &origin = {}, bool automatic = false) {
    if (!prepare(data, error)) return false;
    QLockFile lock(data + "/pack-install.lock");
    if (!lock.tryLock()) { error = "Another pack operation is in progress."; return false; }
    const auto target = parent(data, kind) + '/' + id;
    if (!ordinary(target)) { error = "Installed pack is a link."; return false; }
    const auto pending = data + "/pack-pending/" + kind + '-' + id;
    if (QFileInfo::exists(pending)) { error = "This pack already has a pending change. Restart Agent Pet first."; return false; }
    QTemporaryDir temp(data + "/pack-pending/stage-XXXXXX");
    if (!temp.isValid()) { error = "Cannot stage pack operation."; return false; }
    if (!folder.isEmpty() && (!QDir().mkpath(temp.path() + "/payload") || !QDir().rename(folder, temp.path() + "/payload/" + id))) { error = "Cannot stage pack payload."; return false; }
    QSaveFile record(temp.path() + "/operation.json");
    const auto bytes = QJsonDocument(QJsonObject{{"kind", kind}, {"id", id}, {"remove", folder.isEmpty()}, {"origin", origin}, {"automatic", automatic}}).toJson();
    if (!record.open(QIODevice::WriteOnly) || record.write(bytes) != bytes.size() || !record.commit()
        || !QDir().rename(temp.path(), pending)) { error = "Cannot save pack operation."; return false; }
    return true;
}
}
namespace {
QString originPath(const QString &base, const QString &kind, const QString &id) { return base + "/pack-origins/" + kind + '-' + id + ".json"; }
bool saveOrigin(const QString &base, const QString &kind, const QString &id, QJsonObject origin, QString &error) {
    const auto path = originPath(base, kind, id);
    if (!ordinary(path)) { error = "Unsafe origin record."; return false; }
    if (origin.isEmpty()) { if (!QFile::exists(path) || QFile::remove(path)) return true; }
    else {
        if (origin["revision"].toString() != sourceRevision(origin["source"].toString(), base)) origin = {};
        QSaveFile file(path); const auto bytes = QJsonDocument(origin).toJson();
        if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) return true;
    }
    error = "Cannot save pack origin."; return false;
}
}
QString sourceRevision(const QString &source, const QString &data) {
    QFile f(root(data) + "/sources/" + QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".revision");
    if (!f.exists()) return "initial";
    if (!f.open(QIODevice::ReadOnly) || f.size() > 64) return {};
    return QString::fromLatin1(f.readAll());
}
QJsonObject packOrigin(const QString &kind, const QString &id, const QString &data) {
    if (!identity(kind, id)) return {};
    const auto o = object(originPath(root(data), kind, id));
    if (o["source"].toString().isEmpty() || o["revision"].toString().isEmpty()
        || o["revision"].toString() != sourceRevision(o["source"].toString(), data)) return {};
    const auto m = object(parent(root(data), kind) + '/' + id + "/pack.json");
    if (m["version"] != o["version"]) return {};
    return o;
}
QJsonObject installationOrigin(const Candidate &c, const QString &source, const QString &digest, const QString &data) {
    const QUrl url(source);
    if (url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment()
        || !QRegularExpression("^[0-9a-f]{64}$").match(digest).hasMatch()) return {};
    return {{"source", source}, {"revision", sourceRevision(source, data)}, {"digest", digest},
        {"version", c.metadata["version"]}, {"license", c.metadata["license"]}, {"author", c.metadata["author"]},
        {"terms", QString::fromLatin1(QCryptographicHash::hash(c.terms.toUtf8(), QCryptographicHash::Sha256).toHex())}};
}
QString dataFolder() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
bool validate(const QString &folder, QJsonObject &meta, QString &terms, QString &error) {
    auto fail = [&](const QString &reason) { error = reason; return false; };
    meta = object(folder + "/pack.json");
    const auto kind = meta.value("kind").toString(), id = meta.value("id").toString();
    if (!identity(kind, id) || QFileInfo(folder).fileName() != id) return fail("Invalid pack identity or reserved id.");
    const QSet<QString> keys{"kind", "id", "version", "schema", "minAppVersion", "name", "author", "license", "terms", "source", "pet"};
    for (auto i = meta.begin(); i != meta.end(); ++i) if (!keys.contains(i.key())) return fail("Unknown pack.json field: " + i.key());
    for (const auto &field : {"name", "author", "license", "version", "minAppVersion", "terms"}) {
        const int limit = QString(field) == "author" ? 128 : QString(field) == "terms" ? 240 : (QString(field) == "version" || QString(field) == "minAppVersion") ? 32 : 64;
        if (!meta.value(field).isString() || meta.value(field).toString().trimmed().isEmpty() || meta.value(field).toString().size() > limit) return fail("Missing or invalid pack metadata: " + QString(field));
    }
    if (meta.contains("source") && (!meta.value("source").isString() || meta.value("source").toString().trimmed().isEmpty() || meta.value("source").toString().size() > 256)) return fail("Invalid source label.");
    static const QRegularExpression semver("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*)?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?$");
    static const QRegularExpression release("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$");
    const auto minimum = meta.value("minAppVersion").toString();
    if (!semver.match(meta.value("version").toString()).hasMatch() || !release.match(minimum).hasMatch()) return fail("Invalid version or minAppVersion.");
    const auto required = QVersionNumber::fromString(minimum), app = QVersionNumber::fromString(AGENT_PET_VERSION);
    if (required.segmentCount() != 3 || required > app || (required == app && QString(AGENT_PET_VERSION).contains('-'))) return fail("Pack requires Agent Pet " + minimum + " or newer.");
    if (meta.value("schema").toDouble(-1) != catalogSchema) return fail("Unsupported catalog schema. This app supports schema 2; ask the author for a compatible pack or the required app version.");
    QSet<QString> paths; qint64 total = 0, png = 0, text = 0, json = 0; int count = 0;
    if (!ordinary(folder)) return fail("Pack directory is a link.");
    QDirIterator it(folder, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next(); const auto f = it.fileInfo(); const auto relative = QDir(folder).relativeFilePath(it.filePath());
        if (++count > (kind == "pet" ? 20000 : 4096) || !portablePath(relative) || !ordinary(f.filePath()) || (!f.isDir() && !f.isFile()) || paths.contains(relative.toCaseFolded())) return fail("Unsafe pack entry: " + relative);
        paths.insert(relative.toCaseFolded()); if (f.isDir()) continue;
        total += f.size(); if (total > (kind == "pet" ? 544 : 80) * MiB) return fail("Pack byte limit exceeded.");
        if (f.suffix() == "png") {
            png += f.size(); if (f.size() > 4 * MiB || png > (kind == "pet" ? 512 : 64) * MiB) return fail("PNG byte limit exceeded.");
            QImageReader reader(f.filePath()); const auto size = reader.size();
            if (reader.format() != "png" || size.isEmpty() || size.width() > 2048 || size.height() > 2048 || reader.read().isNull()) return fail("Invalid PNG or dimensions above 2048 pixels: " + relative);
        } else if (f.suffix() == "json") {
            qint64 limit = 8 * MiB;
            if (QStringList{"pack.json", "pet.json", "plugin.json", "events.json"}.contains(relative)) limit = 64 * 1024;
            else if (relative == "animations.json") limit = (kind == "pet" ? 8 : 1) * MiB;
            else json += f.size();
            if (f.size() > limit || json > 8 * MiB || object(f.filePath()).isEmpty()) return fail("Invalid or oversized JSON: " + relative);
        } else if (f.suffix() == "txt" || f.suffix() == "md") {
            text += f.size(); if (text > MiB) return fail("Terms exceed 1 MiB.");
            QFile file(f.filePath()); if (!file.open(QIODevice::ReadOnly)) return fail("Cannot read terms.");
            QStringDecoder decoder(QStringDecoder::Utf8); const QString decoded = decoder(file.readAll());
            if (decoder.hasError()) return fail("Terms must be UTF-8.");
        } else return fail("Unsupported file: " + relative);
    }
    const auto termPath = meta.value("terms").toString();
    if (!portablePath(termPath) || !(termPath.endsWith(".txt") || termPath.endsWith(".md"))) return fail("Invalid terms path.");
    QFile file(folder + '/' + termPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MiB || file.size() == 0) return fail("Missing terms.");
    terms = QString::fromUtf8(file.readAll());
    if (kind == "pet") { const auto pet = localPets::read(folder); if (!pet.error.isEmpty()) return fail(pet.error); return true; }
    const auto plugin = object(folder + "/plugin.json");
    for (const auto &field : {"id", "name", "author", "version", "license", "pet"})
        if (plugin.value(field) != meta.value(field) || !meta.value(field).isString()) return fail("plugin.json must agree with pack.json: " + QString(field));
    if (plugin.contains("min_app_version") && plugin.value("min_app_version") != meta.value("minAppVersion")) return fail("Conflicting minimum app versions.");
    if (object(folder + "/animations.json").value("schema_version").toDouble(-1) != catalogSchema) return fail("Plugin catalog schema must match pack.json.");
    auto candidates = plugins::scan(QFileInfo(folder).path(), AGENT_PET_VERSION);
    QVector<PluginPack> selected; for (const auto &p : candidates) if (p.id == id) selected.append(p);
    if (selected.size() != 1 || selected[0].status == PluginPack::Invalid) return fail(selected.isEmpty() ? "Missing plugin metadata." : selected[0].error);
    const auto target = meta.value("pet").toString(); QString reason;
    auto source = Catalog::read(PetLibrary::shared().root(), target, &reason);
    if (!source.valid()) { const auto pet = localPets::read(PetLibrary::defaultPetFolder() + '/' + target, &source); if (!pet.error.isEmpty()) return fail("Install the plugin's target pet first: " + target); }
    plugins::apply(source, target, {id}, selected);
    if (selected[0].status != PluginPack::Applied) return fail(selected[0].error);
    return true;
}
std::unique_ptr<Candidate> stage(QString &error, const QString &data) {
    const auto base = root(data); if (!prepare(base, error)) return {};
    auto c = std::make_unique<Candidate>(); c->staging = std::make_unique<QTemporaryDir>(base + "/pack-pending/inspect-XXXXXX");
    if (!c->staging->isValid()) { error = "Cannot create staging directory."; return {}; }
    c->folder = c->staging->path() + "/payload";
    return c;
}
bool inspectStaged(Candidate &c, QString &error, const QString &data) {
    const auto base = root(data), unpacked = c.folder;
    const auto meta = object(unpacked + "/pack.json"); const auto id = meta.value("id").toString(), kind = meta.value("kind").toString();
    if (!identity(kind, id)) { error = "Invalid pack identity or reserved id."; return false; }
    c.folder = c.staging->path() + "/validated/" + id;
    if (!QDir().mkpath(c.staging->path() + "/validated") || !QDir().rename(unpacked, c.folder) || !validate(c.folder, c.metadata, c.terms, error)) return false;
    if (kind == "pet") { for (const auto &pet : PetLibrary::shared().pets()) if (pet.id == id && pet.folder.isEmpty()) { error = "This pet id belongs to the bundled artwork index."; return false; } }
    const auto installed = parent(base, kind) + '/' + id;
    if (QFileInfo::exists(installed)) { const auto old = object(installed + "/pack.json"); c.oldVersion = old.value("version").toString("unknown"); c.oldSource = old.value("source").toString("unverified local content"); if (c.oldVersion.isEmpty()) c.oldVersion = "unknown"; }
    if (kind == "plugin" && !QFileInfo::exists(installed)
        && QDir(parent(base, kind)).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() >= plugins::maxPacks) {
        error = "At most 32 plugins may be installed."; return false;
    }
    return true;
}
static std::unique_ptr<Candidate> inspectInput(const QString &archive, QString &error, const QString &data, const VPetOptions *options, QJsonObject *report) {
    auto c = stage(error, data); if (!c) return {};
    if (options) {
        if (!convertVPetMod(archive, c->folder, *options, *report, error)) return {};
    } else if (!QDir().mkpath(c->folder) || !extract(archive, c->folder, error)) return {};
    if (object(c->folder + "/pack.json")["kind"] == "plugin" && QFileInfo(archive).size() > 80 * MiB) { error = "Plugin archive exceeds 80 MiB."; return {}; }
    if (!inspectStaged(*c, error, data)) return {};
    return c;
}
std::unique_ptr<Candidate> inspect(const QString &archive, QString &error, const QString &data) {
    return inspectInput(archive, error, data, nullptr, nullptr);
}
std::unique_ptr<Candidate> inspectVPetMod(const QString &input, const VPetOptions &options, QJsonObject &report, QString &error, const QString &data) {
    return inspectInput(input, error, data, &options, &report);
}
bool install(Candidate &c, QString &error, const QString &data) {
    QJsonObject meta; QString terms;
    if (!validate(c.folder, meta, terms, error)) return false;
    if (meta != c.metadata || terms != c.terms) { error = "Pack metadata or terms changed after confirmation."; return false; }
    if (!c.origin.isEmpty() && c.origin != installationOrigin(c, c.origin["source"].toString(), c.origin["digest"].toString(), data)) {
        error = "Installation origin changed after confirmation."; return false;
    }
    return queue(root(data), meta.value("kind").toString(), meta.value("id").toString(), c.folder, error, c.origin, c.automatic);
}
bool remove(const QString &kind, const QString &id, QString &error, const QString &data) {
    if (!identity(kind, id)) { error = "Invalid pack identity or built-in pack."; return false; }
    if (!QFileInfo::exists(parent(root(data), kind) + '/' + id)) { error = "Pack is not installed."; return false; }
    return queue(root(data), kind, id, {}, error);
}
bool applyPending(QString &error, const QString &data) {
    const auto base = root(data); if (!prepare(base, error)) return false;
    QLockFile lock(base + "/pack-install.lock"); if (!lock.tryLock()) { error = "Another pack operation is in progress."; return false; }
    const QDir pending(base + "/pack-pending");
    QStringList failures;
    for (const auto &name : pending.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!name.startsWith("pet-") && !name.startsWith("plugin-")) continue;
        const auto job = pending.filePath(name); const auto op = object(job + "/operation.json");
        const auto kind = op.value("kind").toString(), id = op.value("id").toString();
        if (!ordinary(job) || !identity(kind, id) || name != kind + '-' + id || !op.value("remove").isBool()) { error = "Invalid pending pack operation."; return false; }
        const auto target = parent(base, kind) + '/' + id, payload = job + "/payload/" + id, backup = job + "/previous";
        const auto rollback = [&] {
            if (!op["remove"].toBool() && QFileInfo::exists(target) && !QFileInfo::exists(payload)) {
                if (!QDir().mkpath(QFileInfo(payload).path()) || !QDir().rename(target, payload)) return false;
            }
            return !QFileInfo::exists(backup) || QDir().rename(backup, target);
        };
        if (!ordinary(target) || !ordinary(backup)) { error = "Unsafe installed pack path."; return false; }
        // Recovery after interruption between the two renames.
        if (QFileInfo::exists(backup) && !QFileInfo::exists(target)) {
            if (!QDir().rename(backup, target)) { error = "Cannot restore previous pack."; return false; }
        }
        if (op["automatic"].toBool() && QFileInfo::exists(payload)
            && (op["origin"].toObject()["revision"].toString() != sourceRevision(op["origin"].toObject()["source"].toString(), base))) {
            QDir(job).removeRecursively(); continue; // Removed/re-added source revokes staged automatic work.
        }
        if (!op.value("remove").toBool()) {
            if (!QFileInfo::exists(payload) && QFileInfo::exists(target)) {
                if (!saveOrigin(base, kind, id, op["origin"].toObject(), error)) return false;
                if (op["remove"].toBool() && kind == "plugin" && !clearRemovedPluginSelection(base, id, error)) return false;
                QDir(job).removeRecursively(); continue;
            }
            QJsonObject meta; QString terms, reason;
            bool valid = validate(payload, meta, terms, reason);
            if (valid && meta.value("kind").toString() != kind) { reason = "Pending pack kind does not match its operation."; valid = false; }
            if (valid && kind == "plugin" && !QFileInfo::exists(target) && QDir(parent(base, kind)).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() >= plugins::maxPacks) {
                reason = "At most 32 plugins may be installed."; valid = false;
            }
            if (!valid) {
                // Validation failures cannot improve by retrying this payload. The restored
                // installed pack is intact; discard the rejected job and process later jobs.
                failures << name + ": " + reason;
                if (!QDir(job).removeRecursively()) failures << name + ": Cannot discard rejected pack operation.";
                continue;
            }
        }
        if (QFileInfo::exists(target) && !QDir().rename(target, backup)) { error = "Cannot retain previous pack."; return false; }
        if (!op.value("remove").toBool() && !QDir().rename(payload, target)) {
            if (QFileInfo::exists(backup)) QDir().rename(backup, target);
            error = "Cannot install pack; previous pack retained."; return false;
        }
        const auto priorOrigin = object(originPath(base, kind, id));
        if (!saveOrigin(base, kind, id, op["remove"].toBool() ? QJsonObject{} : op["origin"].toObject(), error)) {
            rollback(); return false;
        }
        if (op.value("remove").toBool() && kind == "plugin" && !clearRemovedPluginSelection(base, id, error)) {
            QString originError;
            if (!saveOrigin(base, kind, id, priorOrigin, originError)) error += " " + originError;
            if (!rollback()) error += " Cannot restore previous pack.";
            return false;
        }
        if (!QDir(job).removeRecursively()) { error = "Pack changed, but its transaction backup could not be cleaned up."; return false; }
    }
    error = failures.join('\n');
    return failures.isEmpty();
}
int command(const QStringList &args) {
    QString error; bool ok = false;
    if (args.value(1) == "import" && args.size() >= 3) {
        std::unique_ptr<Candidate> c;
        QString source = args[2];
        if (source == "--vpet-mod") {
            VPetOptions options; QSet<QString> seen; QString reportFile;
            if (args.size() < 4 || (args.size() - 4) % 2) { std::fprintf(stderr, "Expected --vpet-mod <path> [--id ID] [--author AUTHOR] [--license LICENCE] --terms FILE [--report FILE]\n"); return 2; }
            source = args[3];
            for (int i = 4; i < args.size(); i += 2) {
                const auto key = args[i];
                if (seen.contains(key)) { std::fprintf(stderr, "Duplicate option.\n"); return 2; } seen.insert(key);
                if (key == "--id") options.id = args[i + 1];
                else if (key == "--author") options.author = args[i + 1];
                else if (key == "--license") options.license = args[i + 1];
                else if (key == "--terms") options.terms = args[i + 1];
                else if (key == "--report") reportFile = args[i + 1];
                else { std::fprintf(stderr, "Unknown VPet import option.\n"); return 2; }
            }
            QJsonObject report; c = inspectVPetMod(source, options, report, error);
            if (c) {
                const auto bytes = QJsonDocument(report).toJson();
                std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
                if (!reportFile.isEmpty()) {
                    QFile file(reportFile); // Never overwrite mod content, terms, or an existing report.
                    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(bytes) != bytes.size() || !file.flush()) { std::fprintf(stderr, "Cannot create report file.\n"); return 1; }
                }
            }
        } else if (args.size() == 3) c = inspect(source, error);
        else { std::fprintf(stderr, "Unexpected import arguments.\n"); return 2; }
         if (!c) { std::fprintf(stderr, "%s\n", qPrintable(error)); return 1; }
        const auto m = c->metadata;
        std::printf("Unverified local content: %s\n%s by %s\nVersion: %s\nLicence: %s\nSource: %s\n%s\n", qPrintable(source), qPrintable(m.value("name").toString()), qPrintable(m.value("author").toString()), qPrintable(m.value("version").toString()), qPrintable(m.value("license").toString()), qPrintable(m.value("source").toString()), qPrintable(c->terms));
        if (!c->oldVersion.isEmpty()) std::printf("Replace installed version %s (%s), including equal versions or downgrades.\n", qPrintable(c->oldVersion), qPrintable(c->oldSource));
        std::printf("Accept these terms and install? Type yes: "); std::fflush(stdout);
        char answer[16]{}; if (!std::fgets(answer, sizeof answer, stdin) || QByteArray(answer).trimmed() != "yes") return 2;
        ok = install(*c, error);
    } else if (args.value(1) == "remove" && args.size() == 4) {
        std::printf("Remove %s %s? Type yes: ", qPrintable(args[2]), qPrintable(args[3])); std::fflush(stdout);
        char answer[16]{}; if (!std::fgets(answer, sizeof answer, stdin) || QByteArray(answer).trimmed() != "yes") return 2;
        ok = remove(args[2], args[3], error);
    } else { std::fprintf(stderr, "Usage: agent-pet import <file.agentpet>\n       agent-pet import --vpet-mod <folder|zip|lps> [--id ID] [--author AUTHOR] [--license LICENCE] --terms FILE [--report FILE]\n       agent-pet remove <pet|plugin> <id>\n"); return 2; }
    if (ok) {
        QLockFile runtime(dataFolder() + "/pack-runtime.lock");
        if (runtime.tryLock()) { ok = applyPending(error); if (ok) std::puts("Pack change installed. It takes effect on the next start."); }
        else std::puts("Pack change queued. Restart Agent Pet to apply it.");
    }
    if (!ok) std::fprintf(stderr, "%s\n", qPrintable(error));
    return ok ? 0 : 1;
}
}
