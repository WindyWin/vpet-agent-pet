# 0014. Artwork in a separate resource file

- Status: Accepted
- Date: 2026-10-05

## Context

Artwork was compiled into the executable ([0009](0009-linux-packaging.md)), so every
code change produced a new executable that carried all of it again, and component
updates ([0010](0010-application-updates.md)) could not reuse unchanged artwork.

## Decision

The artwork pack and catalog are built as a separate binary Qt resource file,
`share/agent-pet/artwork.rcc` (beside the executable in local builds). The player
registers it using [QResource](https://doc.qt.io/qt-6/qresource.html), preserving
the existing resource paths. Artwork terms and application notices remain embedded.
Move the whole bundle together. The RCC uses format 1 without compression to omit
source timestamps and keep identical artwork reproducible across checkouts.

## Consequences

- Installs and packages must keep `artwork.rcc` and the sequence packs beside the
  executable.
- Identical artwork builds identical resource files, so updates reuse them.

## Validation

### Registration moved to the pet library — 2026-10-08

The player no longer registers `artwork.rcc`. `PetLibrary` registers the index and only the active
pet's packs, so the decision above (a separate resource file that keeps resource paths and lets
updates reuse unchanged artwork) still holds. See [0028](0028-pet-packs.md).
