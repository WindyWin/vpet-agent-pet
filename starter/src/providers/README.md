# Provider adapters and integration management

`registry.*` lists provider IDs, names, normalizers, setup formats and hook names.
Validation, display names and Settings use this list. Add a registry entry and
an adapter to support another client; keep the session engine provider-neutral.

`adapters.cpp` retains the Claude Code/Codex mappings and validates every adapter's
normalized envelope. `extended_adapters.cpp` maps OpenCode bridge metadata and
Antigravity hooks. Unknown inputs are silent no-ops; content stays out of IPC.

`integrations.cpp` implements preview, enable, inspect and disable for shared hook
groups, Antigravity named hooks, and the embedded `opencode-plugin.js` (with
`opencode-plugin-legacy.js` kept only to recognize and migrate prior installs). It
preserves foreign entries and uses atomic saves. Antigravity commands carry `--event`
and return neutral JSON as required by that client's contract. See
[setup and coverage](../../docs/integrations.md) and [ADR 0041](../../docs/adr/0041-additional-agent-providers.md).

`opencode-bridge.mjs` is separate from the status plugin: it is the OpenCode half of the
[interaction protocol](../../docs/interactions.md), shares message text and answers questions,
and is not installed by `integrations.cpp`. It runs only when started explicitly.

`codex-bridge.mjs` is the Codex half: it connects to the shared app-server daemon's control socket and is also
started only explicitly.
