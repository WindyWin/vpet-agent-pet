# 0040. Break Buddy and animated meal reminders

- Status: Accepted
- Date: 2026-10-13

## Context

The unused VPet archive has jump-rope animations and layered eating/drinking art.
The user requested a movement companion and eating/drinking animations alongside
the existing meal and water reminders, rather than a separate feeding economy.

## Decision

- Add an independent movement timer to `Wellness`, defaulting to 60 active minutes,
  with Off, 30, 60 and 90 minute choices in Settings. It follows the same presence,
  lock, quiet-hours, Later, re-remind, Skip today and snooze rules as other reminders.
  Eye reminders have priority, then water, then movement.
- The movement question offers **Start break**, **Later** and **Skip today**.
  Starting it shows a 60-second break with a static note. **Take a movement break** in the pet/tray
  menu starts the same break even when automatic movement reminders are off.
  VPet jumps rope alongside it; users can move or stretch as they choose.
- `movement-break` is an optional reaction cue and a reminder trigger, falling back
  to `eye-break`. `break-buddy` is a surprise trigger drawing from `movement-break`,
  then `celebrate` for other pets. During a break, completed exercise reactions
  request another turn through the runtime. Each reaction stays below its existing
  15-second timeout; the break timer owns the full duration; its note shows no ticking number.
- Clicking the note, a lock, mute/snooze, hiding, dragging, an urgent session,
  another note or shutdown ends it without cheering. Completed breaks cheer and
  restart the movement timer. A cancelled break is not replayed.
- Extend `Runtime::withdraw` to interrupt a showing one-shot and return to the
  current session activity. It still cannot cancel shutdown. Clearing break
  state before withdrawal prevents cancellation outcomes from restarting it.
- Map VPet's `lunch` and `water` cues to composed eating/drinking sequences. Preserve
  the existing Done/Later/Skip today flow and configurable lunch/water timing.
  Productivity snack reactions no longer count as an answered water reminder.

## Artwork

Import the neutral `WORK/RopeSkipping` entry, loop and exit (24 original frames).
`scripts/compose_vpet_reminders.py` uses ImageMagick 7 to compose the original back
layer, an original sandwich/water-glass SVG, and the front layer in that order.
It reads `info.lps`, interpolates item poses, converts the upstream 500-unit space
to the 1000-pixel canvas, and includes every character-frame boundary. Consecutive
identical frames are merged. The result is 59 eating and 42 drinking PNG frames,
playable by the existing renderer, with no new runtime dependency.

The script records durations and SHA-256 hashes in the existing catalog/manifest.
The source layers remain listed as unbundled: only the composed derivative is
distributed. VPet character art retains its existing artwork terms; the SVG item
drawings in the script use the application's Apache-2.0 license.

## Consequences

- Settings gain `movement_minutes`; older files default to 60. Timers
  remain in memory, and other pets remain compatible through optional cue fallbacks.
- VPet now has 146 sequences and 1,322 unique PNG files (193.79 MiB).
  419 original sequences and 4,277 frames remain unbundled.
- Meals currently use neutral artwork and one sandwich/glass; there is no inventory,
  purchase flow, hunger meter, or new eating-reminder schedule.

## Validation

### Initial evidence — 2026-10-13

- Visually checked composed eating/drinking frames, including item scale, placement
  and front-layer occlusion.
- Asset verifier accepts all pets and the updated VPet manifest/catalog.
- Focused prototype tests cover independent timers, settings persistence, delayed
  and skipped movement reminders, animation repetition, completion, manual starts
  with automatic reminders off, approval priority, lock, snooze and dragging.
- Behavior tests cover withdrawal of a showing reaction, stale completion feedback,
  restoration of session activity and shutdown remaining non-cancellable.

- Built successfully with Qt 6.11.2. All four prototype CTest shards, pet contracts,
  i18n, behavior, focus, alerts, scaffold and update checks passed. Provider/event
  transport and update-install checks passed outside the sandbox, which blocks
  local socket binding. The refreshed translation catalog is unchanged by lupdate
  and contains no unfinished entries. Plugin and startup suites passed on their
  final reruns as well.
