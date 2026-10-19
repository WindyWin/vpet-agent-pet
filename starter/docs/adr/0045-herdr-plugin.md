# 0045 — Herdr plugin startup and installation

## Context

Herdr users can start Agent Pet with their server and manage its installation
from a plugin action. The integration lives in the separate
[herdr-agent-pet repository](https://github.com/WindyWin/herdr-agent-pet).

## Decision

Document `herdr plugin install WindyWin/herdr-agent-pet` for Linux x86_64 and
macOS with Herdr 0.7.5 or newer. The startup action requires a graphical session
and launches `agent-pet --autostarted`, which preserves single-instance behavior.
The install action verifies the release asset against GitHub's SHA-256 digest.
Linux delegates to the package installer; macOS copies the app bundle to the
existing location or `/Applications`, falling back to `~/Applications`.

The startup script clears its inherited Herdr workspace, tab and pane identity.
Agent Pet's Herdr adapter therefore cannot capture an **Open** navigation target
from this plugin startup flow. The guide and both root README notes expose this
limitation rather than promise navigation back to an agent pane.

## Consequences

Users can manage the pet from Herdr without adding integration code to the app.
Plugin availability and installer behavior remain owned by the external plugin.
The plugin's server startup is insufficient for Herdr **Open** navigation;
normal host capture still requires a valid tab and pane identity.

## Validation

### Documentation and source evidence — 2026-10-19

Inspected the external plugin's `bin/start.sh` and `bin/install.sh` through the
GitHub contents API. Confirmed the identity clearing, graphical-session guard,
Linux package-installer delegation and macOS `ditto` bundle copy/fallback paths.
Checked the local Herdr adapter's requirement for tab/pane identity. Verified
local documentation links and `git diff --check`. No live Herdr installation,
macOS bundle replacement or graphical startup test was performed for this docs
change.
