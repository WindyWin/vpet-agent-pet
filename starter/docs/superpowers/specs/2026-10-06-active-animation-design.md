# Active animation styles — design

Issue: #47 "Active animation preferences: varied thinking, working, and reading". Scope: the whole issue
(alternate loops, desk continuity, transition reactions, preference). Date: 2026-10-06.

## Goal

Thinking, reading and working each repeat one short loop (1.125 s, 1.375 s, 1.625 s). A new
**Active animation** preference makes them vary during a sustained state, keeps the pet at its desk for
short thinking pauses, switches book and pen without leaving the desk, and adds small reactions to real
activity transitions. The animation state (`thinking`, `reading`, `working`) always keeps its meaning:
decoration changes only which sequence plays inside it, never the state itself.

## Styles

| Style | Alternate loops | Opportunity gap | Desk continuity (handover, linger) | Reactions (`enter`, `exit`) |
|---|---|---|---|---|
| Classic (0) | none | — | off | off |
| Subtle (1) | `subtle` tier | 10–18 s | on | off |
| Playful (2, **default**) | `subtle` + `playful` tiers | 6–12 s | on | on |

Classic plays exactly today's sequences. The setting is independent of **Idle animation**: Idle Off does
not turn Active off, and Active Classic does not affect idle fidgets.

## Artwork

Survey of the full upstream archive (see "Asset review" below for verdicts). Imported with
`scripts/add_sequences.py` (all `Nomal` unless noted):

- `Think/Nomal/B_2`, `B_3`, `B_4`, `B_5`; `Think/Happy/B_3`, `B_4`, `C_2`
- `WORK/Study/B_2_Nomal`, `B_3_Nomal`, `B_4_Nomal`
- `WORK/WorkONE/B_2_Nomal`, `WORK/WorkONE/Happy/B`, `WORK/WorkONE/B/Nomal/1` (pen spin)

Composite sequences (catalog entries only; they reuse frames already in the manifest, no new PNGs):

| Sequence id | Frames | Duration |
|---|---|---|
| `WORK/Desk/reading_to_working` | `Study/A_Nomal` 11→6 (reversed), then `WorkONE/A_Nomal` 6–9 | 10 × 125 ms |
| `WORK/Desk/working_to_reading` | `WorkONE/A_Nomal` 9→6 (reversed), then `Study/A_Nomal` 6–11 | 10 × 125 ms |
| `WORK/Desk/ponder_in` | `WorkONE/B/Nomal/1` frames 0–3 | 500 ms |
| `WORK/Desk/ponder_loop` | `WorkONE/B/Nomal/1` frames 4–11 (frame 12 equals frame 4) | 1000 ms |
| `WORK/Desk/ponder_out` | `WorkONE/B/Nomal/1` frames 34–37 (frames 3→0 reversed) | 500 ms |

The handovers swap props while book and paper are raised, with the desk level throughout (seams measured
at 0.015–0.07 RMSE, inside the base loops' own 0.02–0.04 frame-to-frame range; the cross-prop swap is
0.124). Rejected: `WORK/WorkTWO`, `PlayONE`, `StudyTWO`, `Calligraphy` (other props), `RemoveObject`
(ink splats read as failure), `Switch/*`, `LevelUP`, `Say/*`, `State/*` (imply success, failure or
talking), `WorkONE/B/Nomal/2` (eyes closed, reads as dozing), PoorCondition art (mood work, later), and
`WorkONE/Happy/C` (frames out of order). The archive has no lightbulb or surprise art; reactions stay mild.

## Catalog: `activity` section

```json
"activity": {
  "thinking": {
    "loops": [
      {"sequence": "Think/Nomal/B_2", "weight": 2},
      {"sequence": "Think/Nomal/B_3", "weight": 2},
      {"sequence": "Think/Nomal/B_4", "weight": 1},
      {"sequence": "Think/Nomal/B_5", "weight": 1},
      {"sequence": "Think/Happy/B_3", "weight": 1, "style": "playful"},
      {"sequence": "Think/Happy/B_4", "weight": 1, "style": "playful"}
    ],
    "exit": {
      "reading": [{"sequence": "Think/Happy/C_2", "weight": 1}],
      "working": [{"sequence": "Think/Happy/C_2", "weight": 1}]
    }
  },
  "reading": {
    "loops": [
      {"sequence": "WORK/Study/B_2_Nomal", "weight": 2},
      {"sequence": "WORK/Study/B_3_Nomal", "weight": 1}
    ],
    "linger": {"to": "thinking", "max_s": 8, "loop": ["WORK/Study/B_4_Nomal", "WORK/Study/B_3_Nomal"]},
    "handover": {"working": "WORK/Desk/reading_to_working"}
  },
  "working": {
    "loops": [
      {"sequence": "WORK/WorkONE/B_2_Nomal", "weight": 2},
      {"sequence": "WORK/WorkONE/Happy/B", "weight": 1, "style": "playful"},
      {"sequence": "WORK/WorkONE/B/Nomal/1", "weight": 1, "style": "playful"}
    ],
    "enter": {"from": ["thinking"], "choices": [{"sequence": "WORK/WorkONE/Happy/B", "weight": 1}]},
    "linger": {"to": "thinking", "max_s": 8, "in": "WORK/Desk/ponder_in",
               "loop": ["WORK/Desk/ponder_loop"], "out": "WORK/Desk/ponder_out"},
    "handover": {"reading": "WORK/Desk/working_to_reading"}
  }
}
```

