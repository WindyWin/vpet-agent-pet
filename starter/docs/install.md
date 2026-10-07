# Installing Agent Pet

On Linux, Agent Pet is released as a relocatable tarball,
`agent-pet-VERSION-linux-x86_64.tar.gz`, with a per-user installer. Qt, the
artwork and every other library it needs are inside the package; no VPet
installation, Qt, Python or other development runtime is required. For macOS, see
[macOS](#macos), and for Windows, [Windows](#windows); the rest of this page describes
Linux unless it says otherwise.

## Requirements

- x86_64 Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36,
  Arch and newer). Release builds run on Ubuntu 22.04 to set this baseline.
- An X11 session, or a Wayland session with XWayland (the default on GNOME and
  KDE). Native Wayland is not supported yet.
- Claude Code and/or Codex CLI with hooks support for the integrations. See
  [integrations.md](integrations.md) for tested client versions.

## Install

```bash
tar xf agent-pet-0.6.0-linux-x86_64.tar.gz
cd agent-pet-0.6.0-linux-x86_64
./install.sh
```

Run from a terminal without options, the installer asks a few questions, using
`whiptail` if installed, then `dialog`, otherwise plain `[Y/n]` prompts:

1. **Install location**, by default `~/.local/opt/agent-pet` (or the existing
   installation that `~/.local/bin/agent-pet` points to).
