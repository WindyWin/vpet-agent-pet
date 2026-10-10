# 0038. Phù Đồ, a transforming mecha from a contributor's sheet

- Status: Accepted
- Date: 2026-10-10

## Context

[0037](0037-drawn-pets.md) showed that a pet can be drawn entirely by a script. A contributor, WindyWin, wanted a
mecha pet of their own design. Two earlier attempts were rejected and removed before this one:
- a vector redraw from a written brief, which looked nothing like the design;
- a chibi version rigged as a cutout puppet, judged too plain and with too few states.

The contributor then supplied a full game-asset sheet, "Chiến Giáp Phá Đá Đế (mecha code Buddha)":
- four combat forms of one suit:
  - **Phong Lôi**, the balanced vanguard with the Phương Thiên Họa Kích polearm;
  - **Hỏa Dực**, the aerial assault form with the Đại Pháo Ray cannon;
  - **Tứ Thủ**, the four-armed berserker;
  - **Hắc Tháp**, the heavy fortress with Aegis shields and cannons;
- the three drawn stages of a transformation;
- the weapon arsenal.

They also gave a four-phase transformation pipeline to follow:
1. Mở chốt (disengage and vent): the stance drops, the locks clack open, plasma and smoke vent.
2. Dịch khớp (bone articulation): the sub-arms unfold, the head dips and the eyes flare.
3. Khóa giáp (armour snap): the weapons separate or join, and the armour locks into place.
4. Kích hoạt (ignition): the hydraulics lock with a thud, flame bursts from the plume, the screen shakes, and the
   new form settles into its battle idle.

## Decision

- Add **Phù Đồ** (`assets/phudo/`, `scripts/draw_phudo.py`, [terms](../../licenses/PHUDO-ARTWORK-TERMS.md)). It
  downloads when chosen, and replaces the earlier Long pet entirely.
- Animate the designer's own drawings; draw nothing of the suit by script.
  - `scripts/phudo_art/extract.py` cuts every form and the greatsword off the sheet:
    1. It inpaints the labels and dimension lines, and also the drop shadow where a label covers the drawing.
    2. It upscales 4x with Real-ESRGAN's anime model (`esrgan.py`, CPU).
    3. It mattes with rembg's `isnet-anime` and removes enclosed background.
    4. It regrades the cut-outs (`grade.py`): it pulls them off the blueprint's blue haze into deep indigo steel,
       deepens the blacks, restores the palette's saturation, adds local contrast and sharpens the ink.
    The cut-outs are kept at the full 4x resolution.
  - `make_plates.py` splits each form into 52–80 armour plates with a watershed walled by the drawing's ink lines.
  - Both scripts reproduce the committed files exactly.
- Bring each drawing to life without repainting it:
  - breathing and a lean warp the upper body;
  - the crimson plumes and tendrils sway in a travelling wave, with weapons kept rigid;
  - the drawing's own cyan, violet and red lights are relit: dimmed, blooming or overheated;
  - the visor can flare;
  - the whole figure moves with offsets, rotation, squash and stretch, screen shake and afterimages.
  All forms are scaled to one body height.
