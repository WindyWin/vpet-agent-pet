# Local events (M3, protocol version 1)

The desktop monitors normalized events for the current user. M3 supplies the
transport and state engine. Provider-specific payload parsing, configuration
installation and live client validation belong to M4. Do not register this hook
command directly against raw Claude/Codex payloads yet.

## Envelope

One UTF-8 JSON object per Unix datagram, at most 8192 bytes:

```json
{"version":1,"provider":"claude","session_id":"example","event_id":"delivery-1","kind":"tool_start","timestamp_ms":1791072000000,"tool_id":"call-1","activity":"reading","project_path":"/projects/example","parent_id":"parent"}
```

Required fields: `version` (integer 1), `provider` (`claude` or `codex`),
`session_id`, `event_id`, `kind`, and positive integer Unix `timestamp_ms`.
Optional fields: `tool_id`, `parent_id`, `project_path`, `activity`.
`tool_start` and `tool_end` require `tool_id`; activity is `reading` or `working`
(default working). Strings are limited to 256 UTF-16 units, except project paths
(2048); control characters and unknown fields are rejected. No prompt, tool
arguments, output, or raw provider payload is retained or transmitted.

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
not infer provider coverage; real payload mappings are an M4 deliverable.

## Commands and transport

Start `agent-pet`, then send a development event:

```bash
printf '%s\n' '{"version":1,"provider":"claude","session_id":"demo","kind":"prompt","project_path":"/projects/demo"}' | ./build/agent-pet emit
printf '%s\n' '{"version":1,"session_id":"demo","kind":"attention"}' | ./build/agent-pet hook --provider claude
```

Both commands read stdin through EOF; they fill missing event IDs/timestamps.
`hook` always returns zero, with no stdout or stderr, including malformed input,
a closed monitor or a full queue. `emit` returns nonzero with stderr on rejection
or failed delivery. Successful delivery means the kernel accepted the datagram;
it is not an acknowledgement that the state engine accepted the timestamp.
No GUI application or display connection is created by either command.

The stdin deadline is 150 ms. Sending is a single nonblocking syscall, with no
retry. The test budget is under 1 second per callback including process startup;
OS scheduling and process loading prevent a hard real-time wall-clock guarantee.
An unresponsive monitor drops events once its bounded kernel queue fills.

The endpoint is `$XDG_RUNTIME_DIR/agent-pet-<uid>/events.sock`, falling back to
`/tmp/agent-pet-<uid>/events.sock` when XDG_RUNTIME_DIR is unset. Sender and desktop
must share that environment. The directory must be owned by the current UID
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
  finishing the last tool resumes thinking. Prompt begins a new turn and clears
  old tools. Attention persists through tool-end callbacks and alert dismissal;
  prompt, tool-start, interruption, completion or end resolves it. An error while
  waiting does not clear attention.
- Duplicate event IDs are ignored within the bounded deduplication window. Older
  timestamps are ignored for the entire session, including late tool callbacks;
  equal timestamps use receiver order, except tool callbacks cannot overwrite a
  just-finished/interrupted turn. Adapters should use source timestamps when
  available. No distributed ordering can be reconstructed from missing metadata.
  A dropped late completion can leave a tool recorded until prompt/stop/expiry.
- Session end removes the record and alerts and retains a bounded timestamp
  tombstone. A strictly newer event may observe that identity again. Events more
  than 60 seconds in the future or 30 minutes in the past are rejected.
- Errors react for up to 4 seconds then resume the previous activity; finished
  turns react for up to 4 seconds then become idle. The animation's own one-shot
  may finish sooner. Interrupt clears tools and becomes inactive, never finished.
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
ending a session or expiry clears its relevant alerts. Queue presentation,
Next/Dismiss controls and the persistent attention badge belong to M5.

## Verification

`ctest --test-dir build --output-on-failure` runs deterministic replay from
`tests/fixtures/concurrent.json`, ordering/duplicate/expiry/restart tests,
attention/error/dismissal tests, bounds and schema checks, and subprocess/socket
checks. Tests cover no display, absent monitor, stalled stdin, oversized input,
full receive queue, competing receivers, endpoint permissions and restart.
They require permission to bind a Unix socket. Existing animation and settings
tests run alongside these checks.
