# 0017. Session focus: host registry, focus service and desktop backends

- Status: Accepted
- Date: 2026-10-05

## Context

Open must return to the terminal or editor hosting a session. Host detection, tab
or pane selection and window raising lived in `src/desktop/host_focus.*` with direct
X11 and D-Bus calls, which made other desktops and operating systems hard to add.

This is phases 1–3 of the platform extraction: [plan and completion
criteria](../platform-refactor-plan.md), [services and build
registration](../../src/platform/README.md). When it landed, the IPC, startup,
updater and build seams remained. It prepared extension points while retaining
the Linux behavior of the time; it did not add support for another operating
system or desktop environment. The later steps and the macOS port followed
([0020](0020-macos-port.md)).

## Decision

Open, from the bubble or a session row, returns to the application hosting an
existing session. It never starts a terminal or editor, or resumes an agent.
Three concerns are kept apart:

| Concern | Code | Links |
| --- | --- | --- |
| Host capture: detect the host in a hook's environment, decode its target, name it | `hosts::Registry`, `hosts/adapters/<host>.cpp` | Qt Core only; used by `hook` |
| Host activation: select the tab or pane, name the windows to raise | `hosts::Activation` per host, `hosts::FocusService` | Platform contracts; Konsole's half needs D-Bus and lives in `pet_native` |
| Desktop activation and observation: raise or check a native window | `platform::DesktopBackend`: `x11`, `kwin` | `pet_native`, the only target linking X11 and D-Bus |

`hosts::HostContext` is the internal descriptor: adapter ID, process hints nearest
first, an optional backend-qualified window reference (`{"x11", "<id>"}`) and target
data only the adapter decodes. Protocol v1 fields convert to and from it at the
event boundary (`hosts::fromV1`/`toV1`); a non-X11 window is never written into
`host_window`. `Event::parse` asks the registry which host IDs exist, and the
session list asks it for labels. Sessions store the descriptor and know no host names.

