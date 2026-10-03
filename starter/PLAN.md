# Agent Pet first release

Build a standalone desktop pet with assets loaded from this repository and later from its installed application resources. The original VPet app and command are outside the runtime path.

1. Build a transparent, draggable desktop window and play the bundled `idle` sequence using frame durations from `assets/vpet/animations.json`. Test X11 and XWayland behavior before choosing a Linux package format.
2. Add an animation controller that can start a sequence, hold its middle loop, play its ending, and return to idle. Keep decoded images in a bounded cache. The JSON state map has separate folders for each A/B/C phase.
3. Add a small local event receiver and a packaged `agent-pet hook` command. The command reads hook JSON from stdin, extracts only event and session identifiers, sends them through a private local channel, and exits quickly even when the UI is closed.
4. Add Claude Code and Codex hook adapters for SessionStart, UserPromptSubmit, PreToolUse, PostToolUse, PermissionRequest, Stop, and SessionEnd. Handle each provider's supported failure and interruption events. Track concurrent tool calls and sessions; `Stop` means a turn ended, not that the task succeeded.
5. Bundle executable, hook command, sprites, this notice, and required dependencies. Test the package on a machine without VPet. Integrations must add their own hook entries while preserving existing user settings.

The source pack includes only normal mood variants and one selected version of each animation. Expand it from the larger local asset archive only when a new state needs more frames. Preserve attribution and notices for all derived asset packs.
