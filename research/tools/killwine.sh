#!/bin/bash
# Kill every wine process of the isolated test bottle (never touches the real game bottle).
. "$(dirname "$0")/env.sh"
WINEPREFIX="$BOTTLE" "$CX/bin/wineserver" -k
