#include "pet_library.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QResource>
#include <QSet>
#include <utility>

namespace pet {
PetLibrary::PetLibrary(const QString &index, const QString &mapRoot) : index_(index), mapRoot_(mapRoot) {
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
QVector<PetInfo> PetLibrary::pets() const {
    QVector<PetInfo> found;
    for (const auto &id : QDir(QDir(root()).filePath("assets")).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (const auto pet = info(id); !pet.id.isEmpty()) found.append(pet);
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
    if (!ok) {
        qWarning().noquote() << "Ignoring pet" << id << "with an invalid pet.json";
        return {};
    }
    pet.preview = folder.filePath(pet.preview);
    return pet;
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
    QFile list(QDir(root()).filePath("assets/" + id + "/packs.json"));
    // Without a pack list, the pet's frames are inside the index itself.
    if (list.exists()) {
        auto rollBack = [&](const QString &why) {
            for (const auto &pack : std::as_const(packs_)) QResource::unregisterResource(pack, mapRoot_);
            packs_.clear();
            return fail(why);
        };
        if (!list.open(QIODevice::ReadOnly) || list.size() > 1024 * 1024) return rollBack("Cannot read the pack list of " + id);
        const auto packs = QJsonDocument::fromJson(list.readAll()).object()["packs"].toArray();
        if (packs.isEmpty() || packs.size() > 4093) return rollBack("Invalid pack list for " + id);
        static const QRegularExpression packName(QRegularExpression::anchoredPattern("artwork-[0-9a-f]{64}"));
        const QDir directory(QFileInfo(index_).absolutePath());
        QSet<QString> seen;
        for (const auto &value : packs) {
            const auto name = value.toObject()["name"].toString();
            const auto path = directory.filePath(name + ".rcc");
            if (!packName.match(name).hasMatch() || seen.contains(name) || !QResource::registerResource(path, mapRoot_))
                return rollBack("Cannot load the artwork pack " + name + ".rcc. Reinstall Agent Pet to restore it.");
            seen.insert(name);
            packs_.append(path);
        }
    }
    active_ = id;
    catalog_ = catalog;
    return true;
}
}
