#pragma once
#include "catalog.h"
#include <QString>
#include <QStringList>
#include <QVector>

namespace pet {
// A pet in the index, from its pet.json. `preview` is a resource path to the Settings tile.
struct PetInfo { QString id, name, author, url, terms, preview; };

// The installed pets (docs/pets.md): an index (artwork.rcc) holding each pet's pet.json, catalog, preview
// and pack list, beside its sequence packs (artwork-<sha256>.rcc). Listing pets registers only the index;
// activate() registers one pet's packs. Resource registration is process-wide, so the app uses shared();
// tests mount fixture indexes at other `mapRoot`s.
class PetLibrary {
public:
    // With no `index`, finds artwork.rcc beside the executable, in ../share/agent-pet or in ../Resources.
    explicit PetLibrary(const QString &index = {}, const QString &mapRoot = "/");
    ~PetLibrary(); // Unregisters the index and any packs.
    PetLibrary(const PetLibrary &) = delete;
    PetLibrary &operator=(const PetLibrary &) = delete;
    static PetLibrary &shared(); // The installed pets at ":/", kept until the process exits.
    QString error() const { return error_; } // Why the index is unavailable; empty when it loaded.
    QVector<PetInfo> pets() const; // Pets with a valid pet.json, by id. Reads metadata only.
    PetInfo info(const QString &id) const; // An empty `id` when there is no such valid pet.
    // Validates the pet's catalog, including the core state contract, then registers its packs. On failure
    // nothing it tried stays registered and `error` says why, in English. A library activates one pet at most.
    bool activate(const QString &id, QString *error);
    QString active() const { return active_; }
    const Catalog &catalog() const { return catalog_; } // The active pet's; empty before activate().
    QString root() const { return ":" + mapRoot_; } // Pets are at <root>/assets/<id>/.
private:
    QString index_, mapRoot_, error_, active_;
    Catalog catalog_;
    QStringList packs_; // Registered pack files.
    bool indexRegistered_ = false;
};
}
