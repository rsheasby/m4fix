# Why Myst IV: Revelation navigates slowly — technical analysis

This documents how the navigation lag was taken apart, what causes it, and
exactly what m4fix changes. Addresses are RVAs in the GOG v1.03 binaries
(the build bundled in the Steam Mac release; `Myst4.exe` link time 2005-02-17).

- [Engine overview](#engine-overview)
- [How it was measured](#how-it-was-measured)
- [Cause 1 — LEADTOOLS uncached bitmap sections](#cause-1--leadtools-uncached-bitmap-sections)
- [Cause 2 — kernel mutexes in the sound engine](#cause-2--kernel-mutexes-in-the-sound-engine)
- [Cause 3 — the fixed 550 ms pre-transition freeze](#cause-3--the-fixed-550-ms-pre-transition-freeze)
- [Not a cause: the disabled prefetcher](#not-a-cause-the-disabled-prefetcher)
- [Results](#results)
- [What remains](#what-remains)
- [Data formats](#data-formats)

## Engine overview

`Myst4.exe` is a 34 KB stub; the engine ("thor", built on Ubisoft's "ares"
renderer and "ubi" base libraries) lives in ~50 DLLs in `bin/`. Almost every
C++ method is exported by its mangled name, which makes the binaries
unusually easy to read.

The world is a graph of **nodes** (`w<world>_z<zone>_n<node>`, ~620 of them).
Each node is a pre-rendered cube panorama plus overlays (video panels, water,
particles, hotspots, AI scripts). Moving to a node is a *context change*:

```
click on nav hotspot
  └─ ai::ContextChange(w,z,n)                      m4_aisdk_rd   0x6f00
       ├─ signal RequestChange → AiSndTransition::OnRequestContextChange   (vetoes, see cause 3)
       └─ ResourceManager::ContextChange(ContextIdentifier{u8 w, u8 z, u16 n})
            └─ next frames: LoaderManager::InitContext → LoadingRequest::Load × ~40
                 → TextureBox::LoadTextures (cube tiles) → ActivateContext → 500 ms crossfade
```

## How it was measured

All measurement ran in an isolated copy of the game's own CrossOver bottle
(see [research/README.md](../research/README.md)):

- **Tracer** (`research/tracer/tracer.c`): a DLL injected at `Myst4.exe`'s
  entry point that detours ~40 engine functions with calling-convention-agnostic
  enter/exit stubs (the return address is swapped for an exit thunk and kept on
  a per-thread shadow stack). It logs a timeline of context changes and aggregates
  hot functions per move.
- **Benchmark driver**: at the start of each `Engine::DoFrame` the tracer calls
  `ai::ContextChange(w,z,n)`. That's the same entry point a hotspot click uses, so
  the transition AI, prefetch and sound fades all run. It walks a fixed
  31-move route around Tomahna, with 4 s between moves.
- **Sampling profiler**: a thread suspends the main thread every ~1 ms while a
  frame has run for more than 30 ms, recording EIP and call-validated return
  addresses from the stack, symbolised offline against the DLL export tables.
- **Frame grabber**: hooks `IDirect3DDevice9::Present` and saves every
  presented frame around chosen moves. This shows what the player actually sees.

The baseline numbers: median 1040 ms from click to the new node being
active, worst 3622 ms. Each move had one giant frame of 130–3100 ms.

## Cause 1 — LEADTOOLS uncached bitmap sections

Each node's panorama is a cube of 6 faces × 6×6 tiles of 128×128 pixels (a
768² face). Tiles are JPEGs, or `.zap` (an RGB JPEG followed by a grayscale
alpha JPEG); fully transparent tiles are omitted. A second cube of 32×32
grayscale depth tiles ("z layer") sits alongside. So a new node means ~400–850
small image decodes, all through LEADTOOLS 13 (`ltfil13n.dll`
`L_LoadBitmapMemory`, ordinal 118), called from `gfx_tools_rd.dll`, which then
copies pixels out row by row with `L_GetBitmapRow`.

Profiling the original game: **65 %** of the time spent in long frames was in
`ZwMapViewOfSection`, `ZwClose`, `ZwCreateSection` and `ZwUnmapViewOfSection`.
They come from `ltkrn13n.dll!L_AllocateBitmap` (RVA `0x1790`). Its
"conventional memory" branch is:

```asm
1fff1b0e: push 0                 ; lpName
          push eax               ; dwMaximumSizeLow = size + 31
          push 0                 ; dwMaximumSizeHigh
          push 0x18000004        ; SEC_NOCACHE | SEC_COMMIT | PAGE_READWRITE
          push 0
          push -1                ; INVALID_HANDLE_VALUE (pagefile-backed)
          call CreateFileMappingA
          ...
          call MapViewOfFile     ; FILE_MAP_WRITE
```

`L_FreeBitmap` then calls `UnmapViewOfFile` and `CloseHandle`. The same
`0x18000004` pattern appears in `L_InitBitmap` and twice in `L_ChangeBitmapHeight`.

- Every decoded tile creates, maps, unmaps and closes a kernel section object.
- **`SEC_NOCACHE` maps those pages uncached.** The JPEG decoder writes pixels
  into them (`L_PutBitmapRow`), and the engine reads them back row by row, all
  without the CPU cache. On 2004 hardware the cost was comparatively small and
  hidden behind slow disks. On a modern CPU it's a severe penalty, which is
  why the game is slow on fast Windows PCs too. (This penalty can't be measured
  under Wine, which can't map uncached memory. There the cost shows up instead
  as wineserver round trips for every section call.)

**Fix (`fix/memfix.c`):** patch `ltkrn13n.dll`'s import table entries for
`CreateFileMappingA`, `MapViewOfFile`, `UnmapViewOfFile`, `CloseHandle` and
`DuplicateHandle`.

- Anonymous, unnamed sections become zero-filled blocks from a private
  low-fragmentation heap. The returned "handle" is the bookkeeping pointer, and
  views map to the block.
- Everything else (real files, named sections, real handles) is forwarded,
  with `SEC_NOCACHE` stripped.
- Nothing in the game passes these handles anywhere else. The only other
  consumer, `L_CreateLeadDC`'s `DuplicateHandle`, is unused and handled anyway.

Peak live memory in a full benchmark was 1 MB; nothing leaked (0 live sections at exit).

## Cause 2 — kernel mutexes in the sound engine

With cause 1 fixed, the next hot spot was `ZwReleaseMutant` +
`ZwWaitForMultipleObjects`, at ~25 % of the remaining hitch time.

`DareDSound3D_bin_rd.dll` (Ubisoft's "Dare" sound engine) guards its state
with two global locks:

```c
SND_fn_vInitThreadSnd (0x47a10):  g_mutexSnd  = CreateMutexA(NULL, TRUE, NULL);   // initial owner!
                                  g_mutexTool = CreateMutexA(NULL, TRUE, NULL);
_SND_fn_vEnterCriticalSectionThreadSnd_ (0x47ac0): WaitForSingleObject(g_mutexSnd, INFINITE); flag = 1;
SND_fn_vQuitCriticalSectionThreadSnd     (0x47ba0): flag = 0; ReleaseMutex(g_mutexSnd);
_EnterCriticalSectionTool_ / _QuitCriticalSectionTool (0x47bc0 / 0x47ca0): same for g_mutexTool
```

Because the creating thread is the initial owner, in practice every
Enter/Quit is a *recursive* acquire and release by the main thread. Each one
is a kernel call on Windows, and a wineserver round trip under Wine. The
benchmark counted **1.3 million** of these over 31 moves, ~40,000 per move
(during `snd::Manager::CommitBankSet` bank loading and unloading) and thousands
per second even when idle. A debug helper, `_MouchardThreadTool` (0x47cc0;
*mouchard* is French for "snitch"), also `sprintf`s the caller's file and line
into a buffer around lock calls.

**Fix (`fix/lockfix.c`):** patch the sound DLL's imports of `CreateMutexA`,
`WaitForSingleObject`, `ReleaseMutex` and `CloseHandle`. Unnamed mutexes
become `CRITICAL_SECTION`s that track owner and recursion exactly like a mutex.

- Initial ownership is preserved.
- `ReleaseMutex` by a non-owner fails with `ERROR_NOT_OWNER`.
- Timed waits fall back to polling `TryEnterCriticalSection`.
- Waits on threads, events and named mutexes pass straight through.

## Cause 3 — the fixed 550 ms pre-transition freeze

Frame captures showed what the player experiences on each click:

| time after click | on screen |
|---|---|
| 0 – 550 ms | old view, completely static; input disabled |
| 550 ms – ~1.5 s (orig.) | no frames at all: the load hitch (causes 1 and 2) |
| then 500 ms | crossfade to the new node |

The first phase comes from `m4_ai_global_rd.dll`:

```asm
AiSndTransition::OnRequestContextChange (0x10900):
    ai::ContextPrefetch(w,z,n)
    ai::InputEnable(false)
    *allow = false                 ; veto the immediate change
    FadeOutSound(w,z,n)

AiSndTransition::FadeOutSound (0x10720):
    snd::Manager::StopSoundNextNode(w,z,n)   ; = BeforeContextChange: fade sounds not in the next node
    timer.Stop(); timer.Reset()
1077e: push 0x226                  ; 550 ms
    timer.SetTick(); timer.Start() ; → OnTimer → ChangeContextNow
```

The sound fade it waits for is `SoundLoader::m_transitionTime`
(`snd::Manager + 0x220 + 0x18`), which is **2.0 s**, and it keeps running across the
context change. So the 550 ms doesn't wait for anything to finish; it is pure
dead time with a frozen picture.

**Fix (`fix/navfix.c`):** the AI library is loaded at runtime by
`m4_thor_rd.dll` (`thor::Library`), so m4fix hooks that DLL's `LoadLibraryA`.
Once `m4_ai_global_rd.dll` is mapped, it checks that the 5 bytes at `0x1077e` are
exactly `68 26 02 00 00`, then replaces the immediate with `TransitionDelayMs`
(default 100). Anything else is left untouched.

## Not a cause: the disabled prefetcher

The engine has a node-neighbour prefetcher (`thor::prefetch::NodePrefetchStrategy`),
but it ships disabled:

- `AddRequestContext` (0x93770) is `ret 0xc`.
- `ReadNodeDatabse` [sic] (0x2a0d0) is a shared `ret` stub.

So the adjacency map that `OnRequestContext` (0xe6670) consults is never
filled, and `nodedb.ndb` is never read. (That database is also stale: 18 of
its ids reference nodes that no longer exist, and 249 nodes have no entry.)

`ai::ContextPrefetch` at click time still queues the destination. A prefetch job (0xe62f0)
only reads the node's archive bytes in 32 KB chunks on a worker thread. That's
worth ~5–30 ms on an SSD and does no decoding, so re-enabling the neighbour
prefetch would gain little.

## Results

31-move walk, CrossOver 22 on an Apple M2 Max, the same tracer build for both runs
(raw logs in [`research/results/`](../research/results)):

| | original | m4fix |
|---|---|---|
| click → node active, median | 1040 ms | 276 ms |
| mean / worst | 1374 / 3622 ms | 259 / 343 ms |
| longest frame per move | 127–3101 ms | 57–262 ms |
| launch → first node active | 6.4 s | 1.6 s |

Contribution of each fix to the median (same tracer, earlier runs):

| configuration | median click→active |
|---|---|
| original | 1007–1040 ms |
| + LeadtoolsHeap | 772–791 ms |
| + SoundLocks | 721 ms |
| + TransitionDelayMs=100 | 276–281 ms |

Frame captures before and after are in
[`research/results/frames-before-navfix.png`](../research/results/frames-before-navfix.png)
and [`frames-m4fix.png`](../research/results/frames-m4fix.png).

## What remains

The remaining per-move hitch is about 60–260 ms under Rosetta + Wine, and
should be much less on native Windows. About half of it is LEADTOOLS' own
2002-era JPEG decoder (`lfcmp13n.dll`). A further ~10 % is allocator churn:
LEADTOOLS `calloc`s a large work buffer per decode, which its CRT heap serves
with fresh `VirtualAlloc`/`VirtualFree` and page zeroing. A possible future
step is to decode the destination node's tiles in parallel worker threads at
click time (the destination is known from `ContextPrefetch`), and serve
`L_LoadBitmapMemory` from that cache.

## Data formats

### `.m4b` archives (`UBI_BF_SIG`)

```
u32 len; char sig[len]       "UBI_BF_SIG\0"
u32 1; u32 0
dir:  u8  ndirs;  ndirs × { u32 len; char name[len]; dir }
      u32 nfiles; nfiles × { u32 len; char name[len]; u32 size; u32 offset }
```

Offsets are relative to the start of the archive. Archives nest: `data.m4b`
contains one `.m4b` per node, and each node contains `cube/layer_default.m4b`
and so on. A parser is in [`research/tools/m4b.py`](../research/tools/m4b.py).

### `.zap` tiles

```
u32 header_size (32); u32 2; u32 10; u32 10; u32 rgb_jpeg_size; u32 alpha_jpeg_size; u32 width; u32 height
rgb JPEG (3 components) ; alpha JPEG (1 component)
```

### `nodedb.ndb`

```
"ubi/b0-l"; u32 count
count × { u32 n; id key; id neighbours[n] }      id = { u8 world; u8 zone; u16 node }
```
