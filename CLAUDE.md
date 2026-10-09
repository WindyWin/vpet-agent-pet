# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

Agent Pet is a desktop pet for Linux, macOS and Windows (C++17, Qt 6 Widgets, CMake/Ninja) that animates in response to Claude Code and Codex hooks.

- **`starter/` is the application.** All source, tests, packaging and app docs live there; CI runs with `working-directory: starter`. Almost all work happens in this directory.
- The repository root holds the full 749 MB upstream VPet artwork archive (`assets/vpet/pet/vup/`, ~5,500 frames) as a source bundle, its verifier (`scripts/assets.py`), the original `PLAN.md`, and README media (`docs/media/`). The app never reads root assets at runtime.
- `.claude/skills/gh-publish-release/` handles releases (version bump PR → merge → tag → wait for CI → publish). Only use it when the user asks for a release.

## Commands (run from `starter/`)

Build requirements: CMake 3.22+, Ninja, C++17, Qt 6.5+ (Widgets, DBus, Network, Test, LinguistTools), libarchive, X11 (+ Xtst for desktop tests). macOS needs only Qt (no DBus), Ninja and the Xcode tools; `scripts/package_macos.py` builds the `.app` zip and `.dmg` there. Windows needs MSVC (a VS developer prompt), Ninja and Qt for msvc2019_64; `scripts/package_windows.py` (windeployqt + Inno Setup 6) builds the zip and per-user `-setup.exe`, and `scripts/check_install_windows.py` checks install/upgrade/uninstall.

```bash
python3 scripts/verify_assets.py          # asset manifest/catalog check (also run in CI)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/agent-pet                         # run the pet (--preview, --settings, --state thinking, --pet <id>, --no-persist)
```

Tests are Qt Test executables registered with CTest (`updates`, `update-install`, `providers`, `events`, `alerts`, `focus`, `behavior`, `startup`, `prototype-1`…`prototype-4`, `i18n`, `pets`, `plugins`, plus the Python `pet-scaffold` check; macOS and Windows register all but `updates`, `update-install` and `startup`):

```bash
ctest --test-dir build -R events --output-on-failure            # one CTest suite
QT_QPA_PLATFORM=offscreen ./build/prototype-tests variantsAreDrawnByWeight   # one test function
```

`prototype-tests` and `update-tests` need `QT_QPA_PLATFORM=offscreen` when run directly (CTest sets it). CTest runs `prototype-tests` as four `--shard K/4` processes so `ctest -j` spreads the slowest suite over the cores (`-R prototype` still selects them all); its `main` deals the known-slow functions first, so list a new slow one there. `desktop-tests` is built but not registered: it moves the real pointer, so run it (`QT_QPA_PLATFORM=xcb ./build/desktop-tests`) only when the desktop is free.

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

- **Headless subcommands** (`hook`, `emit`, `integration`, `autostart`, `--version`) use `QCoreApplication` and link only `pet_events`. On Windows the same `main.cpp` builds `agent-pet.exe` (GUI subsystem, the pet) and `agent-pet-cli.exe` (console, for hooks and commands); `hookExecutable()`/`petExecutable()` map between them. `hook` runs inside agent clients: it must always exit 0 with no stdout/stderr (Qt message output is suppressed), finish fast (150 ms stdin deadline, one nonblocking send), and never transmit prompt/tool content. The hook only sends a one-bit `risky` verdict for destructive commands.
- **GUI pet**: takes the single-instance lock (the IPC `Receiver`), builds `PetWindow`, wires a `Monitor`, and starts the update `Controller`. On Linux it defaults to `xcb` when `DISPLAY` is set (XWayland); native Wayland is opt-in and unverified.

CMake libraries enforce this split:

