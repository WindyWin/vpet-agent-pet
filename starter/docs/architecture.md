# Desktop architecture and validation

Decision recorded 2026-10-04: C++17, Qt 6 Widgets, CMake and Ninja.
Develop in this starter directory. No original VPet installation is used.

Qt provides translucent top-level windows, native move requests, input-transparent
windows and tray menus with a small native application layer. The prototype uses
software QWidget painting, without a browser or language interpreter at runtime.
The selected 327-frame artwork pack, catalog and artwork terms are embedded as Qt
resources, so moving the executable cannot break sprite lookup.

## Boundaries

| Directory | Responsibility |
| --- | --- |
| `src/desktop` | Transparent pet, alert toast, running-sessions list, host focus, attention badge, context/tray menu, tray status and hiding, drag, touch gestures (`touch`), scale, input and quit |
| `src/animation` | Catalog validation, phased playback with weighted variants and mood art, the idle fidget scheduler (`ambient`), the mood score and celebrations (`mood`) and bounded decoded-frame cache |
| `src/settings` | Validated, atomic preference storage in the user data directory |
| `src/sessions` | Bounded session/tool state, ordering, aggregate activity, alerts and alert labels, and the widget-free show/hide/idle rules (`presence`) |
| `src/ipc` | Private Unix transport, headless hook/emit commands, and hook-side autostart with the `autostart` command |
| `src/providers` | Claude/Codex normalization and integration configuration management |

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
persisted. The settings window remains open only on request and closing it keeps
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

## Window behavior

The pet requests a borderless, translucent, always-on-top tool window. Left drag
uses `QWindow::startSystemMove`, with coordinate movement as a fallback. A press that
leaves the pet in place is a click under 500 ms and a touch from then on (see
[touch reactions](#touch-reactions)); a throw and hiding at an edge move the window
from the application, which native Wayland may refuse. Right-click
opens controls. Space switches preview state, Menu opens controls and Escape quits
while the pet has focus. These are local shortcuts, not global desktop bindings.

Click-through remains a 15-second lease. A single-shot
timer restores input whether or not a tray exists. The tray menu can immediately
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

## Build and packaging

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

## Application updates

`src/updates/release.*` validates stable release metadata, exact repository asset
URLs, platform selection and SHA-256 digests. `controller.*` owns asynchronous
Qt Network requests, bounded daily checks, persisted update preferences, UI and
pending downloads. Hook, emit, integration and autostart commands return before
constructing this service. Requests use TLS verification, size limits and idle
timeouts; package downloads permit HTTPS redirects to GitHub's asset CDN.

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
