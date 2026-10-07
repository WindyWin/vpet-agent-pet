# 0027. Pet packs

- Status: Accepted
- Date: 2026-10-07

## Context

VPet was the only pet, hard-wired into the player, the build rules and the asset scripts (#62).
Contributors could not add a character without C++ and CMake edits, and every sequence pack was
registered at startup. Installed 0.13.x updaters validate the next release's components manifest
themselves and accept only `artwork.rcc` plus flat `artwork-<sha256>.rcc` packs, whose names hash the
frame folder's resource path ([0010](0010-application-updates.md), [0014](0014-artwork-resource-file.md)).

## Decision

- A pet is a folder `assets/<id>/` with `pet.json`, `animations.json`, `preview.png`, `manifest.json`
  and frame folders. Its art terms are in `licenses/`. VPet stays at `assets/vpet/`, so its 136 packs
  keep their names and bytes.
- `add_pets()` (`cmake/pets.cmake`) builds every pet:
  - one pack per frame folder, built exactly as before;
  - `pets/<id>/packs.json`, the pet's pack list and hash tree (a root over a catalog digest and one
    leaf per pack), written by `cmake/pet_tree.cmake`;
  - the index `artwork.rcc`, which holds each pet's metadata, catalog, preview and pack list
    (`assets/<id>/packs.json`).
- `Catalog` parses and validates a catalog. `PetLibrary` registers the index, lists pets from metadata
  only, and on `activate(id)` validates that pet and registers only its packs, rolling back on failure.
  `Player` plays a `Catalog`.
- The app selects 15 states by name. They are listed once, in `src/animation/core-states.json`, which
  is embedded into the build. Every pet must define them with the listed playback. A pet that does not
  is refused at activation, and the `pets` test suite checks every bundled pet. Runtime fallback
  chains were rejected: `PetWindow` and `Monitor` compare requested state names, so substituting
  states would make `requestedState()` disagree with what was asked.
- Users choose the pet from Settings → Pet → Character (shown with two or more pets), or with
  `--pet <id>` for one run. The choice applies at the next start. An unavailable pet logs one line,
  runs VPet and keeps the saved choice.

## Consequences

- A new pet needs no code changes, and `scripts/new_pet.py` scaffolds one that runs at once. Users
  see nothing new until a second pet ships.
- Only the running pet's packs are registered. Previews come from the index.
- `artwork.rcc` ships and updates with each release, so the index format may change in any
  release. The catalog format may too, given a migration of the catalogs in the repository. The
  planned event refactor (#64) will replace `core-states.json` that way.
- Every install still carries every bundled pet's packs. On-demand download of non-bundled pets, using
  the hash tree, is part 2 of #62.

## Validation

### Pet packs evidence — 2026-10-07

- Arch Linux, Qt 6.11.2, CMake 4.4: `ctest --test-dir build` passed all 14 tests, including `pets`
  (catalog contract, library listing and activation, rollback, a broken pet refused, every core state
  of a minimal pet, hash trees against the built files, the `pet` preference, the picker, About) and
  `pet-scaffold` (`new_pet.py`, `add_sequences.py --pet`, `verify_assets.py`). `verify_assets.py`
  reported `OK: vpet (141 sequences).` and `check_translations.py` exited 0.
- VPet's 136 sequence packs were byte-identical before and after the build change: the SHA-256 of
  every `build/artwork-<hex>.rcc` was compared (Task 3 of
  `docs/superpowers/plans/2026-10-07-pet-packs.md`). The built index lists 136 packs for VPet.
- `agent-pet --smoke-test --pet nosuch` logged `Pet "nosuch" is unavailable: Unknown pet or invalid
  pet.json: nosuch; using vpet` and kept running as VPet until the 5 s timeout ended it.
- No `.cpp` or `.h` file under `src` names `assets/vpet` any more; `"vpet"` remains only as the default id.
- Open: macOS and Windows CI runs of the new suites; a second real pet.
