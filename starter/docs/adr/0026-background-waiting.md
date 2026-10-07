# 0026. Background-job waiting

- Status: Accepted
- Date: 2026-10-07

## Context

Claude can stop a turn while a shell, monitor or subagent continues running. A
normal finished reaction followed by an idle “Needs input” notification wrongly
suggests the user must act. The approved [design](../superpowers/specs/2026-10-07-background-waiting-design.md)
introduces a calm state until Claude resumes.

## Decision

- Add optional `waiting: true` to v1 `turn_finished`, rejecting false, non-booleans
  and other kinds. Claude parent Stop sets it only for a non-empty
  `background_tasks` array. Child Stop remains session_end. Only the array's
  presence matters; task content never leaves the adapter. Hook registrations and
  Codex mapping stay unchanged.
- Waiting clears tools, stops the reaction timer and preserves `turnStarted`.
  Input attention and unmatched tool callbacks cannot end it. Approval or
  reasonless attention, new work, interrupt, failure, end and ordinary finish use
  their normal rules. Waiting has no timer beyond the 30-minute session expiry.
- Aggregate priority is attention > exhausted > error > turn-finished > working >
  reading > thinking > waiting > idle > inactive. Session rows place waiting
  between thinking and finished, with a translated title and indigo colour.
- Waiting produces no finish alert, mood reward, bedtime note or recap turn. The
  final finish measures the whole duration and counts once. Checkpoint format and
  recap schema need no changes.
- Bubbles uses phased start A, loop B (or B_2), end C in every mood. Waiting is
  non-urgent and keeps idle-only ambient fidgets and wander quiet.

## Consequences

The bundled pack adds 68 original PNGs (about 25 MB), with 125 ms frames. A
never-ending job waits until user activity or session expiry. Codex background
jobs remain out of scope because its hooks expose no matching signal.

## Validation

### Automated evidence — 2026-10-07

Tests cover adapter privacy and fallback shapes, strict wire validation, state
transitions, input suppression and approval handling, checkpoint restore, expiry,
priority, whole-turn recap accounting, session rows and Bubbles playback in all
moods. Linux Release build with Qt 6.11.2 passes all twelve CTest suites
(the affected prototype shard was rerun after updating its catalog state count).
Asset verification passes for 1,197 PNGs and 141 sequences. `update_translations`
adds the Vietnamese “Đang chờ” entry; translation consistency is checked after
committing, as the checker compares generated strings against HEAD.

### Live Claude payload — pending before release

No real Stop payload has been captured for this change. The field name and array
shape are implemented from the approved design and tested with synthetic payloads.
Before release, capture a Stop while a `run_in_background` shell remains active,
confirm `background_tasks` is a non-empty array, and record the Claude Code version
and result here. Keep the raw payload in a private scratch area, outside the repo.
