# 0037. Kuro and Yun, pets drawn from code

- Status: Accepted
- Date: 2026-10-09

## Context

[0036](0036-fat-cat-pet.md) added the first on-demand pet from borrowed CC0 frames, and showed the limit of that
route: free sprite packs rarely cover the cues (Fat Cat has no sleep, and its single-frame expressions hold
still), and their terms or sizes rule most of them out. Most catalog features that VPet's art happens to fill,
such as touch throws, edge hiding, moves with a vertical component and activity handovers, had never been used by
a second pet, so nothing showed that a pet could use them in its own way.

## Decision

- Add two pets whose art is original and drawn from vector shapes by a script, so the script is the source and
  the art is dedicated to the public domain (CC0):
  - **Kuro** (`assets/kuro/`, `scripts/draw_kuro.py`, [terms](../../licenses/KURO-ARTWORK-TERMS.md)), a chibi
    ninja with its own crescent-moon emblem: 60 sequences, 664 frames, about 11 MB.
  - **Yun** (`assets/yun/`, `scripts/draw_yun.py`, [terms](../../licenses/YUN-ARTWORK-TERMS.md)), a chibi
    xianxia cultivator in a black and crimson robe: 79 sequences, 843 frames, about 18 MB.
- `scripts/pet_art.py` holds what both scripts share: the 1000 × 1000 canvas with VPet's ground line, cairo
  helpers, common props (sparkles, hearts, smoke, confetti, sleep letters) and the writer. The writer
  merges consecutive identical frames into one longer frame and saves 8-bit palette PNGs with alpha, a third of
  the RGBA size; the flat-coloured art does not band. Drawing needs `pycairo` and Pillow; the app does not.
- Both pets stay out of `AGENT_PET_BUNDLED_PETS` and download when chosen, like Fat Cat.
- Motion follows written briefs. Each animation shown in the gallery has a prompt in the style of the Kling, Seedance
  and Runway guides (one action, ordered and timed beats, a keep-list, a short negative list) and sprite-timing rules
  (anticipation, slow-in and slow-out, holds, squash on landing). `pet_art.py` provides `tween` (keyframes with easing
  and per-frame holds) and `follow_through`, which the writer applies to every sequence: scarves, ribbons, hair and
  mantles drag behind the body's vertical motion one frame late and settle after a landing.
- Neither pet borrows a design from another work. Requests to draw characters from existing franchises were
  answered with these original designs instead.

Kuro answers every state cue with its own art: think (a hand seal and a thought bubble, with a light-bulb
variant), read (a scroll), work (a laptop, with a fast-typing variant), the "!" bubble and a wave for attention,
a soot puff and dizzy stars for an error, a drained battery for the quota, a jump with sparkles for a finished
turn, a nap with sleep letters, appearing and vanishing in smoke for start and quit, and a dangle for a drag. It
has happy and poorly idles, head and body touch, eight reaction states covering every reaction cue, and two ninja
dashes as moves.

Yun is where the catalog is pushed further:

| Feature | Yun's use |
| --- | --- |
| Activity loops and handovers ([0021](0021-active-animation.md)) | Thinking is meditation with orbiting qi and a turning formation (a lotus variant in Playful); reading is a jade slip floating by the head, glyphs flowing into the forehead (a bamboo-scroll variant); working is alchemy at a cauldron (a pill-forming variant in Playful). Reading and working hand over directly: the slip fades as the cauldron rises, and back. |
| Moves with a vertical component ([0016](0016-walking.md)) | Six flying-sword moves: level flight, a climbing soar and a descending dive to each side. Climbs need 300 units of room above; dives stop 10 units above the work area's bottom. |
| Touch fall and edge ([0013](0013-touch-reactions.md)) | Thrown, Yun tumbles, summons the sword and rides it down, landing with a salute. At a screen edge it hides behind a cloud and peeks out. |
| State cues | A lightning tribulation for a tool error, qi deviation for the quota, a breakthrough with a light pillar for a finished turn, a nap sitting on a cloud, descending on the sword to start and flying off to quit, a red aura for annoyance and vanishing in a whirl of leaves. |
| Reactions and fidgets | Petal rain, flute, bursting into butterflies and gathering back (rare idle, Konami), immortal peach, tea, tai chi, a sword dance. |

## Consequences

- The picker shows four pets. The `pets` release gets 60 + 79 more packs at the next release.
- The art can be changed by editing a script and re-rendering; `add_sequences.py` copies the frames in. Moving or
  renaming a sequence folder renames its pack, so users download it again.
- Yun's moves can leave it in mid-air after a soar; the next dive, a drag or Recover position brings it down,
  as with VPet's climbs.

## Validation

- 2026-10-09, Ubuntu 24.04 container: `python3 scripts/verify_assets.py` reported
  `OK: fat-cat (9 sequences), kuro (60 sequences), vpet (141 sequences), yun (79 sequences)`.
- In a scratch copy built against the distribution's Qt 6.4.2 (the version requirement and two `QDataStream::Qt_6_5`
  references lowered for the experiment only), `ctest --test-dir build -j8` passed all 16 tests with Kuro; after
  Yun was added, `pets`, `plugins` and `pet-scaffold` passed again, and `build/pets/` held packs for both
  (60 and 79). The real CI on Qt 6.5+ has not run yet.
- `agent-pet --pet kuro|yun --no-persist --state thinking|working` under Xvfb drew Kuro with its thought
  bubble and laptop and Yun meditating and at the cauldron, at the same size and ground line as VPet.
- Rendering `draw_kuro.py` again after the shared kit was split out produced byte-identical frames.
- Open: throws, edge hiding, moves and handovers by hand on a desktop; macOS and Windows CI; the first download of
  either pet through the `pets` release.
- 2026-10-09, after the prompt-driven rework (32 animations rebuilt, every frame re-rendered for follow-through):
  `verify_assets.py` reported `OK: fat-cat (9 sequences), kuro (60 sequences), vpet (141 sequences), yun (79 sequences)`
  (Kuro 655 files, 10.8 MB; Yun 815 files, 17.3 MB), and `pets`, `plugins` and `pet-scaffold` passed again on the
  scratch Qt 6.4 build. Yun's tribulation flash is a radial glow that fades before the frame's edge, so the
  transparent window never shows a square.
