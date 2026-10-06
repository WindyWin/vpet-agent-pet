# 0002. Module boundaries

- Status: Accepted
- Date: 2026-10-04

## Context

The same binary serves the GUI pet and headless commands that run inside agent
clients. Native desktop code (X11, D-Bus, widgets) must stay out of the hook path and
out of tests that cannot use a real desktop.

## Decision

Split the source into directories with one responsibility each; CMake libraries
(`pet_events`, `pet_hosts`, `pet_platform`, `pet_native`, `pet_updates`, `pet_ui`)
enforce which of them may link Qt Widgets, X11 or D-Bus.

| Directory | Responsibility |
| --- | --- |
| `src/desktop` | Transparent pet, alert toast, running-sessions list, Open through the focus service, attention badge, context/tray menu, tray status and hiding, drag, touch gestures (`touch`), walking geometry (`wander`), scale, input and quit |
| `src/animation` | Catalog validation, phased playback with weighted variants and mood art, the idle fidget scheduler (`ambient`), the mood score and celebrations (`mood`) and bounded decoded-frame cache |
| `src/settings` | Validated, atomic preference storage in the user data directory |
| `src/sessions` | Bounded session/tool state, ordering, aggregate activity, alerts and alert labels, and the widget-free show/hide/idle rules (`presence`) |
| `src/ipc` | Private Unix transport, headless hook/emit commands, and hook-side autostart with the `autostart` command |
| `src/providers` | Claude/Codex normalization and integration configuration management |
| `src/hosts` | Host adapters: the neutral `HostContext` and protocol v1 conversion, the registry (capture, target codecs, labels, detection order), the session focus service, and Konsole/tmux/herdr selection |
| `src/platform` | Small contracts for native services (`contracts/`), Linux process and command services (`linux/`), X11 and KWin desktop backends and pointer queries (`desktop/`), explicitly unavailable services (`unsupported/`), and the composition point `native.h` |

## Consequences

- Headless commands link Qt Core only and never touch a display.
- Sessions, alerts and animation logic are testable offscreen; native services sit
  behind small contracts in `src/platform` that tests replace.
- New hosts and desktop backends plug in without changing sessions or UI
  ([0017](0017-session-focus.md)).
