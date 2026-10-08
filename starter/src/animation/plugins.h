#pragma once
#include "catalog.h"
#include <QString>
#include <QStringList>
#include <QVector>

namespace pet {
// A plugin pack (docs/plugins.md): a folder <plugins>/<id>/ holding plugin.json, an animations.json catalog fragment
// and the PNG frames it names. Packs are data only; an enabled pack's fragment is merged into its pet's catalog at
// startup, and its frames load from the folder.
struct PluginPack {
    // Invalid: plugin.json cannot be used. Off: not enabled. OtherPet: enabled, for a pet that is not running.
    // Applied: merged into the running pet. Rejected: enabled, but its fragment or frames failed validation.
    enum Status { Invalid, Off, OtherPet, Applied, Rejected };
    QString id, name, version, author, license, url, pet, minApp, folder;
    Status status = Off;
    QString error; // Why, in English, for Invalid and Rejected.
};
namespace plugins {
inline constexpr int maxPacks = 32; // Folders beyond this many, and enabled ids beyond it, are ignored.
inline constexpr int maxFrames = 2000; // Per pack.
inline constexpr qint64 maxFrameBytes = 4LL * 1024 * 1024;
inline constexpr qint64 maxPackBytes = 64LL * 1024 * 1024; // All of a pack's frames together.
inline constexpr int maxFrameSide = 2048; // Pixels, either way.
QString defaultFolder(); // "plugins" in the per-user application data folder.
// Every pack folder under `folder`, by id, with status Off or Invalid. `appVersion` is checked against a pack's
// min_app_version.
QVector<PluginPack> scan(const QString &folder, const QString &appVersion);
// Merges each pack whose id is in `enabled` and whose pet is `pet` into `source`, in id order. A pack is kept only
// when the merged catalog still builds and meets the cue contract; otherwise `source` stays as it was and the pack
// is Rejected. Sets every pack's status.
void apply(CatalogSource &source, const QString &pet, const QStringList &enabled, QVector<PluginPack> &packs);
}
}
