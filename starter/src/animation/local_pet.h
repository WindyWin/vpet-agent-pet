#pragma once
#include "pet_library.h"

namespace pet::localPets {
inline constexpr int maxFrames = 10000;
inline constexpr qint64 maxPngBytes = 512LL * 1024 * 1024;
inline constexpr qint64 maxTotalBytes = 544LL * 1024 * 1024;
inline constexpr int maxEntries = 20000;
// Validates the complete unpacked directory, never follows links. Failures retain an identity for Settings.
PetInfo read(const QString &folder, CatalogSource *source = nullptr);
}
