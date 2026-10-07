# 0015. Easter eggs

- Status: Accepted
- Date: 2026-10-05

## Context

Phase 4 of the animation epic: occasional surprises tied to dates, times of day,
long turns and risky commands.

## Decision

Phase 4 of the animation epic. Every egg plays a named pool from the catalog's
`reactions` section, so the catalog schema is unchanged and a catalog without a pool
skips that egg. `EasterEggs` (`src/animation/easter_eggs.*`) decides when, from a
replaceable local-time clock and random source:

| Egg | When | Pool |
| --- | --- | --- |
| May 20 | That day: the first ambient fidget greets with it, then one later fidget in four | `may20`: `love_520` (`IDEL/happy_like520`) |
| Birthday | The date set in settings (`MM-dd`; February 29 falls on the 28th in other years): the same greeting, and the day's first finished turn | `birthday`: `birthday` (`BDay`) |
| Late night | 01:00 to 05:00: one fidget in two, and one bedtime tooltip a night when a turn finishes (not while muted or hidden) | `late_night`: `fidget_yawn` |
| Monday blues | Monday 06:00 to noon, once a day: a yawn or bored fidget and a tired tooltip (not while muted or hidden) | `monday`: `fidget_yawn` 2, `fidget_boring` 1 |
| Go home | Monday to Friday from 16:45 to 18:00, once a day: a tooltip to get ready to leave | `leave_work`: `fidget_bubbles` |
| Bedtime | From 22:00, once a day: a yawn and a tooltip telling you to sleep | `sleep`: `fidget_yawn` |
| Friday evening | Friday from 17:00: every finished turn | `friday_evening`: `dance` (`Music`) |
| Long turn | A turn of 15 minutes or more from its prompt | `long_turn`: `milestone` 2, `dance` 1 |
| Danger | A tool start flagged `risky` by the hook | `danger`: `startled` |
| Konami | ↑ ↑ ↓ ↓ ← → ← → B A typed while the pet has focus | `konami`: `dance` 2, `love_520` 1 |

Day eggs ride on `Ambient`: before its ordinary draw it asks the eggs for a state,
and shows one it gets like a fidget (it keeps the idle clock, and `Monitor` leaves it
alone). So they need idle animation on, and they come at the usual fidget pace.
Greetings are remembered per day in memory only, so a restart greets again.

Turn eggs ride on `Mood::celebrate(occasion)`. A pending milestone still comes first,
then the occasion's pool, then a pending snack, which otherwise waits for the next
turn. A long turn outranks the birthday, which outranks Friday evening. Turn length is
`Session::lastTurnMs`: the state engine records the prompt's timestamp and measures to
`turn_finished`. A finish without its prompt seen, or after an interrupt, has no
length. Time spent waiting on the user counts.

Danger and Konami are surprises: `EasterEggs::surprise(pool)` selects the drawn state
at once, and `Monitor::update` stops replacing it with the session animation until it
has played out, something else has been shown, or 15 seconds have passed, whichever
comes first. `attention` and `error` still cut in at once. A surprise is skipped while
the pet is held, stopped or hidden, and a danger one while any session waits on the
user or has a fresh error. `startled` reuses the bundled `Pinch` flail with two loops,
so it needs no art of its own. The hook-side check (`destructiveCommand` in
`src/providers/adapters.*`) splits a command line into simple commands, skips
`sudo`-style wrappers and variable assignments, looks inside `bash -c` scripts, and
matches the patterns listed in [integrations](../integrations.md#adapter-policy).
It is a heuristic for an animation:
it errs toward a false alarm, and an obfuscated command passes unnoticed.

Monday, go-home and sleep reminder times are configurable in Settings, in local
time. Preferences store `monday_time`, `leave_work_time` and `sleep_time` as
`HH:mm`, defaulting to `06:00`, `16:45` and `22:00` for older files. Monday is
eligible for six hours after its configured time, go-home for 75 minutes on
weekdays, and sleep until midnight; windows never carry into the next day.
Changing a time does not repeat a reminder already given that day. The go-home
and sleep notes display their configured times. These settings do not change the
late-night or Friday celebration windows or wellness quiet hours.

The setting (`easter_eggs`, default on) and the birthday (`birthday`, omitted when
unset) are saved in preferences. Off, no egg plays and the Konami code does nothing.
`IDEL/Bubbles` (up to 9.7 MB a sequence) was left out of the Konami pool for size, and
hiding behind the screen edge was not used for danger because it would move the window.

## Consequences

- The catalog schema is unchanged; a catalog without a pool skips that egg.
- Day eggs depend on idle animation being on and come at the fidget pace.
- The hook sends only a one-bit `risky` verdict; the command never leaves the hook.

## Validation

### Easter eggs evidence — 2026-10-05

Imported 7 sequences (113 frames, 17.1 MB) with `scripts/add_sequences.py`:
`IDEL/happy_like520`, `BDay/{A,B,C}` and `Music/A/Nomal`, `Music/B/Nomal_1`,
`Music/C/Nomal_1`. The pack is now 815 files, 115,933,286 bytes. Contact sheets
were checked to pick the art: `Say/Self`, `State/StateONE` and `Switch/Up` read as
thinking or sulking rather than startled, so `startled` reuses `Pinch`. Left out for
size, and addable with the script: `IDEL/Bubbles`, `Music/Single` and the Happy and
PoorCondition dance sets.

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the 6.5 requirement was
lowered only in a scratch copy): all seven CTest suites passed. New tests cover the
occasions at their boundaries (late night, Friday evening, February 29 birthdays),
birthday validation, the bedtime note once a night, the Konami code after a false
start, surprises (skipped when off, unknown or held; ended by other states or the time
limit), day greetings then one fidget in four, late-night yawns, birthday before May
20, celebrations by precedence with milestones and snacks, the monitor's long-turn
celebration, startled reaction left alone by the periodic update then replaced by the
work, cut short by attention and withheld while another session waits, the bedtime
tooltip, the Konami code typed on the window, the persisted setting and birthday with
legacy and malformed files, turn length in the state engine, the `risky` field's
validation, and destructive-command detection with the command never leaving the hook.
The frame test now plays every egg state. Still open: whether the cadence feels right
on a real desktop, and CI on Qt 6.5.3.
