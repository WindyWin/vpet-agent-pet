# Active Animation Styles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an "Active animation" preference (Classic / Subtle / Playful, default Playful) that varies sustained thinking, reading and working, keeps the pet at its desk through short thinking pauses and reading↔working switches, and adds small transition reactions, without ever delaying alerts.

**Architecture:** The catalog gains an optional `activity` section (alternate loops, `enter`/`exit` reactions, `linger`, `handover`). `Player` parses it and owns the mechanics (loop-pass alternates via `vary`, linger, handover, gated reactions). Decoration changes only which sequence plays, never `state()`. A new `Activity` class (modelled on `Ambient`) paces alternates and reactions on one shared clock and switches continuity on. `PetWindow` owns an `Activity`, persists the style, and shows a Settings combo.

**Tech Stack:** C++17, Qt 6.5+ Widgets/Test, CMake/Ninja, Python 3 asset scripts.

**Spec:** `starter/docs/superpowers/specs/2026-10-06-active-animation-design.md`

All paths below are relative to `starter/`. Run every command from `starter/`.

## Global Constraints

- Styles: Classic (0) no alternates, no continuity, no reactions; Subtle (1) `subtle` tier, gap 10–18 s, continuity on; Playful (2, **default**) `subtle` + `playful` tiers, gap 6–12 s, continuity and reactions.
- Classic with no gate and nobody calling `vary` is today's playback, byte for byte.
- Active animation is independent of Idle animation (`Ambient`): neither setting changes the other.
- Attention and error stay urgent; `hold` (drag, touch) enters at once; turn-finished and every other non-linger request ends a linger or alternate at once. Pause freezes everything.
- `Monitor`, `Sessions`, alerts and the v1 protocol are unchanged.
- `preferences.json` key `activity`, integer 0–2, default 2; missing key → default; invalid → file invalid. `PetWindow::writePreferences` keeps re-reading the file before saving.
- Weights 1–1000; `max_s` 1–60; `style` is `subtle` (default) or `playful`.
- New artwork comes only from the root archive through `scripts/add_sequences.py`; composite sequences reuse existing frames (no new PNGs). VPet artwork terms, `licenses/` and `THIRD_PARTY_NOTICES.md` unchanged.
- Docs: `docs/architecture.md` design section + dated evidence section; `README.md` user section.

## Review Focus

1. **Rapid flapping** between thinking/reading/working (tool calls under a second apart): the latest request must always be what `requestedState()` reports, and the pet must settle on it. Pinned by `decorationFollowsEveryRequest` (Task 3).
2. **Style changed in Settings mid-linger or mid-handover**: no stuck decoration; the next change plays like the new style. Pinned by `activityStyleChangesMidDesk` (Task 4).
3. **Monitor's periodic update during a linger or handover** must not reselect and restart anything. Pinned by `monitorLeavesTheDeskAlone` (Task 5).
4. **Catalog without the section, or a state without an entry** (missing optional art) plays as Classic even with Playful selected. Pinned in `malformedActivity` (Task 2) and `activityWithoutArtIsClassic` (Task 4).
5. **Paused/hidden pet mid-linger**: linger time must not count down while paused. Pinned in `lingerKeepsThePetAtItsDesk` (Task 3).

---

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `assets/vpet/vup/Think/…`, `assets/vpet/vup/WORK/…` | Create (via script) | 13 imported sequences, 126 frames |
| `assets/vpet/manifest.json`, `available-animations.json` | Modify (via script) | Bookkeeping for the imports |
| `assets/vpet/animations.json` | Modify | `state` of imported sequences, 5 composite sequences, `activity` section |
| `scripts/verify_assets.py` | Modify | Validate `activity`; activity sequences count as used/owned; optional catalog path argument |
| `src/animation/player.h/.cpp` | Modify | Activity structs, parsing, `vary`, linger, handover, reactions, phased `looped` |
| `src/animation/activity.h/.cpp` | Create | `ActivityStyle`, `Activity` pacing |
| `CMakeLists.txt` | Modify | Add `src/animation/activity.cpp` to `pet_animation` |
| `src/settings/preferences.h/.cpp` | Modify | `activity` preference |
| `src/desktop/pet_window.h/.cpp` | Modify | Own `Activity`, setter/getter, save, Settings combo |
| `tests/prototype_tests.cpp` | Modify | All new tests |
| `docs/architecture.md`, `README.md` | Modify | Design, evidence, user docs |

---

### Task 1: Artwork, composite sequences, catalog section and asset verifier

**Files:**
- Modify: `assets/vpet/animations.json`, `assets/vpet/manifest.json`, `assets/vpet/available-animations.json` (first via script)
- Modify: `scripts/verify_assets.py`

**Interfaces:**
- Consumes: nothing.
- Produces: catalog key `activity` exactly as in the spec; sequence ids `WORK/Desk/reading_to_working`, `WORK/Desk/working_to_reading`, `WORK/Desk/ponder_in`, `WORK/Desk/ponder_loop`, `WORK/Desk/ponder_out`; `scripts/verify_assets.py [catalog.json]`.

- [ ] **Step 1: Import the sequences**

```bash
python3 scripts/add_sequences.py Think/Nomal/B_2 Think/Nomal/B_3 Think/Nomal/B_4 Think/Nomal/B_5 \
  Think/Happy/B_3 Think/Happy/B_4 Think/Happy/C_2 \
  WORK/Study/B_2_Nomal WORK/Study/B_3_Nomal WORK/Study/B_4_Nomal \
  WORK/WorkONE/B_2_Nomal WORK/WorkONE/Happy/B WORK/WorkONE/B/Nomal/1
```

Expected: 13 lines such as `Think/Nomal/B_2: 9 frames, 1125 ms, 1.3 MB` and `WORK/WorkONE/B/Nomal/1: 38 frames, 4750 ms, 4.5 MB`, then the new pack totals.

- [ ] **Step 2: Map them, add the composites and the `activity` section**

Save as `$SCRATCH/map_activity.py` (`$SCRATCH` = the session scratchpad) and run `python3 $SCRATCH/map_activity.py`:

```python
import json
from pathlib import Path

path = Path('assets/vpet/animations.json')
catalog = json.loads(path.read_text())
imported = ['Think/Nomal/B_2', 'Think/Nomal/B_3', 'Think/Nomal/B_4', 'Think/Nomal/B_5', 'Think/Happy/B_3',
            'Think/Happy/B_4', 'Think/Happy/C_2', 'WORK/Study/B_2_Nomal', 'WORK/Study/B_3_Nomal',
            'WORK/Study/B_4_Nomal', 'WORK/WorkONE/B_2_Nomal', 'WORK/WorkONE/Happy/B', 'WORK/WorkONE/B/Nomal/1']
owner = {'Think/': 'thinking', 'WORK/Study/': 'reading', 'WORK/WorkONE/': 'working'}
for i, entry in enumerate(catalog['sequences']):
    if entry['path'] in imported:
        state = next(name for prefix, name in owner.items() if entry['path'].startswith(prefix))
        catalog['sequences'][i] = {'state': state, **entry}
sequences = {entry['path']: entry for entry in catalog['sequences']}

def composite(state, path, parts):
    frames = [dict(sequences[source]['frames'][i]) for source, indexes in parts for i in indexes]
    return {'state': state, 'path': path, 'frames': frames, 'duration_ms': sum(f['duration_ms'] for f in frames)}

study, write, spin = 'WORK/Study/A_Nomal', 'WORK/WorkONE/A_Nomal', 'WORK/WorkONE/B/Nomal/1'
catalog['sequences'] += [
    composite('reading', 'WORK/Desk/reading_to_working', [(study, range(11, 5, -1)), (write, range(6, 10))]),
    composite('working', 'WORK/Desk/working_to_reading', [(write, range(9, 5, -1)), (study, range(6, 12))]),
    composite('working', 'WORK/Desk/ponder_in', [(spin, range(0, 4))]),
    composite('working', 'WORK/Desk/ponder_loop', [(spin, range(4, 12))]),
    composite('working', 'WORK/Desk/ponder_out', [(spin, range(34, 38))]),
]
catalog['activity'] = {
    'thinking': {
        'loops': [
            {'sequence': 'Think/Nomal/B_2', 'weight': 2},
            {'sequence': 'Think/Nomal/B_3', 'weight': 2},
            {'sequence': 'Think/Nomal/B_4', 'weight': 1},
            {'sequence': 'Think/Nomal/B_5', 'weight': 1},
            {'sequence': 'Think/Happy/B_3', 'weight': 1, 'style': 'playful'},
            {'sequence': 'Think/Happy/B_4', 'weight': 1, 'style': 'playful'},
        ],
        'exit': {
            'reading': [{'sequence': 'Think/Happy/C_2', 'weight': 1}],
            'working': [{'sequence': 'Think/Happy/C_2', 'weight': 1}],
        },
    },
    'reading': {
        'loops': [
            {'sequence': 'WORK/Study/B_2_Nomal', 'weight': 2},
            {'sequence': 'WORK/Study/B_3_Nomal', 'weight': 1},
        ],
        'linger': {'to': 'thinking', 'max_s': 8, 'loop': ['WORK/Study/B_4_Nomal', 'WORK/Study/B_3_Nomal']},
        'handover': {'working': 'WORK/Desk/reading_to_working'},
    },
    'working': {
        'loops': [
            {'sequence': 'WORK/WorkONE/B_2_Nomal', 'weight': 2},
            {'sequence': 'WORK/WorkONE/Happy/B', 'weight': 1, 'style': 'playful'},
            {'sequence': 'WORK/WorkONE/B/Nomal/1', 'weight': 1, 'style': 'playful'},
        ],
        'enter': {'from': ['thinking'], 'choices': [{'sequence': 'WORK/WorkONE/Happy/B', 'weight': 1}]},
        'linger': {'to': 'thinking', 'max_s': 8, 'in': 'WORK/Desk/ponder_in',
                   'loop': ['WORK/Desk/ponder_loop'], 'out': 'WORK/Desk/ponder_out'},
        'handover': {'reading': 'WORK/Desk/working_to_reading'},
    },
}
path.write_text(json.dumps(catalog, indent=2) + '\n')
for entry in catalog['sequences'][-5:]:
    print(entry['path'], len(entry['frames']), entry['duration_ms'])
```

Expected output: `reading_to_working 10 1250`, `working_to_reading 10 1250`, `ponder_in 4 500`, `ponder_loop 8 1000`, `ponder_out 4 500`.

- [ ] **Step 3: Run the verifier and see it fail**

