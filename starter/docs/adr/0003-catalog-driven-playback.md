# 0003. Catalog-driven animation playback

- Status: Accepted
- Date: 2026-10-04

## Context

The pet animates with unmodified VPet artwork: numbered PNG frame sequences with
start, loop and end parts. Agent activity maps onto a fixed set of display states.
Memory must stay bounded, and a broken or missing asset must never leave an
invisible pet.

## Decision

The catalog defines eleven display states: idle, thinking, reading, working,
needs_input, tool_error, turn_finished, sleeping, starting, closing and dragging.
`idle` loops; the activity states and dragging use start → held loop → end;
starting, error, finished and closing are one-shot sequences. Error returns to
the previous activity, finished/starting return to idle, and closing holds its
last frame until the application exits. Ordinary state changes finish an active
end sequence. Urgent changes interrupt it immediately. The preview can select
all states, pause, step frames and show the active sequence, phase and timing.

The 35 unmodified `Raise/` frames imported for dragging were checked against the
full archive's SHA-256 manifest. `Raise/Raised_Static/A_Nomal` starts the lift,
`Raise/Raised_Dynamic/Nomal/1` loops while the button is held, and
`Raise/Raised_Static/C_Nomal` lowers the pet before returning to the prior state.
On X11/XWayland a pointer query detects release even if the compositor consumes
the widget's mouse release event. Native Wayland still needs direct testing.

The current pixmap and an 8 MiB `QCache` hold decoded frames for the active
sequence. A sequence change clears the cache. Source dimensions above 2048 px
are rejected; images are scaled to at most 640 px before entering the cache.
A decode error stops the bad sequence and falls back to idle; if idle is broken,
a visible text placeholder and controls remain. Catalog paths, durations and
playback policies are validated before playback. This is a cache bound, not a
guarantee on process RSS, which also includes Qt, source decoding and graphics
memory.

## Consequences

- Behavior is data-driven from `assets/vpet/animations.json`; the player validates it
  at load, and later features extend the catalog with optional sections.
- The frame cache bounds decoded frames, not process RSS.
- Release detection during a drag relies on an X11 pointer query; native Wayland
  needs direct testing.

## Validation

### M2 validation evidence (2026-10-04)

The 215-frame asset verifier and automated playback/settings tests pass. The
user also confirmed the M2 desktop appearance. The
XWayland desktop test used XTest to drag the real window 90 by 50 pixels; it
observed the drag animation, actual window movement, release detection, and return
to working. Captured pet and preview windows are saved under `/tmp` for visual
inspection. A native X11 session and native Wayland behavior have not been checked.

The current packaged prototype is `dist/agent-pet-0.2.0-m2.tar.gz` (115 MiB
extracted) with artwork, notices and private Qt dependencies. Isolated runtime
checking passed in bubblewrap with only the package plus the host loader, libc
and libm visible. The package remains a development
artifact built against this host's glibc.
