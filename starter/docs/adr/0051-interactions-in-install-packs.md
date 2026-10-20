# 0051. Agent conversations in the install packs

- Status: Accepted
- Date: 2026-10-19

## Context

[0047](0047-direct-agent-interactions.md) to [0049](0049-claude-interaction-hooks.md) built direct agent
conversations, but an installed Agent Pet could not use them:

- the service started only with `agent_conversations` in `preferences.json` or `--interactions`, and Settings had no
  control for either;
- the Claude Code hooks registered only from the command line (`integration enable --interactions`);
- the OpenCode and Codex bridges are Node scripts that no package contained and nothing started.

## Decision

- **Settings.** Startup and agents has an Agent conversations group: the opt-in, hide message previews, and the Claude Code
  hooks (the same registration as the command line, with the same confirmation and ownership rules; not on Windows).
  The opt-in and the preview rule take effect at once: `main.cpp` starts or stops the service, hub and bridges on the
  window's `agentConversationsChanged` signal.
- **Packages.** The bridge scripts (`opencode-bridge.mjs`, `opencode-presence.mjs`, `opencode-tui.js`, `codex-bridge.mjs`)
  ship in `share/agent-pet/bridges` (Linux, so inside the `app` update component), `Contents/Resources/bridges`
  (macOS) and `bridges\` (Windows).
- **Launcher.** `interactions::BridgeLauncher` runs the OpenCode bridge (and the Codex bridge off Windows) with the
  system `node` while the service is up, restarts one that exits after 2 s doubling to a minute (reset after a run of
  30 s), and ends them with the service. Without Node or the scripts it starts nothing and logs why once. A build
  tree runs the sources in `src/providers`.

## Consequences

- Turning the feature on remains the only step that shares message text; nothing starts before it.
- Node 20 or newer is a requirement for OpenCode and Codex conversations; it is not bundled. Claude Code needs no Node.
- An idle bridge process per provider runs while the feature is on, even if that agent is not installed.
- A bridge the user already runs by hand (a systemd user unit, say) would connect next to the pet's own and the newer
  connection takes the conversations over. Stop the hand-made one when upgrading.

## Validation

- 2026-10-19: `bridge-launcher` test (fake Node: arguments and endpoint, stop, restart, no Node). Release build run
  offscreen with `--interactions` and a private runtime directory: `interactions.sock` and the OpenCode presence
  socket appear, both bridges run and none is left after the pet exits. Full CTest passes except `pets`, which fails
  only on a git-excluded local pet folder in this checkout.
