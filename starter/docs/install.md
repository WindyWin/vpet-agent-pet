# Installing Agent Pet on Linux

Agent Pet is released as a relocatable tarball,
`agent-pet-VERSION-linux-x86_64.tar.gz`, with a per-user installer. Qt, the
artwork and every other library it needs are inside the package; no VPet
installation, Qt, Python or other development runtime is required.

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
`AGENT_PET_UI=whiptail|dialog|plain` picks the prompt style.

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

## Uninstall

```bash
~/.local/opt/agent-pet/uninstall.sh
```

This removes Agent Pet's own hook entries from both clients, the menu entry, icon,
`~/.local/bin` link and the application directory. Other hooks and settings in
the client configuration files are left as they were. Add `--keep-integrations`
to leave the hook entries in place, or `--purge-settings` to also delete the
pet's saved size, position, alert and startup preferences.

From a terminal without options it shows a checklist instead: *remove hooks*
(on), *delete settings* (off) and *remove command link* (on), then asks to
confirm. `--yes` skips the questions.

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

## Licenses

Agent Pet's application code is under the Apache License 2.0 (`share/agent-pet/LICENSE` and `NOTICE`).
The pet artwork, and the application icon cropped from it, are by the
VUP-Simulator team via [LorisYounger/VPet](https://github.com/LorisYounger/VPet)
and keep their own terms (`share/agent-pet/THIRD_PARTY_NOTICES.md` and
`share/agent-pet/licenses/`); they are not Apache-2.0. Bundled Qt and system libraries
keep their own licenses, collected under `share/agent-pet/runtime-licenses/` and
listed in `share/agent-pet/runtime-manifest.json`.
