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
  `scripts/phudo_art/extract.py` cuts every form, the transformation stages and the greatsword off the sheet:
  1. It inpaints the labels and dimension lines.
  2. It upscales 4x with Real-ESRGAN's anime model (`esrgan.py`, CPU).
  3. It mattes with rembg's `isnet-anime`, hardens the matte and removes enclosed background.
  4. It saves at 3x the sheet's resolution. Running it again reproduces the committed cut-outs exactly.
- Bring each drawing to life without cutting it apart:
  - breathing and a lean warp the upper body;
  - the crimson plumes and tendrils sway in a travelling wave, with weapons kept rigid;
  - the drawing's own cyan, violet and red lights are relit: dimmed, blooming, or overheated orange;
  - the visor can flare, and a soft energy halo can surround the figure;
  - the whole figure moves with offsets, rotation, squash and stretch, screen shake and afterimages.
  All forms are scaled to one body height, so changing form does not change the pet's size.
- Every transformation follows the designer's pipeline:
  1. vent plasma and clack the locks open;
  2. pass through the sheet's own stages where it draws them (Phong Lôi to Tứ Thủ and back), or gather energy;
  3. scan the new form in from the crest down behind a bright seam;
  4. ignite with a shock ring, flame on the plume, a flash and a decaying screen shake.
- Each transformation is its own sequence (`tf/<from>_<to>`, 31–38 frames). Phased states share them as their
  start and end phases, so a form change costs no duplicate frames:
  - working is `tf/phong_tu` → claw combo → `tf/tu_phong`;
  - attention goes through Hắc Tháp;
  - flight moves go through Hỏa Dực;
  - the reaction states that need another form are phased with `loops: 1`.
  Every other state starts and ends in Phong Lôi, so states chain without a jump.
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

- The pack is about 135 MB (1,275 files). That is the largest of the drawn pets, because the drawings are
  detailed and the effects cover much of the canvas; it is still smaller than VPet (176 MB). It downloads on
  demand, so only users who choose Phù Đồ pay for it.
- The terms credit the design and artwork to WindyWin. They may be distributed with Agent Pet; other uses need
  the author's permission.
- To change the motion, edit `draw_phudo.py` and render again. To re-cut the art (a new sheet, a new pose), add a
  box to `extract.py` and run it; its tools (torch, rembg, opencv) are needed only for cutting.
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
