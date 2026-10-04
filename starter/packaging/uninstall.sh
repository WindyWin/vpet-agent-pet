#!/bin/sh
# Remove an Agent Pet installation created by install.sh.
# Usage: <prefix>/uninstall.sh [--keep-integrations] [--purge-settings]
set -eu

prefix=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
receipt="$prefix/.agent-pet-install"
keep_integrations=0
purge=0
for arg in "$@"; do
    case $arg in
        --keep-integrations) keep_integrations=1 ;;
        --purge-settings) purge=1 ;;
        *) echo "Usage: $0 [--keep-integrations] [--purge-settings]" >&2; exit 2 ;;
    esac
done
[ -f "$receipt" ] || { echo "$prefix was not installed by install.sh; nothing removed" >&2; exit 1; }
app="$prefix/bin/agent-pet"

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
        link) if [ -L "$value" ] && [ "$(readlink "$value")" = "$app" ]; then rm -f "$value"; fi ;;
    esac
done < "$receipt"
data_home=${XDG_DATA_HOME:-$HOME/.local/share}
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
