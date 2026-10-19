# Provider integrations (M4)

The C++ hook adapters and integration manager ship in the same executable as the
pet. Claude/Codex and Antigravity hooks require no shell scripts, Python, Node,
display server or transcript reader. OpenCode runs the bundled JavaScript bridge
in its own plugin runtime. Start the pet separately, or enable its autostart preference.

## Setup

Use the executable at its permanent installed location. From the starter build:

```bash
./build/agent-pet integration preview --provider claude
./build/agent-pet integration enable --provider claude
./build/agent-pet integration inspect --provider claude
./build/agent-pet integration disable --provider claude
```

Replace `claude` with `codex` for Codex. Preview prints the proposed configuration
without creating a file. Inspect lists this application's registered handlers;
registration does not prove they are executing. Enable adds/replaces this app's
handlers, and disable removes them. Both operations are idempotent. Use
`--config /absolute/path/to/file.json` for a custom configuration or a disposable
test, and `--executable /absolute/path/to/agent-pet` to register another installation.
Enable requires an existing executable. After moving an installation, enable
again from its new location, then review the updated client hook definitions.

The pet's Settings window shows the same inspect result for each provider
(Enabled, Not enabled or Partial, plus the configuration path) and offers
Enable/Disable buttons after a confirmation naming the file and hook command. A
partial setup, such as one made before Claude's `StopFailure` hook was added, offers
Update, which runs enable and so replaces Agent Pet's entries with the full set.
They run the same operation as the command line, registering the running
executable, so launch the pet from its permanent location before enabling.

Defaults are `$CLAUDE_CONFIG_DIR/settings.json` (otherwise
`~/.claude/settings.json`) and `$CODEX_HOME/hooks.json` (otherwise
`~/.codex/hooks.json`). No TOML, permissions, feature flags, or trust records are
modified. Enable and disable preserve unrelated JSON values and handlers,
including handlers sharing a group with ours. JSON formatting may change on an
actual edit. Malformed JSON or hook structures, oversized files, symlink targets,
and competing Agent Pet writers are rejected. Saves use atomic replacement and
an optimistic external-edit check; other applications do not honor our lock,
so avoid editing the same file concurrently. New files are private to the owner.

Ownership is the complete generated command syntax, ending in
`--registration agent-pet-v1`, with the selected provider. Do not manually add
that marker to someone else's hook. Paths are single-quoted with embedded
apostrophes escaped; shell expansion characters remain literal.

On Windows the hooks run `agent-pet-cli.exe`, the console companion of
`agent-pet.exe`. Claude Code handlers use the exec form, run without a shell
(Claude Code 2.1.139 or newer): `"command"` is the program and `"args"` is
`["hook", "--provider", "claude", "--registration", "agent-pet-v1"]`, and ownership
is that exact `args` array. Codex runs a command through `cmd.exe /d /c`, where a
quoted program never starts, so its command names the program unquoted, by its 8.3
short path when the folder has spaces or `cmd.exe` metacharacters; enabling fails
with an explanation when no short name exists. Ownership is then the exact command
grammar with an unquoted program. See [0024](adr/0024-windows-port.md). Each generated
handler has a one-second client timeout and runs synchronously to reduce event
reordering. The callback itself reads for at most 150 ms, sends once, and exits
zero silently, even on invalid input or an unavailable monitor.

### Autostart

When the send fails because no pet is listening, and only for a `session_start`
event, the hook reads `preferences.json`. If `autostart` is true and `DISPLAY` or
`WAYLAND_DISPLAY` is set, it launches the pet and still exits zero silently.
The launch is fully detached (double fork and `setsid`, working directory `/`,
stdin/stdout/stderr on `/dev/null`, no other inherited descriptors), so the
client never waits on the pet. It runs the same executable as the hook (the
installed path from the hook command), never a `PATH` lookup:

```text
agent-pet --autostarted --launch-event '<normalized event JSON>'
```

