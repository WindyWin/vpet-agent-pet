# Local events (M3, protocol version 1)

The desktop monitors normalized events for the current user. M3 supplies the
transport and state engine. M4 adds [provider adapters and integration setup](integrations.md).
`hook` accepts raw provider payloads; `emit` accepts this normalized protocol.

## Envelope

One UTF-8 JSON object per Unix datagram, at most 8192 bytes:

```json
{"version":1,"provider":"claude","session_id":"example","event_id":"delivery-1","kind":"tool_start","timestamp_ms":1791072000000,"tool_id":"call-1","activity":"reading","project_path":"/projects/example","parent_id":"parent"}
```

Required fields: `version` (integer 1), `provider` (`claude` or `codex`),
`session_id`, `event_id`, `kind`, and positive integer Unix `timestamp_ms`.
Optional fields: `tool_id`, `parent_id`, `project_path`, `activity`, `reason`,
`risky`, and the host fields below.
`tool_start` and `tool_end` require `tool_id`; activity is `reading` or `working`
(default working). `reason` is allowed only on `attention` and is `approval` or
`input`; without it alerts say "Needs attention". `risky` is a boolean allowed only
on `tool_start`: the hook found a destructive shell command (see
[adapter policy](integrations.md#adapter-policy)), and the pet looks startled. It is
the only non-string field besides the version and timestamp. Strings are limited to 256 UTF-16 units, except project paths
(2048); control characters and unknown fields are rejected. No prompt, tool
arguments, output, or raw provider payload is retained or transmitted; `risky` is
a one-bit judgment made inside the hook.

Kinds: `session_start`, `prompt`, `tool_start`, `tool_end`, `attention`, `error`,
`turn_finished`, `interrupt`, `session_end`. A finished turn indicates only that
it stopped; it says nothing about success or test results.

Adapters must preserve stable provider session/tool/event IDs when supplied.
Never invent a common session ID for unidentified callbacks: drop events without
a session ID and tool events without a correlatable tool ID. Parent identity is
optional and is scoped to the same provider. If an event ID is unavailable, the
command generates a UUID per invocation. This cannot deduplicate independently
retried invocations; adapters should supply a stable delivery ID when possible.
Missing timestamps are stamped at callback receipt. These fallback policies do
not infer provider coverage; see the M4 mapping and acceptance record.

### Host fields

`hook` adds where the agent runs, so the pet can bring it forward. These are
identifiers from the hook's environment and `/proc`, never titles or content:

| Field | Value |
| --- | --- |
| `host` | `konsole`, `herdr`, `tmux`, `vscode` or `terminal` (innermost multiplexer wins) |
| `host_pids` | Up to 16 ancestor process IDs, nearest first, comma-separated |
| `host_window` | `$WINDOWID` when the terminal exports it (decimal X11 window); never for `vscode` |
| `host_target` | Konsole `service\|/Windows/N\|/Sessions/M`; herdr `tab\|pane\|socket`; tmux `socket\|%pane` |

Programs started from a terminal inherit its variables: VS Code launched from a
herdr pane gives its own terminals that pane's `HERDR_PANE_ID`. So herdr, tmux
and Konsole variables count only when that program is one of the hook's
ancestors (by `/proc/<pid>/comm`), and are taken as they are only when the
ancestry cannot be read.

Values are validated again before use and passed to D-Bus or to `tmux`/`herdr`
as separate arguments, never through a shell. Accepted `host` values are those of
the host registry (`src/hosts/registry.cpp`); older pets reject IDs they do not know.
The pet converts these fields into its internal host descriptor; see
[session focus](adr/0017-session-focus.md).

## Commands and transport

Start `agent-pet`, then send a development event:

```bash
printf '%s\n' '{"version":1,"provider":"claude","session_id":"demo","kind":"prompt","project_path":"/projects/demo"}' | ./build/agent-pet emit
printf '%s\n' '{"session_id":"demo","hook_event_name":"PermissionRequest"}' | ./build/agent-pet hook --provider claude
```

Both commands read stdin through EOF. `emit` fills missing IDs/timestamps;
`hook` creates a sanitized envelope using the provider adapter. Raw hook input
is capped at 1 MiB; normalized emit input and outgoing datagrams at 8 KiB.
`hook` always returns zero, with no stdout or stderr, including malformed input,
a closed monitor or a full queue. `emit` returns nonzero with stderr on rejection
or failed delivery. Successful delivery means the kernel accepted the datagram;
it is not an acknowledgement that the state engine accepted the timestamp.
No GUI application or display connection is created by either command.

The stdin deadline is 150 ms. Sending is a single nonblocking syscall, with no
retry. The test budget is under 1 second per callback including process startup;
OS scheduling and process loading prevent a hard real-time wall-clock guarantee.
An unresponsive monitor drops events once its bounded kernel queue fills.

The endpoint is `$XDG_RUNTIME_DIR/agent-pet-<uid>/events.sock`. When XDG_RUNTIME_DIR
is unset it falls back to `/tmp/agent-pet-<uid>/events.sock` on Linux and, on macOS,
to the per-user temporary directory (`getconf DARWIN_USER_TEMP_DIR`). Sender and
desktop must share that environment. macOS limits local datagrams to 2 KiB by
default, so both ends raise their socket buffers to carry 8 KiB events. The directory must be owned by the current UID
with mode 0700 and must not be a symlink. The socket is mode 0600. A nonblocking
file lock prevents competing monitors and permits stale socket recovery after a
crash. The persistent lock file is intentionally not unlinked. This channel trusts
processes running as the same user. Each GUI notification drains at most 64
messages; oversized datagrams and invalid envelopes are dropped.

## State and ordering

- Session keys are `(provider, session_id)`; children and identical IDs from
  different providers have independent state. Any non-end event can create a
  record after a missed start. Start on an existing record refreshes metadata.
- Tool IDs are tracked independently. Working tools outrank reading tools;
  finishing the last tool holds its activity for 4 s, then resumes thinking, so
  short back-to-back tool calls show as one working stretch instead of a flicker
  the animation never reaches. Prompt begins a new turn and clears old tools. Attention persists through alert dismissal and the completion of unrelated
  tools. The tool awaiting an answer (the attention event's tool, or for Codex
  permission requests, which carry none, the tools already started) completing
  (or, when none had started, the first tool to complete) means it was approved: attention clears to the remaining activity. A rejection
  emits no callback; prompt, tool-start, interruption, completion or end resolves it. An error while
  waiting does not clear attention.
- Duplicate event IDs are ignored within the bounded deduplication window. Older
  timestamps are ignored for the entire session, including late tool callbacks;
  equal timestamps use receiver order, except tool callbacks cannot overwrite a
  just-finished turn. Adapters should use source timestamps when
  available. No distributed ordering can be reconstructed from missing metadata.
  A dropped late completion can leave a tool recorded until prompt/stop/expiry.
- Session end removes the record and alerts and retains a bounded timestamp
  tombstone. A strictly newer event may observe that identity again. Events more
  than 60 seconds in the future or 30 minutes in the past are rejected.
- Errors with a tool ID remove the failed tool. Errors react for up to 4 seconds
  then resume the remaining activity; finished
  turns react for up to 4 seconds then become idle. The animation's own one-shot
  may finish sooner. Interrupt clears tools and becomes idle, never finished; later
  tool callbacks and errors from the interrupted turn are ignored until the next prompt.
- After 30 minutes without an accepted event, records and their alerts expire.
  This is memory reclamation, not evidence of success or disconnection. Fresh
  events can recreate a record. No records or alerts are persisted or replayed
  after application restart.

Limits: 256 sessions, 128 simultaneous tools per session, 4096 recent event IDs,
256 end/expiry tombstones, 64 pending alerts. New sessions/tools at capacity are
dropped until space is available; oldest event IDs/tombstones are evicted.

Aggregate animation priority is attention > error > turn-finished > working >
reading > thinking > idle > inactive. No observed sessions means idle. The
aggregate feeds playback, including the state restored after dragging. Preview
is most useful without an active event stream; monitoring takes precedence once
fresh events have been observed.

The pending queue aggregates alerts by session and reason, saturating counts at
1,000,000. At capacity the oldest alert is evicted. Selection sorts attention,
then error, then finished, with FIFO ordering within each priority. Dismissal
removes the alert only, and never clears session attention. Resolving attention,
ending a session or expiry clears its relevant alerts.

## Alert presentation

`AlertQueue` (`src/sessions/alerts.h`) is a cursor over the pending queue and
holds no alert data. It shows the highest-priority alert and switches only when
a newly raised or re-raised alert outranks the one shown. Dismiss hides the
shown alert and moves to the one after it. Labels are `project · provider ·
short ID`: the project is the path's last component (with its parent folder
appended when another pending alert has the same name for a different path, or
"Unknown project"); the short ID is the shortest prefix of at least four
characters not shared with another pending session of the same provider.
Aggregated repeats show `(×N)`. The badge counts sessions whose state is
attention, so dismissal never clears it. Mute hides the bubble only; sound is a
system beep when a shown alert is raised or re-raised while not muted.

Finished-turn alerts expire 6 seconds after they are raised and error alerts
after 10 seconds; attention alerts stay until resolved. The `bubbles`
preference (0 requests only, 1 requests and errors, default, 2 everything)
filters which alerts reach the toast. A newly raised alert whose session window
is the active X11 window is dismissed on arrival. Bringing a session forward
from the toast or the session list dismisses that session's alerts; attention
itself remains until the session resolves it.

`sessionRows()` builds the running-sessions list from session records:
subagents fold into their parent (whose status shows the busiest child), and
rows sort attention, error, working, reading, thinking, finished, idle, stopped,
then most recently seen first.

## Verification

`ctest --test-dir build --output-on-failure` runs deterministic replay from
`tests/fixtures/concurrent.json`, ordering/duplicate/expiry/restart tests,
attention/error/dismissal tests, bounds and schema checks, and subprocess/socket
checks. Tests cover no display, absent monitor, stalled stdin, oversized input,
full receive queue, competing receivers, endpoint permissions and restart.
They require permission to bind a Unix socket. Existing animation and settings
tests run alongside these checks.
