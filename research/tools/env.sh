# Shared paths for the research scripts. Override M4_APP if your game lives elsewhere.
ROOT="$(cd "$(dirname "${BASH_SOURCE:-$0}")/../.." && pwd)"
M4_APP="${M4_APP:-$HOME/Library/Application Support/Steam/steamapps/common/Myst IV Revelation/Myst IV Revelation.app}"
CX="$M4_APP/Contents/SharedSupport/myst_iv"                     # CrossOver runtime inside the app
GAME_BIN="$CX/support/myst_iv/drive_c/Myst 4/bin"                 # pristine game binaries (read-only use)
SCRATCH="$ROOT/scratch"                                           # git-ignored work area
BOTTLE="$SCRATCH/bottles/myst_iv"                                 # isolated test bottle (see setup-bottle.sh)
