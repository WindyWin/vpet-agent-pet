# Desktop architecture and validation

Decision recorded 2026-10-04: C++17, Qt 6 Widgets, CMake and Ninja.
Develop in this starter directory. No original VPet installation is used.

Platform extraction: [plan and completion criteria](platform-refactor-plan.md),
[services and build registration](../src/platform/README.md).
Phases 1–3 (host registry, focus service and desktop backends; see
[session focus](#session-focus)) are implemented; IPC, startup, updater and build
seams remain. It prepares extension points while retaining current Linux behavior;
it does not add support for another operating system or desktop environment.

Qt provides translucent top-level windows, native move requests, input-transparent
windows and tray menus with a small native application layer. The prototype uses
software QWidget painting, without a browser or language interpreter at runtime.
The artwork pack and catalog are built as a separate binary Qt resource file,
`share/agent-pet/artwork.rcc` (beside the executable in local builds). The player
registers it using [QResource](https://doc.qt.io/qt-6/qresource.html), preserving
the existing resource paths. Artwork terms and application notices remain embedded.
Move the whole bundle together. The RCC uses format 1 without compression to omit
source timestamps and keep identical artwork reproducible across checkouts.

## Boundaries

| Directory | Responsibility |
| --- | --- |
| `src/desktop` | Transparent pet, alert toast, running-sessions list, Open through the focus service, attention badge, context/tray menu, tray status and hiding, drag, touch gestures (`touch`), walking geometry (`wander`), scale, input and quit |
| `src/animation` | Catalog validation, phased playback with weighted variants and mood art, the idle fidget scheduler (`ambient`), the mood score and celebrations (`mood`) and bounded decoded-frame cache |
| `src/settings` | Validated, atomic preference storage in the user data directory |
| `src/sessions` | Bounded session/tool state, ordering, aggregate activity, alerts and alert labels, and the widget-free show/hide/idle rules (`presence`) |
| `src/ipc` | Private Unix transport, headless hook/emit commands, and hook-side autostart with the `autostart` command |
| `src/providers` | Claude/Codex normalization and integration configuration management |
| `src/hosts` | Host adapters: the neutral `HostContext` and protocol v1 conversion, the registry (capture, target codecs, labels, detection order), the session focus service, and Konsole/tmux/herdr selection |
| `src/platform` | Small contracts for native services (`contracts/`), Linux process and command services (`linux/`), X11 and KWin desktop backends and pointer queries (`desktop/`), explicitly unavailable services (`unsupported/`), and the composition point `native.h` |

The catalog defines eleven display states: idle, thinking, reading, working,
needs_input, tool_error, turn_finished, sleeping, starting, closing and dragging.
`idle` loops; the activity states and dragging use start → held loop → end;
starting, error, finished and closing are one-shot sequences. Error returns to
the previous activity, finished/starting return to idle, and closing holds its
last frame until the application exits. Ordinary state changes finish an active
end sequence. Urgent changes interrupt it immediately. The preview can select
all states, pause, step frames and show the active sequence, phase and timing.

The 35 unmodified `Raise/` frames imported for dragging were checked against the
full archive's SHA-256 manifest. `Raise/Raised_Static/A_Nomal` starts the lift,
`Raise/Raised_Dynamic/Nomal/1` loops while the button is held, and
`Raise/Raised_Static/C_Nomal` lowers the pet before returning to the prior state.
On X11/XWayland a pointer query detects release even if the compositor consumes
the widget's mouse release event. Native Wayland still needs direct testing.

The current pixmap and an 8 MiB `QCache` hold decoded frames for the active
sequence. A sequence change clears the cache. Source dimensions above 2048 px
are rejected; images are scaled to at most 640 px before entering the cache.
A decode error stops the bad sequence and falls back to idle; if idle is broken,
a visible text placeholder and controls remain. Catalog paths, durations and
playback policies are validated before playback. This is a cache bound, not a
guarantee on process RSS, which also includes Qt, source decoding and graphics
memory.

`PreferencesStore` writes `preferences.json` atomically under Qt's
`AppDataLocation`; malformed files are preserved and defaults are used. Size,
position and on-top survive restart. The startup keys `autostart` and
`when_idle` may be written by the display-free `agent-pet autostart` command
while a pet runs, so the pet re-reads the file before every save instead of
overwriting them; the hook reads `autostart` without linking any GUI code. Off-screen positions are brought inside an
available monitor; screen geometry changes trigger recovery. No session state is
persisted; only the [daily recap](#daily-recap)'s counters are, in `recap.json` beside it. The settings window remains open only on request and closing it keeps
the pet running.

## Idle animation

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

## Mood

Phase 2 of the animation epic. Two more optional catalog sections:

- `moods.happy` and `moods.poor` map a state to choices that replace its usual
  choices and variants while the pet is in that mood. Each choice has the state's
  shape and a weight. A state without mood art plays as usual. The player draws at
  entry, so a held state keeps its art until entered again, while an idle loop picks
  up a new mood on its next pass. Mood art applies even with idle animation Off,
  through its first choice.
- `reactions` names weighted pools of states: `turn_finished` (the ways to celebrate
  a finished turn), `snack` and `milestone`. Like fidgets, these states must end by
  themselves and return to idle.

`Mood` (`src/animation/mood.*`) keeps a score from 0 to 100 that starts at 50.
`Monitor` feeds it every accepted `turn_finished` and `error` event from any session,
so duplicates and stale callbacks of an interrupted turn do not count. A finished
turn adds 4 plus its streak length (capped at 6 extra); an error costs 12 and ends the
streak. The score moves one point back toward 50 every 30 seconds. Happy starts at
70 and lasts down to 60; poorly starts at 30 and lasts up to 40, so the mood does not
flicker. Four finished turns in a row make the pet happy, two errors from neutral
make it droopy. The setting caps it: Off is always neutral, Cheerful only never
droops.

When the aggregate becomes `turn-finished`, `Monitor` asks `Mood` what to play: a
pending treat if one is due, else a weighted draw from `reactions.turn_finished`. A
snack is due after 20 finished turns with no gap over 30 minutes between two of them.
A milestone is due on every hundredth finished turn ever seen, so the count is saved
in preferences (`turns`) along with the setting (`mood`). A treat earned while
another session's state outranks the celebration waits for the next one.

The upstream `Eat`, `Drink` and `Gift` animations are two layers with a food or
gift image drawn between them, keyframed by `info.lps`. That image is not in the
archive, and the renderer draws one layer, so the snack plays `Switch/Hunger` or
`Switch/Thirsty` instead, and the milestone plays `LevelUP/Happy`.

## Touch reactions

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

## Easter eggs

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
| Go home | Monday to Friday from 16:45 to 18:00, once a day: a tooltip to get ready to leave | `leave_work`: `fidget_aside` |
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
matches the patterns listed in [integrations](integrations.md#adapter-policy).
It is a heuristic for an animation:
it errs toward a false alarm, and an obfuscated command passes unnoticed.

The setting (`easter_eggs`, default on) and the birthday (`birthday`, omitted when
unset) are saved in preferences. Off, no egg plays and the Konami code does nothing.
`IDEL/Bubbles` (up to 9.7 MB a sequence) was left out of the Konami pool for size, and
hiding behind the screen edge was not used for danger because it would move the window.

## Daily recap

The pet sums up the day's agent work in its `NoteBubble`: "Today: 38 turns across 3
projects · 2 approvals waited 10+ min · longest run 22 min". `Recap`
(`src/sessions/recap.*`, in `pet_core`) counts per local day from the events
`Sessions` accepted, so duplicates and stale callbacks of an interrupted turn never
count. `Monitor::apply` hands it each accepted event with the session's record after
it, and the local date of arrival:

| Counter | From |
| --- | --- |
| Turns, per project | `turn_finished`; the project is the folder name of the session's path (up to 64 a day) |
| Longest run | `Session::lastTurnMs` of those turns, as the long-turn egg measures it |
| Errors | `error` |
| Approvals | `attention` without `reason: input`, once per request until the session moves on |
| Waits | From the request to the session's next accepted event that leaves `attention`; 10 minutes or more counts as long. A session that ends while waiting records no wait |

Subagents end with `session_end`, never `turn_finished`, so only top-level turns
count. Turns, errors and requests count on the day they arrive, a wait on the day it
is answered. Pending requests are kept in memory only, so a restart forgets them.

`RecapStore` writes `recap.json` beside `preferences.json` with `QSaveFile`, two
seconds after the last change and when monitoring stops. It holds 14 days, oldest
dropped first, each with dates, counts and folder names only: no paths, providers,
session IDs, prompts or tool content. Only the pet writes it, so unlike preferences
there is no re-read before saving. A missing or invalid file (bad version, dates out of
order, negative or non-integer counts, over 1 MB) loads as empty and is replaced by the
next save. `--no-persist` and the smoke test use no file.

Right-click → **Today's recap** says the summary; clicking that bubble swaps in the
breakdown (turns per project, busiest first, then errors, approvals with the longest
wait, and the longest run) and keeps it up for 15 seconds. The menu works while muted.
A hidden pet shows the breakdown as a tray notification instead. The go-home reminder
(`leave_work`, weekdays from 16:45) appends the summary when the day has finished
turns and the `recap` preference (default on, Settings → **Recap**) allows it; it
follows the reminder's own rules, so easter eggs must be on and the pet visible and
unmuted.

## Walking

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

## Wellness reminders

Issue #38. `Wellness` (`src/desktop/wellness.*`) is a plain value class that counts
active time with time passed in, and `Monitor` drives it:

- **Activity.** Every accepted event (`Monitor::apply`) and every pointer move seen by
  the 250 ms update (`Monitor::pointer`, `QCursor::pos` by default, replaced in tests)
  calls `Wellness::activity(now)`. A gap between activities counts up to one minute,
  so reading or thinking still counts while a longer pause holds the timers; a gap of
  five minutes or more is a real break and resets both. Only pointer moves and
  `prompt` events are the user's own; other agent events count only while the user was
  seen within five minutes, so an agent working for an absent user pauses and then
  resets the timers. While the screen is locked nothing counts, no reminder shows, one on screen (or its eye-break countdown) goes away without a cheer, and
  both timers start over until the user is seen again (`Wellness::reset`):
  `platform::ScreenLock` (`pet_native`, `src/platform/desktop/screensaver/`) follows
  the `ActiveChanged` signal of `org.freedesktop.ScreenSaver` and
  `org.gnome.ScreenSaver` after one asynchronous `GetActive`, and `main.cpp` hands it
  to `Monitor::locked` (replaced in tests). Keyboard input outside the pet is not seen:
  X11 idle time (XScreenSaver) would need another `pet_native` seam and was left out.
- **Due.** An interval (`eye_minutes` 0/20/30/45, `water_minutes` 0/45/60/90; 0 is
  off; defaults 20 and 60) of counted time makes a reminder due, eyes first. Only the
  intervals are saved in `preferences.json`; an interval that is not a choice falls
  back to the default.
- **Calm.** A due reminder waits until the user was seen within the last minute
  (`Wellness::present`, which also holds it behind a locked screen), the pet is visible and free (not held,
  walking, flying or mid-surprise), alerts are not muted, no alert bubble or note
  shows, and no session waits on the user or has a fresh error. During quiet hours
  (22:00 to 06:00 on the eggs' clock) it is dropped instead, so the morning does not
  open with a backlog. Muting holds it, standing in for the focus mode of issue #34.
- **Showing.** The pet plays a catalog pool through `EasterEggs::surprise(pool, true)`,
  which plays even when easter eggs are off, and says the note in the `NoteBubble`.
  Showing counts as given: ignored, it fades and the timer starts over. The bubble's
  new `clicked` signal answers it: water plays `reminder_done` at once; an eye break
  first counts 20 seconds down in the note (clicking again ends it), then plays
  `reminder_done` with a short note.
- **Snacks.** A finished turn celebrated with a state from the `snack` pool already
  shows the thirsty or hungry art, so it counts as the water reminder and restarts that
  timer.

| Pool | State | Art |
| --- | --- | --- |
| `eye_break` | `fidget_yawn` | `IDEL/yawning`: eyes closed, a long stretch |
| `water` | `snack_thirsty` | `Switch/Thirsty`, shared with the snack |
| `reminder_done` | `cheer_shy` | `Say/Shy` |

The yawn stands in for an eye rub. The archive's `Drink` set is a layered food animation (cup drawn between a back and a
front layer), which the player does not compose, so no new art was imported.

## Window behavior

The pet requests a borderless, translucent, always-on-top tool window. Left drag
uses `QWindow::startSystemMove`, with coordinate movement as a fallback. A press that
leaves the pet in place is a click under 500 ms and a touch from then on (see
[touch reactions](#touch-reactions)); a throw and hiding at an edge move the window
from the application, which native Wayland may refuse. Right-click
opens controls. Walking and climbing move the window from the application too (see
[walking](#walking)) and are off under native Wayland. Space switches preview state, Menu opens controls and Escape quits
while the pet has focus. These are local shortcuts, not global desktop bindings.

The pet and tray share one menu. Its top level keeps the everyday actions (Show pet,
running sessions, today's recap, mute, always on top, Settings, Quit) plus an update
entry only while an update is waiting; previews, click-through, recovery, Updates… and
About sit under More. Settings is three tabs (General with alerts and reminders, Pet
with idle behavior, Startup and agents), with the status line, Updates, Quit and Close
below them, so the dialog fits small screens.

Click-through remains a 15-second lease. A single-shot
timer restores input whether or not a tray exists. The tray menu (under More) can immediately
recover input and position, or quit. The context menu also quits. Closing the pet
quits; closing About does not. Position recovery moves to the primary screen's lower right. Saved positions
are clamped to an available monitor on startup and layout changes.

When DISPLAY is set, the default backend is `xcb`, including under Wayland through
XWayland. Set `QT_QPA_PLATFORM=wayland` explicitly for native Wayland experiments
using a development Qt installation. The M2 package includes xcb and offscreen
plugins only; it does not claim native Wayland support.

Qt documents compositor requirements for [translucent windows](https://doc.qt.io/qt-6/qwidget.html#creating-translucent-windows).
Native Wayland cannot be assumed to honor application positioning, raising, or
always-on-top hints. Native move requests depend on a real input event and the
compositor. See [QWindow movement](https://doc.qt.io/qt-6/qwindow.html#startSystemMove)
and [window flags](https://doc.qt.io/qt-6/qt.html#WindowType-enum).

## Session focus

Open, from the bubble or a session row, returns to the application hosting an
existing session. It never starts a terminal or editor, or resumes an agent.
Three concerns are kept apart:

| Concern | Code | Links |
| --- | --- | --- |
| Host capture: detect the host in a hook's environment, decode its target, name it | `hosts::Registry`, `hosts/adapters/<host>.cpp` | Qt Core only; used by `hook` |
| Host activation: select the tab or pane, name the windows to raise | `hosts::Activation` per host, `hosts::FocusService` | Platform contracts; Konsole's half needs D-Bus and lives in `pet_native` |
| Desktop activation and observation: raise or check a native window | `platform::DesktopBackend`: `x11`, `kwin` | `pet_native`, the only target linking X11 and D-Bus |

`hosts::HostContext` is the internal descriptor: adapter ID, process hints nearest
first, an optional backend-qualified window reference (`{"x11", "<id>"}`) and target
data only the adapter decodes. Protocol v1 fields convert to and from it at the
event boundary (`hosts::fromV1`/`toV1`); a non-X11 window is never written into
`host_window`. `Event::parse` asks the registry which host IDs exist, and the
session list asks it for labels. Sessions store the descriptor and know no host names.

Capture order is registration order: herdr, tmux, Konsole, VS Code, then any
terminal (needs ancestors). Programs inherit their terminal's variables, so a
`Capture` may name its program: herdr, tmux and Konsole count only when one of the
hook's ancestors has that kernel name (`/proc/<pid>/comm`; any name is accepted when
the ancestry is unreadable), and VS Code never takes `$WINDOWID`. `FocusService::focus` copies the context (the monitor
copies the session, because KWin's callback processes events), checks the target
with the adapter's codec before any side effect, selects, then offers each window
the adapter names to each backend in order until one raises it. Selection and
activation are reported separately as `Skipped`, `Unsupported`, `MissingTarget`,
`TargetNotFound`, `Failed`, `TimedOut`, `Requested` or `Confirmed`; only a raised
window (`Requested` or `Confirmed`) dismisses the session's bubbles, as the former
boolean did.

| Host | Selection | When selection fails | Windows tried, in order | Active-window bubble suppression |
| --- | --- | --- | --- | --- |
| Konsole | D-Bus `setCurrentSession` | Raises anyway | Its hints | Yes |
| tmux | `select-window`, `select-pane` | Stops (also when `tmux` is missing) | Attached clients' ancestors, then its own process hints, as one request | Never: `Unknown` |
| herdr | `herdr tab focus`, `herdr agent focus` | Stops | Each live UI client of the same API socket, after selecting the Konsole tab it runs in; then its own hints | Never: `Unknown` |
| VS Code, terminal | None | — | Its hints | Yes |

**Hooks that run outside the terminal.** Codex runs hooks from a shared
`codex app-server` daemon whose parent is `systemd --user`, not the pane. The hook
sees no `herdr` ancestor, so it reports a bare `terminal` host, and the daemon's
inherited `HERDR_PANE_ID` names whichever pane first started it, so it is never
trusted. The hook must stay fast and never block, so it does not look further. At
Open time, a `terminal` host whose session has a provider goes through the
`FocusService` locators: the herdr locator runs `herdr agent list` and takes the
agent whose `agent` equals the provider and whose `cwd` equals the session's
project. With several matches it takes the focused one; if that is still ambiguous
(or the lookup fails) it keeps the captured host and guesses nothing. The result is
an ordinary herdr host, so selection and client raising work as above, and the
captured pids stay as hints. Resolution is read-only and never sent over the wire,
so v1 fields are unchanged.

A malformed target is `MissingTarget`: nothing is selected or run, and the window is
still raised from the remaining hints, as before. Commands are argument arrays run
through `platform::CommandRunner` (1.5 s each; common user tool directories are
searched because desktop launchers often lack them on PATH).

Backends are chosen by what the session offers, never by distribution. `x11` (X11
and XWayland) matches `$WINDOWID` first, then the windows of the nearest process
hint, preferring a title that names the project, and sends `_NET_ACTIVE_WINDOW`:
`Requested`, since the window manager decides. It is `Unsupported` off `xcb`. `kwin`
loads a temporary KWin script that picks only an unambiguous window and reports
back over D-Bus: `Confirmed`, `Failed` or `TimedOut` after 1.5 s; `Unsupported`
without KWin's scripting service. Only `x11` observes the active window; unknown
observation never suppresses a bubble. Failure tooltips follow the result: a
stopped multiplexer selection, a window that was not found or refused, or no
backend able to act, plus each unavailable backend's requirement (KWin: "Wayland
focus requires KDE Plasma 6.").

To add a host: write its `Capture` (detection, codec, label) in
`src/hosts/adapters/<host>.*` and register it in `Registry::builtin()` at its
precedence; give it an `Activation` if it can select a tab or pane and register
that in `platform::createFocusService()` (`src/platform/linux/native.cpp`). Its
identifiers must fit v1 (`host_target` up to 256 characters); richer targets need a
protocol change, and older pets reject unknown host IDs. To add a desktop backend:
implement `platform::DesktopBackend` under `src/platform/desktop/<name>/`, add it to
`pet_native` and register it in `createFocusService()` in order of preference;
return `Unsupported` when the session lacks it. Sessions, alert policy and Qt
presentation stay unchanged in both cases. `tests/focus_tests.cpp` registers a
test-only adapter and fake backends this way.

## Build and packaging

The portable-core profile builds capture, focus contracts, state and animation
without native implementations or application targets. The normal Linux profile
adds headless services, native desktop backends, updates and UI. See the
[platform target map](../src/platform/README.md#build-registration) and README
for core configure/test commands. The full app fails clearly on unsupported OSes.


See the starter README for exact commands. Build requirements: Linux C++17 compiler,
CMake 3.22+, Ninja, Qt 6.5+ Widgets and Test development files. Packaging additionally
uses Python 3, `qmake6`, `ldd` and `patchelf`. Use one consistent Qt installation.

`scripts/package.py` installs the executable and notices, copies xcb and offscreen
platform plugins and their transitive shared-library dependencies, and gives every
ELF file a relative `$ORIGIN` runtime path. `qt.conf` locates bundled plugins.
It produces a relocatable directory, a tar.gz, and a runtime library manifest.
Output directories must be new, preventing stale files from contaminating a build.
The package has no absolute checkout resource paths or interpreter requirement.

The initial standard Qt deployment scan included unrelated KDE/image plugins and
failed rewriting one plugin's runtime path. An explicit plugin set and patchelf
avoid that host-dependent scan. PNG decoding is built into this Qt GUI build.

Release packages are built on Ubuntu 22.04 (glibc 2.35) by CI; that glibc, the
ELF loader, a display server/compositor, fonts/font configuration and graphics
drivers remain operating-system dependencies. The packaging script collects
Arch (`pacman`) or Debian/Ubuntu (`dpkg`) license notices for bundled libraries
and a Qt/ICU notice when Qt comes from the online installer. Qt and transitive
dependencies retain their own licenses. See [install.md](install.md).

## M2 validation evidence (2026-10-04)

The 215-frame asset verifier and automated playback/settings tests pass. The
user also confirmed the M2 desktop appearance. The
XWayland desktop test used XTest to drag the real window 90 by 50 pixels; it
observed the drag animation, actual window movement, release detection, and return
to working. Captured pet and preview windows are saved under `/tmp` for visual
inspection. A native X11 session and native Wayland behavior have not been checked.

The current packaged prototype is `dist/agent-pet-0.2.0-m2.tar.gz` (115 MiB
extracted) with artwork, notices and private Qt dependencies. Isolated runtime
checking passed in bubblewrap with only the package plus the host loader, libc
and libm visible. The package remains a development
artifact built against this host's glibc.

## M1 validation evidence (2026-10-04)

Host: EndeavourOS x86_64, KDE Wayland with XWayland, Intel i5-12450HX (12 logical
CPUs), GCC 16.2.1, Qt 6.11.2, glibc 2.44.

| Check | Observed result |
| --- | --- |
| Asset baseline | Passed: 180 original PNGs, 20 sequences, 22,916,615 bytes |
| Release build and CTest | Passed: timed frame advancement, state switching, invalid-state rejection, alpha presence, size bounds, window flags, explicit input recovery |
| Copied package with spaces | `/tmp/Agent Pet M1/bin/agent-pet --smoke-test` launched from `/tmp`; reported xcb, decoded both states, restored input after 15 seconds and exited 0 |
| Isolated runtime | `scripts/check_isolated.py` passed in bubblewrap: only copied package plus loader/libc/libm mounted, no host Qt, checkout, Python, Node or VPet visible to the app |
| Isolated environment limitation | No fonts/fontconfig mounted; fontconfig emitted a missing-config diagnostic. This test verifies animation/runtime independence, not menu typography |
| Package size | Initial prototype: 111 MiB extracted / 56 MiB compressed, 61 bundled libraries |
| Native Wayland development smoke | Qt wayland backend launched, decoded both states and reported automatic recovery; exit 0. Actual placement, stacking and input delivery are not established by this check |
| Resource baseline | Offscreen 17-second mixed-state smoke: 16.84 seconds elapsed, 2.09 seconds process CPU (about 12.4% of one core), 54,864 KiB peak RSS. Includes launch, PNG decoding, resizing and recovery; not an idle-desktop benchmark |
| User desktop confirmation | User confirmed visual transparency and always-on-top behavior on the current desktop on 2026-10-04 |
| Remaining desktop interactions | Cross-application input pass-through, full drag/quit checklist and tray usability still need explicit manual evidence |
| Native X11 session | Not available in this session; still needs a composited X11 desktop check |

The smoke test drives scaling, on-top changes, both animations, click-through and
automatic recovery before exiting after 17 seconds. Programmatic flag checks do
not prove compositor behavior. M1's native X11 and manual interaction acceptance
gates remain open; do not mark the entire milestone complete on these tests alone.

M2 performance targets recorded at the start of implementation: under 80 MiB peak RSS and
under 5% of one core during a steady 60-second idle animation at 240 px, measured
on the real desktop. The mixed smoke baseline does not establish that idle target.
For M3, target hook callback p95 under 50 ms and a 200 ms hard transport deadline,
including when the UI is absent; no callbacks exist in M1 to measure yet.

## Manual acceptance checklist

Run on a composited X11 session and separately on KDE/GNOME Wayland with XWayland:

1. Launch the copied package from a directory with spaces; check clear space around
   the sprite against light and dark desktop backgrounds.
2. Drag across the screen and across monitors. Switch Idle / Thinking and all sizes.
3. Toggle Always on top; verify stacking against another application.
4. Enable click-through over another application; verify that application receives
   clicks. Wait 15 seconds and verify the pet receives input again.
5. Try immediate recovery and Quit from the tray, if available. Also verify recovery
   with a desktop lacking a tray. Right-click Quit and focused Escape must terminate.
6. Close About and verify animation continues. Record desktop/version and results.

Native Wayland tests are exploratory and must be recorded separately. No hooks,
integration registrations or user settings are changed by the prototype.

## M3 evidence — 2026-10-04

Local monitoring now uses a Qt Core session engine and a private Linux Unix
datagram transport, with no additional runtime dependency. Hook/emit dispatch
happens before QApplication construction, so callbacks do not connect to a
display. The desktop translates aggregate session states into the existing
animation catalog. See [events.md](events.md) for the protocol and bounded-state,
ordering, identity, expiry and pending-alert policies.

Validation on the existing EndeavourOS development host with Qt 6.11.2:

- Release build passed; CTest event and prototype suites passed (about 5 seconds).
  Replay covers concurrent providers/tools/children; other checks cover missing
  starts, duplicate/late callbacks, completion, interrupt, expiry, restart,
  attention dismissal, errors, capacity and schema validation. Playback checks
  validate every aggregate state's catalog mapping and drag restoration.
- Actual Unix socket/subprocess tests passed, including absent/full receiver,
  stalled stdin, no display, competing monitors and private-directory checks.
  Binding required execution outside the tool sandbox; this is not an app error.
- Asset verifier passed: 215 frames, 23 sequences, 27,345,411 bytes.
- The M3 bundle was built under `/tmp/Agent Pet M3` to exercise spaces in paths,
  and copied to `dist/agent-pet-m3` with an updated archive. It includes 61 runtime
  libraries. Packaged callbacks without a monitor or display exited silently
  with code 0 in 10.0 and 10.4 ms (valid and malformed payload samples).
- Filesystem-isolated bundle smoke passed, including playback, input recovery
  and shutdown. The isolated fontconfig warning is nonfatal. Bubblewrap required
  execution outside the tool sandbox to create its namespaces.

M3 does not establish live Claude/Codex hook coverage: raw provider adapters,
client versions and integration configuration belong to M4. Visible alert
controls belong to M5. No new native X11/Wayland desktop acceptance is claimed.

## M4 implementation evidence — 2026-10-04

Provider adapters and headless integration management are implemented in
`src/providers`. The setup guide and explicit live-acceptance gaps are in
[integrations.md](integrations.md). Version probes returned Claude Code 2.1.289
and Codex CLI 0.156.0; the official hook references were checked on this date.

- Release CMake build passed. CTest passed all three suites (`providers`, `events`,
  `prototype`), including 30 synthetic provider contract cases, concurrent tools,
  child isolation, failed-tool cleanup, large raw payloads, ownership-aware setup
  round trips, malformed-file preservation and actual shell path quoting.
- Socket tests require execution outside the restricted sandbox; their first
  sandbox run failed to bind sockets, and the permitted run passed in about 5 s.
- Asset verification passed: 215 PNGs, 23 sequences, 27,345,411 bytes.
- `python3 scripts/package.py` produced `dist/agent-pet-m4.tar.gz` with 61 bundled
  libraries. The copied package launched at `/opt/Agent Pet` in filesystem
  isolation, decoded idle/thinking, recovered input and shut down normally.
  Packaged Claude/Codex silent callbacks, configuration preview and enable also
  passed inside that namespace. The expected fontconfig warning was nonfatal.

No user client hook configuration was installed during implementation. Synthetic
fixtures and subprocess tests do not certify real client/host behavior. Captured
live payloads, real concurrent/child sessions, VS Code integrated terminal tests,
and existing-client configuration reload behavior remain M4 acceptance work.

## M5 implementation evidence — 2026-10-04

`Monitor` (`src/desktop/monitor.cpp`) now owns the receiver, session state and
alert presentation that `main.cpp` previously wired inline. It feeds aggregate
playback, the badge and an `AlertBubble`: a separate frameless, non-activating
top-level window placed right of the character, else left, above or below,
clamped to the pet's screen and moved with the pet. The pet's Quit emits
`quitRequested`, which stops the timer, closes the socket and hides the bubble;
closing settings or preview does not touch the monitor. Mute and sound are new
optional keys in `preferences.json`; files written before M5 still load.

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the build
requirement stays Qt 6.5; the temporary local override was not committed):

- CTest passed all four suites (`providers`, `events`, `alerts`, `prototype`).
  `alerts` replays concurrent sessions across providers, folder-name
  collisions, short-ID collisions, preemption versus a kept Next position,
  dismissal with attention retained, missed/late/ended/expired sessions,
  restart and the 64-alert bound. `prototype` runs an offscreen pet with the
  monitor: bubble text and footer, badge count, mute, dismissal, settings
  close with continued monitoring, quit stopping monitoring, edge placement
  and preference compatibility. Provider fixtures now assert attention reasons.
- The real binary under Xvfb received three `emit`ted events and showed
  "Needs approval / abc-web (work) · Claude Code · a1b2 / 2 more alerts" with
  the badge, beside the character.
- `--smoke-test` passed offscreen; asset verification passed (215 PNGs).

Not covered here: audible sound, a compositor-backed desktop (Xvfb has no
translucency), native X11/Wayland, and packaging (`qmake6`/`patchelf` were not
installed). Live-client acceptance from M4 is still open.

## M6 packaging evidence — 2026-10-04

Package format: a relocatable `agent-pet-VERSION-linux-x86_64.tar.gz` plus a
POSIX `install.sh`/`uninstall.sh`. It installs per user to
`~/.local/opt/agent-pet` (or `--prefix`), adds a desktop entry, hicolor icon and
`~/.local/bin` link, and records them in `.agent-pet-install` so uninstall
removes only those. Upgrades copy beside the old directory and swap it in, so a
running pet keeps its open files and an interrupted copy leaves the old install.
Settings live outside the install (`AppDataLocation`) and survive upgrades.
Hook commands use the stable installed path, and uninstall disables only
handlers matching Agent Pet's own command grammar (inspect first, so a missing
client config is never created). The version comes from CMake `project()` via a
generated `version.h`, shown by `--version` (headless) and About, with the
source revision, Qt version, Apache-2.0 application license and artwork terms.

Asset optimization was measured and not done. The extracted package is 103 MiB
(tar.gz 55 MiB): artwork embedded in the executable is 26 MiB, bundled libraries
75 MiB, of which ICU data alone is 30 MiB. The PNGs are already compressed, the
frame-cache bound test passes, and re-encoding would risk visual changes for a
small gain.

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (requirement still
6.5; the temporary override was not committed): CTest passed all suites,
including a new check that the license and notices are embedded.
`check_isolated.py` passed with only the package and host glibc/loader mounted.
`check_install.py` passed: refusal to overwrite a foreign directory, install
into `/tmp/agent pet check …/My Apps/agent-pet`, `desktop-file-validate`,
both integrations enabled next to an unrelated hook, the offscreen smoke test
from the installed path, in-place upgrade (stale files removed, preferences and
hook files byte-identical), uninstall (unrelated Claude settings restored exactly,
no Codex handlers left, preferences kept) and `--purge-settings`.

CI on Ubuntu 22.04 with Qt 6.5.3 passed the same steps from a clean checkout. It
first exposed a smoke-test timer race (coarse timers may drift 5%; the check now
uses a precise timer at 18 s), a Qt non-UTF-8 locale warning on hook stderr (Qt
messages are now dropped for `hook`), and `desktop-file-validate` 0.26 rejecting
desktop-entry Version 1.5 (now 1.4).

Not covered here: a real desktop launch from the menu, other distributions, and
live Claude/Codex sessions with the installed package.

## Idle animation evidence — 2026-10-04

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


## Mood evidence — 2026-10-05

Imported 19 sequences (191 frames, 30.2 MB) with `scripts/add_sequences.py`:
`Default/Happy/1` and `Default/PoorCondition/1` for idle, Happy and PoorCondition
`aside` and `yawning` for those fidgets, `Say/Shining` and `Say/Shy` as celebrations,
`Switch/Hunger/Nomal` and `Switch/Thirsty/Nomal` as the snack and `LevelUP/Happy`
as the milestone. The pack is now 518 files, 73,660,791 bytes. Left out for size, and
addable with the script: mood art for the other fidgets and for `sleeping`, and
`Default/Happy/2`–`3`, `Default/PoorCondition/2` as extra mood idle loops.
`IDEL/Squat/C_Happy` still has no frame timings. `verify_assets.py` also checks the
`moods` and `reactions` sections.

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the 6.5 requirement was
lowered only in a scratch copy): all seven CTest suites passed. New prototype tests
cover mood art replacing choices (next loop pass, held states, states without art,
unknown moods, variants off), the score with streaks, errors, recovery and both
hysteresis gaps, the Cheerful and Off caps, weighted celebrations, the snack count
and its reset after a break, the hundredth-turn milestone outranking a snack, the
monitor counting only accepted events and celebrating only when the aggregate turns
to `turn-finished`, the persisted setting and turn count, and refusal of malformed
moods and reactions. Every state now also plays under each mood in the frame test.
Still open: tuning the gains and thresholds against real sessions, and CI on Qt 6.5.3.

## Touch reactions evidence — 2026-10-05

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

## Easter eggs evidence — 2026-10-05

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

## Daily recap evidence — 2026-10-06

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the 6.5 requirement was
lowered only for the local build): all eight CTest suites passed. New tests cover
counting by project and day, approvals counted once per request and their waits
measured on answer (none for a session that ended, none for input questions), the
summary and breakdown wording, the 14-day window with an older date never kept, the
store's round trip with only counts and folder names written and invalid files loading
as empty, and through `Monitor`: the menu's summary then breakdown on a click, the
empty-day note, the go-home reminder with and without the recap, the counters surviving
a restart, and the persisted setting in the settings dialog. Still open: how it reads on
a real desktop, and CI on Qt 6.5.3.

## Walking evidence — 2026-10-05

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

## Platform refactor evidence — 2026-10-05

Phases 1–3 of the [plan](platform-refactor-plan.md), one commit each. Phase 1
added characterization tests against the unchanged code: capture precedence and
limits, Konsole/tmux/herdr codecs, labels, v1 host validation, session host
refresh and inheritance, and the monitor's focus tooltips, dismissal and
suppression policy. Phase 2 moved those into `src/hosts` with the same assertions.
Phase 3 replaced `src/desktop/host_focus.*` and `drag_monitor.*` with the focus
service, adapter activations, `x11`/`kwin` backends and Linux process and command
services; X11 and D-Bus are now linked only by `pet_native`. The new `focus`
suite covers backend fallback, `Requested` versus `Confirmed`, unsupported
backends and their requirements, malformed targets, selection-only success, each
adapter's failure policy, tmux client hints, herdr clients before the pane's own
hints, unknown active state, a test-only adapter, and the Linux command runner.
Intended differences: tmux lists its clients once per click instead of twice,
and herdr's client search now needs a valid target.

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the 6.5 requirement
was lowered only in the local build): all eight CTest suites passed. `hook`, run
without a display, sent identical host fields to the previous build's for Konsole,
tmux, herdr, VS Code, generic terminal and oversized-target environments. On Xvfb
with Openbox: `platform::createFocusService()` raised xterm windows by process hint
and by `$WINDOWID` (`x11`, `Requested`, `_NET_ACTIVE_WINDOW` changed) and reported
them active or inactive; tmux observation was `Unknown`; a missing window was
`TargetNotFound` with KWin's requirement; a Konsole target without a D-Bus session
still raised its window; a real tmux 3.4 session switched to the pane's window and
raised the xterm of its attached client, found only through `list-clients`; a
failing tmux selection stopped before any window. In the running pet, a hook run
inside an xterm produced no bubble while that xterm was active, and a later bubble's
Open raised it, with another xterm under the bubble, in each of four runs alternating
this and the previous build (both raised the right window each time). The first,
exploratory click had left focus on the xterm under the bubble; it did not recur.
`desktop-tests`' X11 drag check passed. After merging main's ancestry check for
inherited terminal variables into the registry, `hook` again matched main's build in
nine environments, run beneath processes named `konsole`, `tmux`, `herdr` and `code`
or none. Not checked here: KWin on Plasma 6 (X11 and
Wayland), a live Konsole D-Bus tab switch, herdr, and CI on Qt 6.5.3.

## Wellness reminders evidence — 2026-10-06

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the 6.5 requirement was
lowered only locally): all CTest suites passed. New tests cover active-time counting
(grace, pause, five-minute reset, out-of-order activity), due order and `given`,
interval choices and quiet-hour boundaries; the monitor showing the eye break after 20
minutes of pointer movement with its countdown and cheer, the water reminder held by
a waiting approval until it is answered, the click reaction, muted alerts holding it,
quiet hours dropping it and a break resetting it; and the settings group with
persistence, legacy files and malformed values. Still open: how the cadence feels on
a real desktop, and CI on Qt 6.5.3.

## Application updates

`src/updates/release.*` validates stable release metadata, exact repository asset
URLs and SHA-256 digests, using `platform/contracts/update_layout.h` for asset names. `controller.*` owns asynchronous
Qt Network requests, bounded daily checks, persisted update preferences, UI and
pending downloads. Hook, emit, integration and autostart commands return before
constructing this service. Requests use TLS verification, size limits and idle
timeouts; package downloads permit HTTPS redirects to GitHub's asset CDN.

`scripts/package.py` publishes the full legacy tarball plus independent
archives (`app`, `runtime`, `artwork`, and `artwork-<sequence-hash>`) and a version/architecture-specific
`-components.json`. Archives use sorted paths and fixed tar/gzip metadata. The
manifest records archive names, sizes and SHA-256 hashes, plus each target file's
path, size, hash and executable flag. Runtime libraries/plugins/notices form the
runtime component; the catalog RCC, pack index and artwork notices form artwork;
each PNG sequence directory becomes its own RCC and component, named by the SHA-256
of its source directory path. Remaining files form the app component. Manifest
format 2 supports the variable pack list; the updater also accepts format 1.
Release CI publishes the full archive, manifest and every component. Full archives preserve first installation
and upgrade compatibility with older clients; upload storage is not reduced.

`components.*` validates the manifest after checking its GitHub asset digest,
including target version/architecture, safe unique paths and size bounds. The
controller compares actual installed files with the target file list, so no
previous manifest or intermediate version is needed. Matching components are
reused; missing or altered files cause their component to download. Completed
archives survive a retry. Superseded component downloads are removed when a new
manifest is accepted, and successful installation removes its downloads.
An unsupported manifest or failed component request can fall back once to the full
archive with its independently verified GitHub digest. User cancellation does not
trigger a fallback download.

`--apply-components` rechecks local files, copies matching components into a fresh
stage, and extracts verified archives for the others. It verifies all staged
files and rejects undeclared files before probing or launching the executable.
Copies are independent of the previous installation to preserve rollback. A
component that changes locally after downloading can cause installation to fail
safely; retrying the download fetches the required replacement. Both package
formats share the same transaction, startup health check and recovery logic.

`agent-pet-updater` uses libarchive for bounded extraction, rejects links and
traversal, probes the staged executable, preserves the install receipt, and uses
Linux directory exchange for replacement. The update lock excludes competing
GUI starts; the existing IPC receiver lock excludes a running monitor during
replacement. A journal and per-transaction marker distinguish crashes before
and after the exchange. The newly launched GUI acknowledges readiness after its
window and monitor have started; failure restores the old directory. The helper
waits for the child to exit after committing so QProcess ownership does not kill
the updated app. Settings and external integrations are never rewritten by the
helper. Filesystem/power-loss guarantees remain those of the host filesystem;
the recovery tests exercise process interruption around the directory exchange.

Release bundles include the helper, libarchive, Qt TLS plugins and OpenSSL's
runtime closure, including Qt distributions that dynamically load OpenSSL.
`--check-update-runtime` checks TLS backend availability without a display or
network request and is included in isolated-package validation. CI installs
libarchive/OpenSSL development packages. Publishing the existing draft release
makes it discoverable; the app accepts only matching stable-version assets with
GitHub-provided SHA-256 digests for installation.

Validation: `update-tests` covers version ordering, metadata/URL/architecture
validation, checksum mismatch, offline checks, throttling, skipped versions,
cancellation and failed download writes,
automatic downloads, active-session restart blocking, and interrupted-exchange
recovery. `scripts/check_update.py` runs the real helper against disposable
packages to test unsafe archives, failed startup rollback, successful replacement
and receipt/settings preservation. A disposable release-style build also passed automatic installation at the next
launch and readiness acknowledgement from the actual updated GUI (offscreen).
Isolated-package HTTPS runtime, install/upgrade/autostart/uninstall checks, and all
seven CTest suites passed locally. Actual GitHub release rollout and desktop
interaction remain manual acceptance checks.

Component validation adds target-version/path rejection, application-only network
requests, cached retry, cancellation, corruption repair and readiness after restart.
The helper integration check uses the real component publisher to cover reused
files without cached archives, modified or symlinked installed files, corrupt
archives, final file hashes, removal of obsolete files and failed-start rollback.
