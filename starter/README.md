# Agent Pet

A small starting repository for a desktop pet that reacts to Claude Code and Codex activity. The sprite files are bundled here, so development can proceed without a local VPet installation.

The current asset pack contains **1,197 original PNG frames in 141 animation sequences (171.67 MiB)**. They cover idle (with two alternate loops, a few idle fidgets, and happy and droopy moods), thinking, reading, working, waiting for input, tool errors, several ways to celebrate a finished turn, sleeping, startup, closing, the original dragging animation, reactions to being petted, thrown and pushed against a screen edge, a few easter eggs, and walking, crawling and climbing. [animations.json](assets/vpet/animations.json) maps each state to its sequences, records frame durations, and lists the idle variants, fidgets, mood art, reaction cues, touch hit boxes and moves. [manifest.json](assets/vpet/manifest.json) records every file's size and SHA-256 hash.

[available-animations.json](assets/vpet/available-animations.json) catalogs the **422 remaining sequences and 4,301 frames** from the original character pack. Each entry gives its upstream folder, category, frame count, size, and timing status. These are references for future work; their PNG files are not included here. To add one later, run `python3 scripts/add_sequences.py IDEL/yawning/Nomal …` against a copy of the upstream VPet tree (`--source`; the full archive next to this folder is the default). It copies the frames, updates the manifest and both catalogs, and leaves you to map the sequences in `animations.json`, which `scripts/verify_assets.py` then checks. Retain the artwork notices.

This folder is ready to become its own repository:

```bash
cd agent-pet/starter
python3 scripts/verify_assets.py
git init
```

The asset verifier uses only Python's standard library and works from any current directory. M3 local events and session tracking are implemented alongside the M2 animation and desktop controls. M4 provider adapters and integration setup are implemented; live-client acceptance remains open. M5 alerts and minimal settings are implemented. M6 adds the release package, installer and CI. See [PLAN.md](PLAN.md) and the [architecture overview](docs/architecture.md) and [decision records](docs/adr/README.md).

## Waiting for background jobs

When Claude ends a turn with background tasks still running, the pet uses its idle
animation and the session says **Waiting**. It shows no completion celebration or
“Needs input” bubble for idle notifications during the wait. Approvals still show
normally. Once Claude resumes and finishes without background work, the pet gives
its normal finished reaction and counts one turn, including the waiting time.
Jobs without events for 30 minutes still expire. This feature uses Claude's
`background_tasks` Stop field; Codex has no equivalent signal. See
[ADR 0026](docs/adr/0026-background-waiting.md) for validation status.

## Build and run

From this directory, install CMake 3.22+, Ninja, a C++17 compiler and Qt 6.5+
Widgets/Test/Network/DBus/LinguistTools, libarchive and X11 development packages, then run:

```bash
python3 scripts/verify_assets.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/agent-pet
```

To build only portable libraries and tests, without the application, native
backends or Linux packaging (Qt Core/Gui/Test required):

```bash
cmake -S . -B build-core -DAGENT_PET_PORTABLE_CORE=ON
cmake --build build-core -j 4
ctest --test-dir build-core --output-on-failure
```

This checks platform boundaries. Full application builds run on Linux, macOS and Windows.
See [platform services and extension points](src/platform/README.md).