The pet validates the launch event like any datagram and applies it once it is
listening, so the session that triggered the launch is not lost. With
`--autostarted`, a pet that finds another one holding the single-instance lock
forwards its launch event to that pet and exits silently, so concurrent session
starts launch one pet and lose no events. Without a display (SSH, containers)
nothing is launched. `agent-pet autostart enable|disable|status [--when-idle
keep|hide|quit]` changes only `preferences.json`, never client hook files.
systemd socket activation was considered and not used: it starts a pet with no
desktop session at hand. Starting at login (`agent-pet autostart login
enable|disable|status`) is a separate opt-in that writes
`~/.config/autostart/agent-pet.desktop`; it runs the pet whether or not an agent
is in use, so it is off by default.

## Contract and version baseline

Official references inspected 2026-10-04:
[Claude Code hooks](https://code.claude.com/docs/en/hooks) and
[Codex hooks](https://learn.chatgpt.com/docs/hooks).
Installed version probes returned **Claude Code 2.1.289** and
**Codex CLI 0.156.0**. These are implementation targets, not a claim of completed
live compatibility certification. No minimum-version range is inferred.

Claude user hooks live in settings JSON. Its reference describes automatic
settings reload and a `/hooks` inspector; settings and managed policy can disable
execution. Codex accepts `hooks.json` beside active configuration layers. Its
`/hooks` interface requires review/trust of new or changed definitions; feature
flags and managed policy can suppress hooks. Agent Pet leaves those controls to
the client. For setup verification, restart the client, inspect `/hooks`, and
submit a fresh test turn. Existing-session reload behavior remains unverified.

Both references define session, prompt, tool, permission, stop, and child events.
Claude adds `PostToolUseFailure`, `StopFailure` and typed notifications; Codex adds `Interrupt`.
Codex hosted tools can lack tool hooks. There is no universal input-request event.

## Adapter policy

| Input | Normalized behavior |
| --- | --- |
| SessionStart | Refresh/create idle session metadata |
| UserPromptSubmit | Thinking; starts a new observed turn |
| PreToolUse | Track tool ID; known reading tools select reading, others working |
| PostToolUse | Remove that tool; the last tool's activity is held 4 s before thinking resumes |
| PermissionRequest | Attention (reason approval); never return an approval decision |
| Stop | Turn finished, without claiming success; Claude adds `waiting: true` when `background_tasks` is a non-empty array |
| SessionEnd | Remove session and its alerts |
| SubagentStart / SubagentStop | Create/remove a child using agent_id and parent session_id |
| Claude PostToolUseFailure | Error with failed tool removed; is_interrupt maps to interrupt (idle) |
| Claude StopFailure | Turn failed: error_type rate_limit maps to reason limit, billing_error to billing (out of quota, sticky); any other type has no reason (error reaction, then idle). The error type itself is not sent |
| Claude Notification | Attention for permission_prompt (approval), idle_prompt and elicitation_dialog (input) only |
| Codex PostToolUse with structured isError=true or nonzero numeric exit_code | Error with failed tool removed |
| Codex Interrupt | Idle, never finished; stale tool callbacks ignored until the next prompt |
| Unknown event or notification | Silent no-op |

A callback carrying `agent_id` uses that child identity; a child Stop or StopFailure
cannot finish or fail its parent's turn. Concurrent tools retain their own IDs. Unknown tool
names are treated as working. The one argument examined is a tool start's
`tool_input.command` (a string, or a list of strings): the hook checks it in
memory against a fixed list of destructive patterns (`rm` with recursive and force
flags, a forced `git push`, `git reset --hard`, `git clean -f`, `mkfs`, `dd` onto
`/dev/`, SQL `DROP`/`TRUNCATE`) and sends only `risky: true` for the pet's startled
reaction. Tool failure text, prompt text, tool arguments/results, assistant
messages, and transcript paths never enter the normalized envelope. Raw input is parsed only in memory, bounded
at 1 MiB; oversized callbacks are dropped. The normalized datagram stays at 8 KiB.

Missing/invalid session IDs and uncorrelatable tool events are dropped. Tool
callback IDs are hashes of hook name and tool-call ID, scoped by provider/session
in the state engine. Other events get fresh delivery UUIDs: their repeated
invocations cannot be reliably deduplicated. Timestamps are receipt times because
the supported contract has no common event timestamp. Delayed callbacks cannot
be reliably reordered across processes. No content is hashed for identity.

Failure detection is deliberately limited to the typed fields above. Codex
has no signal for a usage limit or a failed turn, so its quota stops are not shown. Codex
text-only shell failures remain a gap, as do general Claude interruptions without
a failure callback, unsupported input requests, and tool paths that omit hooks.
Attention clears when the tool that was waiting completes (approval; a Codex request with no tool started yet is answered by the next tool to complete), or on a new
tool start, prompt, interruption, stop, or session end. A rejected request emits
no hook of its own, so attention then lasts until the next of those events.

## Coverage and acceptance record

Implemented and automated:

- 30 synthetic, sanitized contract fixtures in `tests/fixtures/providers.json`.
  They are authored examples, **not captured live payloads**.
- Concurrent tools, failed-tool cleanup, duplicate delivery, child isolation,
  missed start, monitor state restart, and unknown/malformed input.
- Headless subprocess callbacks through the real private socket, including a
  prompt payload larger than the former 8 KiB limit.
- Preview/enable/inspect/disable, repeated setup/removal, malformed configuration,
  unrelated settings and shared hook groups, and executed shell path quoting.
- Existing M3 transport tests cover absent/full receivers, stalled input,
  restart, malformed input, and callback timing.

Still required for M4 live acceptance:

| Check | Status |
| --- | --- |
| Actual sessions in Claude Code 2.1.289 / Codex CLI 0.156.0 | Pending |
| Captured sanitized lifecycle, concurrent tool, failure, permission and child events | Pending |
| Terminal and VS Code integrated terminal, pet without focus | Pending |
| Already-running client setup/reload and missed SessionStart | Pending |
| Client continues while pet is closed; relaunch receives fresh events only | Pending |
| Client restart requirements established experimentally | Pending |

For each client, enable from the installed package, review its hooks, launch a
fresh session in a disposable project, and exercise reading, execution, a failed
tool, an approval, a completed turn, and a child. Repeat with another session
running concurrently and with the pet unfocused. Interrupt an active turn where
supported. Repeat in VS Code's terminal. Then close the pet, continue the client,
restart the pet, and verify that only new callbacks restore activity. Finally,
change integration setup with a client already open, record whether reload needs
a restart, disable twice, and verify unrelated hooks still run. Record exact
versions, host and observed event names; sanitize captures before committing them.

Coverage is limited to enabled, supported local clients sharing the current
user's runtime environment. No silent-session discovery, retrospective replay,
remote/container transport, cross-user monitoring, or blanket editor-extension
support is promised. Pending acceptance items keep the overall M4 milestone open.

### Background jobs

Claude parent `Stop` callbacks with a non-empty `background_tasks` array send only
`waiting: true`; task IDs, descriptions and commands never enter the envelope.
Missing, empty or non-array values preserve the ordinary finish. A child `Stop`
still ends only the child session. No hook registration changes are required.
While waiting, input notifications are ignored; approvals still ask for attention.
Codex has no background-task signal or idle notification hook and never sets this
flag. Live Claude payload validation remains a pre-release check in
[ADR 0026](adr/0026-background-waiting.md#validation).

## OpenCode and Antigravity (experimental)

Both appear in **Settings → Startup and agents**, with the same Enable/Disable
controls. From the installed executable:

```bash
agent-pet integration preview --provider opencode
agent-pet integration enable --provider opencode
agent-pet integration inspect --provider opencode
agent-pet integration disable --provider opencode
```

Replace `opencode` with `antigravity` for its hooks. Preview is read-only; enable
and disable preserve foreign configuration. Restart the client after enabling.
On Windows use `agent-pet-cli.exe` for these commands. No real client configuration
is changed by the automated tests.

| Provider | Default setup target | `--config PATH` means |
| --- | --- | --- |
| OpenCode | `~/.config/opencode/plugins/agent-pet.js` | The complete plugin file path |
| Antigravity | `~/.gemini/config/hooks.json` | The hook JSON file path |

OpenCode's target honors `OPENCODE_CONFIG_DIR`, then `XDG_CONFIG_HOME`. Agent Pet
also accepts `ANTIGRAVITY_CONFIG_DIR` as a setup-path override; this is an Agent Pet
option, not a claim that the Antigravity client reads that environment variable.
For workspace-only setup, pass the client's workspace plugin/hooks path explicitly.

The OpenCode plugin runs without extra dependencies and forwards only allowlisted
metadata. It uses a bounded serial queue and an argument-array subprocess launch;
missing executables or a closed pet do not reject client callbacks. Setup refuses
to overwrite or delete an edited/foreign file at its target path. A single global
or workspace installation is recommended: OpenCode can load both copies, causing
duplicate notifications. The same file serves OpenCode 1 through its hook object
and OpenCode 2 through the `setup(ctx)` definition; the plugin recognizes the
previous V1-only file on enable and migrates it in place. OpenCode 2 reconciles
this plugin per active location and every instance receives the server-wide event
stream, so identical public events are forwarded once by event id.

| OpenCode callback | Pet behavior |
| --- | --- |
| Session created (OpenCode 2; created/updated on OpenCode 1) | Refresh identity, parent metadata; can autostart |
| Prompt (OpenCode 2) / chat message (OpenCode 1) | Thinking; new observed turn |
| Tool before/after | Reading/working; correlate by call ID |
| Tool completion with failure (OpenCode 2 hook error; OpenCode 1 tool part error) | Error, removing the failed tool; no error text forwarded |
| Permission asked (legacy SDK: updated) / question asked or OpenCode 2 form created | Attention for approval/input |
| Execution failed/interrupted (OpenCode 2) or session error | Failed turn; OpenCode 1 typed MessageAbortedError becomes interruption |
| Execution succeeded (OpenCode 2) or session idle | Finished, or child session end; suppressed after error until the next prompt |
| Session deleted | Remove session |

Antigravity's named hooks use `--event EVENT` because its payload has no hook name.
The first workspace path supplies the project label. Tool identity is the step
index scoped to the conversation. Invocation zero starts observed thinking;
continuation invocations are ignored to preserve concurrent tool tracking. Tool
errors use only the presence of the typed error field. Stop with `fullyIdle: false`
waits for background work; an explicit termination error fails the turn.

Antigravity callbacks return neutral JSON even when delivery fails: PreToolUse
returns `{"decision":"allow"}` (delegates to the client's policy, never
`auto_approve`), Stop returns `{"decision":"stop"}`, and other callbacks return
`{}`. Claude, Codex and OpenCode callbacks remain silent. Antigravity's general
permission prompts, session closure and child relationships are not inferred.

Contract sources reviewed 2026-10-18: [OpenCode 2 plugins](https://opencode.ai/v2/docs/build/plugins/),
its [V1 plugin migration](https://opencode.ai/v2/docs/build/plugins/migrate-v1/), and
[Antigravity hooks](https://antigravity.google/docs/hooks). OpenCode CLI 2.0.29 is
installed on the development host; the event payloads and hook shapes were verified
live there for the OpenCode 2 bridge (session created, execution ended, permission,
tool hooks and prompt hook). Antigravity is not installed. Automated synthetic and
subprocess tests do not establish live compatibility for Antigravity or a minimum
supported client version. Live turns, permissions, concurrent tools, interruption,
reload, autostart and IDE focus still need acceptance on the actual clients and
operating systems. See [ADR 0041](adr/0041-additional-agent-providers.md).

## Claude Code interaction hooks (opt-in)

Separate from the content-free hooks above: `integration enable --provider claude --interactions` registers five
extra entries marked `--registration agent-pet-interact-v1` (`SessionStart`, `UserPromptSubmit`, `PreToolUse` for
`AskUserQuestion`, `Stop`, `SessionEnd`) in the same `settings.json`. They share prompts, replies and questions with the
local pet's interaction service and can answer questions and wake an idle session. Each registration only ever adds,
replaces or removes its own entries (plain `enable` and `disable` leave these alone, and the reverse); foreign handlers
are preserved. `--question-wait SECONDS` (default 20) sets how long a question waits for the pet. Linux and macOS only.
See [interactions.md](interactions.md#claude-code-bridge-hooks) and [ADR 0049](adr/0049-claude-interaction-hooks.md).

