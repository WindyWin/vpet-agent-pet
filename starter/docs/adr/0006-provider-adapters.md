# 0006. Provider adapters and integration management

- Status: Accepted
- Date: 2026-10-04

## Context

Claude Code and Codex send different hook payloads, and enabling an integration
means editing the user's client configuration, which may already contain other
handlers.

## Decision

Provider adapters and headless integration management are implemented in
`src/providers`. The setup guide and explicit live-acceptance gaps are in
[integrations.md](../integrations.md). Version probes returned Claude Code 2.1.289
and Codex CLI 0.156.0; the official hook references were checked on this date.

Adapters normalize each provider's payload to protocol v1. Enabling or disabling an
integration merges only Agent Pet's own hook entries, recognized by their command
markers, and preserves foreign handlers.

## Consequences

- The event protocol stays provider-neutral; provider mappings are documented in
  [integrations](../integrations.md).
- Synthetic fixtures and subprocess tests do not certify real client behavior; live
  acceptance remains manual.

## Validation

### M4 implementation evidence — 2026-10-04

- Release CMake build passed. CTest passed all three suites (`providers`, `events`,
  `prototype`), including 30 synthetic provider contract cases, concurrent tools,
  child isolation, failed-tool cleanup, large raw payloads, ownership-aware setup
  round trips, malformed-file preservation and actual shell path quoting.
- Socket tests require execution outside the restricted sandbox; their first
  sandbox run failed to bind sockets, and the permitted run passed in about 5 s.
- Asset verification passed: 215 PNGs, 23 sequences, 27,345,411 bytes.
- `python3 scripts/package.py` produced `dist/agent-pet-m4.tar.gz` with 61 bundled
  libraries. The copied package launched at `/opt/Agent Pet` in filesystem
  isolation, decoded idle/thinking, recovered input and shut down normally.
  Packaged Claude/Codex silent callbacks, configuration preview and enable also
  passed inside that namespace. The expected fontconfig warning was nonfatal.

No user client hook configuration was installed during implementation. Synthetic
fixtures and subprocess tests do not certify real client/host behavior. Captured
live payloads, real concurrent/child sessions, VS Code integrated terminal tests,
and existing-client configuration reload behavior remain M4 acceptance work.
