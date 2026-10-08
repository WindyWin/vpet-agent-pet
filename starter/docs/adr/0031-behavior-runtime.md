# 0031. Behavior runtime: one arbiter for the pet's competing behaviors

- Status: Accepted
- Date: 2026-10-08

## Context

Session activity, startup and quitting, touch and dragging, ambient fidgets and naps, walking, edge hiding,
mood treats, easter-egg surprises and celebrations, clock reminders and wellness reminders all compete to
show something on one pet (#67). [0029](0029-cues.md) took animation names out of event code, but each
behavior still decided for itself whether it might run by asking the others: `Monitor::update` returned
while quitting, left `PetWindow::resting()` alone, yielded to `EasterEggs::surprising()`, re-selected the
aggregate after a hold through a `lastAggregate_` comparison and never re-selected `error` or
`turn-finished`; `Monitor::calm()` gated reminders on the bubble, the note, a hold, a surprise, walking and
flight; `EasterEggs` inferred a surprise's lifetime from `Player::requestedState()` and a 15-second clock;
`PetWindow` chained its quit animations by name with a timer per step; and a snack cancelled the water
reminder when it was chosen, whether or not it played.

## Decision

- **`behavior::Runtime`** (`src/behavior/`, library `pet_behavior`, Qt Core only, no timers) is the one
  place that decides. Producers submit an `Intent`: a source and key (its identity), a cue from
  `cues.json` (or, for ambient and touch, one of the pet's own fidget, move or edge states), a **policy
  class**, a **lifetime**, how long it may wait, semantic **effects**, and optionally the state the
  producer already drew. Producers never pick a number.
- **Policy classes**, highest first: Shutdown, Urgent (attention, exhausted, error), Surprise (danger,
  konami, reminder-done), Reminder (eye-break, water, monday, leave-work, sleep), Celebration, Startup,
  Activity (the other session states), Ambient (fidgets, moves, naps, edge hiding). A one-shot interrupts
  only a strictly lower one-shot; equal rank never interrupts. Admission reads a shared `Context`
  (visible, handled, moving, present, muted, speaking, attention):
  - Urgent activity interrupts every reaction below it and refuses new surprises, celebrations and ambient.
  - Reminders wait for calm: the user present, the pet visible and free, not muted, nothing being said,
    nothing waiting on the user.
  - Ambient runs only while the activity is idle-like.
  - Any session activity ends the arrival (`start`), as it always has.
- **Lifetimes.** Session activity is persistent, or a **moment** for cues of the `once` shape
  (`turn-finished`, `error`): presented at most once, so a hold or a reaction never replays it, and a
  celebration that starts stands in for the finished turn. Rests (a nap, hiding at an edge) last while the
  pet idles and end with activity. One-shots end on completion, interruption, missing art or a timeout
  (15 s; 10 s for shutdown).
- **Waiting is bounded:** at most four one-shots, one per identity, ordered by rank and then submission;
  each expires (checked whenever waiting work could start, not only on a tick), and a waiting celebration
  is dropped when the activity moves on. Resubmitting an identity that waits or shows is a duplicate and
  restarts nothing.
- **Feedback is correlated.** Every request carries an instance number; a report about any other
  instance is ignored, so a late completion never ends its replacement. If presentation leaves the
  activity on its own (a developer selection, a phased state ending), the next tick shows it again.
- **Shutdown always finishes:** it cancels everything, refuses later submissions, and reports `finished`
  on completion, on missing art, after its timeout, or at once for a hidden pet.
- **`Stage`** (`src/animation/stage.*`) owns the runtime and presents its requests on the `Player`:
  - What it plays: the producer's drawn state, else the state cue's state, else a draw from the reaction
    cue's pool, else the pet's own state.
  - What it reports: entry starts a request; its end, or a state ending by itself, completes it; any
    other state entered interrupts it; a missing pool is unavailable at once.
  - Two new `Player` signals feed it. `heldChanged` makes the runtime's `handled` context follow drags,
    petting and falls; it comes before the held state's entry, so what waited behind a grabbed reaction
    keeps waiting. `finishing()`, with `completed` now also emitted when a phased state ends by itself,
    tells a natural end from an interruption.
  - `annoyed` is one request: the complaint, a 2.25 s pause, then `quit-angry`.
  - A request for `idle` leaves the pet's own idle decoration (a fidget, a walk, an edge) playing.
- **Outcomes, not animation names, drive bookkeeping.**
  - A reminder's note shows when the runtime admits it, so art and note come together. When the art is
    missing, the note shows alone.
  - Wellness intervals and clock reminders count as given on admission; quiet hours stay with the
    wellness schedule. Each update withdraws reminders no longer due before anything can admit them.
  - A treat the runtime refused or dropped returns to `Mood` for the next turn (`Mood::keep`), and the
    birthday cheer counts as given only once it starts playing.
  - The snack's `water` effect counts only once the snack starts playing.
- `Sessions` keeps ranking sessions; the runtime receives the aggregate as one intent. Alerts, the bubble
  and the tray keep their own delivery. The developer States menu, Space, the preview dialog and `--state`
  still select states directly.

## Consequences

- `Monitor` no longer has `calm()`, the `lastAggregate_` logic or the snack-pool scan. `PetWindow` no
  longer has `resting()`, `quitCue_` or `playQuitAnimation`. `EasterEggs` no longer tracks a surprise
  itself; `surprising()` asks the runtime. `Ambient` and `EasterEggs` play nothing without a `Stage`.
- Intentional behavior changes:
  - Clock reminders (Monday, go-home, sleep) now wait for calm within their window, and muting holds
    them like wellness reminders. Before, they played at once whatever was happening, and muting dropped
    only the note.
  - The snack cancels the water reminder only once it plays.
  - Shutdown has one 10-second bound instead of a 5-second timer per step.
  - Surprises and celebrations are refused while the pet is hidden; a refused treat waits for a later turn.
- Plugin rules (#43) will submit ordinary intents. The loader assigns their class and never grants
  Shutdown, Urgent or Startup, so a plugin cannot mask attention or claim lifecycle priority.
- `docs/superpowers/specs/2026-10-08-behavior-runtime-design.md` has the full policy table and the
  guard-by-guard migration.

## Validation

### Behavior runtime evidence — 2026-10-08

- Ubuntu 24.04, Qt 6.4.2 (the CI's Qt 6.5.3 could not be downloaded in that environment, so the version
  check and one `QDataStream::Qt_6_5` use were relaxed locally, not in the tree), GCC 13, CMake 3.28:
  `ctest --test-dir build` passed all 15 tests.
  - The new `behavior` suite (47 functions, 53 cases) drives the runtime with a hand-set clock and a fake
    presentation. It covers attention versus surprise, activity changes during handling, equal-rank ties,
    expiry, deduplication, bounded waiting, moments, missing optional art, stale feedback, timeouts,
    reminder admission and shutdown failure.
  - `prototype` adds `stagePresentsTheRuntime` on VPet. It checks start and completion, interruption from
    outside, a missing pool, handling, idle decoration and the annoyed-then-quit-angry chain.
  - It also adds a lock-and-unlock case to `monitorWellnessReminders`, which fails without its fix.
  - Every existing `Monitor`, `PetWindow`, `Ambient`, `EasterEggs` and `Mood` test still passes. Three
    were adapted: standalone `Ambient` and `EasterEggs` need a `Stage`, a surprise times out on the
    runtime's tick, and a clock reminder waits for the previous note to close.
  - Review follow-up (same day): grabbing the pet during a surprise no longer starts a reminder waiting
    behind it, a waiting intent past its deadline never starts on a release or completion, a reminder turned
    off while it waits never shows, and the birthday cheer is not lost when its celebration is dropped. The
    first three each have a test (`stagePresentsTheRuntime`, `waitingIntentsExpireBeforeAnyTick`,
    `monitorWithdrawsTurnedOffReminders`) that fails without its fix.
- Open: macOS and Windows CI runs; a manual pass of the acceptance checklist in
  [0008](0008-window-behavior.md#manual-acceptance-checklist) on a desktop.
