# M1 desktop prototype

Decision recorded 2026-10-04: C++17, Qt 6 Widgets, CMake and Ninja.
Develop in this starter directory. No original VPet installation is used.

Qt provides translucent top-level windows, native move requests, input-transparent
windows and tray menus with a small native application layer. The prototype uses
software QWidget painting, without a browser or language interpreter at runtime.
The selected 180-frame artwork pack, catalog and artwork terms are embedded as Qt
resources, so moving the executable cannot break sprite lookup.

## Boundaries

| Directory | Responsibility |
| --- | --- |
| `src/desktop` | Transparent pet, context/tray menu, drag, scale, input and quit |
| `src/animation` | Catalog lookup and timed idle / held-thinking loops |
| `src/settings` | Prototype defaults; persistence deferred to M2 |
| `src/sessions` | Reserved for M3 state management; currently documentation only |
| `src/ipc` | Reserved for M3 private transport; currently documentation only |
| `src/providers` | Reserved for M4 adapters; currently documentation only |

M1 loops `Default/Nomal/1` and `Think/Nomal/B` using each frame's duration.
Start/end transitions and all other state playback belong to M2. The player owns
one current pixmap; Qt may additionally use its default pixmap cache. There is no
unbounded application frame cache. Settings and agent sessions are not persisted.

TODO (M2): add the original dragging animation from the `Raise/` sequences listed
in `assets/vpet/available-animations.json`. Import the selected frames and update
the manifest/catalog, then connect playback to drag start and completion (including
native system moves) and restore the previous state afterward. Dragging currently
keeps the selected idle/thinking animation; dedicated drag playback is deferred.

## Window behavior

The pet requests a borderless, translucent, always-on-top tool window. Left drag
uses `QWindow::startSystemMove`, with coordinate movement as a fallback. Right-click
opens controls. Space switches preview state, Menu opens controls and Escape quits
while the pet has focus. These are local shortcuts, not global desktop bindings.

Click-through is deliberately a 15-second lease in this prototype. A single-shot
timer restores input whether or not a tray exists. The tray menu can immediately
recover input and position, or quit. The context menu also quits. Closing the pet
quits; closing About does not. Position recovery moves to the primary screen's
lower right on X11/XWayland. Settings persistence and monitor-change handling are M2.

When DISPLAY is set, the default backend is `xcb`, including under Wayland through
XWayland. Set `QT_QPA_PLATFORM=wayland` explicitly for native Wayland experiments
using a development Qt installation. The M1 package includes xcb and offscreen
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

The package is a **private development prototype**, not an M6 release. Host glibc,
the ELF loader, a display server/compositor, fonts/font configuration and graphics
drivers remain operating-system dependencies. The observed build host uses glibc
2.44; this binary is not promised to work on older Linux distributions. M6 should
build on an explicit older supported baseline and test each supported distribution.
The packaging script collects Arch package/common license notices when available;
other distros need a corresponding notice collector and a complete distribution
license/source-offer review before publishing. The original-code license remains
the owner's decision. Qt and transitive dependencies retain their own licenses.

## Validation evidence (2026-10-04)

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

Provisional M2 performance targets on this hardware: under 80 MiB peak RSS and
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
