# 0038. Long, a contributor's painted design rigged as a cutout puppet

- Status: Accepted
- Date: 2026-10-10

## Context

[0037](0037-drawn-pets.md) showed that a pet can be drawn entirely by a script. A contributor, WindyWin, then
supplied an original chibi warlord mecha. It is not based on any existing kit or franchise. The design is a sheet
of four painted poses:
- standing with a spear;
- a lunge;
- a fist at the chin;
- a cheer with the spear raised and a shout.

The design is fixed by what is painted:
- a gold crown with a cyan gem;
- a black helm with glowing cyan eyes;
- layered gold-rimmed pauldrons with cyan gems;
- violet crystals behind the shoulders;
- red segmented tendrils hanging at the sides;
- violet thigh plates and red shin fins;
- a black and gold spear with a steel head.

The first attempt (commit `bd49d4c`) redrew the design from a written brief as vector shapes in the style of
Kuro and Yun. The designer rejected it as looking nothing like the sheet. Flat vector plates cannot carry a
painted look.

The second attempt used the four painted poses whole, moving them as rigid images. The designer asked for full
animation instead: the character itself moving, not four pictures sliding about.

## Decision

- Animate the designer's own pixels; draw nothing of the character by script.
  - The sheet is kept as the source: `scripts/long_art/design-sheet.png`.
  - Its four poses were matted with rembg's `isnet-general-use` model.
  - `scripts/long_art/split_poses.py` splits them apart (`pose1.png`…`pose4.png`). Where a spear lay over the
    next figure, it goes to its owner and what it hid is inpainted.
- Rig the standing pose as a cutout puppet, as Spine or Live2D would. `scripts/long_art/make_rig.py`:
  - cuts it into 13 parts by polygons: head, two arms, spear, two thighs, two shins, two boots, two tendril
    clusters and the body. The tendrils take everything beside the torso, and only red lacquer where they cross
    it;
  - gives each part a few pixels of overlap past its cut, drawn under the part in front, so joints show no seam;
  - inpaints what a part hid on the body;
  - drops crumbs of other parts;
  - writes `rig/rig.json` with the draw order and the joints.
- `scripts/draw_long.py` poses the puppet each frame:
  - the head turns about the neck, the arms about the shoulders, and the spear about (and slides through) the
    fist;
  - the upper body leans and shifts about the hips;
  - the legs follow two-bone IK from the hips to wherever the feet are put, bending outwards, with the boots kept
    flat and a foot that cannot be reached hanging from the straight leg;
  - the tendrils sway in a travelling wave and fan out.
- The other three poses are used whole for the moments the sheet paints. The switch from one drawing to another
  happens on a squash: the old pose dips and the new one springs past rest.
- The script also relights the drawing's own cyan gems and eyes and its violet crystals. It can dim them, turn
  them red, or make them bloom with a halo past the outline.
- It adds effects in the sheet's palette, laid out in the same units as before:
  - a strategy hologram and a scan sheet with a beam from the eyes;
  - sparks, strike flashes and spear swooshes;
  - shockwaves and cracks, a red beacon, fireworks and confetti;
  - violet afterimages and jets, steam from the vents, a heart hologram, a blush, a diagnostic sweep.
- Cue mapping (83 sequences, 41 states):

| Cue or feature | Long |
| --- | --- |
| Idle | Breathing through the knees with the head, free arm and spear following a beat late and the tendrils swaying. Variants: fist to the chin with a "?" (the sheet's pose), a crystal surge, a weight shift that lifts and plants the spear. Moods: happy (bouncing, fist pumping), poor (sagging, gems flickering). |
| Thinking | The sheet's fist-at-chin pose with a hologram projected from the eyes; a variant moves a marker along the plan. |
| Reading | The free hand comes up, a text sheet unfolds beside it, the eyes scan it line by line, or flick pages. |
| Working | Spear drills from a guard: thrusts slide the spear through the fist with a lunge, a strike flash and afterimages. A variant twirls the spear like a windmill. Reading and working hand over directly. |
| Attention | Up into the sheet's cheer (spear raised, shouting) with a red beacon. |
| Error, quota | Every joint twitches under crawling arcs and red flashes, the light dies, a slump, a reboot that straightens him up. For the quota, sagging onto the spear with the head hanging and a red cell blinking. |
| Turn finished | A crouch, a jump that becomes the cheer, fireworks, a landing with a shockwave and cracks. |
| Sleep, start, quit | Asleep on his feet: the eyes fade, the head nods down; waking snaps it up. Dropping in on jets with knees tucked and landing in the lunge; launching out of the top of the frame. Annoyance: red eyes, flared tendrils, a shaking fist and a stomp, then a dash away or a jet blast. |
| Touch | Dragged: a pendulum swing with dangling, kicking legs. Patted: the head tips into the hand. Poked: a ticklish twitch. Thrown: a spin with flung limbs, the jets catch him in the lunge, a heavy landing. At an edge he leans out with his fist at his chin. |
| Moves | Jet flight (level, climbing, diving) leaning forward with the spear levelled and the legs trailing, and a low dash in a lunge with afterimages and dust. |
| Reactions and fidgets | A spear kata (sweep, lunging thrust, twirl, planted butt), checking the spear with a glint, a crystal surge, venting steam, a startled jump into guard, a heart hologram, birthday, refuelling with an energy cell, coolant mist, a shy blush, a joint-by-joint calibration under a diagnostic sweep, a war-drum rhythm with the spear. |

## Consequences

- Long's frames carry the painting's texture, so they compress far less than flat art. The pack is about
  83 MB (896 files), larger than Kuro (11 MB) and Yun (17 MB) but smaller than VPet's painted frames (176 MB).
  Long downloads on demand, so only users who choose it pay for that.
- Long's terms differ from Kuro's and Yun's: the design and artwork are WindyWin's. They may be distributed with
  Agent Pet; other uses need the author's permission.
- To change the motion, edit `draw_long.py` and render again. To change a cut or a joint, edit
  `long_art/make_rig.py` and run it, then render again.
- New painted poses from the designer can be added to the sheet. They are cut the same way, and a new view
  could even be rigged.
- The rig cannot turn the body round or show its back, because a single front view has neither. Large rotations
  of a part would show the inpainted areas, so the motion keeps joints within the range a front view supports.
- The drawing tools need `numpy` and `scipy`; cutting needs `opencv-python-headless`, and matting needs `rembg`.
  The app needs none of them.

## Validation

### Long evidence — 2026-10-10

- Ubuntu 24.04 container. `python3 scripts/verify_assets.py` reported
  `OK: fat-cat (9 sequences), kuro (60 sequences), long (83 sequences), vpet (141 sequences), yun (79 sequences)`.
- Scratch copy built against the distribution's Qt 6.4.2. For the experiment only, the version requirement and the
  `QDataStream::Qt_6_5` references were lowered.
  - `pets`, `plugins` and `pet-scaffold` passed after the new pack was built.
  - With Long added to `everyCuePlaysOnEveryPet` in that copy, every state cue played and every reaction in
    every pool could be selected.
- Rendering `draw_long.py` again produced frames byte-identical to the committed ones.
- Contact sheets were checked:
  - the rest pose matches the sheet with no seams;
  - crouches, raised arms, spear spins and leg steps show no holes or stray strips;
  - effects stay inside the canvas apart from deliberate exits.
- A test reel was sent to the designer for review.
- Open:
  - the designer's verdict on the motion;
  - throws, edge hiding and moves by hand on a desktop;
  - CI on Qt 6.5+, macOS and Windows;
  - the first download through the `pets` release.