On macOS (11 or newer), install Ninja and Qt 6.5+ (for example with Homebrew or
the Qt online installer); X11, D-Bus and libarchive are not needed:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(qmake -query QT_INSTALL_PREFIX)"
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/agent-pet
```

Releases add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`
for one universal app. On macOS, **Open** selects tmux and herdr panes but cannot
bring windows forward yet, and updates are announced but installed by hand; see
[macOS](docs/install.md#macos) and the [design notes](docs/adr/0020-macos-port.md).

On Windows (10 1809 or newer, x64), build from a Visual Studio 2019/2022 developer
prompt with Ninja and Qt 6.5+ for MSVC 2019 64-bit:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.5.3\msvc2019_64"
cmake --build build
ctest --test-dir build --output-on-failure
.\build\agent-pet.exe
```

Windows builds `agent-pet.exe` (the pet, no console window) and `agent-pet-cli.exe` (the
same program for hooks and commands). Updates are announced and installed with the setup
program; see [Windows](docs/install.md#windows) and the
[design notes](docs/adr/0024-windows-port.md).

Left-drag to move; the original Raise animation plays while dragging and returns
to the prior state on release. Right-click for the running sessions, today's recap,
mute, always-on-top, Settings and Quit; **More** holds the animation preview,
temporary click-through, recovery, updates and artwork terms. Settings is split into
General, Pet, and Startup and agents tabs. A tray
icon appears on supported desktops: left-click hides or shows the pet (it keeps
monitoring while hidden), its tooltip shows session status and its icon carries
the attention badge. Click-through automatically ends after 15 seconds. Space
switches idle/thinking and Escape quits when the pet has focus.

`./build/agent-pet --preview` opens the developer preview, where every bundled
state can be selected. It shows the active sequence, phase, frame duration,
transition history and decoded-frame cache use; you can pause and step frames.
`--state thinking` starts with another state. `--settings` opens the settings
window. Size, position and always-on-top are saved in the user's application data
directory and restored on restart. Click-through always starts disabled. Closing
settings or preview leaves the pet running. `--pet <id>` runs another installed pet once.
About shows the running pet's artwork credit and terms.
Settings → Startup (or `agent-pet autostart enable [--when-idle keep|hide|quit]`)
lets a connected agent's session start launch the pet, and chooses whether it
stays, hides or quits once no sessions remain; see [install guide](docs/install.md#autostart).

## Idle animation

While nothing needs the pet it no longer repeats one loop forever. Settings → **Idle
animation** chooses how much it does on its own: **Off** (no fidgets or alternate loops),
**Subtle** (the default: a fidget about once a minute) or **Lively** (every 15–25
seconds). Fidgets are short one-shots (a glance aside, boredom, a yawn, a crouch, and
now and then a rare meow). The bigger ones wait for more idle time, and a fidget avoids
repeating the one before it whenever another is eligible. The idle loop itself alternates between three variants.
After ten quiet minutes the pet dozes off, and wakes through its usual end-of-sleep
animation. Any agent activity, error, alert or drag replaces a fidget at once, and a
hidden pet does nothing. Preview any fidget from the right-click menu → More → Preview state
(`fidget_*`). The catalog sections behind this are described in
[the design record](docs/adr/0011-idle-animation.md).

## Active animation

While an agent thinks, reads or works, the pet no longer repeats one short loop.
Settings → **Active animation** chooses how: **Classic** (one loop per activity, as
before), **Subtle** (a calmer variation every 10–18 seconds; a short thinking pause
between tools keeps it at its desk, and switching between reading and working swaps
book and pen without getting up) or **Playful** (the default: variations every 6–12
seconds, pen spinning, and now and then a small happy reaction when it gets to work).
It is independent of Idle animation. Requests, errors, finished turns, pausing and
dragging always take over at once. See
[the design record](docs/adr/0021-active-animation.md).

## Mood

The pet also keeps a mood from what your agents do. A run of finished turns makes it
happy, which shows in its idle loop and in some fidgets; repeated tool errors make it
droopy. With no news it drifts back to neutral within a few minutes. A finished turn
is celebrated in one of three ways. After a long productive stretch (twenty finished
turns without a half-hour break) its tummy rumbles or it looks thirsty, a hint to take
a break, and every hundredth finished turn gets a bigger celebration. Settings →
**Mood** chooses **Off** (always neutral), **Cheerful only** (never droopy) or **Full**
(the default). Preview the reactions from Preview state (`cheer_*`, `snack_*`,
`milestone`); see [the design record](docs/adr/0012-mood.md).

## Touch

A quick click still opens the running-sessions list. Hold the pet still for half a
second instead and it reacts to where you hold it: a head pat, a poke in the tummy, or
a pinched cheek. It keeps reacting until you let go. Moving while holding turns it into
an ordinary drag. Let go of a drag while still moving fast and the pet is thrown: it
tumbles to the bottom of the screen, lands and gets back up. Push it past the left or
right edge of your screen while it idles and it hides there, peeking out from behind
the edge. Agent activity brings it back out, and so does dragging it away. Settings →
**Touch** turns all of this off, leaving plain dragging. Preview the reactions from
Preview state (`touch_*`, `pinch`, `fall_*`, `edge_*`); see
[the design record](docs/adr/0013-touch-reactions.md).

Throw it five times within 30 seconds, pet it continuously for 8 seconds, or drag and
hold it for 15 seconds, and it gets angry and quits. It stops accepting interactions,
shows a speech bubble explaining why, plays `angry`, pauses briefly so you can read it,
then plays `closing_angry` (a jump and disappearance) before exiting. Normal Quit keeps
its usual animation. Releasing the pet resets the hold timers; throws expire after 30 seconds.
Turning Touch off clears both counters and disables this reaction.

## Easter eggs

A few surprises, on by default. On May 20 the idle pet greets you with a heart, and
on your birthday (Settings → **Birthday**, day and month only) it celebrates, both
when it idles and on the day's first finished turn. Late at night it yawns more, and
the first turn that finishes after 1 AM brings a gentle bedtime note. The pet also
keeps time: on Monday morning it is tired and down about the week, at 4:45 PM on
weekdays it blows bubbles and tells you to get ready to go home, and at 10 PM it
tells you to go to sleep (each once a day, while it is running and visible; like the
wellness reminders, it waits until you are around and nothing else needs you, and
muted alerts hold it).
Settings → **Monday reminder**, **Go-home reminder** and **Sleep reminder** let
you change these local times; the times above are the defaults. Turns finished
on a Friday evening end in a dance, and a turn that ran for a quarter of an hour or
more gets a bigger celebration. When an agent starts a destructive shell command,
such as `rm -rf` or `git push --force`, the pet jumps; the hook judges the command
itself and sends only that verdict. There is at least one more to find. Settings →
**Easter eggs** turns them all off. The special-day fidgets need idle animation on.
Preview them from Preview state (`love_520`, `birthday`, `dance`, `startled`); see
[the design record](docs/adr/0015-easter-eggs.md).

## Wellness reminders

While you work, the pet reminds you to rest your eyes and to drink some water. After
20 minutes of activity it says "Look at something far away for 20 seconds" and
closes its eyes for a stretch (the 20-20-20 rule); click the note and it counts the 20 seconds down, then
cheers. After 60 minutes it gets thirsty and says "Time for some water 💧"; click to
say you had some and it cheers. Ignored, a reminder fades and comes back only after
the next interval. Activity means moving the pointer, sending a prompt, or agent
events while you were around in the last five minutes: a few idle minutes pause the
timers, and five minutes away (or a locked screen) counts as a break and starts both
over. Reminders wait until you have moved the pointer in the last minute, and while
an alert bubble shows, an agent waits on you or alerts are muted. They skip quiet hours (10 PM to 6 AM, which have the bedtime
note). Settings → **Reminders** sets the eye break (off, 20, 30 or 45 minutes) and
water (off, 45, 60 or 90 minutes); both are on by default. See
[the design record](docs/adr/0019-wellness-reminders.md).

## Daily recap

Right-click → **Today's recap** and the pet sums up what your agents did today:
"Today: 38 turns across 3 projects · 2 approvals waited 10+ min · longest run 22 min".
Click the bubble for turns per project, errors, approvals with the longest wait, and
the longest run. On weekdays the go-home reminder (default 4:45 PM) includes the summary too;
Settings → **Recap** turns that off. The counters are kept for two weeks in
`recap.json` next to the preferences: counts and project folder names only, never
prompts, commands or paths. See [the design record](docs/adr/0018-daily-recap.md).

## Wandering

After about four quiet minutes the idle pet sometimes goes for a stroll: it walks or
crawls along the screen at its current height and stops short of the edge. A happy pet
trots instead, and a droopy one trudges. Once it reaches a left or right screen edge it
may climb up or down it, clinging to the edge, then step back into view. Any agent
activity, a drag or Recover position stops it where it is, and it never wanders off a
screen. Settings → **Wander** turns it off, so the pet stays where you put it; idle
animation Off keeps it still too. Native Wayland does not let an application move its
own window, so there the pet stays put. Preview the moves from Preview state
(`walk_*`, `trot_*`, `trudge_*`, `crawl_*`, `climb_*`); see
[the design record](docs/adr/0016-walking.md).

The default uses X11/XWayland when DISPLAY is available. For a development-only
native Wayland check: `QT_QPA_PLATFORM=wayland ./build/agent-pet`.
Native Wayland placement and stacking support remain unverified.

## Pets

VPet is the bundled pet, and the app is built for more. A pet is a folder of frames, a catalog, a
preview and its own art terms under `assets/`, and a rebuild picks up every such folder with no code
changes. Once two or more pets are installed, Settings → Pet → **Character** shows a tile per pet. The
chosen pet appears the next time Agent Pet starts, and only its artwork is loaded. `--pet <id>` runs
another installed pet once without saving the choice. A pet that cannot load is skipped and VPet runs
instead. VPet ships with the app; other pets show their download size and download when chosen, verified
pack by pack against hashes the app already carries, with progress and Cancel in Settings. The app tells a pet what is happening through named cues (a session needs you, a turn finished,
time for water), and each pet decides which of its animations answers each one. To make a pet, see
[the pet guide](docs/pets.md) and the design records for [pet packs](docs/adr/0028-pet-packs.md),
[cues](docs/adr/0029-cues.md) and [on-demand pets](docs/adr/0030-on-demand-pets.md).

## Language

The pet speaks English or Vietnamese (Tiếng Việt). By default it follows the system
language. Settings → General → **Language** picks one, and the switch happens right away,
without a restart. In Vietnamese the pet speaks as "em" and calls you "bạn". The
command line, the installer and these docs stay in English. To add or fix
translations, see [translations](docs/i18n.md).

## Arch Linux

Arch users can build the package with `makepkg -si` from `packaging/arch/` once a
release has attached its source tarball. The same `PKGBUILD` is planned for the AUR as
`agent-pet`, but it is not published there yet. It uses the system Qt and pacman owns
upgrades and removal. See [Arch Linux](docs/install.md#arch-linux).

## Updates

**Settings → Updates** offers daily release notifications, automatic downloads,
optional installation on the next normal launch, and fully automatic download and install when idle (the default).
Updates preserve settings and hooks and restore the previous version if startup
fails. Development builds and the macOS and Windows apps offer notifications and manual downloads.
While an update downloads, the Updates window shows one progress bar for the whole update
(with “file 2 of 5” when it comes in several parts) instead of restarting at 0% for each file.
See [update behavior and recovery](docs/install.md#update-notifications-and-automatic-updates).

## Local events

The pet now accepts normalized local events, tracks concurrent sessions and
active tools, and selects the aggregate animation. `hook` and `emit` work without
a display. For a running pet, try:

```bash
printf '%s\n' '{"version":1,"provider":"claude","session_id":"demo","kind":"prompt"}' | ./build/agent-pet emit
```

See [the event protocol](docs/events.md) for all events, identity fallbacks,
ordering, limits, expiry, and command behavior. See [integration setup and coverage](docs/integrations.md) for raw Claude/Codex
hooks and preview/enable/inspect/disable commands.

## Out of quota

When a Claude Code turn stops because the usage limit or the credits ran out, the
pet takes to bed with a thermometer and a cough, and a bubble says **Usage limit
reached** or **Out of credits**. Both stay until that session works again: your next
prompt, or Claude resuming on its own. Requests that need your answer still show
first. Other provider failures, such as an overloaded API, raise a short **Turn
failed** reaction and then let the pet rest, instead of leaving it thinking. Codex has
no hook for this, so its limits are not shown.

Pets set up with an earlier version show the Claude integration as **Partial** in
Settings; click **Update** to add the new hook, then restart Claude Code.

```bash
printf '%s\n' '{"version":1,"provider":"claude","session_id":"a1b2c3","kind":"turn_failed","reason":"limit","project_path":"/projects/demo"}' | ./build/agent-pet emit
```

## Alerts and running sessions

Approval/input requests, quota stops and tool errors raise a one-line toast beside the pet,
for example `● Needs approval  abc-web  +2  Open  ×`. Hover it for the
provider, short session ID and full project path. When several projects share a
folder name, the parent folder is added. Click the toast or **Open** to bring
the agent's terminal or editor forward; **×** dismisses it; **+N** opens the
session list. Repeated alerts from the same session and reason are counted
(`×2`). A new request that outranks the shown alert takes its place. Reply in
the agent's own terminal or editor; Agent Pet never answers a request.

To keep the toast quiet: tool errors fade after 10 seconds and finished turns
after 6 (only requests wait for you); no toast appears for a session whose
window is already active; and Settings → Show bubbles chooses between requests
only, requests and errors (default), or finished turns too. The pet animates
every event regardless.

Click the pet (or right-click → Running sessions…) for a small list of observed
sessions, most urgent first: project, status (Needs approval, Working,
Thinking, Idle · 3 min…), provider, short ID and host. Subagents fold into
their parent. Click a row to go to that session.

"Go to session" uses identifiers the hook records from its environment: Konsole
switches to the session's tab over D-Bus, tmux selects the pane, herdr runs
`herdr tab focus` and `herdr agent focus`, and on X11/XWayland the window that
owns the agent's parent processes is activated (VS Code windows are told apart
by the project name in their title). Detached Herdr sessions are matched to
their attached terminal clients. On KDE Plasma 6, KWin scripting also raises
native Wayland windows; other Wayland compositors are not supported. Open keeps
the alert visible if it cannot raise a window, and its tooltip says whether the
tab or pane could not be selected, the window was not found, or this desktop cannot
raise windows at all. Sessions that
started before this version appear without a host until their next event.

On Windows the hook also records the console window the agent runs in, and Open raises
it (Windows Terminal, including PowerShell or Command Prompt windows handed off to it, or
a classic console window). When no console window is found, it falls back to the window
that owns the agent's parent processes. tmux and herdr are not used on Windows.

An orange badge stays on the pet while any observed session waits for approval
or input, even after its alert is dismissed. It clears on that session's next
prompt, tool start, interruption, stop or end. Right-click → Mute alerts hides
bubbles (the badge remains). Settings also offer an optional sound for new
alerts and show each integration's status with Enable/Disable (or Update, for a
partial setup) buttons that merge only Agent Pet's hook entries. Closing settings keeps monitoring; Quit
stops it. Alerts live only in memory and are not replayed after restart.

```bash
printf '%s\n' '{"version":1,"provider":"claude","session_id":"a1b2c3","kind":"attention","reason":"approval","project_path":"/projects/demo"}' | ./build/agent-pet emit
```

## Provider setup

Preview the exact configuration before enabling either integration:

```bash
./build/agent-pet integration preview --provider claude
./build/agent-pet integration enable --provider claude
./build/agent-pet integration inspect --provider claude
./build/agent-pet integration disable --provider claude
```

Use `--provider codex` for Codex. Register from a permanent executable location.
Review new Codex hooks in `/hooks`; restart the client and submit a fresh turn to
verify setup. Use `--config /tmp/pet-test/settings.json` for disposable setup tests.
See [supported mappings and pending live acceptance](docs/integrations.md).

## Package and verify

Release users should follow [docs/install.md](docs/install.md) (install, integration
setup, upgrade, troubleshooting and removal). To build the package yourself,
Python 3, `qmake6` (or `QMAKE=/path/to/qmake`), `ldd` and `patchelf` are needed:

```bash
python3 scripts/package.py
./dist/agent-pet-0.6.0-linux-x86_64/bin/agent-pet --smoke-test
```

The output is `dist/agent-pet-VERSION-linux-ARCH/` and the matching `.tar.gz`,
with `install.sh`, `uninstall.sh`, the desktop launcher and icon, notices and a
runtime library manifest. The version comes from `project()` in CMakeLists.txt;
`-DAGENT_PET_REVISION=<commit>` records the source revision shown by
`agent-pet --version` and About. Output directories must be new.

Packaging also writes `-app.tar.gz`, `-runtime.tar.gz`, `-artwork.tar.gz`,
`-artwork-<sequence-hash>.tar.gz` and `-components.json` beside the full archive.
Each PNG sequence directory becomes a separate resource pack with a stable name
based on its path. Adding or editing a sequence only replaces that pack and the
small catalog; other sequences and the runtime are reused when their installed
files still match. Editing animation timing alone only changes the catalog.

`share/agent-pet/artwork.rcc` is the pet index (each pet's metadata, catalog, preview and pack
list), and `share/agent-pet/artwork-<sequence-hash>.rcc` contains each sequence's frames.
Local builds load the same files from `build/`. Component manifest format 2
supports these packs; the updater also accepts legacy format 1 bundles.
Older updaters fall back to the full archive for the first upgrade to this layout.
First installs still download all artwork. Keep the full archive for first
installs and older updaters. CI uploads the full archive and all components for
each release; this split reduces update downloads, not release upload volume.

Two checks exercise the package without a display:

```bash
python3 scripts/check_isolated.py dist/agent-pet-0.6.0-linux-x86_64
python3 scripts/check_install.py dist/agent-pet-0.6.0-linux-x86_64.tar.gz
```

`check_isolated.py` (bubblewrap, user namespaces) mounts only the package and
host glibc/loader, then runs the smoke test, both hook commands and integration
setup. `check_install.py` installs into a path with spaces under a throwaway
HOME, enables both integrations next to an unrelated hook, upgrades in place and
uninstalls, checking that settings survive upgrades and only Agent Pet's hook
entries are removed. The smoke test verifies idle/thinking, control changes and
15-second recovery, then plays shutdown; it does not replace the desktop
interaction checklist in [ADR 0008](docs/adr/0008-window-behavior.md#manual-acceptance-checklist).
The XWayland drag test runs separately because it moves the real pointer:

```bash
QT_QPA_PLATFORM=xcb ./build/desktop-tests
```

Run it only when the desktop is free. Native X11 and broader manual acceptance
evidence remain open.

The macOS app is packaged on a Mac with Qt's `macdeployqt` (found through
`qmake`), `codesign`, `ditto`, `otool`, `lipo`, `sips` and `iconutil`:

```bash
QMAKE=/path/to/qmake python3 scripts/package_macos.py --universal
```

It writes `dist/agent-pet-VERSION-macos-universal.zip` containing `Agent Pet.app`
(signed ad hoc, not notarized) and `INSTALL.txt`, and
`agent-pet-VERSION-macos-universal.dmg` with the same files plus an Applications
shortcut. Before zipping it checks that every binary loads only bundled or system
libraries and has both architectures, and runs `--version` and
`--check-update-runtime` from the bundle; it then verifies and mounts the disk image
read-only and checks its contents, signature and version.

The Windows release is packaged from a Visual Studio developer prompt with Qt's
`windeployqt` (found through `qmake`) and Inno Setup 6:

```powershell
$env:QMAKE = "C:\Qt\6.5.3\msvc2019_64\bin\qmake.exe"; python scripts/package_windows.py --smoke-test
python scripts/check_install_windows.py dist\agent-pet-VERSION-windows-x86_64-setup.exe
```

It writes `dist/agent-pet-VERSION-windows-x86_64.zip` (both executables, artwork, Qt,
the Visual C++ runtime, notices and `INSTALL.txt`) and the per-user setup program
`agent-pet-VERSION-windows-x86_64-setup.exe`, after running `--version`,
`--check-update-runtime` and the smoke test with only Windows on `PATH`. The install check
installs into a folder with spaces against throwaway Claude Code and Codex
configurations, runs each registered hook the way its agent does, upgrades and
uninstalls.

CI ([.github/workflows/release.yml](../.github/workflows/release.yml)) runs all of
the above from a clean checkout on Ubuntu 22.04, macOS 14 and Windows Server 2022 with
Qt 6.5.3, uploads the Linux full tarball, component tarballs and manifest, the macOS zip
and dmg and the Windows zip and setup program, and attaches them to a draft GitHub
release for `v*` tags. CI adds an English/Vietnamese download table for the three
operating systems above GitHub's generated release notes, with portable and source
downloads in a collapsible section. It verifies that all six linked packages exist
and are nonempty before creating the draft. Component assets remain available for
automatic updates.

### CI commit rules

- Use `docs: ...` or `docs(scope): ...` for documentation-only commits. On a push
  to `main`, a head commit message starting with `docs` skips all three platform
  build/test/package jobs. GitHub may still show a workflow run with skipped jobs.
- The rule checks the pushed head commit. For a squash merge, keep the `docs`
  prefix in its commit title; a default `Merge pull request ...` title does not
  match. Use the prefix only when the whole push is documentation-only.
- Documentation-only pull requests already skip builds by changed paths;
  bundled notices and licenses still trigger PR builds. Use a non-`docs` title
  for changes that affect the shipped app or build, including CI configuration.
- Version tags and **Run workflow** still build, even on a `docs` commit. A docs
  push does not cancel an already running code build. Existing `release/*`
  branch/merge skips remain in place; the version tag builds the release.

## Artwork and publishing

Artwork credit: **VUP-Simulator team**, via [LorisYounger/VPet](https://github.com/LorisYounger/VPet). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the [upstream artwork terms](licenses/VPET-ARTWORK-TERMS.md).

The upstream terms require source attribution and a link for noncommercial use and require the authorization information to accompany distributions. They list extra conditions for commercial use, including contacting the rights holder. Git itself adds no license and does not change those terms. A public repository containing these sprites is a distribution, so keep the notices and this credit in the repository. If the app shows the sprites to users, carry the credit into its About screen and release package as well.

Agent Pet's original application code is released under the [Apache License 2.0](LICENSE) (see also [NOTICE](NOTICE)). It does not cover the bundled artwork, the icon cropped from it, or anything under `licenses/`: the artwork keeps its separate upstream terms. The upstream VPet Apache-2.0 license copied in `licenses/` covers VPet's code only; neither it nor Agent Pet's license grants rights to the sprites.
