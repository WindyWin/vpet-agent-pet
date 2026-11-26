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
| herdr | `herdr agent list` (verify, below), `herdr tab focus`, `herdr agent focus` | Stops | Each live UI client of the same API socket, after selecting the Konsole tab it runs in; then its own hints | Never: `Unknown` |
| VS Code, terminal | None | — | Its hints | Yes |

**Hooks that run outside the terminal.** Codex runs hooks from a shared
`codex app-server` daemon. Its parent may be `systemd --user` (the hook sees no
`herdr` ancestor and reports a bare `terminal`) or the pane that started it (the
hook sees a `herdr` ancestor and the daemon's inherited `HERDR_TAB_ID`/`HERDR_PANE_ID`
and reports *that* pane, which may belong to another project: issue #151). Neither
the environment nor an ancestor named `herdr` proves which pane runs the session,
and the hook must stay fast, so it does not look further and v1 fields are
unchanged. Instead, every Open asks the locators first, with the provider, the
project and the provider's own session ID (`FocusSubject`), and the answer is a
`Resolution`:

| Resolution | Meaning | Then |
| --- | --- | --- |
| `Captured` | No locator applies, or one could not check a capture it trusts | The captured host is selected and raised as before |
| `Resolved` | A locator verified the captured pane or found the current one | That host is selected and raised |
| `Unavailable` | The host could not be asked, and the capture cannot be trusted alone | Nothing is selected or raised |
| `Stale` | The captured pane is gone or shows another session, and no other pane fits | Nothing is selected or raised |
| `Ambiguous` | Several panes fit, or several claim the same session | Nothing is selected or raised |

The herdr locator applies to `terminal` and `herdr` hosts of a session with a
provider. It runs `herdr agent list` against the captured target's own API socket
(named sessions and other servers are never confused with the pet's default), or
the pet's default socket for a bare terminal, and decides in this order:

1. **Native session.** An agent whose `agent_session` (a string, or an object
   with `id`/`session_id`) equals the session ID, for the same provider, is
   authoritative: one is `Resolved`, two are duplicate registrations and
   `Ambiguous`. Herdr 0.9.5 reports it for some agents only, so it is not required.
2. **Trusted capture.** Providers whose hooks run inside the pane (every one but
   Codex) captured a truthful pane. It is `Resolved` with its tab
   refreshed from the list (the pane may have moved), unless herdr shows another
   `agent_session` there now (`Stale`, then step 3). An unlisted pane, or a failed
   lookup, keeps the capture (`Captured`): herdr lists only agents it detects.
3. **Candidates.** Otherwise the candidates are the agents of that provider whose
   `cwd` is the session's project and that name no other session. One is
   `Resolved`; several are `Ambiguous`. The focused pane no longer breaks a tie:
   an unfocused session is as likely to be the one clicked. For a shared-daemon
   provider (`codex`, the `sharedDaemonProviders` argument) the captured pane is
   just one of these candidates, and missing from the list or showing another
   project it is `Stale`; a failed lookup is `Unavailable`.
4. A bare terminal that herdr does not run, or cannot be asked about, stays
   `Captured`, so plain-terminal Codex sessions are raised from their hints as before.

A `Resolved` host keeps the captured process hints only when the hook ran in that
very pane (trusted capture); otherwise they lead to the terminal that started the
daemon and are dropped, so only the live UI clients of the pane's socket are raised.
Resolution is read-only, runs on every Open (so restored sessions and legacy
records with inherited targets are revalidated against the live server rather
than trusted because the daemon survived), and is never sent over the wire. Failure
tooltips name the resolution (several candidates, stale pane, multiplexer
unreachable) instead of a generic failure.

Not covered yet (follow-ups to #151): Codex does not tell the hook which
interactive client a turn belongs to, so several Codex sessions in one folder
without `agent_session` remain `Ambiguous` until herdr or Codex supplies that
relationship (explicit per-client registration is the upstream prerequisite).
Several UI clients attached to one herdr server are still tried in process order.
Open is synchronous on the UI thread, bounded by the 1.5 s command timeout, so
overlapping requests cannot interleave.

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

### Shared-daemon pane identity (issue #151) — 2026-11-08

Locators now run for captured `herdr` hosts as well as bare terminals and return a
`Resolution`; `FocusService` stops before any side effect unless it is `Captured`
or `Resolved`. `Monitor` passes the provider session ID. The `focus` suite's
`herdrLocatesAgentBehindSharedDaemon` was rewritten: the reported case (an
inherited Project B pane for a Project A Codex session) selects the one Project A
agent and drops the daemon's hints; with two Project A agents it is `Ambiguous`
and nothing is selected or raised, whichever is focused; a native `agent_session`
picks an unfocused pane and excludes agents naming other sessions; duplicate
`agent_session` claims are `Ambiguous`; a closed or reassigned inherited pane is
`Stale`; an unreachable herdr is `Unavailable` for Codex and `Captured` for Claude;
a trusted Claude pane is kept among same-project Claude panes with its tab
refreshed, queried on its own named socket, and is `Stale` once another session is
named there. All other suites passed in a cloud Ubuntu 24.04 container built
against conda-forge Qt 6.8.4 (`pack-updates` fails there on main too, since it runs
as root). Not checked here: a real Herdr 0.9.5 with a shared Codex daemon (herdr is
not installable in the container, and its socket API documentation was
unreachable, so the `agent_session` shape is read leniently), KDE Wayland.
