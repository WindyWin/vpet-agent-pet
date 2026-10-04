# Prompts shared by install.sh and uninstall.sh; sourced, not run.
# Uses whiptail if present, then dialog, otherwise plain [Y/n] prompts on stdin.
# AGENT_PET_UI=whiptail|dialog|plain overrides the choice (plain is used for tests).
# Answers are returned in REPLY. Cancel or end of input exits before anything changes.

# tui_init TITLE SHARE_DIR — SHARE_DIR holds this file and dialogrc.
tui_init() {
    tui_title=$1
    tui=${AGENT_PET_UI:-}
    if [ -z "$tui" ]; then
        tui=plain
        if [ -t 0 ] && [ -t 1 ]; then
            if command -v whiptail >/dev/null 2>&1; then tui=whiptail
            elif command -v dialog >/dev/null 2>&1; then tui=dialog
            fi
        fi
    fi
    case $tui in
        whiptail|dialog|plain) ;;
        *) echo "AGENT_PET_UI must be whiptail, dialog or plain" >&2; exit 2 ;;
    esac
    # Lists show labels only; the tags are internal.
    if [ "$tui" = dialog ]; then tui_notags=--no-tags; else tui_notags=--notags; fi
    # The default blue themes can be unreadable on terminals with custom palettes. Unless the
    # user has chosen colors, draw with the terminal's own foreground and background.
    if [ "$tui" = dialog ] && [ -z "${DIALOGRC:-}" ] && [ ! -f "$HOME/.dialogrc" ] && [ -f "$2/dialogrc" ]; then
        DIALOGRC=$2/dialogrc; export DIALOGRC
    fi
    if [ "$tui" = whiptail ] && [ -z "${NEWT_COLORS:-}${NEWT_COLORS_FILE:-}" ]; then
        NEWT_MONO=1; export NEWT_MONO
    fi
}

tui_cancel() {
    [ "$tui" != dialog ] || clear
    echo "Cancelled; nothing was changed." >&2
    exit 1
}

# Runs a whiptail or dialog widget; its answer goes to REPLY.
tui_widget() {
    if [ "$tui" = whiptail ]; then
        REPLY=$(whiptail --title "$tui_title" "$@" 3>&1 1>&2 2>&3) || tui_cancel
    else
        REPLY=$(dialog --clear --title "$tui_title" --stdout "$@") || tui_cancel
    fi
}

# tui_message TEXT
tui_message() {
    if [ "$tui" = plain ]; then printf '%s\n' "$1" >&2
    else tui_widget --msgbox "$1" 12 74
    fi
}

# tui_input PROMPT DEFAULT
tui_input() {
    if [ "$tui" = plain ]; then
        printf '%s [%s]: ' "$1" "$2"
        IFS= read -r REPLY || tui_cancel
        [ -n "$REPLY" ] || REPLY=$2
    else
        tui_widget --inputbox "$1" 10 74 "$2"
    fi
}

# tui_confirm QUESTION on|off — returns 0 for yes. on/off is the default answer.
tui_confirm() {
    if [ "$tui" = plain ]; then
        if [ "$2" = on ]; then hint='[Y/n]'; else hint='[y/N]'; fi
        while :; do
            printf '%s %s ' "$1" "$hint"
            IFS= read -r answer || tui_cancel
            case $answer in
                '') [ "$2" = on ]; return ;;
                [Yy]|[Yy][Ee][Ss]) return 0 ;;
                [Nn]|[Nn][Oo]) return 1 ;;
                *) echo "Please answer y or n." ;;
            esac
        done
    fi
    set -- "$1" "$2" "$( [ "$2" = on ] || echo --defaultno )"
    status=0
    if [ "$tui" = whiptail ]; then
        whiptail --title "$tui_title" ${3:+"$3"} --yesno "$1" 16 74 || status=$?
    else
        dialog --clear --title "$tui_title" ${3:+"$3"} --yesno "$1" 16 74 || status=$?
    fi
    [ "$status" -le 1 ] || tui_cancel # Escape
    return "$status"
}

# tui_checklist PROMPT TAG LABEL on|off ... — REPLY lists the checked tags, space separated.
tui_checklist() {
    prompt=$1; shift
    if [ "$tui" = plain ]; then
        echo "$prompt"
        REPLY=
        while [ $# -ge 3 ]; do
            if tui_confirm "  $2?" "$3"; then REPLY="$REPLY $1"; fi
            shift 3
        done
        REPLY=${REPLY# }
        return
    fi
    tui_widget --separate-output $tui_notags --checklist "$prompt

Up/Down moves, Space checks or unchecks [*], Tab switches to Ok/Cancel, Enter confirms." $(($# / 3 + 11)) 74 $(($# / 3)) "$@"
    REPLY=$(printf '%s\n' "$REPLY" | tr -d '"' | tr '\n' ' ')
    REPLY=${REPLY% }
}

# tui_menu PROMPT DEFAULT_TAG TAG LABEL ... — REPLY is the chosen tag.
tui_menu() {
    prompt=$1; default=$2; shift 2
    if [ "$tui" != plain ]; then
        tui_widget --default-item "$default" $tui_notags --menu "$prompt

Up/Down chooses, Enter confirms." $(($# / 2 + 10)) 74 $(($# / 2)) "$@"
        return
    fi
    echo "$prompt"
    number=0; default_number=1
    for item in "$@"; do
        number=$((number + 1))
        [ $((number % 2)) = 1 ] || continue
        [ "$item" != "$default" ] || default_number=$((number / 2 + 1))
    done
    while :; do
        number=0
        for item in "$@"; do
            number=$((number + 1))
            if [ $((number % 2)) = 0 ]; then echo "  $((number / 2))) $item"; fi
        done
        printf 'Choice [%s]: ' "$default_number"
        IFS= read -r answer || tui_cancel
        [ -n "$answer" ] || answer=$default_number
        number=0; REPLY=
        for item in "$@"; do
            number=$((number + 1))
            if [ $((number % 2)) = 1 ] && [ "$((number / 2 + 1))" = "$answer" ]; then REPLY=$item; fi
        done
        [ -z "$REPLY" ] || return 0
        echo "Please enter a number from the list."
    done
}
