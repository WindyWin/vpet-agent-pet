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
- Windows named-pipe ownership is not yet verified on the client side.

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

