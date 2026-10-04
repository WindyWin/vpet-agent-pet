# Agent Pet

A small starting repository for a desktop pet that reacts to Claude Code and Codex activity. The sprite files are bundled here, so development can proceed without a local VPet installation.

The current asset pack contains **215 original PNG frames in 23 animation sequences (26.08 MiB)**. They cover idle, thinking, reading, working, waiting for input, tool errors, turn completion, sleeping, startup, closing, and the original dragging animation. [animations.json](assets/vpet/animations.json) maps each state to its sequences and records frame durations. [manifest.json](assets/vpet/manifest.json) records every file's size and SHA-256 hash.

[available-animations.json](assets/vpet/available-animations.json) catalogs the **535 remaining sequences and 5,283 frames** from the original character pack. Each entry gives its upstream folder, category, frame count, size, and timing status. These are references for future work; their PNG files are not included here. To add one later, copy that sequence from the upstream VPet tree, add it to the bundled animation map and manifest, and retain the artwork notices.

This folder is ready to become its own repository:

```bash
cd agent-pet/starter
python3 scripts/verify_assets.py
git init
```

The asset verifier uses only Python's standard library and works from any current directory. M3 local events and session tracking are implemented alongside the M2 animation and desktop controls. M4 provider adapters and integration setup are implemented; live-client acceptance remains open. M5 alerts and minimal settings are implemented. M6 adds the release package, installer and CI. See [PLAN.md](PLAN.md) and the [architecture and validation record](docs/architecture.md).

## Build and run

From this directory, install CMake 3.22+, Ninja, a C++17 compiler and Qt 6.5+
Widgets/Test/Network, libarchive and X11 development packages, then run:

```bash
python3 scripts/verify_assets.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/agent-pet
```

Left-drag to move; the original Raise animation plays while dragging and returns
to the prior state on release. Right-click for animation preview, settings,
always-on-top, temporary click-through, recovery, artwork terms and Quit. A tray
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
settings or preview leaves the pet running. About shows the bundled artwork terms.
Settings → Startup (or `agent-pet autostart enable [--when-idle keep|hide|quit]`)
lets a connected agent's session start launch the pet, and chooses whether it
stays, hides or quits once no sessions remain; see [install guide](docs/install.md#autostart).

The default uses X11/XWayland when DISPLAY is available. For a development-only
native Wayland check: `QT_QPA_PLATFORM=wayland ./build/agent-pet`.
Native Wayland placement and stacking support remain unverified.

## Updates

**Settings → Updates** offers daily release notifications, automatic downloads,
and optional installation on the next normal launch. Notify only is the default.
Updates preserve settings and hooks and restore the previous version if startup
fails. Development builds offer notifications and manual downloads.
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

## Alerts and running sessions

Approval/input requests and tool errors raise a one-line toast beside the pet,
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
the alert visible if it cannot raise a window. Sessions that
started before this version appear without a host until their next event.

An orange badge stays on the pet while any observed session waits for approval
or input, even after its alert is dismissed. It clears on that session's next
prompt, tool start, interruption, stop or end. Right-click → Mute alerts hides
bubbles (the badge remains). Settings also offer an optional sound for new
alerts and show each integration's status with Enable/Disable buttons that
merge only Agent Pet's hook entries. Closing settings keeps monitoring; Quit
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
interaction checklist in [architecture.md](docs/architecture.md).
The XWayland drag test runs separately because it moves the real pointer:

```bash
QT_QPA_PLATFORM=xcb ./build/desktop-tests
```

Run it only when the desktop is free. Native X11 and broader manual acceptance
evidence remain open.

CI ([.github/workflows/release.yml](../.github/workflows/release.yml)) runs all of
the above from a clean checkout on Ubuntu 22.04 with Qt 6.5.3, uploads the
tarball, and attaches it to a draft GitHub release for `v*` tags.

## Artwork and publishing

Artwork credit: **VUP-Simulator team**, via [LorisYounger/VPet](https://github.com/LorisYounger/VPet). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the [upstream artwork terms](licenses/VPET-ARTWORK-TERMS.md).

The upstream terms require source attribution and a link for noncommercial use and require the authorization information to accompany distributions. They list extra conditions for commercial use, including contacting the rights holder. Git itself adds no license and does not change those terms. A public repository containing these sprites is a distribution, so keep the notices and this credit in the repository. If the app shows the sprites to users, carry the credit into its About screen and release package as well.

Agent Pet's original application code is released under the [Apache License 2.0](LICENSE) (see also [NOTICE](NOTICE)). It does not cover the bundled artwork, the icon cropped from it, or anything under `licenses/`: the artwork keeps its separate upstream terms. The upstream VPet Apache-2.0 license copied in `licenses/` covers VPet's code only; neither it nor Agent Pet's license grants rights to the sprites.
