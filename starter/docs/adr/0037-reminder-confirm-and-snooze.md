# 0037. Confirmable reminders, re-remind and Snooze

- Status: Accepted
- Date: 2026-10-09

## Context

The water, eye-break, lunch and go-home reminders were plain speech bubbles. A click meant "done", but nothing said
so, go-home had no answer at all, and an ignored reminder was counted as given at the moment it appeared: it faded
after seven seconds and did not come back until the next interval or tomorrow. There was no way to say "not now",
and no quiet mode (#34); [0015](0015-easter-eggs.md) left it out.

## Decision

- **Confirmable reminders.** `eyes`, `water`, `lunch` and `leave-work` show `NoteBubble::ask`: the words with **Done**,
  **Later** and **Skip today**. A click on the words still counts as Done, so the old habit works. Monday blues and
  the bedtime note stay plain remarks.
  - Done gives the reminder (`Monitor::give`: the wellness timer starts over, or the clock reminder is done for the
    day) and earns the happy reaction; an eye break still counts its 20 seconds first.
  - Skip today gives it without the reaction and lets it go until tomorrow (`Nudges::skipDay`; a skipped wellness
    reminder lets its interval pass silently, so the other one can still come).
  - Later puts it off for ten minutes and does not use up one of its asks.
- **Re-remind.** A reminder is no longer given when it appears, only when it is answered, skipped or has been asked
  `Nudges::maxAsks` (3) times. A bubble stays up for a minute; if it fades unanswered, or is replaced by another
  note, it is asked again after ten minutes (`Nudges`, in memory). Withdrawal is unchanged: a break, a locked screen,
  quiet hours, the end of the lunch window or turning it off all make it not due, and `Nudges::forget` drops it.
- **Snooze (#34).** `Snooze` (in memory, never persisted) is on `PetWindow` beside Mute: 15 minutes, 30 minutes,
  1 hour, until this turn finishes (the last turn going on, not whichever session finishes first), until tomorrow (06:00, where the quiet hours end), with **Resume now** and the
  time left while it is on. `PetWindow::quiet(now)` (muted or snoozed) is the one test for "say nothing": the alert
  bubble and sound, remarks, the bedtime note, and, through `Context::muted`, every reminder. The attention badge and
  the animations are unchanged. The pet shows a small "z" badge and the tray tooltip says "snoozed". Alerts that arrived
  meanwhile are not replayed: when it ends the alert bubble shows what is still pending as one bubble with "+N". A
  reminder on screen when a snooze starts is taken down and put off. Held reminders stay due and show after the
  snooze, if they are still due.

## Consequences

- Reminder timing changed: ignoring a reminder used to lose it for a whole interval, and now it comes back three
  times at ten-minute spacing before being given.
- `Monitor` gained `Nudges` and the answer handlers; the runtime, `Wellness` and `EasterEggs` are unchanged.
- No preference was added, so nothing in `preferences.json` changes. A restart clears a snooze and forgets pending
  asks.
- The old tooltip "Click a reminder to say you did it." and one Vietnamese entry changed; the new strings are
  translated.

## Validation

### Initial evidence — 2026-10-09

- `snoozeAndNudges` covers the durations, the end of a turn, "until tomorrow" before and after 06:00, and the ask
  counting (three asks, Later not counted, replacement, forget).
- `remindersAreConfirmedAndAskedAgain` drives a water reminder: Later returns after ten minutes; ignoring returns
  twice more and then gives; Skip today gives without cheering; Done cheers.
- `snoozeSilencesEverythingButTheBadge`: no reminder and no bubble while snoozed, the badge and the tray tooltip
  still show an approval, resuming shows the waiting alert, a timed snooze ends by itself and one "until this
  turn finishes" ends on `turn_finished`.
- The lunch and recap tests now assert that a reminder asks, and is given only once answered.
