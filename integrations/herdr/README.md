# Agent Pet Herdr Plugin

A native [Herdr](https://github.com/herdrdev/herdr) plugin that bridges Herdr coding agents into **Agent Pet**.

It sits on your screen, gets busy when your Herdr agents work, waves at you when an agent needs user approval, and brings you directly back to the exact **Workspace → Tab → Split Pane** when you click **Open** on the pet.

---

## Installation

### From Herdr CLI
```bash
herdr plugin install WindyWin/vpet-agent-pet/integrations/herdr
```

### Local Development
To test this plugin locally from the repository root:
```bash
herdr plugin link ./integrations/herdr
```

---

## Features

- **Zero-Config Agent Mirroring:** No need to configure separate hooks for Claude Code, Codex, or OpenCode. Herdr monitors all agent panes and forwards status transitions directly to Agent Pet.
- **Full 3-Level Focus Navigation:** Uses the 4-part target (`<workspace>|<tab>|<pane>|<socket>`). When an agent needs input, clicking **Open** on the pet or alert toast runs:
  1. `herdr workspace focus <workspace_id>`
  2. `herdr tab focus <tab_id>`
  3. `herdr agent focus <pane_id>`
  and brings your terminal window directly to the right split pane.
- **Auto-Start:** Automatically launches `agent-pet` on Herdr startup if a local graphical display server (`$DISPLAY` or `$WAYLAND_DISPLAY`) is available.
- **Headless Safety:** Gracefully detects headless/SSH environments and skips GUI startup.
- **Built-in Diagnostics:** Run `herdr pet doctor` to check display, binary, socket, and daemon health.

---

## Commands and Actions

| Command | Action | Description |
|---|---|---|
| `herdr pet start` | `start` | Launches Agent Pet desktop app and starts the bridge daemon |
| `herdr pet stop` | `stop` | Stops the bridge daemon |
| `herdr pet status` | `status` | Checks daemon state, display server, and Agent Pet binary |
| `herdr pet doctor` | `doctor` | Runs diagnostic health checks |

### Panes

- **Doctor Pane:** An interactive terminal overlay running diagnostic checks.
- **Logs Pane:** Tails the bridge daemon log file (`herdr-agent-pet.log`).

---

## How It Works

```
Herdr Multiplexer
      │ (events.subscribe over Unix socket / Windows named pipe)
      ▼
herdr-agent-pet bridge daemon
      │ (Translates pane.agent_status_changed → Agent Pet v1 JSON)
      ▼
Agent Pet Desktop App
      │ (User clicks "Open" on alert or session row)
      ▼
herdr workspace focus <w> && herdr tab focus <t> && herdr agent focus <p>
```