Capture order is registration order: herdr, tmux, Konsole, VS Code, then any
terminal (needs ancestors). Programs inherit their terminal's variables, so a
`Capture` may name its program: herdr, tmux and Konsole count only when one of the
hook's ancestors has that kernel name (`/proc/<pid>/comm`; any name is accepted when
the ancestry is unreadable), and VS Code never takes `$WINDOWID`. `FocusService::focus` copies the context (the monitor
copies the session, because KWin's callback processes events), checks the target
with the adapter's codec before any side effect, selects, then offers each window
the adapter names to each backend in order until one raises it. Selection and
activation are reported separately as `Skipped`, `Unsupported`, `MissingTarget`,
`TargetNotFound`, `Failed`, `TimedOut`, `Requested` or `Confirmed`; only a raised
window (`Requested` or `Confirmed`) dismisses the session's bubbles, as the former
boolean did.

| Host | Selection | When selection fails | Windows tried, in order | Active-window bubble suppression |
| --- | --- | --- | --- | --- |
| Konsole | D-Bus `setCurrentSession` | Raises anyway | Its hints | Yes |
| tmux | `select-window`, `select-pane` | Stops (also when `tmux` is missing) | Attached clients' ancestors, then its own process hints, as one request | Never: `Unknown` |
| herdr | `herdr tab focus`, `herdr agent focus` | Stops | Each live UI client of the same API socket, after selecting the Konsole tab it runs in; then its own hints | Never: `Unknown` |
| VS Code, terminal | None | — | Its hints | Yes |

**Hooks that run outside the terminal.** Codex runs hooks from a shared
`codex app-server` daemon whose parent is `systemd --user`, not the pane. The hook
sees no `herdr` ancestor, so it reports a bare `terminal` host, and the daemon's
inherited `HERDR_PANE_ID` names whichever pane first started it, so it is never
trusted. The hook must stay fast and never block, so it does not look further. At
Open time, a `terminal` host whose session has a provider goes through the
`FocusService` locators: the herdr locator runs `herdr agent list` and takes the
agent whose `agent` equals the provider and whose `cwd` equals the session's
project. With several matches it takes the focused one; if that is still ambiguous
(or the lookup fails) it keeps the captured host and guesses nothing. The result is
an ordinary herdr host, so selection and client raising work as above, and the
captured pids stay as hints. Resolution is read-only and never sent over the wire,
so v1 fields are unchanged.

A malformed target is `MissingTarget`: nothing is selected or run, and the window is
still raised from the remaining hints, as before. Commands are argument arrays run
through `platform::CommandRunner` (1.5 s each; common user tool directories are
searched because desktop launchers often lack them on PATH).

Backends are chosen by what the session offers, never by distribution. `x11` (X11
and XWayland) matches `$WINDOWID` first, then the windows of the nearest process
hint, preferring a title that names the project, and sends `_NET_ACTIVE_WINDOW`:
`Requested`, since the window manager decides. It is `Unsupported` off `xcb`. `kwin`
loads a temporary KWin script that picks only an unambiguous window and reports
back over D-Bus: `Confirmed`, `Failed` or `TimedOut` after 1.5 s; `Unsupported`
without KWin's scripting service. Only `x11` observes the active window; unknown
observation never suppresses a bubble. Failure tooltips follow the result: a
stopped multiplexer selection, a window that was not found or refused, or no
backend able to act, plus each unavailable backend's requirement (KWin: "Wayland
focus requires KDE Plasma 6.").

To add a host: write its `Capture` (detection, codec, label) in
`src/hosts/adapters/<host>.*` and register it in `Registry::builtin()` at its
precedence; give it an `Activation` if it can select a tab or pane and register
that in `platform::createFocusService()` (`src/platform/linux/native.cpp`, and
`src/platform/macos/native.cpp` where it applies). Its
identifiers must fit v1 (`host_target` up to 256 characters); richer targets need a
protocol change, and older pets reject unknown host IDs. To add a desktop backend:
implement `platform::DesktopBackend` under `src/platform/desktop/<name>/`, add it to
`pet_native` and register it in `createFocusService()` in order of preference;
return `Unsupported` when the session lacks it. Sessions, alert policy and Qt
presentation stay unchanged in both cases. `tests/focus_tests.cpp` registers a
test-only adapter and fake backends this way.

## Consequences

- `pet_native` is the only target linking X11 and D-Bus.
- Selection and activation report separate outcomes, so tooltips can say what
  failed.
- Hosts and backends are added by registration; sessions, alerts and UI are
  untouched. The macOS port builds on these seams ([0020](0020-macos-port.md)).

## Validation

### Platform refactor evidence — 2026-10-05

Phases 1–3 of the [plan](../platform-refactor-plan.md), one commit each. Phase 1
added characterization tests against the unchanged code: capture precedence and
limits, Konsole/tmux/herdr codecs, labels, v1 host validation, session host
refresh and inheritance, and the monitor's focus tooltips, dismissal and
suppression policy. Phase 2 moved those into `src/hosts` with the same assertions.
Phase 3 replaced `src/desktop/host_focus.*` and `drag_monitor.*` with the focus
service, adapter activations, `x11`/`kwin` backends and Linux process and command
services; X11 and D-Bus are now linked only by `pet_native`. The new `focus`
suite covers backend fallback, `Requested` versus `Confirmed`, unsupported
backends and their requirements, malformed targets, selection-only success, each
adapter's failure policy, tmux client hints, herdr clients before the pane's own
hints, unknown active state, a test-only adapter, and the Linux command runner.
Intended differences: tmux lists its clients once per click instead of twice,
and herdr's client search now needs a valid target.

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the 6.5 requirement
was lowered only in the local build): all eight CTest suites passed. `hook`, run
without a display, sent identical host fields to the previous build's for Konsole,
tmux, herdr, VS Code, generic terminal and oversized-target environments. On Xvfb
with Openbox: `platform::createFocusService()` raised xterm windows by process hint
and by `$WINDOWID` (`x11`, `Requested`, `_NET_ACTIVE_WINDOW` changed) and reported
them active or inactive; tmux observation was `Unknown`; a missing window was
`TargetNotFound` with KWin's requirement; a Konsole target without a D-Bus session
still raised its window; a real tmux 3.4 session switched to the pane's window and
raised the xterm of its attached client, found only through `list-clients`; a
failing tmux selection stopped before any window. In the running pet, a hook run
inside an xterm produced no bubble while that xterm was active, and a later bubble's
Open raised it, with another xterm under the bubble, in each of four runs alternating
this and the previous build (both raised the right window each time). The first,
exploratory click had left focus on the xterm under the bubble; it did not recur.
`desktop-tests`' X11 drag check passed. After merging main's ancestry check for
inherited terminal variables into the registry, `hook` again matched main's build in
nine environments, run beneath processes named `konsole`, `tmux`, `herdr` and `code`
or none. Not checked here: KWin on Plasma 6 (X11 and
Wayland), a live Konsole D-Bus tab switch, herdr, and CI on Qt 6.5.3.
