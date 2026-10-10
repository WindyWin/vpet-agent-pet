# Making a pet

A pet is a folder under `assets/` with its own frames, catalog, preview and art terms. A rebuild picks up
every folder that has a `pet.json`; no C++ or CMake changes are needed. Once two or more pets are
known, Settings → Pet shows a **Character** row with a tile per pet, and the chosen pet appears the
next time Agent Pet starts. VPet ships with the app; other pets, such as Fat Cat ([0036](adr/0036-fat-cat-pet.md), a worked example of a
small pet on borrowed CC0 art), Kuro and Yun ([0037](adr/0037-drawn-pets.md), original art drawn by `scripts/draw_kuro.py` and `scripts/draw_yun.py`; Yun shows touch throws, edge hiding, flying moves and activity handovers in a pet of its own) and Phù Đồ ([0038](adr/0038-phudo-mecha-pet.md), a contributor's design sheet animated by `scripts/draw_phudo.py`; it shows how one pet can switch between several forms through transformation sequences shared by its states), download when chosen (see
[Bundled and on-demand pets](#bundled-and-on-demand-pets)). `agent-pet --pet <id>` runs another pet
once without saving the choice. Only the running pet's artwork is loaded.
To add animations to an existing pet without a rebuild, make a [plugin pack](plugins.md) instead.

## A minimal pet

1. Put the art's terms in `licenses/`, for example `licenses/CAT-ARTWORK-TERMS.md`. Packaging, the
   installers and the About dialog carry every file there. Art whose terms do not allow redistribution
   cannot be added.
2. Prepare one idle sequence: a folder of PNG frames, each named with its index and its duration in
   milliseconds, like VPet's: `_000_125.png`, `_001_125.png`, …
3. Scaffold the pet, check it, and run it:

   ```bash
   python3 scripts/new_pet.py cat --name "Cat" --author "Jane Doe" --url https://example.com/cat \
       --terms CAT-ARTWORK-TERMS.md --idle ~/art/cat/idle
   python3 scripts/verify_assets.py
   cmake --build build && ./build/agent-pet --pet cat
   ```

   The catalog has one state per playback shape (`idle`, `busy`, `hello`, `oops`, `bye` and `held`), all
   playing the idle sequence, and maps every cue the app raises onto them, so the pet already works.
4. Add sequences and give states their own art:

   ```bash
   python3 scripts/add_sequences.py --pet cat --source ~/art/cat think/start think/loop think/end
   ```

   Then edit `assets/cat/animations.json`. Add a state for the new sequences, for example
   `"think": ["think/start", "think/loop", "think/end"]` with `"playback": {"think": {"mode": "phased",
   "after": "idle"}}`, and point the cue at it: `"cues": {"thinking": "think", …}`. Set each sequence's
   `state` field to a state that uses it. Run `python3 scripts/verify_assets.py` until it says OK, then
   `ctest --test-dir build -R pets`, which checks every bundled pet against the cues below.

## The folder

```text
assets/<id>/
  pet.json          identity and credits
  animations.json   the catalog, in the same format as VPet's
  preview.png       the Settings tile, at most 512 × 512 pixels
  manifest.json     every frame file with its size and SHA-256
  <folders>/…       PNG frame sequences, one folder each
licenses/<terms>    the art's terms
```

| `pet.json` key | Value |
| --- | --- |
| `schema_version` | `1` |
| `id` | the folder name: lowercase letters, digits and `-`, at most 32 |
| `name` | shown on the tile as written, 1–64 characters |
| `author` | the artwork credit in About, 1–128 characters |
| `url` | optional `https://` page that About links with the credit, at most 256 characters |
| `terms` | file name of the art's terms in `licenses/` |
| `preview` | PNG file name at the folder's root |

Unknown keys are errors. Frames live only in subfolders; the folder's root holds only the files above
(VPet also keeps its `available-animations.json` there). Folder names use `A–Z`, `a–z`, `0–9`, `.`, `_`
and `-`, and do not start with `.`. Frame file names are PNGs without any of `" & ' < > ; \ [ ] : * ? |` or control characters, which
the build and Windows cannot carry. No id, folder or file may use a name Windows reserves (`CON`, `PRN`,
`AUX`, `NUL`, `COM0`–`COM9` or `LPT0`–`LPT9`, also with an extension, as in `nul.png`) or end in `.`.
`new_pet.py` and `add_sequences.py` refuse such names before copying anything, and `verify_assets.py`
rejects them. The catalog's `asset_root` (default `assets/<id>`) must lie inside the pet's folder, and every
frame lies under it; `add_sequences.py` refuses to copy frames for a catalog whose `asset_root` does not.

Each frame folder builds into one resource pack, named after the SHA-256 of its resource path
(`artwork-<sha256 of assets/<id>/<folder>>.rcc`). Editing one sequence therefore replaces only its
pack in updates.

## Cues

The app never asks for a state by name. It raises **cues**, and each pet decides what plays. The
vocabulary lives in [`src/animation/cues.json`](../src/animation/cues.json); the C++ catalog,
`new_pet.py` and `verify_assets.py` read it, and nothing else repeats it.

A **state cue** plays one state, which must have the cue's playback. Without an entry in the catalog's
`cues` section it plays its default state, so a pet with VPet's state names needs no mapping. A pet
whose state cues cannot all play does not start; Agent Pet logs why and shows VPet instead.

| Cue | When | Default state | Mode, then |
| --- | --- | --- | --- |
| `idle` | nothing is happening (cannot be remapped) | `idle` | loop, then idle |
| `waiting` | an agent waits on background work | `idle` | loop, then idle |
| `thinking`, `reading`, `working` | an agent thinks, reads files, runs tools | same name | phased, then idle |
| `attention` | a session needs you | `needs_input` | phased, then idle |
| `exhausted` | the agent's usage limit is reached | `out_of_quota` | loop, then idle |
| `error` | a tool failed | `tool_error` | once, then back to what was playing |
| `turn-finished` | a turn finished, when no `celebrate` pool exists | `turn_finished` | once, then idle |
| `inactive` | every session went quiet | `sleeping` | phased, then idle |
| `nap` | a long quiet spell while idle | `sleeping` | phased, then idle |
| `start` | Agent Pet starts | `starting` | once, then idle |
| `quit` | you quit | `closing` | once, then stop |
| `annoyed`, `quit-angry` | pestered into leaving: the complaint, then the angry exit | `angry`, `closing_angry` | once, then stop |
| `drag` | you drag the pet (needs a state no other cue plays) | `dragging` | phased, then idle |

A phased state has three sequences (start, loop, end) and no `loops` count: it ends when the app moves
on. Art without separate start and end can list the same sequence three times. Cues may share a state.

A **reaction cue** draws one state from a weighted pool, and plays nothing when the pet maps none. Each
state in a pool must end by itself and return to idle (`once`, or `phased` with `loops`). The app never
draws from a pool directly: each of [the pet's own triggers](plugins.md#the-pets-own-triggers) has a
built-in rule that draws from the pool of its cue, and a plugin pack's rules can join it.

A cue is a request, not a promise: the behavior runtime ([0031](adr/0031-behavior-runtime.md)) decides
when it may play, so a surprise waits out an urgent session and a reminder waits until the user is free.
A pet only says how each cue looks.

| Cue | When |
| --- | --- |
| `celebrate` | a turn finished (falls back to the `turn-finished` state cue) |
| `snack`, `milestone` | a treat after a long productive stretch, every hundredth turn |
| `long-turn`, `friday-evening`, `birthday` | how some finished turns are celebrated instead |
| `may20`, `birthday`, `late-night` | special days and hours, among the idle fidgets |
| `monday`, `lunch`, `leave-work`, `sleep` | the configured clock reminders; `lunch` falls back to `snack`, then `celebrate` when unmapped |
| `eye-break`, `water`, `reminder-done` | wellness reminders, and answering one |
| `danger`, `konami` | a destructive command starts; the Konami code |
| `plugin-event` | a plugin pack's [rule](plugins.md#eventsjson) reacted to an event. The rule names its own state, so a catalog never maps this cue |

The `cues` section maps both kinds:

```json
"cues": {
  "attention": "wave",
  "celebrate": [{"state": "cheer", "weight": 2}, {"state": "spin", "weight": 1}]
}
```

Everything else in the catalog is optional: variants, mood art, ambient fidgets, touch, moves and
activity decoration. VPet's catalog and ADRs 0011–0021 show what each does; [ADR 0029](adr/0029-cues.md)
records the cue design.

The catalog's `schema_version` is 2. `python3 scripts/migrate_catalog.py <animations.json>…` brings a
schema 1 catalog up to date: it moves its `reactions` into `cues` under the new names.

## Licensing

Each pet carries its own terms; About shows the running pet's credit, link and terms. VPet's artwork
stays under the VPet artwork terms. Keep `THIRD_PARTY_NOTICES.md` in step when a pet's art comes from
elsewhere.

## Build output and the hash tree

For each pet the build writes `pets/<id>/packs.json` into the build folder and stores it in the index
(`artwork.rcc`) as `assets/<id>/packs.json`, beside the pet's `pet.json`, `animations.json` and preview:

```json
{
  "root": "sha256:…",
  "catalog": "sha256:…",
  "packs": [
    {"name": "artwork-…", "sequence": "idle", "sha256": "…", "bytes": 1234}
  ]
}
```

`root` and `catalog` carry a `sha256:` prefix. Each pack's `sha256` is bare hex, and so are the digests
the two inputs below contain. `sequence` is the pack's frame folder relative to the pet's folder, for
example `vup/Default/Nomal/1` for VPet.

- `catalog` is the SHA-256 of `"pet.json <sha256>\nanimations.json <sha256>\n<preview> <sha256>\n"`, where
  `<preview>` is the preview's file name and each digest is that file's SHA-256.
- Each pack lists the built `.rcc` file's SHA-256 and size; packs are sorted by name.
- `root` is the SHA-256 of `"agent-pet-pet-tree 1\ncatalog <catalog>\n"` followed by one
  `"<name> <sha256>\n"` line per pack, in the same order.

Installed packs are found by name. Pets that are not bundled use the digests: see below. `ctest -R pets`
recomputes every digest from the built files.

## Bundled and on-demand pets

Only the pets in `AGENT_PET_BUNDLED_PETS` (CMake, default `vpet`) install their packs. Every other pet is in
the index with its preview and hash tree, and its packs download when a user chooses it in Settings; the
tile shows the download size, and a progress bar with Cancel shows while it runs. The pet appears on the
next start once every pack is in.

- Downloads come from one long-lived GitHub release, tag `pets`, where each pack is named by its SHA-256
  (`<sha256>.rcc`). The release workflow runs `scripts/pet_blobs.py`, which collects every pack that is
  not bundled, and uploads only the ones that release does not have yet, so app releases never upload
  unchanged pet art again.
- The store is `pets/` in the per-user data folder (see [install](install.md)). A pack enters it only
  after its size and SHA-256 match the leaf in the installed index; a mismatch is fetched once more,
  then the download fails and nothing is kept. Redirects may only go to GitHub's asset hosts over HTTPS.
- `<id>.root` in the store records the tree root last found complete, so after an app update an
  unchanged pet costs one comparison, and a changed one downloads only the packs whose leaves changed.
- A downloaded pack that is missing or damaged when the pet starts is removed, VPet runs instead, and the
  picker offers the download again. Packs no pet in the index uses are removed at startup.

Try a pet as on-demand locally by leaving it out of the bundled list:
`cmake -S . -B build -DAGENT_PET_BUNDLED_PETS=vpet` keeps every pack in the build folder, where it still
counts as installed, but `cmake --install` and the packages carry only VPet's. ADR
[0030](adr/0030-on-demand-pets.md) records the design.
