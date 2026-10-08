# Pet packs (Part 1) — design

Date: 2026-10-07. Status: approved in conversation; ADR 0028 records the decision. Updated 2026-10-08 after
merging main, which took ADR 0027 and stopped playing a `waiting` animation, so `waiting` left the contract.
Tracking: issue #62 (Part 1 of 2; Part 2 is on-demand download of non-bundled pets).

## Intent

A contributor can add a **whole new pet character** (its own art, catalog and terms) as
a folder in the repository, with no C++ or CMake edits. Users choose the pet in Settings
from **preview-image tiles**; only the chosen pet's artwork is loaded. VPet becomes the
first pack and plays exactly as today.

Scope is **format only**: no second pet ships to users. Test fixtures prove that a
non-VPet pack builds, loads and plays.

Success:

1. A new pet is a folder `assets/<id>/` plus its terms file in `licenses/`; a rebuild
   makes it selectable.
2. Only the active pet's sequence packs are registered as resources.
3. A minimal pet (one sequence reused by every core state) runs every app state.
4. VPet plays as before; all existing suites pass; an update from 0.13.x re-downloads
   no VPet sequence pack.
5. The index carries a per-pet hash tree (root and one leaf per sequence pack) that a
   test proves matches the built files.
6. `starter/docs/pets.md` walks a contributor through a minimal pet end to end.

## Constraints

- **Installed versions must keep updating.** 0.13.x validates the next release's
  components manifest with its own code (`src/updates/components.cpp`), which accepts
  only `artwork.rcc` plus flat `artwork-<64 hex>.rcc` packs at fixed paths. The install
  layout and pack naming stay exactly as they are.
- **VPet stays byte-identical.** A pack's name is the SHA-256 of its source directory
  path (`assets/vpet/vup/Default/Nomal/1`), and its bytes contain each frame's resource
  path. VPet's files therefore stay at `assets/vpet/`, and its 136 packs (one per frame folder) keep
  their names and bytes. Only `artwork.rcc` (the index, ~200 KB) changes.
- The index lives in `artwork.rcc`, which always ships and updates together with the
  binary. Its format belongs to one version, so it can change freely between versions.
- `hook`, protocol v1, sessions, alerts and the updater are untouched.
- `preferences.json` is re-read before every save. New interface strings use `tr()` and
  get Vietnamese translations; logs and CLI output stay plain English.

## Decisions changed during design (versus the first text of #62)

- **Required core states instead of runtime fallback chains.** `select()` of an unknown
  state is an error today, and `PetWindow`/`Monitor` compare requested state names
  (`"closing"`, `"idle"`) to drive quitting, walking and edge hiding. Resolving names at
  runtime would make `requestedState()` disagree with what was asked. Instead the
  catalog must define every core state, which is cheap because states may share
  sequences, and a scaffold script generates a minimal catalog.
- **Restart to apply.** Choosing a pet saves the preference; the pet changes on the next
  start. `Player` never swaps catalogs at runtime.

## Pack format

```text
assets/<id>/
  pet.json          identity and credits
  animations.json   schema-v1 catalog, unchanged format
  preview.png       picker tile
  manifest.json     frame files with size and SHA-256 (for verify_assets.py)
  <subfolders>/…    PNG frame sequences; durations in file names as today
licenses/<file>     the pet's art terms, beside the existing notices
```

VPet keeps `vup/`, `manifest.json` and `available-animations.json`, and gains
`pet.json` and `preview.png`. `available-animations.json` stays VPet-only (the list of
archive sequences not yet bundled).

`pet.json`:

```json
{
  "schema_version": 1,
  "id": "vpet",
  "name": "VUP",
  "author": "VUP-Simulator team",
  "terms": "VPET-ARTWORK-TERMS.md",
  "preview": "preview.png"
}
```

