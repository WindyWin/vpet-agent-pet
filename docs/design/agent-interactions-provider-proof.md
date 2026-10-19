# Agent interactions: provider proof (OpenCode)

Status: stage 1 evidence for [agent-interactions-spec.md](agent-interactions-spec.md). Date: 2026-10-19.

Tested with **OpenCode v2.0.29** (shared background service, `opencode api` client), model `openrouter/deepseek/deepseek-v4.1-flash-1017`, a throwaway session that was deleted afterward. Integration mode: the OpenCode V2 HTTP API on the background service. This works for any session on that service, not only a managed session. Claude Code and Codex were probed afterwards; see their sections below.

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
4. ~~Evaluate Claude Code and Codex later.~~ Done: see the next two sections. Both still need a bridge and a decision on the opt-in model.

# Codex 0.163.0 (probed 2026-10-19)

The installed CLI is 0.160.0, but it talks to the shared app-server daemon, which runs 0.163.0. Integration mode: the app-server JSON-RPC protocol over the daemon's control socket (`~/.codex/app-server-control/app-server-control.sock`). The socket speaks WebSocket (an HTTP upgrade, then one JSON-RPC message per text frame), so `codex app-server proxy` is only a byte relay. The protocol schema comes from `codex app-server generate-json-schema`. Tests that start turns or questions ran against a private `codex app-server --listen unix://…` with an empty `CODEX_HOME` (auth copied in, no hooks), never against the user's daemon. Against the live daemon the probe only did `initialize`, `thread/loaded/list`, `thread/list` and `thread/read` without turns. A second client plays the pet and the first plays the TUI.

