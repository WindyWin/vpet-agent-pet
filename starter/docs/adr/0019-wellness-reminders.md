# 0019. Wellness reminders

- Status: Accepted
- Date: 2026-10-06

## Context

Issue #38: remind the user to rest their eyes and drink water, counting only time
the user is actually present and never interrupting work that needs them.

## Decision

Issue #38. `Wellness` (`src/desktop/wellness.*`) is a plain value class that counts
active time with time passed in, and `Monitor` drives it:

- **Activity.** Every accepted event (`Monitor::apply`) and every pointer move seen by
  the 250 ms update (`Monitor::pointer`, `QCursor::pos` by default, replaced in tests)
  calls `Wellness::activity(now)`. A gap between activities counts up to one minute,
  so reading or thinking still counts while a longer pause holds the timers; a gap of
  five minutes or more is a real break and resets both. Only pointer moves and
  `prompt` events are the user's own; other agent events count only while the user was
  seen within five minutes, so an agent working for an absent user pauses and then
  resets the timers. While the screen is locked nothing counts, no reminder shows, one on screen (or its eye-break countdown) goes away without a cheer, and
  both timers start over until the user is seen again (`Wellness::reset`):
  `platform::ScreenLock` (`pet_native`, `src/platform/desktop/screensaver/`) follows
  the `ActiveChanged` signal of `org.freedesktop.ScreenSaver` and
  `org.gnome.ScreenSaver` after one asynchronous `GetActive`, and `main.cpp` hands it
  to `Monitor::locked` (replaced in tests). Keyboard input outside the pet is not seen:
  X11 idle time (XScreenSaver) would need another `pet_native` seam and was left out.
- **Due.** An interval (`eye_minutes` 0/20/30/45, `water_minutes` 0/45/60/90; 0 is
  off; defaults 20 and 60) of counted time makes a reminder due, eyes first. Only the
  intervals are saved in `preferences.json`; an interval that is not a choice falls
  back to the default.
- **Calm.** A due reminder waits until the user was seen within the last minute
  (`Wellness::present`, which also holds it behind a locked screen), the pet is visible and free (not held,
  walking, flying or mid-surprise), alerts are not muted, no alert bubble or note
  shows, and no session waits on the user or has a fresh error. During quiet hours
  (22:00 to 06:00 on the eggs' clock) it is dropped instead, so the morning does not
  open with a backlog. Muting holds it, standing in for the focus mode of issue #34.
- **Showing.** The pet plays a catalog pool through `EasterEggs::surprise(pool, true)`,
  which plays even when easter eggs are off, and says the note in the `NoteBubble`.
  Showing counts as given: ignored, it fades and the timer starts over. The bubble's
  new `clicked` signal answers it: water plays `reminder_done` at once; an eye break
  first counts 20 seconds down in the note (clicking again ends it), then plays
  `reminder_done` with a short note.
- **Snacks.** A finished turn celebrated with a state from the `snack` pool already
  shows the thirsty or hungry art, so it counts as the water reminder and restarts that
  timer.

| Pool | State | Art |
| --- | --- | --- |
| `eye_break` | `fidget_yawn` | `IDEL/yawning`: eyes closed, a long stretch |
| `water` | `snack_thirsty` | `Switch/Thirsty`, shared with the snack |
| `reminder_done` | `cheer_shy` | `Say/Shy` |

The yawn stands in for an eye rub. The archive's `Drink` set is a layered food animation (cup drawn between a back and a
front layer), which the player does not compose, so no new art was imported.

## Consequences

- Keyboard input outside the pet is not seen; X11 idle time would need another
  `pet_native` seam.
- Reminders reuse existing art and the surprise mechanism; no new art was imported.
- Only the intervals are persisted.

## Validation

### Wellness reminders evidence — 2026-10-06

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the 6.5 requirement was
lowered only locally): all CTest suites passed. New tests cover active-time counting
(grace, pause, five-minute reset, out-of-order activity), due order and `given`,
interval choices and quiet-hour boundaries; the monitor showing the eye break after 20
minutes of pointer movement with its countdown and cheer, the water reminder held by
a waiting approval until it is answered, the click reaction, muted alerts holding it,
quiet hours dropping it and a break resetting it; and the settings group with
persistence, legacy files and malformed values. Still open: how the cadence feels on
a real desktop, and CI on Qt 6.5.3.
