# 0055. Bedtime animations

- Status: Accepted
- Date: 2026-10-25

## Behavior

From the configured Sleep reminder time (22:00 by default) until 06:00 local time,
VPet uses tired working art and a quieter completion: the full desk exit,
`LevelUP/PoorCondition`, then `IDEL/yawning/PoorCondition`. Once idle, it enters
`Sleep/A_Nomal` and loops `Sleep/B_Nomal` until work or interaction wakes it.
Continuing work returns through its full entrance. Urgent attention, errors and
quota stops interrupt completion or sleep immediately.

Settings → Pet → Follow bedtime animations controls this independently of
notifications, easter eggs, ambient animation and mood. Preferences store
`bedtime_animations`, defaulting to true for older files. The window crosses midnight;
a configured time before 06:00 covers only that morning's remaining interval. Setting
the start to 06:00 leaves an empty interval. Schedule changes apply on the next
monitor tick. This uses local wall time, so waking or restarting overnight requires
no remembered daily trigger.

## Presentation

The optional catalog `bedtime` section maps states to weighted sequence choices,
with the same playback shape as the state's normal art. It overrides mood art while
enabled without changing the mood score or selected mood. States without nighttime
choices retain their ordinary art. Custom pets need no new states or cues; their
existing `nap` mapping supplies sleep.

VPet provides nighttime choices for `working` and `turn_finished`. Other tool,
reading and thinking states retain their ordinary art. Nighttime working suppresses
daytime desk decorations so those cannot replace its tired loop or skip its exit.
Changing the schedule while working uses the current version's exit before entering
the new one. The rest of the daytime continuity system is unchanged.

The acknowledgment and yawn are one composite sequence referencing existing PNGs,
so no idle frame or independent animation request separates them. The behavior
runtime owns that sequence as a Celebration, returning to current activity after
completion. Milestone and snack rewards remain pending for a later daytime finish.
Only idle or background waiting admits the persistent bedtime nap, under the existing
Ambient policy. Sleep stays asleep until interrupted; its exit runs on an ordinary
wake and urgent activity cuts directly to attention.

## Assets

Imported `WORK/WorkONE/PoorCondition/{A,B,c}` and `LevelUP/PoorCondition` from
the local source archive: 56 PNGs, approximately 8.14 MB. The already bundled yawn
and normal sleep phases complete the sequence. The subdued acknowledgment still
has blush and small sparkles; it is not a newly drawn sleepy nod.

Run `python3 scripts/compose_bedtime.py` after importing those sequences to recreate
the composite and mappings. It reuses frames without copying pixels and preserves
other catalog sections. `sleepy_acknowledge` keeps the source acknowledgment
independently available in animation previews.

## Validation

Prototype coverage checks local schedule boundaries, midnight, early-morning starts,
settings persistence and malformed preferences; optional catalog validation and
custom-pet fallback; mood independence, full desk transitions, composite frame
boundaries, completion into sleep or other work, urgent interruption, and waking at
06:00 or when disabled. Asset verification covers the new mappings and frame manifest.

### Linux evidence — 2026-10-25

The Qt 6.11.2 build passes. All four prototype shards, behavior, plugins and i18n
pass together after rebuilding the combined workspace changes (7/7 CTest suites).
All six new bedtime cases also pass together. `verify_assets.py` validates all bundled assets and the new
bedtime choices. Pet contract, cue playback and hash-tree checks pass; three
pet-import checks pass when allowed to use Qt's external test-data directories.
The broader pet suite retains one workspace-specific failure: `bundledPetsMeetTheContract`
expects only VPet, while the existing local assets also include Bocchi.
