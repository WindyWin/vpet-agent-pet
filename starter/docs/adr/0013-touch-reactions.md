# 0013. Touch reactions

- Status: Accepted
- Date: 2026-10-05

## Context

Phase 3 of the animation epic: the user should be able to pet, throw and tuck
away the pet. A single click already opens the running-sessions list and a drag
already moves it.

## Decision

Phase 3 of the animation epic. A single click already opens the running-sessions
list, so petting is a press held still for 500 ms (`touch::holdMs`), the same length
that already separated a click from a long press. The optional `touch` catalog section
holds the hit boxes and edge lines from the upstream `vup.lps`, in its 500-unit
artwork space (`scale`), so they follow the pet's size:

- `regions`: ordered rectangles, each with the state a held press plays. The cheek
  (`pinch`) comes first because it sits inside the head (`touch_head`); below is the
  body (`touch_body`). A press elsewhere, such as the feet or a transparent margin,
  pets nothing.
- `fall.left` and `fall.right`: the states for a pet thrown in that direction.
- `edge.left` and `edge.right`: the state for hiding at that edge, and `at`, where the
  screen edge cuts the artwork (219 and 281, from `vup.lps` `side`).

Every one of these states is phased with an open-ended loop, so the player holds it
until released. The drag's hold has become a general one: `Player::hold(state)` plays a
state the user controls, keeps every `select` meanwhile as the state to resume, and
swaps in another held state at once (petting that starts moving becomes a drag).
`release()` plays its end and goes on to the latest request. A touch state is never
resumed, like a fidget, so a pet dragged out of hiding stays out.

`PetWindow` polls the native button and window position every 40 ms during a press,
as before, and keeps the samples. At a release that was not a click, `letGo` measures
the speed over the last 80 ms (`touch::velocity`), so a pet brought to rest first
reads as still. At 900 px/s or more the pet is thrown: the fall state replaces the
drag as the held state, and `touch::Flight` moves the window under gravity, bouncing
softly off the sides of its screen, until it lands on the bottom of the work area.
Then the release plays the landing and whatever sessions asked for meanwhile. A flight
ends after 2.5 s even if it never landed, which covers native Wayland, where the
application cannot move its window. A press catches a falling pet, and hiding,
click-through and quit put it down where it is.

Otherwise, if an eighth of the window is past the left or right side of its screen,
and no other screen continues on that side, an idle pet hides. Its window moves so the
screen edge cuts the artwork at the catalog's line and stays there across resizes and
layout changes. `Monitor` treats a requested touch reaction like a fidget or a nap:
an acceptable look for an idle aggregate, so its periodic update leaves it alone, while
real activity replaces it. It counts from the request, not the entry, because the
drag's 3-second put-down plays first. The pet plays its way out of hiding and moves back
into view; Recover position also brings it out. A pet with work to show is put back
into view instead of hiding. Hidden, it ignores held presses,
because it is only partly there, but it can still be clicked or dragged out.

The setting (`touch` in preferences, default on) switches holds, throws and hiding off
together; dragging and clicking are unchanged. The art is the `Nomal` set only:
`Touch_Head`, `Touch_Body/*_Happy/tb1` (the archive has no neutral body touch, and its
`ill` set is a bed scene), `Pinch`, `MOVE/fall.*` and `SideHide_*_Main`. The
`SideHide_*_Rise` peek when the pointer comes near was left out for size.

## Consequences

- The drag's hold became a general `Player::hold`, reused by every touch state.
- Throws and hiding move the window from the application, so native Wayland may
  refuse them; a flight ends after 2.5 s regardless.
- One setting turns holds, throws and hiding off together.

## Validation

### Touch reactions evidence — 2026-10-05

Imported 21 sequences (184 frames, 25.2 MB) with `scripts/add_sequences.py`:
`Touch_Head/{A,B,C}_Nomal`, `Touch_Body/{A,B,C}_Happy/tb1`, `Pinch/Nomal/{A,B,C}`,
`MOVE/fall.left` and `fall.right` `{A,B,C}_Nomal`, and `SideHide_Left_Main` and
`SideHide_Right_Main` `Nomal/{A,B_1,C}`. The pack is now 702 files, 98,846,610 bytes.
The fall landings join two numbered runs (`FLA_000`…, then `FLB_000`…), so
`add_sequences.py` now orders a folder by name, then index, when its indexes repeat;
a contact sheet confirmed that order lands and then gets up. `SideHide_*_Main/Nomal/B_2`
cannot be imported (its single frame has no index) and is not needed. Left out for
size, and addable with the script: `SideHide_*_Rise` (peeking when the pointer comes
near), Happy and PoorCondition touch and pinch art, and `Touch_Body/Happy_Turn`.

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the 6.5 requirement was
lowered only in the local build): all seven CTest suites passed. New prototype tests
cover the hit boxes at three sizes, held touches keeping later requests and ending
into them, a touch turning into a drag, touch states never being resumed, the release
speed, outer-edge detection with one and two screens, hide positions, the flight's
bounce, landing and time limit, a thrown pet landing on the bottom of the offscreen
screen and then showing the request that arrived mid-flight, hiding only when idle,
the monitor leaving a pet alone from the moment it is asked to hide until activity
brings it out into view, the right edge, dragging out of hiding, Recover position, the setting switching everything off, a real held
press through Qt's test mouse (petting, and still a click when short), the persisted
setting, and refusal of malformed `touch` sections. Still open: judging the hold,
throw and edge thresholds by hand on X11 and XWayland, and CI on Qt 6.5.3.
