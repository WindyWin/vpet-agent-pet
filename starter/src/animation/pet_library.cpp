#include "pet_library.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QResource>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <utility>

namespace pet {
namespace {
// A resource folder's entry names in one lookup (QDir looks each entry up again, which costs as much as checking
// every frame by path).
struct Folder : QResource {
    using QResource::QResource;
    using QResource::children;
};
const QRegularExpression &hexDigest() {
    static const QRegularExpression pattern(QRegularExpression::anchoredPattern("[0-9a-f]{64}"));
    return pattern;
}
QByteArray contents(const QString &path, qint64 limit) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.size() <= limit ? file.readAll() : QByteArray();
}
}
PetLibrary::PetLibrary(const QString &index, const QString &mapRoot, const QString &store)
    : index_(index), mapRoot_(mapRoot), store_(store.isEmpty() ? defaultStore() : store) {
    if (index_.isEmpty()) {
        const QDir executable(QCoreApplication::applicationDirPath());
        // Installed bundles keep it under share (Linux) or Resources (macOS application bundle); local builds
        // and Windows put it beside the executable.
        index_ = executable.filePath("artwork.rcc");
        for (const auto *installed : {"../share/agent-pet/artwork.rcc", "../Resources/artwork.rcc"})
            if (QFile::exists(executable.filePath(installed))) { index_ = executable.filePath(installed); break; }
    }
    indexRegistered_ = QResource::registerResource(index_, mapRoot_);
    if (!indexRegistered_) error_ = "Cannot load the pet index " + index_;
}
PetLibrary::~PetLibrary() {
    for (const auto &pack : std::as_const(packs_)) QResource::unregisterResource(pack, mapRoot_);
    if (indexRegistered_) QResource::unregisterResource(index_, mapRoot_);
}
PetLibrary &PetLibrary::shared() {
    static auto *library = new PetLibrary; // Never destroyed: frames stay registered while anything plays.
    return *library;
}
QString PetLibrary::defaultStore() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pets";
}
QVector<PetInfo> PetLibrary::pets() const {
    QVector<PetInfo> found;
    for (const auto &id : QDir(QDir(root()).filePath("assets")).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        if (auto pet = info(id); !pet.id.isEmpty()) {
            for (const auto &pack : missing(id)) pet.download += pack.bytes;
            found.append(pet);
        }
        else if (QFile::exists(QDir(root()).filePath("assets/" + id + "/pet.json")))
            qWarning().noquote() << "Ignoring pet" << id << "with an invalid pet.json";
    }
    return found;
}
PetInfo PetLibrary::info(const QString &id) const {
    if (!validPetId(id)) return {};
    const QDir folder(QDir(root()).filePath("assets/" + id));
    QFile file(folder.filePath("pet.json"));
    if (!file.exists()) return {};
    QJsonParseError parse{};
    QJsonObject object; // Read with value(): operator[] on a non-const object inserts a null member.
    if (file.open(QIODevice::ReadOnly) && file.size() <= 64 * 1024) object = QJsonDocument::fromJson(file.readAll(), &parse).object();
    static const QSet<QString> keys{"schema_version", "id", "name", "author", "url", "terms", "preview"};
    static const QRegularExpression fileName(QRegularExpression::anchoredPattern("[A-Za-z0-9_-][A-Za-z0-9._-]*"));
    PetInfo pet{object.value("id").toString(), object.value("name").toString(), object.value("author").toString(), object.value("url").toString(),
                object.value("terms").toString(), object.value("preview").toString()};
    bool ok = parse.error == QJsonParseError::NoError && object.value("schema_version").toInt() == 1 && pet.id == id
        && !pet.name.isEmpty() && pet.name.size() <= 64 && !pet.author.isEmpty() && pet.author.size() <= 128
        && (!object.contains("url") || (pet.url.startsWith("https://") && pet.url.size() <= 256))
        && fileName.match(pet.terms).hasMatch() && fileName.match(pet.preview).hasMatch() && pet.preview.endsWith(".png");
    for (auto it = object.begin(); ok && it != object.end(); ++it) ok = keys.contains(it.key());
    if (!ok) return {}; // Silent: pets() warns for a listed folder, activate() reports the reason.
    pet.preview = folder.filePath(pet.preview);
    return pet;
}
PetTree PetLibrary::tree(const QString &id) const {
    if (!validPetId(id)) return {};
    const auto object = QJsonDocument::fromJson(contents(QDir(root()).filePath("assets/" + id + "/packs.json"), 1024 * 1024)).object();
    static const QRegularExpression packName(QRegularExpression::anchoredPattern("artwork-[0-9a-f]{64}"));
    auto digest = [](const QJsonValue &value) {
        const auto text = value.toString();
        return text.startsWith("sha256:") && hexDigest().match(text.mid(7)).hasMatch() ? text.mid(7) : QString();
    };
    PetTree tree{digest(object.value("root")), digest(object.value("catalog")), {}};
    const auto packs = object.value("packs").toArray();
    if (tree.root.isEmpty() || tree.catalog.isEmpty() || packs.isEmpty() || packs.size() > 4093) return {};
    // The root must hash this catalog digest and these leaves, so a tree cannot claim packs it was not built from.
    QByteArray text = "agent-pet-pet-tree 1\ncatalog " + tree.catalog.toLatin1() + "\n";
    QSet<QString> names;
    for (const auto &value : packs) {
        const auto entry = value.toObject();
        const PetPack pack{entry.value("name").toString(), entry.value("sequence").toString(), entry.value("sha256").toString(),
                           entry.value("bytes").toInteger()};
        if (!packName.match(pack.name).hasMatch() || names.contains(pack.name) || !hexDigest().match(pack.sha256).hasMatch()
            || pack.bytes < 1 || pack.bytes > maxPackBytes)
            return {};
        names.insert(pack.name);
        tree.packs.append(pack);
        text += pack.name.toLatin1() + " " + pack.sha256.toLatin1() + "\n";
    }
    if (QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex() != tree.root.toLatin1()) return {};
    return tree;
}
QString PetLibrary::stamp(const QString &id) const {
    return QString::fromLatin1(contents(store_ + "/" + id + ".root", 256)).trimmed();
}
QVector<PetPack> PetLibrary::missing(const QString &id) const {
    const auto tree = this->tree(id);
    if (tree.root.isEmpty() || stamp(id) == tree.root) return {};
    const QDir installed(QFileInfo(index_).absolutePath());
    QVector<PetPack> needed;
    for (const auto &pack : tree.packs)
        if (!QFile::exists(installed.filePath(pack.name + ".rcc")) && !QFile::exists(blobPath(pack.sha256))) needed.append(pack);
    return needed;
}
bool PetLibrary::markDownloaded(const QString &id, QString *error) {
    auto fail = [error](const QString &reason) {
        if (error) *error = reason;
        return false;
    };
    const auto tree = this->tree(id);
    if (tree.root.isEmpty()) return fail("Unknown pet or invalid pack list: " + id);
    const QDir installed(QFileInfo(index_).absolutePath());
    for (const auto &pack : tree.packs)
        if (!QFile::exists(installed.filePath(pack.name + ".rcc")) && !QFile::exists(blobPath(pack.sha256)))
            return fail("Pack " + pack.name + " of " + id + " is not downloaded");
    QSaveFile file(store_ + "/" + id + ".root");
    if (!QDir().mkpath(store_) || !file.open(QIODevice::WriteOnly) || file.write(tree.root.toLatin1() + "\n") < 0 || !file.commit())
        return fail("Cannot write to " + store_);
    return true;
}
// Whether the stored file for `pack` has the size and SHA-256 its leaf in the installed index promises.
static bool intactBlob(const QString &path, const PetPack &pack) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() != pack.bytes) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) && hash.result().toHex() == pack.sha256.toLatin1();
}
void PetLibrary::repair(const QString &id) const {
    QFile::remove(store_ + "/" + id + ".root");
    for (const auto &pack : tree(id).packs) {
        const auto path = blobPath(pack.sha256);
        if (QFile::exists(path) && !intactBlob(path, pack)) {
            qWarning().noquote() << "Removing damaged download" << path;
            QFile::remove(path);
        }
    }
}
int PetLibrary::prune() const {
    // Without the index nothing is known to be needed, so nothing is removed.
    if (!error_.isEmpty()) return 0;
    QSet<QString> needed, pets;
    for (const auto &id : QDir(QDir(root()).filePath("assets")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto tree = this->tree(id);
        if (tree.root.isEmpty()) continue;
        pets.insert(id);
        for (const auto &pack : tree.packs) needed.insert(pack.sha256 + ".rcc");
    }
    if (pets.isEmpty()) return 0;
    static const QRegularExpression blob(QRegularExpression::anchoredPattern("[0-9a-f]{64}\\.rcc"));
    static const QRegularExpression partial(QRegularExpression::anchoredPattern("[0-9a-f]{64}\\.rcc\\..+"));
    static const QRegularExpression stampFile(QRegularExpression::anchoredPattern("([a-z0-9-]{1,32})\\.root"));
    int removed = 0;
    QDir store(store_);
    for (const auto &name : store.entryList(QDir::Files | QDir::Hidden)) {
        const auto stamped = stampFile.match(name);
        const bool unused = (blob.match(name).hasMatch() && !needed.contains(name)) || partial.match(name).hasMatch()
            || (stamped.hasMatch() && !pets.contains(stamped.captured(1)));
        if (unused && store.remove(name)) ++removed;
    }
    return removed;
}
bool PetLibrary::activate(const QString &id, QString *error) {
    auto fail = [error](const QString &reason) {
        if (error) *error = reason;
        return false;
    };
    if (!active_.isEmpty()) return fail("Another pet is already active: " + active_);
    if (!error_.isEmpty()) return fail(error_);
    if (info(id).id.isEmpty()) return fail("Unknown pet or invalid pet.json: " + id);
    QString reason;
    const auto catalog = Catalog::load(root(), id, &reason);
    if (!catalog.valid()) return fail(reason);
    if (const auto broken = catalog.contractError(); !broken.isEmpty()) return fail(broken);
    auto rollBack = [&](const QString &why) {
        for (const auto &pack : std::as_const(packs_)) QResource::unregisterResource(pack, mapRoot_);
        packs_.clear();
        return fail(why);
    };
    // A damaged download is removed once nothing maps it (Windows cannot delete a mapped file), so it downloads again.
    auto damaged = [&](const QString &why) {
        rollBack(why);
        repair(id);
        return false;
    };
    // Without a pack list, the pet's frames are inside the index itself. Each pack is installed beside the index,
    // or else downloaded into the store.
    bool stored = false;
    PetTree tree;
    if (QFile::exists(QDir(root()).filePath("assets/" + id + "/packs.json"))) {
        tree = this->tree(id);
        if (tree.root.isEmpty()) return rollBack("Invalid pack list for " + id);
        const QDir installed(QFileInfo(index_).absolutePath());
        for (const auto &pack : std::as_const(tree.packs)) {
            auto path = installed.filePath(pack.name + ".rcc");
            const bool bundled = QFile::exists(path);
            if (!bundled) { path = blobPath(pack.sha256); stored = true; }
            if (!bundled && !QFile::exists(path)) {
                QFile::remove(store_ + "/" + id + ".root"); // No longer complete: the picker offers the download again.
                return rollBack(QString("Pet %1 is not downloaded: pack %2 is missing.").arg(id, pack.name));
            }
            // The store is writable by anyone running as the user: a pack is trusted only once it matches its leaf.
            if (!bundled && !intactBlob(path, pack))
                return damaged(QString("The downloaded pack %1 of %2 is damaged; download the pet again.").arg(pack.name, id));
            if (!QResource::registerResource(path, mapRoot_)) {
                if (bundled) return rollBack("Cannot load the artwork pack " + pack.name + ".rcc. Reinstall Agent Pet to restore it.");
                return damaged(QString("The downloaded pack %1 of %2 is damaged; download the pet again.").arg(pack.name, id));
            }
            packs_.append(path);
        }
    }
    // A damaged or mismatched pack registers but lacks frames, and a pack missing from the list leaves them
    // unregistered: refuse the pet now rather than fail during play. Each lookup searches every registered
    // file, so each frame folder is listed once and its frames are found in that list (about 2 ms for VPet;
    // one lookup per frame cost 18 ms at every start).
    QHash<QString, QSet<QString>> folders;
    for (const auto &frames : std::as_const(catalog.sequences))
        for (const auto &frame : frames) {
            const auto slash = frame.path.lastIndexOf('/');
            const auto path = frame.path.left(slash);
            auto folder = folders.find(path);
            if (folder == folders.end()) {
                const auto children = Folder(path).children();
                folder = folders.insert(path, QSet<QString>(children.begin(), children.end()));
            }
            if (!folder->contains(frame.path.mid(slash + 1))) {
                const auto what = "Missing frame " + frame.path.mid(root().size() + 1) + ". ";
                return stored ? damaged(what + "Download the pet again.") : rollBack(what + "Reinstall Agent Pet to restore it.");
            }
        }
    // Downloaded packs are verified when stored, so finding them all complete is worth one stamp.
    if (stored && stamp(id) != tree.root) markDownloaded(id, nullptr);
    active_ = id;
    catalog_ = catalog;
    return true;
}
}
