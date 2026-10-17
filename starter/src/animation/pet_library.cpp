#include "pet_library.h"
#include "local_pet.h"
#include "version.h"
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
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
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
PetLibrary::PetLibrary(const QString &index, const QString &mapRoot, const QString &store, const QString &localFolder)
    : index_(index), mapRoot_(mapRoot), store_(store.isEmpty() ? defaultStore() : store),
      localFolder_(localFolder.isEmpty() ? defaultPetFolder() : localFolder) {
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
QString PetLibrary::defaultPetFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/packs/pets";
}
QVector<PetInfo> PetLibrary::pets() const {
    QVector<PetInfo> found;
    for (const auto &id : QDir(QDir(root()).filePath("assets")).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        if (auto pet = resourceInfo(id); !pet.id.isEmpty()) {
            found.append(pet);
        }
        else if (QFile::exists(QDir(root()).filePath("assets/" + id + "/pet.json")))
            qWarning().noquote() << "Ignoring pet" << id << "with an invalid pet.json";
    }
    for (const auto &id : QDir(localFolder_).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        auto pet = localPets::read(QDir(localFolder_).filePath(id));
        if (!resourceInfo(id).id.isEmpty()) {
            pet.error = "This id belongs to a pet in the app index; choose a different id.";
            pet.preview.clear();
        }
        found.append(pet);
    }
    return found;
}
PetInfo PetLibrary::info(const QString &id) const {
    if (auto pet = resourceInfo(id); !pet.id.isEmpty()) return pet;
    if (!validPetId(id) || !QFileInfo::exists(QDir(localFolder_).filePath(id))) return {};
    return localPets::read(QDir(localFolder_).filePath(id));
}
QString PetLibrary::terms(const QString &id) const {
    const auto pet = info(id);
    return pet.id.isEmpty() || !pet.error.isEmpty() ? QString() : terms(pet);
}
QString PetLibrary::terms(const PetInfo &pet) const {
    if (pet.id.isEmpty() || !pet.error.isEmpty()) return {};
    return QString::fromUtf8(contents(pet.folder.isEmpty() ? ":/licenses/" + pet.terms : pet.terms, 1024 * 1024));
}
PetInfo PetLibrary::resourceInfo(const QString &id) const {
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
bool PetLibrary::retiredPet(const QString &id) const {
    static const QSet<QString> retired{"fat-cat", "whale-girl", "kuro", "yun", "bocchi"};
    if (!retired.contains(id)) return false;
    const auto replacement = info(id);
    return replacement.folder.isEmpty() || !replacement.error.isEmpty();
}
int PetLibrary::removeLegacyDownloads() const {
    if (QFileInfo(store_).isSymLink()) return 0;
    static const QRegularExpression legacy(QRegularExpression::anchoredPattern(
        "(?:[0-9a-f]{64}\\.rcc(?:\\..+)?|[a-z0-9-]{1,32}\\.root)"));
    QDir folder(store_);
    int removed = 0;
    for (const auto &file : folder.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks))
        if (legacy.match(file.fileName()).hasMatch() && folder.remove(file.fileName())) ++removed;
    return removed;
}
bool PetLibrary::activate(const QString &id, QString *error) {
    auto fail = [error](const QString &reason) {
        if (error) *error = reason;
        return false;
    };
    if (!active_.isEmpty()) return fail("Another pet is already active: " + active_);
    if (!validPetId(id)) return fail("Invalid pet identifier: " + id);
    const bool local = resourceInfo(id).id.isEmpty() && QFileInfo::exists(QDir(localFolder_).filePath(id));
    QString reason;
    CatalogSource source;
    if (local) {
        const auto pet = localPets::read(QDir(localFolder_).filePath(id), &source);
        if (!pet.error.isEmpty()) return fail(pet.error);
    } else {
        if (!error_.isEmpty()) return fail(error_);
        if (resourceInfo(id).id.isEmpty()) return fail("Unknown pet or invalid pet.json: " + id);
        source = Catalog::read(root(), id, &reason);
    }
    if (!source.valid()) return fail(reason);
    auto catalog = Catalog::build(source, &reason);
    if (!catalog.valid()) return fail(reason);
    if (const auto broken = catalog.contractError(); !broken.isEmpty()) return fail(broken);
    auto rollBack = [&](const QString &why) {
        for (const auto &pack : std::as_const(packs_)) QResource::unregisterResource(pack, mapRoot_);
        packs_.clear();
        return fail(why);
    };
    if (!local) {
        // Sequence resources are installed beside the index; local pets use plain PNGs.
        PetTree tree;
        if (QFile::exists(QDir(root()).filePath("assets/" + id + "/packs.json"))) {
            tree = this->tree(id);
            if (tree.root.isEmpty()) return rollBack("Invalid pack list for " + id);
            const QDir installed(QFileInfo(index_).absolutePath());
            for (const auto &pack : std::as_const(tree.packs)) {
                auto path = installed.filePath(pack.name + ".rcc");
                if (!QResource::registerResource(path, mapRoot_))
                    return rollBack("Cannot load the artwork pack " + pack.name + ".rcc. Reinstall Agent Pet to restore it.");
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
                    auto relative = frame.path.mid(root().size());
                    if (relative.startsWith('/')) relative.remove(0, 1); // root() ends in a slash at "/" but not at other mounts.
                    const auto what = "Missing frame " + relative + ". ";
                    return rollBack(what + "Reinstall Agent Pet to restore it.");
                }
            }
    }
    // Packs check their own frames, which are files rather than resources.
    if (!pluginFolder_.isEmpty()) {
        loadedPlugins_ = plugins::scan(pluginFolder_, AGENT_PET_VERSION);
        rules_ = {};
        plugins::apply(source, id, enabledPlugins_, loadedPlugins_, &rules_);
        if (std::any_of(loadedPlugins_.begin(), loadedPlugins_.end(), [](const PluginPack &pack) { return pack.status == PluginPack::Applied; }))
            catalog = Catalog::build(source, nullptr);
    }
    active_ = id;
    catalog_ = catalog;
    return true;
}
void PetLibrary::setPlugins(const QString &folder, const QStringList &enabled) {
    pluginFolder_ = folder.isEmpty() ? plugins::defaultFolder() : folder;
    enabledPlugins_ = enabled;
}
QVector<PluginPack> PetLibrary::plugins() const {
    auto packs = plugins::scan(pluginFolder(), AGENT_PET_VERSION);
    for (auto &pack : packs)
        for (const auto &loaded : loadedPlugins_)
            if (loaded.id == pack.id && pack.status != PluginPack::Invalid
                && (loaded.status == PluginPack::Applied || loaded.status == PluginPack::Rejected || loaded.status == PluginPack::OtherPet)) {
                pack.status = loaded.status;
                pack.error = loaded.error;
            }
    return packs;
}
}
