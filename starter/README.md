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

The asset verifier uses only Python's standard library and works from any current directory. M3 local events and session tracking are implemented alongside the M2 animation and desktop controls. M4 provider adapters and integration setup are implemented; live-client acceptance remains open. M5 alerts and minimal settings are implemented. See [PLAN.md](PLAN.md) and the [architecture and validation record](docs/architecture.md).

## Build and run

From this directory, install CMake 3.22+, Ninja, a C++17 compiler and Qt 6.5+
Widgets/Test and X11 development packages, then run:

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
menu appears on supported desktops. Click-through automatically ends after 15
seconds. Space switches idle/thinking and Escape quits when the pet has focus.

`./build/agent-pet --preview` opens the developer preview, where every bundled
state can be selected. It shows the active sequence, phase, frame duration,
transition history and decoded-frame cache use; you can pause and step frames.
`--state thinking` starts with another state. `--settings` opens the settings
window. Size, position and always-on-top are saved in the user's application data
directory and restored on restart. Click-through always starts disabled. Closing
settings or preview leaves the pet running. About shows the bundled artwork terms.

The default uses X11/XWayland when DISPLAY is available. For a development-only
native Wayland check: `QT_QPA_PLATFORM=wayland ./build/agent-pet`.
Native Wayland placement and stacking support remain unverified.

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

## Alerts

Approval/input requests, tool errors and finished turns raise a compact bubble
beside the pet, for example `Needs approval` / `fcis-web · Claude Code · a1b2`.
Hover the project for its full path. When several projects share a folder name,
the parent folder is added. The footer shows how many alerts are pending; Next
cycles them and Dismiss hides the shown one. Repeated alerts from the same
session and reason are counted (`×2`) rather than queued again. A new request
that outranks the shown alert takes its place. Reply in the agent's own
terminal or editor; Agent Pet never answers a request.

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

Python 3, `qmake6`, `ldd` and `patchelf` are build-time packaging tools:

```bash
python3 scripts/package.py
./dist/agent-pet-m5/bin/agent-pet --smoke-test
```

The output is `dist/agent-pet-m5/` and `dist/agent-pet-m5.tar.gz`. Use a new
`--output` directory on subsequent runs. Copy/extract the entire directory to
another location and run `bin/agent-pet`; no separate Qt or interpreter is needed.
The package targets a compatible Linux host ABI, not every distribution.

For a filesystem-isolated headless check (requires bubblewrap and permitted user
namespaces), run:

```bash
python3 scripts/check_isolated.py dist/agent-pet-m5
```

This mounts only the package and host glibc libraries. It verifies idle/thinking,
control changes and 15-second recovery, then plays shutdown and exits after about 19 seconds. It does not
replace the desktop interaction checklist in [architecture.md](docs/architecture.md).
The XWayland drag test runs separately because it moves the real pointer:

```bash
QT_QPA_PLATFORM=xcb ./build/desktop-tests
```

Run it only when the desktop is free. Native X11 and broader manual acceptance
evidence remain open. See the M2 record in [architecture.md](docs/architecture.md).

## Artwork and publishing

Artwork credit: **VUP-Simulator team**, via [LorisYounger/VPet](https://github.com/LorisYounger/VPet). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the [upstream artwork terms](licenses/VPET-ARTWORK-TERMS.md).

The upstream terms require source attribution and a link for noncommercial use and require the authorization information to accompany distributions. They list extra conditions for commercial use, including contacting the rights holder. Git itself adds no license and does not change those terms. A public repository containing these sprites is a distribution, so keep the notices and this credit in the repository. If the app shows the sprites to users, carry the credit into its About screen and release package as well.

No license has been selected for the new Agent Pet application code. Choose one before inviting reuse or contributions, and state clearly that it applies to your original code while the bundled artwork keeps its separate terms. The upstream Apache-2.0 license copied in `licenses/` is for VPet code and does not grant an Apache-2.0 license to the sprites or future Agent Pet code.
