# 0030. On-demand pets

- Status: Accepted
- Date: 2026-10-08

## Context

[0028](0028-pet-packs.md) made pets folders under `assets/` and gave each a hash tree in the index (a
root over a catalog digest and one leaf per sequence pack), but every install still carried every
pet's packs (#62, part 2). More characters would grow every download and update for everyone. The
in-place updater only runs on Linux, and package-manager installs under `/usr` cannot be written.

## Decision

- Only the pets in `AGENT_PET_BUNDLED_PETS` (default `vpet`) install their packs and appear in
  `artwork-packs.json`. The index still lists every pet with its preview and tree, so the picker can
  offer them. VPet's packs, names and bytes are unchanged, and the updater is untouched.
- Downloaded packs go into a per-user, content-addressed store: `pets/<sha256>.rcc` in the
  application data folder, writable on every platform and install type. `PetLibrary` takes each pack
  from the install when present, else from the store, so a build folder (which holds every pack) runs
  any pet.
- `<id>.root` in the store holds the tree root last found complete. Equal to the index root, the pet
  is ready without looking at its files. Otherwise only the leaves absent from both places download.
  A complete activation from the store writes the stamp.
- `PetDownloader` fetches the missing leaves one at a time from the long-lived `pets` release
  (`releases/download/pets/<sha256>.rcc`). Bytes stream into a `QSaveFile` while being hashed; the
  file enters the store only when its size and SHA-256 match the leaf from the installed index, which
  the user already trusts. A mismatch is fetched once more, then the download fails. Network rules
  follow the updater: TLS verification, the leaf's size as the limit (and 64 MiB per pack in any tree),
  a 30 s idle timeout and 10 min per pack, and redirects only over HTTPS to `github.com`,
  `objects.githubusercontent.com` or `release-assets.githubusercontent.com`.
- The Settings picker shows a pet that still needs packs with ⬇ and its size in the tooltip.
  Choosing it starts the download, with a progress bar and Cancel, while the tiles wait. Once complete,
  the pet becomes the saved choice for the next start. A failure or cancel keeps the current choice and
  says why. The window owns the downloader, so closing Settings does not stop it.
- A downloaded pack found missing or damaged when a pet activates (it does not register, or frames are
  missing) is re-hashed and removed if wrong, the stamp goes, VPet runs, and the picker offers the
  download again.
- At startup, store files that no pet in the index references are removed: old packs, unfinished
  downloads and stamps of unknown pets. Without a loadable index nothing is removed.
- The release workflow runs `scripts/pet_blobs.py`, which copies each pack that is not bundled to
  `<sha256>.rcc` after checking it against its tree, and uploads only the blobs the `pets` release
  lacks. That release is a prerelease that is never "latest", so update checks never see it.

## Consequences

- New characters cost nothing to users who keep VPet. A pet's art is uploaded once per new content,
  never again with each app release.
- On-demand pets work on Linux, macOS and Windows, independently of the in-place updater.
- Choosing a pet still applies at the next start; live switching remains future work.
- A pet removed from a later index loses its downloaded packs at the next start.
- The `pets` release must keep every blob some shipped index references; deleting one breaks the
  download of that pet for those versions.

## Validation

### On-demand pets evidence — 2026-10-08

- Ubuntu 24.04, Qt 6.4.2 (locally relaxed version check, as in [0029](0029-cues.md)), GCC 13:
  `ctest --test-dir build` passed all 14 tests. `pets` adds: a fixture build without `duo`'s packs
  lists their size, refuses to activate, then activates from the store and stamps the root; with the
  stamp a later check is one comparison, and a pack removed afterwards drops the stamp at activation;
  a damaged blob is removed while the good one stays; cleanup keeps exactly what the index needs;
  the downloader, over a fake `pets` release, fetches only the missing leaf, retries a tampered blob
  once, rejects it twice, reports offline and cancel with nothing stored, and finishes at once for a
  complete pet; the picker requests a download, shows progress and Cancel, and makes the pet the
  choice only once complete.
- `scripts/pet_blobs.py` on the fixture build with `duo` treated as not bundled copied its two packs
  as `<sha256>.rcc`, matching their digests; on the app build it found nothing to upload.
- `agent-pet --smoke-test` (offscreen) passed with startup cleanup enabled.
- Open: the first real upload to the `pets` release (no on-demand pet shipped yet; [0036](0036-fat-cat-pet.md) adds the first), macOS and Windows CI
  runs, and a manual check against GitHub's redirect to its asset host.
