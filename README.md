# m4fix — fast navigation for Myst IV: Revelation

Myst IV: Revelation is famously sluggish to move around in, even on fast
modern machines with SSDs: every click freezes the picture for a second or
more before the transition starts. **m4fix** removes that lag.

It's an unofficial, drop-in fix for the Steam and GOG releases (Windows, and the
CrossOver-based Mac version on Steam). It changes no game files on disk and
uninstalls cleanly.

| measured on the Steam Mac version (M2 Max) | original | with m4fix |
|---|---|---|
| click → arriving at the next node (median) | 1040 ms | **276 ms** |
| click → arriving at the next node (worst of 31 moves) | 3622 ms | **343 ms** |
| longest freeze during a move | up to 3.1 s | **≤ 0.26 s** |
| game launch → first playable view | 6.4 s | **1.6 s** |

The game's 0.5 s crossfade between nodes is kept. It just starts almost
immediately now.

## Why the game is slow

None of it is your disk. Reading a node's data takes ~5–30 ms; the time
goes to three problems that only appear on modern systems:

1. **Uncached image memory.** The game's 2002 image library (LEADTOOLS 13)
   puts every decoded texture tile in an *uncached*, memory-mapped kernel
   section, and creates and destroys hundreds of them per move. Decoding
   into uncached memory and reading it back is extremely slow on today's CPUs.
2. **Kernel mutexes in the sound engine.** It takes ~40,000 kernel-level
   locks per move while swapping sound banks.
3. **A hard-coded 550 ms freeze.** After every click the game disables input
   and waits a fixed 550 ms, with the picture frozen, before it even starts
   loading. It was meant to give the ambient sound a head start fading out on
   2004 hardware, but that fade is 2 s long and continues across the move anyway.

[docs/TECHNICAL.md](docs/TECHNICAL.md) has the full analysis, with addresses,
disassembly and measurements.

## Install

Download the latest `m4fix-x.y.z.zip` from
[Releases](../../releases) and unzip it. **Quit the game first.**

### Windows (Steam or GOG)

Run **`Windows\m4fix-setup.exe`**. It finds the game automatically (Steam
libraries, GOG, the original Ubisoft install) and asks before changing
anything. If Windows asks for administrator rights, that's because the game is
in `Program Files`.

If SmartScreen says "Windows protected your PC", choose *More info → Run
anyway*. The installer isn't code-signed.

Command line: `m4fix-setup.exe /install | /uninstall | /status [/path "<game folder>"] [/quiet]`

### Mac (Steam version)

Double-click **`Mac/Install m4fix.command`**. A Terminal window shows what it
found and asks before installing.

- The first time, macOS may refuse to open it because it's from an unidentified
  developer: right-click the file → **Open** → **Open**.
- If it reports that macOS blocked changes to the game, open **System Settings →
  Privacy & Security → App Management**, allow **Terminal**, and run it again.

Command line: `./m4fix.sh install | uninstall | status [--path "/path/to/Myst IV Revelation.app"] [--yes]`

### Manual install (any platform)

In the folder that contains `Myst4.exe`:

1. Rename `CrashRpt.dll` to `CrashRpt_orig.dll`.
2. Copy `CrashRpt.dll` and `m4fix.ini` from `Manual install/` into that folder.

On the Steam Mac version that folder is
`Myst IV Revelation.app/Contents/SharedSupport/myst_iv/support/myst_iv/drive_c/Myst 4/bin`.

## Settings

`m4fix.ini` sits next to `Myst4.exe`. Changes take effect the next time you start the game.

| setting | default | meaning |
|---|---|---|
| `LeadtoolsHeap` | `1` | Put decoded images in normal, cached memory. |
| `SoundLocks` | `1` | Use lightweight locks in the sound engine. |
| `TransitionDelayMs` | `100` | Pause between clicking and the move starting. `550` restores the original timing. Range 1–550. |
| `Log` | `0` | `1` writes `m4fix.log` next to the game, listing what was applied. |

## Uninstall

Run the installer again and choose *uninstall* (`m4fix-setup.exe /uninstall`,
or `./m4fix.sh uninstall`), or rename `CrashRpt_orig.dll` back to
`CrashRpt.dll`. Steam's *Verify integrity of game files* also restores the original.

## FAQ

**Will it affect my saves?** No. Saves and game data are never touched.

**A game update or "verify files" removed it.** Run the installer again. It
notices the restored original and backs it up before reinstalling.

**Is it safe if my copy is a different version?** Each patch first checks
that the exact original code is in place, and skips itself if not. Set
`Log=1` to see what was applied.

**Does it change how the game looks or sounds?** No. The visuals and the
2-second sound fade are unchanged. The only behavioural change is the shorter
pause after a click, which you can set back to `550`.

**Which versions are tested?** Fully tested on the Steam Mac version (Myst4.exe
under CrossOver 22): an automated 31-move benchmark, frame captures and full
installer runs. The Windows installer and the fix were exercised under Wine
using the game binaries the Mac version bundles (the GOG Windows build, v1.03).
They haven't been run on native Windows yet. If you try them there, please
report back in an issue.

## How it works

`Myst4.exe` loads `CrashRpt.dll` (an old crash reporter) at startup. m4fix
stands in for it. It forwards all four CrashRpt functions to the original
(kept as `CrashRpt_orig.dll`), and applies its fixes in memory before the
engine starts:

- **`LeadtoolsHeap`:** redirects `ltkrn13n.dll`'s memory-mapped bitmap sections
  to zero-filled heap blocks ([`fix/memfix.c`](fix/memfix.c)).
- **`SoundLocks`:** turns `DareDSound3D_bin_rd.dll`'s mutexes into critical
  sections with identical ownership rules ([`fix/lockfix.c`](fix/lockfix.c)).
- **`TransitionDelayMs`:** changes one verified constant in
  `m4_ai_global_rd.dll` when the engine loads it ([`fix/navfix.c`](fix/navfix.c)).

The first two work by patching individual DLLs' import tables, so nothing
outside those DLLs is affected.

## Building

Needs the 32-bit MinGW-w64 toolchain (`brew install mingw-w64`, or
`apt install gcc-mingw-w64-i686 binutils-mingw-w64-i686 zip`):

```bash
./build.sh
```

This produces `build/CrashRpt.dll`, `build/m4fix-setup.exe` (with the DLL embedded) and
`dist/m4fix-<version>.zip`. The research tools that produced the numbers above
are in [`research/`](research/README.md).

## Repository layout

| path | contents |
|---|---|
| `fix/` | the fix (CrashRpt proxy DLL), C, no C runtime |
| `installer/` | Windows setup (`setup.c`) and Mac installer (`mac/`) |
| `config/m4fix.ini` | default settings |
| `docs/TECHNICAL.md` | full analysis |
| `research/` | tracer, profiler, benchmark driver, archive parser, results |

## Disclaimer

m4fix is an unofficial fan project, not affiliated with or endorsed by Cyan
Worlds or Ubisoft. Myst is a trademark of Cyan Worlds, Inc. No game files are
included in this repository. Released under the [MIT License](LICENSE).
