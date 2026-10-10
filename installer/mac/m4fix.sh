#!/bin/bash
# m4fix installer for the Mac (CrossOver-wrapped) version of Myst IV: Revelation.
#
#   ./m4fix.sh                       interactive: find the game, show status, ask what to do
#   ./m4fix.sh install   [--path P] [--yes]
#   ./m4fix.sh uninstall [--path P] [--yes]
#   ./m4fix.sh status    [--path P]
#
# P may be the game's .app, or the "bin" folder containing Myst4.exe.
# Exit codes: 0 ok, 1 error, 2 game not found, 3 cancelled, 4 game is running.
set -u

VERSION="1.0.0"
MARKER="M4FIX-CRASHRPT-PROXY"
HERE="$(cd "$(dirname "$0")" && pwd)"

# Payload: next to this script (release zip) or in the repo build tree.
find_payload() {
    for d in "$HERE" "$HERE/files" "$HERE/../files" "$HERE/../../build"; do
        [ -f "$d/CrashRpt.dll" ] && grep -aq "$MARKER" "$d/CrashRpt.dll" && { PAYLOAD_DLL="$d/CrashRpt.dll"; break; }
    done
    for d in "$HERE" "$HERE/files" "$HERE/../files" "$HERE/../../config"; do
        [ -f "$d/m4fix.ini" ] && { PAYLOAD_INI="$d/m4fix.ini"; break; }
    done
    [ -n "${PAYLOAD_DLL:-}" ] && [ -n "${PAYLOAD_INI:-}" ]
}

c_bold=$'\033[1m'; c_red=$'\033[31m'; c_grn=$'\033[32m'; c_yel=$'\033[33m'; c_off=$'\033[0m'
[ -t 1 ] || { c_bold=; c_red=; c_grn=; c_yel=; c_off=; }
info() { printf '%s\n' "$*"; }
ok()   { printf '%s%s%s\n' "$c_grn" "$*" "$c_off"; }
warn() { printf '%s%s%s\n' "$c_yel" "$*" "$c_off" >&2; }
die()  { printf '%sError:%s %s\n' "$c_red" "$c_off" "$2" >&2; exit "$1"; }

# Locate drive_c/Myst 4/bin inside an .app, or accept a bin folder directly.
bin_of() {
    local p="$1"
    [ -f "$p/Myst4.exe" ] && { printf '%s\n' "$p"; return 0; }
    [ -f "$p/bin/Myst4.exe" ] && { printf '%s\n' "$p/bin"; return 0; }
    if [ -d "$p" ]; then
        local exe
        exe="$(find "$p" -maxdepth 9 -name Myst4.exe -path '*drive_c*' -print -quit 2>/dev/null)"
        [ -n "$exe" ] && { dirname "$exe"; return 0; }
    fi
    return 1
}

steam_libraries() {
    local steam="$HOME/Library/Application Support/Steam"
    printf '%s\n' "$steam"
    local vdf="$steam/steamapps/libraryfolders.vdf"
    [ -f "$vdf" ] && sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)".*/\1/p' "$vdf"
}

find_game() {
    local lib app
    while IFS= read -r lib; do
        for app in "$lib/steamapps/common/Myst IV Revelation/"*.app; do
            [ -d "$app" ] && bin_of "$app" && return 0
        done
    done < <(steam_libraries)
    for app in "/Applications/Myst IV"*.app "$HOME/Applications/Myst IV"*.app; do
        [ -d "$app" ] && bin_of "$app" && return 0
    done
    return 1
}

is_m4fix() { [ -f "$1" ] && grep -aq "$MARKER" "$1"; }

game_running() { pgrep -fq 'Myst4\.exe'; }

# Copy with a clear message when macOS App Management blocks writes into the .app.
copy() {
    local err
    if ! err="$(cp -f "$1" "$2" 2>&1)"; then
        if printf '%s' "$err" | grep -qi "not permitted"; then
            die 1 "macOS blocked changes to the game app.
  Open System Settings > Privacy & Security > App Management, allow Terminal
  (or the app you ran this from), then run the installer again."
        fi
        die 1 "copy to $2 failed: $err"
    fi
    xattr -d com.apple.quarantine "$2" 2>/dev/null || true
}