Rules, enforced by both `Player::load` and `scripts/verify_assets.py`:

- A key names a `phased` state without `loops` (it ends only when asked), and is not `idle`, `dragging`,
  a fidget or a touch state. Every part is optional; a state without an entry, or a catalog without the
  section, plays as Classic. That is the fallback for missing optional art.
- `loops`: non-empty list; each `sequence` is a known sequence, `weight` 1–1000, `style` is `subtle`
  (default) or `playful`.
- `enter`: `from` lists other `activity` states; `choices` is a non-empty weighted list of sequences.
- `exit`: keys are other `activity` states; values are non-empty weighted lists of sequences.
- `linger`: `to` is another `activity` state; `max_s` 1–60; `loop` a non-empty list of sequences;
  `in` and `out` optional single sequences.
- `handover`: keys are other `activity` states; values a single sequence.
- Sequence ownership in `verify_assets.py`: a sequence used only by `activity` names that state in its
  `state` field, so "every bundled sequence is used" keeps holding.

## Playback (Player)

`Player` owns the mechanics; it reads the section at load and exposes it read-only
(`const ActivityArt *activity(const QString &state)`). Two switches gate them: `setContinuity(bool)`
(handover and linger) and a reaction gate, `setReactionGate(std::function<bool()>)`, asked each time an
`enter` or `exit` could play and consuming that opportunity when it returns true. With continuity off, no
gate and nobody calling `vary`, playback is today's, byte for byte.

- **Loop passes.** `looped(state)` is now also emitted when a phased state's loop phase starts another
  pass (it was loop-mode only). `Ambient` reacts only to `idle`, so it is unaffected. Before emitting, a
  pass that played an alternate returns to the base loop sequence: an alternate lasts exactly one pass.
- **`bool vary(const QString &sequence)`**: allowed only in a phased state's loop phase at frame 0 (that
  is, from a `looped` handler) and only for a sequence listed in that state's `loops`. It swaps the pass's
  sequence and redisplays frame 0 in the same event-loop turn, so nothing is painted in between.
- **Enter reaction.** When a state with `enter` is entered from a state in `from` and the gate allows,
  its first loop pass is a draw from `choices`.
- **Exit reaction.** When a state with `exit[target]` leaves for `target` (a non-urgent select) and the
  gate allows, the end phase plays a draw from that list instead of the catalog's end sequence.
- **Handover.** With continuity on, a non-urgent select from X (loop phase, alternate pass or linger) to Y
  where X has `handover[Y]` plays that sequence and then enters Y directly in its loop phase (Y's start is
  skipped). `entered(Y)` is emitted when Y's loop begins. `phase()` reports `"handover"` meanwhile.
- **Linger.** With continuity on, a non-urgent select from X (loop phase) to `X.linger.to` keeps X and
  sets the request pending (`requestedState()` already returns it, so `Monitor` sees the request as
  honoured). It plays `in` (if any), then passes of `loop` (drawn by equal weight, avoiding an immediate
  repeat). `phase()` reports `"linger"`. Linger time is the sum of displayed frame durations, so a paused
  pet's linger does not count down. Outcomes:
  - **Expiry**: at the first linger pass boundary at or after `max_s`, plays `out` (if any), then X's end,
    then the pending state, as today.
  - **Resume**: `select(X)` plays `out` (if any), clears the request and returns to X's base loop; no end
    or start.
  - **Handover**: `select(Y)` with `handover[Y]` plays `out` (if any), then the handover.
  - **Anything else** (turn-finished, idle, a state without handover): cuts straight to X's end, exactly as
    a select from the loop phase does today. Urgent selects and `hold` cut immediately, as today.
- Nothing decorative waits for a pass to finish: every change of the requested state takes effect at
  once, as the end phase already does today.

## Pacing (Activity)

