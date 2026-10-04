#!/bin/sh
# Install or upgrade Agent Pet for the current user from an extracted package.
# Usage: ./install.sh [--prefix DIR] [--no-desktop] [--no-bin-link]
#                     [--claude] [--codex] [--autostart [--when-idle keep|hide|quit]]
#                     [--yes] [--interactive]
# Run without options from a terminal to be asked instead. With options, --yes,
# or when stdin is not a terminal, it installs without questions and changes
# agent hooks or autostart only for --claude, --codex and --autostart.
set -eu

usage() {
    echo "Usage: $0 [--prefix DIR] [--no-desktop] [--no-bin-link]" >&2
    echo "       [--claude] [--codex] [--autostart [--when-idle keep|hide|quit]] [--yes] [--interactive]" >&2
    echo "Default DIR: \$HOME/.local/opt/agent-pet" >&2
    exit 2
}

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
prefix="$HOME/.local/opt/agent-pet"
prefix_given=0
desktop=1
bin_link=1
claude=0
codex=0
autostart=0
when_idle=
# What is currently set up; only the interactive checklist turns these off.
claude_was=0
codex_was=0
autostart_was=0
interactive=auto
[ $# -eq 0 ] || interactive=0
while [ $# -gt 0 ]; do
    case $1 in
        --prefix) [ $# -ge 2 ] || usage; prefix=$2; prefix_given=1; shift 2 ;;
        --no-desktop) desktop=0; shift ;;
        --no-bin-link) bin_link=0; shift ;;
        --claude) claude=1; shift ;;
        --codex) codex=1; shift ;;
        --autostart) autostart=1; shift ;;
        --when-idle) [ $# -ge 2 ] || usage; when_idle=$2; shift 2 ;;
        --yes|-y) interactive=0; shift ;;
        --interactive) interactive=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done
case $when_idle in
    ''|keep|hide|quit) ;;
    *) echo "--when-idle must be keep, hide or quit" >&2; exit 2 ;;
esac
if [ -n "$when_idle" ] && [ "$autostart" = 0 ] && [ "$interactive" != 1 ]; then
    echo "--when-idle applies with --autostart" >&2; exit 2
fi
if [ "$interactive" = auto ]; then
    if [ -t 0 ]; then interactive=1; else interactive=0; fi
fi

