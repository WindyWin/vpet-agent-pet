# 0018. Daily recap

- Status: Accepted
- Date: 2026-10-06

## Context

Users want a summary of the day's agent work without the pet storing sessions,
paths or content.

## Decision

The pet sums up the day's agent work in its `NoteBubble`: "Today: 38 turns across 3
projects · 2 approvals waited 10+ min · longest run 22 min". `Recap`
(`src/sessions/recap.*`, in `pet_core`) counts per local day from the events
`Sessions` accepted, so duplicates and stale callbacks of an interrupted turn never
count. `Monitor::apply` hands it each accepted event with the session's record after
it, and the local date of arrival:

| Counter | From |
| --- | --- |
| Turns, per project | `turn_finished`; the project is the folder name of the session's path (up to 64 a day) |
| Longest run | `Session::lastTurnMs` of those turns, as the long-turn egg measures it |
| Errors | `error` |
| Approvals | `attention` without `reason: input`, once per request until the session moves on |
| Waits | From the request to the session's next accepted event that leaves `attention`; 10 minutes or more counts as long. A session that ends while waiting records no wait |

Subagents end with `session_end`, never `turn_finished`, so only top-level turns
count. Turns, errors and requests count on the day they arrive, a wait on the day it
is answered. Pending requests are kept in memory only, so a restart forgets them.

`RecapStore` writes `recap.json` beside `preferences.json` with `QSaveFile`, two
seconds after the last change and when monitoring stops. It holds 14 days, oldest
dropped first, each with dates, counts and folder names only: no paths, providers,
session IDs, prompts or tool content. Only the pet writes it, so unlike preferences
there is no re-read before saving. A missing or invalid file (bad version, dates out of
order, negative or non-integer counts, over 1 MB) loads as empty and is replaced by the
next save. `--no-persist` and the smoke test use no file.

Right-click → **Today's recap** says the summary; clicking that bubble swaps in the
breakdown (turns per project, busiest first, then errors, approvals with the longest
wait, and the longest run) and keeps it up for 15 seconds. The menu works while muted.
A hidden pet shows the breakdown as a tray notification instead. The go-home reminder
(`leave_work`, weekdays from 16:45) appends the summary when the day has finished
turns and the `recap` preference (default on, Settings → **Recap**) allows it; it
follows the reminder's own rules, so easter eggs must be on and the pet visible and
unmuted.

## Consequences

- `recap.json` is the only persisted record of agent work: counts and folder names,
  14 days.
- Only the pet writes it, so it needs no re-read before saving.
- Pending approvals are in memory, so a restart forgets them.

## Validation

### Daily recap evidence — 2026-10-06

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the 6.5 requirement was
lowered only for the local build): all eight CTest suites passed. New tests cover
counting by project and day, approvals counted once per request and their waits
measured on answer (none for a session that ended, none for input questions), the
summary and breakdown wording, the 14-day window with an older date never kept, the
store's round trip with only counts and folder names written and invalid files loading
as empty, and through `Monitor`: the menu's summary then breakdown on a click, the
empty-day note, the go-home reminder with and without the recap, the counters surviving
a restart, and the persisted setting in the settings dialog. Still open: how it reads on
a real desktop, and CI on Qt 6.5.3.