New `src/animation/activity.{h,cpp}` in `pet_ui`, modelled on `Ambient`:

```cpp
enum class ActivityStyle { Classic = 0, Subtle = 1, Playful = 2 };
class Activity : public QObject {
public:
    explicit Activity(Player &player, QObject *parent = nullptr);
    void setStyle(ActivityStyle style);   // Sets player continuity and the reaction gate.
    ActivityStyle style() const;
    static QPair<int, int> gapSeconds(ActivityStyle style); // Subtle {10, 18}, Playful {6, 12}
    void setRandom(Random random);
    void setClock(std::function<qint64()> milliseconds);
};
```

- Entering a state with an `activity` entry starts the opportunity clock if it is not running
  (`nextDue = now + gap`). Entering anything else stops it, so the next activity starts with a full gap.
- On `looped(state)` for an activity state, when `now >= nextDue` and the style is not Classic: draw an
  alternate by weight from the allowed tiers, skipping the previous alternate when another exists, call
  `player.vary`, and draw the next gap.
- The reaction gate returns true only for Playful with `now >= nextDue`, and then draws the next gap.
  Alternates and reactions share one clock, so Playful does not react to every tool call, and one
  transition gets at most one reaction (an `exit` spends the opportunity its `enter` would have used).
- `Think/Happy` and `WorkONE/Happy` art here is a Playful expression, not mood art: it plays whatever
  the mood, and the mood system keeps owning idle and fidgets only.
- `setStyle` takes effect immediately; a new pace starts from now.

## Preference and settings

- `Preferences::activity` (`enum Activity { ActivityClassic, ActivitySubtle, ActivityPlayful }`), JSON
  key `activity`, integer 0–2, default `ActivityPlayful`. A missing key uses the default; an invalid
  value marks the file invalid like the other keys.
- `PetWindow` owns an `Activity` next to `Ambient`, applies the loaded style, exposes
  `setActivityStyle(int)` / `activityStyle()`, and saves through `writePreferences`, which still re-reads
  the file first.
- Settings, **Pet** tab, below *Idle animation*: **Active animation** combo with
  "Classic (one loop per activity, as before)", "Subtle (calm variations; stays at the desk for short
  thinking pauses)", "Playful (also pen spinning and small reactions)". The tooltip says it applies while an
  agent thinks, reads or works, is independent of Idle animation, and never delays alerts.

## Responsiveness

- Attention and error are urgent selects; `hold` (drag, touch) enters at once. Both cut any alternate,
  reaction, handover or linger immediately, as today.
- Turn-finished and every other non-linger request end a linger or an alternate at once.
- Pause stops the frame timer, so alternates, linger time and reactions all freeze.
- `Monitor`, `Sessions`, alerts and the v1 protocol are unchanged.

## Testing

Qt Test cases in `tests/prototype_tests.cpp` on the real catalog, with fake clock and random:

1. Catalog validation: the shipped section loads; unknown sequences, non-phased or looped states, bad
   `style`, `max_s` out of range, `linger.to` or handover to a non-activity state are refused.
2. Classic: the sequence order for sustained states and for thinking↔reading↔working transitions equals
   today's.
3. Alternates: none before the gap; after it, exactly one pass then the base loop; no immediate repeat;
   Subtle never draws `playful`, Playful does.
4. Linger: reading→thinking stays at the desk with `requestedState() == "thinking"` and phase `linger`;
   back to reading resumes without end or start; past `max_s` plays end then thinking; paused time does
   not count.
5. Handover: reading↔working skips end and start; works from a linger too.
6. Reactions: Playful plays `exit`/`enter` only when the gate allows, and spends the opportunity; Subtle
   never does.
7. Responsiveness: urgent select, `hold`, and turn-finished during an alternate or linger change state at
   once.
8. Preferences: default Playful, save/load, legacy file, invalid value; the settings combo changes
   behaviour live and persists.
9. `scripts/verify_assets.py` passes and rejects a malformed `activity` section.

Manual: build, run `./build/agent-pet --no-persist`, drive sustained and rapidly alternating
thinking/reading/working with `agent-pet emit`, under each style, and record the result as dated evidence.

## Documentation

- `docs/architecture.md`: an "Active animation" design section (catalog section, playback, pacing,
  responsiveness) and an "Active animation evidence — <date>" section.
- `README.md`: a user-facing section on the Active animation setting.
- `THIRD_PARTY_NOTICES.md`/`licenses/` unchanged (same VPet artwork terms).

## Out of scope

Mood art for active states (Happy/PoorCondition sets exist and are pose-compatible; a later issue), new
artwork, and any change to session timing or the wire protocol.
