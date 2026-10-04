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

By default this installs to `~/.local/opt/agent-pet` and adds:

- an **Agent Pet** application-menu entry (`~/.local/share/applications/agent-pet.desktop`),
- its icon (`~/.local/share/icons/hicolor/256x256/apps/agent-pet.png`),
- a `~/.local/bin/agent-pet` link, unless that name is already taken.

Options: `--prefix DIR` installs elsewhere (paths with spaces are fine; quotes,
`$`, `%`, backticks and backslashes are refused), `--no-desktop` skips the menu
entry and icon, and `--no-bin-link` skips the link. The installer only replaces a
directory that an earlier `install.sh` created. Without installing, you can also run
`bin/agent-pet` straight from the extracted folder.

Start the pet from the application menu or with `agent-pet`. Right-click the pet
for Settings, About, Mute alerts and Quit.

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

## Uninstall

```bash
~/.local/opt/agent-pet/uninstall.sh
```

This removes Agent Pet's own hook entries from both clients, the menu entry, icon,
`~/.local/bin` link and the application directory. Other hooks and settings in
the client configuration files are left as they were. Add `--keep-integrations`
to leave the hook entries in place, or `--purge-settings` to also delete the
pet's saved size, position and alert preferences.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| The pet never reacts | `agent-pet integration inspect --provider claude` should list handlers. Restart the client after enabling and submit a new prompt; sessions started earlier appear on their next event. In Codex, trust the hooks in `/hooks`. A `disableAllHooks` setting or managed policy blocks them. |
| `integration enable` says the configuration is malformed | The client file is not valid JSON. Agent Pet never overwrites it; fix the file, then enable again. |
| `could not load the Qt platform plugin "xcb"` | No X server is reachable. Run from a desktop session with `DISPLAY` set (XWayland provides one on Wayland desktops). |
| The pet is hidden or will not take clicks | Right-click the tray icon, or run `agent-pet --settings`, and choose *Recover pet position and input*. Click-through always ends after 15 seconds. |
| "Another Agent Pet monitor is running" | Only one pet listens for events per user. Quit the other one from its menu. |
| `GLIBC_2.xx not found` | The distribution is older than the glibc 2.35 baseline. |
| No transparency (black box around the pet) | The desktop has no compositor. Enable compositing in your window manager. |
| Moved the install folder | Run `agent-pet integration enable` again from the new location so the hooks point at it. |

The installed version is shown by `agent-pet --version` and in **About**.

## Licenses

Agent Pet's application code is under the MIT License (`share/agent-pet/LICENSE`).
The pet artwork, and the application icon cropped from it, are by the
VUP-Simulator team via [LorisYounger/VPet](https://github.com/LorisYounger/VPet)
and keep their own terms (`share/agent-pet/THIRD_PARTY_NOTICES.md` and
`share/agent-pet/licenses/`); they are not MIT. Bundled Qt and system libraries
keep their own licenses, collected under `share/agent-pet/runtime-licenses/` and
listed in `share/agent-pet/runtime-manifest.json`.
