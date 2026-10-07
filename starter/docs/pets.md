# Making a pet

A pet is a folder under `assets/` with its own frames, catalog, preview and art terms. A rebuild picks up
every folder that has a `pet.json`; no C++ or CMake changes are needed. Once two or more pets are
installed, Settings → Pet shows a **Character** row with a tile per pet, and the chosen pet appears the
next time Agent Pet starts. `agent-pet --pet <id>` runs another pet once without saving the choice. Only
the running pet's artwork is loaded.

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

   Every state the app needs now plays the idle sequence, so the pet already works.
4. Add sequences and give states their own art:

   ```bash
   python3 scripts/add_sequences.py --pet cat --source ~/art/cat think/start think/loop think/end
   ```

   Then edit `assets/cat/animations.json`. Point states at the new sequences, for example
   `"thinking": ["think/start", "think/loop", "think/end"]`. Set each sequence's `state` field to a state
   that uses it. Run `python3 scripts/verify_assets.py` until it says OK, then
   `ctest --test-dir build -R pets`, which checks every bundled pet against the core states below.

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
and `-`. Frame file names are PNGs without any of `" & ' < > ; \ [ ] : * ? |` or control characters, which
the build and Windows cannot carry. `new_pet.py` and `add_sequences.py` refuse such names before copying
anything, and `verify_assets.py` rejects them. The catalog's `asset_root` (default `assets/<id>`) must lie
inside the pet's folder, and every frame lies under it.

Each frame folder builds into one resource pack, named after the SHA-256 of its resource path
(`artwork-<sha256 of assets/<id>/<folder>>.rcc`). Editing one sequence therefore replaces only its
pack in updates.

## States the app needs

The app selects these states by name, so every pet defines them with this playback. The list lives in
[`src/animation/core-states.json`](../src/animation/core-states.json). `new_pet.py` and the C++ catalog
read it, and nothing else repeats it. A pet that breaks the contract does not start; Agent Pet logs why
and shows VPet instead.

| State | When | Mode, then |
| --- | --- | --- |
| `idle` | nothing is happening | loop |
| `starting` | Agent Pet starts | once, then idle |
| `thinking`, `reading`, `working` | an agent thinks, reads files, runs tools | phased, then idle |
| `waiting` | an agent waits for background jobs | phased, then idle |
| `needs_input` | a session needs you | phased, then idle |
| `out_of_quota` | the agent's usage limit is reached | loop |
| `tool_error` | a tool failed | once, then back to what was playing |
| `turn_finished` | a turn finished | once, then idle |
| `sleeping` | a long quiet spell | phased, then idle |
| `dragging` | you drag the pet | phased, then idle |
| `angry`, `closing`, `closing_angry` | quitting (after being pestered, normally, angrily) | once, then stop |

A phased state has three sequences (start, loop, end) and no `loops` count: it ends when the app moves
on. Art without separate start and end can list the same sequence three times. Everything else in the
catalog is optional: variants, mood art, reactions, ambient fidgets, touch, moves and activity
decoration. VPet's catalog and ADRs 0011–0021 show what each does.

This contract is provisional. A planned refactor (#64) replaces it with events that each pet maps to its
own animations, and will ship a migration for existing catalogs.

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

Agent Pet uses only the pack names today. On-demand pets (part 2 of #62) will compare a stored root
with the index root, then fetch only the packs whose leaves changed. `ctest -R pets` recomputes every
digest from the built files.
