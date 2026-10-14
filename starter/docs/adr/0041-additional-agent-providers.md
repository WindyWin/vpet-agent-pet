# 0041 — Provider registry, OpenCode and Antigravity

Status: Accepted; live compatibility experimental.

## Context

The event engine was reusable, but validation, display names and settings assumed
two providers. Setup assumed both used the same hook-group JSON structure.
OpenCode uses JS/TS plugins; Antigravity uses named hook definitions and camelCase
payloads. Neither should be forced into the existing clients' configuration schema.

## Decision

Use a compiled provider registry with IDs, names, normalizers, setup formats and
hook names. The wire envelope and pet behavior remain shared. Keep the existing
Claude/Codex adapter; give the new clients independent metadata mappings.

Embed a dependency-free OpenCode plugin in the executable. Setup owns one complete
generated file, recognizes it by exact generated content (including its executable
path), refuses edited/foreign files and removes only that file on disable. The
plugin uses an argument-array spawn, serializes sends in a bounded queue, ignores
delivery failures, and caps each child at 500 ms. It forwards metadata only to
`hook`, retaining existing host capture and autostart. No additional Node runtime
is required by Agent Pet; the plugin runs in OpenCode's runtime.

Antigravity owns marked commands inside the `agent-pet-v1` namespace, preserving
foreign namespaces and handlers. Register `--event` because the native payload
does not identify the event. Correlate tools by conversation plus step index;
only invocation zero maps to a new observed turn, so model continuations do not
clear active tools. Stop with background work maps to waiting. Hooks return the
documented neutral JSON response (`allow`, never `auto_approve`, for PreToolUse;
`stop` for Stop; `{}` otherwise), including when the pet is unavailable. Existing
providers retain silent callbacks.

## Consequences

Adding providers no longer requires changing protocol validation or settings rows.
New setup formats can reuse the existing bounded reads, writer lock, external-edit
check and atomic save. The JavaScript bridge has a separate optional Node test.
An edited bridge is intentionally not overwritten; future bridge revisions need
an explicit migration/ownership policy for previously generated versions.

Full feature parity is not claimed: Antigravity has no general permission-request,
session-end or child identity callback in the inspected contract. The reset of
invocation numbers across turns and step identity across concurrent tools need
live acceptance. OpenCode's failure followed by idle is suppressed until a new
chat message. Neither integration promises precise IDE conversation selection,
remote transport or discovery of sessions that send no callbacks. Installers'
existing Claude/Codex prompts remain; new integrations are available from Settings
or the CLI.

## Validation

### Contract review — 2026-10-14

- [OpenCode plugin documentation](https://opencode.ai/docs/plugins/); locally
  installed OpenCode CLI 1.19.0 and `@opencode-ai/plugin`/SDK declarations from
  the user's OpenCode installation (plugin package 1.18.1).
- [Antigravity hook documentation](https://antigravity.google/docs/hooks), including
  named configuration, `conversationId`, `stepIdx`, `invocationNum`, `fullyIdle`
  and required hook responses. Antigravity is not installed on this host.
- Automated fixtures are synthetic, not captured live payloads. Tests cover
  normalization, privacy, concurrent tools, interruption, child identity, setup
  idempotence, foreign configuration preservation, edited plugin refusal, and
  subprocess delivery through the real local transport.
- Live authenticated agent sessions and Windows/macOS execution remain pending.

### Automated verification — 2026-10-14

Linux, Qt 6.11.2: the full application built successfully and all 17 CTest suites
passed, including the new OpenCode plugin test, provider subprocess/setup tests,
and all four desktop prototype shards. Uninstall script syntax was checked;
Windows installer execution remains pending.
