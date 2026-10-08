# Agent Pet

**English** | [Tiếng Việt](README.vi.md)

**A little desktop pet that keeps you company while Claude Code and Codex work.**

It sits on your screen, thinks when your agent thinks, gets busy when it runs tools, and waves at you when a session needs your approval, so you can look away from the terminal without missing anything.

<p align="center">
  <img src="docs/media/thinking.gif" width="120" alt="Thinking">
  <img src="docs/media/working.gif" width="120" alt="Working">
  <img src="docs/media/needs-input.gif" width="120" alt="Needs input">
  <img src="docs/media/turn-finished.gif" width="120" alt="Turn finished">
</p>

- **Know at a glance** what every agent session is doing.
- **Never miss a request**: a small note pops up when an agent waits for you, and one click takes you to the right terminal.
- **A pet, not just a status light**: drag it, pet it, throw it, watch it wander and nap.
- **Looks after you**: gentle reminders to rest your eyes and drink water, and a recap of your day.
- **Private**: it only learns *what kind* of thing is happening, never your prompts, code or commands.

Works on **Linux** x86_64 (X11, or Wayland desktops with XWayland such as GNOME and KDE), **macOS 11+** (Apple silicon and Intel), and **Windows 10 version 1809+ / Windows 11** (x64).

## Get started

