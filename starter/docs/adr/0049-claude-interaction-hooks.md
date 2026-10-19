# 0049. Claude Code interaction hooks

- Status: Accepted
- Date: 2026-10-19

## Context

[0047](0047-direct-agent-interactions.md) puts questions, messages and follow-ups on a private stream, one bridge per
provider. Claude Code has no local API for a running interactive terminal session (no daemon protocol, no documented
socket; the socket behind its cross-session messaging is undocumented and its key is a credential, so it is not
used). The [provider proof](../../../docs/design/agent-interactions-provider-proof.md) measured what hooks can do:

- A `PreToolUse` hook matching `AskUserQuestion` receives the question and can answer it
  (`permissionDecision: "allow"` with `updatedInput` holding `answers`). While it runs the terminal shows no dialog.
- A command hook with `asyncRewake` runs in the background and, when it exits 2, wakes even an idle session with its
  stderr as hook feedback. It fires once per process, dies with the session and is never acknowledged.
- `Stop` hooks carry `last_assistant_message`; `UserPromptSubmit` carries the prompt. Both are content.
- `~/.claude/sessions/<pid>.json` records `startedAt` and `status` (idle, busy, waiting) per Claude process.

The stream says a bridge acts only on the conversations its own connection registered, and a newer connection that
registers one takes it over. Hooks are many, short-lived processes, and the waiting one exits when it wakes the session,
so hooks cannot each own the conversation.

## Decision

An opt-in registration, separate from the status hooks, plus one **session host** per Claude process.

- **Registration.** `agent-pet integration enable|disable|preview|inspect --provider claude --interactions
  [--question-wait SECONDS]`. It writes five entries marked `--registration agent-pet-interact-v1` into the same
  `settings.json`: `SessionStart` and `Stop` (`asyncRewake`, timeout one day), `UserPromptSubmit` and `SessionEnd`
  (`async`), and `PreToolUse` with matcher `AskUserQuestion` (synchronous, timeout wait + 10 s). The two registrations
  are independent: ownership is the complete command grammar, so each operation touches only its own entries and
  foreign handlers; plain `enable` or `disable` does not change interaction hooks and the reverse. The existing status
  hook, its marker, 150 ms deadline and content-free protocol are unchanged. Linux and macOS only in this first cut:
  Windows would need the exec-form handler (0024) and is refused with a message.
- **Hooks** are the new headless subcommand `agent-pet claude-interact --event E` (C++; no Node needed, fast start).
  They are silent on every failure path and exit 0, except a follow-up wake (exit 2, text on stderr). They find
  Claude Code as the nearest ancestor with a session file for the hook's `session_id`; without one they do nothing.
- **Session host** (`claude-interact host`, detached, started by the first hook when a pet is listening). It owns the one
  interaction connection, registers `{claude, session_id, "<pid>.<startedAt>"}` once (the epoch is the Claude process, so
  a host restart is a gap in history, not a new conversation, and `--resume` is a new conversation), and holds the
  state: messages, open question, the one armed waiter and command outcomes. Hooks reach it over a private local socket
  next to the interaction one (one lock per session). It exits when its Claude process is gone, on `SessionEnd`, or
  when the pet has been away for a minute; the next hook starts a new one.
- **Questions.** One single-select `AskUserQuestion` becomes a request (option IDs `1…n`, custom answer allowed,
  `expires_at` = now + wait). The hook waits for the host, which waits for the pet; an answer is printed as
  `{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"allow","updatedInput":{<tool_input>,"answers":{<question>:<label or text>}}}}`.
  On timeout, unavailability (no pet link), interruption or anything unexpected the hook prints nothing and the normal
  terminal dialog opens. Several questions or a multi-select one in one call are left to the terminal.
- **Follow-ups.** `SessionStart` and every `Stop` start a waiter hook that blocks on the host; a newer waiter releases the
  older (exit 0), so at most one lives. A follow-up goes to it and the hook exits 2 with the user's text, labelled as sent
  from the pet. Ready means: after `Stop`, not after `UserPromptSubmit`, a waiter is armed, no question is open.
  Claude Code's own `status` from the session file must not say busy or waiting, or the follow-up is rejected
  `not_ready`; the pet never steers a turn.
- **Acknowledgement.** The hook confirms to the host after it has written the answer or the wake text, and only then
  does the host report `accepted`. No confirmation (hook gone, 3 s) is `unknown`, never retried. That is the strongest
  claim available: Claude Code gives no receipt.
