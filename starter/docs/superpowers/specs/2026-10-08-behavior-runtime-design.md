# Behavior runtime — design

Date: 2026-10-08. Status: implemented; ADR 0031 records the decision.
Tracking: issue #67. Builds on #62 Part 1 (pet packs, ADR 0028) and #64 (cues, ADR 0029), both
on `main`; this design uses #64's cue vocabulary (`src/animation/cues.json`) and its `Player::play`,
`pool` and `stateFor` mapping API. Order: #62 Part 1 → #64 → #67 → #43.

Where it lives:

| File | Role |
| --- | --- |
| `src/behavior/runtime.*` (`pet_behavior`, Qt Core) | the runtime: admission, rank, interruption, waiting, feedback, shutdown |
| `src/animation/stage.*` | `Stage`: owns the runtime and presents its requests on the `Player` |
| `tests/behavior_tests.cpp` (`behavior` suite) | the policy, with a hand-set clock and a fake presentation |
| `tests/prototype_tests.cpp` | `Stage` on VPet, and the producers' wiring through `Monitor` and `PetWindow` |

The tests were first written against the header alone and checked against a throwaway reference
implementation; the runtime then replaced it. A few decisions changed while implementing, and this
document now describes what was built:

- **Startup** ranks just above Activity, and any session activity ends it, as before. The first design
  let only urgent activity cut it, which changed how every launch with sessions looked.
- **Quiet hours** left the runtime's context: they are the wellness schedule's, and as a context flag
  they would have refused the default 22:00 sleep reminder.
- **`Stage` owns the runtime** and re-emits its outcomes as Qt signals, so several producers can listen.
- **Producers may hand over a drawn state** (`Intent::state`), so `Mood`'s and `EasterEggs`' own draws, and
  their tests, stay where they are.
- **Presentation leaving the activity** on its own (a developer selection, a phased state ending) gets
  the activity shown again on the next tick, as `Monitor` re-asserted it before.

## Intent

The pet has many behaviors that want to show something: session activity, startup and shutdown,
touch and dragging, ambient fidgets and naps, wandering, hiding at a screen edge, mood treats,
easter-egg surprises and celebrations, clock reminders and wellness reminders. Today each decides
for itself whether it may run by inspecting the others or the `Player`'s requests. #64 took animation
names out of event code; this design takes the arbitration out of it, with one small runtime between
the producers and the cue mapping:

```text
producer (Monitor, PetWindow, Mood, EasterEggs, Wellness, Ambient)
        │  Intent {source, key, cue, policy, lifetime, expiresAt, effects}
        ▼
behavior::Runtime ── admission, rank, interruption, deferral, expiry, timeouts
        │  Request {instance, cue, interrupt}           ▲ Feedback {Started, Completed,
        ▼                                               │          Interrupted, Unavailable}
Stage (adapter) ── Player::play / pool (#64) ──────────┘
        │
        ▼  Outcome {Admitted, Started, Completed, Interrupted, Unavailable, TimedOut, Expired, Dropped}
producer bookkeeping (notes, cooldowns, treats, effects)
```