- `id`: `[a-z0-9-]{1,32}`, equal to the folder name.
- `name`: 1–64 characters, shown as-is (a proper noun; not translated).
- `author`: 1–128 characters.
- `terms`: a file name (`[A-Za-z0-9._-]+`) in the top-level `licenses/` folder.
  Packaging, the AUR `PKGBUILD`, the installers and the About dialog already carry
  every file there, so a new pet needs no packaging change.
- `preview`: a PNG at the pet folder's root, at most 512 × 512 px (checked by
  `verify_assets.py`; the picker scales it anyway).
- Unknown keys are rejected by `verify_assets.py` and by `PetLibrary`, so typos surface.

Frames live only in subfolders of `assets/<id>/`; the pet folder's root holds metadata
and the preview, so the preview never becomes a sequence pack. The catalog's existing
`asset_root` (VPet: `assets/vpet/vup`) must lie inside `assets/<id>/`, and every frame
path must start with `asset_root + "/"`. That replaces today's hard-coded
`assets/vpet/vup/` check.

### Core state contract

The app selects these states by name, so every catalog must define them with this
playback shape (VPet's current shape). The contract is **one data file**,
`src/animation/core-states.json`, the single source for `Catalog`, `new_pet.py` and the
contributor guide; CMake embeds it into `pet_animation` as a generated header, so
`Catalog` needs no resource registration to read it:

```json
{
  "schema_version": 1,
  "states": {
    "idle": { "mode": "loop", "after": "idle" },
    "starting": { "mode": "once", "after": "idle" },
    "thinking": { "mode": "phased", "after": "idle" }
  }
}
```

(abridged; the file lists every row of the table below). It is the only place where
the event side and pets meet, and it is provisional until the planned event refactor
(see "Future: event refactor").

| State | Mode | `after` |
| --- | --- | --- |
| `idle` | loop | idle |
| `starting`, `turn_finished` | once | idle |
| `tool_error` | once | previous |
| `closing`, `closing_angry`, `angry` | once | stop |
| `thinking`, `reading`, `working`, `needs_input`, `sleeping`, `dragging` | phased (no `loops`) | idle |
| `out_of_quota` | loop | idle |

A phased state may list the same sequence three times. Other states and every optional
section (`variants`, `moods`, `reactions`, `ambient`, `touch`, `moves`, `activity`) keep
today's rules and stay optional.

## Code structure

All new code lives in `pet_animation` (`src/animation/`).

### `Catalog` (`catalog.h`, `catalog.cpp`; Qt Core only)

- Holds the parsed data now inside `Player`: sequences, animations, fidgets, moods,
  reactions, touch, moves, activity, `sleepAfterS`, `moveScale`. The plain data types
  move from `player.h` to `catalog.h` (`Frame`, `Choice`, `Animation`, `Fidget`,
  `Reaction`, `drawReaction`, `TouchRegion`, `Touch`, `Sides`, `Move`, `ActivityChoice`,
  `ActivityLinger`, `ActivityArt`); `player.h` includes `catalog.h`, so includers keep
  compiling.
- `static Catalog load(const QString &root, const QString &pet, QString *error)` reads
  `<root>/assets/<pet>/animations.json`, resolves frame paths against `root`, and runs
  today's validation unchanged, plus the `asset_root` containment rule. On failure it
  returns an empty catalog and sets `error`.
- `QString contractError() const` checks the core state contract from `core-states.json`
  separately, so a `Player` still plays the partial catalogs of the prototype tests;
  `PetLibrary::activate()` refuses a pet that fails it.
- `bool valid() const` (non-empty with an idle loop).

### `PetLibrary` (`pet_library.h`, `pet_library.cpp`)

Owns resource registration, which today sits at the top of `Player::load()`.

- `PetLibrary(const QString &index = {}, const QString &mapRoot = "/")`. With no
  `index` it locates `artwork.rcc` exactly as today (beside the executable,
  `../share/agent-pet/`, `../Resources/`); tests pass the path of a fixture index. The
  index and every pack it later registers go under `mapRoot` (via
  `QResource::registerResource(path, mapRoot)`), so a fixture library mounted at
  `/fixture` never sees the real pets, and vice versa. `root()` is `":" + mapRoot`.
- `static PetLibrary &shared()`: the process-wide library over the installed index at
  `"/"`. Resource registration is process-global, so one shared owner is the honest
  model.
- `QVector<PetInfo> pets() const`: one entry per `<root>/assets/<id>/pet.json` whose
  `pet.json` is valid, sorted by id; reads metadata only and registers no sequence pack.
  `PetInfo` holds `id`, `name`, `author`, `terms`, and `preview` (a resource path). An
  invalid `pet.json` is skipped with a `qWarning`.
- `bool activate(const QString &id, QString *error)`: registers that pet's sequence
  packs listed in `<root>/assets/<id>/packs.json`, from the directory holding the
  index. On any failure it unregisters what it registered and returns false. A pet
  without `packs.json` keeps its frames inside the index itself (legacy layout). Each
  library activates at most one pet; a second call fails.
- `QString active() const`, `QString root() const`.

### `Player`

- Plays a `Catalog`; it no longer reads files or registers resources. Its public API
  stays as it is (accessors forward to the catalog).
- `Player(QObject *parent = nullptr)`: plays `PetLibrary::shared()`'s active pet,
  activating `vpet` first when none is active. Existing `pet::Player player;` tests keep
  working.
- `Player(QObject *parent, const QString &root, const QString &pet = "vpet")`: loads
  `Catalog::load(root, pet)` from a directory, for fixture tests (today's
  `Player(nullptr, directory.path())` keeps its meaning).
- A load failure keeps today's behavior: `error()` holds the reason and the window shows
  the "Artwork unavailable" placeholder.

### Startup (`main.cpp`, GUI path)

1. Resolve the pet: `--pet <id>` (not saved), else the `pet` preference, else `vpet`.
2. `PetLibrary::shared().activate(id)`. On failure, log in English
   (`Pet "<id>" is unavailable: <reason>; using vpet`) and activate `vpet`.
3. Construct `PetWindow` as today; its `Player` plays the active pet.

Headless subcommands never construct a `PetLibrary`.

## Build, index and hash tree

`CMakeLists.txt` gains a function, `add_pets(<target> SOURCE <dir> OUTPUT <dir>)`, that
builds pets from a source folder into an index and packs. It runs once for `assets/`
(output: the build directory, as today) and once for `tests/fixtures/pets/` (output:
`<build>/test-pets/`). Each source folder maps to the resource prefix `assets/`, so a
fixture frame `tests/fixtures/pets/mini/idle/_000_100.png` is the resource
`assets/mini/idle/_000_100.png`, and catalogs read the same in both cases.

- **Discovery:** `file(GLOB … CONFIGURE_DEPENDS "<source>/*/pet.json")`.
- **Sequence packs (unchanged):** every PNG in a subfolder of a pet folder belongs to
  the sequence named by its directory; each sequence builds
  `artwork-<sha256(resource directory path)>.rcc`, with the same
  `qt_add_binary_resources` options as today (format 1, no compression). For VPet the
  resource path equals today's source path (`assets/vpet/vup/Default/Nomal/1`), so its
  names and bytes do not change.
- **Pack list and tree:** after the packs build, a script step (`cmake -P
  cmake/pet_tree.cmake`, no new build dependency) writes
  `<build>/pets/<id>/packs.json`:

  ```json
  {
    "root": "sha256:<hex>",
    "catalog": "sha256:<hex>",
    "packs": [
      { "name": "artwork-<hex>", "sequence": "vup/Default/Nomal/1", "sha256": "<hex>", "bytes": 421888 }
    ]
  }
  ```

  - `catalog` = SHA-256 of the UTF-8 text
    `"pet.json <hex>\nanimations.json <hex>\npreview.png <hex>\n"`, each `<hex>` being
    that file's SHA-256.
  - Each pack's `sha256` and `bytes` describe the built `.rcc` file (the unit Part 2
    downloads). `sequence` is relative to the pet folder. Packs sort by `name`.
  - `root` = SHA-256 of the UTF-8 text `"agent-pet-pet-tree 1\ncatalog <hex>\n"`
    followed by one `"<name> <sha256>\n"` line per pack, in that order.
  - The step depends on every pack and on the pet's metadata, so any change rebuilds
    the index.
