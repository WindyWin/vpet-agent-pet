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