| Target | Contents |
| --- | --- |
| `pet_events` (Qt Core only) | `sessions/` (session/tool state machine, alerts, presence), `ipc/` (event orchestration over the Unix datagram socket at `$XDG_RUNTIME_DIR/agent-pet-<uid>/events.sock`, or the per-user temp dir on macOS; autostart), `settings/` (atomic `preferences.json`), `providers/` (Claude/Codex hook → normalized event adapters, integration config merge) |
| `pet_hosts` (Qt Core only) | `hosts/`: `HostContext` and v1 conversion, the host `Registry` (capture, target codecs, labels, detection order), `FocusService`, tmux/herdr selection; `platform/desktop/window_match` |
| `pet_platform` | `platform/posix/` (event socket, hook input, detached launch, `QProcess` command runner) plus `platform/linux/` (`/proc` processes, XDG autostart) or `platform/macos/` (sysctl/libproc processes, launchd agent); or `platform/windows/` (named-pipe events, Toolhelp processes, CreateProcess, HKCU Run key, hook handlers) |
| `pet_native` | Linux: the only target linking X11 and D-Bus: `platform/desktop/x11` and `kwin` backends, X11 pointer queries, Konsole's D-Bus selection, the D-Bus screen-lock watcher (`platform/desktop/screensaver`). macOS: `platform/macos/native.cpp` (Core Graphics pointer and screen-lock queries, no window backend yet). Windows: `platform/windows/native.cpp` (Win32 window backend, pointer and screen-lock queries). Each provides `platform::createFocusService()` (composition) |
| `pet_updates` | release metadata validation, component manifests, libarchive installer (Linux; macOS only announces releases); also used by the separate Linux `agent-pet-updater` helper |
| `pet_behavior` (Qt Core only) | `behavior/`: the runtime that decides which competing behavior may show (policy classes, waiting, feedback, shutdown); no timers, no `Player` |
| `pet_ui` | `animation/` (catalog-driven player, `Stage` presenting the runtime on it, ambient fidgets, mood, easter eggs) and `desktop/` (`PetWindow`, `Monitor`, alert bubble, session list, touch, wander); no native includes |

Event flow: provider hook JSON → `providers/adapters.cpp` normalizes to protocol v1 → datagram → `Receiver` → `Monitor::apply` → `Sessions` (ordering, dedup, expiry, aggregate priority: attention > exhausted > error > turn-finished > working > reading > thinking > idle) → the aggregate state is submitted to the behavior runtime as the cue of the same name, which arbitrates it against touch, ambient, mood, easter eggs and reminders; `Stage` plays the winner's state on the `Player`, and `AlertQueue` drives the bubble and badge. Nothing about sessions is persisted except the daily `Recap` counters (`recap.json`, counts only). Open goes `Monitor` → `hosts::FocusService` (injected from `main.cpp`) → host `Activation` (select tab/pane) → `platform::DesktopBackend`s in order (X11, then KWin), returning separate selection and activation outcomes. `starter/docs/adr/0017-session-focus.md` has the per-host policies.

