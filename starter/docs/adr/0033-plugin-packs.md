# 0033. Plugin packs: catalog fragments merged into a pet

- Status: Accepted
- Date: 2026-10-08

## Context

A pet's animations come from its catalog and artwork packs, which [0028](0028-pet-packs.md) builds into
`artwork.rcc` and `artwork-<sha>.rcc`. Adding even one animation to VPet needed `scripts/add_sequences.py`,
a rebuild and a release. Issue #43 asks for **plugin packs**: data-only folders that add animations to a pet
and react to custom events. This record covers its phase 1: catalog fragments and Settings → Plugins, played
through the existing cues ([0029](0029-cues.md)). Phase 2 (a `custom` event kind with rules) and phase 3
(built-in triggers as rules) build on it later and submit ordinary intents to the behavior runtime
([0031](0031-behavior-runtime.md)).

## Decision

- A pack is a folder `<AppData>/plugins/<id>/` with `plugin.json` (id, name, version, author, license, the
  pet it extends, optional url and minimum app version), an `animations.json` catalog fragment and PNG
  frames. No code, scripts or native plugins. [plugins.md](../plugins.md) is the format reference.
- `Catalog::load` is split into `Catalog::read` (file, schema, sequences, frame paths) and `Catalog::build`
  (every other section, against those sequences). `plugins::apply` merges each enabled pack into the
  `CatalogSource` between the two, at the JSON level, in id order. After each pack it runs `build` and
  `contractError` on the result, so a pack is checked exactly as a pet's own catalog is and against
  everything merged before it. A pack that fails is dropped as a whole, and the source goes back to its
  state before that pack.
- Merging is additive by default. New states are named `<id>.<name>`. A pack's sequences take the id
  `<id>:<path>`, which no pet sequence can have (catalog paths never contain `:`). Its states, variants
  and mood art name only its own sequences. Variants, fidgets and reaction-pool entries join the pet's.
- Replacing anything the pet has (a state, a state cue's mapping, a whole reaction pool, existing mood
  art) needs an `overrides` entry: `states.<name>`, `cues.<name>` or `moods.<mood>.<state>`. Unused
  overrides are errors. Each replaced name is claimed by one pack, and a second pack replacing it is
  rejected. A replaced state loses the pet's variants and mood art for it, since they must keep its shape.
- `touch`, `moves`, `activity`, `asset_root` and `ambient.sleep_after_s` stay the pet's. They depend on
  its geometry and timing, and the issue asked for animations played through existing triggers.
- Frames are files that resolve inside the pack folder; symbolic links are followed only that far. They
  load from the folder through `QImageReader`, like resource frames, so `artwork.rcc` and the
  update path's pack reuse are unaffected. At startup only each frame's PNG header is read. Limits per
  pack: 2,000 frames, 4 MiB and 2,048 pixels a side per frame, 64 MiB in all, 1 MiB of fragment; 32
  packs.
- `PetLibrary::setPlugins(folder, enabled)` is called before `activate`. Packs merge after the pet's
  own packs and frames are verified, so a pack can never make the pet fail to start. `plugins()` rescans
  the folder for Settings and keeps what activation decided for each pack.
- `preferences.json` gains `plugins`, a list of enabled ids (valid ids, at most 32; a non-array
  invalidates the file like any wrong type). `PetWindow::setPlugins` saves it through the usual re-read
  before save. `--no-persist` and `--smoke-test` load no packs.
- Settings gains a **Plugins** tab (`PluginList`): one checkable line per pack with version, author,
  license and status (*Loaded*, *Not loaded: reason*, *Cannot be used: reason*, *Off*, or what the next
  start will do), plus **Open plugins folder**. Loading is restart-only, like choosing a pet.

## Consequences

- A user can add animations and new ways to celebrate, snack or fidget without a rebuild. A pack author
  gets the same validation messages as a pet author, in Settings and in the log.
- Packs are tied to one pet by id, because frame size, origin and state shapes differ between pets.
- Pack art is under its own terms, shown in Settings; the app's and VPet's notices are unchanged.
- Phase 2 can give packs an `events.json` whose rules submit intents (never Shutdown, Urgent or Startup)
  that draw from cues or pack states. Rate limiting and custom-event validation belong there.
- No hot reload: a pack edited while the pet runs applies on the next start.

## Validation

### Plugin pack evidence — 2026-10-08

- Ubuntu 24.04, Qt 6.4.2 (the CI's Qt 6.5.3 could not be downloaded in that environment, so the version
  check and the `QDataStream::Qt_6_5` uses were relaxed in a local copy, not in the tree), GCC 13, CMake
  3.28: `ctest --test-dir build` passed all 16 tests, including the new `plugins` suite (15 cases):
  plugin.json rules; adding states, reactions, fidgets, variants and mood art; namespacing; overrides
  for states, state cues, pools and mood art; claim conflicts between packs; missing, escaping, linked,
  non-PNG, oversized and mistimed frames, forbidden sections and contract breaks each rejecting only
  their pack; enabled/other-pet/off status; the example pack against VPet's real catalog, played by a
  `Player` from its files; `PetLibrary` activation with a bad pack; preferences; the Settings list; and
  `PetWindow` saving the choice.
- `update_translations` added 14 strings, all translated into Vietnamese. A screenshot of the Plugins
  tab under the offscreen platform showed a loaded pack and an unusable pack's reason.