Download the latest version from the [Releases page](https://github.com/WindyWin/vpet-agent-pet/releases/latest). Choose your operating system in the **Download / Tải xuống** table at the top of the release; each package includes the pet artwork.

**Linux**

1. Download `agent-pet-<version>-linux-x86_64.tar.gz` and extract it.
2. In the extracted folder, run `./install.sh`. It asks a few simple questions: where to install, whether to add a menu entry and connect Claude Code and/or Codex, and whether the pet should start on its own when an agent session starts.
3. Restart Claude Code or Codex and send a prompt. The pet reacts.

<img src="docs/media/installer.png" width="480" alt="Installer checklist in a terminal">

**macOS**

1. Download the `.dmg`, open it and drag **Agent Pet** into **Applications**.
2. Open it. The first time, macOS asks you to confirm: go to **System Settings → Privacy & Security → Open Anyway** (on macOS 14 or older, Control-click the app → **Open**).
3. Right-click the pet → **Settings → Startup and agents**, press **Enable** next to Claude Code or Codex, then restart the client.

**Windows**

1. Download and run `agent-pet-<version>-windows-x86_64-setup.exe`. It installs for your user without administrator rights and adds **Agent Pet** to the Start menu. Qt, the Visual C++ runtime and the artwork are included.
2. In setup, choose whether to connect Claude Code and/or Codex and start the pet at sign-in or when an agent session starts. You can also connect them later from **Settings → Startup and agents**.
3. Restart Claude Code or Codex and send a prompt. Claude Code hooks need **2.1.139+**; in Codex, trust the hooks in `/hooks`.

The Windows build is not code-signed yet. If SmartScreen warns, choose **More info → Run anyway**. For portable use, extract `agent-pet-<version>-windows-x86_64.zip` into a folder whose path has no spaces, then run `agent-pet.exe`. See the [Windows install guide](starter/docs/install.md#windows) for details.

Need more detail, or something isn't working? See the [install guide](starter/docs/install.md) and its [troubleshooting table](starter/docs/install.md#troubleshooting).

## What it does

*All pictures are from the real app; project names are made up.*

### Shows what your agents are up to

Run as many sessions as you like. The pet always shows the one that matters most right now: a request waiting for you beats an error, which beats a finished turn, and so on.

<table>
  <tr>
    <td align="center"><img src="docs/media/thinking.gif" width="140" alt="Thinking"><br>Thinking</td>
    <td align="center"><img src="docs/media/reading.gif" width="140" alt="Reading"><br>Reading files</td>
    <td align="center"><img src="docs/media/working.gif" width="140" alt="Working"><br>Working</td>
    <td align="center"><img src="docs/media/needs-input.gif" width="140" alt="Needs input"><br>Waiting for you</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/media/tool-error.gif" width="140" alt="Tool error"><br>Something failed</td>
    <td align="center"><img src="docs/media/turn-finished.gif" width="140" alt="Turn finished"><br>Done!</td>
    <td align="center"><img src="docs/media/sleeping.gif" width="140" alt="Sleeping"><br>Naps after 10 quiet minutes</td>
    <td align="center"><img src="docs/media/startled.gif" width="140" alt="Startled"><br>Jumps at risky commands like <code>rm -rf</code></td>
  </tr>
</table>

It doesn't just loop one pose while it works, either. It swaps its book for a pen without leaving the desk, twirls the pen, and every so often does something new. In Settings you can make it calmer (**Subtle**) or keep the classic single loop (**Classic**); the lively style (**Playful**) is the default.

<table>
  <tr>
    <td align="center"><img src="docs/media/reading-to-working.gif" width="180" alt="Pet at its desk swapping a book for a pen"><br>From reading to writing</td>
    <td align="center"><img src="docs/media/pen-spin.gif" width="180" alt="Pet twirling its pen at the desk"><br>Twirling the pen</td>
  </tr>
</table>

### Tells you when an agent needs you

When an agent asks for approval or input, or a tool fails, a short note appears beside the pet. Click **Open** to jump straight to that agent's terminal or editor, or **×** to dismiss it. An orange badge stays on the pet until you've answered. You can mute the notes, or turn on a sound for new ones.

<img src="docs/media/alert-toast.png" width="420" alt="Needs approval note beside the pet with Open and dismiss buttons">

### All your sessions in one list

Click the pet to see every running session, most urgent first. Click one to jump to it.

<img src="docs/media/session-list.png" width="420" alt="List of four sessions: needs approval, working, thinking and idle">

### Play with it

Drag it anywhere and it dangles from your cursor. Hold still on its head or tummy and it enjoys being petted. Let go mid-swing and it tumbles down, then gets back up. Push it past the side of the screen and it hides there, peeking out until your agents get busy again.

Just don't overdo it: throw it around too much, pet it nonstop or hold on to it too long, and it gets grumpy and leaves.

<table>
  <tr>
    <td align="center"><img src="docs/media/drag-throw.gif" width="420" alt="Dragging and throwing the pet"><br>Drag and throw</td>
    <td align="center"><img src="docs/media/edge-hide.gif" width="240" alt="Pet hiding at the screen edge"><br>Hide at the edge</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/media/touch-head.gif" width="160" alt="Head pat"><br>Head pat</td>
    <td align="center"><img src="docs/media/touch-body.gif" width="160" alt="Tummy poke"><br>Tummy poke</td>
  </tr>
  <tr>
    <td align="center" colspan="2"><img src="docs/media/angry-leave.gif" width="340" alt="Pet stomping angrily with a 'Too much petting! I need a break. Bye!' bubble, then jumping away"><br>Too much petting: it storms off</td>
  </tr>
</table>

### A life of its own

When nothing needs it, the pet yawns, looks around and every so often goes for a walk, crawls along the screen or climbs up its edge. It also has moods: a good run of finished work makes it happy, lots of errors make it droopy, and after a long busy stretch it gets hungry, a hint that you could use a break too.

<table>
  <tr>
    <td align="center"><img src="docs/media/walk-left.gif" width="240" alt="Walking"><br>Walking</td>
    <td align="center"><img src="docs/media/crawl-left.gif" width="240" alt="Crawling"><br>Crawling</td>
    <td align="center"><img src="docs/media/climb-up-right.gif" width="240" alt="Climbing the screen edge"><br>Climbing</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/media/fidget-yawn.gif" width="160" alt="Yawning"><br>Idle fidget</td>
    <td align="center"><img src="docs/media/cheer-shining.gif" width="160" alt="Happy celebration"><br>Happy</td>
    <td align="center"><img src="docs/media/snack-hungry.gif" width="160" alt="Hungry"><br>Time for a break?</td>
  </tr>
</table>

### Little surprises

Friday evenings, your birthday, late nights, very long tasks and every 100th finished task get their own reactions. On weekdays it reminds you to get ready to go home, and at night it tells you to go to bed. There's at least one more surprise to find.

<table>
  <tr>
    <td align="center"><img src="docs/media/dance.gif" width="160" alt="Dance"><br>Friday evening dance</td>
    <td align="center"><img src="docs/media/birthday.gif" width="160" alt="Birthday"><br>Your birthday</td>
    <td align="center"><img src="docs/media/love-520.gif" width="160" alt="Heart on May 20"><br>May 20</td>
    <td align="center"><img src="docs/media/milestone.gif" width="160" alt="Milestone"><br>Every 100th task</td>
  </tr>
</table>

### Takes care of you

Every 20 minutes of work it reminds you to look at something far away for 20 seconds (click the note and it counts down with you), and every hour it reminds you to drink some water. It waits until you're actually at the computer, stays quiet while an agent needs you, and leaves you alone at night.

<table>
  <tr>
    <td align="center"><img src="docs/media/eye-break.png" width="300" alt="Eye break reminder beside the pet"><br>Eye break</td>
    <td align="center"><img src="docs/media/water.png" width="300" alt="Water reminder beside the pet"><br>Time for water</td>
  </tr>
</table>

### Sums up your day

Right-click → **Today's recap** for a one-line summary like *"Today: 38 turns across 3 projects · longest run 22 min"*. Click it for a per-project breakdown. On weekdays the go-home reminder includes it too.

<table>
  <tr>
    <td align="center"><img src="docs/media/recap.png" width="300" alt="Recap summary"><br>Summary</td>
    <td align="center"><img src="docs/media/recap-breakdown.png" width="300" alt="Recap breakdown with turns per project"><br>Breakdown</td>
  </tr>
</table>

### Make it yours

Right-click the pet (or its tray / menu bar icon) for everything: your sessions, today's recap, mute, always on top, Settings and Quit. In **Settings** you can:

- change its size and choose which notes pop up;
- make it calmer or livelier (when idle and while working), and switch off wandering, moods, touch or surprises;
- set or turn off the eye and water reminders, and enter your birthday;
- connect Claude Code and Codex with one click, and have it start with your agents or at login;
- choose how updates are installed.

The interface supports **English and Vietnamese**. It follows your system language by default; choose **Settings → General → Language** to switch immediately.

If it ever gets in the way, click the tray icon to hide it. It keeps watching your sessions and shows a badge on the icon instead.

<table>
  <tr>
    <td align="center" valign="top"><img src="docs/media/menus/after-menu.png" width="380" alt="Right-click menu"><br>Right-click menu</td>
    <td align="center" valign="top"><img src="docs/media/menus/after-settings.png" width="340" alt="Settings window"><br>Settings</td>
  </tr>
</table>

### Stays up to date

On Linux the pet updates itself in the background by default, downloading only what changed and keeping your settings. If a new version fails to start, it goes back to the previous one. On macOS and Windows it tells you when a new version is out: replace the macOS app or run the newer Windows setup program. Settings and enabled hooks are kept when upgrading in place.

## Your privacy

- Everything stays on your computer. The pet only hears from your agents through a private local channel.
- It learns *what kind* of thing is happening (thinking, working, waiting, finished), never your prompts, code, file contents or commands. For risky commands it gets only a simple "this looks dangerous" yes or no.
- The only things it saves are your settings and the recap's daily counts (with project folder names), kept for two weeks.
- Connecting it adds its own small entries to Claude Code's or Codex's hook settings and leaves everything else there untouched. Uninstalling removes only those entries.

## Good to know

- **Not reacting?** Restart Claude Code or Codex after connecting, then send a new prompt. In Codex, approve the new hooks in `/hooks`.
- **Lost the pet?** Click the tray icon, or right-click it → **More → Recover pet position and input**.
- **macOS:** jumping to a terminal window and automatic updates aren't available yet. **Open** still switches tmux and herdr panes.
- **Windows:** **Open** brings the agent's Windows Terminal or classic console window to the front, with an editor/terminal fallback based on process ancestry. Automatic update installation isn't available yet; run the newer setup program. For hook commands and console output, use `agent-pet-cli.exe`.
- **Linux on Wayland:** the pet runs through XWayland, which GNOME and KDE provide by default. Pure Wayland isn't supported yet.

## Issues and discussion

Open an issue from the [template chooser](https://github.com/WindyWin/vpet-agent-pet/issues/new/choose): **Bug report** for a problem, **Feature request** for an improvement, or **Discussion topic** for a question or idea. Search existing issues first and keep each issue focused on one topic. The forms guide you through the relevant context; bug reports ask for your version, environment and steps to reproduce.

## For developers

### Architecture at a glance

End-to-end target architecture. **Dashed boxes are planned**; the remaining pieces
exist today. Until #64 and #67 land, `Monitor`, `PetWindow` and behavior modules
share cue selection and arbitration. Arrows show runtime flow, not build order.

```mermaid
flowchart TB
  agents["Claude Code / Codex"] --> input["Provider adapters · headless hook / emit"]
  input --> ipc["Private local IPC · validated events"]
  ipc --> sessions["Sessions · aggregate activity / attention"]
  sessions --> runtime["#67 Behavior runtime<br/>Intents → eligibility / priority / interruption"]
  life["Touch / wander / mood / wellness<br/>Easter eggs / startup / quit"] --> runtime
  custom["#43 Custom events / rules"] --> runtime
  runtime --> cues["#64 Cue mapping<br/>Semantic cue → pet state / reaction"]
  cues --> player["Player · phases / frames / transitions"]
  player --> window["PetWindow · pet / movement / input"]
  player -. presentation outcomes .-> runtime
  window --> life

  sessions --> alerts["Alerts / session list / tray"]
  alerts --> focus["Open session · host adapters"]
  focus --> platform["Platform services · Linux / macOS / Windows"]
  window --> platform
  sessions --> recap["Daily recap / session checkpoint"]

  packs["#62 PetLibrary / Catalog<br/>Active pet · artwork / credits / cue overrides"] --> cues
  packs --> player
  plugins["#43 Plugin packs<br/>Catalog fragments / rule data"] --> packs
  plugins --> custom
  updates["App updates / packaging"] --> packs
  downloads["#62 Part 2<br/>Verified pet downloads"] --> packs
  settings["Settings / preferences"] -. configure .-> life
  settings -. select pet / plugins .-> packs

  classDef planned fill:#fff4d6,stroke:#9a6700,stroke-dasharray:5 5,color:#24292f
  class runtime,cues,packs,custom,plugins,downloads planned
```

Session priority belongs to `Sessions`; #67 chooses among behavior intents; #64
chooses how the pet expresses the winning cue. Alerts keep their own delivery path.
Movement and native window operations stay in `PetWindow` and platform services.
See the [code map](starter/docs/architecture.md#code-map) for source paths and decisions.

### Roadmap mapped to the architecture

Open work, checked against GitHub on 2026-10-07. Each issue owns its detailed
acceptance checklist; check a row here when that phase lands and update the chart
when a planned layer becomes implemented.

```mermaid
flowchart LR
  packs["#62 Part 1 · pet packs"] --> cues["#64 · cue mapping"]
  cues --> runtime["#67 · arbitration"]
  runtime --> rules["#43 · custom events / rules"]
  packs --> fragments["#43 · catalog fragments"]
  fragments --> rules
  packs --> downloads["#62 Part 2 · downloads"]
```

| Done | Architecture area | Task / issue | Depends on |
| --- | --- | --- | --- |
| ☐ | PetLibrary / Catalog | [#62 Part 1 — selectable pet packs](https://github.com/WindyWin/vpet-agent-pet/issues/62) | Standalone foundation |
| ☐ | Cue mapping | [#64 — semantic cues and per-pet mapping](https://github.com/WindyWin/vpet-agent-pet/issues/64) | #62 Part 1 |
| ☐ | Behavior runtime | [#67 — intent arbitration and lifecycle](https://github.com/WindyWin/vpet-agent-pet/issues/67) | #64 |
| ☐ | Plugin catalog | [#43 Phase 1 — catalog fragments and plugin settings](https://github.com/WindyWin/vpet-agent-pet/issues/43) | #62 Part 1 |
| ☐ | Plugin rules → runtime | [#43 Phases 2–3 — custom events and data-driven triggers](https://github.com/WindyWin/vpet-agent-pet/issues/43) | Catalog fragments, #64 and #67 |
| ☐ | Pet distribution | [#62 Part 2 — verified on-demand downloads](https://github.com/WindyWin/vpet-agent-pet/issues/62) | #62 Part 1 |
| ☐ | Provider adapters | [#36 — third agent client](https://github.com/WindyWin/vpet-agent-pet/issues/36) | Existing event contract |
| ☐ | Alerts / settings | [#34 — snooze / focus mode](https://github.com/WindyWin/vpet-agent-pet/issues/34) | Existing alert delivery |
| ☐ | Alerts / attention | [#35 — escalating approval nudges](https://github.com/WindyWin/vpet-agent-pet/issues/35) | Existing attention tracking |
| ☐ | Mood / interaction | [#37 — earned treats to feed the pet](https://github.com/WindyWin/vpet-agent-pet/issues/37) | Existing mood counters; coordinate reactions with #67 |

### Build and contribution guides

The app lives in [`starter/`](starter/README.md): building from source, running tests, command-line options, sending demo events and packaging. Further reading:

- [Architecture overview](starter/docs/architecture.md) and [design decision records](starter/docs/adr/README.md)
- [Event protocol](starter/docs/events.md) and [Claude Code / Codex integrations](starter/docs/integrations.md)
- [CI commit rules](starter/README.md#ci-commit-rules)

This repository root also keeps the original VPet artwork archive (about 5,500 frames, 735 MiB) as a source bundle for adding new animations. The app itself doesn't need it. Check it with `python3 scripts/assets.py verify`, or list every sequence with `python3 scripts/assets.py catalog`.

## Credits and license

Character artwork by the **VUP-Simulator team**, from [LorisYounger/VPet](https://github.com/LorisYounger/VPet). The artwork keeps its own [terms](licenses/VPET-ARTWORK-TERMS.md); see [third-party notices](THIRD_PARTY_NOTICES.md).

Agent Pet's own code is licensed under [Apache-2.0](starter/LICENSE). That license doesn't cover the artwork.
