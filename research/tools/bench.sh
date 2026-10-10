#!/bin/bash
# bench.sh OUTNAME ["memfix lockfix ..."]  - run the 31-hop navigation bench in the scratch bottle, save trace + samples.
. "$(dirname "$0")/env.sh"
cd "$ROOT"
B="$BOTTLE/drive_c/Myst 4"
T=research/tools
$T/killwine.sh 2>/dev/null; sleep 1
cp research/tracer/m4trace.dll research/tracer/m4launch.exe "$B/bin/"
if [ -n "$PROXY" ]; then cp build/CrashRpt.dll "$B/bin/CrashRpt.dll"; else cp "$B/bin/CrashRpt_orig.dll" "$B/bin/CrashRpt.dll"; fi
cp "research/routes/${ROUTE:-tomahna31}.txt" "$B/bench.txt"
rm -f "$B/trace.log" "$B/samples.bin" "$B/"*.on
for f in $2; do touch "$B/$f.on"; done
($T/runwine.sh "C:\\Myst 4\\bin\\m4launch.exe" > "$SCRATCH/$1.out" 2>&1 &)
for i in $(seq 1 60); do
  sleep 5
  grep -q "BENCH DONE" "$B/trace.log" 2>/dev/null && break
  grep -q "exited with" "$SCRATCH/$1.out" 2>/dev/null && { echo "game exited early"; break; }
done
sleep 1; $T/killwine.sh
mkdir -p "$SCRATCH/results"; cp "$B/trace.log" "$SCRATCH/results/$1.log"; cp "$B/samples.bin" "$SCRATCH/results/$1.samples" 2>/dev/null
grep -E "fix|profiler" "$SCRATCH/results/$1.log"