Success (issue #67, "Done when"):

1. One documented policy governs competing built-in behavior, with stable tie-breaking and bounded
   deferred work.
2. Producers submit semantic intents and never inspect unrelated modules or animation names to
   decide whether they may run.
3. Session aggregate priority is unchanged; urgent activity interrupts discretionary reactions.
4. Activity updates during drag or fall resolve to the latest activity after release or landing.
5. A one-shot returns to the current activity; obsolete, duplicate and expired reactions never replay.
6. Missing mappings, failed presentation, stale callbacks and timeouts cannot leave arbitration stuck.
7. Shutdown completes even when its presentation fails or never reports.
8. Reminder notes, cooldowns and the snack/water effect follow documented admission/outcome rules.

## Constraints

- `Sessions` keeps ranking sessions (attention > exhausted > error > turn-finished > working >
  reading > thinking > idle, plus waiting and inactive). The runtime receives the aggregate as one
  intent (persistent, or a moment for cues of the "once" shape) and never ranks sessions again.
- `hook`, protocol v1, session aggregation, alert delivery (`AlertQueue`, the bubble, the tray badge)
  and the notification system are untouched. An attention bubble still appears however the pet is
  animated.
- `Player` keeps playback phases, held states and its "latest request plays after release" rule.
  Movement physics, geometry and native window operations stay in `PetWindow`.
- The runtime is Qt Core only, has no timers of its own (the monitor's 250 ms tick drives `tick()`),
  reads time from an injected clock, and needs no desktop, `Player` or artwork in tests.
- No unbounded queue: at most `maxDeferred` (4) waiting one-shots, at most one per identity.
- The developer surfaces (the States menu, Space, the preview dialog and `--state`) keep selecting
  `Player` states directly. They are tools, not behaviors.

## Today: the cross-module guards this replaces

| Where | Guard | Replaced by |
| --- | --- | --- |
| `Monitor::update` | returns while `window_.quitting()` | Shutdown rejects every later submission (rule 1) |
| `Monitor::update` | leaves an idle pet alone while `window_.resting()` (ambient fidget or nap, or a touch state requested) | rests are persistent Ambient intents shown while activity is idle-like (rule 9); petting is handling (rule 2) |
| `Monitor::update` | yields to `eggs().surprising()` unless urgent | rank and interruption (rules 3, 4) |
| `Monitor::update` | `!player.held()`, the `lastAggregate_` dance, and never re-selecting `error` or `turn-finished` | handling withholds presentation and resolves once on release (rule 2); moments are presented at most once (rule 12) |
| `Monitor::update` | selects `mood().celebrate(…)`'s state instead of playing `turn-finished` | `Mood` still picks one celebration cue; `Monitor` submits it as a Celebration, which takes the moment's place (rule 12) |
| `Monitor::update` | `celebration.cue == "snack"` marks water given at selection | the snack intent carries the `water` effect, applied on Started (rule 11) |
| `Monitor::calm` | attention, bubble/note, held, surprising, walking, flying | the Reminder admission condition over `Context` (rule 7) |
| `Monitor::remindWellness` | quiet hours mark the reminder given | unchanged: quiet hours are the wellness schedule's, so `Monitor` lets the reminder go before submitting it |
| `Monitor::apply` | no danger surprise while urgent or hidden | Surprise is Rejected while urgent, handled or hidden (rule 3) |
| `EasterEggs::surprise` | refuses while `held()` or `stopped()`; draws from `pool(cue)` and selects itself | Surprise admission; `Stage` draws from the pool |
| `EasterEggs::surprising` | infers the surprise's lifetime from `requestedState()` and a 15 s clock | outcomes correlated by instance, with `oneShotTimeoutMs` (rule 8) |
| `Ambient::looped` | `held()`, `stopped()`, plays fidgets and `nap` itself | Ambient admission; Ambient keeps its clocks and draws, then submits |
| `PetWindow` slide end, `letGo` | hides at the edge only if `player_.requested("idle")` | the edge is a persistent Ambient intent, refused unless idle-like |
| `PetWindow::playQuitAnimation` | `play` failure, `quitCue_` matched against `completed`, a 5 s timer per step | Shutdown's Unavailable/TimedOut outcomes and `shutdownTimeoutMs` (rule 1) |

## Ownership

| Layer | Owns |
| --- | --- |
| Producers (`Monitor`, `PetWindow` lifecycle and touch, `Mood`, `EasterEggs`, `Wellness`, `Ambient`) | triggers, counters, schedules, which cue and policy class to submit, bookkeeping on outcomes |
| `behavior::Runtime` | admission, rank, interruption, deferral and its bound, expiry, timeouts, returning to the current activity, the shutdown guarantee |
| `Stage` (new adapter in `pet_ui`) | turns a `Request` into `Player::play(cue)` or a draw from `Player::pool(cue)`; turns `Player` signals into `Feedback` for that request's instance |
| #64 cue mapping (`Catalog`, `cues.json`) | cue → state or reaction pool for the active pet; every state cue is guaranteed to play, a reaction cue without a pool plays nothing |
| `Player` | phases, frames, held states and their release, presentation signals |
| `PetWindow` / platform | pointer handling, flight, walking and sliding physics, geometry, native windows |

`PetWindow` owns the `Stage`, which owns the `Runtime`, as it owns `Player` today; `Monitor` reaches
them through `window_.stage()`. `Stage` sets `handled` from `Player::heldChanged`; `PetWindow` sets
`visible` and `moving` when it hides, shows or moves the pet; `Monitor` sets `present`, `muted`,
`speaking` and `attention`, and refreshes `visible` and `moving` on each tick.

## Contract

The full declarations are in `src/behavior/runtime.h`. In short:

- **`Intent`**: `source` + `key` is the identity (deduplication, withdrawal); `cue` is a cue from
  `cues.json`, never an animation name (Ambient and touch intents may name a fidget, move or edge
  state instead, since those catalog sections belong to each pet, per ADR 0029); `policy` is one of
  the central classes below; `lifetime` is `Persistent`, `Moment` (session activity of the "once"
  shape: `turn-finished`, `error`) or `OneShot`; `expiresAt` is how long a one-shot may wait (0: now
  or never); `effects` are semantic side effects reported with Started.
- **`submit()`** answers at once: `Admitted`, `Deferred`, `Duplicate` or `Rejected`.
- **`outcome`** reports each instance's history: at most one `Admitted`, then optionally `Started`,
  then exactly one terminal outcome (`Completed`, `Interrupted`, `Unavailable`, `TimedOut`), or
  `Expired`/`Dropped` without ever being admitted.
- **`present`** hands presentation a `Request {instance, cue, interrupt}`; **`report(instance,
  feedback)`** answers it, possibly before `present` returns (an optional cue without a pool). A report
  for anything but the instance holding presentation is stale and ignored.
- **`setContext()`** and **`tick()`** re-evaluate: a waiting reminder starts once things are calm, and
  timeouts and expiry are enforced.
- **`finished`** is called exactly once when shutdown is done.

## Policy classes

Producers choose a class, never a number. Rank, from highest:

| Rank | Class | Cues | Lifetime | Admitted when | May wait | Interrupt flag |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Shutdown | `quit`, `annoyed` (then `quit-angry`) | one-shot, `shutdownTimeoutMs` | always, even while handled | — | yes |
| 2 | Urgent | `attention`, `exhausted`, `error` | persistent; `error` a moment | always; presented after handling | — | yes |
| 3 | Surprise | `danger`, `konami`, `reminder-done` | one-shot | visible, not handled, not urgent | no (refused) | yes |
| 4 | Reminder | `eye-break`, `water`, `monday`, `leave-work`, `sleep` | one-shot | calm (below) | yes, until `expiresAt` | yes |
| 5 | Celebration | `celebrate`, `snack`, `milestone`, `long-turn`, `birthday`, `friday-evening` | one-shot | visible, not urgent; waits while handled | yes, until `expiresAt` or the activity changes | no |
| 6 | Startup | `start` | one-shot, ended by any session activity | always | — | yes |
| 7 | Activity | `working`, `reading`, `thinking`, `turn-finished`, `waiting`, `idle`, `inactive` | persistent; `turn-finished` a moment | always | — | no |
| 8 | Ambient | occasion fidgets (`may20`, `birthday`, `late-night`), the pet's fidgets and moves; `nap` and the pet's edge states as rests | one-shot or persistent rest | visible, not handled, not urgent, activity idle-like | no | no |

"Calm" (Reminder admission): the user is present, the pet is visible, not muted, no bubble or note
is showing, no session waits on the user, the pet is not handled or moving, and the activity is not
urgent. "Idle-like" activity: `idle`, `waiting`, or none yet.

The interrupt flag is `Player::play`'s: cut the current state instead of letting it finish its
exit. Returning to the activity after a one-shot never interrupts.

Urgency now lives here: `Monitor::urgent()` (ADR 0029 kept it on the event side) becomes the choice
between the Urgent and Activity classes.

## Rules

1. **Shutdown.** It interrupts the showing one-shot, drops every waiting one, ends any rest and is
   presented even while the pet is handled (`PetWindow` ends the drag first, as today). Every later
   submission is `Rejected`, except the same identity, which is a `Duplicate` (a second Quit, or
   `annoyed` after `quit`). `finished` fires exactly once: on Completed, on Unavailable, after
   `shutdownTimeoutMs` without a terminal report, at once when the pet is hidden (no request is
   made), and also when nobody presents at all (the request times out).
2. **Handling.** While `handled` (dragged, petted while held, falling), the runtime makes no requests
   other than Shutdown. Activity changes are recorded; when handling ends it presents once, for
   whatever is current then. Intermediate activities, urgent ones included, are not replayed. Surprises
   and Ambient are refused, Reminders and Celebrations may wait. `Player`'s own hold is unchanged: the
   runtime simply does not ask it for anything meanwhile.
3. **Urgent activity** interrupts a showing Surprise, Reminder, Celebration, Startup or Ambient
   one-shot, drops waiting Surprises, Celebrations and Ambient, ends a rest, and refuses new
   Surprises, Celebrations and Ambient. Reminders wait. Any session activity, urgent or not, ends
   Startup.
4. **Preemption.** A one-shot that may run is admitted at once if nothing shows or it strictly
   outranks what shows; the shown one is Interrupted. Equal or lower rank waits if its class and
   `expiresAt` allow it, and is Rejected otherwise. Equal rank never interrupts.
5. **Order of waiting work.** When presentation frees up (a terminal outcome, a context change, a
   timeout), the runtime admits the first waiting one-shot that may run, by rank and then by
   submission order (a sequence counter, so equal timestamps stay deterministic). Otherwise it
   presents the current rest or activity.
6. **Activity during a one-shot.** A non-urgent activity change is recorded and shown when the
   one-shot ends, without interrupt; earlier changes are obsolete and never shown. A Celebration still
   waiting when the activity changes is Dropped: the turn it celebrated is old news.
7. **Reminders** are admitted only when calm. Admission is when the producer shows the note, so art
   and note are admitted together and a reminder that never gets admitted shows no note.
8. **Feedback and timeouts.** Each request has a fresh instance number. Started applies effects;
   Completed, Interrupted and Unavailable end the instance and free presentation. A one-shot whose
   presentation reports no terminal outcome within `oneShotTimeoutMs` (15 s, today's
   `EasterEggs::surpriseMs`) is TimedOut. An optional cue the pet has no art for reports Unavailable
   and frees presentation at once.
9. **Rests.** A nap or a hide at the screen edge is a persistent Ambient intent: shown while the
   activity is idle-like and nothing else shows, Interrupted by any other activity and never
   restored when the activity returns to idle. Withdrawing it (the pet dragged out of hiding) returns
   to the activity.
10. **Deduplication and bounds.** A submission whose identity is showing or waiting is a Duplicate and
    restarts nothing; the same persistent activity resubmitted every tick is a Duplicate. At most
    `maxDeferred` one-shots wait; beyond that a new one is Rejected.
11. **Effects** apply on Started only, so a refused, dropped or unavailable reaction never counts as
    shown.
12. **Moments.** Session activity of the "once" shape (`turn-finished`, `error`) is presented at most
    once per submission. Coming back to it after a reaction or a hold shows nothing new: `Player`
    already went on to the cue's `after` state by itself, as `Monitor`'s `turn-finished`/`error`
    exception does today. A Celebration stands in for the moment it celebrates: `Monitor` submits the
    `turn-finished` moment and then `Mood`'s celebration in the same tick, and since `Player` replaces a
    "once" state at once, only the celebration shows. A Celebration that waited (behind a surprise)
    and then starts marks the still-unshown moment as shown; one that ends Unavailable without
    starting leaves the moment to show.

## Bookkeeping on outcomes

Producers keep their counters and schedules and update them from outcomes, never from animation
names:

| Producer | Opportunity consumed (cooldown starts) on | Otherwise |
| --- | --- | --- |
| Wellness reminder (`eye-break`, `water`) | Admitted (the note is shown, with or without art); let go in quiet hours before submitting, as today | Expired: still due, resubmitted on the next tick (one identity, so bounded); withdrawn once no longer due (taken, turned off, a lock) |
| Clock reminder (`monday`, `leave-work`, `sleep`) | Admitted (once a day) | waits within its window (`expiresAt` = window end); Expired: not given today, as when the pet was not running |
| Mood treat (`snack`, `milestone`) | Admitted (`Mood::celebrate()` already reports the cue it drew from) | Rejected, Dropped, Expired: the treat stays pending for the next finished turn, as a passed-over snack does today |
| Birthday cheer | Admitted (once a day) | not consumed |
| Snack's `water` effect | Started: `Wellness::given("water")` | nothing |
| `reminder-done`, `danger`, `konami` | nothing to consume | — |

`EasterEggs::reminder()` and `Mood::celebrate()` split into "what is due" and "mark it given", so the
mark waits for the outcome. `Mood` keeps choosing exactly one celebration cue per finished turn
(a milestone, else the occasion `EasterEggs::celebration()` names, else a snack, else `celebrate`); the
runtime never shows two. The bedtime note and the recap note are passing remarks with no art: they
stay with `Monitor` and are not intents.

## Presentation adapter (`Stage`)

`Stage` owns the runtime and drives `Player` through #64's mapping:

- It plays the producer's drawn state, else the state a state cue maps to (`Player::stateFor`), else a
  draw from the reaction cue's pool, else one of the pet's own states (a fidget, a move, an edge).
- Unavailable, reported before `present` returns: nothing to play, as for a reaction cue with an empty
  pool, or a selection that fails.
- Started: `Player::entered` for the state it selected (or at once when that state already shows).
  Completed: `Player::completed` for it, which `Player` now also emits when a phased state's end phase
  finishes. Interrupted: any other state entered, unless `Player::finishing()` says the state is ending
  by itself into the next one.
- An interruption is reported at once, so `EasterEggs::surprising()` and the like are right
  immediately; whatever the runtime shows next is selected only after the player has finished entering
  the interrupting state, since `entered` listeners must not select.
- `Player::heldChanged` sets the runtime's `handled`, so drags, petting and falls hold presentation the
  moment they start.
- A request whose state is idle leaves the pet's own idle decoration playing (a fidget, a walk, a touch
  reaction such as an edge), wherever it came from, as `Monitor`'s `resting()` check did.
- `Stage` remembers only the latest request's instance, so signals about an earlier one are never
  reported against its replacement; the runtime ignores stale instances as a second line of defense.
- `annoyed` is one request with steps: the `annoyed` state cue, a 2.25 s pause to read the remark,
  then `quit-angry` (straight to it when `annoyed` cannot play). `Stage` reports Completed after the
  last step; `shutdownTimeoutMs` (10 s) covers them all, and `PetWindow` ticks the runtime while
  quitting because the monitor has stopped.
- The runtime's clock is `EasterEggs::now()`, the pet's local clock, so tests that move it move the
  runtime too.

## Behavior kept, and intentional differences

Kept: session priority; attention interrupting surprises; surprises playing out over non-urgent
activity; celebrations waiting behind a surprise; reminders waiting for calm; quiet hours letting
reminders go; the note-only reminder when the pet has no art; drag and fall holding playback and
showing the latest activity afterwards; a hidden pet quitting at once; edge hiding only when idle;
one celebration per finished turn, shown instead of the `turn-finished` state; `turn-finished` and
`error` never replayed.

Also kept: the first session activity, a surprise or a reminder still ends the arrival (`start`) at once.

Intentional differences:

1. **Clock reminders wait for calm.** Today `monday`, `leave-work` and `sleep` play at once whatever
   is happening, and muting drops only the note. As Reminders they wait (within their window) until
   the user is present and nothing else is being said, and muting holds them like wellness reminders.
2. **Snack/water counts on Started**, not on selection: a snack that never played no longer cancels
   the water reminder.
3. **One shutdown bound** of 10 s replaces two 5 s timers (one per quit step).
4. **Reactions are refused while hidden.** Today a celebration or surprise could be selected on a
   paused, hidden pet. Treats are therefore kept for a later turn instead of being spent unseen.

## Migration

Each step keeps every existing suite green and removes the guards it covers.

1. **Runtime and Stage.** `src/behavior/runtime.cpp`, a `pet_behavior` library (Qt Core), linked by
   `pet_ui`; register `behavior` with CTest in place of `behavior-tests-spec`. `Stage` over
   `Player::play`/`pool`. Nothing submits yet.
2. **Session activity and lifecycle.** `Monitor::update` submits the aggregate (Urgent or Activity,
   a moment for `turn-finished` and `error`) followed by `Mood`'s celebration; `PetWindow` submits
   `start` and `quit`/`annoyed` and quits on `finished`. Remove the `quitting()` check, the
   `held()`/`lastAggregate_` re-select logic, `quitTimer_`, `quitCue_` and `playQuitAnimation`'s chain.
3. **Handling, rests and wander.** `PetWindow` sets `handled` and `moving`; edge hiding becomes a rest
   intent from `touch`, naps one from `Ambient`; `Ambient` submits fidgets and moves (its move gate
   stays for geometry). Remove `PetWindow::resting()` and the `requested("idle")` checks in
   `PetWindow`.
4. **Surprises and treats by outcome.** `EasterEggs` submits Surprises; `Mood` and `EasterEggs` keep
   their counters by outcome; `EasterEggs::surprise`/`surprising` and the `celebration.cue == "snack"`
   check go.
5. **Reminders.** `Monitor` sets `present`, `muted`, `speaking` and `attention`; wellness and
   clock reminders become Reminder intents and show their notes on Admitted. `Monitor::calm` goes.
6. **Docs.** ADR 0031 (behavior runtime), `architecture.md` code map, `pets.md` (cues a pet answers
   are arbitrated, not guaranteed to play), and `prototype-tests` cases for the wiring.

## How #43 joins

Custom events and data-driven rules submit ordinary intents with a class from this table, through
the same `submit()`. A plugin may not use Shutdown, Urgent or Startup, so it cannot mask attention or
claim lifecycle priority; the loader assigns or clamps the class. #43's phase 3 can later describe
built-in triggers as data without changing these rules.

## Tests

`tests/behavior_tests.cpp` drives a `Runtime` with a hand-set clock and a recording fake
presentation. Coverage against the issue's list:

| Issue case | Tests |
| --- | --- |
| attention versus surprise | `urgentActivityInterruptsASurprise`, `urgentActivityKeepsDiscretionaryReactionsAway` |
| activity changes during handling | `handlingHoldsPresentationUntilRelease`, `handlingRefusesSurprisesAndHoldsReminders`, `shutdownTakesOverWhileHandled` |
| equal-priority ties | `equalOrLowerRankWaitsOrIsRejected`, `waitingIntentsGoByRankThenSubmissionOrder` |
| expiry and deduplication | `waitingIntentsExpire`, `sameIdentityIsNotRestartedOrQueuedTwice`, `activityIsPresentedOnceAndRepeatsAreDuplicates`, `waitingIsBounded` |
| obsolete reactions | `activityChangesDuringAReactionShowTheLatestAfterIt`, `obsoleteCelebrationIsDroppedWhenActivityMovesOn`, `restEndsWithActivityAndIsNotReplayed` |
| moments (`turn-finished`, `error`) | `celebrationTakesTheFinishedTurnsPlace`, `momentDuringAReactionShowsAfterIt`, `waitingCelebrationStandsInForTheMoment`, `unavailableCelebrationLeavesTheMomentToShow`, `momentIsShownOnceAcrossHandling` |
| missing optional mappings | `unavailableOptionalCueReleasesItsClaim`, `reminderKeepsItsNoteWhenTheArtIsMissing`, `presentationMayAnswerBeforeItReturns` |
| stale feedback, timeouts | `staleFeedbackCannotFinishTheReplacement`, `silentPresentationTimesOut` |
| shutdown failure | `shutdownCancelsEverythingAndFinishesOnce`, `shutdownFinishesWhenItsCueIsUnavailable`, `shutdownFinishesWithoutFeedback`, `shutdownOfAHiddenPetFinishesAtOnce`, `shutdownFinishesWithoutAPresentation` |
| reminder notes and effects | `reminderWaitsForCalm` (7 rows), `reminderThatNeverGetsItsTurnShowsNoNote`, `effectsApplyOnlyWhenTheReactionStarts` |
| presentation leaving on its own | `activityLeftByPresentationIsShownAgainOnTick`, `momentLeftByPresentationIsNotReplayed`, `restLeftByPresentationEnds` |
| other policy | `higherRankInterruptsALowerReaction`, `reminderInterruptsAFidget`, `ambientRunsOnlyWhenIdleAndInView`, `ambientNeverInterruptsAReaction`, `withdrawnRestReturnsToActivity`, `startupEndsWithTheFirstSessionActivity`, `reactionsCutTheArrivalShort`, `startupCompletesIntoTheActivity`, `admitsAsksWithoutSubmitting`, `showingNamesTheReactionOnScreen`, `drawnStateIsHandedToPresentation` |

`prototype-tests` covers the wiring: `stagePresentsTheRuntime` (entry, end, outside interruption, a
missing pool, handling, idle decoration, the annoyed chain on VPet), and the existing `Monitor`,
`PetWindow`, `Ambient`, `EasterEggs` and `Mood` tests, which now run through the runtime. A case in
`monitorWellnessReminders` checks that a reminder waiting through a locked screen does not come back
after it.

## Settled questions

1. The intentional differences above are in the build and listed in ADR 0031; the arrival keeps its
   old behavior instead.
2. A waiting Reminder is admitted over a rest (a nap) at once: the user is present, so the pet wakes
   up to remind them.
3. `Context` writers run on every monitor tick, plus `handled` the moment the player is held or let go
   and `visible` when the pet is hidden or shown.