status() {
    info "Game folder: $BIN"
    if is_m4fix "$BIN/CrashRpt.dll"; then
        ok "m4fix: installed ($(grep -ao "$MARKER [0-9.]*" "$BIN/CrashRpt.dll" | head -1 | cut -d' ' -f2))"
        return 0
    fi
    info "m4fix: not installed"
    return 1
}

install() {
    find_payload || die 1 "CrashRpt.dll / m4fix.ini payload not found next to this script."
    local dll="$BIN/CrashRpt.dll" orig="$BIN/CrashRpt_orig.dll" update=0
    is_m4fix "$dll" && update=1
    # Keep the genuine CrashRpt.dll; refresh the backup if an update/verify restored it.
    if [ -f "$dll" ] && [ $update = 0 ]; then copy "$dll" "$orig"; fi
    [ -f "$orig" ] || warn "No original CrashRpt.dll found; installing anyway (crash reporting disabled)."
    copy "$PAYLOAD_DLL" "$dll.m4fix-new" && mv -f "$dll.m4fix-new" "$dll" || die 1 "could not replace $dll"
    [ -f "$BIN/m4fix.ini" ] || copy "$PAYLOAD_INI" "$BIN/m4fix.ini"
    if [ $update = 1 ]; then ok "m4fix $VERSION updated."; else ok "m4fix $VERSION installed."; fi
    info "Settings: $BIN/m4fix.ini"
}

uninstall() {
    local dll="$BIN/CrashRpt.dll" orig="$BIN/CrashRpt_orig.dll"
    if is_m4fix "$dll"; then
        [ -f "$orig" ] || die 1 "CrashRpt_orig.dll is missing, cannot restore the original. Use Steam > Verify integrity of game files."
        mv -f "$orig" "$dll" 2>/dev/null || copy "$orig" "$dll"
        rm -f "$orig"
    elif [ -f "$orig" ] && ! is_m4fix "$orig"; then
        rm -f "$orig"   # the game already has its own CrashRpt.dll back; drop the stale backup
    fi
    rm -f "$BIN/m4fix.ini" "$BIN/m4fix.log"
    ok "m4fix removed; original game files restored."
}

confirm() {
    [ "$YES" = 1 ] && return 0
    local a; read -r -p "$1 [y/N] " a
    case "$a" in [yY]*) return 0 ;; *) return 1 ;; esac
}

ACTION="" GIVEN="" YES=0
while [ $# -gt 0 ]; do
    case "$1" in
        install|uninstall|status) ACTION="$1" ;;
        --path) shift; GIVEN="${1:-}" ;;
        --yes|-y) YES=1 ;;
        -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die 1 "unknown argument: $1 (try --help)" ;;
    esac
    shift
done

printf '%sm4fix %s%s - Myst IV: Revelation navigation fix\n\n' "$c_bold" "$VERSION" "$c_off"
if [ -n "$GIVEN" ]; then
    BIN="$(bin_of "$GIVEN")" || die 2 "Myst4.exe not found under: $GIVEN"
else
    BIN="$(find_game)" || die 2 "Myst IV: Revelation not found. Pass its .app with --path \"/path/to/Myst IV Revelation.app\"."
fi

if [ "$ACTION" = status ]; then status; exit $?; fi

if [ -z "$ACTION" ]; then
    if status; then
        echo
        read -r -p "[R]einstall/update, [U]ninstall, or [Q]uit? " a
        case "$a" in [rR]*) ACTION=install ;; [uU]*) ACTION=uninstall ;; *) exit 3 ;; esac
    else
        echo
        confirm "Install m4fix into this game?" || exit 3
        ACTION=install
    fi
    YES=1
else
    status >/dev/null || true
fi

if game_running; then die 4 "Myst IV is running. Quit the game first."; fi
case "$ACTION" in
    install) [ "$YES" = 1 ] || confirm "Install m4fix into $BIN?" || exit 3; install ;;
    uninstall) [ "$YES" = 1 ] || confirm "Remove m4fix from $BIN?" || exit 3; uninstall ;;
esac
