#!/bin/bash
# Run a Windows program in the isolated test bottle using the game's bundled CrossOver wine.
. "$(dirname "$0")/env.sh"
export CX_ROOT="$CX" CX_BOTTLE_PATH="$SCRATCH/bottles"
exec "$CX/bin/wine" --bottle myst_iv --no-update --no-gui --workdir "C:/Myst 4/bin" "$@"
