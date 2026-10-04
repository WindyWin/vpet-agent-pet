#!/bin/sh
# Remove an Agent Pet installation created by install.sh.
# Usage: <prefix>/uninstall.sh [--keep-integrations] [--purge-settings] [--yes] [--interactive]
# Run without options from a terminal to choose from a checklist instead.
set -eu

usage() { echo "Usage: $0 [--keep-integrations] [--purge-settings] [--yes] [--interactive]" >&2; exit 2; }
prefix=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
receipt="$prefix/.agent-pet-install"
keep_integrations=0
purge=0
keep_link=0
interactive=auto
[ $# -eq 0 ] || interactive=0
for arg in "$@"; do
    case $arg in
        --keep-integrations) keep_integrations=1 ;;
        --purge-settings) purge=1 ;;
        --yes|-y) interactive=0 ;;
        --interactive) interactive=1 ;;
        *) usage ;;
    esac
done
[ -f "$receipt" ] || { echo "$prefix was not installed by install.sh; nothing removed" >&2; exit 1; }
app="$prefix/bin/agent-pet"
data_home=${XDG_DATA_HOME:-$HOME/.local/share}
if [ "$interactive" = auto ]; then
    if [ -t 0 ]; then interactive=1; else interactive=0; fi
fi

if [ "$interactive" = 1 ]; then
    . "$prefix/share/agent-pet/tui.sh"
    tui_init "Agent Pet uninstaller"
    on() { if [ "$1" = 1 ]; then echo on; else echo off; fi; }
    set -- hooks "Remove Agent Pet hooks from Claude Code and Codex" "$(on $((1 - keep_integrations)))" \
           settings "Delete settings ($data_home/agent-pet)" "$(on $purge)"
    if grep -q '^link=' "$receipt"; then set -- "$@" link "Remove the agent-pet command link" on; fi
    tui_checklist "Remove Agent Pet from $prefix. Also:" "$@"
    keep_integrations=1; purge=0; keep_link=1
    for tag in $REPLY; do
        case $tag in
            hooks) keep_integrations=0 ;;
            settings) purge=1 ;;
            link) keep_link=0 ;;
        esac
    done
    tui_confirm "Remove Agent Pet $( "$app" --version 2>/dev/null | sed 's/^agent-pet //' ) from
  $prefix?" on || tui_cancel
    [ "$tui" != plain ] || echo
fi

if [ "$keep_integrations" = 0 ]; then
    # disable removes only handlers in Agent Pet's own command format; other
    # hooks and settings in the same files are preserved byte-for-byte when
    # nothing of ours is present.
    for provider in claude codex; do
        if "$app" integration inspect --provider "$provider" 2>/dev/null | grep -q '"owned_handlers": [1-9]'; then
            "$app" integration disable --provider "$provider" >/dev/null
            echo "Removed Agent Pet hooks for $provider"
        fi
    done
fi

for pid in $(pgrep -x agent-pet 2>/dev/null || true); do
    if [ "$(readlink "/proc/$pid/exe" 2>/dev/null)" = "$app" ]; then
        echo "Agent Pet is still running (pid $pid); quit it from its menu." >&2
    fi
done

while IFS='=' read -r key value; do
    case $key in
        file) if [ -f "$value" ]; then rm -f "$value"; fi ;;
        link) if [ "$keep_link" = 0 ] && [ -L "$value" ] && [ "$(readlink "$value")" = "$app" ]; then rm -f "$value"; fi ;;
    esac
done < "$receipt"
if command -v update-desktop-database >/dev/null 2>&1 && [ -d "$data_home/applications" ]; then
    update-desktop-database "$data_home/applications" >/dev/null 2>&1 || true
fi
if [ "$purge" = 1 ]; then
    rm -rf "$data_home/agent-pet"
    echo "Removed settings in $data_home/agent-pet"
fi
cd /
rm -rf "$prefix"
echo "Removed $prefix"
