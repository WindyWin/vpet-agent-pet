# 0020. macOS port

- Status: Accepted
- Date: 2026-10-06

## Context

Bring the pet to macOS without forking shared code, building on the platform
seams from [0017](0017-session-focus.md) and the
[platform refactor plan](../platform-refactor-plan.md).

## Decision

The macOS build reuses everything above the platform contracts unchanged: provider
normalization, sessions, alerts, animation, the UI and the event protocol. Its
platform layer (`src/platform/macos/`, with `src/platform/posix/` shared with Linux)
supplies:

- **Event transport.** The same Unix datagram socket, lock and permissions. With no
  `XDG_RUNTIME_DIR`, the endpoint is under the per-user temporary directory from
  `confstr(_CS_DARWIN_USER_TEMP_DIR)`, which does not depend on a shell's
  environment, so hooks started by GUI editors and a pet started by launchd agree.
  macOS caps local datagrams at 2 KiB (`net.local.dgram.maxdgram`) by default, so
  both ends raise their socket buffers to carry 8 KiB events.
- **Process services.** Ancestry and command names from `sysctl(KERN_PROC_PID)`;
  terminal clients for tmux and herdr from `proc_listallpids`, `proc_pidpath`, the
  controlling terminal and `KERN_PROCARGS2` (arguments and environment, readable for
  the user's own processes).
- **Login start.** A per-user launchd agent,
  `~/Library/LaunchAgents/io.github.windywin.agent-pet.plist` (`RunAtLoad`, Aqua
  sessions only). Hook-triggered autostart uses the shared double-fork launch.
- **Native queries.** The left button from `CGEventSourceButtonState` (a system
  window move can swallow the release before Qt sees it) and the screen lock from
  the session dictionary (`CGSSessionScreenIsLocked`). The window origin keeps Qt's
  value.
- **Focus.** tmux and herdr panes are selected as on Linux. No desktop backend can
  raise windows yet; the `macos` backend reports `Unsupported` with that reason.
- **Updates.** Release assets are named `agent-pet-VERSION-macos-universal.zip`, so
  the pet announces new versions and links the release page. There is no managed
  install prefix, so nothing is downloaded or installed in place.

Qt tool windows hide while another application is active on macOS; the pet and its
bubbles set `WA_MacAlwaysShowToolWindow`. Artwork packs are found under
`Contents/Resources` beside the executable's `Contents/MacOS`.

`scripts/package_macos.py` assembles `Agent Pet.app` from a build (executable,
artwork packs, notices, generated `.icns`, `Info.plist` with `LSUIElement`), runs
`macdeployqt`, signs the bundle ad hoc, checks that every Mach-O file resolves
inside the bundle or the OS and is universal, runs `--version` and
`--check-update-runtime` from the bundle, and zips it with `ditto`. It also builds a
compressed HFS+ disk image (`hdiutil create -format UDZO`) holding the app,
`INSTALL.txt` and an `Applications` link to drag onto, retrying `hdiutil`'s
occasional "Resource busy" on CI; it then verifies the image, mounts it read-only
and checks the exact contents, the link target, the app's signature and its
reported version. The zip remains the asset the update check matches. Without an
Apple Developer ID the app is not notarized: users approve the first launch, and
an ad hoc signature changes with every build, so permissions macOS ties to the
signature would need granting again after each upgrade (none are used yet).
Hook and login registration refuse an `AppTranslocation` path, where macOS runs an
unmoved download from a temporary copy.

## Consequences

- Shared POSIX code lives in `src/platform/posix/`; OS-specific `#ifdef`s stay minimal.
- No macOS desktop backend can raise windows yet.
- Without a Developer ID the app is not notarized, and ad hoc signatures change every
  build.

## Validation

### macOS port evidence — 2026-10-06

- Linux, after moving shared code to `src/platform/posix/`: full build and all
  eight CTest suites pass locally (Ubuntu 24.04, Qt 6.4.2 with the version floor
  lowered only for that local check; CI uses Qt 6.5.3).
- macOS: CI run 113 (`macos-14`, Qt 6.5.3, universal arm64 + x86_64, deployment
  target 11.0) passes both profiles: the 4 portable-core suites and the providers,
  events, alerts, focus and prototype suites. Packaging produced a 146 MiB
  `agent-pet-0.10.0-macos-universal.zip`; every bundled Mach-O file is universal and
  loads only bundled or system libraries, `codesign --verify --deep --strict` passes,
  and the bundled binary reports `agent-pet 0.10.0` and "Update HTTPS runtime:
  available". The first runs caught a compile error, an offscreen test reading the
  hardware button, and a test socket path over the 104-byte macOS limit.
- macOS disk image: CI run 114 built a 149 MiB
  `agent-pet-0.10.0-macos-universal.dmg` beside the 146 MiB zip; `hdiutil verify`
  passed and the image, mounted read-only, held exactly the app, `INSTALL.txt` and
  the `Applications` link, with a valid signature and the expected version.
- Not yet verified by hand on a Mac: the pet window over other apps and full-screen
  spaces, the menu bar icon, dragging, login start, and hooks from Terminal, iTerm2
  and VS Code.
