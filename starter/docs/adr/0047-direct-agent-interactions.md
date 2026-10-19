# 0047. Direct agent interactions

- Status: Accepted
- Date: 2026-10-19

## Context

The pet reports what agents do but cannot answer an agent's question or send a follow-up. The status protocol is
content-free by design, so message text must not travel through it. The full design is in the
[specification](../../../docs/design/agent-interactions-spec.md).

## Decision

- A provider-neutral, Qt Core only `interactions` library owns conversations, requests, drafts, read state and
  command arbitration. It is testable without a display.
- A separate private stream (Qt local socket, owner-only directory, lock file, 64 KiB length-prefixed JSON frames)
  carries it. The status datagrams are unchanged.
- Each provider has a bridge that speaks the stream. OpenCode's is a dependency-free Node script.
- The desktop shows one collapsed preview, an expanded panel and a list. All state stays in the service.
- It is opt-in (`agent_conversations` preference or `--interactions`); remote control is a later, separate connector.

## Consequences

- Message text is shared between an agent and the pet only after the user opts in.
- Delivery is acknowledged by the provider, never assumed; an unconfirmed send is never retried automatically.
- **Windows pipe ownership is not verified.** Qt creates the pipe without `FILE_FLAG_FIRST_PIPE_INSTANCE`, so another
  account on the same machine could create a pipe of the predictable name first, and a bridge that connects to it
  would send that account the conversation text. On POSIX the private 0700 directory prevents this. The event
  transport avoids it by checking the pipe's owner, but the clients here are Node bridges, which cannot read a pipe's
  owner, and the C++ `InteractionClient` is used only by tests. A real fix is a change to the protocol (the service
  proving it holds a per-user secret before a bridge sends any text) and needs a Windows machine to build and test,
  so it is not part of this change. Until then, on a Windows machine shared with other accounts, leave
  `agent_conversations` off and do not run a bridge. Recorded from the review of PR 126.

## Validation

- 2026-10-19: core, transport, bridge end-to-end and desktop widget tests pass; live run against OpenCode 2.0.29.
- 2026-10-19: after first use, the expanded panel became a frameless bubble drawn like the toast (no title bar),
  radio choices got an explicit ring/dot so a selected option differs from the rest, and the preview gained an
  Open button that raises the agent's terminal or editor without opening the panel.
- 2026-10-19: agent text renders as Markdown through Qt's parser (HTML off, images and links inert); the list drops
  ended, disconnected and long-quiet conversations; the bridge ends sessions OpenCode no longer has. Closing an
  OpenCode terminal is not observable (its service keeps the session), hence the quiet-time rule.
- 2026-10-19: following herdr (its OpenCode plugin runs in each terminal and the server-side plugin steps aside for
  shared servers), a terminal plugin reports the session on screen over a socket that lives as long as the
  terminal, so a closed terminal ends its conversation; Open failing offers to detach the conversation (`release`
  frame, confirmed by the user). Not verified against a real OpenCode terminal in this change: restart OpenCode
  after `--install-tui` and check that closing a terminal ends its conversation.

- 2026-10-19: review of PR 126. Already fixed before this pass and verified in the code: the `Host` destructor detaches
  its connections, JSON integers are range-checked before the `qint64` cast, the runtime directory fails closed, the
  queued-bytes drop is deferred, prompt IDs are a hash of the whole command ID, the event stream buffer is capped
  and accepts CRLF, a walk stops when a conversation holds the pet. Fixed here: a refused `conversation_open` gets
  its error and no `resync` (`aRefusedOpenGetsAnErrorAndNoResync`); a sub-agent that is already shown keeps
  receiving its events, so a question answered in the OpenCode terminal closes in the pet; the panel answers a
  command for the conversation it was sent to even after the user looked at another, and keeps that conversation's
  composer busy meanwhile; the e2e test gives Node a full pipe path on Windows. Not fixed: Windows pipe ownership
  (above).
