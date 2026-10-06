# 0007. Monitor and alert presentation

- Status: Accepted
- Date: 2026-10-04

## Context

Sessions waiting on the user must be noticed without stealing focus, and
`main.cpp` wired the receiver, session state and alert presentation inline.

## Decision

`Monitor` (`src/desktop/monitor.cpp`) now owns the receiver, session state and
alert presentation that `main.cpp` previously wired inline. It feeds aggregate
playback, the badge and an `AlertBubble`: a separate frameless, non-activating
top-level window placed right of the character, else left, above or below,
clamped to the pet's screen and moved with the pet. The pet's Quit emits
`quitRequested`, which stops the timer, closes the socket and hides the bubble;
closing settings or preview does not touch the monitor. Mute and sound are new
optional keys in `preferences.json`; files written before M5 still load.

## Consequences

- `Monitor` is the single owner of monitoring; its native queries are
  `std::function`s that tests replace.
- The bubble is a separate non-activating window, so it never takes focus from the
  user's application.

## Validation

### M5 implementation evidence — 2026-10-04

Validation in a cloud Ubuntu 24.04 container with Qt 6.4.2 (the build
requirement stays Qt 6.5; the temporary local override was not committed):

- CTest passed all four suites (`providers`, `events`, `alerts`, `prototype`).
  `alerts` replays concurrent sessions across providers, folder-name
  collisions, short-ID collisions, preemption versus a kept Next position,
  dismissal with attention retained, missed/late/ended/expired sessions,
  restart and the 64-alert bound. `prototype` runs an offscreen pet with the
  monitor: bubble text and footer, badge count, mute, dismissal, settings
  close with continued monitoring, quit stopping monitoring, edge placement
  and preference compatibility. Provider fixtures now assert attention reasons.
- The real binary under Xvfb received three `emit`ted events and showed
  "Needs approval / abc-web (work) · Claude Code · a1b2 / 2 more alerts" with
  the badge, beside the character.
- `--smoke-test` passed offscreen; asset verification passed (215 PNGs).

Not covered here: audible sound, a compositor-backed desktop (Xvfb has no
translucency), native X11/Wayland, and packaging (`qmake6`/`patchelf` were not
installed). Live-client acceptance from M4 is still open.
