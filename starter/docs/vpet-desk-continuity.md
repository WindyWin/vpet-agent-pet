# VPet desk continuity artwork audit — 2026-10-22

Updated 2026-10-25: the player now reads the pose metadata and waits for entrances
and standing performances to reach the desk before switching props. Automated
playback and asset validation are recorded in ADR 0052. A live desktop check
remains separate from sequence correctness.

## Source audit

Inspected all frames of the seven tool entrances/exits, sampled every ordinary
loop, and inspected the WorkONE pen-spin endpoints. VPet has no tool mood
replacements: happy/poor moods use these same tool choices. Thinking's standing
mood/alternate loops are **not** seated sources.

| State | Ordinary loop | Prop | Seated source coverage |
| --- | --- | --- | --- |
| working | WorkONE/B_1_Nomal | pen/paper | base, B_2_Nomal, Happy/B, pen spin |
| reading | Study/B_1_Nomal | green book | base, B_2_Nomal, B_3_Nomal |
| editing | Calligraphy/Nomal/B | brush and scroll | base, pen spin |
| executing | WorkTWO/B_1_Nomal | computer | base, pen spin |
| searching | StudyTWO/Nomal/B | open red book | base, pen spin |
| browsing | StudyPaint/Nomal/B | standing easel | **art gap**, no seated entrance/loop/exit |
| delegating | PlayONE/Nomal/B | computer/headphones | base, pen spin |
| seated thinking | Think/Extended/desk_loop | pen | tail loop only; full performance lands here |

The six seated tools share the desk geometry. Their entrances first carry and
tilt the table (frames 0–5); only frames 6 onward introduce props at a stationary
desk. Their exits first remove props, then tilt/carry the table away. Seated
swaps use only these stationary parts. Calligraphy's longer exit rolls up the
scroll before leaving the desk. Full entrances/exits remain available for real
arrival/departure and Classic playback.

Exit slices (Python exclusive end): working `[:3]`, reading `[:4]`, editing
`[:17]`, executing `[:4]`, searching `[:3]`, delegating `[:3]`. Destination
entrance slices are `[6:]`. Pen-spin overrides use `WORK/Desk/ponder_out`
before the new prop: the old tool's props must not reappear first. The existing
`ponder_in` introduces seated thinking. These are shared frame references;
no additional PNGs, manifest changes or license changes are necessary.

## Complete directed coverage

S = seated swap; F = original end/start fallback (art gap); — = same state.
Thinking means the **seated** thinking tail, never any arbitrary thinking loop.

| From ↓ / to → | working | reading | editing | executing | searching | browsing | delegating | thinking |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| working | — | S | S | S | S | F | S | S |
| reading | S | — | S | S | S | F | S | S |
| editing | S | S | — | S | S | F | S | S |
| executing | S | S | S | — | S | F | S | S |
| searching | S | S | S | S | — | F | S | S |
| browsing | F | F | F | F | F | — | F | F |
| delegating | S | S | S | S | S | F | — | S |
| thinking | S | S | S | S | S | F | S | — |

Of 42 directed tool pairs, 30 have seated coverage and 12 require the full standing arrival/departure.
Of 14 tool/thinking directions, 12 have seated coverage and 2 require the full standing arrival/departure.
The standing easel cannot convincingly be made seated by concatenating frames;
its full arrival/departure is deliberate. Its full resume to seated thinking
remains available, and subsequent requests wait for that clip to land. Its existing Playful pen-spin alternate still
plays, but does not opt the entire browsing activity into seated transitions.

## Reproduction and visual review

Run `python3 scripts/compose_desk_continuity.py` from `starter/` to reproduce the
catalog additions. `compose_long_thinking.py` also calls this composer, so it
cannot restore the old exit + entrance resumes. Both reuse licensed frames.

For moving previews, save a baseline catalog before editing, then run:

```sh
python3 scripts/preview_desk_continuity.py --baseline /tmp/vpet-before.json --output /tmp/vpet-desk-continuity.html
```

The standalone HTML includes all 56 directed pairs, ordinary/Playful source
poses, original frame timings, a slower playback option, and frame/sequence
labels. Inspect prop removal/arrival, table edges, loop joins and real exit
behavior. A frame matrix or passing sequence assertions alone cannot establish
smoothness. Current preview artifact: `/tmp/vpet-desk-fix/preview.html`.
