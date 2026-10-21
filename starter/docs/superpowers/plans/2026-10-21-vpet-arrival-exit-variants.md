# VPet appearance and exit variants

Status: Implemented 2026-10-21. Live desktop pacing acceptance remains pending.

## Goal and default direction

Vary the animation when VPet first appears and when it quits, using the existing
local VPet archive. Implemented first version: automatic weighted variation with
no new preference. Mood-based pools or a manual selector remain alternatives if
the user prefers them.

Appearance means application startup (`start` → `starting`). Tray show/hide,
screen unlock, edge hiding, and per-turn `turn-start` are outside this change.

## Current behavior

- `starting`: `StartUP/Nomal`, 14 frames, 1,750 ms, once → idle.
- `closing`: `Shutdown/Nomal_1`, 12 frames, 1,500 ms, once → stop.
- `closing_angry`: `Shutdown/PoorCondition`, 13 frames, 1,625 ms.
- A pestered pet plays `annoyed`, pauses for 2,250 ms, then `quit-angry`.
- `Player::choose()` already draws weighted variants on state entry. Mood pools
  override those choices when provided. No new selection engine is needed.
- The behavior runtime allows activity to interrupt startup and bounds shutdown
  to 10 seconds. A hidden pet quits immediately.

## Proposed artwork and weights

Keep the current base sequences. Add these entries to `variants` in
`starter/assets/vpet/animations.json`; weights below include the base choice.

| State | Sequence | Duration | Weight |
| --- | --- | --- | --- |
| starting | StartUP/Nomal (existing) | 1,750 ms | 2 |
| starting | StartUP/Happy | 1,750 ms | 1 |
| starting | StartUP/Happy_1 | 1,375 ms | 1 |
| closing | Shutdown/Nomal_1 (existing) | 1,500 ms | 2 |
| closing | Shutdown/Nomal_2 | 1,500 ms | 2 |
| closing | Shutdown/Happy_1 | 1,875 ms | 1 |
| closing | Shutdown/2/Nomal | 4,125 ms | 1 |

These five imports total 85 frames and 10,556,682 source PNG bytes
(about 10.07 MiB). All have valid timing in the local archive. Preview every
candidate before accepting the pool: names alone do not establish how the
animation feels. In particular, keep the longer 4,125 ms exit only if its pacing
feels acceptable during explicit Quit.

Treat the Happy sequences as cheerful gestures in the automatic pool; this does
not change the mood score. Keep `closing_angry` separate so a pestered departure
retains its meaning. Leave illness and New Year artwork for a later, explicitly
designed mood or seasonal feature.

Selection happens once per state entry, with repeats allowed. Avoid adding
persistent history just to prevent repeats across application launches.

## Implementation steps

1. Preview the five candidates and compare their first/last frames with the
   existing arrival, idle, and exit. Check clipping, placement, transparency,
   and whether `Shutdown/Nomal_2` is visibly distinct from the base sequence.
2. Import approved sequences through `starter/scripts/add_sequences.py` using
   the local `assets/vpet/pet/vup` archive. Keep the generated manifest and
   available-animation inventory consistent; preserve existing artwork notices.
3. Assign imported sequences to `starting` or `closing`, add the weighted
   variant entries, and set base weights through the existing playback schema.
   Preserve `once`/`idle` and `once`/`stop` completion rules.
4. Use the current Player, Stage, and behavior runtime unchanged unless a
   regression demonstrates a necessary fix. Do not add cues, preferences, or
   catalog schema fields for these pools.
5. Add focused deterministic selection and completion coverage, then document
   the feature and validation in the current architecture/user documentation.

## Acceptance and validation

- Force random draws to reach every variant and check weight boundaries.
  Check displayed sequence identity while state remains `starting`/`closing`.
- Each arrival completes once into idle; each normal exit completes once and
  stops. No reselection during a sequence or playback of all variants in a row.
- Activity and urgent alerts still interrupt startup immediately.
- Ordinary quit, annoyed quit, hidden quit, duplicate quit, missing animation,
  and stalled presentation preserve the existing shutdown guarantees.
- Pets without these variant entries retain their own arrival and exit behavior.
- `turn-start` continues using VPet's `Switch/Up/Nomal`; the lifecycle pool must
  not replace per-turn artwork.
- Run `python3 scripts/verify_assets.py` from `starter/`, build affected targets,
  and run the `prototype-*`, `behavior`, and `pets` CTest suites in the configured
  build directory. Use deterministic advancement rather than wall-clock sleeps.
- Perform a desktop walkthrough of every candidate plus startup interrupted by
  activity, normal Quit, and annoyed Quit. Record actual bundle size growth.

## Optional scope changes

If mood-based selection is preferred, use existing `moods` pools and decide
startup semantics explicitly: the current mood score is not restored across
launches. If manual selection is preferred, design preference persistence,
settings labels, and interaction with the existing Mood setting before coding.

## Implementation evidence — 2026-10-21

- Imported all five planned sequences. All 85 PNG files match the original
  archive bytes exactly; original frame durations and artwork terms are retained.
- Inspected first, quarter, middle, three-quarter, and last frames of each
  candidate alongside the base sequences. The two normal box exits have distinct
  expressions. The longer portal exit remains in the pool at weight 1.
- Added deterministic coverage for all ten possible weighted draws, every frame
  of the selected sequence, original total duration, single completion, arrival
  → idle, exit → stop, and the separate per-turn opening artwork.
- Checked all four arrival draws through Stage with activity, attention, and error
  interruption. Pinned the older normal-quit regression to the base random draw.
- Asset verification passes (VPet: 176 sequences). Application and affected test
  targets build successfully with Qt 6.11.2.
- The full local `pets` suite has one unrelated environment failure:
  `bundledPetsMeetTheContract` expects only VPet, while a pre-existing Bocchi
  folder excluded via `.git/info/exclude` is included by CMake's asset scan.
  The local artwork folder was left untouched.
- Live desktop pacing remains unverified; offscreen tests and sampled frames
  establish playback correctness, not the feel of a real startup or quit.

- All four `prototype-*` shards pass after the deterministic normal-quit test
  update; `behavior` passes. Every lifecycle variant and interruption case passes.
- The first full `pets` run passed 58 cases with only the Bocchi assertion
  failing. Subsequent runs passed 55 cases and also failed three existing UI
  import/consent cases (`sourceBrowserCanBeDestroyedDuringTermsConsent`,
  `vpetImportReviewsBeforeInstallation`, `vpetImportCanCloseWhileConverting`).
  These checks remain unresolved; the pet suite is not reported as fully green.
  The cross-pet `everyCuePlaysOnEveryPet` check passes.
