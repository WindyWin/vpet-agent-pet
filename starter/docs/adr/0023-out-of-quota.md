# 0023. Out-of-quota animation

- Status: Accepted
- Date: 2026-10-07

## Context

When a Claude Code turn stopped on a usage limit or on exhausted credits, no hook the
pet listened to fired. The pet kept thinking or working until the session expired 30
minutes later, so a user away from the terminal could not tell that the agent had
stopped and was waiting for the limit to reset. See the
[design spec](../superpowers/specs/2026-10-07-out-of-quota-design.md).

Claude Code fires `StopFailure` when a turn ends on an API error, with an
`error_type` category (`rate_limit`, `billing_error`, `overloaded`, `server_error`,
`authentication_failed` and others). Its output is ignored. Codex CLI 0.156.0 has no
hook or field for a limit or a failed turn.

## Decision

- **Wire.** A new v1 kind `turn_failed` means the turn ended on a provider failure.
  Its optional `reason` is `limit` (from `rate_limit`) or `billing` (from
  `billing_error`). Other error types send no reason. The hook maps the category and
  never sends the provider's own value. `turn_failed` never carries `tool_id`.
  `reason` remains forbidden on every kind except `attention` and `turn_failed`.
- **Hook.** Claude's hook list adds `StopFailure` (12 events). A failure carrying
  `agent_id` becomes `session_end` for the child, like `Stop`, so a subagent never
  marks its parent.
- **Sessions.** A failed turn clears tools and attention. With a reason the session
  is `exhausted` and keeps that reason, with no timer. A prompt, tool start,
  finished turn, interrupt, end or expiry leaves it; tool ends, errors and attention
  do not. Unlike an interrupt, the failed turn's later tool callbacks are accepted,
  because Claude can resume a quota-stopped turn by itself without a new
  `UserPromptSubmit`, and its first tool or finish is the sign that work resumed.
  Without a reason the failure reacts as an error for 4 seconds and then becomes
  idle, not the activity before it.
- **Priority.** attention > exhausted > error > turn-finished > working > reading >
  thinking > idle > inactive, for the animation, the alert queue
  (`alertRank` in `sessions/state.h`) and the session list. A request waiting on the
  user is still shown first. Exhausted does not count toward the attention badge.
- **Alerts.** An `exhausted` alert ("Usage limit reached" or "Out of credits") has no
  expiry and is dismissed when its session leaves the state. It is shown from the
  "requests and errors" bubble level, in amber (`#e67700`). A failure without a reason
  raises an `error` alert with the internal reason `turn`, titled "Turn failed",
  which fades after 10 seconds like other errors.
- **Animation.** The display state `out_of_quota` loops the previously unused
  `Default/Ill` art: the pet sick in bed (`Default/Ill/1`, weight 2) and coughing
  (`Default/Ill/2`, weight 1). Like attention and error it interrupts the current
  animation and keeps surprises, the risky-command startle and wellness reminders
  away. Mood counts it as a failure, and the daily recap counts it under errors.
- **Upgrade.** Settings offers **Update** for a partial integration. Update runs
  `enable`, which replaces Agent Pet's own handlers with the full set and keeps
  foreign ones, so a setup from an earlier version gains `StopFailure`.

## Consequences

- The artwork pack grows by 33 frames (about 3 MB in `artwork.rcc`).
- Codex quota stops stay invisible until Codex exposes a signal.
- No reset time is known, so the pet shows no countdown. The state lasts until the
  session works again, or 30 minutes after its last event.
- Existing Claude setups show as Partial until the user clicks Update or runs
  `agent-pet integration enable --provider claude`.

## Validation

### Out-of-quota evidence — 2026-10-07

Linux, Qt 6.11.2, Release build: all twelve CTest tests pass (prototype in four
shards). `providers` covers the `StopFailure` mapping for each quota category,
another category, a missing category, a subagent failure and Codex, plus a session
reaching `exhausted` from hook input. `events` covers the sticky state across 20
minutes, tool ends, toolless errors and attention, its clearing by prompt, tool
start, finished turn and end, the reasonless failure going idle, the aggregate
priority and the checkpoint round trip. `alerts` covers parsing (reasons per kind, no
`tool_id`), queue order, titles, fading and the session-list status. `prototype`
plays every `out_of_quota` frame in each mood and maps `exhausted` to it.
`verify_assets.py` passes with 137 sequences. Still to be checked against a live
Claude Code session that actually hits its limit.
