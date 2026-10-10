# Research tools

The tools behind the measurements in [docs/TECHNICAL.md](../docs/TECHNICAL.md).
They run the Mac (CrossOver) version of the game in an **isolated copy** of
its bottle, so your real install and saves are only read, never changed.

## Setup

```bash
brew install mingw-w64
python3 -m venv .venv && .venv/bin/pip install -r research/tools/requirements.txt
./build.sh research                   # builds the fix plus research/tracer/m4trace.dll and m4launch.exe
research/tools/setup-bottle.sh        # creates scratch/bottles/myst_iv (≈700 MB; game data is symlinked)
```

If the game isn't at the default Steam location, set `M4_APP` to its `.app`.

## Benchmark

```bash
research/tools/bench.sh original ""            # 31-move walk, original CrashRpt.dll
PROXY=1 research/tools/bench.sh fixed ""       # same, with build/CrashRpt.dll (m4fix) installed
.venv/bin/python research/tools/hops.py scratch/results/fixed.log
```

A game window opens and drives itself for about 2.5 minutes; don't click in it.
`ROUTE=tomahna8` selects the short route. `research/tools/make_route.py "1 1 20" 30`
generates routes from the node graph.

The second argument enables tracer-side switches (files `C:\Myst 4\<name>.on`):

| switch | effect |
|---|---|
| `memfix`, `lockfix`, `navfix` | apply individual fixes from inside the tracer (for A/B tests) |
| `capture` | save presented frames around moves 4, 5 and 8 to `C:\Myst 4\frames\` (see `contact.py`) |

## Tools

| file | purpose |
|---|---|
| `tracer/tracer.c` | detours, timeline log, per-move stats, sampling profiler, benchmark driver |
| `tracer/capture.c` | D3D9 `Present` frame grabber |
| `tracer/launcher.c` | starts `Myst4.exe` suspended and makes its entry point `LoadLibrary` the tracer |
| `tools/genhooks.py` | resolves exported functions to bodies and computes relocatable prologues → `tracer/hooks.h` |
| `tools/hops.py` | per-move latency and long-frame report from a trace log |
| `tools/profile.py` | symbolises profiler samples (`-s` stacks, `-c NAME` callers) |
| `tools/m4b.py` | `UBI_BF_SIG` archive parser |
| `tools/x86dis.py`, `xref.py`, `callsites.py` | disassembly with import/export names, cross-references, import call sites |
| `tools/contact.py` | contact sheet of captured frames |
| `tools/runwine.sh`, `killwine.sh` | run or kill programs in the test bottle using the game's bundled wine |

`results/` holds the logs behind the published numbers (`original.log`, `m4fix.log`) and frame contact sheets.
