# 0036. Fat Cat, the first on-demand pet

- Status: Accepted
- Date: 2026-10-09

## Context

[0028](0028-pet-packs.md) made a pet a folder under `assets/`, [0029](0029-cues.md) let each pet answer the app's
cues in its own art, and [0030](0030-on-demand-pets.md) made pets outside `AGENT_PET_BUNDLED_PETS` download when
chosen. No second pet existed, so the picker, the hash tree and the download path had never run on real art.
A pet needs art that may be redistributed in the app's releases and in the long-lived `pets` release, and it
needs enough frames to answer the cues without looking like a placeholder.

## Decision

- Add **Fat Cat** (`assets/fat-cat/`) from megupets' Orange Fat Cat pack, which is CC0 and ships individual
  transparent PNG frames. Other candidates were dropped: Pet Cats Pack (CC0, but 20 × 14 px sprites and a
  purchase-flow download), Catgirls and Chibi Animals (no redistribution), 16-Bit Kitty (CC-BY).
- It stays out of `AGENT_PET_BUNDLED_PETS`, so installs and updates carry VPet only; the cat's nine packs
  (about 11 MB) go to the `pets` release through `scripts/pet_blobs.py`.
- Frames are re-exported with one scale onto 1000 × 1000 canvases like VPet's. The sequences use different
  canvas offsets, so each frame is placed in the idle pose's coordinates first; the falling `Dead` frames are
  centred on the body and rest on the idle feet line. Frames are 8-bit: the first 16-bit export was 30 MB
  against 11 MB. The prep script is not kept: the art is read-only input, and `manifest.json` records what ships.
- Cue mapping. The pack has no sleeping art, so `nap` and `inactive` stay awake (idle).

  | Cue | State | Art |
  | --- | --- | --- |
  | idle, reading, nap, inactive | `idle`, `calm` | open eyes, a short blink |
  | thinking | `ponder` | TSK (side-eye) |
  | working | `work` | Walk |
  | attention, drag, quit | `alert`, `held`, `bye` | Jump (the cat waves) |
  | error | `oops` | Dead, once, then back |
  | exhausted | `zombie` | the zombie cat |
  | turn finished, celebrate | `cheer`, `grin` | HAPPY, HI |
  | start | `grin` | HI |
  | annoyed, quit-angry | `angry` | GRUMPY, then stop |

## Consequences

- The picker shows two pets, so Settings → Pet → **Character** appears for the first time in a default build.
- Single-frame expressions hold for 1.6 s; they do not animate. A later pack can give them motion.
- Fat Cat's art is CC0, so unlike VPet's it carries no notice duties; the credit stays in About and in
  `THIRD_PARTY_NOTICES.md`.
- The `pets` release gets its first blobs at the next app release; until then choosing the cat from an
  installed release cannot download it.

## Validation

- 2026-10-09, Arch Linux, Qt 6, KDE Wayland session (the pet under XWayland): `verify_assets.py` reported
  `OK: fat-cat (9 sequences), vpet (141 sequences)`. `ctest --test-dir build -j4` passed all 16 tests, among them
  `pets` (which recomputes every digest) and `pet-scaffold`.
- `scripts/pet_blobs.py build <out>` collected nine on-demand packs for `fat-cat` and none for `vpet` (136 bundled).
- `./build/agent-pet --pet fat-cat --no-persist` showed, by events through `emit` and `hook`: the idle cat,
  thinking (TSK), working (walk), a permission request (the wave and the "Needs approval" bubble), a quota stop
  (the zombie with "Usage limit reached"), a tool error (the fallen cat) and a finished turn (HI). The cat kept its
  size and ground line between animations.
- Open: the first upload to the `pets` release and a download of the cat through it, macOS and Windows CI runs,
  and Settings' tile, size label and Cancel with this art.
