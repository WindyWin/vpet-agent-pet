# 0024. Windows port

- Status: Accepted
- Date: 2026-10-07

## Context

Bring the pet to Windows 10 and 11 (x64) with a normal installer, reusing everything
above the platform contracts as the [macOS port](0020-macos-port.md) did. Windows
differs from both POSIX systems in the places the contracts isolate: it has no local
datagram sockets, no `fork`, a GUI/console split decided at link time, and its
agents start hooks differently. Claude Code runs shell-form hooks through Git Bash
(PowerShell when Git Bash is missing) and, since 2.1.139, spawns *exec-form* hooks
(`command` plus `args`) directly with no shell. Codex runs a hook's command as
`cmd.exe /e:ON /v:OFF /d /c "<command>"`, where a command whose program token is quoted
never starts ([openai/codex#46454](https://github.com/openai/codex/issues/46454)).

## Decision

The Windows build reuses provider normalization, sessions, alerts, animation, the UI
and the event protocol unchanged. Its platform layer, `src/platform/windows/`, supplies:

- **Event transport.** A message-mode named pipe,
  `\\.\pipe\agent-pet-<user SID>-events`, one event per connection: hooks connect,
  write one message and close. The pet keeps eight overlapped instances listening,
  driven by `QWinEventNotifier`, so a burst of hooks queues while the pet is busy. The
  first instance is created with `FILE_FLAG_FIRST_PIPE_INSTANCE`, which is the
  single-instance lock. The pipe is owned by and open only to the user's SID and
  rejects remote clients. Senders check that the pipe's owner is their own SID and
  connect with `SECURITY_IDENTIFICATION`, so another user who creates the name first
  can deny service but cannot receive events or impersonate the hook. The 8192-byte
  limit is kept: a longer message fails its read with `ERROR_MORE_DATA` and is dropped.
  `XDG_RUNTIME_DIR`, normally unset on Windows, selects a separate pipe as it selects a
  separate socket on POSIX, which keeps tests away from a running pet. Events that wait on
  different instances while the pet is busy can be delivered in either order; sessions
  already order events by `timestamp_ms`, as [events](../events.md) specifies, so no
  ordering is added in the transport.
- **Two executables.** `agent-pet.exe` is a GUI-subsystem program, so starting the pet
  opens no console window; `agent-pet-cli.exe` is the same `main.cpp` linked as a console
  program. Hooks and commands run the console program, whose standard streams work
  under every agent, shell and `cmd.exe`. `hookExecutable()` maps the pet to its console
  companion for registration, and `petExecutable()` maps back for login start and
  hook-triggered autostart.
- **Hook registration.** The hook contract returns a whole handler instead of a command
  string. Claude Code gets the exec form (`"command": "C:\\…\\agent-pet-cli.exe"`,
  `"args": ["hook", "--provider", "claude", "--registration", "agent-pet-v1"]`), which
  needs no quoting at all. Codex gets `<program> hook --provider codex --registration
  agent-pet-v1` with an unquoted program: when the path has spaces or `cmd.exe`
  metacharacters, its 8.3 short path; enabling fails with an explanation when the
  volume keeps no short names. Ownership is the exact `args` array, or the exact command
  grammar with an unquoted program.
- **Hook input.** Standard input is an anonymous pipe, which cannot be waited on, so the
  150 ms deadline is kept by peeking (`PeekNamedPipe`); a redirected file is read
  directly.
- **Process services.** Ancestry and executable names from a Toolhelp snapshot. A
  recorded parent ID can outlive its process and be reused, so the chain stops at a
  "parent" created after its child. tmux and herdr do not run natively, so there are no
  terminal clients or multiplexer activations.
- **Detached launch.** `CreateProcessW` with `DETACHED_PROCESS`, no inherited handles,
  the system directory as working directory, and `CREATE_BREAKAWAY_FROM_JOB` when the
  agent's job allows it (retried without it otherwise).
- **Login start.** A value named `Agent Pet` under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, written with `QSettings`; tests
  pass their own registry key as the location.
- **Focus.** A `windows` desktop backend lists visible, unowned, non-tool top-level
  windows (`EnumWindows`), skips Explorer's (an ancestor of anything started from the
  Start menu), matches them with the shared `matchWindow`, restores a minimized one and
  calls `SetForegroundWindow`, which Windows allows because the pet is the foreground
  application right after the user clicks it. `active()` compares with
  `GetForegroundWindow`. The hook first names the window precisely: it attaches to the
  console of its nearest ancestor that has one (agents start hooks without a console
  window) and sends that console window's root owner as `host_window`, an HWND on
  Windows. Under ConPTY the console window is a hidden pseudo window owned by the
  terminal's window, so this finds Windows Terminal even for a shell handed off to it,
  whose parent is Explorer; a conhost console is its own window. The first console found
  decides; when its window is hidden and unowned (VS Code's terminal), ancestry is used.
- **Native queries.** The left button (the swapped button when buttons are swapped) from
  `GetAsyncKeyState`, because the system move loop can swallow the release; the screen
  lock from the input desktop, which is `Default` while the user works and cannot be
  opened while Winlogon's secure desktop shows.
- **Updates.** Release assets are named `agent-pet-VERSION-windows-x86_64.zip`, so the pet
  announces new versions; there is no managed install prefix, and the setup program
  installs upgrades.

Artwork packs sit beside the executables, where the player already looks first.

`scripts/package_windows.py` stages both executables, the artwork packs, notices and Qt's
notices, runs `windeployqt` (platform, style, image-format and TLS plugins, no Qt
translations), copies the Visual C++ runtime app-locally from the developer environment's
redistributable folder, and runs `--version`, `--check-update-runtime` and optionally
`--smoke-test` with only Windows on `PATH`. It zips the folder as the portable asset the
update check matches and compiles `packaging/windows/agent-pet.iss` with Inno Setup 6
into `agent-pet-VERSION-windows-x86_64-setup.exe`.

The setup program installs per user without administrator rights into
`%LOCALAPPDATA%\Programs\AgentPet` (no spaces, for Codex), only into a new or empty folder
or over its own earlier installation (the folder its uninstall entry names), adds a Start
menu entry and
optionally a desktop shortcut, and offers to connect Claude Code and Codex, start at
sign-in and start with agent sessions, reporting a failed step instead of hiding it. An
upgrade closes a running pet through the Restart Manager and deletes nothing beforehand:
packs a new version no longer lists stay until uninstall (the pet loads only the packs
`artwork.rcc` names), so a failed or cancelled upgrade still leaves working artwork. The
uninstaller stops only pets started from its own folder, removes the login value only when
it names its own `agent-pet.exe`, and removes only Agent Pet's hook entries. `scripts/check_install_windows.py` installs into a folder with spaces with
throwaway Claude Code and Codex configurations, runs each registered hook the way its
agent does, upgrades, uninstalls, and checks that unrelated settings survive.

## Consequences

- Claude Code hooks on Windows need Claude Code 2.1.139 or newer (exec form).
- A Codex hook needs a folder path `cmd.exe` can run unquoted; the setup program's
  default qualifies, and a portable copy should be extracted to such a folder.
- The executables and setup program are not code-signed, so SmartScreen warns on first
  run until they gain reputation or a certificate.
- `agent-pet.exe` prints nothing to a console; command-line use goes through
  `agent-pet-cli.exe`.
- **Open** finds classic console windows and Windows Terminal from the agent's console,
  not its ancestry; a terminal older than ConPTY's window ownership, or one whose pseudo
  window has no owner, falls back to ancestry. `host_window` means an HWND on Windows,
  since a hook and its pet always share one desktop.
- Windows update checkpoints verify the live agent executable and process creation
  time before restoring sessions ([0031](0031-windows-automatic-updates.md)); macOS
  checkpoints still lack process identity.

## Validation

### Windows port evidence — 2026-10-07

- Linux (Ubuntu 24.04, Qt 6.4.2 with the version floor lowered only for that local
  check), after the hook-handler contract change: full build and all 12 CTest suites pass.
- Windows sources syntax-checked with MinGW-w64 GCC 13 (`-Wall -Wextra`) against Qt
  headers before the first CI run.
- Windows: CI run 139 (`windows-2022`, MSVC 2022, Qt 6.5.3 msvc2019_64) passes both
  profiles: the 4 portable-core suites and the providers, events, alerts, focus,
  prototype (4 shards) and i18n suites, including the named-pipe transport, the
  single-instance lock, a 20-hook burst against an undrained pet, and both hook handlers
  run from a folder with spaces as Claude Code and Codex start them. With only Windows on
  `PATH`, the staged `agent-pet-cli.exe` reports `agent-pet 0.12.0`, "Update HTTPS runtime:
  available", and passes `--smoke-test` on the `windows` platform plugin. Packaging
  produced a 177 MiB `agent-pet-0.12.0-windows-x86_64.zip` and a 137 MiB
  `agent-pet-0.12.0-windows-x86_64-setup.exe`; the silent install, hook, upgrade and
  uninstall check passed, leaving unrelated Claude Code settings unchanged. Linux and macOS
  passed in the same run.
- Not yet verified by hand on a Windows desktop: the pet over other windows and across
  monitors and DPI scales, dragging, the tray icon, **Open** from Windows Terminal and VS
  Code, sign-in start, SmartScreen's prompt, and hooks from real Claude Code and Codex sessions.

### Codex terminal focus — 2026-10-07

- Problem: **Open** did not raise the terminal of a Codex session on Windows. Codex
  (`codex-rs/hooks/src/engine/command_runner.rs`) starts hooks as `cmd.exe /C` with
  `CREATE_NO_WINDOW` in a job object, so the ancestry is `cmd.exe → codex.exe → shell →
  …`; it reaches `WindowsTerminal.exe` only when Terminal started the shell itself. A
  shell started from the Start menu or Run and handed off to Terminal has Explorer as
  its parent, and a classic console window belongs to `conhost.exe`, never an ancestor.
- Fix: the console-window hint above. Linux: full build and all 12 CTest suites pass,
  with `alerts` checking that `host_window` carries only the native backend's window.
  Not yet verified on a Windows desktop or in Windows CI for this change.