Each pet is a folder `assets/<id>/` (VPet: `assets/vpet/`) with `pet.json`, a `preview.png` and the `animations.json` catalog. The catalog covers display states, sequences with per-frame durations, playback phases (start → loop → end, one-shots), weighted variants, mood art, cue mappings and reaction pools, ambient fidgets, touch hit boxes and moves. Behavior is largely data-driven from it: `Catalog` validates it at load, and `PetLibrary` registers only the chosen pet's packs. `cmake/pets.cmake` builds each frame folder into a separate `artwork-<sha>.rcc` and indexes every pet's metadata, catalog, preview and hash tree in `artwork.rcc` (not the executable), so updates can reuse unchanged artwork. Only `AGENT_PET_BUNDLED_PETS` (default `vpet`) install their packs; other pets download on demand (`PetDownloader`) from the `pets` release into a per-user content-addressed store (`<data>/pets/<sha256>.rcc`), verified against the index's leaves. Event code never names an animation: it raises cues from the vocabulary in `src/animation/cues.json`, and each catalog may map a cue to its own state or reaction pool (catalog schema 2; `scripts/migrate_catalog.py` upgrades older ones). Behaviors never select on the `Player` or check each other: they submit a `behavior::Intent` (source, key, cue, policy class, lifetime) to `window.stage().runtime()` and act on its outcomes; `starter/docs/adr/0031-behavior-runtime.md` has the policy table. `docs/pets.md` is the contributor guide. User-installed **plugin packs** (`<data>/plugins/<id>/`: `plugin.json`, an `animations.json` fragment, PNG frames) extend one pet: `PetLibrary::activate` merges each enabled pack between `Catalog::read` and `Catalog::build` (`plugins::apply`), re-validating after each and dropping a pack that fails; new names are `<id>.<name>` and replacing the pet's states, cue mappings, pools or mood art needs an `overrides` entry (`starter/docs/plugins.md`, `adr/0033-plugin-packs.md`). A pack's optional `events.json` adds rules (`EventRules`, applied by `Monitor::react`) that answer `custom` events (`agent-pet emit --custom NAME`: a reaction kind that never reaches `Sessions`) and some agent events, as Surprise-class intents that give way to a session needing the user (`adr/0034-custom-events.md`). The pet's own moments are triggers in the same table (`src/animation/triggers.json`: danger, konami, celebrations and treats, special-day fidgets, reminders): code raises a trigger and keeps its class and timing, and `Player::rules()` (the pet's own rule per trigger, drawing from the cue's pool, then the packs') decides what plays. Never draw from `Player::pool` in event code (`adr/0035-built-in-triggers-as-rules.md`).

## Conventions and gotchas

- **Adding sprites**: copy them from the root archive (the default `--source`) with `scripts/add_sequences.py IDEL/yawning/Nomal …`. The script updates `manifest.json`, `available-animations.json` and the catalog; then map them in `animations.json`. For another pet, use `--pet <id> --source DIR`, and start a pet with `scripts/new_pet.py`. `verify_assets.py` checks every pet folder and fails if any frame is missing from its manifest *or* unused by its catalog. Keep VPet's files where they are: moving them renames its packs and makes every user download them again.
- Test hooks: `Monitor::hostActive` / `bringForward` are `std::function`s (returning `ActiveState` / `FocusResult`) replaced in tests; `tests/focus_tests.cpp` drives `FocusService` with fake activations, backends, runners and process services. Follow those patterns instead of adding real-desktop dependencies to tests.
- Adding a host: register its `Capture` in `hosts::Registry::builtin()` (order is detection precedence) and its `Activation`, if any, in `platform::createFocusService()` (`platform/linux/native.cpp`, and `platform/macos/native.cpp` where it applies). Adding a desktop backend: implement `platform::DesktopBackend`, add it to `pet_native` and register it there. Neither touches sessions, alerts or UI. Keep v1 wire fields unchanged.
- `preferences.json` can be written concurrently by the headless `autostart` command, so the pet re-reads it before every save. Keep that when touching settings.
- Integration enable/disable must merge only Agent Pet's own hook entries (recognized by command markers) and preserve foreign handlers. `check_install.py` enforces this.
- Interface text is translated (English source, Vietnamese in `starter/translations/agent-pet_vi.ts`; see `starter/docs/i18n.md`). Wrap user-visible strings in `tr()`: the class's own in QObjects, otherwise a context from `src/i18n/contexts.h` (`Pet` is the pet's own voice). Use `%1` with `.arg()`, singular/plural pairs instead of `%n`, and give dialog buttons explicit `tr()` text (Qt has no Vietnamese catalog). CLI output, the hook, logs and developer diagnostics stay plain English. After changing strings run `cmake --build build --target update_translations`, translate the new entries and commit the `.ts`; CI runs `scripts/check_translations.py` to catch a stale file. Widgets relabel in `changeEvent(QEvent::LanguageChange)` → `retranslate()`.
- Docs are kept in step with features: `starter/docs/events.md` (protocol is authoritative there), `integrations.md` (provider mappings), `install.md`, `i18n.md` (translation workflow), and `architecture.md` (short overview and code map). Each design decision or feature gets an ADR in `starter/docs/adr/` (`NNNN-slug.md`: Context, Decision, Consequences, and a Validation section of dated evidence) plus a row in `adr/README.md`; later evidence is appended to the existing ADR. User-visible features also get a section in `starter/README.md`. `starter/docs/platform-refactor-plan.md` is the platform refactor plan (steps 1–5 implemented); the macOS and Windows ports build on it (`adr/0020-macos-port.md`, `adr/0024-windows-port.md`).
- Hook registration differs on Windows: Claude Code gets the exec form (`command` + `args`, no shell), Codex an unquoted `cmd.exe` command (8.3 short path for folders with spaces). Change `hookHandler()` and `ownedHookHandler()` together.
- Platform code shared by Linux and macOS goes in `platform/posix/`; keep OS-specific `#ifdef`s there minimal and put larger differences in `platform/linux/` or `platform/macos/`. Qt tool windows need `WA_MacAlwaysShowToolWindow` or macOS hides them while another app is active.
- Artwork is under the separate VPet artwork terms, not the app's Apache-2.0 license. Keep `licenses/`, `THIRD_PARTY_NOTICES.md` and the credit with any distribution.
