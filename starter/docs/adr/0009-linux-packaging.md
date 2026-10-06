# 0009. Relocatable Linux package and per-user install

- Status: Accepted
- Date: 2026-10-04

## Context

Users should run the pet without installing Qt, the package must run from any
directory, and hook commands written into client configuration need a stable path.

## Decision

The portable-core profile builds capture, focus contracts, state and animation
without native implementations or application targets. The normal Linux and macOS
profiles add headless services, native desktop backends, updates and UI. See the
[platform target map](../../src/platform/README.md#build-registration) and README
for core configure/test commands. The full app fails clearly on other OSes.

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
dependencies retain their own licenses. See [install.md](../install.md).

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

## Consequences

- glibc, the ELF loader, the display server, fonts and graphics drivers remain host
  dependencies; release builds target Ubuntu 22.04's glibc.
- Upgrades swap directories, so a running pet keeps its files and an interrupted copy
  leaves the old install.
- Uninstall removes only what the receipt records and only Agent Pet's own hooks.

## Validation

### M6 packaging evidence — 2026-10-04

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