normalize_prefix() {
    case $prefix in
        /*) ;;
        *) prefix="$(pwd)/$prefix" ;;
    esac
    prefix=${prefix%/}
}
# Prints why $prefix cannot be used and returns the exit status to use, or returns 0.
prefix_problem() {
    # The prefix goes into hook commands and the desktop launcher. Spaces are fine;
    # characters that need shell or desktop-entry escaping are refused instead.
    case $prefix in
        *'"'*|*'`'*|*'$'*|*'\'*|*'%'*|*"'"*) echo "Install path must not contain quotes, \$, %, \` or backslashes: $prefix"; return 2 ;;
    esac
    if printf '%s' "$prefix" | grep -q '[[:cntrl:]]'; then
        echo "Install path must not contain control characters"; return 2
    fi
    if [ "$source_dir" = "$prefix" ]; then
        echo "Already installed at $prefix"; return 1
    fi
    # Only replace a directory that a previous install.sh created.
    if [ -e "$prefix" ] && [ ! -f "$prefix/.agent-pet-install" ]; then
        echo "$prefix exists and is not an Agent Pet installation; choose another --prefix"; return 1
    fi
    return 0
}

[ -x "$source_dir/bin/agent-pet" ] || { echo "Run install.sh from an extracted Agent Pet package" >&2; exit 1; }
new_version=$("$source_dir/bin/agent-pet" --version)
new_app="$source_dir/bin/agent-pet"

hooks_enabled() {
    "$new_app" integration inspect --provider "$1" 2>/dev/null | grep -q '"owned_handlers": [1-9]'
}
client_present() {
    case $1 in
        claude) [ -d "${CLAUDE_CONFIG_DIR:-$HOME/.claude}" ] || command -v claude >/dev/null 2>&1 ;;
        codex) [ -d "${CODEX_HOME:-$HOME/.codex}" ] || command -v codex >/dev/null 2>&1 ;;
    esac
}
yes_no() { if [ "$1" = 1 ]; then echo yes; else echo no; fi; }
# "enable", "keep", "remove" or "no" for a hook or autostart change.
change() {
    if [ "$1" = 1 ]; then
        if [ "$2" = 1 ]; then echo keep; else echo enable; fi
    elif [ "$2" = 1 ]; then echo remove
    else echo no
    fi
}

if [ "$interactive" = 1 ]; then
    . "$source_dir/share/agent-pet/tui.sh"
    tui_init "Agent Pet installer" "$source_dir/share/agent-pet"
    # Offer the existing installation that the command link points to, if any.
    existing=$(readlink "$HOME/.local/bin/agent-pet" 2>/dev/null || true)
    existing=${existing%/bin/agent-pet}
    if [ "$prefix_given" = 0 ] && [ -n "$existing" ] && [ -f "$existing/.agent-pet-install" ]; then prefix=$existing; fi
    while :; do
        tui_input "Install location" "$prefix"
        prefix=$REPLY
        normalize_prefix
        if problem=$(prefix_problem); then break; fi
        tui_message "$problem"
    done
    receipt="$prefix/.agent-pet-install"
    if [ -f "$receipt" ]; then
        # Upgrade: start from what the earlier install set up.
        grep -q '^file=.*/agent-pet\.desktop$' "$receipt" && desktop=1 || desktop=0
        grep -q '^link=' "$receipt" && bin_link=1 || bin_link=0
    fi
    hooks_enabled claude && claude_was=1
    hooks_enabled codex && codex_was=1
    status=$("$new_app" autostart status 2>/dev/null || true)
    case $status in *'"autostart": true'*) autostart_was=1 ;; esac
    [ -n "$when_idle" ] || when_idle=$(printf '%s\n' "$status" | sed -n 's/.*"when_idle": "\([a-z]*\)".*/\1/p')
    [ -n "$when_idle" ] || when_idle=keep
    on() { if [ "$1" = 1 ]; then echo on; else echo off; fi; }
    # A fresh install connects the clients it finds; an upgrade keeps what is set up.
    if [ -f "$receipt" ]; then
        [ "$claude" = 1 ] || claude=$claude_was
        [ "$codex" = 1 ] || codex=$codex_was
    else
        { [ "$claude" = 1 ] || [ "$claude_was" = 1 ] || client_present claude; } && claude=1
        { [ "$codex" = 1 ] || [ "$codex_was" = 1 ] || client_present codex; } && codex=1
    fi
    [ "$autostart" = 1 ] || autostart=$autostart_was
    set -- desktop "Menu entry and icon" "$(on $desktop)" \
           bin "agent-pet command in ~/.local/bin" "$(on $bin_link)"
    # Clients are offered when found or already connected.
    offer_claude=0; offer_codex=0
    if [ "$claude_was" = 1 ] || client_present claude; then
        offer_claude=1
        set -- "$@" claude "Connect Claude Code (hooks in ${CLAUDE_CONFIG_DIR:-~/.claude}/settings.json)" "$(on $claude)"
    else claude=0
    fi
    if [ "$codex_was" = 1 ] || client_present codex; then
        offer_codex=1
        set -- "$@" codex "Connect Codex (hooks in ${CODEX_HOME:-~/.codex}/hooks.json)" "$(on $codex)"
    else codex=0
    fi
    set -- "$@" autostart "Start the pet when an agent session starts" "$(on $autostart)"
    tui_checklist "Choose what to set up:" "$@"
    desktop=0; bin_link=0; claude=0; codex=0; autostart=0
    for tag in $REPLY; do
        case $tag in
            desktop) desktop=1 ;;
            bin) bin_link=1 ;;
            claude) claude=1 ;;
            codex) codex=1 ;;
            autostart) autostart=1 ;;
        esac
    done
    if [ "$autostart" = 1 ]; then
        tui_menu "When no agent sessions remain (after two minutes):" "$when_idle" \
            keep "Keep the pet running" \
            hide "Hide it; the tray icon stays and it returns with the next session" \
            quit "Quit; the next session starts it again"
        when_idle=$REPLY
    fi
    if [ -f "$receipt" ]; then
        action="Upgrade $("$prefix/bin/agent-pet" --version 2>/dev/null || echo "unknown version") to $new_version in"
    else
        action="Install $new_version to"
    fi
    summary="$action
  $prefix