Run: `python3 scripts/verify_assets.py`
Expected: FAIL with `State, variant and mood maps do not cover the bundled sequences` and `Sequence names no state that uses it: …` for the new sequences. The verifier does not know `activity` yet.

- [ ] **Step 4: Teach the verifier the `activity` section**

In `scripts/verify_assets.py`:

1. Docstring and catalog argument. Replace the first lines:

```python
#!/usr/bin/env python3
"""Verify the selected VPet artwork and animation catalog after copying the folder.

    python3 scripts/verify_assets.py               # the bundled pack and its catalog
    python3 scripts/verify_assets.py other.json    # another catalog against the same pack
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/vpet'
manifest = json.loads((ASSETS / 'manifest.json').read_text())
animations = json.loads((Path(sys.argv[1]) if len(sys.argv) > 1 else ASSETS / 'animations.json').read_text())
```

2. Delete these two checks from their current place (right after the mood loop and the owners loops):

```python
if used != bundled_sequences:
    errors.append('State, variant and mood maps do not cover the bundled sequences')
```

```python
for sequence in animations['sequences']:
    if sequence.get('state') not in owners.get(sequence['path'], set()):
        errors.append(f'Sequence names no state that uses it: {sequence["path"]}')
```

Keep the `owners = {}` construction loops where they are.

3. After the `if 'moves' in animations:` block, before `available_sequences = …`, add:

```python
# Activity decoration (alternate loops, reactions, desk continuity) for states that end only when asked.
# A sequence it plays counts as used by, and owned by, the state it decorates.
touch = animations.get('touch', {})
touch_states = ({region.get('state') for region in touch.get('regions', [])} | set(touch.get('fall', {}).values())
                | {edge.get('state') for edge in touch.get('edge', {}).values()})
activity = animations.get('activity', {})


def claim(state, path):
    if path not in bundled_sequences:
        return False
    used.add(path)
    owners.setdefault(path, set()).add(state)
    return True


def pool(state, choices, styled=False):
    if not isinstance(choices, list) or not choices:
        return False
    valid = True
    for choice in choices:
        if not isinstance(choice, dict):
            return False
        valid = claim(state, choice.get('sequence')) and valid
        valid = valid and count(choice.get('weight')) and choice['weight'] <= 1000
        valid = valid and (choice.get('style', 'subtle') in ('subtle', 'playful') if styled else 'style' not in choice)
    return valid


for state, entry in activity.items():
    if not held(state) or state in touch_states or not isinstance(entry, dict):
        errors.append(f'Activity must decorate a phased state that ends only when asked: {state}')
        continue
    others = set(activity) - {state}
    valid = 'loops' not in entry or pool(state, entry['loops'], styled=True)
    if 'enter' in entry:
        enter = entry['enter']
        valid = (valid and isinstance(enter, dict) and isinstance(enter.get('from'), list) and bool(enter['from'])
                 and all(source in others for source in enter['from']) and pool(state, enter.get('choices')))
    if 'exit' in entry:
        leaving = entry['exit']
        valid = (valid and isinstance(leaving, dict) and bool(leaving)
                 and all(target in others and pool(state, choices) for target, choices in leaving.items()))
    if 'linger' in entry:
        linger = entry['linger']
        valid = (valid and isinstance(linger, dict) and linger.get('to') in others
                 and count(linger.get('max_s')) and linger['max_s'] <= 60
                 and isinstance(linger.get('loop'), list) and bool(linger['loop'])
                 and all(claim(state, path) for path in linger['loop'])
                 and all(claim(state, linger[key]) for key in ('in', 'out') if key in linger))
    if 'handover' in entry:
        handover = entry['handover']
        valid = (valid and isinstance(handover, dict) and bool(handover)
                 and all(target in others and claim(state, path) for target, path in handover.items()))
    if not valid:
        errors.append(f'Invalid activity: {state}')

if used != bundled_sequences:
    errors.append('State, variant, mood and activity maps do not cover the bundled sequences')
for sequence in animations['sequences']:
    if sequence.get('state') not in owners.get(sequence['path'], set()):
        errors.append(f'Sequence names no state that uses it: {sequence["path"]}')
```

`held()` (defined before the touch block) already means: a known, phased state without `loops`, not idle, dragging or a fidget.

- [ ] **Step 5: Run the verifier and see it pass**

Run: `python3 scripts/verify_assets.py`
Expected: `OK: <N> original artwork files, <M> sequences, …` with N = previous count + 126.

- [ ] **Step 6: Check that it refuses broken sections**

```bash
S=$SCRATCH/broken; mkdir -p "$S"; python3 - "$S" <<'EOF'
import json, sys
from pathlib import Path
base = json.loads(Path('assets/vpet/animations.json').read_text())
def broken(name, change):
    catalog = json.loads(json.dumps(base)); change(catalog['activity'])
    Path(sys.argv[1], name + '.json').write_text(json.dumps(catalog))
broken('style', lambda a: a['thinking']['loops'][0].update(style='wild'))
broken('linger', lambda a: a['reading']['linger'].update(to='idle'))
broken('max', lambda a: a['working']['linger'].update(max_s=61))
broken('state', lambda a: a.update(idle={}))
broken('unused', lambda a: a['reading'].pop('handover'))
broken('missing', lambda a: a['working']['handover'].update(reading='WORK/Desk/nowhere'))
EOF
for f in style linger max state unused missing; do
  python3 scripts/verify_assets.py "$S/$f.json" >/dev/null 2>&1 && echo "UNEXPECTED PASS: $f" || echo "refused: $f"
done
```

Expected: six `refused:` lines, no `UNEXPECTED PASS`.

- [ ] **Step 7: Build and run the existing suites**

The player ignores the unknown section for now; the frame glob picks up the new folders on build.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build -j 4 && ctest --test-dir build --output-on-failure
```

Expected: all suites pass.

- [ ] **Step 8: Commit**

```bash
git add assets/vpet scripts/verify_assets.py
git commit -m "feat(assets): add active animation art and catalog section (#47)"
```

---

### Task 2: Player reads and validates the `activity` section

**Files:**
- Modify: `src/animation/player.h`, `src/animation/player.cpp`
- Test: `tests/prototype_tests.cpp`

**Interfaces:**
- Consumes: the catalog section from Task 1.
- Produces (in `player.h`, namespace `pet`):

```cpp
struct ActivityChoice { QString sequence; int weight = 1; bool playful = false; };
struct ActivityLinger { QString to; int maxMs = 0; QString in, out; QStringList loop; };
struct ActivityArt {
    QVector<ActivityChoice> loops;
    QStringList enterFrom; QVector<ActivityChoice> enter;
    QMap<QString, QVector<ActivityChoice>> exit;
    ActivityLinger linger;
    QMap<QString, QString> handover;
};
const ActivityArt *Player::activity(const QString &state) const; // nullptr without an entry
```

- [ ] **Step 1: Write the failing tests**

Add to `PrototypeTests` in `tests/prototype_tests.cpp`, after `malformedVariantsAndFidgets`:

```cpp
    void activitySectionIsShipped() {
        pet::Player player;
        QVERIFY(!player.activity("idle")); QVERIFY(!player.activity("needs_input"));
        const auto *thinking = player.activity("thinking"), *reading = player.activity("reading"),
                   *working = player.activity("working");
        QVERIFY(thinking && reading && working);
        QCOMPARE(thinking->loops.size(), 6); QCOMPARE(reading->loops.size(), 2); QCOMPARE(working->loops.size(), 3);
        QVERIFY(thinking->loops.at(4).playful); QVERIFY(!thinking->loops.at(0).playful);
        QCOMPARE(thinking->exit.value("working").first().sequence, QString("Think/Happy/C_2"));
        QCOMPARE(working->enterFrom, QStringList{"thinking"});
        QCOMPARE(reading->linger.to, QString("thinking")); QCOMPARE(reading->linger.maxMs, 8000);
        QVERIFY(reading->linger.in.isEmpty()); QCOMPARE(reading->linger.loop.size(), 2);
        QCOMPARE(working->linger.in, QString("WORK/Desk/ponder_in"));
        QCOMPARE(working->linger.out, QString("WORK/Desk/ponder_out"));
        QCOMPARE(reading->handover.value("working"), QString("WORK/Desk/reading_to_working"));
        QCOMPARE(working->handover.value("reading"), QString("WORK/Desk/working_to_reading"));
    }
    void malformedActivity() {
        QTemporaryDir fixtures; auto base = fixture(fixtures.path()); // "idle" and "work" sequences exist.
        auto states = base["states"].toObject(); auto playback = base["playback"].toObject();
        for (const auto *state : {"thinking", "reading", "working"}) {
            states[state] = QJsonArray{"work", "work", "work"};
            playback[state] = QJsonObject{{"mode", "phased"}, {"after", "idle"}};
        }
        states["blink"] = QJsonArray{"work"}; playback["blink"] = QJsonObject{{"mode", "once"}, {"after", "idle"}};
        base["states"] = states; base["playback"] = playback;
        auto choice = [](const QString &sequence, int weight = 1) { return QJsonObject{{"sequence", sequence}, {"weight", weight}}; };
        const QJsonObject good{
            {"thinking", QJsonObject{
                {"loops", QJsonArray{choice("work"), QJsonObject{{"sequence", "idle"}, {"weight", 2}, {"style", "playful"}}}},
                {"exit", QJsonObject{{"working", QJsonArray{choice("idle")}}}}}},
            {"reading", QJsonObject{
                {"linger", QJsonObject{{"to", "thinking"}, {"max_s", 8}, {"loop", QJsonArray{"work", "idle"}}}},
                {"handover", QJsonObject{{"working", "idle"}}}}},
            {"working", QJsonObject{
                {"enter", QJsonObject{{"from", QJsonArray{"thinking"}}, {"choices", QJsonArray{choice("idle")}}}},
                {"linger", QJsonObject{{"to", "thinking"}, {"max_s", 60}, {"in", "work"}, {"loop", QJsonArray{"idle"}}, {"out", "work"}}}}}};
        auto with = [&](const QJsonObject &activity) { auto catalog = base; catalog["activity"] = activity; return catalog; };
        auto broken = [&](const QString &state, const QString &key, const QJsonValue &value) {
            auto activity = good; auto entry = activity[state].toObject(); entry[key] = value; activity[state] = entry;
            return with(activity);
        };
        // Without the section, or with an empty entry, everything plays as before.
        QVERIFY(loads(base)); QVERIFY(loads(with(good))); QVERIFY(loads(with(QJsonObject{{"reading", QJsonObject{}}})));
        {
            QTemporaryDir directory; fixture(directory.path()); writeCatalog(directory.path(), base);
            pet::Player plain(nullptr, directory.path()); QVERIFY(plain.valid()); QVERIFY(!plain.activity("working"));
        }
        // Only phased states that end when asked can be decorated.
        QVERIFY(!loads(with(QJsonObject{{"idle", QJsonObject{}}})));
        QVERIFY(!loads(with(QJsonObject{{"blink", QJsonObject{}}})));
        QVERIFY(!loads(with(QJsonObject{{"nobody", QJsonObject{}}})));
        auto counted = with(good); auto policies = counted["playback"].toObject();
        policies["thinking"] = QJsonObject{{"mode", "phased"}, {"after", "idle"}, {"loops", 2}};
        counted["playback"] = policies; QVERIFY(!loads(counted));
        // Each broken part is refused.
        QVERIFY(!loads(broken("thinking", "loops", QJsonArray{})));
        QVERIFY(!loads(broken("thinking", "loops", QJsonArray{choice("missing")})));
        QVERIFY(!loads(broken("thinking", "loops", QJsonArray{choice("work", 0)})));
        QVERIFY(!loads(broken("thinking", "loops", QJsonArray{choice("work", 1001)})));
        QVERIFY(!loads(broken("thinking", "loops", QJsonArray{QJsonObject{{"sequence", "work"}, {"weight", 1}, {"style", "wild"}}})));
        QVERIFY(!loads(broken("thinking", "exit", QJsonObject{{"thinking", QJsonArray{choice("idle")}}}))); // Itself.
        QVERIFY(!loads(broken("thinking", "exit", QJsonObject{{"idle", QJsonArray{choice("idle")}}})));
        QVERIFY(!loads(broken("thinking", "exit", QJsonObject{{"working", QJsonArray{}}})));
        QVERIFY(!loads(broken("working", "enter", QJsonObject{{"from", QJsonArray{"idle"}}, {"choices", QJsonArray{choice("idle")}}})));
        QVERIFY(!loads(broken("working", "enter", QJsonObject{{"from", QJsonArray{"thinking"}}, {"choices", QJsonArray{}}})));
        QVERIFY(!loads(broken("working", "enter", QJsonObject{{"from", QJsonArray{}}, {"choices", QJsonArray{choice("idle")}}})));
        QVERIFY(!loads(broken("reading", "linger", QJsonObject{{"to", "idle"}, {"max_s", 8}, {"loop", QJsonArray{"work"}}})));
        QVERIFY(!loads(broken("reading", "linger", QJsonObject{{"to", "thinking"}, {"max_s", 0}, {"loop", QJsonArray{"work"}}})));
        QVERIFY(!loads(broken("reading", "linger", QJsonObject{{"to", "thinking"}, {"max_s", 61}, {"loop", QJsonArray{"work"}}})));
        QVERIFY(!loads(broken("reading", "linger", QJsonObject{{"to", "thinking"}, {"max_s", 8}, {"loop", QJsonArray{}}})));
        QVERIFY(!loads(broken("reading", "linger", QJsonObject{{"to", "thinking"}, {"max_s", 8}, {"in", "missing"}, {"loop", QJsonArray{"work"}}})));
        QVERIFY(!loads(broken("reading", "handover", QJsonObject{{"idle", "work"}})));
        QVERIFY(!loads(broken("reading", "handover", QJsonObject{{"working", "missing"}})));
        QVERIFY(!loads(broken("reading", "handover", QJsonObject{})));
    }
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j 4 2>&1 | tail -5`
Expected: compile error, `'class pet::Player' has no member named 'activity'`.

- [ ] **Step 3: Implement the structs, accessor and parsing**

In `src/animation/player.h`, after `struct Move { … };`:

```cpp
// Decoration of a sustained activity (thinking, reading, working) from the catalog's "activity" section.
// It changes which sequence plays inside the state, never the state itself.
struct ActivityChoice { QString sequence; int weight = 1; bool playful = false; };
// Staying at the desk while another activity is requested: `in`, passes of `loop`, then `out`.
struct ActivityLinger { QString to; int maxMs = 0; QString in, out; QStringList loop; };
struct ActivityArt {
    QVector<ActivityChoice> loops; // Alternate loop passes; `playful` ones only in the Playful style.
    QStringList enterFrom; QVector<ActivityChoice> enter; // A first loop pass after one of these states.
    QMap<QString, QVector<ActivityChoice>> exit; // An end phase for leaving to that state.
    ActivityLinger linger; // None while `linger.to` is empty.
    QMap<QString, QString> handover; // To that state without leaving the desk.
};
```

In the `public:` part of `Player`, after `int moveScale() const …;`:

```cpp
    // The decoration of an activity state, or null for a state without any.
    const ActivityArt *activity(const QString &state) const;
