# Provider adapters and integration management

`adapters.cpp` normalizes Claude Code and Codex hooks into the versioned session
protocol. Each provider has its own failure/notification/interruption mapping.
Unknown inputs are silent no-ops; content fields are excluded from IPC.

`integrations.cpp` implements headless preview, enable, inspect and disable with
app-owned command markers, preservation of foreign handlers, atomic saves and
shell-safe executable paths. See [setup and coverage](../../docs/integrations.md).
