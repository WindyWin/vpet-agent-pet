# 0053. Turn choreography: a getting-ready beat

- Status: Accepted
- Date: 2026-10-17

## Context

The pet shows session states exactly as they happen, so an agent turn
(prompt → thinking → tools → finished) snaps between loops with no sense of
beginning. Starting a new task looks like any other `thinking` or tool state,
and watching the pet it is hard to tell when one task ended and the next began.

ADR 0052 gave each turn tool-aware activity states, but nothing marks its first
moment. [Issue #107](https://github.com/WindyWin/vpet-agent-pet/issues/107)
proposes a small, optional choreography on top of the existing states: a one-shot
beat when a prompt opens a new turn, and a wrap-up beat before a turn finishes.
This record covers the first half (the start beat).

## Decision

Add one **state cue** to the schema-2 vocabulary, `turn-start`, default state
`turn_start`, playing `once` and then idle, with `fallback: "start"` (ADR 0052's
degradation rule). VPet draws it from `Switch/Up/Nomal` (13 frames, ~1.9 MB).

`Monitor::apply` raises the beat once per accepted prompt, right after the
update that submits the turn's aggregate. It is submitted as a **Reminder-class
one-shot moment** from the `lifecycle` source, so it is a moment rather than a
session state:

- Attention, an exhausted quota and an error are Urgent session activity; the
  Reminder gate (`gate()`) refuses while `attention != 0` or the pet is handling
  a hold, hidden, mute or speaking, and Urgent activity interrupts a showing beat.
  A new prompt therefore never interrupts a session that needs the user.
- A destructive command's `danger` surprise is a higher rank (`Surprise`), so it
  cuts the beat and still startles the pet.
- A finished turn's celebration (`Celebration`) and the tool-aware activity
  states settle in after the beat plays out.

Other pets need no change: they have no `turn_start` state, so `stateFor`
resolves `turn-start` through the `start` fallback to their arrival state, which
shares the cue's `once`/`idle` shape and satisfies the contract. Mapping real art
is a `turn_start` state; a `cues` entry is only needed for a different name.

## Consequences

- VPet gains one state (162 sequences, 1,546 files, 224.33 MiB) and a new beat;
  every other pet plays its own arrival animation at each prompt until it draws
  one, and none needs a catalog edit.
- The beat is data-driven and optional like every cue, and the behavior runtime
  keeps deciding whether it may play.
- The wrap-up beat from the issue is not implemented: `turn-finished`/`celebrate`
  remain the exit, and a separate wrap-up moment can follow this cue's pattern.
- `Monitor` submits one more intent per prompt; sessions, the recap and the wire
  protocol are untouched.

## Validation

### Start beat, tests and assets — 2026-10-17

Linux, Qt 6.11.2 (offscreen, xcb). `python3 scripts/verify_assets.py` reports
every pet valid (VPet now 162 sequences). `ctest --test-dir build -j 4` passes
all 17 suites, including the new `PrototypeTests::monitorOpensEachTurnOnce` (a
prompt plays `turn_start` once, then the activity; attention is not interrupted
and an error preempts the beat) and the catalog fallback check in
`pets_tests.cpp` (no `turn_start` state resolves `turn-start` to `starting`).
The vocabulary rises to 41 cues; `pets` and `prototype` shards were updated for
the new state and the beat in the monitor tests. `check_translations.py` passes
(the beat has no text). The wrap-up beat stays open.
