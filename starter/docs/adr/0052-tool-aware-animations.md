# 0052. Tool-aware activity animations

- Status: Accepted
- Date: 2026-10-17

## Context

Every provider hook already knows which tool an agent just started, but the app
flattened that knowledge to two classes on the wire: `reading` for a handful of
read-only tools and `working` for everything else. The pet therefore looked the
same whether the agent was running a shell command, editing files, delegating a
subagent, browsing the web, or searching the tree. The information was there and
thrown away at the adapter boundary.

## Decision

Widen the `activity` the adapters send to a small, ordered class vocabulary:
`executing`, `editing`, `delegating`, `browsing`, `searching`, `working`,
`reading`. `working` remains the fallback for anything unrecognized, MCP tools
included. The mapping stays in the adapter (case-insensitive on the tool name)
and only the class travels; the wire field is unchanged, its allowed values are
just validated against the wider list.

Each class is also a session state and an animation cue of the same name, so a
turn shows its most active tool: with several tools in flight the state machine
picks the highest class in that order. The aggregate priority inserts the tool
classes between `turn-finished` and `thinking`, keeping `attention`, `exhausted`,
`error` and `turn-finished` above them.

The vocabulary is expected to grow, so a cue may name a `fallback`. When a
catalog has no state for the cue, it plays the fallback cue's state instead of
failing the contract; the five new tool cues fall back to `working`. That keeps
every pet that has not drawn the new states valid without editing its catalog.
VPet draws all seven from sequences already in its archive: `StudyTWO`,
`Calligraphy`, `WorkTWO`, `StudyPaint` and `PlayONE` join `Study` and `WorkONE`.

## Consequences

- Pets other than VPet need no change: they inherit `working` through the cue
  fallback until someone draws the classes. Mapping one is a `cues` entry.
- The VPet pack grows by the imported sequences (about 29 MB); other pets are
  untouched and downloaded packs are unaffected.
- A turn that mixes a command and a read now shows executing, not reading. This
  is the intended ordering, and the 4 s hold still smooths back-to-back tools.
- `alerts.cpp`, `session_list.cpp` and `monitor.cpp` treat the tool classes as
  busy through the shared `activityClasses()`/`busy()` helpers instead of
  naming `working` and `reading`.
- The `activity` wire values are a closed set; adding a class touches the
  vocabulary, adapters, cues and VPet's catalog together.

## Validation

### Tool-aware states and tests — 2026-10-17

Linux, Qt 6.9 (offscreen). `python3 scripts/verify_assets.py` reports every pet
valid (VPet now 161 sequences). `ctest --test-dir build -j 4` passes all 17
suites, including the new `EventTests::toolActivities` (class ordering and the
`working` fallback) and the catalog fallback checks in `pets_tests.cpp`. The
five new cues raise the vocabulary to 40; `pets` and `prototype` shards were
updated for the new states.

### Pen spin lost after the split — 2026-10-20

The release that introduced these states dropped the chin-in-hand pen spin
(`WORK/WorkONE/B/Nomal/1`) from most turns. It is a `playful` loop in the
catalog's `activity` section, which is keyed by state name and held entries only
for `thinking`, `reading` and `working`; Bash, Edit, Grep, WebFetch and Task now
map to states without one. VPet's `activity` section now gives `editing`,
`executing`, `searching`, `browsing` and `delegating` the same playful loop, so
it plays again in the Playful style and nowhere else. `PrototypeTests::
toolStatesKeepThePenSpin` covers all five.

The clip is drawn on the bare WorkONE desk, so the monitor, scroll or other prop
of the current state is absent for its one pass. The `linger` and `handover`
parts of `working` (the ponder loop and the prop swaps under `WORK/Desk/`) are
WorkONE transitions and were not copied; going from a tool state to thinking
still plays the plain end.
