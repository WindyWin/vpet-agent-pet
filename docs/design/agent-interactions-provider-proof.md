# Agent interactions: provider proof (OpenCode)

Status: stage 1 evidence for [agent-interactions-spec.md](agent-interactions-spec.md). Date: 2026-10-19.

Tested with **OpenCode v2.0.29** (shared background service, `opencode api` client), model `openrouter/deepseek/deepseek-v4.1-flash-1017`, a throwaway session that was deleted afterward. Integration mode: the OpenCode V2 HTTP API on the background service. This works for any session on that service, not only a managed session. Claude Code 2.1.303 and Codex 0.160.0 were not evaluated.

## Result against the capability gate

| # | Gate item | Result | Evidence |
| --- | --- | --- | --- |
| 1 | Stable conversation, message, question IDs | Pass | Session `ses_…`, message `msg_…`, form `frm_…`. All are server-assigned. Message IDs may also be client-supplied. |
| 2 | Structured question with actual allowed answers | Pass | The agent's `question` tool created a pending form (`GET /api/session/{id}/form`). The form had `metadata.kind = "question"`, field `q0` of type `string`, `options[{value,label,description}]` and `custom: true`. |
| 3 | Answer in the same running conversation | Pass | `POST /api/session/{id}/form/{formID}/reply` with `{"answer":{"q0":"Add focused tests"}}` returned 204. The blocked tool call completed with the answer, and the same assistant turn continued ("You chose …"). |
| 4 | Follow-up after a completed turn | Pass | `POST /api/session/{id}/prompt` was admitted and started a new turn in the same session. |
| 5 | Authoritative accept/reject | Partial | Replies are authoritative: 204 accepted, 400 `FormInvalidAnswerError`, 404 `FormNotFoundError`, 409 `FormAlreadySettledError`. A prompt returns an admission receipt (durable inbox item), which proves admission but not that the agent has started on it. |
| 6 | Disconnect detection and reconcile | Pass (pull and push) | Pending forms (`GET …/form`), messages (`GET …/message`) and form state (`GET …/form/{id}` → `pending`, `answered`, `cancelled`) can be re-read at any time, so a snapshot can be rebuilt. `GET /api/event` (SSE) is documented as volatile (a slow consumer fails the stream, and events during disconnection are missed). Event payloads were not observed because the CLI client did not surface the stream. Verify with a direct client. |

## Behaviors the bridge must handle

- **Prompts steer a running turn.** The follow-up response reported `delivery: "steer"`. The spec's v1 rule is no implicit interruption while busy, so the bridge must gate `send_follow_up` on the session being idle (`GET /api/session/active`, or the `idle` message type) and report `ready` itself.
- **Command idempotency by ID.** Re-posting a prompt with the same client `id` returns the original item and creates no duplicate. A different payload under the same `id` is silently ignored and returns the original. The service must still reject a changed payload itself (compare against the stored command), as the spec requires.
- **Unknown-outcome lookup exists.** `GET …/message/{messageID}` and the inbox list let the bridge resolve a timed-out follow-up without resending. For form replies, form state resolves the outcome (`answered` with the stored answer).
- **Double answer is rejected server-side.** A second reply returned 409. Request revision is not exposed, so the service's own revision counter must be keyed on form ID.
- **Answer shape.** Answer keys are field keys (`q0`) and values are `string`, `number`, `boolean` or `string[]`. A wrong type is rejected with 400. `custom: true` allows values outside `options`. A multi-question tool call produces one form with several fields, so the first release should accept only single-field forms and show the rest as unsupported content.
- **Form kinds.** Forms are a general mechanism (`multiselect`, `external`, `when` conditions). Only `metadata.kind = "question"` with a single `string` field and options is in scope for v1. Permission approvals use a separate `permission` API and stay out of scope.
- **Auth.** Direct HTTP to the service returns `UnauthorizedError`. `opencode api` and the client library handle discovery and auth. The bridge needs the supported client and pairing flow (`/api/pair`), not a hand-rolled unauthenticated socket.
- **Message content.** Assistant text is in `content[]` items (`type: "text"`). Tool items carry the question input. Map only `text` items into the message record.

## Capability advertisement for this provider

`messages`: yes. `single_choice`: yes. `custom_answer`: yes (when `custom: true`). `follow_up`: yes, when idle. `reconcile`: yes, by pull. `idempotent_commands`: partial. Prompts deduplicate by ID. Form replies are protected by the settled-state conflict.

## Event stream (observed 2026-10-19)

A direct client (Basic auth, user `opencode`, the password from the service registration file) read `GET /api/event` as server-sent events. Relevant types, each `{id, type, location?, data}` with `data.sessionID`: `form.created` (`data.form`), `form.replied` (`data.id`, `data.answer`), `form.cancelled`, `session.execution.started`, `session.execution.succeeded` / `failed` / `interrupted`, `session.text.ended` (full text, `assistantMessageID`, `ordinal`), `session.inbox.enqueued` (user prompts) and `session.deleted`. Events carry `durable.seq` numbers, but they are per-session and not contiguous over messages, so the bridge numbers messages itself. The stream is live-only; a drop loses events, so the bridge re-reads state when it reconnects. Message pages: `order` and `limit` for the first page, then `cursor` alone (combining them is an error); `cursor.next` is always present, so stop on a short page; user text is a top-level `text`, assistant text is in `content[]` parts, and `idle` entries are skipped.

## Open items before UI work

1. ~~Observe `GET /api/event`~~ Done: see above. The bridge uses it for low-latency updates, with pull reconciliation as the fallback.
2. Confirm behavior across a service restart, and across a client disconnect while a form is pending.
3. ~~Decide the bridge host.~~ Done: a dependency-free Node script (`starter/src/providers/opencode-bridge.mjs`) using `fetch` with the registered password, not `@opencode/client`.
4. Evaluate Claude Code and Codex later. Neither was probed in this pass.
