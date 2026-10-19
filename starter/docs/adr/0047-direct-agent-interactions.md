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
