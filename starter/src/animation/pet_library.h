#pragma once
#include "catalog.h"
#include <QString>
#include <QStringList>
#include <QVector>

namespace pet {
// A pet in the index, from its pet.json. `preview` is a resource path to the Settings tile. `download` is how many
// bytes of its packs are neither installed nor downloaded yet (pets() fills it in; 0 for a pet ready to run).
struct PetInfo { QString id, name, author, url, terms, preview; qint64 download = 0; };
// One sequence pack in a pet's hash tree (docs/pets.md): installed as <name>.rcc beside the index, or downloaded
// as <sha256>.rcc into the store.
struct PetPack { QString name, sequence, sha256; qint64 bytes = 0; };
struct PetTree { QString root, catalog; QVector<PetPack> packs; }; // `root` and `catalog` without "sha256:".

// The known pets (docs/pets.md): an index (artwork.rcc) holding each pet's pet.json, catalog, preview and hash
// tree, beside the installed sequence packs (artwork-<sha256>.rcc). Pets that are not bundled download their packs
// into a per-user store (PetDownloader), named by content: <sha256>.rcc, and <id>.root holding the tree root last
// found complete there. Listing pets registers only the index; activate() registers one pet's packs. Resource
// registration is process-wide, so the app uses shared(); tests mount fixture indexes at other `mapRoot`s.
class PetLibrary {
public:
    static constexpr qint64 maxPackBytes = 64LL * 1024 * 1024; // A larger pack in a tree makes the tree invalid.
    // With no `index`, finds artwork.rcc beside the executable, in ../share/agent-pet or in ../Resources. With no
    // `store`, uses defaultStore().
    explicit PetLibrary(const QString &index = {}, const QString &mapRoot = "/", const QString &store = {});
    ~PetLibrary(); // Unregisters the index and any packs.
    PetLibrary(const PetLibrary &) = delete;
    PetLibrary &operator=(const PetLibrary &) = delete;
    static PetLibrary &shared(); // The installed pets at ":/", kept until the process exits.
    static QString defaultStore(); // "pets" in the per-user application data folder.
    QString store() const { return store_; }
    QString blobPath(const QString &sha256) const { return store_ + "/" + sha256 + ".rcc"; }
    QString error() const { return error_; } // Why the index is unavailable; empty when it loaded.
    QVector<PetInfo> pets() const; // Pets with a valid pet.json, by id. Reads metadata only.
    PetInfo info(const QString &id) const; // An empty `id` when there is no such valid pet.
    PetTree tree(const QString &id) const; // From the index; empty `root` when missing or invalid.
    // The packs still to download: neither installed nor in the store. Empty at once when the store's stamp
    // matches the tree root; otherwise each pack's file is looked for. Also empty for an unknown pet.
    QVector<PetPack> missing(const QString &id) const;
    // Every pack is now installed or in the store: stamps the tree root, so later checks are one comparison.
    bool markDownloaded(const QString &id, QString *error);
    // Removes store files no pet in the index needs (old packs, stamps of unknown pets, unfinished downloads).
    // Returns how many were removed.
    int prune() const;
    // Validates the pet's catalog, including the core state contract, then registers its packs. On failure
    // nothing it tried stays registered and `error` says why, in English. A library activates one pet at most.
    bool activate(const QString &id, QString *error);
    QString active() const { return active_; }
    const Catalog &catalog() const { return catalog_; } // The active pet's; empty before activate().
    QString root() const { return ":" + mapRoot_; } // Pets are at <root>/assets/<id>/.
private:
    QString stamp(const QString &id) const;
    // Re-hashes the pet's packs in the store, removing any that do not match, and drops its stamp.
    void repair(const QString &id) const;
    QString index_, mapRoot_, store_, error_, active_;
    Catalog catalog_;
    QStringList packs_; // Registered pack files.
    bool indexRegistered_ = false;
};
}