```

In the `private:` members, after `QMap<QString, Move> moves_;`:

```cpp
    QMap<QString, ActivityArt> activity_;
```

In `src/animation/player.cpp`, change the `invalid` lambda in `load` to clear it as well:

```cpp
    auto invalid = [this](const QString &reason) {
        sequences_.clear(); animations_.clear(); activity_.clear(); fail(reason); return false;
    };
```

In `load`, after the `if (catalog.contains("moves")) { … }` block and before `return true;`:

```cpp
    // Activity decoration: alternate loops, reactions and desk continuity for states that end only when
    // asked. Every part is optional; a state without an entry plays only its own sequences.
    if (catalog.contains("activity")) {
        const auto activity = catalog["activity"].toObject();
        auto decorated = [&](const QString &state) {
            const auto found = animations_.constFind(state);
            return found != animations_.constEnd() && found->mode == "phased" && found->loops == 0 && state != "idle"
                && state != "dragging" && !fidgetStates_.contains(state) && !touchStates_.contains(state);
        };
        auto pool = [&](const QJsonValue &value, QVector<ActivityChoice> &choices, bool styled) {
            if (!value.isArray() || value.toArray().isEmpty()) return false;
            for (const auto &item : value.toArray()) {
                const auto object = item.toObject();
                const auto style = object.value("style");
                const ActivityChoice choice{object["sequence"].toString(), object["weight"].toInt(0), style == "playful"};
                if (!sequences_.contains(choice.sequence) || choice.weight < 1 || choice.weight > maxWeight
                    || (!style.isUndefined() && (!styled || (style != "subtle" && style != "playful"))))
                    return false;
                choices.append(choice);
            }
            return true;
        };
        if (!catalog["activity"].isObject()) return invalid("Invalid activity section.");
        for (auto it = activity.begin(); it != activity.end(); ++it) {
            const auto object = it.value().toObject();
            auto other = [&](const QString &state) { return state != it.key() && activity.contains(state); };
            ActivityArt art;
            bool ok = decorated(it.key()) && it.value().isObject();
            if (ok && object.contains("loops")) ok = pool(object["loops"], art.loops, true);
            if (ok && object.contains("enter")) {
                const auto enter = object["enter"].toObject();
                ok = !enter["from"].toArray().isEmpty() && pool(enter["choices"], art.enter, false);
                for (const auto &state : enter["from"].toArray()) {
                    ok = ok && other(state.toString());
                    art.enterFrom.append(state.toString());
                }
            }
            if (ok && object.contains("exit")) {
                const auto exit = object["exit"].toObject();
                ok = !exit.isEmpty();
                for (auto target = exit.begin(); ok && target != exit.end(); ++target)
                    ok = other(target.key()) && pool(target.value(), art.exit[target.key()], false);
            }
            if (ok && object.contains("linger")) {
                const auto linger = object["linger"].toObject();
                const int maxS = linger["max_s"].toInt(0);
                art.linger = {linger["to"].toString(), maxS * 1000, linger["in"].toString(), linger["out"].toString(), {}};
                ok = other(art.linger.to) && maxS >= 1 && maxS <= 60 && !linger["loop"].toArray().isEmpty()
                    && (!linger.contains("in") || sequences_.contains(art.linger.in))
                    && (!linger.contains("out") || sequences_.contains(art.linger.out));
                for (const auto &id : linger["loop"].toArray()) {
                    ok = ok && sequences_.contains(id.toString());
                    art.linger.loop.append(id.toString());
                }
            }
            if (ok && object.contains("handover")) {
                const auto handover = object["handover"].toObject();
                ok = !handover.isEmpty();
                for (auto target = handover.begin(); ok && target != handover.end(); ++target) {
                    ok = other(target.key()) && sequences_.contains(target.value().toString());
                    art.handover.insert(target.key(), target.value().toString());
                }
            }
            if (!ok) return invalid("Invalid activity for " + it.key());
            activity_.insert(it.key(), art);
        }
    }
```

After `Player::move`:

```cpp
const ActivityArt *Player::activity(const QString &state) const {
    const auto found = activity_.constFind(state);
    return found == activity_.constEnd() ? nullptr : &*found;
}
```

- [ ] **Step 4: Run the tests and see them pass**

```bash
cmake --build build -j 4 && QT_QPA_PLATFORM=offscreen ./build/prototype-tests activitySectionIsShipped malformedActivity
```

Expected: `Totals: 4 passed, 0 failed` (2 tests + init/cleanup).

- [ ] **Step 5: Run the whole prototype suite**

Run: `ctest --test-dir build -R prototype --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/animation/player.h src/animation/player.cpp tests/prototype_tests.cpp
git commit -m "feat(animation): load and validate the activity catalog section (#47)"
```

---

### Task 3: Player playback: phased `looped`, `vary`, linger, handover, reactions

**Files:**
- Modify: `src/animation/player.h`, `src/animation/player.cpp`
- Test: `tests/prototype_tests.cpp`

**Interfaces:**
- Consumes: `ActivityArt`, `Player::activity()` from Task 2.
- Produces (public in `Player`):

```cpp
void setContinuity(bool enabled);              // handover and linger; default false
bool continuity() const;
void setReactionGate(std::function<bool()> gate); // asked when an enter/exit reaction could play
bool vary(const QString &sequence);            // alternate for this loop pass
```

`looped(state)` is now also emitted at each new pass of a phased state's loop phase. `phase()` may also return `"linger"` or `"handover"`.

- [ ] **Step 1: Write the failing tests**

Add `#include <QRandomGenerator>` to the test includes. Add to `PrototypeTests`, after `malformedActivity`:

```cpp
    void phasedLoopsAnnounceEachPass() {
        pet::Player player; player.setPaused(true);
        QSignalSpy looped(&player, &pet::Player::looped);
        player.select("reading", true);
        QVERIFY(!player.vary("WORK/Study/B_2_Nomal")); // Start phase.
        finishSequence(player); QCOMPARE(looped.size(), 0); // Its first pass is no new pass.
        finishSequence(player);
        QCOMPARE(looped.size(), 1); QCOMPARE(looped.last().first().toString(), QString("reading"));
        QVERIFY(!player.vary("WORK/WorkONE/B_2_Nomal")); // Another state's alternate.
        QVERIFY(!player.vary("WORK/Study/C_Nomal")); // Not an alternate at all.
        QVERIFY(player.vary("WORK/Study/B_2_Nomal"));
        QCOMPARE(player.sequence(), QString("WORK/Study/B_2_Nomal")); QCOMPARE(player.frameIndex(), 0);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("loop"));
        player.advance(); QVERIFY(!player.vary("WORK/Study/B_3_Nomal")); // Mid-pass.
        // An alternate lasts one pass.
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Study/B_1_Nomal")); QCOMPARE(looped.size(), 2);
        // A listener varies the pass it is told about.
        const auto connection = connect(&player, &pet::Player::looped, &player,
                                        [&] { QVERIFY(player.vary("WORK/Study/B_3_Nomal")); });
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Study/B_3_Nomal"));
        disconnect(connection);
    }
    void everyActivityAlternateDecodes() {
        pet::Player player; player.setPaused(true); player.setRenderSize(640);
        for (const auto *state : {"thinking", "reading", "working"})
            for (const auto &loop : player.activity(state)->loops) {
                player.select(state, true); finishSequence(player);
                QVERIFY2(player.vary(loop.sequence), qPrintable(loop.sequence));
                for (int i = 0, count = player.frameCount(); i < count; ++i) {
                    QVERIFY2(!player.pixmap().isNull(), qPrintable(player.error()));
                    QVERIFY(player.cacheKiB() <= pet::Player::cacheLimitKiB); player.advance();
                }
                QCOMPARE(player.state(), QString(state)); QVERIFY2(player.error().isEmpty(), qPrintable(player.error()));
            }
    }
    void lingerKeepsThePetAtItsDesk() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; }); player.setContinuity(true);
        player.select("reading", true); finishSequence(player);
        // A short thinking pause keeps the book open; the request counts as honoured.
        player.select("thinking");
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.requestedState(), QString("thinking"));
        QCOMPARE(player.phase(), QString("linger")); QCOMPARE(player.sequence(), QString("WORK/Study/B_4_Nomal"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Study/B_3_Nomal")); // Never twice in a row.
        player.advance(); player.select("thinking"); QCOMPARE(player.frameIndex(), 1); // The same request restarts nothing.
        // Paused, nothing is shown, so no linger time passes.
        QTest::qWait(30); QCOMPARE(player.frameIndex(), 1); QCOMPARE(player.phase(), QString("linger"));
        // Reading again resumes the base loop: no end, no start.
        QVERIFY(player.select("reading"));
        QCOMPARE(player.phase(), QString("loop")); QCOMPARE(player.sequence(), QString("WORK/Study/B_1_Nomal"));
        QCOMPARE(player.requestedState(), QString("reading"));
        // A long pause ends at the first pass boundary at or past max_s, counted in frames shown:
        // 1250 + 1500 + 1250 + 1500 + 1250 = 6750 ms, and the sixth pass reaches 8250 ms.
        player.select("thinking");
        for (int pass = 0; pass < 5; ++pass) { finishSequence(player); QCOMPARE(player.phase(), QString("linger")); }
        finishSequence(player);
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("WORK/Study/C_Nomal"));
        finishSequence(player); QCOMPARE(player.state(), QString("thinking")); QCOMPARE(player.phase(), QString("start"));
        // Working ponders chin in hand, with an in and an out.
        player.select("working", true); finishSequence(player);
        player.select("thinking");
        QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_in")); QCOMPARE(player.phase(), QString("linger"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_loop"));
        player.select("working");
        QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_out")); QCOMPARE(player.phase(), QString("linger"));
        QCOMPARE(player.requestedState(), QString("working"));
        finishSequence(player);
        QCOMPARE(player.phase(), QString("loop")); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal"));
        // 500 ms in, then 1000 ms passes: the eighth reaches 8500 ms, then out, end and thinking.
        player.select("thinking"); finishSequence(player);
        for (int pass = 0; pass < 7; ++pass) { finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_loop")); }
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_out"));
        finishSequence(player);
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("WORK/WorkONE/C_Nomal"));
        finishSequence(player); QCOMPARE(player.state(), QString("thinking"));
    }
    void handoverSwapsPropsAtTheDesk() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; }); player.setContinuity(true);
        QSignalSpy entered(&player, &pet::Player::entered);
        player.select("reading", true); finishSequence(player); entered.clear();
        player.select("working");
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("handover"));
        QCOMPARE(player.sequence(), QString("WORK/Desk/reading_to_working"));
        QCOMPARE(player.requestedState(), QString("working")); QCOMPARE(entered.size(), 0);
        // No end and no start: straight into working's loop.
        finishSequence(player);
        QCOMPARE(player.state(), QString("working")); QCOMPARE(player.phase(), QString("loop"));
        QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal")); QCOMPARE(entered.size(), 1);
        // From a linger too: the pen comes down first, then the props swap.
        player.select("thinking"); QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_in"));
        finishSequence(player); player.select("reading");
        QCOMPARE(player.sequence(), QString("WORK/Desk/ponder_out")); QCOMPARE(player.requestedState(), QString("reading"));
        finishSequence(player);
        QCOMPARE(player.phase(), QString("handover")); QCOMPARE(player.sequence(), QString("WORK/Desk/working_to_reading"));
        finishSequence(player); QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("loop"));
        // Reading's linger has no out, so its handover starts at once.
        player.select("thinking"); player.select("working");
        QCOMPARE(player.sequence(), QString("WORK/Desk/reading_to_working"));
        // The same request again changes nothing; any other plays the usual end at once.
        player.select("working"); QCOMPARE(player.phase(), QString("handover"));
        player.select("idle");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("WORK/Study/C_Nomal"));
        QCOMPARE(player.requestedState(), QString("idle"));
    }
    void reactionsAskTheGate() {
        pet::Player player; player.setPaused(true);
        int asked = 0; bool allow = false;
        player.setReactionGate([&] { ++asked; return allow; });
        player.select("thinking", true); QCOMPARE(asked, 0); // Nothing reacts to coming from idle.
        finishSequence(player);
        // Leaving for reading asks once; refused, the usual end plays, and reading has no welcome.
        player.select("reading"); QCOMPARE(asked, 1); QCOMPARE(player.sequence(), QString("Think/Nomal/C"));
        finishSequence(player); QCOMPARE(player.state(), QString("reading")); QCOMPARE(asked, 1);
        // Allowed: thinking leaves for working with a happy turn instead of its end...
        player.select("thinking", true); finishSequence(player);
        allow = true; player.select("working"); QCOMPARE(asked, 2);
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("Think/Happy/C_2"));
        // ...and working, entered from thinking, asks for its welcome: the first loop pass.
        finishSequence(player); QCOMPARE(asked, 3); QCOMPARE(player.sequence(), QString("WORK/WorkONE/A_Nomal"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/Happy/B"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal")); // One pass.
        // Transitions without art never ask.
        player.select("idle"); QCOMPARE(asked, 3); QCOMPARE(player.sequence(), QString("WORK/WorkONE/C_Nomal"));
        // An urgent select skips the end and its reaction, but the welcome still asks.
        player.select("thinking", true); finishSequence(player);
        player.select("working", true); QCOMPARE(asked, 4);
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/Happy/B"));
    }
    void decorationNeverDelaysAlerts() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; }); player.setContinuity(true);
        auto lingerInReading = [&] {
            player.select("reading", true); finishSequence(player); player.select("thinking");
            QCOMPARE(player.phase(), QString("linger"));
        };
        // An alternate pass ends at once, for an urgent change or not.
        player.select("reading", true); finishSequence(player); finishSequence(player);
        QVERIFY(player.vary("WORK/Study/B_2_Nomal")); player.advance();
        player.select("turn_finished");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("WORK/Study/C_Nomal"));
        player.select("reading", true); finishSequence(player); finishSequence(player);
        QVERIFY(player.vary("WORK/Study/B_2_Nomal"));
        player.select("needs_input", true);
        QCOMPARE(player.state(), QString("needs_input")); QCOMPARE(player.phase(), QString("start"));
        // So does a linger: an alert, an error, a drag, a finished turn or idle each act at once.
        lingerInReading(); player.select("needs_input", true); QCOMPARE(player.state(), QString("needs_input"));
        lingerInReading(); player.select("tool_error", true); QCOMPARE(player.state(), QString("tool_error"));
        lingerInReading(); player.beginDrag(); QCOMPARE(player.state(), QString("dragging"));
        player.endDrag(); QCOMPARE(player.requestedState(), QString("thinking")); // The request it was showing.
        lingerInReading(); player.select("turn_finished");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.requestedState(), QString("turn_finished"));
        lingerInReading(); player.select("idle"); QCOMPARE(player.sequence(), QString("WORK/Study/C_Nomal"));
        // Continuity off: a short thinking pause plays the usual end.
        player.setContinuity(false); player.select("reading", true); finishSequence(player);
        player.select("thinking"); QCOMPARE(player.phase(), QString("end"));
    }
    void decorationFollowsEveryRequest() {
        // Tool calls can flip the request many times a second; whatever the timing, the latest one wins.
        pet::Player player; player.setPaused(true); player.setContinuity(true);
        QRandomGenerator generator(47);
        player.setRandom([&](int bound) { return int(generator.bounded(bound)); });
        int asked = 0; player.setReactionGate([&] { return ++asked % 2 == 0; });
        const QStringList requests{"thinking", "reading", "working", "idle", "turn_finished"};
        for (int step = 0; step < 3000; ++step) {
            if (generator.bounded(10) < 4) {
                const auto request = requests.at(int(generator.bounded(int(requests.size()))));
                player.select(request);
                QCOMPARE(player.requestedState(), request);
            } else {
                for (int i = int(generator.bounded(1, 8)); i > 0; --i) player.advance();
            }
            QVERIFY2(player.error().isEmpty(), qPrintable(player.error()));
        }
        player.select("reading");
        for (int i = 0; i < 40 && !(player.state() == "reading" && player.phase() == "loop"); ++i) finishSequence(player);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("loop"));
    }
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j 4 2>&1 | tail -5`
Expected: compile errors, `no member named 'vary'`, `'setContinuity'`, `'setReactionGate'`.

