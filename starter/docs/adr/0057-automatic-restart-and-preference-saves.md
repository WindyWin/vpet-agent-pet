# 0057. Automatic restart for startup-only changes, and reliable preference saves

- Status: Accepted
- Date: 2026-11-04

## Context

On Windows a user chose another pet, restarted Agent Pet, and the old pet came back. Two defects explain it.
`PreferencesStore::load` set `writable_ = false` when the file could not be opened, and never set it back. On
Windows a scanner, a hook process or `agent-pet autostart` can hold `preferences.json` for a moment, so one
failed load made every later `save` fail for the rest of the run. `PetWindow::setPet` ignored the result, so
the choice was lost without a message. `QSaveFile::commit` can also fail briefly on a sharing violation.

Separately, a chosen pet, an installed or removed pack and a plugin change are read only at startup, so
Settings told the user to restart by hand.

## Decision

- `load` judges the file afresh each time (`writable_` and `error_` are reset) and retries a failed open a few
  times. `save` retries the replace a few times. A truly damaged file is still preserved, not overwritten.
- `PetWindow::setPet` returns whether the choice was saved and keeps the old pet when it was not. Settings
  warns the user in that case instead of promising a restart.
- `desktop/restart.h` lets startup-only changes ask for a restart. `main` installs the handler (persisting runs
  only) with a debounce timer; when it fires, sessions are checkpointed and `PetWindow::requestRestart` runs the
  normal quit. After the single-instance receiver and pack lock are released, `main` relaunches the executable
  with `launchDetached`. Several quick changes restart once, and choosing the running pet again cancels it.
- Without a handler (tests, `--no-persist`, `--smoke-test`) the old wording remains: restart by hand.
- A hot swap of the pet was rejected: `PetLibrary` holds one active pet per process and the player, catalog and
  stage are built around it.

## Consequences

The new run restores sessions from `sessions.json`, as an update restart does. The pet plays its quit
animation before relaunching. Command-line `--pet` is not carried over, so the saved choice wins.

## Validation

### Unit tests — 2026-11-04

`ctest --test-dir build -j 6` on Linux (Qt 6.12, offscreen): 35/35 passed, including `storeRecoversAfterAFailedLoad`
and `choosingAPetRestartsTheApp`. Not yet exercised on a Windows host; the sharing-violation cause is inferred
from the code, so confirm on Windows that the choice now survives and the relaunch works.
