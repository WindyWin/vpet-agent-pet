# 0011. Idle variants and ambient fidgets

- Status: Accepted
- Date: 2026-10-04

## Context

A pet that loops a single idle sequence looks static. The archive has idle
variants and many short fidgets that can play while nothing else happens.

## Decision

Phase 1 of the animation epic ([issue 12](https://github.com/WindyWin/vpet-agent-pet/issues/12)).
`animations.json` gained three optional parts; catalogs without them load as before.

- `playback.<state>.weight` and a top-level `variants` map give a state extra,
  weighted ways to play. Each variant has the same number of sequences as the state
  (one, or three for start/loop/end). One is drawn when the state is entered, and an
  idle loop draws again on every pass, so it can alternate. `playback.<state>.loops`
  makes a phased state end by itself after that many passes of its loop part.
- `ambient.fidgets` lists the states that may play while idle, each with a `weight`,
  an optional `min_idle_s` and an optional `rare` flag. A fidget must end by itself
  (one-shot, or phased with `loops`) and return to idle; the loader refuses others.
  `ambient.sleep_after_s` is the idle time before the pet dozes off.
- Fidgets are ordinary states named `fidget_*`, so the preview and the state menu
  play them. The player always replaces one at once, never waiting out its exit, and
  never resumes one after an error or a drag.

`Ambient` (`src/animation/ambient.*`) schedules them. It runs on the player's idle
loop signals, so a paused or hidden pet does nothing, and it keeps an idle clock that
any non-idle, non-fidget state resets. After a random gap (45–90 s Subtle, 15–25 s
Lively) the next idle pass starts a fidget: with a 1 in 30 chance from the rare
pool, otherwise by weight among the fidgets whose `min_idle_s` has passed, skipping the
one before unless it is the only one eligible. Past `sleep_after_s` it starts `sleeping` instead. Off draws no numbers,
plays no fidgets and also turns idle variants off. `Monitor` treats a fidget or an
ambient nap as an acceptable way to show an idle aggregate, so its periodic update
does not cut them; real activity still replaces them. The random source and the clock
are injectable, which is how the tests script every draw.

## Consequences

- Catalogs without the new sections load as before.
- Fidgets are ordinary states, so the preview and state menu can play them, and real
  activity always replaces them at once.
- Random draws and the clock are injectable, so tests script every draw.

## Validation

### Idle animation evidence — 2026-10-04

Added `scripts/add_sequences.py` and imported 15 sequences (112 frames, 16.1 MB)
from the full archive: `Default/Nomal/2` and `/3` as idle variants, the `aside`,
`Boring`, `yawning` and `Squat` fidgets (two middle variants for `aside` and
`Squat`) and `Meow/Nomal/1` as the rare one. The pack is now 327 files, 43,470,164
bytes; the offscreen binary that embeds it is 44 MB. `verify_assets.py` also checks
variants, weights, loops and the ambient section. Choices left out for size can be
added with the script: `IDEL/meowlook/Nomal` (4.2 MB), `IDEL/Tennis/Nomal` and
`IDEL/Bubbles` (up to 9.7 MB a sequence).

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (requirement still 6.5;
the temporary override was not committed): all five CTest suites passed, repeatedly.
New prototype tests script the random draws and the clock to cover weighted
variants and their loop-by-loop switching, variants off, fidget phases and loop
counts, immediate replacement by real activity, return to idle after an error or a
drag, the Subtle and Lively gaps, idle-time tiers, the rare pool, no repeats, the
idle clock, the ten-minute nap and its wake-up, Off drawing nothing, the monitor
leaving fidgets and naps alone, the persisted preference and settings control, and
refusal of malformed variants and fidgets. Removing the monitor guard makes the
monitor test fail, so it is exercised. A first version of the loop re-pick skipped
variants-off and left an alternate loop playing after switching to Off; a test now
covers it. The offscreen `--smoke-test` passed. Still open: judging the pacing by
eye on a real desktop, and CI on Qt 6.5.3.