- **Messages.** `UserPromptSubmit` prompts (except text Claude Code injects itself, including our own wake) and
  `Stop` `last_assistant_message` become messages, shortened to 12 KiB. Sharing is opt-in twice: the hooks exist only
  after `--interactions`, and they send nothing unless the pet runs the interaction service (`agent_conversations` or
  `--interactions`; otherwise no socket exists and the hooks exit at once). `hide_message_previews` is a display
  rule in the pet and applies as for any bridge. Capabilities: `messages`, `single_choice`, `custom_answer`,
  `follow_up`, `idempotent_commands` (not `reconcile`).

Alternatives rejected: letting each hook register its own connection (they take the conversation from each other, and
the waiter dies at every wake); one long-lived waiter as the owner (it dies when it delivers, so a question in the woken
turn would have no owner); Claude Code channels or the cross-session socket (research preview with a startup flag; a
credential, respectively); polling the transcript for messages (async, and content again).

## Consequences

- While a question waits for the pet (default 20 s, 1 to 120) the terminal shows no dialog, so the terminal user cannot
  answer or even see the question. After the wait the dialog opens as usual. This is a real cost; choose a short wait.
- Only sessions that started with the hooks can be reached: Claude Code reads hooks at startup, so earlier sessions
  need a restart.
- A wake is hook feedback, not a user message: the model sees a system reminder that we label as the user's words. The
  UI treats it as the user's; the transcript does not.
- No acknowledgement from Claude Code: `accepted` means our hook wrote it. A re-arm needs a `Stop`; a killed waiter
  (hook timeout of a day, a crashed host) leaves the conversation not ready until the next `Stop`.
- A host restart (crash, or the pet was away over a minute) leaves a gap in the message history, shown as such.
- The session file is undocumented: if its shape changes the hooks do nothing (fail closed), they do not guess.
- Claude Code's own `UserPromptSubmit` fires for injected text too; we recognise our wake, `<task-notification>` and
  `<system-reminder>` and do not show them as messages.

## Validation

### Automated evidence — 2026-10-19

`claude-bridge` (Qt Test, `tests/claude_bridge_tests.cpp`): question parsing and the exact answer JSON; hook outputs
and the order print-then-confirm; silence on every failure path (garbage input, no session file, no pet, unknown
arguments, a dead relay); registration merge (foreign and status handlers preserved, markers, idempotent, disable
removes only ours, bounds, malformed input); the two registrations kept apart through `runIntegration`; and an end to
end run of the real `agent-pet claude-interact` binary and its detached host against the real interaction service:
ready after `SessionStart`, user and agent messages, a question answered by option and by free text, a question that
expires, one cancelled by an interrupted hook, a busy session refusing a follow-up, a follow-up waking with exit 2 and
the exact stderr, a replayed command ID, `SessionEnd`, and a host that ends the conversation when the Claude process is
gone. Linux, Qt 6.11.2.

### Live evidence, Claude Code 2.1.303 — 2026-10-19

Real `claude --model claude-haiku-5-5 --setting-sources local --settings <scratch>` in tmux in a scratch directory, the
hooks written by `integration enable --provider claude --interactions --question-wait 15`, and a small Node harness
standing in for the pet's service on a private endpoint (the user's pet and settings were not involved).
- `SessionStart` started the host, which registered the conversation (title = folder, epoch `<pid>.<startedAt>`) and
  reported `ready`; a typed prompt gave a user message and `ready: false`.
- A question was shown to the harness with its options; unanswered, it expired after 15 s and the terminal dialog opened
  and worked (the prompt line read "running PreToolUse hook · 11s" meanwhile).
- The harness answered a question by option ID: the session printed "User answered Claude's questions" with that option
  and the model continued; the command result was `accepted`. A late answer after expiry was `rejected request_closed`.
- A follow-up to the idle session woke it ("Stop hook feedback"), the model asked a question in the woken turn, and the
  same host served it. Nine further text-only follow-ups in a row all woke the session (the eight-continuation cap of
  blocking Stop hooks does not apply to `asyncRewake`).
- The wake text came back as a `UserPromptSubmit` prompt (`<task-notification>… Stop hook blocking error …`), so
  the bridge now ignores injected prompts; a final run showed one user message per follow-up.
- `/exit` produced `conversation_ended` and no host process remained. Test transcripts, settings and the tmux session were
  removed.

Not verified: macOS; Windows (refused); the 24 hour hook timeout actually elapsing; a host that outlives a pet restart
over a real pet (the harness did not restart); behavior with a terminal user typing during a wait; Claude Code versions
other than 2.1.303.

### Release from the pet — 2026-10-19

The session host honors `release`, a `conversation_ended` error and a refused registration: it hands an open question
back to the terminal and stays dormant, and a new prompt reopens the session under the epoch
`<pid>.<startedAt>.<time>`, while hooks still attach by the process identity.
`aReleasedConversationReturnsOnlyWithANewPromptUnderANewEpoch` runs it with the real hooks and service. Not run live.
