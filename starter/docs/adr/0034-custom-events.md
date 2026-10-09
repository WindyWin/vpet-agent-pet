# 0034. Custom events and plugin event rules

- Status: Accepted
- Date: 2026-10-08

## Context

[0033](0033-plugin-packs.md) let a pack add animations that play through the pet's existing cues. Issue #43
phase 2 asks for the other half: scripts, CI jobs and other agents should be able to tell the pet that
something happened (`deploy_succeeded`, `tests_failed`), and a pack should be able to say what the pet does
about it, with no code. Protocol v1 had a closed set of kinds that all feed `Sessions`, so there was no way to
send a one-off "something happened". The pet must not let a script mask a session that needs the user, nor
flood the pet with reactions, and the `hook` path must stay fast, silent and content-free.

## Decision

- **Protocol: version 1 plus one kind.** `custom` has the provider `custom` (and only custom events do),
  a required `name` (`[a-z0-9_-]{1,64}`) and the identity every event has (`session_id` as the sender's
  label, `event_id`, `timestamp_ms`). Other optional fields are rejected. The envelope, limits and
  unknown-field rule are unchanged, so no version bump is needed: an older pet drops the unknown kind
  (`emit` only learns that the datagram was delivered, as for any event). `Event` gains `name`; `validEventName` is a header both the parser
  and the rule loader use.
- **A reaction, not session state.** `Monitor::apply` takes a custom event before `Sessions`, and
  `Sessions::apply` refuses one. It creates no session, alert or badge, and leaves the aggregate, the recap and
  the wellness timers alone. It has an effect only through a rule.
- **Only `emit` sends them.** `agent-pet emit --custom NAME` builds the event without input; `--provider custom`
  with JSON on stdin works too. `hook` rejects the provider `custom`, so the hook path is unchanged.
- **Rules live in the pack.** An optional `events.json` holds up to 128 rules `{on, state?, say?, weight?,
  cooldown_ms?}`. `on` is `custom:<name>` or one of `session_start`, `prompt`, `attention`, `error`,
  `turn_finished`, `turn_failed`, `interrupt`, `session_end` (tool events are too frequent). A rule names a
  state, a remark, or both. Its state must end by itself and return to idle, like a reaction pool's entries,
  and is checked against the merged catalog, the pack's own states included. `plugins::apply` reads the rules
  after the catalog builds and meets the contract, so a wrong rule rejects the pack as a whole, with the
  reason in Settings and the log. Rules join an `EventRules` table in pack id order; `PetLibrary` keeps it
  and `main.cpp` hands it to the `Monitor`.
- **Policy: Surprise.** A reaction is submitted as the intent `plugin/<trigger>` with the new reaction cue
  `plugin-event`, the class `Surprise` and the rule's state, and its remark is said when the runtime admits
  it (not while muted). The loader cannot grant Shutdown, Urgent or Startup, as [0031](0031-behavior-runtime.md)
  promised: a reaction is kept away while a session needs the user, is out of quota or just failed, while the pet
  is held or hidden, and while another surprise plays, and is then dropped, never queued. So a custom event can
  never mask attention or pile up.
- **Rate limits** (`EventRules`, time from the caller so tests drive it): after a reaction plays, its trigger
  rests for the rule's `cooldown_ms` (10 s by default, 1 s to 1 h); at most 12 reactions play a minute across all
  triggers; an event more than a minute from the pet's clock is ignored. Choosing a reaction changes nothing
  (`pick`); only one that was admitted (`commit`) rests its trigger, so a held-off event does not spend it.
  When several rules match, one is drawn by weight.
- **Agent events** match after the sessions accept them, so a duplicate, a stale callback or a
  `turn_finished` that only waits on background work is not an occurrence. Rules for agent events and custom
  names are separate: a custom event never matches `turn_finished`.
- **Out of scope here:** a toast through the tray, sound, conditions on rules (project, day), custom events
  without a pack, hot reload. Phase 3 (the built-in triggers as rules) stays open.

## Consequences

- A CI script can make the pet dance or droop with one command, and a pack author needs only JSON and PNG.
- The attention guarantee rests on the runtime's policy table rather than on a check of its own, so it
  cannot drift from the rest of the pet.
- A pack's remark is text from the pack in the pack's language and is not translated; it is plain text of at
  most 120 characters.
- Any process of the same user can send a custom event, as it can any event (the socket trusts the user);
  the rate limits and the Surprise class bound what it can do to a reaction a pack opted into.
- The vocabulary gains `plugin-event`, which no catalog maps: the rule supplies the state. Pets and
  packs need no change.

## Validation

### Custom event evidence — 2026-10-08

- Ubuntu 24.04, Qt 6.4.2 (the CI's Qt 6.5.3 could not be downloaded in that environment; the version check and
  the `QDataStream::Qt_6_5` uses were relaxed in a local copy, not in the tree), GCC 13, CMake 3.28:
  `ctest --test-dir build` passed all 16 tests.
- `events` covers the wire format (valid names and limits; provider/kind/name must go together; every
  agent-only field rejected), `Sessions` refusing a custom event and attention staying the aggregate, and the
  CLI (`emit --custom`, `--provider custom` on stdin, a bad name, a mismatched provider, and `hook` never sending
  one) against a real receiver.
- `plugins` adds the rule file (every rejection and its reason, a wrong rule dropping the animations with it,
  rules following their pack in id order, the example pack's rules against VPet), the rate limits with
  scripted draws and clocks, and the `Monitor`: a custom event plays the pack's state and says its remark and
  leaves the sessions, recap and badge alone; the same event right away, a stale one and a held-off one do not
  react or spend the cooldown; an attention request keeps its state and the event reacts once it is answered; an
  agent event's rule reacts, a duplicate prompt, a tool event and a resting trigger do not.
