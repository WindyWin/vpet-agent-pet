# 0023. Arch Linux package (AUR)

- Status: Accepted
- Date: 2026-10-07

## Context

Arch has no store: third-party software is shared as a `PKGBUILD` in the AUR, which
users build with `makepkg` or an AUR helper. Our portable tarball bundles Qt and
installs into the user's home with its own updater, which is the wrong shape for a
system package. Arch builds against its own Qt and wants pacman to own files and
upgrades.

GitHub's generated source tarball for a tag holds the whole repository, including the
749 MB upstream artwork bundle at the root that the app never reads. A `PKGBUILD`
fetching it would download about 940 MB for a 155 MB application.

## Decision

`packaging/arch/PKGBUILD` builds from source with the system Qt, using the existing
CMake `install()` rules, into `/usr`.

- **Source.** The release workflow attaches `agent-pet-<version>-source.tar.gz`, made
  with `git archive` of `starter/` alone, to the draft release. The `PKGBUILD` fetches
  that, not GitHub's `/archive` tarball.
- **Cleanup in `package()`.** The portable installer scripts (`install.sh`,
  `uninstall.sh`, `INSTALL.txt`, `tui.sh`, `dialogrc`) are removed, and the desktop
  entry is generated with `Exec=agent-pet` into `/usr/share/applications`.
- **Updater.** The in-app updater only acts on a prefix holding an
  `.agent-pet-install` receipt, which only `install.sh` writes. A pacman install has
  none, so the app behaves like a development build: it still checks for releases and
  shows the notification and manual download, but never installs, and upgrades stay
  with pacman. Nothing in the code changes.
- **Licences.** `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md` and `licenses/` go to
  `/usr/share/licenses/agent-pet`; the artwork is `LicenseRef-VPet-artwork`.
- **Publishing.** The AUR package `agent-pet` is a git repository holding `PKGBUILD`
  and `.SRCINFO`. Each release bumps `pkgver`, runs `updpkgsums`, regenerates
  `.SRCINFO` and pushes. The `PKGBUILD` here is the reference copy.

## Consequences

- Arch users get native Qt, pacman upgrades and a clean removal.
- Each release grows by one 155 MB asset, and the AUR update stays a manual step until
  automated.
- Tests run in `check()`, so a Qt update that breaks them shows up at build time.

## Validation

### Local build evidence — 2026-10-07

`makepkg` on Arch Linux built a `git archive` of `starter/` (version 0.12.0) against
the system Qt. `check()` ran CTest offscreen: 12 of 12 passed. The package holds
`agent-pet` and `agent-pet-updater` in `/usr/bin`, the desktop entry, the icon, the
artwork under `/usr/share/agent-pet` and the licences, with no installer scripts or
receipt. Open: `namcap` was not installed on the build host, a clean-chroot build, and
the first push to the AUR.