| # | Gate item | Result | Evidence |
| --- | --- | --- | --- |
| 1 | Stable conversation, message, question IDs | Pass | Thread UUID, turn ID, item IDs. A question carries `threadId`, `turnId`, `itemId` and a JSON-RPC request ID. A user message item carries `clientId` (the supplied `clientUserMessageId`). |
| 2 | Structured question with allowed answers | Pass, with a flag | `item/tool/requestUserInput`: `questions[{id, header, question, isOther, isSecret, options[{label, description}]}]`, `isBlocking`. The tool exists only in Plan mode unless `features.default_mode_request_user_input` is on (off in the user's daemon). |
| 3 | Answer in the same running conversation | Pass | A second client answered a question the first client's turn had raised. It replied to the replayed request with `{"answers":{"<question id>":{"answers":["Blue"]}}}`; both clients then got `serverRequest/resolved`, the turn continued and the model used the answer. |
| 4 | Follow-up after a completed turn | Pass | `turn/start` on an idle thread starts a new turn. |
| 5 | Authoritative accept/reject | Partial | `turn/start` returns the turn ID. `turn/steer` rejects a wrong `expectedTurnId` (-32600). A reply to a question has no result frame beyond `serverRequest/resolved`. |
| 6 | Disconnect detection and reconcile | Pass | `thread/resume` on a thread with a pending question replays the request to the new client. A question stays pending after its only client disconnects (`status: active, waitingOnUserInput`). `thread/read`/`thread/loaded/list` rebuild state. |

Behaviors the bridge must handle:

- **Questions go to the connection that started the turn.** A second client that has not resumed the thread receives only `thread/status/changed`. After `thread/resume` it receives the pending request, later events and `serverRequest/resolved`. `thread/resume` fails with "no rollout found" until the thread's first turn has been recorded.
- **`turn/start` while a turn runs steers it.** It returns the running turn's ID and appends a user message. The bridge must gate follow-ups on status `idle`, as for OpenCode. Status values: `idle`, `active` with `activeFlags` (`waitingOnUserInput`, `waitingOnApproval`), `notLoaded`, `systemError`.
- **`clientUserMessageId` is not deduplicated.** The same ID sent twice while running added a second user message; sent again after completion it started a second turn. Items do expose `clientId`, so a bridge can resolve an unknown outcome by reading the thread before it ever retries.
- **Two kinds of thread.** `thread/list` includes saved threads with status `notLoaded`. Resuming one loads it into the daemon. Whether a plain terminal `codex` session is on the shared daemon was not established, so a bridge must advertise only threads the daemon has loaded or that a TUI is known to be attached to, and must not resume saved threads on its own.
- **Question wording differs.** Answers are keyed by question `id`, Codex questions may be non-blocking (`isBlocking: false`), and `isSecret` must be refused.
- **No hooks involved.** The existing content-free status hooks are untouched; a Codex bridge talks to the daemon directly.

Capabilities for this provider: `messages` yes (item notifications, `thread/read`), `single_choice` yes, `custom_answer` yes when `isOther`, `follow_up` yes when idle, `reconcile` yes by pull, `idempotent_commands` no (the bridge must track commands itself).

# Claude Code 2.1.303 (probed 2026-10-19)

There is no local API for an interactive session: no daemon protocol, no documented socket. Each session does listen on a private socket (`$XDG_RUNTIME_DIR/cc-socks/<pid>.sock`) with a key file under `~/.claude/sessions/`. That is the internal channel behind its cross-session `SendMessage`. It is undocumented and its key is a credential, so it was not probed and must not be used. `claude agents --json` lists interactive sessions (pid, cwd, session ID, name, `idle`/`busy`) read-only. Everything below was run in a scratch directory with `--setting-sources local` and a custom `--settings` file, so none of the user's hooks fired.

| # | Gate item | Result | Evidence |
| --- | --- | --- | --- |
| 1 | Stable IDs | Partial | Hooks carry `session_id`, `prompt_id`, `tool_use_id` and `transcript_path`. There is no message ID. |
| 2 | Structured question with allowed answers | Pass (hook) | A `PreToolUse` hook matching `AskUserQuestion` receives `tool_input.questions[{question, header, options[{label, description}], multiSelect}]` plus `tool_use_id`. |
| 3 | Answer in the same running conversation | Pass (hook) | The hook returned `permissionDecision: "allow"` with `updatedInput: {questions, answers: {<question text>: <label>}}`. The interactive session printed "User answered Claude's questions → Blue" and the model continued. `allow` alone is not enough. |
| 4 | Follow-up after a completed turn | Pass (hook) | A `SessionStart` command hook with `asyncRewake: true` waited in the background, exited 2 with a message on stderr while the session was idle, and woke it: it showed "Stop hook feedback" and the model answered the injected instruction. |
| 5 | Authoritative accept/reject | Fail | Neither path returns a receipt. The hook only sees its process end; the wake text is delivered as a system reminder with no acknowledgement. |
| 6 | Disconnect detection and reconcile | Partial | Hook processes die with the session. State can be rebuilt only from hook events and the transcript file (written asynchronously). |

Behaviors and risks:

- **The question hook blocks the terminal.** While the `PreToolUse` hook runs ("running PreToolUse hook · 11s") the terminal shows no question dialog, so the user cannot answer locally until the hook returns. A usable design needs a short pet-side timeout that returns no decision, after which the terminal dialog appears. A hook that waits is also contrary to the current "hooks never wait" rule, so it would be a separate, opt-in registration.
- **Follow-up needs a hook that is already running.** `asyncRewake` fires once per process: the bridge must re-arm after each wake (for example from a `Stop` hook) and the hook must connect to the pet's interaction stream, not poll a file. A session that never ran the hook (started before the integration) cannot be reached.
- **`asyncRewake` is not a user message.** Text arrives as hook feedback in a system reminder, labelled "Stop hook feedback". Anything the pet forwards must be treated as user-authored input in the UI but is presented to the model as a system reminder.
- **Channels are an alternative.** An MCP server declaring `claude/channel` can push `<channel>` events into a session and receive replies through a tool, and can relay tool-permission prompts. It needs `--channels` or `--dangerously-load-development-channels` at session start (interactive only, with a confirmation dialog), is a research preview with organization controls, and gives no delivery acknowledgement. Not exercised here.
- **`claude -p` is no substitute.** In 2.1.303 `AskUserQuestion` is disabled in print mode, so a launched `-p`/SDK process (stream-json with `--permission-prompts host`) is a different, pet-owned conversation and out of scope.
- **Reading the conversation.** The `Stop` hook input has `last_assistant_message`; prompts arrive via `UserPromptSubmit`. Both include content, so they would need the same explicit opt-in as the OpenCode bridge.

Capabilities for this provider if built on hooks: `messages` partial (assistant text at Stop, user prompts at submit, no history), `single_choice` yes, `custom_answer` yes (free text goes in `answers`), `follow_up` yes only while a re-armed wake hook is alive, `reconcile` no, `idempotent_commands` no.
