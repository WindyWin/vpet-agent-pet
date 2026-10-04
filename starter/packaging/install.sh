#!/bin/sh
# Install or upgrade Agent Pet for the current user from an extracted package.
# Usage: ./install.sh [--prefix DIR] [--no-desktop] [--no-bin-link]
set -eu

usage() {
    echo "Usage: $0 [--prefix DIR] [--no-desktop] [--no-bin-link]" >&2
    echo "Default DIR: \$HOME/.local/opt/agent-pet" >&2
    exit 2
}

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
prefix="$HOME/.local/opt/agent-pet"
desktop=1
bin_link=1
while [ $# -gt 0 ]; do
    case $1 in
        --prefix) [ $# -ge 2 ] || usage; prefix=$2; shift 2 ;;
        --no-desktop) desktop=0; shift ;;
        --no-bin-link) bin_link=0; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done
case $prefix in
    /*) ;;
    *) prefix="$(pwd)/$prefix" ;;
esac
prefix=${prefix%/}
# The prefix goes into hook commands and the desktop launcher. Spaces are fine;
# characters that need shell or desktop-entry escaping are refused instead.
case $prefix in
    *'"'*|*'`'*|*'$'*|*'\'*|*'%'*|*"'"*) echo "Install path must not contain quotes, \$, %, \` or backslashes: $prefix" >&2; exit 2 ;;
esac
if printf '%s' "$prefix" | grep -q '[[:cntrl:]]'; then
    echo "Install path must not contain control characters" >&2; exit 2
fi
[ -x "$source_dir/bin/agent-pet" ] || { echo "Run install.sh from an extracted Agent Pet package" >&2; exit 1; }
if [ "$source_dir" = "$prefix" ]; then
    echo "Already installed at $prefix" >&2; exit 1
fi

data_home=${XDG_DATA_HOME:-$HOME/.local/share}
receipt="$prefix/.agent-pet-install"
new_version=$("$source_dir/bin/agent-pet" --version)
old_version=
if [ -e "$prefix" ]; then
    # Only replace a directory that a previous install.sh created.
    if [ ! -f "$receipt" ]; then
        echo "$prefix exists and is not an Agent Pet installation; choose another --prefix" >&2; exit 1
    fi
    old_version=$("$prefix/bin/agent-pet" --version 2>/dev/null || echo "unknown version")
fi

remove_receipt_entries() {
    # Remove launcher files and the bin link that an earlier install recorded.
    while IFS='=' read -r key value; do
        case $key in
            file) if [ -f "$value" ]; then rm -f "$value"; fi ;;
            link) if [ -L "$value" ] && [ "$(readlink "$value")" = "$prefix/bin/agent-pet" ]; then rm -f "$value"; fi ;;
        esac
    done < "$1"
}

# Copy beside the destination, then swap, so a failed copy never leaves a
# half-installed app and a running pet keeps its already-open files.
parent=$(dirname -- "$prefix")
mkdir -p "$parent"
staging="$prefix.installing.$$"
previous="$prefix.previous.$$"
trap 'rm -rf "$staging"' EXIT
mkdir "$staging"
(cd "$source_dir" && tar cf - --exclude=./.agent-pet-install .) | (cd "$staging" && tar xf -)
"$staging/bin/agent-pet" --version >/dev/null

[ -z "$old_version" ] || remove_receipt_entries "$receipt"
{
    echo "version=$new_version"
    echo "prefix=$prefix"
} > "$staging/.agent-pet-install"
if [ "$desktop" = 1 ]; then
    applications="$data_home/applications"
    icons="$data_home/icons/hicolor/256x256/apps"
    mkdir -p "$applications" "$icons"
    cp "$staging/share/icons/hicolor/256x256/apps/agent-pet.png" "$icons/agent-pet.png"
    escaped=$(printf '%s' "$prefix" | sed 's/[&|]/\\&/g')
    sed "s|@EXEC@|\"$escaped/bin/agent-pet\"|" "$staging/share/agent-pet/agent-pet.desktop.in" > "$applications/agent-pet.desktop.tmp"
    mv "$applications/agent-pet.desktop.tmp" "$applications/agent-pet.desktop"
    echo "file=$applications/agent-pet.desktop" >> "$staging/.agent-pet-install"
    echo "file=$icons/agent-pet.png" >> "$staging/.agent-pet-install"
fi
if [ "$bin_link" = 1 ]; then
    bin_dir="$HOME/.local/bin"
    link="$bin_dir/agent-pet"
    mkdir -p "$bin_dir"
    if [ -L "$link" ] && [ "$(readlink "$link")" = "$prefix/bin/agent-pet" ]; then
        echo "link=$link" >> "$staging/.agent-pet-install"
    elif [ -e "$link" ] || [ -L "$link" ]; then
        echo "Leaving existing $link unchanged" >&2
    else
        ln -s "$prefix/bin/agent-pet" "$link"
        echo "link=$link" >> "$staging/.agent-pet-install"
    fi
fi

if [ -e "$prefix" ]; then
    mv "$prefix" "$previous"
    mv "$staging" "$prefix"
    rm -rf "$previous"
else
    mv "$staging" "$prefix"
fi
trap - EXIT
if command -v update-desktop-database >/dev/null 2>&1 && [ "$desktop" = 1 ]; then
    update-desktop-database "$data_home/applications" >/dev/null 2>&1 || true
fi

if [ -n "$old_version" ]; then
    echo "Upgraded $old_version -> $new_version in $prefix"
    echo "Settings and integration entries were kept. Quit and restart a running pet to use the new version."
else
    echo "Installed $new_version in $prefix"
    echo "Start it from your application menu or run: \"$prefix/bin/agent-pet\""
    echo "Enable integrations from the pet's Settings, or run:"
    echo "  \"$prefix/bin/agent-pet\" integration enable --provider claude"
fi
