# Background-job waiting — design

Date: 2026-10-07. Status: approved in conversation; ADR 0026 will record the decision.

## Intent

When Claude ends a turn with a background job still running (a `run_in_background`
shell, a monitor, a background subagent, …), it is not waiting for the user: it wakes
on its own when the job finishes. Today the pet shows the turn as finished, and about
60 seconds later Claude Code's `idle_prompt` notification turns it into "Needs input"
with a sticky bubble, although nothing is asked of the user.

Success: a Claude turn that stops with background work in flight puts the pet into a
calm **waiting** animation (blowing bubbles), with no bubble, badge or celebration.
An `idle_prompt` during that time changes nothing. When Claude wakes and finishes
with nothing left running, the pet shows the normal finished reaction.

## Constraints

- Claude Code's `Stop` hook input carries `background_tasks`: one entry per task
  still in flight when the turn ended (`id`, `type` such as `shell`, `subagent`,
  `monitor`, `workflow`, `status`, `description`, and `command` for shell tasks).
  No hook fires when a background task completes; Claude starts a new turn on
  completion, which ends in another `Stop`.
- Claude's documented `idle_prompt` rule already skips background *agents*, but not
  background shells, which is the reported bug.
- Codex hooks have no idle or notification event and no background-task field
  (`Stop` carries `turn_id`, `stop_hook_active`, `last_assistant_message`). Codex only
  reaches attention through `PermissionRequest`, so it has no false "needs input".
  Codex is out of scope.
- The hook stays as before: always exit 0, silent, fast. Only a one-bit verdict
  leaves it; task ids, descriptions and commands are never read into the envelope.
- The v1 wire protocol only grows: one optional boolean. Existing fields keep their
  meaning.
- The `background_tasks` shape comes from the documentation. It is checked against
  a real `Stop` payload before release (see Validation).

## Wire protocol

New optional field `waiting` (boolean, `true` only), allowed only on
`turn_finished`: the turn ended with background work still running. It is rejected
on every other kind, like `risky` outside `tool_start`. Without it `turn_finished`
means what it does today.

Adapter (`providers/adapters.cpp`, Claude only): a `Stop` without `agent_id` whose
`background_tasks` is a non-empty array sets `"waiting": true`. A `Stop` with
`agent_id` stays `session_end` for the child and never carries `waiting`. A missing,
empty or non-array field (older Claude Code) sends plain `turn_finished`. Codex never
sets it. `hookEvents` is unchanged, so no integration update is needed.

`Event` gains `bool waiting`, parsed and validated next to `risky`.

## Session state

New session state `waiting`. `Sessions::apply`:

- `turn_finished` with `waiting`: clears tools, state `waiting`, no reaction timer.
  It keeps `turnStarted`, so the turn's duration is measured once, from the prompt
  to the final finish, including the job. `lastTurnMs` is not set.
- While `waiting`:
  - `attention` with reason `input` (`idle_prompt`, `elicitation_dialog`) is
    ignored: no state change, no alert. `approval` and reasonless attention still
    raise attention as today.
  - `prompt`, `tool_start`, `interrupt`, `turn_failed` and `session_end` leave it
    by their usual rules.
  - `turn_finished` leaves it: with `waiting` again it stays `waiting`; without, it
    becomes `turn-finished` with the normal reaction and alert.
  - `tool_end` and `error` without a matching started tool do not leave it.
- No timer ends it. The 30-minute session expiry still applies, so a job that runs
  longer than 30 minutes with no events in between drops the session as today. A
  task that never ends (such as `tail -f`) keeps the pet waiting until the user acts.

Aggregate priority becomes attention > exhausted > error > turn-finished > working >
reading > thinking > **waiting** > idle > inactive, so live work in another session
shows first. Session list rows sort attention, exhausted, error, working, reading,
thinking, waiting, finished, idle, stopped. `unresolvedAttention` and the badge are
unchanged.

Checkpoint/restore keeps the new state like the others; `turnStarted` is already
saved.

## Alerts and monitor

- A `waiting` finish raises no alert and dismisses nothing beyond what any
  non-attention state already dismisses.
- Session-row status "Waiting" through `Alerts::tr`, with a Vietnamese entry.
  Session list colour: indigo `#4c6ef5`, distinct from the active blue `#1971c2`.
- `Monitor`: a `waiting` finish does not call `mood().finished`, does not set
  `lastTurnMs_`, and does not trigger the bedtime note. The final real finish does.
  `waiting` is not `urgent`: a risky-command startle and easter eggs behave as for
  `thinking`.
- Ambient fidgets and wander run only in `idle`, so they stay quiet while waiting.

## Animation

- Sprites added with
  `scripts/add_sequences.py IDEL/Bubbles/A IDEL/Bubbles/B IDEL/Bubbles/B_2 IDEL/Bubbles/C`
  (68 PNGs at 125 ms: start 12 frames, loops 24 and 24, end 8), which updates
  `manifest.json`, `available-animations.json` and the catalog's `sequences`.
- Catalog: `states.waiting` = `[IDEL/Bubbles/A, IDEL/Bubbles/B, IDEL/Bubbles/C]`,
  `variants.waiting` = `[{"sequences": [IDEL/Bubbles/A, IDEL/Bubbles/B_2, IDEL/Bubbles/C], "weight": 1}]`,
  `playback.waiting` = `{"mode": "phased", "after": "idle"}`. Each new sequence's
  `state` field is `waiting`.
- `session_playback.h` needs no entry: `waiting` passes through as the animation key.
- Bubbles has only Nomal art, so every mood plays it.

## Recap

A `turn_finished` with `waiting` is not counted as a turn. The final finish counts
once, with the whole duration. No new fields in `recap.json`.

## Testing

- `providers`: `Stop` with non-empty `background_tasks` → `turn_finished` +
  `waiting`; empty array, missing field and non-array → no `waiting`; subagent `Stop`
  with tasks → `session_end` without `waiting`; Codex `Stop` never sets it; the
  envelope carries no task content.
- `events`: `waiting` accepted only on `turn_finished` and only as `true`. State:
  enters `waiting`; `input` attention ignored; `approval` attention honoured;
  prompt, `tool_start`, interrupt leave it; a plain finish after waiting gives
  `turn-finished`; `turnStarted` survives so `lastTurnMs` spans the job; aggregate
  priority puts waiting below thinking and above idle; checkpoint round trip.
- `alerts`: no alert on a waiting finish, no "Needs input" alert from `idle_prompt`
  while waiting, row title and sort order.
- `prototype`: the catalog loads, `waiting` resolves to the Bubbles sequences and its
  variant, and the session state maps to it.
- `i18n`/CI: `update_translations` run, Vietnamese entry filled in,
  `check_translations.py` clean. `verify_assets.py` passes.

## Validation before release

Capture one real Claude Code `Stop` payload with a `run_in_background` shell still
running (for example by logging the hook's stdin to the scratch area during a manual
run) and confirm the `background_tasks` field name and array shape. Record the
Claude Code version and the result in ADR 0026's Validation section.

## Docs

`docs/events.md` (field, state rules, priority), `docs/integrations.md` (Claude
`Stop` row; Codex has no background-task signal), ADR `0026-background-waiting.md`
plus its `adr/README.md` row, and a `starter/README.md` section.

## Out of scope

- Codex background terminals (no signal).
- Showing which job runs, a count, or a progress estimate.
- Extending the 30-minute session expiry for long jobs.
- Happy or poor-condition mood art for waiting.