2. **A checklist**: menu entry and icon, the `agent-pet` command in
   `~/.local/bin`, *Connect Claude Code* (shown when `~/.claude` or a `claude`
   command exists), *Connect Codex* (when `~/.codex` or `codex` exists) and
   *Start the pet when an agent session starts* (see [Autostart](#autostart)).
3. **When no sessions remain** (only with autostart): keep the pet running, hide
   it, or quit.
4. **A summary** to confirm. Cancelling at any point changes nothing.

After copying, it connects the chosen clients with
`"PREFIX/bin/agent-pet" integration enable --provider …` and turns on autostart
with `agent-pet autostart enable`. Hooks always use the installed executable's
full path, never the `~/.local/bin` link. The installer warns when
`~/.local/bin` is not on your `PATH`.

By default this installs to `~/.local/opt/agent-pet` and adds:

- an **Agent Pet** application-menu entry (`~/.local/share/applications/agent-pet.desktop`),
- its icon (`~/.local/share/icons/hicolor/256x256/apps/agent-pet.png`),
- a `~/.local/bin/agent-pet` link, unless that name is already taken.

Options: `--prefix DIR` installs elsewhere (paths with spaces are fine; quotes,
`$`, `%`, backticks and backslashes are refused), `--no-desktop` skips the menu
entry and icon, and `--no-bin-link` skips the link. The installer only replaces a
directory that an earlier `install.sh` created. Without installing, you can also run
`bin/agent-pet` straight from the extracted folder.

Any option, `--yes`, or a stdin that is not a terminal (scripts, CI) installs
without questions and leaves agent hooks and autostart alone. Opt in with
`--claude`, `--codex` and `--autostart [--when-idle keep|hide|quit]`. Add
`--interactive` to be asked anyway, with the other options as the defaults.
`AGENT_PET_UI=whiptail|dialog|plain` picks the prompt style. The prompts draw with your terminal's own colors unless you have a `~/.dialogrc` (or set `DIALOGRC` / `NEWT_COLORS`).

Start the pet from the application menu or with `agent-pet`. Right-click the pet
for Settings, About, Mute alerts and Quit.

## Tray icon and hiding

On desktops with a system tray (KDE Plasma, Xfce, Cinnamon, GNOME with the
AppIndicator extension), left-click the tray icon to hide or show the pet, or use
**Show pet** at the top of its menu. A hidden pet keeps monitoring sessions:
alert bubbles wait until it is shown again, while the tray icon gets the
attention badge (or a red dot for a tool error) and its tooltip reads, for
example, `Agent Pet — 2 sessions · 1 needs attention`. The alert sound setting
still applies. A pet you hide stays hidden until you show it, even when new
sessions start; every launch starts visible. Without a tray, hiding is
unavailable. *Recover pet position and input* also shows a hidden pet.

## Autostart

With autostart on, a connected agent's `SessionStart` hook launches the pet when
none is running, and the new pet shows that session right away. Turn it on in
**Settings → Startup**, in the installer, or without a display:

```bash
agent-pet autostart enable --when-idle hide   # or keep (default) / quit
agent-pet autostart status
agent-pet autostart disable
```

The setting lives in the pet's `preferences.json`, so toggling it never rewrites
the client hook configuration; the integrations must be enabled for it to work.
The hook only launches the pet from a graphical session (`DISPLAY` or
`WAYLAND_DISPLAY` set), so SSH and container sessions never start one. Sessions
already running when the pet starts appear on their next event, as before.

*When no sessions remain* decides what happens two minutes after the last
session ends (a new session cancels it): **keep** the pet running, **hide** it
(it returns with the next session; without a tray it keeps running), or
**quit** (the next session starts it again when autostart is on). It only applies
after the pet has seen a session.

### Start at login

Separately, the pet can start whenever you log in to your desktop, before any
agent session. It is off by default. Turn it on with **Settings → Startup →
Start Agent Pet at login** or:

```bash
agent-pet autostart login enable   # writes ~/.config/autostart/agent-pet.desktop
agent-pet autostart login status
agent-pet autostart login disable
```

## Connect Claude Code or Codex

Open **Settings** from the pet's menu and press **Enable** next to Claude Code or
Codex, or use the command line:

```bash
agent-pet integration preview --provider claude   # shows the exact change
agent-pet integration enable --provider claude
agent-pet integration inspect --provider claude
```

Use `--provider codex` for Codex. Enabling adds Agent Pet's hook commands to
`~/.claude/settings.json` (or `$CLAUDE_CONFIG_DIR`) or `~/.codex/hooks.json` (or
`$CODEX_HOME`) and leaves everything else in those files untouched. The hooks
point at the installed executable, so they keep working across upgrades.

Then restart the client and submit a new prompt. In Codex, review and trust the
new definitions in `/hooks`. The pet reacts to sessions from that point on.

## Upgrade

Extract the newer package and run its `./install.sh` with the same `--prefix` (or
none). The application directory is replaced as a whole; settings in
`~/.local/share/agent-pet/` and enabled integrations are kept. Quit and restart
a running pet to switch to the new version.

Interactively, the checklist starts from the current setup: connected clients,
autostart and the when-idle choice are pre-selected, and unchecking one disables
it.

## Update notifications and automatic updates

Open **Settings → Updates** or **Updates…** from the pet/tray menu.
Checks run 15 seconds after launch and at most once per day, using published
stable releases from `WindyWin/vpet-agent-pet` on GitHub. **Check now** checks
immediately. Turn off **Check automatically once a day** to use manual checks.
Network failures during background checks do not interrupt the pet.

Choose one of four modes:

| Mode | Behavior |
| --- | --- |
| **Notify only** | A menu indicator shows the available version; choose Download update or open the release page. |
| **Download automatically** | Downloads and verifies the package, then waits for **Restart and update**. |
| **Install automatically on next launch** | Downloads and verifies the package, then installs during the next normal launch. |
| **Download and install automatically** (default for new installs) | Downloads and verifies the package, then restarts into the new version after saving the latest monitored sessions, including pending approval and input requests. On Linux, sessions are restored only when the same agent process is still running (checked using its PID, start time and boot identity). Exited, expired or unverifiable sessions are skipped; new hook events can make them visible again. Each version is attempted once, so a rolled-back update does not loop. |

**Later / Close** leaves the update available. **Skip this version** suppresses
that version and discards its pending package; **Check now** shows it again.
The update window shows the installed version, last successful check, download
progress, and release-page link. Updates never replace agent approval bubbles.
Downloads can be cancelled or retried. Completed, verified components are kept
for retries; the interrupted component restarts from zero.

Releases separate application files, runtime libraries and artwork. The updater
checks installed files against the new release and downloads only components that
changed or need repair. A code-only update normally reuses all artwork and runtime
libraries. Artwork is split into stable packs per animation sequence, with a small
separate catalog. Changing one image downloads its sequence pack; adding a sequence
downloads its new pack and catalog. Changing a library downloads the runtime component. This is component-level reuse, not binary patches.
Skipping versions is supported because files are compared with the target release.

Existing installations receive one full update to obtain this updater and the
sequence-pack layout. Later updates use component downloads when the release
provides a verified component manifest. Releases without that manifest use the full
archive; an unsupported manifest or failed component download also falls back to
the verified full archive. Cancelling stops the download. The full archive remains
available for manual installation and recovery.

Only installed release builds can download/install from inside the app. Local
builds (revision `local`) and copies run directly from an extracted archive
support notifications and the manual download link. Older releases without a
GitHub SHA-256 asset digest also require manual download.

Fully automatic mode and **Restart and update** save monitored sessions before
restarting the pet. A save failure blocks the restart so you can fix it and retry.
Agent processes continue running independently of the pet. Session-triggered
launches postpone installation on next launch so monitoring starts immediately.

The helper verifies downloads again, rejects unsafe archive paths and links,
and assembles the complete target beside the existing installation. Reused files
are copied independently and verified again; obsolete files are omitted. The
helper then atomically switches directories.
The executable path, installer receipt, hooks, desktop/menu choices and settings
are preserved. If the new app cannot start and acknowledge readiness within
30 seconds, the previous version is restored. A recovery journal handles an
interrupted replacement at the next normal launch. Installation requires a
writable installation and parent directory and a filesystem supporting Linux
`renameat2(RENAME_EXCHANGE)`; otherwise the existing installation is kept.
Allow space for the downloaded components (or full archive) plus both unpacked
versions. Smaller downloads do not eliminate staging space.

Update state and pending downloads live in
`~/.local/share/agent-pet/updates/` (or `$XDG_DATA_HOME/agent-pet/updates/`).
`result.txt` records the last installation result. Update checks send no session,
project, or agent data. They contact GitHub over HTTPS; downloads are matched
against GitHub's SHA-256 asset digest. A component manifest is verified against
that digest and supplies SHA-256 hashes for component archives and installed files.
This trusts the repository's release
account and HTTPS metadata, without a separate publisher signing key.

## Uninstall

```bash
~/.local/opt/agent-pet/uninstall.sh
```

This removes Agent Pet's own hook entries from both clients, the menu entry, icon,
`~/.local/bin` link and the application directory. Other hooks and settings in
the client configuration files are left as they were. Add `--keep-integrations`
to leave the hook entries in place, or `--purge-settings` to also delete the
pet's saved size, position, alert and startup preferences and its daily recap
counters (`recap.json`).

From a terminal without options it shows a checklist instead: *remove hooks*
(on), *delete settings* (off) and *remove command link* (on), then asks to
confirm. `--yes` skips the questions.

## Language

The pet's interface is in English or Vietnamese. **Automatic** (the default) follows the
system language. On Linux, that is the first of `LANGUAGE`, `LC_ALL`, `LC_MESSAGES`
and `LANG` that is set, as for other desktop applications: `LANG=vi_VN.UTF-8` gives
Vietnamese, and any language other than Vietnamese or English gives English. On macOS,
it is the first of the preferred languages in System Settings. Right-click the pet →
**Settings → General → Language** to pick one regardless. The change applies at once,
is saved in `preferences.json` as `"language": "auto"`, `"en"` or `"vi"`, and
survives upgrades. The command-line interface and the installer are always in English.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| The pet never reacts | `agent-pet integration inspect --provider claude` should list handlers. Restart the client after enabling and submit a new prompt; sessions started earlier appear on their next event. In Codex, trust the hooks in `/hooks`. A `disableAllHooks` setting or managed policy blocks them. |
| `integration enable` says the configuration is malformed | The client file is not valid JSON. Agent Pet never overwrites it; fix the file, then enable again. |
| `could not load the Qt platform plugin "xcb"` | No X server is reachable. Run from a desktop session with `DISPLAY` set (XWayland provides one on Wayland desktops). |
| The pet is hidden or will not take clicks | Click the tray icon, or right-click it and choose *Recover pet position and input*. Click-through always ends after 15 seconds. Quitting and starting the pet again always shows it. |
| Autostart does nothing | `agent-pet autostart status` should say `"autostart": true`, and the integration must be enabled. The pet starts on the next *new* session in a graphical session; restart the client after enabling hooks. |
| "Another Agent Pet monitor is running" | Only one pet listens for events per user. Quit the other one from its menu. |
| `GLIBC_2.xx not found` | The distribution is older than the glibc 2.35 baseline. |
| No transparency (black box around the pet) | The desktop has no compositor. Enable compositing in your window manager. |
| Moved the install folder | Run `agent-pet integration enable` again from the new location so the hooks point at it. |

The installed version is shown by `agent-pet --version` and in **About**.

## Arch Linux

Build the package from `packaging/arch/PKGBUILD`, once a release has attached its
source tarball. The same recipe is planned for the AUR as `agent-pet`, but it is not
published there yet. From the `starter/` directory, run:

```bash
cd packaging/arch
makepkg -si
```

It builds against the system Qt and installs into `/usr` with pacman owning the files,
so upgrade and removal go through pacman. There is no `.agent-pet-install` receipt, so
the app behaves like a development build: it still notifies you of new releases and
offers a manual download, but never installs one. Upgrade through pacman instead. See
[ADR 0023](adr/0023-arch-aur-package.md).

## macOS

macOS releases contain one `Agent Pet.app` for Apple silicon and Intel Macs running
macOS 11 or newer, with Qt and the artwork inside, in two forms:

- `agent-pet-VERSION-macos-universal.dmg`, a disk image: open it and drag the app
  onto the **Applications** shortcut. Recommended.
- `agent-pet-VERSION-macos-universal.zip`, the same app and `INSTALL.txt` in a zip.

`INSTALL.txt` beside the app repeats these steps.

1. Drag **Agent Pet.app** into **Applications** *before* opening it. macOS runs an
   app opened straight from Downloads or the disk image from a temporary copy
   ("App Translocation"); Agent Pet refuses to register hooks or login start from
   that copy, because the path would stop working. Eject the disk image afterwards.
2. Open it. The app is signed ad hoc but not notarized by Apple (that needs a paid
   Apple Developer ID), so macOS blocks the first launch:
   - macOS 15 or newer: open it once, then **System Settings → Privacy & Security →
     Open Anyway**.
   - macOS 14 or older: Control-click the app → **Open** → **Open**.
   - Or remove the quarantine flag yourself:
     `xattr -dr com.apple.quarantine "/Applications/Agent Pet.app"`.
3. The pet appears with an icon in the menu bar and no Dock icon. Right-click the
   pet → **Settings → Startup and agents** to connect Claude Code or Codex, then
   restart the client.

The command-line interface is the same as on Linux, from inside the bundle:

```bash
"/Applications/Agent Pet.app/Contents/MacOS/agent-pet" integration enable --provider claude
"/Applications/Agent Pet.app/Contents/MacOS/agent-pet" autostart enable --when-idle hide
"/Applications/Agent Pet.app/Contents/MacOS/agent-pet" autostart login enable
```

*Start at login* writes a launchd agent,
`~/Library/LaunchAgents/io.github.windywin.agent-pet.plist`; macOS may show a
"Background Items Added" notice. Settings, the recap and update state are under
`~/Library/Application Support/agent-pet`.

**Upgrade:** quit the pet, replace the app in Applications and open it again; the
first launch may need approving again. Hooks and settings are kept as long as the
app stays at the same path. The pet announces new releases, but installing them is
manual on macOS.

**Not yet on macOS:** bringing the agent's terminal or editor window to the front
(**Open** still selects tmux and herdr panes) and automatic update installation.

**Uninstall:** disable the hooks and *Start Agent Pet at login* in Settings (or
`integration disable --provider …` and `autostart login disable`), quit the pet,
move the app to the Trash, and optionally delete
`~/Library/Application Support/agent-pet`.

## Windows

Windows releases are for x64 Windows 10 version 1809 or newer and Windows 11, with Qt,
the Visual C++ runtime and the artwork included, in two forms:

- `agent-pet-VERSION-windows-x86_64-setup.exe`, a setup program. Recommended.
- `agent-pet-VERSION-windows-x86_64.zip`, the same files for portable use.

**Install or upgrade:** run the setup program. It installs for the current user without
administrator rights into `%LOCALAPPDATA%\Programs\AgentPet` and adds **Agent Pet** to
the Start menu. Its optional tasks connect Claude Code and Codex, start the pet when you
sign in, and start it when an agent session starts; a step that fails is reported, and
can be retried from the pet's **Settings → Startup and agents**. Running a newer setup
program upgrades in place, closing a running pet; settings and enabled hooks are kept.
The programs are not code-signed yet, so SmartScreen may warn on first run: choose
**More info → Run anyway**.

For portable use, extract the zip into a folder whose path has no spaces and run
`agent-pet.exe` (Codex starts hooks through `cmd.exe`, which cannot run a program from a
quoted path; Agent Pet uses the folder's short 8.3 name where Windows keeps one, and
refuses to register Codex hooks otherwise).

There are two programs: `agent-pet.exe` is the pet and opens no console window;
`agent-pet-cli.exe` is the same program for hooks and the command line:

```powershell
& "$env:LOCALAPPDATA\Programs\AgentPet\agent-pet-cli.exe" integration enable --provider claude
& "$env:LOCALAPPDATA\Programs\AgentPet\agent-pet-cli.exe" autostart enable --when-idle hide
& "$env:LOCALAPPDATA\Programs\AgentPet\agent-pet-cli.exe" autostart login enable
```

Claude Code hooks are registered in exec form (a program and its arguments, run without a
shell) and need Claude Code 2.1.139 or newer. Codex hooks need trusting in Codex `/hooks`,
as on other systems. *Start at login* writes an `Agent Pet` value under
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, which Task Manager's **Startup
apps** also lists. Settings, the recap and update state are under `%APPDATA%\agent-pet`.

**Open** brings the agent's terminal window to the front: the console window the agent runs
in (Windows Terminal, including PowerShell or Command Prompt windows handed off to it, or a
classic console window), otherwise the editor or terminal found by process ancestry. tmux
and herdr are not used on Windows.

**Not yet on Windows:** automatic update installation (the pet announces new versions; run
the newer setup program).

**Uninstall:** **Settings → Apps → Installed apps → Agent Pet → Uninstall**. It stops the
pet and removes only Agent Pet's hook entries and its sign-in value; other hooks are left as
they were. Delete `%APPDATA%\agent-pet` to remove settings too.

## Licenses

Agent Pet's application code is under the Apache License 2.0 (`share/agent-pet/LICENSE` and `NOTICE`).
The pet artwork, and the application icon cropped from it, are by the
VUP-Simulator team via [LorisYounger/VPet](https://github.com/LorisYounger/VPet)
and keep their own terms (`share/agent-pet/THIRD_PARTY_NOTICES.md` and
`share/agent-pet/licenses/`); they are not Apache-2.0. Bundled Qt and system libraries
keep their own licenses, collected under `share/agent-pet/runtime-licenses/` and
listed in `share/agent-pet/runtime-manifest.json`. In the macOS app these files are
under `Agent Pet.app/Contents/Resources/`; on Windows they are in the installation folder.