- [ ] **Step 3: Implement the header changes**

In `src/animation/player.h`, add `#include <functional>` with the other includes. In `public:` after `const ActivityArt *activity(…) const;`:

```cpp
    // Handover and linger keep the pet at its desk between activities. Off, every change plays the
    // outgoing state's end, as without the section.
    void setContinuity(bool enabled) { continuity_ = enabled; }
    bool continuity() const { return continuity_; }
    // Asked each time an enter or exit reaction could play; true spends that opportunity on it. Unset,
    // none plays.
    void setReactionGate(std::function<bool()> gate) { reactionGate_ = std::move(gate); }
    // Plays one of the state's alternate loops for this pass instead of its own. Only at the first frame
    // of a loop pass, as from a `looped` handler; false otherwise.
    bool vary(const QString &sequence);
```

Replace the `looped` signal comment:

```cpp
    // A looping state, or a phased state's loop phase, started another pass; listeners may select a
    // new state or vary the pass.
    void looped(const QString &state);
```

Replace the private declarations `void enter(const QString &state);` and `void enterSequence(int phase);` with:

```cpp
    enum class Decoration { None, LingerIn, Linger, LingerOut, Handover };
    void enter(const QString &state, int phase = 0);
    // `sequence` replaces the phase's own for this pass, as an alternate or a reaction does.
    void enterSequence(int phase, const QString &sequence = {});
    bool decorate(const QString &target);
    void decorationEnded();
    void leaveLinger();
    void beginEnd();
    QString drawChoice(const QVector<ActivityChoice> &pool);
    QString drawLinger();
```

After `QMap<QString, ActivityArt> activity_;`:

```cpp
    std::function<bool()> reactionGate_;
    Decoration decoration_ = Decoration::None;
    QString welcome_, lingerLast_; // `welcome_`: the enter reaction waiting for the first loop pass.
    int lingerMs_ = 0;
    bool continuity_ = false;
```

- [ ] **Step 4: Implement the mechanics**

In `src/animation/player.cpp`, add `#include <algorithm>` and `#include <utility>`.

Replace `Player::finish`:

```cpp
void Player::finish() {
    if (stopped_ || held_ || animations_.value(state_).mode != "phased" || phase_ == 2) return;
    beginEnd();
}
```

In `Player::phase`, after the `"unavailable"` line:

```cpp
    if (decoration_ == Decoration::Handover) return "handover";
    if (decoration_ != Decoration::None) return "linger";
```

In `Player::select`, replace the non-urgent phased branch:

```cpp
    // Update a queued transition without restarting the outgoing exit sequence.
    if (!interrupt && animations_.value(state_).mode == "phased" && !stopped_) {
        if (decorate(state)) return true;
        pending_ = state;
        if (phase_ != 2) beginEnd();
        return true;
    }
```

Add after `Player::select`:

```cpp
// A non-urgent change from an activity's loop phase that can stay at the desk; false plays the usual end.
bool Player::decorate(const QString &target) {
    const auto found = activity_.constFind(state_);
    if (!continuity_ || phase_ != 1 || found == activity_.constEnd()) return false;
    const auto &art = *found;
    if (decoration_ != Decoration::None && target == pending_) return true; // Already on its way there.
    const bool lingering = decoration_ == Decoration::LingerIn || decoration_ == Decoration::Linger
        || decoration_ == Decoration::LingerOut;
    if (decoration_ == Decoration::None && !art.linger.to.isEmpty() && target == art.linger.to) {
        pending_ = target; lingerMs_ = 0; lingerLast_.clear();
        decoration_ = art.linger.in.isEmpty() ? Decoration::Linger : Decoration::LingerIn;
        enterSequence(1, art.linger.in.isEmpty() ? drawLinger() : art.linger.in);
        return true;
    }
    if (lingering && (target == state_ || art.handover.contains(target))) {
        // Back to work, or on to the other prop; a playing out decides what follows it.
        pending_ = target == state_ ? QString() : target;
        if (decoration_ == Decoration::LingerOut) return true;
        if (art.linger.out.isEmpty()) leaveLinger();
        else { decoration_ = Decoration::LingerOut; enterSequence(1, art.linger.out); }
        return true;
    }
    if (decoration_ == Decoration::None && art.handover.contains(target)) {
        pending_ = target; decoration_ = Decoration::Handover;
        enterSequence(1, art.handover.value(target));
        return true;
    }
    return false;
}
// A handover or a linger sequence just ended.
void Player::decorationEnded() {
    const auto &art = activity_[state_];
    if (decoration_ == Decoration::Handover) {
        const auto target = pending_;
        pending_.clear();
        enter(target, 1);
        return;
    }
    if (decoration_ == Decoration::LingerIn || (decoration_ == Decoration::Linger && lingerMs_ < art.linger.maxMs)) {
        decoration_ = Decoration::Linger;
        enterSequence(1, drawLinger());
        return;
    }
    if (decoration_ == Decoration::Linger && !art.linger.out.isEmpty()) {
        decoration_ = Decoration::LingerOut;
        enterSequence(1, art.linger.out);
        return;
    }
    leaveLinger();
}
// After a linger: back to the loop, on to a handover, or the usual end and the pending state.
void Player::leaveLinger() {
    const auto &art = activity_[state_];
    if (pending_.isEmpty()) { decoration_ = Decoration::None; enterSequence(1); }
    else if (art.handover.contains(pending_)) {
        decoration_ = Decoration::Handover;
        enterSequence(1, art.handover.value(pending_));
    } else beginEnd();
}
// The end phase, or a reaction drawn for leaving to the pending state when the gate allows one.
void Player::beginEnd() {
    decoration_ = Decoration::None;
    QString farewell;
    const auto found = activity_.constFind(state_);
    if (found != activity_.constEnd() && reactionGate_)
        if (const auto pool = found->exit.value(pending_); !pool.isEmpty() && reactionGate_()) farewell = drawChoice(pool);
    enterSequence(2, farewell);
}
QString Player::drawChoice(const QVector<ActivityChoice> &pool) {
    int total = 0;
    for (const auto &choice : pool) total += choice.weight;
    int roll = pool.size() == 1 ? 0 : qBound(0, random_(total), total - 1);
    for (const auto &choice : pool) {
        if (roll < choice.weight) return choice.sequence;
        roll -= choice.weight;
    }
    return pool.first().sequence;
}
// Linger passes are drawn evenly, never the same one twice in a row unless it is the only one.
QString Player::drawLinger() {
    auto loop = activity_[state_].linger.loop;
    if (loop.size() > 1) loop.removeAll(lingerLast_);
    lingerLast_ = loop.size() == 1 ? loop.first() : loop.at(qBound(0, random_(int(loop.size())), int(loop.size()) - 1));
    return lingerLast_;
}
bool Player::vary(const QString &sequence) {
    const auto found = activity_.constFind(state_);
    if (found == activity_.constEnd() || stopped_ || phase_ != 1 || decoration_ != Decoration::None || index_ != 0
        || std::none_of(found->loops.begin(), found->loops.end(),
                        [&](const ActivityChoice &choice) { return choice.sequence == sequence; }))
        return false;
    sequence_ = sequence;
    display(); // In the same event-loop turn as the pass's first frame, so nothing is painted in between.
    return true;
}
```

Replace `Player::enter` and `Player::enterSequence`:

```cpp
void Player::enter(const QString &state, int phase) {
    const auto from = state_;
    state_ = state;
    stopped_ = false;
    chosen_ = choose(state);
    loopCount_ = 0;
    decoration_ = Decoration::None;
    welcome_.clear();
    // A welcome replaces the first loop pass when the pet comes from one of the listed states.
    const auto art = activity_.constFind(state);
    if (art != activity_.constEnd() && art->enterFrom.contains(from) && reactionGate_ && reactionGate_())
        welcome_ = drawChoice(art->enter);
    emit entered(state);
    enterSequence(phase, phase == 1 ? std::exchange(welcome_, {}) : QString());
}
void Player::enterSequence(int phase, const QString &sequence) {
    timer_.stop();
    phase_ = phase;
    sequence_ = sequence.isEmpty() ? chosen_.value(phase) : sequence;
    index_ = 0;
    pixmap_ = {};
    cache_.clear();
    display();
}
```

In `Player::advance`, make these three edits.

(a) Replace the first lines up to the frame step:

```cpp
void Player::advance() {
    timer_.stop();
    if (stopped_ || !sequences_.contains(sequence_)) return;
    // Linger time is the frames shown, so a paused or hidden pet never runs it down.
    if (decoration_ == Decoration::LingerIn || decoration_ == Decoration::Linger) lingerMs_ += duration_;
    if (++index_ < sequences_[sequence_].size()) { display(); return; }
    if (decoration_ != Decoration::None) { decorationEnded(); return; }
```

(b) Replace `if (phase_ == 0) { enterSequence(1); return; }` with:

```cpp
        if (phase_ == 0) { enterSequence(1, std::exchange(welcome_, {})); return; }
```

(c) Replace the tail starting at the `// Another pass of a loop.` comment:

```cpp
    // Another pass of a loop. An idle loop may switch variant or mood here, between two identical first
    // frames, and with variants off this is where it returns to the catalog's own entry. A phased loop
    // returns to its own sequence: an alternate or a welcome lasts one pass.
    if (animation.mode == "loop") {
        chosen_ = choose(state_);
        sequence_ = chosen_.value(0);
    } else sequence_ = chosen_.value(phase_);
    index_ = 0;
    display();
    emit looped(state_); // Last: a listener may select a new state or vary this pass.
}
```

- [ ] **Step 5: Run the new tests and see them pass**

```bash
cmake --build build -j 4 && QT_QPA_PLATFORM=offscreen ./build/prototype-tests phasedLoopsAnnounceEachPass \
  everyActivityAlternateDecodes lingerKeepsThePetAtItsDesk handoverSwapsPropsAtTheDesk reactionsAskTheGate \
  decorationNeverDelaysAlerts decorationFollowsEveryRequest
```

Expected: all pass.

