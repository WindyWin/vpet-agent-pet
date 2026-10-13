# 0039. Whale Girl, a pet from a PNG animation pack

- Status: Accepted
- Date: 2026-10-10
- Updated: 2026-10-12

## Context

Whale Girl originally used the `deepseek-whalechan--qimi` Codex spritesheet: 73 extracted frames
on 500 × 500 canvases. The local `pet-sources/packs/whale-png` collection contains a much more
detailed character with full motion sequences. Replace the existing pet with that pack while
retaining its id and Settings selection.

## Decision

- Use **Multimodal-AI-Companion-Assets v1.0.0**. Import all **1,662 animation frames across 16
  actions**; the seventeenth upstream entry is a low-resolution static fallback, which is not used.
- Credit the original character **「溟月」 by 上善无形**, whale-maid design **ZipZipPipe**, and animation
  producer **Multimodal-AI-Companion**. Preserve the source license and notice in
  `licenses/WHALE-GIRL-ARTWORK-TERMS.md`; the adapted artwork is **CC BY-NC-SA 4.0**.
- Keep `scripts/import_whale_girl.py` as a repeatable importer. The ignored source pack is read-only.
  The archive checksum and metadata revision are recorded in the artwork terms.
- Preserve the source pixels on **600 × 600** transparent canvases with a shared ground line.
  Align each frame's visible feet, retain horizontal alignment and apply the supplied jump/roll
  trajectories, including the upstream floor clamp for rolls. Reject frames that would be clipped.
- Use the upstream player's **40 ms interval (25 fps)** for motion. These clips are complete
  performances, not authored animation phases: thinking holds frame 75, sleep frame 65, and shy
  and surprise frame 45. Entry plays up to that pose once; the persistent loop is the same image
  for 1,000 ms; exit plays the remaining frames. This retains the gesture without restarting it
  whenever the agent remains busy. Reading and working share thinking art because this pack has
  no reading or typing performance. Jump, shy and surprise reactions play the full clip once.
- The full crying clip is 4.28 s, longer than the session engine's 4 s error window. The error cue
  skips 16 neutral lead-in frames (640 ms), so the entire crying gesture finishes in 3.64 s and
  returns to the previous activity once. The full 107-frame clip remains available in the preview.
  Repeated aggregate error ticks do not replay it. No session-engine timing is changed.
- Keep the id `whale-girl`. The pack downloads when chosen; a local build produces its resources
  beside the executable. Updated frame hashes and catalog give the replacement a new pack tree.

| Cue | State | Source action |
| --- | --- | --- |
| idle, waiting | `idle` | settled seated frames from the end of 坐下; gentle forward/reverse loop |
| thinking, working | `think`, `work` | thinking / 思考 |
| reading | `read` | thinking / 思考; held pose |
| attention | `alert` | surprise / 惊讶 |
| error | `oops` | crying / 哭泣, after its neutral lead-in; once, then previous state |
| exhausted | `drained` | holds the seated pose at the end of 坐下 |
| turn finished, start, reminder done | `cheer`, `greet` | happy / 开心 |
| celebrate, milestone | `cheer`, `jump`, `dance` | happy, jumping / 跳跃, dancing / 跳舞 |
| quit | `bye` | back / 背面 |
| annoyed, quit-angry | `pout` | seated angry / 坐地上生气 |
| nap, inactive | `nap` | standing asleep / 站着睡着 |
| drag | `held` | shy / 害羞, with its own held state |
| danger | `startle` | surprise / 惊讶, once |
| snack, lunch, water | `shy` | shy / 害羞; this pack has no food prop |

Idle uses the last 16 settled frames of the sit-down clip, played forward then backward with
800 ms holds at each end. It never repeats the sit-down gesture; the old front clip remains
available as `stand` in the preview.

Ambient fidgets use shy, roll / 打滚, jump and dance poses. The `rest` preview plays sit / 坐下 → holds
the final seated pose for one second → rise / 起身 once, then returns to idle. It is excluded from
ambient fidgets while the default idle is already seated. Side and back are
walking cycles and are excluded from idle fidgets; turn is excluded too. Rolls, jumps and dancing
are rare fidgets. Long idle periods hold the sleeping pose after its entry gesture.

## Consequences

- Existing Whale Girl preferences select the replacement without changing ids or application code.
- More detailed animation increases the artwork size from about 13 MB to about 250 MiB. It remains
  an on-demand pet. The frame decoder loads frames during playback rather than keeping every
  decoded frame in memory.
- This pack has no notebook or cup action; the cue table records the chosen fallbacks.
- Jump and roll motion is baked into the artwork. These reactions do not move the desktop window.
- Artwork remains non-commercial and now explicitly carries share-alike terms. The upstream
  attribution and permission chain is preserved as stated by the publisher.

## Validation

The `whaleGirlHoldsItsPoseAndPlaysErrorsOnce` pet test advances the real generated catalog through
long thinking, reading, working, attention and nap states, checks the unchanged held pose, checks
exit to idle, and verifies one complete error reaction finishes within four seconds and does not
replay on repeated aggregate ticks. It also rejects walking states among idle fidgets.

The importer checks source counts, numbered frame order, trajectory lengths, frame dimensions and
clipping before replacement, then validates the generated catalog and manifest. Run:

```bash
python3 scripts/import_whale_girl.py
python3 scripts/verify_assets.py
cmake --build build -j4
ctest --test-dir build -R '^(pets|pet-scaffold)$' --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/agent-pet --pet whale-girl --smoke-test
```

The importer requires Pillow. The full source pack is local, ignored by Git, and is not required
when building from the generated assets. The existing pet is backed up in a temporary directory
before replacement.
