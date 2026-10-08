# Architecture decision records

Each record states one design decision: the context that forced it, what was
decided, its consequences, and the dated evidence that validated it. Records are in
the order the decisions were made. For a map of the code, start with the
[architecture overview](../architecture.md).

| ADR | Decision | Date |
| --- | --- | --- |
| [0001](0001-desktop-stack.md) | Desktop stack: C++17, Qt 6 Widgets, CMake and Ninja | 2026-10-04 |
| [0002](0002-module-boundaries.md) | Module boundaries | 2026-10-04 |
| [0003](0003-catalog-driven-playback.md) | Catalog-driven animation playback | 2026-10-04 |
| [0004](0004-preferences-storage.md) | Atomic preferences storage | 2026-10-04 |
| [0005](0005-local-event-transport.md) | Local event transport and session engine | 2026-10-04 |
| [0006](0006-provider-adapters.md) | Provider adapters and integration management | 2026-10-04 |
| [0007](0007-alert-presentation.md) | Monitor and alert presentation | 2026-10-04 |
| [0008](0008-window-behavior.md) | Window behavior and recovery (with the manual acceptance checklist) | 2026-10-04 |
| [0009](0009-linux-packaging.md) | Relocatable Linux package and per-user install | 2026-10-04 |
| [0010](0010-application-updates.md) | Verified component updates | 2026-10-04 |
| [0011](0011-idle-animation.md) | Idle variants and ambient fidgets | 2026-10-04 |
| [0012](0012-mood.md) | Mood and celebrations | 2026-10-05 |
| [0013](0013-touch-reactions.md) | Touch reactions | 2026-10-05 |
| [0014](0014-artwork-resource-file.md) | Artwork in a separate resource file | 2026-10-05 |
| [0015](0015-easter-eggs.md) | Easter eggs | 2026-10-05 |
| [0016](0016-walking.md) | Walking, crawling and climbing | 2026-10-05 |
| [0017](0017-session-focus.md) | Session focus: host registry, focus service and desktop backends | 2026-10-05 |
| [0018](0018-daily-recap.md) | Daily recap | 2026-10-06 |
| [0019](0019-wellness-reminders.md) | Wellness reminders | 2026-10-06 |
| [0020](0020-macos-port.md) | macOS port | 2026-10-06 |
| [0021](0021-active-animation.md) | Active animation styles | 2026-10-06 |
| [0022](0022-localization.md) | Localization: English and Vietnamese, switched live | 2026-10-06 |
| [0023](0023-arch-aur-package.md) | Arch Linux package for the AUR, built from a starter-only source tarball | 2026-10-07 |
| [0024](0024-windows-port.md) | Windows port and setup program | 2026-10-07 |
| [0025](0025-out-of-quota.md) | Out-of-quota animation from Claude StopFailure | 2026-10-07 |
| [0026](0026-background-waiting.md) | Background-job waiting from Claude Stop | 2026-10-07 |
| [0027](0027-update-overall-progress.md) | One overall progress bar for updates | 2026-10-07 |
| [0028](0028-pet-packs.md) | Pet packs: selectable pet characters | 2026-10-07 |
| [0029](0029-cues.md) | Cues: events decoupled from how a pet shows them | 2026-10-08 |
| [0030](0030-on-demand-pets.md) | On-demand pets: verified downloads of pets that are not bundled | 2026-10-08 |
| [0031](0031-behavior-runtime.md) | Behavior runtime: one arbiter for the pet's competing behaviors | 2026-10-08 |
| [0032](0032-windows-automatic-updates.md) | Windows automatic updates through verified Setup upgrades | 2026-10-08 |
| [0033](0033-plugin-packs.md) | Plugin packs: catalog fragments merged into a pet | 2026-10-08 |

## Writing a record

Add the next number as `NNNN-short-slug.md` with this shape, and a row above:

```markdown
# NNNN. Title

- Status: Accepted
- Date: YYYY-MM-DD

## Context
## Decision
## Consequences
## Validation
```

Validation holds dated evidence (`### <what> evidence — YYYY-MM-DD`): what was
run, on which host and Qt version, what passed, and what remains open. Later
evidence for the same decision is appended there. A decision that replaces an
earlier one sets the old record's status to `Superseded by NNNN` instead of
rewriting it.
