# 0021. Active animation styles

- Status: Accepted
- Date: 2026-10-06

## Context

[Issue 47](https://github.com/WindyWin/agent-pet/issues/47): thinking, reading and
working loop one sequence each, and switching between them plays a full end and
start. See the [design spec](../superpowers/specs/2026-10-06-active-animation-design.md).

## Decision

[Issue 47](https://github.com/WindyWin/agent-pet/issues/47). The optional `activity`
section of `animations.json` decorates states that end only when asked (phased, no
`loops`, not idle, dragging, a fidget or a touch state); the shipped catalog decorates
`thinking`, `reading` and `working`. Decoration changes which sequence plays inside a
state, never the state itself, and a state without an entry plays as before.

- `loops`: weighted alternate loop passes, `style` `subtle` (default) or `playful`.
- `enter`: a first loop pass drawn from `choices` when entered from a state in `from`.
- `exit`: per target state, an end sequence that replaces the usual one.
- `linger`: a non-urgent request for `to` keeps the pet at its desk: `in`, passes of
  `loop` (never the same twice in a row), then `out`. It ends at the first pass
  boundary at or after `max_s` (1–60) and goes on through the state's end, as before.
  Linger time counts frames shown, so a paused pet never runs it down.
- `handover`: per target state, a sequence that swaps props at the desk and enters the
  target's loop directly.

Composite sequences under `WORK/Desk/` reuse frames already in the pack: the two
handovers swap book and paper while both are raised, and `ponder_*` is the chin-in-hand
start of the pen-spin sequence.

`Player` owns the mechanics. `looped` is now also emitted for every new pass of a phased
loop, and `vary(sequence)` swaps one alternate into that pass (it reverts at the next).
`setContinuity` switches linger and handover; while one plays, `phase()` reports
`linger` or `handover`, and `requestedState()` already reports the request, so
`Monitor` sees it honoured and changes nothing. `setReactionGate` is asked whenever an
`enter` or `exit` could play. Resuming the lingering state returns to its base loop;
asking for a handover target plays `out` then the handover; anything else cuts to the
end at once. During a handover, the destination of the playing swap stays fixed,
while `requestedState()` tracks the latest request. A request for reading, working or
thinking finishes that swap, then returns, lingers or stays at the destination desk
as requested. This can delay a flip-back by at most the remaining 1.25 seconds of the
swap. Other requests cut to the usual end; urgent selects and `hold` interrupt
immediately. Pause freezes playback.

`Activity` (`src/animation/activity.*`) paces it. One opportunity clock runs from the
first activity state to the last; when it is due, the next loop pass plays an alternate
(Subtle: `subtle` tier every 10–18 s; Playful: both tiers every 6–12 s), and in Playful
the gate spends the same opportunity on a reaction instead, so a transition gets at most
one reaction and quick tool calls do not each get one. Classic draws nothing and leaves
continuity off. The style is the `activity` preference (default Playful), independent
of Idle animation, and changes the policy for the next request immediately; a
handover already playing can finish at its destination. `Think/Happy` and `WorkONE/Happy` art is a
Playful expression here, not mood art.

## Consequences

- Decoration changes which sequence plays inside a state, never the state; undecorated
  states play as before.
- A handover can delay a flip-back by at most 1.25 seconds; urgent states still
  interrupt immediately.
- Classic keeps the previous behavior.

## Validation

### Active animation evidence — 2026-10-06

The pack contains 13 new sequences (126 original files, 15,802,577 bytes):
`Think/Nomal/B_2`–`B_5`, `Think/Happy/B_3`, `B_4`, `C_2`,
`WORK/Study/B_2_Nomal`–`B_4_Nomal`, `WORK/WorkONE/B_2_Nomal`,
`WORK/WorkONE/Happy/B` and `WORK/WorkONE/B/Nomal/1`. Five composite
`WORK/Desk/*` sequences reuse bundled frames. The importer accepts the archive's
byte-identical duplicate index and unprefixed frame name.

`python3 scripts/verify_assets.py` passed: 1,077 original artwork files,
133 sequences, 150,075,135 bytes. It also rejected all six broken catalog variants:
bad style, linger to idle, `max_s` 61, an idle entry, an unused handover sequence and
an unknown sequence.

Validation: the Release build with Qt 6.11.2 passed all eight CTest suites
(`100% tests passed out of 8`, 93.96 seconds; prototype 87.78 seconds).
The initial sandboxed run could not bind local sockets; the final complete run used
local socket access. `git diff --check` also passed.

The prototype regressions cover catalog validation, alternate decoding, loop passes,
linger/resume/expiry, handovers, shared reaction pacing, style changes, Classic
compatibility, preference persistence and monitor updates. The random request test
uses 600 activity-weighted steps, records every displayed frame and requires both
handovers, linger, ponder out and both reactions. A deterministic thinking-to-working
tail covers the welcome reaction, which the handover behavior now avoids in the
seeded random run. A separate regression checks that a handover lands before following
the latest desk request, with alerts, errors and dragging interrupting immediately.

The live desktop walkthrough under Classic, Subtle and Playful has not been performed
in this continuation. Perceived cadence and visual smoothness remain manual checks;
the automated checks exercise state, sequence, timing and Settings behavior offscreen.
