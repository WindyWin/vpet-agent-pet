# Out-of-quota animation — design

Date: 2026-10-07. Status: approved in conversation; ADR 0023 records the decision.

## Intent

When an agent turn stops because the provider's usage limit or credits ran out, the
pet shows it clearly and keeps showing it, so the user knows to wait for the reset or
top up. Today such a turn sends no hook the pet listens to, so the pet keeps thinking
or working until the session expires 30 minutes later.

Success: a Claude session that hits `rate_limit` or `billing_error` turns the pet into
a sick-in-bed animation with a sticky bubble that names the reason, and both clear on
the next prompt in that session. Other API failures end the turn visibly instead of
leaving the pet thinking.

## Constraints

- Only Claude Code reports this. Its `StopFailure` hook fires when a turn ends on an
  API error and carries `error_type`: `rate_limit`, `overloaded`,
  `authentication_failed`, `oauth_org_not_allowed`, `account_on_hold`,
  `billing_error`, `invalid_request`, `model_not_found`, `server_error`,
  `max_output_tokens`, `cloud_credential_error`, `unknown`. Output and exit code are
  ignored. Codex CLI 0.156.0 has no hook or field for limits or failed turns, so
  Codex sessions are out of scope until it adds one.
- The hook stays as before: always exit 0, silent, fast, and nothing but the
  normalized event leaves it. `error_type` is a category, not content, and is mapped
  to a reason inside the hook. The raw value is never sent.
- The v1 wire protocol only grows: one new kind, one new reason pair. Existing
  fields keep their meaning.
- No reset time is available, so nothing guesses one.

## Wire protocol

New kind `turn_failed`: the turn ended on a provider failure. Optional `reason`:

| Claude `error_type` | `reason` |
| --- | --- |
| `rate_limit` | `limit` |
| `billing_error` | `billing` |
| anything else, or missing | omitted |

`reason` stays forbidden on all other kinds except `attention` (`approval`/`input`).
`turn_failed` carries no `tool_id` and is rejected with one. Like `Stop`, a
`StopFailure` with an `agent_id` becomes `session_end` for the child, so a subagent's
failure never marks its parent. Event IDs are a delivery UUID, as for other lifecycle
events.

`hookEvents("claude")` adds `StopFailure` (12 events). Codex is unchanged.

## Session state

`Sessions::apply` for `turn_failed`:

- Clears the session's tools, attention, attention tools and reason, and dismisses
  its attention alert. The turn is over.
- Does not set `interrupted`. An API failure happens at a model call, after the
  turn's tools have already reported, so there are no stale callbacks to drop. And
  Claude can auto-resume a quota-stopped turn without a new `UserPromptSubmit`, so
  later tool events must still be accepted.
- With `reason` `limit` or `billing`: state `exhausted`, `Session::reason` keeps the
  reason. There is no timer. A `prompt` (the user retrying), a `tool_start` (work
  resumed, for example by auto-resume), `turn_finished`, `session_end` or the
  30-minute expiry leaves it. `tool_end` and `error` without a matching tool
  start do not, and neither does `attention`, which outranks it anyway.
- Without a reason: state `error` with the usual 4-second reaction, then resume
  `idle` (not the pre-failure activity).

Aggregate priority becomes attention > exhausted > error > turn-finished > working >
reading > thinking > idle > inactive. Session list rows sort attention, exhausted,
error, working, reading, thinking, finished, idle, stopped. `unresolvedAttention` and
the badge are unchanged: exhausted is not waiting on a decision.

Checkpoint/restore keeps the new state and reason like the others.

## Alerts

- `turn_failed` with a quota reason raises an alert of kind `exhausted` with the
  reason, `expires = 0` (sticky). It clears when the session leaves `exhausted`, not on a timer. Dismissal hides it without changing the
  session.
- `turn_failed` without a reason raises an `error` alert (fades after 10 s) whose
  title is "Turn failed" rather than "Tool error". The alert keeps kind `error` with
  a new internal reason `turn` to pick the title.
- Queue rank: attention 0, exhausted 1, error 2, finished 3.
- Titles: `limit` → "Usage limit reached", `billing` → "Out of credits",
  session-row status the same. All through `Alerts::tr` with Vietnamese entries.
- The bubble shows `exhausted` at the "requests and errors" level and above, like
  errors. Accent colour amber `#e67700`. The session list uses the same colour.
- Monitor dismissal on focus covers the new kind.

## Animation

- New display state `out_of_quota`, playback `{"mode": "loop", "after": "idle"}`,
  sequences from the unused `Default/Ill` art (pet sick in bed with a thermometer):
  `Default/Ill/1` (sick loop, weight 2) and `Default/Ill/2` (cough, weight 1) as
  weighted variants.
- Sprites added with `scripts/add_sequences.py Default/Ill/1 Default/Ill/2`, which
  updates `manifest.json`, `available-animations.json` and the catalog. About 33
  PNGs go into `artwork.rcc`.
- `session_playback.h` maps session state `exhausted` → `out_of_quota`.
- `Monitor` treats `exhausted` like `attention`/`error` where it suppresses things:
  no risky-command startle, no easter-egg surprise, no ambient fidgets or wellness
  interruptions while it shows. The player holds the state (`select(..., true)`).
- Mood: entering `exhausted` counts as a failure (`mood().failed(now)`), the same as
  an error.

## Integration upgrade

Users who enabled Claude hooks before this version have 11 of 12 handlers, so
`inspect` reports a partial install. In the settings integration row a partial
install shows **Update** instead of **Disable**. Update runs `enable`, which already
merges idempotently and keeps foreign handlers. Disable stays available when all
handlers are present. `check_install.py` keeps enforcing that foreign handlers
survive.

## Recap

A `turn_failed` event counts under the existing daily `errors` counter. No new
fields in `recap.json`.

## Testing

- `providers` (`tests/providers_tests.cpp`): each `error_type` mapping, missing
  `error_type`, subagent `StopFailure` → `session_end`, `StopFailure` absent for
  Codex, `hookEvents("claude")` count.
- `events`: `turn_failed` parse rules (reason values, no `tool_id`, reason rejected
  on other kinds). Exhausted is sticky across time, `tool_end` and toolless
  `error`, and is cleared by prompt, `tool_start`, `turn_finished` and end. A failure without reason reacts, then goes idle. Aggregate
  priority puts exhausted below attention and above error. Checkpoint round trip.
- `alerts`: sticky exhausted alert, rank order, titles for both reasons and for
  "Turn failed", clearing on prompt.
- `prototype`: the catalog loads, `out_of_quota` resolves to `Default/Ill` sequences,
  and the session state maps to it.
- `i18n`/CI: `update_translations` run, Vietnamese entries filled in,
  `check_translations.py` clean. `verify_assets.py` passes.

## Docs

`docs/events.md` (kind, reason, state rules, priority, alerts), `docs/integrations.md`
(Claude `StopFailure` row, Codex gap), ADR `0023-out-of-quota.md` plus its
`adr/README.md` row, and a `starter/README.md` section.

## Out of scope

- Codex quota detection (no signal).
- Showing a reset time or countdown.
- Distinct animations for other failure types (`overloaded`, auth, …).
- Claude `Notification` `quota_auto_resume_*` types. Work resuming after
  auto-resume already clears the state through its first `tool_start` or
  `turn_finished`.
