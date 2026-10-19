# Interaction protocol and service

Stages 2, 3 and 4 (bridge half) of [the direct-interaction specification](../../docs/design/agent-interactions-spec.md): the
provider-neutral core, its private stream, a fake bridge, the OpenCode bridge and the desktop preview, panel and list.
None of it runs unless the user opts in (below). The status protocol ([events](events.md)) is unchanged and
stays content-free.

| Part | Code |
| --- | --- |
| Records, JSON validation, limits | `src/interactions/model.*` |
| Length-prefixed framing | `src/interactions/frame.*` |
| Service: store, state machines, arbitration, reconcile | `src/interactions/service.*` |
| Service ↔ transport glue, timeout clock | `src/interactions/host.*` |
| Stream contract | `src/platform/contracts/interaction_transport.h` |
| Stream implementation and bridge-side client (Qt local sockets) | `src/platform/local_stream/` |
| OpenCode bridge (Node, no dependencies) | `src/providers/opencode-bridge.mjs` |
| Codex bridge (Node, no dependencies) | `src/providers/codex-bridge.mjs` |
| Claude Code bridge (opt-in hooks and a per-session host) | `src/providers/claude_hook.cpp`, `claude_host.cpp`, `claude_registration.cpp` |
| Tests | `tests/interaction_tests.cpp` (core, fake bridge), `tests/interaction_transport_tests.cpp` (real socket), `tests/opencode_bridge_test.mjs` (bridge against fakes), `tests/interaction_bridge_tests.cpp` (real service + real bridge) |

`pet_interactions` needs Qt Core only and builds in the portable-core configuration. `pet_interaction_stream`
adds Qt Network.

## Stream

Frames are a 4-byte big-endian length and that many bytes of UTF-8 JSON, at most 64 KiB; zero or larger lengths
close the connection before anything is buffered. The endpoint is `interactions.sock` in the private
`agent-pet-<uid>` runtime directory (mode 0700, socket 0600), or a per-user named pipe on Windows. One process
owns an endpoint through a lock file, which a stale socket does not hold. At most 16 connections; a peer whose
unsent output passes 1 MiB is dropped. This is the same-user trust boundary as the status socket, not a sandbox
against other processes of the user.

## Frames

Every frame has `"version": 1` and a `"type"`. A conversation is
`{"provider", "session_id", "conversation_epoch"}`.

Bridge → service:

| Type | Fields |
| --- | --- |
| `hello` | `version` or `versions`, `provider`, `generation`, `conversations`: `[{conversation, title, ready, capabilities}]`. Must be first; sent once. |
| `conversation_open` | One conversation entry's fields at the top level, to register more later. |
| `message` | `conversation`, `message {message_id, sequence, role, text, created_at, history}` (`history` marks a message that predates the bridge; it starts read) |
| `request` | `conversation`, `request {request_id, revision, question, options[{id,label}], custom_answer, state, expires_at}` |
| `request_state` | `conversation`, `request_id`, `state` (answered, cancelled, expired, replaced), `replaced_by` |
| `ready` | `conversation`, `ready` (a new turn may start) |
| `command_result` | `command_id`, `outcome` (accepted, rejected, unknown), `reason`, `receipt`, `still_answerable` |
| `snapshot` | `conversation`, `watermark`, `first_sequence`, `ready`, `capabilities`, `messages`, `requests`, `command_outcomes[{command_id, outcome, reason}]` |
| `conversation_ended` | `conversation` |