- **Index:** `artwork.rcc` holds, per pet, `assets/<id>/pet.json`,
  `assets/<id>/animations.json`, `assets/<id>/preview.png` and
  `assets/<id>/packs.json`. Today's `assets/vpet/packs.json` (an array of every pack)
  becomes VPet's tree file. The install rules for `artwork.rcc` and the packs stay as
  they are.
- Every install keeps shipping every bundled pet's packs (today only VPet's). Part 2
  changes that for non-default pets.

In Part 1, `PetLibrary` reads only the pack `name`s; the digests exist for Part 2 and
are proven by a test.

## Choosing a pet

- **Preference:** `QString pet = "vpet"` in `Preferences`, limited to
  `[a-z0-9-]{1,32}` (an invalid string reads as `vpet`). A well-formed id the build does
  not know is kept as-is and falls back to VPet for that run, since it may come from a
  newer version or, later, a Part 2 download.
- **CLI:** `--pet <id>` for the GUI, overriding the preference for one run without
  saving it.
- **Settings:** a **Character** row at the top of the **Pet** tab, only when
  `PetLibrary::shared().pets()` has two or more entries. The row is a small `PetPicker`
  widget (`src/desktop/pet_picker.{h,cpp}`, in `pet_ui`) built from a
  `QVector<PetInfo>`, the saved id and the running id, so tests drive it without a
  `PetWindow`. One checkable tile per pet (`QToolButton` in an exclusive
  `QButtonGroup`): the preview scaled to about 96 px, the name underneath, tooltip
  "by %1". The checked tile has an accent border and a check mark; the accessible name
  is the pet's name. Choosing a tile emits `chosen(id)`, and `PetWindow` saves `pet`
  (re-reading preferences first). While the chosen pet differs from the running one, a
  note says "%1 will appear the next time Agent Pet starts." Previews come from the
  index, so showing the picker registers no artwork.
