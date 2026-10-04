# Provider integrations (M4)

The C++ hook adapters and integration manager ship in the same executable as the
pet. They require no shell scripts, Python, Node, display server, transcript
reader, or running client process discovery. Start the pet separately.

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
Enable/Disable buttons after a confirmation naming the file and hook command.
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
apostrophes escaped; shell expansion characters remain literal. Each generated
handler has a one-second client timeout and runs synchronously to reduce event
reordering. The callback itself reads for at most 150 ms, sends once, and exits
zero silently, even on invalid input or an unavailable monitor.

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
Claude adds `PostToolUseFailure` and typed notifications; Codex adds `Interrupt`.
Codex hosted tools can lack tool hooks. There is no universal input-request event.

## Adapter policy

| Input | Normalized behavior |
| --- | --- |
| SessionStart | Refresh/create idle session metadata |
| UserPromptSubmit | Thinking; starts a new observed turn |
| PreToolUse | Track tool ID; known reading tools select reading, others working |
| PostToolUse | Remove that tool and resume remaining activity |
| PermissionRequest | Attention (reason approval); never return an approval decision |
| Stop | Turn finished, without claiming success |
| SessionEnd | Remove session and its alerts |
| SubagentStart / SubagentStop | Create/remove a child using agent_id and parent session_id |
| Claude PostToolUseFailure | Error with failed tool removed; is_interrupt maps to inactive |
| Claude Notification | Attention for permission_prompt (approval), idle_prompt and elicitation_dialog (input) only |
| Codex PostToolUse with structured isError=true or nonzero numeric exit_code | Error with failed tool removed |
| Codex Interrupt | Inactive, never finished |
| Unknown event or notification | Silent no-op |

A callback carrying `agent_id` uses that child identity; a child Stop cannot
finish its parent's turn. Concurrent tools retain their own IDs. Unknown tool
names are treated as working, without examining arguments. Tool failure text,
prompt text, tool arguments/results, assistant messages, and transcript paths
never enter the normalized envelope. Raw input is parsed only in memory, bounded
at 1 MiB; oversized callbacks are dropped. The normalized datagram stays at 8 KiB.

Missing/invalid session IDs and uncorrelatable tool events are dropped. Tool
callback IDs are hashes of hook name and tool-call ID, scoped by provider/session
in the state engine. Other events get fresh delivery UUIDs: their repeated
invocations cannot be reliably deduplicated. Timestamps are receipt times because
the supported contract has no common event timestamp. Delayed callbacks cannot
be reliably reordered across processes. No content is hashed for identity.

Failure detection is deliberately limited to the typed fields above. Codex
text-only shell failures remain a gap, as do general Claude interruptions without
a failure callback, unsupported input requests, and tool paths that omit hooks.
Attention persists through tool completion when no explicit resolution is known;
it clears on a new tool start, prompt, interruption, stop, or session end. This
can retain attention after approval until a subsequent observable event.

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
