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


## Follow-up: content-addressed release components (2026-10-19, issue #100)

Format 3 manifests retain version-local app archives but pin shared runtime and
artwork archives to `release-blobs/sha256-<archive-digest>.tar.gz`. The release is a
published prerelease created with `--latest=false`. URL validation accepts only
the exact official repository, matching version tag or shared digest filename;
manifest, archive and installed-file digests remain mandatory. Installed artwork
RCC names stay unchanged. Runtime archives exclude only the release-specific
Qt `SOURCES.txt`, now carried at its original installed path by the app component.

`check_release_blobs.py` demonstrates zero shared uploads for a second release
with changed app/source notice, and exactly one upload for a changed artwork pack.
It also checks corrupt metadata, pagination, the 900/1000-asset limits, retention
of drafts and recent releases, and the grace period. Updater tests cover strict
shared URLs and downloads; `check_update.py` covers component reuse and rollback.
The full Linux tarball remains available for old readers that reject format 3;
CI runs `check_install.py` against the previous two published stable versions.

Pruning defaults to dry-run, retains three stable releases plus all drafts and
prereleases, and only deletes unreferenced content older than 14 days. Publication
and pruning share a workflow concurrency group. macOS retains the ZIP required
by existing update/install consumers; Windows retains Setup. Redundant DMG and
portable ZIP builds remain opt-in local packaging options. The optional AUR
source/artwork tarball split is deferred.
