#!/bin/bash
# Create the isolated test bottle: a copy of the game's CrossOver bottle with the
# 7 GB of game data symlinked, a private copy of bin/ (so DLLs can be swapped),
# and windowed mode. The real game install is only read.
set -e
. "$(dirname "$0")/env.sh"
SRC="$CX/support/myst_iv"
[ -f "$GAME_BIN/Myst4.exe" ] || { echo "game not found; set M4_APP" >&2; exit 1; }
mkdir -p "$SCRATCH/bottles"
rsync -a --exclude "drive_c/Myst 4" "$SRC/" "$BOTTLE/"
G="$BOTTLE/drive_c/Myst 4"; mkdir -p "$G/save"
for f in "$SRC/drive_c/Myst 4"/*; do
    case "$(basename "$f")" in bin|save) ;; *) ln -sfn "$f" "$G/" ;; esac
done
rm -rf "$G/bin"; cp -a "$GAME_BIN" "$G/bin"
cp "$G/bin/CrashRpt.dll" "$G/bin/CrashRpt_orig.dll"
printf '{graphic full_screen ( false ) }\r\n' > "$G/bin/game.dsc"
echo "test bottle ready: $BOTTLE"
