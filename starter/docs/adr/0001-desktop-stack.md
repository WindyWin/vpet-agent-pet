# 0001. Desktop stack: C++17, Qt 6 Widgets, CMake and Ninja

- Status: Accepted
- Date: 2026-10-04

## Context

Agent Pet shows the VPet character as a transparent, always-on-top desktop
pet that reacts to agent hooks. It needs translucent windows, native moves, input
pass-through and a tray menu on Linux desktops (X11, and Wayland through XWayland),
plus a headless command that agent clients run on every callback. The runtime must
not depend on a browser, a language interpreter or an original VPet installation.

## Decision

Use C++17, Qt 6 Widgets, CMake and Ninja. Develop in the `starter/` directory.
No original VPet installation is used.

Qt provides translucent top-level windows, native move requests, input-transparent
windows and tray menus with a small native application layer. The prototype uses
software QWidget painting, without a browser or language interpreter at runtime.

## Consequences

- One native binary with a small native layer; Qt and its dependencies are bundled
  in the release package ([0009](0009-linux-packaging.md)).
- Translucency, stacking and positioning depend on the compositor. Programmatic flag
  checks do not prove compositor behavior, so desktop behavior needs manual
  acceptance ([0008](0008-window-behavior.md)).
- Native Wayland is opt-in and unverified; X11/XWayland is the supported path.

## Validation

### M1 validation evidence (2026-10-04)

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
