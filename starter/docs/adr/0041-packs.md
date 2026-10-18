# 0041. Packs: file format, storage and trust

- Status: Accepted (design; implementation follows in phases 1–7)
- Date: 2026-10-16
- Scope: [#92](https://github.com/WindyWin/vpet-agent-pet/issues/92), phase 0 of
  [#91](https://github.com/WindyWin/vpet-agent-pet/issues/91)

## Context

Pets currently enter the build through `assets/<id>/` and are distributed as Qt
resources, either bundled or downloaded into `<data>/pets/<sha256>.rcc`.
Plugins already load JSON and PNG files from a user folder. These separate paths
couple pet releases to app releases, duplicate artwork in packages, and require
this project to redistribute every pet it lists.

Users need to import pets and plugins, convert locally installed workshop art,
and optionally install or update packs from independent sources. This record
sets the common contract before those features are implemented. The existing
[pet](../pets.md) and [plugin](../plugins.md) guides describe current behavior.
The transitions below replace parts of [0028](0028-pet-packs.md),
[0030](0030-on-demand-pets.md) and [0033](0033-plugin-packs.md) only when their
implementation phases land; those records remain historical evidence.

Issue #92 reserved this filename before the unrelated
[0041 provider record](0041-additional-agent-providers.md) merged. Both filenames
are retained; links and index titles distinguish the two decisions.

## Decision

### Archive and metadata

A `.agentpet` file is a ZIP archive with `pack.json` at its root, without an
extra enclosing folder. Entries contain only JSON, PNG and UTF-8 licence/terms
text (`.txt` or `.md`). Scripts, executables, libraries, Qt resource binaries,
nested archives and executable workshop plugins are rejected, never executed.
Use stored or deflated ZIP entries; encrypted archives are unsupported.

`pack.json` is a UTF-8 JSON object with these fields. Unknown fields and wrong
types are errors; future format extensions need an explicit compatibility decision.

| Field | Contract |
| --- | --- |
| `kind` | Required string: `pet` or `plugin`. |
| `id` | Required string, `[a-z0-9-]{1,32}`; portable filename rules from the pet guide also apply. |
| `version` | Required SemVer 2.0 string, at most 32 characters. |
| `schema` | Required positive integer: the animation catalog schema, currently `2`, not the metadata schema. |
| `minAppVersion` | Required release version `major.minor.patch`, at most 32 characters; oldest compatible app. |
| `name` | Required nonempty string, at most 64 characters. |
| `author` | Required nonempty artwork credit, at most 128 characters. |
| `license` | Required nonempty licence identifier or short label, at most 64 characters; not a claim of redistribution permission. |
| `terms` | Required relative path to a nonempty UTF-8 terms file inside the archive. |
| `source` | Optional nonempty provenance label, at most 256 characters, such as `vpet-mod`; never grants trust or authorizes downloads. |
| `pet` | Required only for `plugin`: target pet id using the same id rules. Forbidden for `pet`. |

For example, a pet manifest can be:

```json
{
  "kind": "pet",
  "id": "example-cat",
  "version": "1.0.0",
  "schema": 2,
  "minAppVersion": "0.16.0",
  "name": "Example Cat",
  "author": "Example artist",
  "license": "CC0-1.0",
  "terms": "licenses/TERMS.txt"
}
```

The version in this example illustrates syntax, not an assertion that 0.16.0
supports importing packs. Authors must set the first app version supporting the
features their pack actually uses.

A pet archive contains the files from `assets/<id>/` at its root: `pet.json`,
`animations.json`, `preview.png`, `manifest.json` and frame folders, plus
`pack.json` and its terms file. Existing optional catalog inventory JSON may be
retained. Preserve the frame layout and the catalog's logical
`asset_root: assets/<id>`; the disk loader maps that logical root to the installed
pack directory, never to an arbitrary host path. Every referenced frame must
resolve inside that directory. Preview remains at most 512 × 512 pixels.

A plugin archive contains today's `plugin.json`, `animations.json`, optional
`events.json` and frame folders, plus `pack.json` and terms. Existing fragment,
namespace, override, cue and event-rule restrictions still apply. `pack.json`
is the installation identity; duplicated metadata in `pet.json` or `plugin.json`
must agree, including plugin `min_app_version` when present. A pet's legacy
`terms` filename resolves under the archive's `licenses/` folder and must identify
the same file as `pack.json.terms`. This retains the current payload formats
without two competing identities.

### Storage, identity and replacement

`<data>` is the existing per-user application data directory (see
[installation](../install.md)). Installed pet packs live in
`<data>/packs/pets/<id>/`; plugin packs use the existing
`<data>/plugins/<id>/` directory. Both are separate from the legacy
`<data>/pets/<sha256>.rcc` download store. Legacy manually installed plugins
remain supported; importing over one is an explicit replacement.

Identity is `(kind, id)`. Built-in pet ids are reserved: `vpet` cannot be installed
or shadowed by a pet pack. A plugin may target `vpet`; its own id cannot use a
reserved built-in id. Pet and plugin namespaces are separate. Reject ambiguous
or duplicate archive paths, including case-folding collisions, on every OS.

Installing an already installed identity replaces the whole pack after user
confirmation, never merges files. Show old/new versions and provenance. A higher
SemVer version is an update; equal or lower versions require explicit replacement
or downgrade confirmation. Plugin fragment merging into a running pet is a
separate operation and keeps its existing rules.

Extract to a fresh staging directory on the same filesystem, validate completely,
then commit the replacement with rollback on failure. Serialize installs and
retain the old pack until commit succeeds. Never expose a partly extracted pack
to discovery. Changes take effect on the next start; do not replace files under
an active reader. Defer such a commit until restart. Invalid selected pets fall
back to VPet with a visible reason; invalid plugins are skipped with a reason.
Source URL, digest and update consent are app-owned installation records outside
the payload. A pack cannot set or overwrite them.

### Trust and consent

All three origins pass exactly the same archive, metadata, compatibility,
resource and runtime validation. Origin never relaxes a limit.

| Origin | What installation shows |
| --- | --- |
| Local file (including converted workshop art) | Filename, declared author, version, licence and full terms; identify it as unverified local content and require acceptance. |
| Official source | Official source identity, author, version, licence and full terms; require acceptance on first install. Official listing means the project reviewed redistribution permission, not exemption from validation. |
| Third-party source | Source URL, declared author, version, licence and full terms; warn that the project has not reviewed this source or its redistribution rights, and require explicit acceptance. |

Render metadata and terms as inert text. Local provenance labels do not make a
pack official. Official status comes only from the app's configured official
source; downloaded archives must match the source index's size and SHA-256.
A digest establishes integrity against that index, not authorship. Third-party
sources are added explicitly by the user. No pack can add a source or opt into
updates. Automatic updates are opt-in per source/pack in phase 6, restricted to
the recorded origin and increasing versions; changed terms or origin require
fresh acceptance. Local packs have no automatic update origin by default.

### Validation and limits

Start from the current plugin frame limits and allow larger complete pets:

| Resource (per pack) | Plugin | Pet |
| --- | --- | --- |
| Frame references across all sequences | 2,000 | 10,000 |
| Individual PNG file | 4 MiB | 4 MiB |
| PNG width or height | 2,048 px | 2,048 px |
| All PNG bytes, including preview | 64 MiB | 512 MiB |
| `animations.json` | 1 MiB | 8 MiB |
| ZIP entry count, including directories | 4,096 | 20,000 |
| Archive bytes and total extracted bytes (each bounded) | 80 MiB | 544 MiB |

Additionally, cap `pack.json`, `pet.json`, `plugin.json` and `events.json` at
64 KiB each, terms at 1 MiB total, and remaining JSON at 8 MiB total. Preserve
the existing maximum of 32 installed/enabled plugins. Count actual streamed
bytes, including unreferenced entries; do not trust ZIP size declarations.
Reject excessive entry counts and expansion before committing. Inspect PNG
headers before decoding and retain bounded/lazy frame caching rather than
loading all accepted frames into memory.

Reject absolute paths, drive/UNC paths, traversal, backslashes, symbolic/hard
links, special files and names disallowed by the portable pet filename rules.
Every entry and every catalog/terms reference must stay inside the staging
root. Validate JSON, PNG content, manifest sizes/hashes, referenced files,
catalog schema, cue contracts and plugin rules before installation. Runtime
loading also checks compatibility and paths because user folders can change
after import. No code loading or live reload is introduced.

### Compatibility and distribution

Require `pack.json.schema == animations.json.schema_version` and a schema the
running app explicitly supports (currently exactly `2`). Metadata schemas remain
`1` for `pet.json` and `plugin.json`; `events.json` keeps its own schema `1`.
Reject newer or unsupported older catalogs with an actionable message; never
silently reinterpret them. Compare `minAppVersion` numerically with the app's
release version and reject packs requiring a newer release. Prerelease app
builds do not satisfy the corresponding final release minimum.

Authors upgrade old catalogs with `scripts/migrate_catalog.py` (currently schema
1 → 2), revalidate, update `pack.json.schema` and `minAppVersion` as needed, bump
the pack version and rebuild the archive. Import does not mutate an author's
catalog to make it load.

VPet is the only pet built into the final app. Phase 4 retires
`AGENT_PET_BUNDLED_PETS`, the on-demand downloader and the legacy `.rcc` download
store. Until then the current distribution remains operational. That phase must
handle existing selections explicitly, fall back to VPet when necessary and
remove only legacy download data, never the new installed pack folders.

Official `.agentpet` files live in a separate gallery repository starting in
phase 4, released independently of the app. The same repository hosts the
official source `index.json` in phase 5. Third parties may host compatible
indexes elsewhere. Workshop conversion reads already installed local files;
it neither downloads from Steam nor grants redistribution permission. App
release asset deduplication (#100) remains independent of this contract.

## Consequences

- Pet and plugin installation share a format and validation boundary; authors
  can distribute data without rebuilding or releasing the application.
- Full-pack replacement costs more disk space during staging, but avoids stale
  frames surviving an update and makes failure rollback possible.
- Larger pet limits accommodate existing artwork while bounding extraction;
  they are ceilings, not permission to decode the whole pack into memory.
- Terms travel with imported artwork. Local use does not imply permission to
  publish the same artwork in the official gallery.
- Phases 1–7 must implement the contract, UI consent and migration. This ADR does
  not itself add importing, sources, conversion or automatic updates.

## Validation

### Design evidence — 2026-10-16

- Reviewed #91/#92 against the pet and plugin guides, ADRs 0028/0030/0033, plugin
  limits in `src/animation/plugins.h`, and `scripts/migrate_catalog.py`.
- Measured the checked-in PNGs, including previews, at `c1e7660`: VPet has 1,323
  PNGs / 193.84 MiB; Whale Girl has 1,663 / 250.15 MiB. Across all five pets the
  largest PNG is below 0.44 MiB and the largest side is 1,000 px. Pet-sized byte
  limits therefore cover current artwork with room to grow; plugin-sized byte
  limits would exclude VPet and Whale Girl.
- Documentation links, required sections and the example JSON are checked in
  this phase. No runtime behavior changes or runtime test claims are made.
- Implementation acceptance must cover both kinds and all origins; unsupported
  schemas/app versions; reserved ids; replacement/downgrade consent; interrupted
  extraction and rollback; traversal, links, duplicate names and ZIP expansion;
  invalid/oversized PNGs and boundary limits; changed terms; next-start activation;
  and VPet fallback. Phase 4 must also verify migration from the legacy store.

### Phase 1 evidence — 2026-10-16

- Implemented #93: `PetLibrary` discovers unpacked pets under `<data>/packs/pets`,
  validates their metadata, file budgets, paths, PNG headers, manifest hashes and
  core cue contract, and maps logical catalog paths to disk. Existing plugin
  merging and lazy `QImageReader` playback also work with these pets.
- Settings marks local pets, disables invalid choices with reasons, explains a
  missing/invalid saved choice and opens the install folder. About can display
  each valid installed pet's terms. Resource-index ids retain priority during
  the transition; a conflicting local folder is listed as unusable.
- Linux (EndeavourOS), Qt 6.11.2, GCC 16.2.1: Release build succeeded. All 17 CTest
  suites passed across the sandbox run and an unsandboxed rerun of providers,
  events, update-install and startup (the sandbox blocks their local sockets).
  The final pets/plugins/i18n run passed after boundary checks were added.
- The checked-in `local-cat` fixture covers discovery, playback, terms, plugin
  extension, missing/invalid fallback, reserved ids, symlinks, case collisions,
  checksums, compatibility, invalid cue contracts, per-file and aggregate PNG
  limits, oversized catalogs/previews and the 10,000-frame boundary. Pruning
  leaves installed pet folders intact, and a fresh library can activate them.
- An isolated user-data smoke run with `--pet local-cat --smoke-test` completed
  playback, input recovery and shutdown without falling back to VPet. The new
  interface strings are translated into Vietnamese. Native macOS and Windows
  execution remains to be verified in CI/manual acceptance; the loader uses Qt
  filesystem/image APIs and platform-specific per-user paths.

### Review follow-up: local display discovery (2026-10-18)

The app validates local pack display metadata once during startup, before creating windows. Settings and About reuse that explicit snapshot, so opening either dialog does not scan and hash installed artwork again. Manually added or changed folders appear after restarting; an explicit refresh is available to library callers. This cache only supplies display metadata: activation always validates the current on-disk pack, and frame loading retains its bounded file and image checks. Regression coverage changes a frame after discovery and proves display reuse never authorizes activation, then verifies that refreshing reports the invalid pack. About pet selection also retains all application and Qt notices.

### Review follow-up: bounded startup discovery (2026-10-18)

Display snapshots now read only `pack.json` and `pet.json` (64 KiB each), check safe terms/preview paths and their file limits, and inspect the preview PNG header. They never enumerate frame directories, read catalogs/manifests, decode preview pixels or hash inactive artwork. Discovery visits at most 256 local folders; the pet guide records this display limit. Full validation remains mandatory when activating a pack. Regression coverage corrupts a frame before snapshot creation: display discovery still succeeds, activation rejects the corrupt artwork, and explicit metadata refresh detects a subsequently invalid pack.json.

### Phase 3 conversion (2026-10-17)

- `convert_vpet_mod.py` reads the data-only pet LPS records and PNG graph
  folders. It retains the app VPet catalog's sequence identities, playback,
  variants, moods, cue pools and activity transitions; each sequence resolves
  to matching mod art, its `Nomal` equivalent, or that mod's default idle.
  Frame ordering/timing comes from the existing `frames_of()` helper. Portable
  output frame names handle upstream filenames containing Chinese characters.
- Touch rectangles and side offsets come from the definition. Raise points and
  tags are preserved in the report. Raise points are not runtime catalog fields.
  Matching movement art uses the template geometry/speeds; fallback-only
  movement is disabled. Arbitrary workshop movement programs are not imported.
- The real root `assets/vpet/pet/vup` archive converted and passed the phase 2
  application's validator and deterministic pack writer. Of 146 template
  sequences, 139 matched directly and seven used idle: the five app-authored
  `WORK/Desk/*` transitions and `Reminders/eat` / `Reminders/drink` are absent
  upstream. The report counts 30 fully matched cues, five with fallbacks
  (including activity transitions), and 419 unused graph folders. Preview
  resizing was exercised with Pillow 13.0.0.
- Regression tests cover the root graph mapping and frame durations, sparse-mod
  mood/idle fallback, touch metadata, ignored code files, Unicode frame names,
  terms requirements, path traversal, symlinks and deterministic packing. Both
  sparse and full fixture outputs pass the native pack validator.
- **Acceptance still pending:** two independent real workshop mods with their
  licence text have not been supplied. The bundled VUP archive and generated
  fixtures do not satisfy that requirement. Record their mapping gaps here
  before closing #95; the converter's PR remains a draft meanwhile.

### Phase 2 review corrections (2026-10-18)

- Drop handling now accepts the event before queuing archive inspection and
  consent dialogs, allowing native drag sources to finish promptly. Queuing pet
  removal preserves the saved pet ID; startup retains the documented fallback.
- ZIP extraction treats an exhausted-input `Z_BUF_ERROR` as a request for the
  next input chunk, while still requiring the final stream marker and checksum.
  The importer regression uses a 64 KiB input/output boundary and separately
  rejects the same stream without its final block.
- Invalid pending payloads and plugin-limit rejections are discarded after
  restoring any interrupted replacement. Failures are reported together after
  unrelated jobs have run. Filesystem/recovery failures remain retryable.
  Transaction tests verify preservation of the restored pack, application of a
  later removal, and successful retries without manually deleting a failed job.

### Review follow-up: local display discovery (2026-10-18)

The app validates local pack display metadata once during startup, before creating windows. Settings and About reuse that explicit snapshot, so opening either dialog does not scan and hash installed artwork again. Manually added or changed folders appear after restarting; an explicit refresh is available to library callers. This cache only supplies display metadata: activation always validates the current on-disk pack, and frame loading retains its bounded file and image checks. Regression coverage changes a frame after discovery and proves display reuse never authorizes activation, then verifies that refreshing reports the invalid pack. About pet selection also retains all application and Qt notices.

### Review follow-up: bounded startup discovery (2026-10-18)

Display snapshots now read only `pack.json` and `pet.json` (64 KiB each), check safe terms/preview paths and their file limits, and inspect the preview PNG header. They never enumerate frame directories, read catalogs/manifests, decode preview pixels or hash inactive artwork. Discovery visits at most 256 local folders; the pet guide records this display limit. Full validation remains mandatory when activating a pack. Regression coverage corrupts a frame before snapshot creation: display discovery still succeeds, activation rejects the corrupt artwork, and explicit metadata refresh detects a subsequently invalid pack.json.

### Scaffold version validation (2026-10-18)

- `new_pet.py` rejects versions outside the pack validator's SemVer grammar or
  32-character limit before creating any folders. Scaffold regressions cover
  invalid release/prerelease/build identifiers, trailing whitespace, overlong
  versions, valid prerelease/build versions and the exact 32-character boundary.
  Invalid versions are rejected in both bundled and local modes.

### Phase 4 gallery separation (2026-10-17)

- The application source/build now contains only VPet. Fat Cat, Kuro, Yun and
  Whale Girl sources, authoring helpers, terms and notices move to the separate
  gallery. Each pack embeds its own terms. CC0 applies to the first three;
  Whale Girl retains the recorded CC BY-NC-SA 4.0 attribution, noncommercial and
  share-alike conditions. No Bocchi source or redistribution terms were present,
  so it is excluded; the workshop conversion path remains available.
- Removed the pet RCC downloader, selection/progress UI, bundled-pet option and
  old `pets` release publishing steps. The remote historical release is retained
  for existing versions. VPet's 141 resource pack names, catalog tree and leaf
  digests compare equal with the phase 2 build.
- A saved legacy choice without a valid local replacement is changed once to
  VPet, with an English/Vietnamese notice linking to the gallery. Explicit
  `--pet`, preview/no-persist runs and unknown custom identities keep their
  existing behavior. Normal startup removes only recognizable legacy blob,
  partial and stamp files; tests cover preserving installed packs, unknown files
  and linked directories.
- Gallery tooling builds four deterministic archives, imports each into a fresh
  Linux data folder, and produces a version-1 `index.json` plus previews. Index
  fields are `schema_version: 1`, `packs`, with HTTPS preview/download URLs,
  byte counts, SHA-256, identity, descriptive metadata and compatibility fields.
  This is the producer contract for phase 5. Gallery CI pins the VPet-only
  validator revision because older indexed builds reserve these pet ids.
- Linux Release build, asset verification, all 20 CTest suites, gallery source
  validation, all four archive imports and isolated package smoke check passed.
  Four CTest suites required local socket/process access outside the restricted
  development sandbox. macOS/Windows verification follows in PR CI.

### Phase 5 sources (2026-10-18)

- Settings has an official gallery source by default and explicitly added HTTPS
  third-party catalogs. A bounded, validated version-1 index is cached atomically;
  malformed or failed refreshes retain the last good catalog. Removing a source
  or an entry never removes installed packs.
- Each download streams to a temporary file with a hard byte bound, exact catalog
  size and SHA-256 checks, HTTPS-only redirects and timeouts. Archive identity and
  metadata must match the listing before full terms consent and the existing
  transactional importer. Direct links explicitly disclose their lack of a digest.
- Installs and updates are manual and require terms acceptance each time. Plugins
  retain their enabled state; new plugins are not enabled. Source-origin records
  and persistent automatic-update consent will be implemented with phase 6;
  catalog presence is not consent to automatic updates.
- English/Vietnamese publishing and usage guides are in `pack-sources.md` and
  `pack-sources.vi.md`. Transport tests use an injected network manager to cover
  checksums, short/oversized bodies, headers, unsafe redirects, cancellation and
  retry without an external server. Parser/store tests cover compatibility,
  duplicate/invalid entries and last-good offline cache retention.
- Validation: Linux Release build and all 22 CTest suites passed (the four local
  socket/process suites outside the sandbox). Focused source/import/pet/plugin/
  translation suites passed again after UI changes. Asset validation preserved
  main's 148 VPet sequences. A live HTTPS smoke check fetched the official four-pack
  catalog, verified Fat Cat 1.0.0's exact size and SHA-256, matched its metadata,
  and imported/applied it in isolated temporary per-user storage.

- Review follow-up (2026-10-18): the pet window now owns and reuses the source
  browser, so closing or rebuilding Settings preserves ongoing work and removal
  callbacks. Terms consent uses a parent-owned heap dialog and guards the browser
  after the nested event loop. Regression tests cover Settings closure/reopening,
  browser reuse, and destruction during terms consent. All five focused
  source/import/transaction/pet/translation CTest suites passed outside the sandbox
  (the consent test stages through Qt's test-mode AppData location).

### Phase 2 review corrections (2026-10-18)

- Drop handling now accepts the event before queuing archive inspection and
  consent dialogs, allowing native drag sources to finish promptly. Queuing pet
  removal preserves the saved pet ID; startup retains the documented fallback.
- ZIP extraction treats an exhausted-input `Z_BUF_ERROR` as a request for the
  next input chunk, while still requiring the final stream marker and checksum.
  The importer regression uses a 64 KiB input/output boundary and separately
  rejects the same stream without its final block.
- Invalid pending payloads and plugin-limit rejections are discarded after
  restoring any interrupted replacement. Failures are reported together after
  unrelated jobs have run. Filesystem/recovery failures remain retryable.
  Transaction tests verify preservation of the restored pack, application of a
  later removal, and successful retries without manually deleting a failed job.
### Review follow-up: local display discovery (2026-10-18)

The app validates local pack display metadata once during startup, before creating windows. Settings and About reuse that explicit snapshot, so opening either dialog does not scan and hash installed artwork again. Manually added or changed folders appear after restarting; an explicit refresh is available to library callers. This cache only supplies display metadata: activation always validates the current on-disk pack, and frame loading retains its bounded file and image checks. Regression coverage changes a frame after discovery and proves display reuse never authorizes activation, then verifies that refreshing reports the invalid pack. About pet selection also retains all application and Qt notices.

### Review follow-up: bounded startup discovery (2026-10-18)

Display snapshots now read only `pack.json` and `pet.json` (64 KiB each), check safe terms/preview paths and their file limits, and inspect the preview PNG header. They never enumerate frame directories, read catalogs/manifests, decode preview pixels or hash inactive artwork. Discovery visits at most 256 local folders; the pet guide records this display limit. Full validation remains mandatory when activating a pack. Regression coverage corrupts a frame before snapshot creation: display discovery still succeeds, activation rejects the corrupt artwork, and explicit metadata refresh detects a subsequently invalid pack.json.

### Source-browser removal follow-up (2026-10-18)

Both Settings tabs now delegate removal notifications to one handler, so the reused browser behaves identically whichever tab opens it first. Queued pet removal preserves the saved choice, as required by the phase 2 transaction policy; startup supplies the fallback after actual removal. Plugin removal updates the enabled list. Regression cases exercise both tab orders and verify persisted pet/plugin preferences; all five focused pack/UI suites passed.

### Queued plugin removal preferences (2026-10-18)

- Queuing a removal preserves both the running plugin selection and the saved
  selection. `applyPending` reloads preferences and removes the committed plugin
  ID after moving its installed directory to the recovery backup, before deleting
  the transaction. Unrelated and unavailable plugin IDs remain selected.
- Preference read/save failures restore the plugin and retain the pending job;
  restart recovery repeats this idempotent commit. CLI removals use the same path,
  and unselected removals do not create a preferences file. Startup passes the
  committed selection into `PetLibrary::setPlugins` before loading artwork.
- Regression tests cover queued and failed removal, malformed preferences,
  interrupted removal, partial queue success, successful cleanup, and reinstall
  without resurrecting the removed selection. Both Settings tabs retain the
  selection while the shared source browser only queues the removal.

### Phase 6 automatic source updates (2026-10-18)

- App-owned `pack-origins/<kind>-<id>.json` stores source identity/revision,
  installed version, archive digest and accepted author/licence/terms digest.
  Pending operations carry the next origin alongside the staged payload. Startup
  finalizes the origin before deleting the old payload backup; a crash after
  payload replacement resumes that commit. Local replacements clear origins.
- A separate pack updater checks only sources of installed, recorded origins.
  It shares the existing app updater's automatic-check policy, persists daily
  attempt throttling and uses bounded sequential HTTPS transfers. The binary
  updater's automatic install mode does not authorize pack updates.
- Automatic pack updates are separately opt-in per source and default off.
  Only increasing SemVer versions from the recorded source, with unchanged
  author/licence/full displayed terms, can be staged without another prompt.
  Every archive passes digest, size, index agreement and importer validation.
  Incompatible versions remain visible in cached Settings catalogs.
- Removing a source rotates a persistent revocation revision before updating
  its source list. This revokes installed origins, opt-ins and unapplied automatic
  operations without deleting installed artwork. Re-adding a URL cannot revive
  consent. Disabling an opt-in cancels its current download, while already
  verified staged changes remain pending. Failed or malformed state fails closed.
- Existing phase-5 installs have no trustworthy persisted origin and are not
  inferred from matching catalog ids or versions. A new explicit catalog install
  establishes origin; local/direct URL imports never do.
- Optional session-list/bubble notifications are deferred; Settings provides
  available/incompatible updates, origin labels, progress and cancellation.
- Validation: Linux Release build and all 23 CTest suites passed (19 in the
  sandbox, four native IPC/process suites rerun outside it). Final focused
  source/update/transaction/import/pet/plugin/i18n suites passed after review.
  Tests include opt-in and local-import isolation, SHA/size/terms failures,
  incompatible catalogs, interrupted downloads, source removal/re-addition,
  persisted throttling, corrupt caches/settings, payload/origin crash recovery
  and rollback when the origin cannot be saved. Asset verification passed for
  all 148 VPet sequences; Vietnamese strings and usage docs are updated.

- Review follow-up (2026-10-18): failed source removal now restores the prior
  revision (or removes a newly created sidecar), preserving installed origins and
  automatic-update permission when the source list cannot be saved. Regression
  cases cover both revision states. Browser tests also check that background
  refresh preserves foreground status and settings errors remain visible. All six
  focused pack/source/import/pet/translation CTest suites passed.

- `new_pet.py` rejects versions outside the pack validator's SemVer grammar or
  32-character limit before creating any folders. Scaffold regressions cover
  invalid release/prerelease/build identifiers, trailing whitespace, overlong
  versions, valid prerelease/build versions and the exact 32-character boundary.
  Invalid versions are rejected in both bundled and local modes.

### Phase 7: native VPet import and local Steam discovery (2026-10-18)

- Embed the existing VPet catalog and cue contract as conversion templates, without
  duplicating bundled artwork. The C++ converter preserves the Python ordering,
  normal/idle fallbacks, touch mapping, movement filtering and JSON report. Shared
  generated fixtures compare output semantically and compare copied bytes, including
  all source archive frame names when the root artwork archive is available.
- GUI and CLI stage converted output in the existing pack transaction directory,
  run full validation, then require licence consent. GUI conversion runs on a worker
  with no widget access; closing the owner discards the result. Consent uses an owned
  asynchronous dialog so window destruction cannot invalidate a stack dialog.
- Extend the strict ZIP32 reader with an explicit mod mode. All entry paths, types,
  local headers and expansion bounds are checked; only LPS and PNG payloads are
  extracted. Unrelated plugin payloads are skipped, never loaded. Unicode source
  names map to portable output names. Input and output limits bound conversion.
- Steam discovery parses bounded Valve KeyValues files (modern and legacy library
  forms), deduplicates canonical libraries, and visits local app 1920960 workshop
  directories. It does not contact Steam. On this Linux host, the real
  `~/.local/share/Steam/steamapps/libraryfolders.vdf` lists the default library and
  `/run/media/wwindy/Game/SteamLibrary`, and `appmanifest_1920960.acf` confirms VPet
  is installed. Neither library currently has a `workshop/content/1920960` directory.
  Thus library format/path verification is real; installed workshop-item discovery
  is covered by fixtures. Manual Windows workshop conversion and two independently
  licensed real-mod acceptance runs remain unavailable locally, not claimed complete.
- Validation: Linux Release build and all 24 CTest suites passed. After the final
  Unicode ordering and dialog-lifetime changes, all nine affected converter,
  Steam, import, transaction, source, update, pet, plugin and i18n suites passed
  again. CLI tests verify refusal leaves no installed pet and reports cannot
  overwrite licence text; GUI tests close during conversion and during consent.
