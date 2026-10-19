# 0046. Plan limits footer in the session popup

- Status: Accepted
- Date: 2026-10-19

## Context

The session popup reports what each agent is doing, but nothing about the
account behind it. Hitting a plan limit mid-turn looks like a slow or broken
agent, and the user must open the provider's web dashboard to discover the
cause. Both CLIs already hold an OAuth sign-in on the same computer whose
providers expose usage endpoints for that account, so the pet can look the
limits up itself.

## Decision

- A `quota::Service` reads each provider sign-in read-only: `auth.json` (or
  `CODEX_HOME`) for Codex and `.credentials.json` (or `CLAUDE_CONFIG_DIR`) for
  Claude Code, with the macOS Keychain read through `/usr/bin/security` when it
  may hold a newer token. It never renews, writes or shells out beyond that
  query, and never stores tokens, usage or account identity beyond memory.
- Usage comes only from `https://chatgpt.com/backend-api/wham/usage` and
  `https://api.anthropic.com/api/oauth/usage`, requested with the exact bearer
  headers of the signed-in account, redirects refused. Malformed answers never
  replace a known reading with zero, and 429 honours `Retry-After` as backoff.
- The panel is the footer of the session popup (its visibility polls the data,
  the pet's lifetime does not own it), refreshed at most every 5 minutes while
  shown, 30 seconds minimum between attempts, minding cached staleness: only
  the CLI itself renews sign-ins, and an expired or missing sign-in never
  triggers a network request. A "Refresh" button forces an immediate check.
  Each provider is one row: a ring gauge for its shortest window (5 hours when
  reported), the longest unscoped window beside it, and the reset times; every
  window, including scoped ones such as Opus, is in the row's tooltip.
  Account limits are subscription-wide, never a session's token count.
- Readings are keyed by a credential hash: a different account starts with no
  numbers instead of inheriting the previous account's usage.
- Because the footer makes the popup much taller, popup placement must never
  clamp onto the pet: the candidates above and below slide along a screen edge and the ones beside the pet slide up off the bottom edge, and
  the last-resort clamp ends the popup above the pet (or starts it below) so
  the pet stays visible. Rebuilding the footer after each reading must show
  every created widget again, because hidden rebuilt widgets contribute zero to
  layout hints and an unshown reading would be clipped.

## Consequences

The pet shows plan limits for the accounts whose CLI runs on the user's own
computer; accounts behind other machines or web sessions stay invisible, and a
locked Keychain only explains why. Sending the bearer token to the two fixed
endpoints is the price of showing live numbers; no redirect or mirror can
carry it elsewhere. Provider API changes that rename fields show as failures
with last known usage, not invented zeros.

## Validation

### Automated evidence — 2026-10-19

The `quota` suite (offscreen) covers usage parsing (window kinds, resets,
malformed bodies, clamping outside 0–100), OAuth-only credential parsing and
exact request hosts/headers with redirects refused, `CODEX_HOME` authority
without touching files, cache dedupe and minimum intervals, `Retry-After`
backoff that a forced refresh cannot bypass, failures keeping the old
reading, account identities clearing readings, missing/expired sign-ins
making no request, and the footer's geometry, polling and retranslation. The
popover placement regression test in `prototypeTests` pins the pet parked
near the bottom-right corner with the popup at full quota height.