- [ ] **Step 6: Run every suite (bare players have continuity off and no gate, so existing tests are unchanged)**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add src/animation/player.h src/animation/player.cpp tests/prototype_tests.cpp
git commit -m "feat(animation): play activity alternates, linger, handover and reactions (#47)"
```

---

### Task 4: `Activity` pacing

**Files:**
- Create: `src/animation/activity.h`, `src/animation/activity.cpp`
- Modify: `CMakeLists.txt:36-37`
- Test: `tests/prototype_tests.cpp`

**Interfaces:**
- Consumes: `Player::activity`, `setContinuity`, `setReactionGate`, `vary`, signals `entered`/`looped` (Task 3).
- Produces:

```cpp
enum class ActivityStyle { Classic = 0, Subtle = 1, Playful = 2 };
class Activity : public QObject {
    explicit Activity(Player &player, QObject *parent = nullptr);
    void setStyle(ActivityStyle style);
    ActivityStyle style() const;            // default Playful
    void setRandom(Random random);
    void setClock(std::function<qint64()> milliseconds);
    static QPair<int, int> gapSeconds(ActivityStyle style); // Subtle {10,18}, Playful {6,12}
};
```

- [ ] **Step 1: Write the failing tests**

Add `#include "animation/activity.h"` at the top of `tests/prototype_tests.cpp`. Add after `decorationFollowsEveryRequest`:

```cpp
    void activityAlternatesOnePassAtATime() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        pet::Activity activity(player); Draws draws; qint64 now = 1000000;
        activity.setRandom(draws.random()); activity.setClock([&] { return now; });
        QCOMPARE(activity.style(), pet::ActivityStyle::Playful); QVERIFY(player.continuity());
        QCOMPARE(pet::Activity::gapSeconds(pet::ActivityStyle::Playful), (QPair<int, int>{6, 12}));
        QCOMPARE(pet::Activity::gapSeconds(pet::ActivityStyle::Subtle), (QPair<int, int>{10, 18}));
        // Entering thinking starts the clock: a gap draw of 0 is the shortest wait, 6 s.
        draws.values = {0}; player.select("thinking", true); finishSequence(player);
        QCOMPARE(player.sequence(), QString("Think/Nomal/B"));
        now += 5999; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B"));
        // Due: thinking's loops weigh 2+2+1+1+1+1; a roll of 2 is B_3. Then the next gap.
        now += 1; draws.values = {2, 0}; finishSequence(player);
        QCOMPARE(player.sequence(), QString("Think/Nomal/B_3")); QCOMPARE(player.frameIndex(), 0);
        QCOMPARE(player.state(), QString("thinking")); QCOMPARE(player.phase(), QString("loop"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B")); // One pass only.
        // Never the same alternate twice in a row: without B_3 (2,1,1,1,1) a roll of 2 is B_4.
        now += 6000; draws.values = {2, 0}; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B_4"));
        // Subtle waits 10 to 18 s and never draws the playful tier: the highest roll is B_5.
        draws.values = {0}; activity.setStyle(pet::ActivityStyle::Subtle); // A new pace starts from now.
        finishSequence(player); now += 9999; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B"));
        now += 1; draws.values = {99, 0}; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B_5"));
        // Playful reaches the happy loops with the same roll.
        draws.values = {0}; activity.setStyle(pet::ActivityStyle::Playful); finishSequence(player);
        now += 6000; draws.values = {99, 0}; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Happy/B_4"));
        // Classic varies nothing, ever, and draws nothing.
        activity.setStyle(pet::ActivityStyle::Classic); QVERIFY(!player.continuity());
        finishSequence(player); now += 3600000; finishSequence(player); QCOMPARE(player.sequence(), QString("Think/Nomal/B"));
        QCOMPARE(draws.unexpected, 0);
    }
    void activityReactionsShareTheClock() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        pet::Activity activity(player); Draws draws; qint64 now = 1000000;
        activity.setRandom(draws.random()); activity.setClock([&] { return now; });
        draws.values = {0}; player.select("thinking", true); finishSequence(player); // Due in 6 s.
        // Not due: the usual end, and no welcome.
        player.select("working"); QCOMPARE(player.sequence(), QString("Think/Nomal/C"));
        finishSequence(player); finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal"));
        // Due: the exit reaction spends the opportunity (next gap drawn)...
        player.select("thinking", true); finishSequence(player);
        now += 6000; draws.values = {0}; player.select("working");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.sequence(), QString("Think/Happy/C_2"));
        // ...so working's welcome does not follow: one reaction per transition.
        finishSequence(player); finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal"));
        // Straight in from thinking, only the welcome can play, for one pass.
        player.select("thinking", true); finishSequence(player);
        now += 6000; draws.values = {0}; player.select("working", true);
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/Happy/B"));
        finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal"));
        // Subtle never reacts, however long it waits.
        draws.values = {0}; activity.setStyle(pet::ActivityStyle::Subtle);
        player.select("thinking", true); finishSequence(player);
        now += 3600000; player.select("working"); QCOMPARE(player.sequence(), QString("Think/Nomal/C"));
        finishSequence(player); finishSequence(player); QCOMPARE(player.sequence(), QString("WORK/WorkONE/B_1_Nomal"));
        QCOMPARE(draws.unexpected, 0);
    }
    void classicActivityIsTodaysPlayback() {
        // The same requests show the same sequences with a Classic pace as with no pacing at all.
        auto script = [](pet::Player &player) {
            QStringList shown;
            auto note = [&] { shown << player.state() + " " + player.phase() + " " + player.sequence(); };
            auto step = [&](int passes) { for (int i = 0; i < passes; ++i) { finishSequence(player); note(); } };
            player.select("reading", true); note(); step(3);
            player.select("thinking"); note(); step(3);
            player.select("working"); note(); step(3);
            player.select("reading"); note(); step(3);
            player.select("working"); player.select("reading"); note(); step(3);
            return shown;
        };
        pet::Player plain; plain.setPaused(true); plain.setRandom([](int) { return 0; });
        pet::Player classic; classic.setPaused(true); classic.setRandom([](int) { return 0; });
        pet::Activity activity(classic); Draws draws; qint64 now = 0;
        activity.setRandom(draws.random()); activity.setClock([&] { return now += 60000; }); // Always due.
        activity.setStyle(pet::ActivityStyle::Classic);
        const auto expected = script(plain);
        QCOMPARE(script(classic), expected);
        QCOMPARE(draws.unexpected, 0);
        QVERIFY(expected.contains("reading end WORK/Study/C_Nomal"));
        QVERIFY(expected.contains("thinking start Think/Nomal/A"));
        // Playful, the same requests stay at the desk.
        pet::Player playful; playful.setPaused(true); playful.setRandom([](int) { return 0; });
        pet::Activity paced(playful); paced.setRandom([](int) { return 0; }); paced.setClock([] { return qint64(0); });
        const auto desk = script(playful);
        QVERIFY(desk.contains("reading linger WORK/Study/B_4_Nomal"));
        QVERIFY(desk.contains("reading handover WORK/Desk/reading_to_working"));
    }
    void activityStyleChangesMidDesk() {
        pet::Player player; player.setPaused(true); player.setRandom([](int) { return 0; });
        pet::Activity activity(player); activity.setRandom([](int) { return 0; }); activity.setClock([] { return qint64(0); });
        player.select("reading", true); finishSequence(player); player.select("thinking");
        QCOMPARE(player.phase(), QString("linger"));
        // Switched to Classic mid-linger, the next change plays the usual end at once...
        activity.setStyle(pet::ActivityStyle::Classic); player.select("reading");
        QCOMPARE(player.phase(), QString("end")); QCOMPARE(player.requestedState(), QString("reading"));
        for (int i = 0; i < 4 && !(player.state() == "reading" && player.phase() == "loop"); ++i) finishSequence(player);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("loop"));
        // ...and a handover in progress still lands where it was going.
        activity.setStyle(pet::ActivityStyle::Playful); player.select("working");
        QCOMPARE(player.phase(), QString("handover"));
        activity.setStyle(pet::ActivityStyle::Classic); finishSequence(player);
        QCOMPARE(player.state(), QString("working")); QCOMPARE(player.phase(), QString("loop"));
    }
    void activityWithoutArtIsClassic() {
        // A catalog without the section: a Playful pace has nothing to vary or react with.
        QTemporaryDir directory; auto catalog = fixture(directory.path()); writeCatalog(directory.path(), catalog);
        pet::Player player(nullptr, directory.path()); player.setPaused(true);
        pet::Activity activity(player); Draws draws; qint64 now = 0;
        activity.setRandom(draws.random()); activity.setClock([&] { return now += 60000; });
        player.select("working", true);
        for (int i = 0; i < 5; ++i) { finishSequence(player); QCOMPARE(player.sequence(), QString("work")); }
        QCOMPARE(draws.unexpected, 0);
    }
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j 4 2>&1 | tail -5`
Expected: compile error, `animation/activity.h: No such file or directory`.

- [ ] **Step 3: Implement `Activity`**

`src/animation/activity.h`:

```cpp
#pragma once
#include "player.h"
#include <QObject>
#include <functional>

namespace pet {
enum class ActivityStyle { Classic = 0, Subtle = 1, Playful = 2 };

// Paces the decoration of thinking, reading and working: now and then a loop pass plays one of the
// state's alternates, and in Playful the same opportunity may go to a reaction to a real transition
// instead, so it reacts at most once per transition and not to every tool call. Desk continuity
// (handover, linger) is the player's; this class only switches it on. It runs on the player's loop
// passes, so a paused or hidden pet does nothing. The art comes from the catalog's "activity" section.
class Activity : public QObject {
    Q_OBJECT
public:
    explicit Activity(Player &player, QObject *parent = nullptr);
    ~Activity() override;
    void setStyle(ActivityStyle style);
    ActivityStyle style() const { return style_; }
    // Replaceable for tests. The draws are: a gap when an activity starts, on a style change and after each
    // alternate or reaction; per alternate, the weighted pick before its gap.
    void setRandom(Random random) { random_ = random ? std::move(random) : systemRandom(); }
    void setClock(std::function<qint64()> milliseconds);
    // The shortest and longest wait between opportunities, in seconds.
    static QPair<int, int> gapSeconds(ActivityStyle style);
private:
    void entered(const QString &state);
    void looped(const QString &state);
    bool react();
    qint64 gapMs();
    Player &player_;
    ActivityStyle style_ = ActivityStyle::Playful;
    Random random_ = systemRandom();
    std::function<qint64()> clock_;
    qint64 nextDue_ = -1; // -1 outside an activity state.
    QString last_; // The previous alternate, not drawn twice in a row.
};
}
```

`src/animation/activity.cpp`:

```cpp
#include "activity.h"
#include <QDateTime>

namespace pet {
Activity::Activity(Player &player, QObject *parent)
    : QObject(parent), player_(player), clock_([] { return QDateTime::currentMSecsSinceEpoch(); }) {
    connect(&player_, &Player::entered, this, &Activity::entered);
    connect(&player_, &Player::looped, this, &Activity::looped);
    player_.setContinuity(style_ != ActivityStyle::Classic);
    player_.setReactionGate([this] { return react(); });
}
Activity::~Activity() { player_.setReactionGate({}); }
QPair<int, int> Activity::gapSeconds(ActivityStyle style) {
    return style == ActivityStyle::Playful ? QPair<int, int>{6, 12} : QPair<int, int>{10, 18};
}
void Activity::setStyle(ActivityStyle style) {
    if (style == style_) return;
    style_ = style;
    player_.setContinuity(style != ActivityStyle::Classic);
    if (nextDue_ >= 0) nextDue_ = style == ActivityStyle::Classic ? 0 : clock_() + gapMs(); // A new pace starts from now.
}
void Activity::setClock(std::function<qint64()> milliseconds) {
    if (milliseconds) clock_ = std::move(milliseconds);
}
qint64 Activity::gapMs() {
    const auto [low, high] = gapSeconds(style_);
    return qint64(low + qBound(0, random_(high - low + 1), high - low)) * 1000;
}
void Activity::entered(const QString &state) {
    // One clock runs from the first activity to the last, across the transitions between them.
    if (!player_.activity(state)) nextDue_ = -1;
    else if (nextDue_ < 0) nextDue_ = style_ == ActivityStyle::Classic ? 0 : clock_() + gapMs();
}
void Activity::looped(const QString &state) {
    const auto *art = player_.activity(state);
    if (style_ == ActivityStyle::Classic || !art || nextDue_ < 0 || clock_() < nextDue_) return;
    QVector<const ActivityChoice *> pool;
    for (const auto &loop : art->loops)
        if (!loop.playful || style_ == ActivityStyle::Playful) pool.append(&loop);
    if (pool.size() > 1) pool.removeIf([this](const ActivityChoice *loop) { return loop->sequence == last_; });
    if (!pool.isEmpty()) {
        int total = 0;
        for (const auto *loop : pool) total += loop->weight;
        int roll = qBound(0, random_(total), total - 1);
        const ActivityChoice *pick = pool.first();
        for (const auto *loop : pool) {
            if (roll < loop->weight) { pick = loop; break; }
            roll -= loop->weight;
        }
        if (player_.vary(pick->sequence)) last_ = pick->sequence;
    }
    nextDue_ = clock_() + gapMs();
}
bool Activity::react() {
    if (style_ != ActivityStyle::Playful || nextDue_ < 0 || clock_() < nextDue_) return false;
    nextDue_ = clock_() + gapMs();
    return true;
}
}
```

In `CMakeLists.txt`, change the `pet_animation` sources:

```cmake
qt_add_library(pet_animation STATIC
    src/animation/player.cpp src/animation/ambient.cpp src/animation/activity.cpp src/animation/mood.cpp
    src/animation/easter_eggs.cpp)
```

- [ ] **Step 4: Run the new tests and see them pass**

```bash
cmake --build build -j 4 && QT_QPA_PLATFORM=offscreen ./build/prototype-tests activityAlternatesOnePassAtATime \
  activityReactionsShareTheClock classicActivityIsTodaysPlayback activityStyleChangesMidDesk activityWithoutArtIsClassic
```

Expected: all pass.

- [ ] **Step 5: Run every suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add src/animation/activity.h src/animation/activity.cpp CMakeLists.txt tests/prototype_tests.cpp
git commit -m "feat(animation): pace active alternates and reactions (#47)"
```

---

### Task 5: Preference, PetWindow and Settings

**Files:**
- Modify: `src/settings/preferences.h`, `src/settings/preferences.cpp`
- Modify: `src/desktop/pet_window.h`, `src/desktop/pet_window.cpp`
- Test: `tests/prototype_tests.cpp`

**Interfaces:**
- Consumes: `Activity`, `ActivityStyle` (Task 4).
- Produces: `Preferences::activity` (`enum Activity { ActivityClassic, ActivitySubtle, ActivityPlayful }`); `PetWindow::activity()`, `setActivityStyle(int)`, `activityStyle()`; Settings combo with accessible name `"Active animation"`.

- [ ] **Step 1: Write the failing tests**

Add after `ambientPreference`:

```cpp
    void activityPreference() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        {
            pet::PetWindow window(nullptr, path);
            QCOMPARE(window.activityStyle(), int(pet::Preferences::ActivityPlayful)); QVERIFY(window.player().continuity());
            window.showSettings(); auto *dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
            QComboBox *combo = nullptr;
            for (auto *box : dialog->findChildren<QComboBox*>()) if (box->accessibleName() == "Active animation") combo = box;
            QVERIFY(combo); QCOMPARE(combo->count(), 3); QCOMPARE(combo->currentIndex(), 2);
            QVERIFY(combo->itemText(0).startsWith("Classic")); QVERIFY(combo->toolTip().contains("never delays alerts"));
            combo->setCurrentIndex(0); // Live.
            QCOMPARE(window.activity().style(), pet::ActivityStyle::Classic); QVERIFY(!window.player().continuity());
            QVERIFY(window.savePreferences()); dialog->close();
        }
        QCOMPARE(pet::PreferencesStore(path).load().activity, int(pet::Preferences::ActivityClassic));
        pet::PetWindow restored(nullptr, path); QCOMPARE(restored.activity().style(), pet::ActivityStyle::Classic);
        // Independent of the idle animation, both ways.
        restored.setAmbientLevel(0); QCOMPARE(restored.activityStyle(), 0);
        restored.setActivityStyle(1); QCOMPARE(restored.ambientLevel(), 0); QVERIFY(restored.player().continuity());
        restored.setActivityStyle(99); QCOMPARE(restored.activityStyle(), 2);
        // Older files have no key; a bad value is refused like any other and preserved.
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true})"); file.close();
        pet::PreferencesStore legacy(path); QCOMPARE(legacy.load().activity, int(pet::Preferences::ActivityPlayful));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":1,"size":200,"on_top":true,"activity":3})"); file.close();
        pet::PreferencesStore invalid(path); QCOMPARE(invalid.load().activity, int(pet::Preferences::ActivityPlayful));
        QVERIFY(!invalid.save(pet::Preferences{}));
    }
    void monitorLeavesTheDeskAlone() {
        QTemporaryDir directory;
        pet::PetWindow window(nullptr, directory.path() + "/preferences.json"); window.show();
        pet::Monitor monitor(window); auto &player = window.player(); player.setPaused(true);
        player.setRandom([](int) { return 0; });
        window.activity().setRandom([](int) { return 0; }); window.activity().setClock([] { return qint64(0); }); // Never due.
        const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 seq = 0;
        auto event = [&](QString kind, qint64 at, QString tool = {}, QString activity = {}) {
            ++seq; return pet::Event{"claude", "s1", QString::number(seq), kind, tool, {}, "/work/abc-web", activity, at, {}};
        };
        QVERIFY(monitor.apply(event("prompt", now), now));
        QVERIFY(monitor.apply(event("tool_start", now + 1, "t1", "reading"), now + 1));
        for (int i = 0; i < 6 && !(player.state() == "reading" && player.phase() == "loop"); ++i) finishSequence(player);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("loop"));
        // The tool ends; after the activity hold the aggregate is thinking, and the pet stays at its book.
        QVERIFY(monitor.apply(event("tool_end", now + 2, "t1"), now + 2));
        monitor.update(now + 5002);
        QCOMPARE(player.state(), QString("reading")); QCOMPARE(player.phase(), QString("linger"));
        QCOMPARE(player.requestedState(), QString("thinking"));
        // The periodic update sees its request honoured and restarts nothing.
        const auto sequence = player.sequence(); player.advance(); const int frame = player.frameIndex();
        monitor.update(now + 6000);
        QCOMPARE(player.phase(), QString("linger")); QCOMPARE(player.sequence(), sequence); QCOMPARE(player.frameIndex(), frame);
        // The next tool picks the book back up: no end, no start.
        QVERIFY(monitor.apply(event("tool_start", now + 7000, "t2", "reading"), now + 7000));
        QCOMPARE(player.phase(), QString("loop")); QCOMPARE(player.sequence(), QString("WORK/Study/B_1_Nomal"));
        // A request for the user still cuts in at once.
        QVERIFY(monitor.apply(event("attention", now + 7001), now + 7001));
        QCOMPARE(player.state(), QString("needs_input"));
    }
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build -j 4 2>&1 | tail -5`
Expected: compile errors, `'ActivityPlayful' is not a member of 'pet::Preferences'`, `no member named 'activityStyle'`.

- [ ] **Step 3: Implement the preference**

`src/settings/preferences.h`, after `int ambient = AmbientSubtle;`:

```cpp
    // How sustained thinking, reading and working vary: one loop each as before, calm alternates with
    // desk continuity, or also pen spinning and small reactions. Independent of `ambient`.
    enum Activity { ActivityClassic = 0, ActivitySubtle = 1, ActivityPlayful = 2 };
    int activity = ActivityPlayful;
```

`src/settings/preferences.cpp`, in the validation chain after the `ambient` line:

```cpp
        || (object.contains("activity") && !integer(object["activity"], 0, 2))
```

after `result.ambient = …;`:

```cpp
    result.activity = object["activity"].toInt(Preferences::ActivityPlayful);
```

and in `save`, change `{"ambient", preferences.ambient},` to `{"ambient", preferences.ambient}, {"activity", preferences.activity},`.

- [ ] **Step 4: Wire it into `PetWindow`**

`src/desktop/pet_window.h`: add `#include "animation/activity.h"` before `#include "animation/ambient.h"`. After `int ambientLevel() const …;`:

```cpp
    Activity &activity() { return activity_; }
    void setActivityStyle(int style); // Preferences::Activity; persisted.
    int activityStyle() const { return int(activity_.style()); }
```

After the member `Ambient ambient_;`:

```cpp
    Activity activity_;
```

`src/desktop/pet_window.cpp`:

- Initializer list: `ambient_(player_, this), activity_(player_, this), mood_(player_, this), …`
- After `ambient_.setLevel(AmbientLevel(qBound(0, preferences.ambient, 2)));`:

```cpp
    activity_.setStyle(ActivityStyle(qBound(0, preferences.activity, 2)));
```

- After `PetWindow::setAmbientLevel`:

```cpp
void PetWindow::setActivityStyle(int style) {
    style = qBound(0, style, 2);
    if (style == activityStyle()) return;
    activity_.setStyle(ActivityStyle(style));
    if (ready_) saveTimer_.start();
}
```