Menu entry and icon: $(yes_no $desktop)
Command link ~/.local/bin/agent-pet: $(yes_no $bin_link)"
    [ "$offer_claude" = 0 ] || summary="$summary
Claude Code hooks: $(change $claude $claude_was)"
    [ "$offer_codex" = 0 ] || summary="$summary
Codex hooks: $(change $codex $codex_was)"
    summary="$summary
Autostart on session start: $(change $autostart $autostart_was)"
    [ "$autostart" = 0 ] || summary="$summary (when idle: $when_idle)"
    tui_confirm "$summary

Proceed?" on || tui_cancel
    [ "$tui" != plain ] || echo
else
    normalize_prefix
    if problem=$(prefix_problem); then :; else
        status=$?; echo "$problem" >&2; exit "$status"
    fi
fi

data_home=${XDG_DATA_HOME:-$HOME/.local/share}
receipt="$prefix/.agent-pet-install"
old_version=
if [ -e "$prefix" ]; then
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
else
    echo "Installed $new_version in $prefix"
fi

# Hooks always name the installed executable by its full path, never the link.
app="$prefix/bin/agent-pet"
failed=0
setup() {
    # setup WHAT COMMAND...: runs a post-install step; a failure is reported, not fatal.
    what=$1; shift
    if output=$("$@" 2>&1 >/dev/null); then echo "$what"
    else echo "Could not finish: $what. ${output:-}" >&2; failed=1
    fi
}
for provider in claude codex; do
    if [ "$provider" = claude ]; then want=$claude was=$claude_was name="Claude Code"
    else want=$codex was=$codex_was name=Codex
    fi
    if [ "$want" = 1 ]; then setup "Connected $name; restart it to load the hooks" "$app" integration enable --provider "$provider"
    elif [ "$was" = 1 ]; then setup "Removed Agent Pet hooks for $name" "$app" integration disable --provider "$provider"
    fi
done
if [ "$autostart" = 1 ]; then
    setup "Autostart on agent session start: on${when_idle:+ (when idle: $when_idle)}" \
        "$app" autostart enable ${when_idle:+--when-idle "$when_idle"}
elif [ "$autostart_was" = 1 ]; then
    setup "Autostart on agent session start: off" "$app" autostart disable
fi

configured=$((claude + codex + claude_was + codex_was))
if [ -n "$old_version" ]; then
    if [ "$interactive" = 1 ]; then
        echo "Settings were kept. Quit and restart a running pet to use the new version."
    else
        echo "Settings and integration entries were kept. Quit and restart a running pet to use the new version."
    fi
else
    echo "Start it from your application menu or run: \"$prefix/bin/agent-pet\""
    if [ "$configured" = 0 ]; then
        echo "Enable integrations from the pet's Settings, or run:"
        echo "  \"$prefix/bin/agent-pet\" integration enable --provider claude"
    fi
fi
if [ "$interactive" = 1 ] && [ "$bin_link" = 1 ]; then
    case ":$PATH:" in
        *":$HOME/.local/bin:"*) ;;
        *) echo "Note: ~/.local/bin is not on your PATH, so the agent-pet command needs its full path until you add it." >&2 ;;
    esac
fi
exit "$failed"
