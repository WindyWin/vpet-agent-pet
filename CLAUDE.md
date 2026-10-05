# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

Agent Pet is a Linux desktop pet (C++17, Qt 6 Widgets, CMake/Ninja) that animates in response to Claude Code and Codex hooks.

- **`starter/` is the application.** All source, tests, packaging and app docs live there; CI runs with `working-directory: starter`. Almost all work happens in this directory.
- The repository root holds the full 749 MB upstream VPet artwork archive (`assets/vpet/pet/vup/`, ~5,500 frames) as a source bundle, its verifier (`scripts/assets.py`), the original `PLAN.md`, and README media (`docs/media/`). The app never reads root assets at runtime.
- `.claude/skills/gh-publish-release/` handles releases (version bump PR → merge → tag → wait for CI → publish). Only use it when the user asks for a release.

## Commands (run from `starter/`)

Build requirements: CMake 3.22+, Ninja, C++17, Qt 6.5+ (Widgets, DBus, Network, Test), libarchive, X11 (+ Xtst for desktop tests).

```bash
python3 scripts/verify_assets.py          # asset manifest/catalog check (also run in CI)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/agent-pet                         # run the pet (--preview, --settings, --state thinking, --no-persist)
```

Tests are Qt Test executables registered with CTest (`updates`, `update-install`, `providers`, `events`, `alerts`, `startup`, `prototype`):

```bash
ctest --test-dir build -R events --output-on-failure            # one CTest suite
QT_QPA_PLATFORM=offscreen ./build/prototype-tests variantsAreDrawnByWeight   # one test function
```

`prototype` and `updates` need `QT_QPA_PLATFORM=offscreen` when run directly (CTest sets it). `desktop-tests` is built but not registered: it moves the real pointer, so run it (`QT_QPA_PLATFORM=xcb ./build/desktop-tests`) only when the desktop is free.

Send events to a running pet without hooks:

```bash
printf '%s\n' '{"version":1,"provider":"claude","session_id":"demo","kind":"prompt"}' | ./build/agent-pet emit
printf '%s\n' '{"session_id":"demo","hook_event_name":"PermissionRequest"}' | ./build/agent-pet hook --provider claude
```

Packaging (needs `qmake6` or `QMAKE=…`, `ldd`, `patchelf`; output dirs must be new):

```bash
python3 scripts/package.py
python3 scripts/check_isolated.py dist/agent-pet-<ver>-linux-x86_64        # bubblewrap sandbox smoke test
python3 scripts/check_install.py dist/agent-pet-<ver>-linux-x86_64.tar.gz  # install/upgrade/uninstall
```

The version comes only from `project(AgentPet VERSION …)` in `starter/CMakeLists.txt`; CI fails a `v*` tag that doesn't match it.

## Architecture

One binary, `agent-pet`, has two personalities chosen in `src/main.cpp` before any `QApplication` exists:

- **Headless subcommands** (`hook`, `emit`, `integration`, `autostart`, `--version`) use `QCoreApplication` and link only `pet_events`. `hook` runs inside agent clients: it must always exit 0 with no stdout/stderr (Qt message output is suppressed), finish fast (150 ms stdin deadline, one nonblocking send), and never transmit prompt/tool content. The hook only sends a one-bit `risky` verdict for destructive commands.
- **GUI pet**: takes the single-instance lock (the IPC `Receiver`), builds `PetWindow`, wires a `Monitor`, and starts the update `Controller`. Defaults to `xcb` when `DISPLAY` is set (XWayland); native Wayland is opt-in and unverified.

CMake libraries enforce this split:

| Target | Contents |
| --- | --- |
| `pet_events` (Qt Core only) | `sessions/` (session/tool state machine, alerts, presence), `ipc/` (Unix datagram socket at `$XDG_RUNTIME_DIR/agent-pet-<uid>/events.sock`, autostart), `settings/` (atomic `preferences.json`), `providers/` (Claude/Codex hook → normalized event adapters, integration config merge, host detection from `/proc`) |
| `pet_updates` | release metadata validation, component manifests, libarchive installer; also used by the separate `agent-pet-updater` helper |
| `pet_ui` | `animation/` (catalog-driven player, ambient fidgets, mood, easter eggs) and `desktop/` (`PetWindow`, `Monitor`, alert bubble, session list, touch, wander, host focus via X11/D-Bus/tmux/herdr) |

Event flow: provider hook JSON → `providers/adapters.cpp` normalizes to protocol v1 → datagram → `Receiver` → `Monitor::apply` → `Sessions` (ordering, dedup, expiry, aggregate priority: attention > error > turn-finished > working > reading > thinking > idle) → `PetWindow`/`Player` selects the animation state, and `AlertQueue` drives the bubble and badge. Nothing about sessions is persisted.

`assets/vpet/animations.json` is the animation catalog: display states, sequences with per-frame durations, playback phases (start → loop → end, one-shots), weighted variants, mood art, reactions, ambient fidgets, touch hit boxes and moves. Behavior is largely data-driven from it; the player validates it at load. The artwork and catalog are compiled into a separate `artwork.rcc` (not the executable), so updates can reuse unchanged artwork.

## Conventions and gotchas

- **Adding sprites**: copy them from the root archive (the default `--source`) with `scripts/add_sequences.py IDEL/yawning/Nomal …`. The script updates `manifest.json`, `available-animations.json` and the catalog), then map them in `animations.json`. `verify_assets.py` fails if any PNG under `assets/vpet/vup` is missing from the manifest *or* unused by the catalog.
- Test hooks: `Monitor::hostActive` / `bringForward` are `std::function`s replaced in tests; follow that pattern instead of adding real-desktop dependencies to tests.
- `preferences.json` can be written concurrently by the headless `autostart` command, so the pet re-reads it before every save. Keep that when touching settings.
- Integration enable/disable must merge only Agent Pet's own hook entries (recognized by command markers) and preserve foreign handlers. `check_install.py` enforces this.
- Docs are kept in step with features: `starter/docs/events.md` (protocol is authoritative there), `integrations.md` (provider mappings), `install.md`, and `architecture.md` (a design section per feature plus a dated evidence section). User-visible features also get a section in `starter/README.md`. `starter/docs/platform-refactor-plan.md` is a proposed, not-yet-implemented refactor.
- Artwork is under the separate VPet artwork terms, not the app's Apache-2.0 license. Keep `licenses/`, `THIRD_PARTY_NOTICES.md` and the credit with any distribution.