- In `writePreferences`, change the ambient line to:

```cpp
    preferences.ambient = ambientLevel(); preferences.activity = activityStyle(); preferences.mood = moodLevel(); preferences.turns = mood_.turns();
```

- In `showSettings`, right after `connect(ambient, &QComboBox::currentIndexChanged, this, &PetWindow::setAmbientLevel);`:

```cpp
    auto *activity = new QComboBox(dialog);
    activity->addItems({"Classic (one loop per activity, as before)",
                        "Subtle (calm variations; stays at the desk for short thinking pauses)",
                        "Playful (also pen spinning and small reactions)"});
    activity->setCurrentIndex(activityStyle()); activity->setAccessibleName("Active animation");
    activity->setToolTip("How the pet thinks, reads and works while an agent is busy: alternate loops now and then,\n"
                         "staying at its desk through short thinking pauses, and (Playful) small reactions.\n"
                         "Independent of Idle animation. Requests, errors and finished turns still show at once:\n"
                         "it never delays alerts.");
    layout->addRow("&Active animation", activity);
    connect(activity, &QComboBox::currentIndexChanged, this, &PetWindow::setActivityStyle);
```

- [ ] **Step 5: Run the new tests and see them pass**

```bash
cmake --build build -j 4 && QT_QPA_PLATFORM=offscreen ./build/prototype-tests activityPreference monitorLeavesTheDeskAlone
```

Expected: both pass.

- [ ] **Step 6: Run every suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass. Window-based tests now run Playful with the real clock; gaps are at least 6 s, so only continuity can show up within a test. If a window test fails because a reading→thinking or reading↔working change now lingers or hands over, add `window.activity().setStyle(pet::ActivityStyle::Classic);` right after that test constructs its window (the test is about something else), and say so in the commit message.

- [ ] **Step 7: Commit**

```bash
git add src/settings src/desktop/pet_window.h src/desktop/pet_window.cpp tests/prototype_tests.cpp
git commit -m "feat(settings): add the Active animation preference (#47)"
```

---

### Task 6: Documentation and manual evidence

**Files:**
- Modify: `docs/architecture.md`, `README.md`

**Interfaces:**
- Consumes: everything above.
- Produces: docs only.

- [ ] **Step 1: README section**

Insert after the `## Idle animation` section, before `## Mood`:

```markdown
## Active animation

While an agent thinks, reads or works, the pet no longer repeats one short loop.
Settings → **Active animation** chooses how: **Classic** (one loop per activity, as
before), **Subtle** (a calmer variation every 10–18 seconds; a short thinking pause
between tools keeps it at its desk, and switching between reading and working swaps
book and pen without getting up) or **Playful** (the default: variations every 6–12
seconds, pen spinning, and now and then a small happy reaction when it gets to work).
It is independent of Idle animation. Requests, errors, finished turns, pausing and
dragging always take over at once. See
[the architecture notes](docs/architecture.md#active-animation).
```

- [ ] **Step 2: Architecture design section**

Insert after the `## Idle animation` section, before `## Mood`:

```markdown
## Active animation

[Issue 47](https://github.com/WindyWin/agent-pet/issues/47). The optional `activity`
section of `animations.json` decorates states that end only when asked (phased, no
`loops`, not idle, dragging, a fidget or a touch state); the shipped catalog decorates
`thinking`, `reading` and `working`. Decoration changes which sequence plays inside a
state, never the state itself, and a state without an entry plays as before.

- `loops`: weighted alternate loop passes, `style` `subtle` (default) or `playful`.
- `enter`: a first loop pass drawn from `choices` when entered from a state in `from`.
- `exit`: per target state, an end sequence that replaces the usual one.
- `linger`: a non-urgent request for `to` keeps the pet at its desk: `in`, passes of
  `loop` (never the same twice in a row), then `out`. It ends at the first pass
  boundary at or after `max_s` (1–60) and goes on through the state's end, as before.
  Linger time counts frames shown, so a paused pet never runs it down.
- `handover`: per target state, a sequence that swaps props at the desk and enters the
  target's loop directly.

Composite sequences under `WORK/Desk/` reuse frames already in the pack: the two
handovers swap book and paper while both are raised, and `ponder_*` is the chin-in-hand
start of the pen-spin sequence.

`Player` owns the mechanics. `looped` is now also emitted for every new pass of a phased
loop, and `vary(sequence)` swaps one alternate into that pass (it reverts at the next).
`setContinuity` switches linger and handover; while one plays, `phase()` reports
`linger` or `handover`, and `requestedState()` already reports the request, so
`Monitor` sees it honoured and changes nothing. `setReactionGate` is asked whenever an
`enter` or `exit` could play. Resuming the lingering state returns to its base loop;
asking for a handover target plays `out` then the handover; anything else cuts to the
end at once. Urgent selects, `hold` and pause act exactly as before.

`Activity` (`src/animation/activity.*`) paces it. One opportunity clock runs from the
first activity state to the last; when it is due, the next loop pass plays an alternate
(Subtle: `subtle` tier every 10–18 s; Playful: both tiers every 6–12 s), and in Playful
the gate spends the same opportunity on a reaction instead, so a transition gets at most
one reaction and quick tool calls do not each get one. Classic draws nothing and leaves
continuity off. The style is the `activity` preference (default Playful), independent
of Idle animation, and takes effect at once. `Think/Happy` and `WorkONE/Happy` art is a
Playful expression here, not mood art.
```

- [ ] **Step 3: Verify assets, build and run all tests**

```bash
python3 scripts/verify_assets.py
cmake --build build -j 4 && ctest --test-dir build --output-on-failure
```

Expected: `OK: …`; all suites pass. Keep both outputs for the evidence section.

- [ ] **Step 4: Manual run under each style**

```bash
./build/agent-pet --no-persist &
emit() { printf '%s\n' "$1" | ./build/agent-pet emit; }
emit '{"version":1,"provider":"claude","session_id":"demo","kind":"prompt"}'
sleep 20   # sustained thinking: alternates every 6–12 s under Playful
emit '{"version":1,"provider":"claude","session_id":"demo","kind":"tool_start","tool_id":"t1","activity":"reading"}'
sleep 15   # sustained reading
emit '{"version":1,"provider":"claude","session_id":"demo","kind":"tool_end","tool_id":"t1"}'
sleep 6    # 4 s hold, then thinking: the pet should linger at its book
emit '{"version":1,"provider":"claude","session_id":"demo","kind":"tool_start","tool_id":"t2","activity":"working"}'
sleep 4    # handover from book to pen, no end/start
for i in 1 2 3 4 5 6; do   # rapid alternation
  emit "{\"version\":1,\"provider\":\"claude\",\"session_id\":\"demo\",\"kind\":\"tool_end\",\"tool_id\":\"t$((i+1))\"}"
  emit "{\"version\":1,\"provider\":\"claude\",\"session_id\":\"demo\",\"kind\":\"tool_start\",\"tool_id\":\"t$((i+2))\",\"activity\":\"$([ $((i%2)) = 0 ] && echo reading || echo working)\"}"
  sleep 1
done
emit '{"version":1,"provider":"claude","session_id":"demo","kind":"attention","reason":"approval"}'   # must show at once
sleep 3
emit '{"version":1,"provider":"claude","session_id":"demo","kind":"turn_finished"}'
```

Repeat with the style set to Subtle and to Classic in Settings (right-click → Settings → Pet). Watch the right-click → More → Preview dialog's state/phase/sequence line to confirm `linger` and `handover`. Note per style: whether alternates appear and how often, linger on short pauses, handovers, reactions (Playful only), and that attention shows at once.

- [ ] **Step 5: Evidence section**

Append after `## Wellness reminders evidence — 2026-10-06` (before `## Application updates`), with the numbers taken from Steps 3–4 and Task 1 Step 1:

```markdown
## Active animation evidence — 2026-10-06

Imported 13 sequences (126 frames, about 15.8 MB) with `scripts/add_sequences.py`:
`Think/Nomal/B_2`–`B_5`, `Think/Happy/B_3`, `B_4`, `C_2`, `WORK/Study/B_2_Nomal`–`B_4_Nomal`,
`WORK/WorkONE/B_2_Nomal`, `WORK/WorkONE/Happy/B` and `WORK/WorkONE/B/Nomal/1`, plus five
composite `WORK/Desk/*` sequences that reuse existing frames. `verify_assets.py` now
checks the `activity` section, counts its sequences as used and owned, takes an optional
catalog path, and refused six broken variants (bad style, linger to idle, `max_s` 61, an
idle entry, an unused handover sequence, an unknown sequence).

Validation: <the ctest summary line from Step 3>. New prototype tests cover catalog
validation, phased `looped` and `vary`, every alternate decoding, linger (resume, expiry
by frames shown, in/out), handover (also from a linger), the reaction gate, alerts and
drags cutting decoration at once, a 3,000-step random request sequence always reporting
the latest request, Activity pacing per style with no repeats and one reaction per
transition, Classic matching unpaced playback sequence for sequence, style changes
mid-desk, a catalog without the section, the persisted preference and settings combo,
and the monitor leaving a linger alone.

Manual run (`./build/agent-pet --no-persist`, `agent-pet emit`): <what Step 4 showed per
style: alternates and their pace, linger, handover, reactions, attention at once>.
```

Replace both `<…>` with what was actually observed before committing; do not commit the brackets.

- [ ] **Step 6: Commit**

```bash
git add README.md docs/architecture.md
git commit -m "docs: describe active animation styles (#47)"
```

---

## Self-Review

- **Spec coverage:** styles and gaps → Task 4; artwork and composites → Task 1; catalog rules → Tasks 1 (Python) and 2 (C++); playback (loop passes, `vary`, enter, exit, handover, linger and its outcomes) → Task 3; pacing (shared clock, no repeats, tiers, gate) → Task 4; preference and settings → Task 5; responsiveness → Task 3 `decorationNeverDelaysAlerts` and Task 5 monitor test; testing items 1–9 → Tasks 1–5; docs and evidence → Task 6.
- **Types:** `ActivityChoice`, `ActivityLinger`, `ActivityArt`, `Player::activity/setContinuity/continuity/setReactionGate/vary`, `ActivityStyle`, `Activity::setStyle/style/gapSeconds/setRandom/setClock`, `Preferences::activity`, `PetWindow::activity/setActivityStyle/activityStyle` are used with the same names throughout.
- **Library name:** the animation sources build into `pet_animation` in `CMakeLists.txt` (CLAUDE.md groups it under `pet_ui`).
