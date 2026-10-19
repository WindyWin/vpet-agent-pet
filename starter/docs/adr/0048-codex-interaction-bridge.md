# 0048. Codex interaction bridge

- Status: Accepted
- Date: 2026-10-19

## Context

[0047](0047-direct-agent-interactions.md) added the provider-neutral interaction service and an OpenCode bridge. The
[provider proof](../../../docs/design/agent-interactions-provider-proof.md) measured Codex 0.163.0: its shared
app-server daemon exposes threads, items and `item/tool/requestUserInput` questions over JSON-RPC, carried on
WebSocket frames over a unix control socket. Questions are delivered only to the connection that started the turn
(others get them by resuming the thread), `turn/start` on a running thread steers it, and `clientUserMessageId` is
recorded but not deduplicated.

## Decision

- A dependency-free Node bridge (`src/providers/codex-bridge.mjs`) speaks the interaction stream to Agent Pet and
  JSON-RPC to the daemon. It hand-rolls a small RFC 6455 client over `node:net`, because Node's WebSocket cannot
  dial a unix socket, and resolves the control socket's symlink to stay under the socket path limit.
- It tracks threads the daemon has loaded (plus later activity) and never resumes saved ones. It resumes a tracked
  thread only when it waits for user input, to have the pending question replayed, and never answers any other
  server request. Sub-agent threads appear only when they ask.
- Only one non-secret question, with options or free text, is shown; the rest stays in the terminal.
- Follow-ups are gated on a fresh `idle` read, so a thread that reads as running is refused; a terminal turn that
  starts between that read and `turn/start` can still be steered, since the API has no conditional start. They are
  never retried: a lost outcome is `unknown` and is resolved by finding the user message with its client ID.
- An answer is confirmed by the daemon's `serverRequest/resolved`, not by the write succeeding.
- `idempotent_commands` is not advertised; the bridge still returns the known result for a repeated command ID.
- Same opt-in as OpenCode: not started by the app, no UI.

## Consequences

- Only sessions on the shared daemon are reachable, and questions exist only where Codex offers the tool (Plan mode,
  or `features.default_mode_request_user_input`).
- While subscribed, the bridge receives every approval request for that thread and leaves it unanswered.
- A turn started in the terminal between the idle check and `turn/start` would be steered; the API offers no
  conditional start.
- Numbering messages needs full-history `thread/read`, which Codex marks deprecated for paginated threads; moving to
  `thread/turns/list` is a later change.
- POSIX only for now; the Windows control channel is unverified.

## Validation

- 2026-10-19: `codex_bridge_test.mjs` (40 tests) runs the bridge against a fake daemon with its own WebSocket
  codec: handshake, symlinked socket, ping and fragmented replies, snapshots and numbering, message and question
  mapping, answers and custom answers, rejected and unknown outcomes, follow-up gating (no steering), repeated command
  IDs, lost follow-ups resolved by client ID, reconnects of both links, secret and multi-question refusal.
  `interaction-codex-bridge` runs the real C++ service against the real bridge and the fake daemon. The tests found
  one real defect (an answer waited on a notification queued behind it), now fixed.
- 2026-10-19: against a private Codex 0.163.0 daemon with an empty `CODEX_HOME` and the question feature on: a second
  client discovered a terminal turn, resumed its thread, received the replayed question, answered it (the turn
  continued with the answer), and sent a follow-up to the idle thread, which started a turn; killing and restarting
  the daemon reconnected the bridge with one logged line. That run preceded the fix above; the fixed build was
  re-run only against fakes because the account's model quota was exhausted (threads ended in `systemError`).
  Against the user's own daemon only `initialize`, `thread/loaded/list` and `thread/read` without turns were used.
- 2026-10-19: the bridge honors the service's `release` frame and its `conversation_ended` error: the thread is no
  longer tracked (discovery and reconnects skip it) until a new turn, which returns it under a new epoch. The
  bridges had matched the error code `ended`; the service sends `conversation_ended`, fixed here and in the OpenCode
  bridge. `codex_bridge_test.mjs` covers release, the error, a release for another epoch, and a daemon reconnect.
