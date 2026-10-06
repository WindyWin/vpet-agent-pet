# 0005. Local event transport and session engine

- Status: Accepted
- Date: 2026-10-04

## Context

Agent clients run hooks on every callback. A hook must not slow the agent down,
must work when the pet or the display is absent, and must not transmit prompt or
tool content.

## Decision

Local monitoring now uses a Qt Core session engine and a private Linux Unix
datagram transport, with no additional runtime dependency. Hook/emit dispatch
happens before QApplication construction, so callbacks do not connect to a
display. The desktop translates aggregate session states into the existing
animation catalog. See [events.md](../events.md) for the protocol and bounded-state,
ordering, identity, expiry and pending-alert policies.

## Consequences

- Hooks finish with one nonblocking send and exit silently, whether or not a pet is
  listening.
- The session engine is Qt Core only and fully testable with synthetic events.
- Live provider coverage is left to the adapters ([0006](0006-provider-adapters.md)).

## Validation

### M3 evidence — 2026-10-04

Validation on the existing EndeavourOS development host with Qt 6.11.2:

- Release build passed; CTest event and prototype suites passed (about 5 seconds).
  Replay covers concurrent providers/tools/children; other checks cover missing
  starts, duplicate/late callbacks, completion, interrupt, expiry, restart,
  attention dismissal, errors, capacity and schema validation. Playback checks
  validate every aggregate state's catalog mapping and drag restoration.
- Actual Unix socket/subprocess tests passed, including absent/full receiver,
  stalled stdin, no display, competing monitors and private-directory checks.
  Binding required execution outside the tool sandbox; this is not an app error.
- Asset verifier passed: 215 frames, 23 sequences, 27,345,411 bytes.
- The M3 bundle was built under `/tmp/Agent Pet M3` to exercise spaces in paths,
  and copied to `dist/agent-pet-m3` with an updated archive. It includes 61 runtime
  libraries. Packaged callbacks without a monitor or display exited silently
  with code 0 in 10.0 and 10.4 ms (valid and malformed payload samples).
- Filesystem-isolated bundle smoke passed, including playback, input recovery
  and shutdown. The isolated fontconfig warning is nonfatal. Bubblewrap required
  execution outside the tool sandbox to create its namespaces.

M3 does not establish live Claude/Codex hook coverage: raw provider adapters,
client versions and integration configuration belong to M4. Visible alert
controls belong to M5. No new native X11/Wayland desktop acceptance is claimed.
