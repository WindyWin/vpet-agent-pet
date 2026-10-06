# 0016. Walking, crawling and climbing

- Status: Accepted
- Date: 2026-10-05

## Context

Phase 5 of the animation epic: the idle pet moves around the screen using the
upstream move definitions.

## Decision

Phase 5 of the animation epic. The optional `moves` catalog section ports the
upstream `vup.lps` `move` lines into the artwork's 500-unit space (`scale`). Each move
names a phased state whose loop has a count (upstream `Distance`), and gives:

- `speed`: units per second, eight times upstream's per-frame `SpeedX`/`SpeedY`
  (one 125 ms frame).
- `room`: the space a side needs before the move starts (upstream `Trigger*` with a
  "greater" flag), and `near`: the space a side must be under (the plain flag), which
  is how a climb finds its wall.
- `keep`: the space a side keeps while moving (`Check*`); at that much or less the
  move ends early and plays its end on the spot.
- `wall`: for a climb, the screen edge it clings to and `at`, where that edge cuts the
  artwork (upstream `LocateLength`: 145 from the left, 185 from the right, so 315).
  Contact sheets and alpha bounds put the hands on that line.
- `mood`: `happy` or `poor`, for a move only that mood plays (upstream `ModeType`).

| Move | State | Speed | Starts when | Ends early at |
| --- | --- | --- | --- | --- |
| Walk | `walk_left`/`_right` (7 passes) | 112 | 200 units of room on that side | 100 |
| Trot (happy) | `trot_left`/`_right` (5) | 160 | as walk | 100 |
| Trudge (droopy) | `trudge_left`/`_right` (5) | 80 | as walk | 100 |
| Crawl | `crawl_left`/`_right` (8) | 80 | as walk | 100 |
| Climb | `climb_up_left` … `climb_down_right` (7) | 80 | within 100 of the wall, 200 above or below | 100 above or below |

Moves are ordinary ambient fidgets (`min_idle_s` 240, after the shorter fidgets come
in and before the ten-minute nap), so they keep the idle clock, `Monitor` leaves them
alone, and anything real replaces them at once. Because one moves the window,
`Ambient` asks a gate before letting it into the draw: `PetWindow::canWander` says yes
only when wandering is on, the mood matches, the pet is shown, free (not held,
falling, sliding or hiding) and the move `wander::fits` its room. Without a gate, as in
the headless tests, moves never play.

`PetWindow` notices a move state from `Player::entered` and drives the window with a
33 ms timer while the player is in the move's loop phase and not paused; the start and
end play on the spot. `wander::step` turns speed and elapsed time into pixels for the
pet's size (at most 100 ms of it per tick, so a stalled event loop does not jump), and
the window keeps fractions of a pixel. Once `wander::keeps` fails, `Player::finish()`
plays the move's end at once and then idle. A climb first slides onto its wall during
its start, using the touch slide, and when anything else shows it slides back into
plain view. A drag, Recover position, hiding the pet or turning the setting off stops
a move where it is. A move only measures the screen holding most of the window, so it
never crosses onto another monitor, and an outside move (a screen change) is picked up
instead of fought.

Native Wayland leaves window placement to the compositor, so the gate refuses every
move under the `wayland` platform plugin; the default X11/XWayland session walks.
The setting (`wander` in preferences, default on) is independent of Touch; idle
animation Off stops all fidgets, moves included. The art is the `Nomal` set for walk,
crawl and climb, with `walk.*.faster` (Happy) and `walk.*.slow/B_PoorCondition_1`
(PoorCondition) for the mood walks.

## Consequences

- Moves are ordinary fidgets behind a gate; without a gate (headless tests) they never
  play.
- Native Wayland refuses application placement, so the gate refuses every move there.
- A move stays on the screen holding most of the window.

## Validation

### Walking evidence — 2026-10-05

Imported 30 sequences (136 frames, 18.3 MB) with `scripts/add_sequences.py`:
`MOVE/walk.left` and `walk.right` `{A,B,C}_Nomal`, `walk.*.faster/{A,B,C}_Happy`,
`walk.*.slow/PoorCondition_A`, `B_PoorCondition_1` and `PoorCondition_C`,
`crawl.*/{A,B,C}_Nomal` and `climb.left` and `climb.right` `{A,B,C}_Nomal`. The pack
is now 951 files, 134,272,558 bytes. Contact sheets confirmed the phases (step off,
walk, turn to face front; lie down, crawl, get up; leap, climb, drop to the feet), and
the climb loop's alpha bounds (124–335 units across) match the wall lines. Left out for
size, and addable with the script: the other two `walk.*.slow` loops, Happy and
PoorCondition crawl and climb art, and `climb.top.*` (hanging along the top edge,
which needs the pet to be up there first).

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the 6.5 requirement was
lowered only in the local build): all seven CTest suites passed. New prototype tests
cover distances in artwork units, the room, wall and keep conditions at their
boundaries, steps by size and time with the 100 ms cap, `Player::finish`, the catalog's
moves and pass counts, moves left out of the ambient draw without a gate, the gate
being asked only after four idle minutes and its answer deciding the draw, the
window's gate (room, mood, wall, setting), a walk standing still through its start,
travelling in its loop and stopping short of the edge, the monitor leaving a walk alone
until a prompt ends it, a climb sliding onto its wall at the hands, rising, and stepping
back into view when work starts, Recover and the setting stopping a walk, the persisted
setting with legacy and malformed files, and refusal of malformed `moves` sections;
`verify_assets.py` checks the section too. The frame test plays every move. Still
open: pace and distances by eye on X11 and XWayland, and CI on Qt 6.5.3.
