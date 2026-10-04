# Agent Pet

A small starting repository for a desktop pet that reacts to Claude Code and Codex activity. The sprite files are bundled here, so development can proceed without a local VPet installation.

The first asset pack contains **180 original PNG frames in 20 animation sequences (21.85 MiB)**. They cover idle, thinking, reading, working, waiting for input, tool errors, turn completion, sleeping, startup, and closing. [animations.json](assets/vpet/animations.json) maps each state to its sequences and records frame durations. [manifest.json](assets/vpet/manifest.json) records every file's size and SHA-256 hash.

[available-animations.json](assets/vpet/available-animations.json) catalogs the **538 remaining sequences and 5,318 frames** from the original character pack. Each entry gives its upstream folder, category, frame count, size, and timing status. These are references for future work; their PNG files are not included here. To add one later, copy that sequence from the upstream VPet tree, add it to the bundled animation map and manifest, and retain the artwork notices.

This folder is ready to become its own repository:

```bash
cd agent-pet/starter
python3 scripts/verify_assets.py
git init
```

The asset verifier uses only Python's standard library and works from any current directory. The M1 desktop prototype is implemented; agent hooks are planned. See [PLAN.md](PLAN.md) and the [architecture and validation record](docs/architecture.md).

## Build and run the M1 prototype

From this directory, install CMake 3.22+, Ninja, a C++17 compiler and Qt 6.5+
Widgets/Test development packages, then run:

```bash
python3 scripts/verify_assets.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/agent-pet
```

Left-drag to move. Right-click for Idle/Thinking, size, always-on-top, temporary
click-through, recovery, About and Quit. A tray menu appears on supported desktops.
Click-through automatically ends after 15 seconds. Space switches animation and
Escape quits when the pet has focus. `--state thinking` starts with thinking.

The default uses X11/XWayland when DISPLAY is available. For a development-only
native Wayland check: `QT_QPA_PLATFORM=wayland ./build/agent-pet`.
Native Wayland placement and stacking support remain unverified.

## Package and verify

Python 3, `qmake6`, `ldd` and `patchelf` are build-time packaging tools:

```bash
python3 scripts/package.py
./dist/agent-pet-m1/bin/agent-pet --smoke-test
```

The output is `dist/agent-pet-m1/` and `dist/agent-pet-m1.tar.gz`. Use a new
`--output` directory on subsequent runs. Copy/extract the entire directory to
another location and run `bin/agent-pet`; no separate Qt or interpreter is needed.
The package targets a compatible Linux host ABI, not every distribution.

For a filesystem-isolated headless check (requires bubblewrap and permitted user
namespaces), run:

```bash
python3 scripts/check_isolated.py dist/agent-pet-m1
```

This mounts only the package and host glibc libraries. It verifies idle/thinking,
control changes and 15-second recovery, then exits after 17 seconds. It does not
replace the desktop interaction checklist in [architecture.md](docs/architecture.md).
M1 still needs native X11 and manual desktop acceptance evidence before completion.

## Artwork and publishing

Artwork credit: **VUP-Simulator team**, via [LorisYounger/VPet](https://github.com/LorisYounger/VPet). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the [upstream artwork terms](licenses/VPET-ARTWORK-TERMS.md).

The upstream terms require source attribution and a link for noncommercial use and require the authorization information to accompany distributions. They list extra conditions for commercial use, including contacting the rights holder. Git itself adds no license and does not change those terms. A public repository containing these sprites is a distribution, so keep the notices and this credit in the repository. If the app shows the sprites to users, carry the credit into its About screen and release package as well.

No license has been selected for the new Agent Pet application code. Choose one before inviting reuse or contributions, and state clearly that it applies to your original code while the bundled artwork keeps its separate terms. The upstream Apache-2.0 license copied in `licenses/` is for VPet code and does not grant an Apache-2.0 license to the sprites or future Agent Pet code.
