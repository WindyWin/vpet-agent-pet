# Interaction protocol and service

Stage 2 of [the direct-interaction specification](../../docs/design/agent-interactions-spec.md): the
provider-neutral core, its private stream and a fake bridge. No provider bridge or UI is wired yet, and
nothing here is started by the application. The status protocol ([events](events.md)) is unchanged and
stays content-free.

| Part | Code |
| --- | --- |
| Records, JSON validation, limits | `src/interactions/model.*` |
| Length-prefixed framing | `src/interactions/frame.*` |
| Service: store, state machines, arbitration, reconcile | `src/interactions/service.*` |
| Service ↔ transport glue, timeout clock | `src/interactions/host.*` |
| Stream contract | `src/platform/contracts/interaction_transport.h` |
| Stream implementation and bridge-side client (Qt local sockets) | `src/platform/local_stream/` |
| Tests | `tests/interaction_tests.cpp` (core, fake bridge), `tests/interaction_transport_tests.cpp` (real socket) |

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
| `message` | `conversation`, `message {message_id, sequence, role, text, created_at}` |
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

## Bridge notes

Replies and follow-ups reach the bridge as `command` frames and must be answered with a `command_result`; the
fake bridge in `tests/fake_bridge.h` shows each frame. The OpenCode mapping found in the
[provider proof](../../docs/design/agent-interactions-provider-proof.md) (`session.form.reply`,
`session.prompt` with the command ID as message ID, form and message listing for snapshots) fits this shape.
