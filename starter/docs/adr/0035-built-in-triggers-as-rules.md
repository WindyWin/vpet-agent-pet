# 0035. The pet's own triggers as rules

- Status: Accepted
- Date: 2026-10-09

## Context

Issue #43 phase 3. [0034](0034-custom-events.md) gave plugin packs a rule table (`events.json`) for custom
and agent events, but the pet's own moments still had a separate path. Each one read the catalog in
code: `EasterEggs` drew `danger`, `konami`, `reminder-done` and the special-day fidgets from
`Player::pool`, and checked pools to choose a celebration; `Mood::celebrate` drew `celebrate`, `snack` and
`milestone`; `Stage` drew a reminder's art and held a special case to let `lunch` borrow `snack` or
`celebrate`. A pack could add entries to those pools ([0033](0033-plugin-packs.md)), but it could not
answer a moment the pet has no pool for, add a remark, or choose one of the pet's other pools. So
built-in and plugin behavior worked in two different ways.

## Decision

- **Triggers are named in data.** `src/animation/triggers.json`, embedded like `cues.json`, lists the
  pet's own triggers: `danger`, `konami`, `reminder-done` (surprises); `celebrate`, `snack`, `milestone`,
  `long-turn`, `birthday`, `friday-evening` (celebrations); `may20`, `birthday-greeting`, `late-night`
  (fidgets); `monday`, `lunch`, `leave-work`, `sleep`, `eye-break`, `water` (reminders). Each one has a
  class and the cues its built-in rule draws from, the first one the pet has a pool for (`lunch`:
  `lunch`, `snack`, `celebrate`). The birthday's fidget greeting and its cheer are different moments, so
  the greeting is the trigger `birthday-greeting` on the same cue.
- **One rule table per pet.** `EventRules(pools)` starts with the pet's own rules: one rule (no pack,
  weight 1) for each trigger whose cues the catalog answers. The packs' rules follow, in pack id order.
  The `Player` holds the table, because everything that raises a trigger already shares it. The library's
  player takes the library's pack rules (`Player::setRules`), and `Monitor` reads the same table, so
  `Monitor::setRules` and the `main.cpp` wiring are gone.
- **Code raises triggers, and the table chooses what plays.** `EasterEggs` (surprises, fidgets, which
  celebration applies), `Mood::celebrate` and `Stage` (a reminder's art) ask `answers(trigger)` and
  `draw(trigger)`. None of them read a pool. A drawn rule gives a state (its own, or one drawn from its
  cue's pool) and possibly a remark. With only the pet's own rule there is nothing to draw between, so
  VPet with no pack draws the same states with the same random numbers as before.
- **The trigger keeps its class, timing and words.** A pack's rule changes only what plays: a `danger`
  rule still plays as a Surprise from `eggs`, a `milestone` rule as a Celebration from `mood` in the
  finished turn's place, a fidget rule as an ambient fidget, and a reminder rule as the reminder's art.
  The easter eggs switch, the reminder schedule, the treat counters and the outcome bookkeeping stay in
  code. So the attention guarantee and the policy table of [0031](0031-behavior-runtime.md) still hold:
  no class a pack could not already reach.
- **Pack rules for triggers** (`plugins::readRules`): `on` may name a trigger. Such a rule needs a `state` or
  a `cue`. It may `say` only for surprises and celebrations, because fidgets are silent and a reminder's
  note is the pet's own translated text. It takes no `cooldown_ms`, and the rate limits do not apply: the
  pet paces its own triggers, and a noisy script must not use up the limits a milestone needs.
- **Rules can name a cue.** Any rule, for an event or a trigger, may give `cue` instead of `state`: a
  reaction cue that the merged catalog has a pool for (checked at load; pools are added to or replaced but
  never emptied, so a later pack cannot remove one). A pack without art can make the pet celebrate a
  deploy with its own `celebrate` pool.
- **Remarks travel with the intent.** `behavior::Intent` gains `remark`, which the runtime never reads.
  `Monitor::outcome` says it on `Admitted` unless the pet is muted, whatever the source. This replaces
  phase 2's `remarks_` map, so the remark of a celebration that waits is said when it plays.
- **Out of scope:** the bedtime, reminder and recap notes and the touch reactions stay in code (they are
  words and handling, not art); a rule cannot remove the pet's own reaction (an override of the pool does
  that); conditions on rules; hot reload.

## Consequences

- One mechanism answers both built-in and plugin moments, and a pack can wire art to any of them without
  code: a startle for a pet without one, a cake on the hundredth turn, a different lunch.
- A pack's rule competes with the pet's own by weight. Weight 1 plays it about half the time, and 1000 almost
  always. Replacing the art for good still goes through an `overrides` entry on the pool.
- Adding a trigger means a line in `triggers.json`, a cue in `cues.json` and the code that raises it.
  `plugin_tests` checks that every reaction cue except `plugin-event` belongs to a trigger.
- `Mood::Celebration` gains `say`. `EasterEggs::surprise` takes a trigger name. `Player::rules()` replaces
  the `Monitor`'s table. Pool names in code are now trigger names.

## Validation

### Built-in trigger evidence — 2026-10-09

- Ubuntu 24.04, Qt 6.4.2 (the CI's Qt 6.5.3 could not be downloaded in that environment; the version check and
  the `QDataStream::Qt_6_5` uses were relaxed in a local copy, not in the tree), GCC 13, CMake 3.28:
  `ctest --test-dir build` passed all 16 tests. `prototype-1` to `prototype-4`, which script every random draw
  of celebrations, treats, easter eggs and reminders against VPet, pass unchanged.
- `plugins` adds `triggersMatchTheCueVocabulary` (the file's classes and keys, sorted names, every cue a
  reaction cue, every reaction cue but `plugin-event` reached), `thePetsOwnRulesFollowItsPools` (the test pet's
  `lunch` falls back to `celebrate`; on VPet every trigger draws exactly what its pool would, for the same
  numbers), `packsAnswerThePetsTriggers` (pack rules join after the pet's own and compete by weight, cue rules
  draw from the pet's pool, a trigger without art of its own), `stageDrawsRemindersFromTheRuleTable`, and
  `packRulesAnswerThePetsOwnTriggers` (a pack's `danger` rule plays as the eggs' Surprise with its remark, a
  `celebrate` rule as the mood's Celebration with its remark, the pet's own rule says nothing, and turning the
  easter eggs off still stops it). `aWrongRuleRejectsItsPack` covers the new rejections, and the example pack
  answers `konami` on VPet.
