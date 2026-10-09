# 0038. Long, a mecha pet from a contributor's design

- Status: Accepted
- Date: 2026-10-09

## Context

[0037](0037-drawn-pets.md) showed that a pet can be drawn entirely by a script. Kuro and Yun are small, round chibi
figures, so they did not test a tall, many-part figure with long appendages and a weapon. Those need more than the
shared kit: limbs that bend correctly in every pose, parts that move independently, and effects that stay inside
the 1000 × 1000 canvas.

A contributor, WindyWin, supplied a design sheet and a written brief for an original warlord mecha. It is not
based on an existing kit or franchise. The brief fixes the following:

- **Silhouette:** a gold crest of horns, red plumes on the back and a halberd longer than the suit.
- **Proportions:** a small head, shoulders about 1.6 times the hips, long legs.
- **Colours:** a black and silver base. Gold appears only on the crest, the chest trim and the joint accents; violet
  only on the shoulders and thighs; red only on the plumes, the shin blades and the halberd head; cyan only as one
  small light per shoulder.
- **Parts:** the plumes are segmented, are not wings, and attach to a backpack frame rather than to the shoulders.
  The halberd head comes off its shaft. The left hand is a bare mechanical hand.

Some earlier requests in the same work asked for characters from existing franchises; those were declined, and
this design is the contributor's own.

## Decision

- Add **Long** (`assets/long/`, `scripts/draw_long.py`, [terms](../../licenses/LONG-ARTWORK-TERMS.md)): 83
  sequences, 894 frames, about 45 MB. Like Kuro and Yun, it downloads when chosen.
- Credit the design to its author.
  - The terms let anyone copy and distribute the frames with Agent Pet, changed or not.
  - Other uses of the design need the author's permission, so Long is not CC0 like Kuro and Yun.
  - `pet.json` credits "WindyWin (design), Agent Pet contributors".
- Draw the suit from faceted plates on a two-bone rig.
  - Elbows and knees take whichever inverse-kinematics solution points outwards, so crouches and raised arms keep
    a wide mecha stance.
  - The whole suit draws at 0.7 scale (`BODY`), which leaves room for the halberd and plumes.
  - A `spin` field turns the suit about its waist for tumbles.
- Give the parts the brief describes their own motion.
  - **Plumes:** four chains of 18 lacquer segments pinned to the backpack frame. They arc up and out. They respond
    to `wind` and `flutter`, to `spread` (a pheasant display), to `droop` (power loss) and to the shared
    `follow_through` drag.
  - **Plumes reaching (`reach`):** the plumes on the side of a target leave their arcs on a curve, come over the
    shoulder and work there as extra hands. This is used for the forge and for covering the face.
  - **Halberd head (`blade`):** detaches and flies on its own path. Thinking, working and a fidget take it off and
    click it back with a spark.
  - **Halberd grip (`w_grip`):** the hand moves along the shaft so the halberd can spin about its middle or be
    raised without leaving the canvas.
  - **Crystal jets:** violet jets under the crystal hinges carry flight and landings.
  - **Afterimages:** violet silhouettes of the suit where it just was, for dashes, tumbles and take-offs.
- Map every cue to Long's own art:

| Cue or feature | Long |
| --- | --- |
| Thinking | A strategy hologram projected from the open left palm: rings, nodes lighting in turn, links drawing themselves; a variant moves a red marker along the plan (Playful). |
| Reading | A floating text sheet that the visor scans line by line with a red beam; a variant flicks through pages. |
| Working | The halberd head flies to the left, two plumes reach over the shoulder and forge it with white-hot sparks; a welding variant flashes (Playful). Reading and working hand over directly: the sheet folds as the head lifts off, and back. |
| Attention | The halberd raised high with its head glowing, a red "!" beacon pulsing over the crest. |
| Error, quota | A short circuit: the core flares, arcs crawl over the suit, the plumes spasm, a slump, then a reboot with flickering eyes. For the quota, a low squat leaning on the halberd with the plumes trailing on the floor and a red battery blinking. |
| Turn finished | Two eased twirls of the halberd with a swoosh, the blade raised to fireworks, then the butt slammed down: shockwave, ground cracks, plumes flared. |
| Sleep, start, quit | Standby (eyes dark, plumes folded, drifting Z marks) and a boot-up; landing on the jets with afterimages and a shockwave; a lift-off out of the top of the frame. Annoyance ends in a dash off-screen, an angry quit in a jet blast upwards. |
| Touch | Patting the crest: happy eyes and wagging plumes with a small heart hologram. Poking the chest: the core flickers and the suit twitches. Thrown: a spin about the waist with afterimages, the jets catch it, a three-point landing. At a screen edge it leans out and scans. |
| Moves | Jet flight (level, climbing, diving) to each side with the halberd held forward, and a low ground dash with afterimages and dust (speed 300, needs 260 units of room). |
| Reactions and fidgets | A halberd kata (sweep, lunge, twirl), checking the halberd head, the plume display, venting steam, a startled jump into guard, a heart hologram, birthday fireworks and confetti, refuelling with an energy cell, coolant mist, covering the face with the plumes, a joint calibration routine, a war-drum rhythm with the halberd. |

## Consequences

- Long is the largest drawn pet at about 45 MB, because its plates are shaded and it covers more of the canvas.
  The download is on demand and pack by pack, so only users who choose Long pay for it.
- The `pets` release gets 83 more packs at the next release.
- Long's terms differ from Kuro's and Yun's: a pack based on Long's design must keep its terms and credit.
- To change the art, edit `draw_long.py` and render again. As with every pet, renaming a sequence folder renames its
  pack.

## Validation

### Long evidence — 2026-10-09

- Ubuntu 24.04 container. `python3 scripts/verify_assets.py` reported
  `OK: fat-cat (9 sequences), kuro (60 sequences), long (83 sequences), vpet (141 sequences), yun (79 sequences)`.
- Scratch copy built against the distribution's Qt 6.4.2. For the experiment only, the version requirement and the
  `QDataStream::Qt_6_5` references were lowered.
  - `ctest --test-dir build -j 2` passed all 16 tests.
  - With Long, Kuro and Yun added to `everyCuePlaysOnEveryPet` in that copy, `pets` passed: every state cue plays
    and every reaction in every pool can be selected on each of them.
- Rendering `draw_long.py` again produced frames byte-identical to the committed ones.
- Contact sheets of each sequence were checked:
  - The halberd, plumes and effects stay inside the canvas, apart from deliberate exits (dash, take-off, landing).
  - The colour rules of the brief hold.
- Open:
  - Throws, edge hiding and moves by hand on a desktop.
  - CI on Qt 6.5+, macOS and Windows.
  - The first download through the `pets` release.
