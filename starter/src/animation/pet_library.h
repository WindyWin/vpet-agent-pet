#pragma once
#include "catalog.h"
#include "plugins.h"
#include <QString>
#include <QStringList>
#include <QVector>

namespace pet {
// VPet in the installed index, or a validated local pet. Preview is a resource or disk path.
struct PetInfo { QString id, name, author, url, terms, preview;
    QString folder, license, version, error; // Nonempty folder: a local pack; error: listed but unusable.
};
// One installed sequence resource in VPet's hash tree (docs/pets.md).
struct PetPack { QString name, sequence, sha256; qint64 bytes = 0; };
struct PetTree { QString root, catalog; QVector<PetPack> packs; }; // `root` and `catalog` without "sha256:".

// The known pets (docs/pets.md): an index (artwork.rcc) holding each pet's pet.json, catalog, preview and hash
// tree, beside the installed sequence packs (artwork-<sha256>.rcc). The store argument is used only to
// clean up legacy downloads. Listing pets registers only the index; activate() registers its packs. Resource
// registration is process-wide, so the app uses shared(); tests mount fixture indexes at other `mapRoot`s.
class PetLibrary {
public:
    static constexpr qint64 maxPackBytes = 64LL * 1024 * 1024; // A larger pack in a tree makes the tree invalid.
    // With no `index`, finds artwork.rcc beside the executable, in ../share/agent-pet or in ../Resources. With no
    // `store`, uses defaultStore(); with no `localFolder`, uses defaultPetFolder().
    explicit PetLibrary(const QString &index = {}, const QString &mapRoot = "/", const QString &store = {},
                        const QString &localFolder = {});
    ~PetLibrary(); // Unregisters the index and any packs.
    PetLibrary(const PetLibrary &) = delete;
    PetLibrary &operator=(const PetLibrary &) = delete;
    static PetLibrary &shared(); // The installed pets at ":/", kept until the process exits.
    int removeLegacyDownloads() const; // Only legacy blobs/stamps, never installed packs.
    bool retiredPet(const QString &id) const; // A former built-in pet without a valid local replacement.
    static QString defaultStore(); // "pets" in the per-user application data folder.
    static QString defaultPetFolder(); // "packs/pets" in the per-user data directory.
    QString petFolder() const { return localFolder_; }
    QString terms(const QString &id) const; // Bounded plain text; resolves info using the current display snapshot.
    QString terms(const PetInfo &pet) const; // Reads bounded terms using previously checked display metadata.
    QString error() const { return error_; } // Why the index is unavailable; empty when it loaded.
    // Read bounded local display metadata before opening any windows (up to 256 folders). This snapshot is refreshed explicitly
    // (normally at startup); activate() always validates disk contents independently.
    void refreshDisplaySnapshot();
    QVector<PetInfo> pets() const; // Indexed pets and local display metadata; full validation belongs to activate().
    PetInfo info(const QString &id) const; // An empty `id` when there is no such valid pet.
    PetTree tree(const QString &id) const; // From the index; empty `root` when missing or invalid.
    // Validates the pet's catalog, including the core state contract, then registers its packs. On failure
    // nothing it tried stays registered and `error` says why, in English. A library activates one pet at most.
    // Enabled plugin packs for the pet are merged in last; a pack that fails is left out and never fails the pet.
    bool activate(const QString &id, QString *error);
    // Plugin packs (plugins.h) that activate() merges into the pet: those in `enabled`, from `folder` (empty: the
    // default). Without this call no pack loads.
    void setPlugins(const QString &folder, const QStringList &enabled);
    QString pluginFolder() const { return pluginFolder_.isEmpty() ? plugins::defaultFolder() : pluginFolder_; }
    // The packs in the plugin folder now, each with what activation made of it: one added since is Off (or Invalid).
    QVector<PluginPack> plugins() const;
    // The reaction rules (events.json) of the packs activate() applied, in pack id order; empty without any.
    const EventRules &eventRules() const { return rules_; }
    QString active() const { return active_; }
    const Catalog &catalog() const { return catalog_; } // The active pet's; empty before activate().
    QString root() const { return ":" + mapRoot_; } // Pets are at <root>/assets/<id>/.
private:
    QVector<PetInfo> readLocalPets(bool displayOnly = false) const;
    PetInfo resourceInfo(const QString &id) const;
    QString index_, mapRoot_, store_, error_, active_, pluginFolder_, localFolder_;
    QStringList enabledPlugins_;
    QVector<PluginPack> loadedPlugins_; // As activate() found them.
    Catalog catalog_;
    EventRules rules_;
    QStringList packs_; // Registered pack files.
    QVector<PetInfo> displaySnapshot_;
    bool hasDisplaySnapshot_ = false;
    bool indexRegistered_ = false;
};
}
