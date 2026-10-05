# Agent Pet first release

Build a standalone desktop pet with assets loaded from this repository and later from its installed application resources. The original VPet app and command are outside the runtime path.

Scope revised 2026-10-04: a passive local session monitor with an animated pet, compact attention notifications, and minimal settings. Users keep working in their existing terminals/editors; Agent Pet does not launch, focus, resume, embed, or control those sessions.

M1 implementation (2026-10-04): the C++17 / Qt 6 prototype now plays bundled idle
and thinking animations and provides drag, size, on-top, timed click-through,
recovery and quit controls. CMake/CTest and a relocatable Linux prototype packager
are available. Copied-package XWayland and filesystem-isolated runtime smoke
checks pass. Native X11 and manual desktop acceptance remain open; see
[architecture and evidence](docs/architecture.md) and [build commands](README.md).
Provider payload adapters and integration management are implemented in M4; live acceptance is pending.

M2 implemented 2026-10-04: all 23 bundled sequences and 215 frames now play through
the catalog-defined phases. The original `Raise/` animation plays during native
XWayland drags and returns to the prior state. The decoded cache is bounded at
8 MiB; a developer preview, persistent size/position/on-top settings, monitor
recovery, settings, quit and artwork terms are available. Automated frame,
transition, resource-error and settings tests pass, as do the real XWayland drag
and isolated package checks. The user visually confirmed the result. See
[architecture and evidence](docs/architecture.md). Native X11 and native Wayland
are still untested as full desktop sessions.

M3 implemented 2026-10-04: versioned normalized events, private bounded Unix
transport, headless hook/emit commands, bounded session/tool tracking, deduplication,
ordering, expiry and aggregate playback. Deterministic replay and transport tests
cover the policies in [events.md](docs/events.md). Provider adapters are implemented in M4, and visible alert controls in M5.

M4 implementation added 2026-10-04: separate provider mappings, headless integration
preview/enable/inspect/disable, ownership-aware JSON merging, atomic writes and
contract/transport tests. Target versions: Claude Code 2.1.289 and Codex CLI
0.156.0. Live sessions, captured fixtures and terminal/editor coverage are still
required before marking M4 complete. See [integration evidence](docs/integrations.md).

M5 implemented 2026-10-04: a compact alert bubble beside the pet names the
project, provider, short session ID and reason (needs approval/input, tool error,
turn finished), with a pending count and Next/Dismiss. A persistent badge marks
unresolved requests independently of dismissal. Mute, optional sound and
integration status/enable/disable live in settings and the context menu.
Replay, queue and offscreen window tests pass; see [M5 evidence](docs/architecture.md).

Idle animation, phase 1 of the animation epic ([issue 12](https://github.com/WindyWin/vpet-agent-pet/issues/12)),
implemented 2026-10-04: weighted idle variants, one-shot fidgets chosen by idle time,
a rare pool, dozing off after ten quiet minutes, and an Off / Subtle / Lively setting
(default Subtle). Automated tests pass; pacing still needs a look on a real desktop.
Mood, touch reactions, easter eggs and walking are later phases of that issue.
See [the notes](docs/architecture.md#idle-animation).

Mood, phase 2 of that issue, implemented 2026-10-05: finished turns and tool errors
move a mood score that picks happy or droopy idle and fidget art and fades back to
neutral; finished turns are celebrated three ways; a snack reaction after twenty
turns without a break and a milestone every hundredth turn (count persisted); and an
Off / Cheerful only / Full setting (default Full). Eat, Drink and Gift need a layered
renderer and item art the archive lacks, so single-layer stand-ins play instead.
Automated tests pass; tuning still needs real sessions. See [the notes](docs/architecture.md#mood).

Touch reactions, phase 3 of that issue, implemented 2026-10-05: holding the pet still
for half a second pets its head, body or cheek (a click still opens the session list);
letting go of a fast drag throws it, so it falls to the bottom of its screen and gets
up; letting go past an outer screen edge while idle makes it hide behind that edge
until activity or a drag brings it out; and a Touch setting (default on). Automated
tests pass; the feel of the hold, throw and edge thresholds still needs a real
desktop. See [the notes](docs/architecture.md#touch-reactions).

1. Build a transparent, draggable desktop window and play the bundled `idle` sequence using frame durations from `assets/vpet/animations.json`. Test X11 and XWayland behavior before choosing a Linux package format.
2. Add an animation controller that can start a sequence, hold its middle loop, play its ending, and return to idle. Keep decoded images in a bounded cache. The JSON state map has separate folders for each A/B/C phase.
3. Add a small local event receiver and a packaged `agent-pet hook` command. The command reads hook JSON from stdin, extracts normalized event/session metadata and project path when available, sends them through a private local channel, and exits quickly even when the UI is closed. Exclude prompt text, tool arguments, and output. Create records on any supported event, including when SessionStart was missed.
4. Add Claude Code and Codex hook adapters for SessionStart, UserPromptSubmit, PreToolUse, PostToolUse, PermissionRequest, Stop, and SessionEnd. Handle each provider's supported failure and interruption events. Track concurrent tool calls and sessions; `Stop` means a turn ended, not that the task succeeded.
5. Add compact pet alerts for supported approval/input requests, errors, and finished turns, identified by project, provider, and short session ID. Use a bounded queue with duplicate aggregation, a pending count, and next/dismiss controls. Add mute and optional sound. Dismissal does not resolve underlying attention; retain an attention badge when needed. Alert controls are Next and Dismiss; users respond in their existing terminal/editor.
6. Provide minimal settings/tray controls for size, position, notifications, integrations/coverage, attribution, and quit. Closing settings keeps monitoring active; explicit quit stops it. These controls, the pet, and its alert bubble are the complete product interface.
7. Bundle executable, hook command, sprites, this notice, and required dependencies. Test the package on a machine without VPet. Integrations must add their own hook entries while preserving existing user settings.

## Coverage and acceptance

Track sessions observed by enabled integrations in the current user's local environment, regardless of supported terminal/editor host. Validate actual provider versions and host combinations before claiming support. Existing sessions appear only when they emit a supported event to the integration; integration setup may require restarting the client. Do not promise silent-session discovery, retrospective activity, remote/container coverage, or support for every agent application.

Keep bounded session state and pending alerts in memory. Persist only preferences and integration configuration. On restart, wait for fresh events without restoring activity or replaying old alerts. Silence does not prove success, completion, or disconnection. Unsupported lifecycle events remain visible coverage limitations.

Validate concurrent sessions in the same project, missed start events, late/duplicate callbacks, monitor restart, attention priority, settings closure, and alert dismissal. The first release is complete when supported sessions can be identified and monitored without locating or controlling their original windows.

The source pack includes normal mood art for every state, happy and poor art only for the idle loop and the `aside` and `yawn` fidgets, and a selection of variants. Expand it from the larger local asset archive only when a new state needs more frames. Preserve attribution and notices for all derived asset packs.
