# Architecture

Agent Pet is one C++17 / Qt 6 Widgets binary, built with CMake and Ninja
([0001](adr/0001-desktop-stack.md)). Headless subcommands (`hook`, `emit`,
`integration`, `autostart`) run with Qt Core only; the GUI pet owns the event
socket, the window and the update controller. Design decisions, with the evidence
that validated each one, are recorded as [architecture decision records](adr/README.md).

The root README has the [end-to-end architecture chart](../../README.md#architecture-at-a-glance)
and [issue-linked roadmap](../../README.md#roadmap-mapped-to-the-architecture), including
the planned pet library, cue mapping, behavior runtime and plugin boundaries.
The code map below describes the current implementation.

## Code map

| Area | Code | Decision |
| --- | --- | --- |
| Module and library split | `src/*`, `CMakeLists.txt` | [0002](adr/0002-module-boundaries.md) |
| Event transport, sessions | `src/ipc`, `src/sessions`, [events](events.md) | [0005](adr/0005-local-event-transport.md) |
| Provider hooks, integration setup | `src/providers`, [integrations](integrations.md) | [0006](adr/0006-provider-adapters.md) |
| Alerts, bubble, badge | `src/desktop/monitor.*`, `src/sessions` | [0007](adr/0007-alert-presentation.md) |
| Open a session's host | `src/hosts`, `src/platform` | [0017](adr/0017-session-focus.md) |
| Preferences | `src/settings` | [0004](adr/0004-preferences-storage.md) |
| Window, menus, recovery | `src/desktop` | [0008](adr/0008-window-behavior.md) |
| Pets, playback and catalog | `src/animation` (`Catalog`, `PetLibrary`, `Player`), `assets/<id>/`, `cmake/pets.cmake`, [pets](pets.md) | [0003](adr/0003-catalog-driven-playback.md), [0014](adr/0014-artwork-resource-file.md), [0028](adr/0028-pet-packs.md) |
| Idle, active, mood, touch, eggs, walking | `src/animation`, `src/desktop` | [0011](adr/0011-idle-animation.md), [0021](adr/0021-active-animation.md), [0012](adr/0012-mood.md), [0013](adr/0013-touch-reactions.md), [0015](adr/0015-easter-eggs.md), [0016](adr/0016-walking.md) |
| Recap, wellness | `src/sessions/recap.*`, `src/desktop/wellness.*` | [0018](adr/0018-daily-recap.md), [0019](adr/0019-wellness-reminders.md) |
| Packaging, updates | `scripts/package*.py`, `src/updates`, [install](install.md) | [0009](adr/0009-linux-packaging.md), [0010](adr/0010-application-updates.md) |
| macOS | `src/platform/macos`, `src/platform/posix` | [0020](adr/0020-macos-port.md) |
| Windows | `src/platform/windows`, `packaging/windows` | [0024](adr/0024-windows-port.md) |
| Translations, language choice | `src/i18n`, `translations/`, [translations](i18n.md) | [0022](adr/0022-localization.md) |

The [platform refactor plan](platform-refactor-plan.md) describes the platform
seams; [platform services](../src/platform/README.md) lists their build
registration. Desktop behavior is accepted by hand with the
[manual acceptance checklist](adr/0008-window-behavior.md#manual-acceptance-checklist).
