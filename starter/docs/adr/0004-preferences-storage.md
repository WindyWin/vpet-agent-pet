# 0004. Atomic preferences storage

- Status: Accepted
- Date: 2026-10-04

## Context

Size, position and other settings must survive restarts, a corrupt file must not
lose the pet, and the display-free `agent-pet autostart` command may write the same
file while a pet runs.

## Decision

`PreferencesStore` writes `preferences.json` atomically under Qt's
`AppDataLocation`; malformed files are preserved and defaults are used. Size,
position and on-top survive restart. The startup keys `autostart` and
`when_idle` may be written by the display-free `agent-pet autostart` command
while a pet runs, so the pet re-reads the file before every save instead of
overwriting them; the hook reads `autostart` without linking any GUI code. Off-screen positions are brought inside an
available monitor; screen geometry changes trigger recovery. No session state is
persisted; only the [daily recap](0018-daily-recap.md)'s counters are, in `recap.json` beside it. The settings window remains open only on request and closing it keeps
the pet running.

## Consequences

- The pet re-reads `preferences.json` before every save; keep that when touching
  settings.
- New settings are optional keys; files written by older versions keep loading with
  defaults for the missing keys.
- Malformed files are kept for inspection rather than overwritten on load.