- Transform piece by piece, as in Transformers and Iron Man, following the designer's four phases:
  1. **Unlock:** the locks clack open, plasma vents, and every plate opens out from the chest with energy light
     in the seams.
  2. **Articulation:** each plate of the new form flies in from the matching plate of the old one. The plates are
     paired by position through an optimal assignment. On the way a plate arcs round a glowing core, spins, and
     flips over: the old plate turns edge-on and the new one comes face-on. The legs go first and the head last,
     with a spark as each lands. Plates the new form lacks fold into the core; extra ones (Tứ Thủ's sub-arms) grow
     out of it.
  3. **Armour snap:** the plates close onto the body and lock.
  4. **Ignition:** a shock ring, flame on the plume, a flash and a decaying screen shake.
  A quick version of the same morph is used for the form cycle and for snapping into Hỏa Dực when thrown.
- Each transformation is its own sequence (`tf/<from>_<to>`, 43 frames). Phased states share them as their start
  and end phases, so a form change costs no duplicate frames:
  - working is `tf/phong_tu` → claw combo → `tf/tu_phong`;
  - attention goes through Hắc Tháp;
  - flight moves go through Hỏa Dực;
  - the reaction states that need another form are phased with `loops: 1`.
  Every other state starts and ends in Phong Lôi.
- Cue mapping (86 sequences, 45 states):

| Cue or feature | Phù Đồ |
| --- | --- |
| Idle | Phong Lôi breathing, its plumes swaying. Variants: crackling lightning, venting, a visor sweep. Moods: happy (bouncing, sparkles), poor (dark, flickering). |
| Thinking, reading | A strategy hologram (a plan variant); a scanned text sheet (a page-flick variant). |
| Working | Transforms into Tứ Thủ: four-arm slash combos with blade streaks and hit-stop. Playful variant: the Đại Trảm Đao materialises and cleaves. Also a flame roar. Reading and working hand over through the transformation. |
| Attention | Transforms into Hắc Tháp: the cannons charge under a red beacon. |
| Error, quota | Overheating, arcs and shaking, a dead slump with smoke, a flickering reboot; for the quota, sagging and dark with a blinking cell. |
| Turn finished | Lightning strikes the polearm twice, a jump with fireworks, a landing shock. |
| Sleep, start, quit | Standby and a flare-up wake; Hỏa Dực streaks down and transforms on landing; it transforms and launches away. Anger: an overheated transformation, a roar in flames, then a dash or a jet blast. |
| Touch | Dragged (pendulum swing), patted (heart hologram), poked (sparks and twitches). Thrown: a spin that snaps into Hỏa Dực, hovering on jets, transforming back down. Peeking at an edge. |
| Moves | Hỏa Dực flight (level, climbing, diving) entered and left through the transformation; a ground dash with afterimages. |
| Reactions and fidgets | Phong Lôi Kích (lightning into the polearm, a charged lunge); Đại Trảm Đao summoned and swung; Tứ Thủ Loạn Trảm; the Đại Pháo Ray beam from Hỏa Dực; a Hắc Tháp artillery barrage; the Aegis barrier dome (danger); a war drum of the anchors; a full form cycle (milestones, Konami); refuel, coolant, vent, a heart, a bow, a systems calibration of every form. |

## Consequences

- The pack is about 206 MB (1,429 files), more than VPet (176 MB). The regraded, sharper drawings compress less
  than the first soft cut, and the piece-by-piece transformations are long. It downloads on demand, so only users
  who choose Phù Đồ pay for it.
- The terms credit the design and artwork to WindyWin. They may be distributed with Agent Pet; other uses need
  the author's permission.
- To change the motion, edit `draw_phudo.py` and render again. To re-cut the art (a new sheet, a new pose), add a
  box to `extract.py`, run it and `make_plates.py`. Their tools (torch, rembg, opencv, scikit-image) are needed
  only for cutting.
- The sheet is 1024 × 637 pixels, so each form was drawn about 240 pixels tall. Upscaling cannot recover detail
  that is not there; a higher-resolution export of each form would make the pet sharper still.
- The drawings are single views, so a form never turns round. Hỏa Dực is drawn facing left, so rightward flight
  mirrors it.

## Validation

### Phù Đồ evidence — 2026-10-10

- Ubuntu 24.04 container. `python3 scripts/verify_assets.py` reported
  `OK: fat-cat (9 sequences), kuro (60 sequences), phudo (86 sequences), vpet (141 sequences), yun (79 sequences)`.
- Scratch copy built against the distribution's Qt 6.4.2. For the experiment only, the version requirement and the
  `QDataStream::Qt_6_5` references were lowered.
  - `pets`, `plugins` and `pet-scaffold` passed after the new pack was built.
  - With Phù Đồ, Kuro and Yun added to `everyCuePlaysOnEveryPet` in that copy, every state cue played and every
    reaction in every pool could be selected.
- Rendering `draw_phudo.py` again, and running `extract.py` again, produced byte-identical files.
- Contact sheets of the transformations, attacks, flight and reactions were checked by eye. A reel of the form
  changes was sent to the designer.
- Open:
  - the designer's verdict;
  - throws, edge hiding and moves by hand on a desktop;
  - CI on Qt 6.5+, macOS and Windows;
  - the first download through the `pets` release.

### Piece-by-piece transformations and regrade — 2026-10-10

- The designer asked for crisper, more vivid art, and for transformations that assemble part by part like
  Transformers or Iron Man.
- The cut-outs were regraded and kept at 4x. Each form was split into armour plates, and the scan-line
  transformations were replaced by the plate morph described above. The sheet's three stage drawings are no longer
  used.
- `verify_assets.py` reported `OK: ... phudo (86 sequences) ...`. In the scratch Qt 6.4.2 build, `pets` (with Phù
  Đồ in `everyCuePlaysOnEveryPet`), `plugins` and `pet-scaffold` passed.
- Re-rendering a transformation and re-running `make_plates.py` reproduced the committed files byte for byte.