Service → bridge: `welcome` (`resync`: conversations to send a `snapshot` for, `refused`), `resync`, `command`
(the spec's answer and follow-up shape), and `error` (`code`, `message`, optional `request_id`).

A bridge can act only on conversations its own connection registered. A newer connection that registers a
conversation takes it over; the old one then gets `unknown_conversation`.

## Rules the service enforces

- **Capabilities** are advertised per conversation (`messages`, `single_choice`, `custom_answer`, `follow_up`,
  `reconcile`, `idempotent_commands`). An unadvertised action is rejected as `unsupported_action`.
- **Requests** move `pending → submitting → answered`; `cancelled`, `expired` and `replaced` end one, and an
  ended request never reopens. A higher revision replaces the question; a lower or equal one is ignored. A
  provider `expires_at` is checked at submit time and by `expire()`.
- **Answers** need a known request, the current revision, `pending` state, and exactly one option ID from the
  list or (when allowed) custom text. The first valid command reserves the request; a competing one is rejected
  as `already_submitting` or `already_answered`.
- **Follow-ups** need `ready`, no open question, and none in flight or unconfirmed. The service never queues
  or interrupts.
- **Command IDs** are idempotent: the same ID with the same payload returns the known state, a different payload
  is `command_id_reused`. Validation rejections are not recorded.
- **Delivery uncertainty.** No result within `commandTimeoutMs` (15 s), a lost connection, or a bridge-reported
  `unknown` leaves the request `unknown` and is never retried. A late `command_result` still settles it, and a
  `rejected` one with `still_answerable` reopens it. While `unknown`, a second answer is `delivery_unconfirmed`.
  An unconfirmed follow-up blocks further ones until a snapshot arrives.
- **Snapshots** are authoritative and validated whole before anything applies. They merge messages (a gap
  is flagged when `first_sequence` is beyond what the service has), reconcile requests (a pending request the
  provider no longer lists is `expired`; an `unknown` one answered or still pending is resolved), and settle
  commands from `command_outcomes`. An `unknown` request the provider no longer lists becomes `expired` with
  its command left unknown.
- **Identity.** A new epoch for the same provider and session ends the old conversation. Ended conversations
  keep readable history, never accept commands, and cannot be re-registered.
- **Bounds** (`Service::budget`): 64 conversations, 200 messages each, 16 open requests, 100 tracked commands,
  16 KiB text, 20 options, 256-character labels, 16 MiB aggregate. Eviction takes read messages, then inactive
  conversations without an open request, in-flight command or draft, then unread messages. Open requests,
  in-flight commands and drafts are never evicted: when only they remain, a new record is refused with a
  `capacity` error and `capacityExceeded` is set. An oversized question is refused whole as
  `unsupported_content`; it is never truncated.

## Desktop

Off by default: no socket is created and no message text is shared until `agent_conversations` is `true` in the
preferences file or the pet is started with `--interactions`. There is no Settings control or bridge launcher yet.
`hide_message_previews` makes previews show identity and state only.

| Part | Code |
| --- | --- |
| Wording and colors shared by the three views | `src/desktop/conversation_text.*` |
| Collapsed preview and conversation list | `src/desktop/conversation_bubbles.*` |
| Expanded panel and composer | `src/desktop/conversation_panel.*` |
| Coordinator: what to preview, hiding, fading, placement | `src/desktop/conversations.*` (`ConversationHub`) |
| Tests (offscreen; `PET_SCREENSHOT_DIR=… conversation-tests screenshots` writes the states) | `tests/conversation_tests.cpp` |

The panel and the preview read and write only the service: drafts (`setDraft`), read state (`markRead`), answers
and follow-ups (`submit`). A question outranks a message in the single preview; a new message never opens the panel
or takes focus; hiding a preview neither answers nor reads anything; an ordinary preview fades after 8 s (it waits
while hovered) and stays unread in the list; the same message delivered again does nothing. The pet's menu has
Agent conversations… (shown only while the service runs); the panel's Open raises the agent's terminal through the
existing host focus, using the status session with the same provider and ID. While the panel is open the pet does
not wander. Ctrl+Enter (Cmd+Enter on macOS) sends, Enter adds a line, Esc collapses and keeps drafts.

Known gaps against the specification: the running-sessions rows have no Conversation action or unread marker (use
the menu or the preview's `+N`); the pet badge does not yet merge interaction questions with status attention;
a focused host does not suppress previews; there is no preview sound; the header has no parent-path hint because
bridges send only a project name; Vietnamese strings are not translated yet.

## OpenCode bridge

`node src/providers/opencode-bridge.mjs [--endpoint <path>] [--opencode-url <url>]` connects OpenCode 2's HTTP API
to the interaction stream. It is not started by the app yet and, unlike the content-free status plugin, shares
message text, so it will start only from an explicit opt-in. Node 20 or newer; no packages. It finds OpenCode
through its service registration file (`OPENCODE_SERVICE_FILE` or `~/.local/state/opencode/service.json`) and
authenticates with the registered password, which it never logs. The endpoint defaults to the one the app
serves (`XDG_RUNTIME_DIR/agent-pet-<uid>/interactions.sock`, a per-user pipe on Windows); macOS defaults to
`$TMPDIR`, which is unverified, so pass `--endpoint` or `AGENT_PET_INTERACTION_ENDPOINT` there.

| Pet concept | OpenCode |
| --- | --- |
| Conversation | A session: ID, and `time.created` as the epoch. The project folder name is the title. |
| Message | A user message, or each non-empty text part of an assistant message (`<message id>:<n>`). Sequence numbers count them from the start of the session; only the newest 50 are sent in a snapshot. Text is shortened with `…` to fit a frame. |
| Request | A form with `metadata.kind: "question"` and one string field. Option `value` is the option ID, the field's description the question, `custom` the custom-answer flag. Forms with several fields, multiselect or external fields stay with OpenCode. |
| Ready | The session is not in `GET /api/session/active`; `session.execution.started` / `succeeded` / `failed` / `interrupted` toggle it. |
| `answer_request` | `POST …/form/{id}/reply` → 204 accepted; 409 is accepted only if the stored answer is the one sent (a retry), else `already_answered`; 404 `unknown_request`; 400 `invalid_answer` (still answerable); a lost connection is `unknown`. |
| `send_follow_up` | `POST …/prompt` with `id = msg_apet_<command id>`, which OpenCode deduplicates. Checked against the active set first: a running session is refused as `not_ready`, never steered. |

Tracked sessions are those running, with an open question, touched in the last 30 minutes (up to 8), or that
show activity while the bridge runs; sub-agent sessions appear only when they ask something. The bridge keeps
its state across a link loss and a lost event stream: a new `hello` brings a fresh generation, and snapshots
carry anything missed, including the outcome of commands it ran while the service was away. After an event
stream gap it re-reads active sessions, messages and forms.

## Bridge notes

Replies and follow-ups reach a bridge as `command` frames and must be answered with a `command_result`; the
fake bridge in `tests/fake_bridge.h` shows each frame.

## Codex bridge

`node src/providers/codex-bridge.mjs [--endpoint <path>] [--codex-socket <path>]` connects the shared Codex
app-server daemon to the interaction stream. Like the OpenCode bridge it is not started by the app, shares
message text, and will start only from an explicit opt-in. Node 20 or newer; no packages; Linux and macOS (the
daemon's control channel on Windows is unverified, so its tests are not registered there). Tests:
`tests/codex_bridge_test.mjs` (against `tests/fake_codex.mjs`, a fake daemon) and `tests/interaction_codex_bridge_tests.cpp`
(real service, real bridge, fake daemon).

**Daemon requirement.** It talks to the daemon's control socket, `$CODEX_HOME/app-server-control/app-server-control.sock`
(`~/.codex/...` when unset), which speaks WebSocket (an HTTP upgrade, then one JSON-RPC message per text frame).
Node's built-in WebSocket cannot dial a unix socket, so the bridge carries a small RFC 6455 client over `node:net`
(masking, ping/pong, fragments, 16/64-bit lengths, a 64 MiB message cap). The socket is a symlink into `/tmp`, and
is resolved before connecting because a unix socket path is limited to about 108 bytes. Only sessions on this
daemon are visible: a terminal `codex` that does not use the shared daemon cannot be reached. The daemon need not
exist when the bridge starts; it retries with backoff and logs the change once. The handshake is `initialize`
with `capabilities.experimentalApi` (questions need it), then `initialized`.

| Pet concept | Codex |
| --- | --- |
| Conversation | A thread: ID, and `createdAt` as the epoch. The working folder's name is the title. |
| Message | A `userMessage` item (its text parts) or an `agentMessage` item whose phase is `final_answer` or unset; `commentary` narration, reasoning, commands and tool items are skipped. Sequence numbers count messages from the start of the thread; only the newest 50 are sent in a snapshot. Text is shortened with `…` to fit a frame. Streaming deltas are ignored, and the last agent message of a running turn waits until the thread is idle. |
| Request | `item/tool/requestUserInput` with exactly one question that is not `isSecret`. `options[].label` is the option ID and label; free text is a custom answer when there are no options or `isOther` is set. The item ID is the request ID. Several questions, secret input and duplicate labels stay in the terminal. |
| Ready | Thread status `idle` with no open question and a live daemon connection. `active` (with or without `waitingOnUserInput` / `waitingOnApproval`), `notLoaded` and `systemError` are not ready. |
| `answer_request` | The bridge sends the JSON-RPC response `{answers: {<question id>: {answers: [<label or text>]}}}` to the request's ID. Codex gives no reply, so acceptance is the daemon's `serverRequest/resolved` (or, after 5 s, the thread no longer waiting for input). A request still pending after that is `unknown` (`not_confirmed`, answerable) and settles as accepted if it resolves later. |
| `send_follow_up` | Gated on a fresh `thread/read` showing `idle`, then `turn/start` with `clientUserMessageId = apet_<command id hash>`. A running thread is refused as `not_ready`: `turn/start` on a running thread steers it, so it is never sent. |

**Which threads.** Those the daemon has loaded (`thread/loaded/list`): up to 16, taking running or questioning ones
first, then the most recently updated, plus any that report activity later (`thread/started`, status changes).
Saved threads (`notLoaded`) are never read or resumed, because resuming loads them into the daemon. Sub-agent threads
(a parent thread or sub-agent source) appear only when they ask a question. An archived or deleted thread ends its
conversation; an unloaded one only stops being ready.

**Questions.** Codex sends a question only to the connection that started the turn. When a tracked thread reports
`waitingOnUserInput`, the bridge calls `thread/resume` on it, which attaches this connection and replays the pending
request with a fresh RPC ID. That is the only use of `thread/resume`. Once subscribed the bridge also receives the
thread's other server requests (approvals and so on); it never answers them, not even to refuse, so the terminal can.
Questions can be non-blocking (`isBlocking: false`); they are treated the same.

**Delivery is never assumed.** `clientUserMessageId` is not deduplicated by the daemon, so the bridge never retries.
If the `turn/start` outcome is lost (timeout or a dropped connection) it answers `unknown` and then looks in the
thread for a user message with that client ID: found means accepted (with the turn as receipt), and not found, once
the connection it went out on is gone, means rejected as `not_delivered`. Repeating a command ID returns the known
result without sending anything. The `idempotent_commands` capability is not advertised: that memory is the bridge
process's and a restart loses it.

**Reconnecting.** A lost daemon connection makes every conversation not ready; pending questions are kept (so a
snapshot does not expire them) but cannot be answered (`provider_unavailable`, answerable later) until the daemon is
back and replays them. At every (re)connect the bridge rereads loaded threads, resumes those waiting for input,
settles questions that no longer wait and publishes messages written meanwhile. A lost Agent Pet link is handled as
for OpenCode: a new `hello` generation and snapshots, including the outcomes of commands run while it was away.

**Plan mode.** In Codex the question tool exists only in Plan mode unless the `default_mode_request_user_input`
feature is on (`-c features.default_mode_request_user_input=true`); it is off by default, so in a default session
there are no questions to show. Approval prompts are a different request type and are out of scope.

Known gaps: a thread in `systemError` is treated as not ready (whether Codex accepts a turn there is unverified); a
turn started in the terminal between the idle check and `turn/start` would be steered (the API has no conditional
start); full-history `thread/read` is used to number messages, which the daemon marks deprecated for paginated
threads; threads that never go through the shared daemon are invisible.

## Claude Code bridge (hooks)

Claude Code has no API for a running terminal session, so this bridge works through an opt-in set of hooks and a
per-session host process ([ADR 0049](adr/0049-claude-interaction-hooks.md)). It is separate from the content-free status
hooks, which are unchanged.

```bash
./build/agent-pet integration preview --provider claude --interactions
./build/agent-pet integration enable  --provider claude --interactions --question-wait 20
./build/agent-pet integration inspect --provider claude --interactions
./build/agent-pet integration disable --provider claude --interactions
```

Restart Claude Code sessions afterwards (hooks load at startup). The pet must run the interaction service
(`agent_conversations` or `--interactions`); otherwise the hooks exit at once and nothing is shared. Linux and macOS.

| Pet concept | Claude Code |
| --- | --- |
| Conversation | `session_id` and epoch `<pid>.<startedAt>` of the Claude process (from its `sessions/<pid>.json`); the folder name is the title. |
| Message | `UserPromptSubmit` prompt (user) and `Stop` `last_assistant_message` (agent), shortened to 12 KiB. No history before the hooks ran. |
| Request | One single-select `AskUserQuestion`: option IDs `1…n`, custom text allowed, `expires_at` = the wait. Several questions or multi-select stay in the terminal. |
| Ready | After `Stop`, not after `UserPromptSubmit`, a wake waiter is armed, no question open, and the session file does not say busy or waiting. |
| `answer_request` | The `PreToolUse` hook prints `permissionDecision: allow` with `updatedInput.answers`; `accepted` after the hook confirms it printed. |
| `send_follow_up` | The armed `asyncRewake` hook exits 2 with the text on stderr; `accepted` after it confirms it wrote it. Refused as `not_ready` while busy. |

Moving parts: hooks (`agent-pet claude-interact --event …`) are short-lived and talk to one **session host**
(`claude-interact host`, detached, one per Claude process, started by the first hook when a pet is listening). The
host holds the only interaction connection for the session, so hooks never take the conversation from each other;
it ends with the Claude process, on `SessionEnd`, or after the pet has been away for a minute.

Know before enabling:

- **The terminal dialog is suppressed while a question waits for the pet** (default 20 s, `--question-wait` 1 to 120).
  The terminal user cannot answer in that time; afterwards the normal dialog opens. Keep the wait short.
- A follow-up reaches only sessions that started with the hooks, and only while idle with a waiter armed (re-armed at
  each `Stop`). It arrives as "Stop hook feedback", labelled as the user's message, not as a typed prompt.
- Claude Code acknowledges nothing: `accepted` means the hook wrote it; no confirmation is `unknown` and never retried.
- Messages and questions go to the local pet only; `hide_message_previews` applies in the pet as for any bridge.