- **About:** the fixed VPet credit becomes "Artwork: %1" with the active pet's author,
  and the dialog shows the active pet's terms file (`:/licenses/<terms>`, or "Artwork
  terms unavailable." when missing). Application notices are unchanged.
- New strings are translated in `translations/agent-pet_vi.ts`.

## Contributor tooling

- **`scripts/new_pet.py <id> --name N --author A --terms FILE --idle FOLDER
  [--preview PNG]`** (stdlib only). It creates `assets/<id>/`, copies the idle frames
  into `assets/<id>/idle/`, and writes `pet.json`, `manifest.json` and an
  `animations.json` in which every core state listed in `core-states.json` uses that
  sequence with its contract shape. The preview defaults to the first idle frame if it fits 512 × 512; otherwise
  the script asks for `--preview`. It refuses an existing folder or a missing terms
  file, and the result verifies and runs.
- **`scripts/add_sequences.py --pet <id> [--source DIR] SEQUENCE…`.** Without `--pet`
  (or with `--pet vpet`) it behaves as today: archive source and
  `available-animations.json`. For another pet `--source` is required; frames are
  copied to `assets/<id>/<sequence>/`, and that pet's manifest and catalog `sequences`
  are updated. Mapping sequences to states stays a hand edit.
- **`scripts/verify_assets.py`** checks every pet folder at the file level: `pet.json`
  fields and id; the terms file exists in `licenses/`; the preview is a PNG within
  512 × 512; the manifest matches the files; every frame is used by the catalog; frame
  paths lie under `asset_root`; sequence timing is consistent. VPet's
  `available-animations.json` checks stay. Catalog semantics, including the core state
  contract, are validated by C++ `Catalog` alone; the contract itself lives only in
  `core-states.json`.

## Error handling

| Situation | Behavior |
| --- | --- |
| Saved or `--pet` id unknown, `pet.json` invalid, or packs fail to register | Log in English, roll back registrations, run VPet; the saved preference is kept |
| VPet itself fails | Today's "Artwork unavailable" placeholder and controls |
| Catalog invalid (including a missing core state) | `Player::error()` with the reason; placeholder as today |
| Terms file missing at runtime | About shows "Artwork terms unavailable." |
| Fewer than two valid pets | The Character row is hidden |

## Testing

A new Qt Test suite `pets` (`tests/pets_tests.cpp`), registered on every platform, using
fixture pets under `tests/fixtures/pets/` that are built into a test index and packs by
the same CMake function:

- `mini`: one small sequence used by every core state.
- `duo`: two sequences, so pack registration is observable per sequence.

Fixture art is a few tiny generated PNGs under the app's Apache-2.0 license.

Cases:

- `Catalog` rejects a missing core state, a wrong shape for a core state, an
  `asset_root` outside the pet folder, and a frame outside `asset_root`; it accepts
  VPet's catalog unchanged.
- `mini` plays every core state through `select()` with no error.
- A `PetLibrary` over the fixture index at `/fixture` lists both fixtures (and not
  VPet) and registers no sequence pack (their frame resources do not exist yet);
  `activate("duo")` makes only `duo`'s frames exist; a missing pack rolls back every
  registration; a second `activate` fails.
- For each bundled pet and each fixture, the recomputed digests of the built `.rcc`
  files and metadata match `packs.json` (`root`, `catalog`, every leaf).
- `Preferences` round-trips `pet`, keeps an unknown well-formed id, and reads an
  invalid one as `vpet`.
- `PetPicker` is hidden with one pet; with two it shows two exclusive tiles, checks the
  saved one, emits `chosen(id)`, and shows the restart note only when the chosen id
  differs from the running one.

Existing suites run unchanged; `prototype-tests` keeps exercising VPet through
`pet::Player player;`. `verify_assets.py` keeps running in CI and now covers every pet.

## Documentation

- `starter/docs/pets.md`: contributor guide (a minimal pet with `new_pet.py`, the
  format, the core state contract, licensing, adding sequences, the hash tree). It marks
  the core state contract as provisional until the event refactor, which will ship a
  migration for existing catalogs.
- `starter/docs/adr/0028-pet-packs.md` and its row in `adr/README.md`.
- `starter/docs/architecture.md` (code map: `Catalog`, `PetLibrary`), the
  `starter/README.md` "Pets" section, and the asset notes in `CLAUDE.md`.
- Issue #62 updated with the restart-to-apply and core-state decisions.

## Future: event refactor

A later refactor will rework how events choose animations. Today events reach the pet
as names: `Sessions` → `Monitor` (`sessionAnimation()`) → `Player::select("needs_input")`,
plus direct requests from `PetWindow`, `Mood`, `EasterEggs` and `Ambient`
(`select("closing")`, `reactions("snack")`). This design keeps that path untouched and
leaves these seams for it:

- **One meeting point.** `core-states.json` is the whole contract between the event
  side and pets. The refactor edits or replaces that one file; nothing else in pet packs
  names an event.
- **One parser.** `Catalog` is where a per-pet mapping section (for example events or
  roles to states) would be parsed and validated, apart from playback.
- **Free to change the catalog format.** The index is version-locked to the binary
  (it ships in `artwork.rcc`), and every catalog lives in the repository (VPet and the
  test fixtures), so a new catalog section or a `schema_version` bump needs only a
  migration script run over them, shipped with that refactor.
- **Reaction pools stay optional.** Pool names (`snack`, `danger`, `milestone`, …) are
  already soft: an empty pool plays nothing, so a pet may omit them.

## Out of scope (Part 2 or later)

- Downloading non-bundled pets on demand; the content-addressed store; the `pets`
  release tag; a download state in the picker.
- Switching pets without a restart, or a "Restart now" button.
- More than one pet on screen; code or scripts in packs; packs from user folders (#43).
- The event refactor itself (see above).
- Shipping a second pet.
