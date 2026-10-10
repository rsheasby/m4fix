# How m4fix works — exactly what it patches

This page lists every change m4fix makes, as before/after diffs: what happens
on disk, and every byte and pointer it changes in memory. For *why* these
are the problems, see [TECHNICAL.md](TECHNICAL.md).

All addresses are **RVAs** (offsets from the module's load address) in the
GOG v1.03 game binaries, the build the Steam Mac release ships. Source files
are linked inline.

- [The big picture](#the-big-picture)
- [On disk](#on-disk)
- [Getting loaded: the CrashRpt.dll stand-in](#getting-loaded-the-crashrptdll-stand-in)
- [Patch 1 — LEADTOOLS bitmap memory (`LeadtoolsHeap`)](#patch-1--leadtools-bitmap-memory-leadtoolsheap)
- [Patch 2 — sound engine locks (`SoundLocks`)](#patch-2--sound-engine-locks-soundlocks)
- [Patch 3 — the pre-transition delay (`TransitionDelayMs`)](#patch-3--the-pre-transition-delay-transitiondelayms)
- [Safety checks](#safety-checks)
- [Checking it yourself](#checking-it-yourself)

## The big picture

m4fix never rewrites game code on disk, and almost never in memory. Two of
the three fixes redirect **import table** entries. Every Windows DLL has an
import address table (IAT): a list of pointers it calls through to reach
Windows APIs. m4fix points a handful of those entries, in one specific DLL
each, at its own replacement functions. Only that DLL's calls are affected;
the rest of the game and Windows itself are unchanged.

The third fix changes a single 32-bit constant (4 bytes) in the game's AI
code, and only after checking the surrounding instruction is exactly what's expected.

```
Myst4.exe
 ├─ CrashRpt.dll  ← m4fix (forwards to CrashRpt_orig.dll, applies patches 1–3)
 ├─ m4_thor_rd.dll ─ IAT: LoadLibraryA ──────────────► m4fix hook (for patch 3)
 │    ├─ gfx_tools_rd.dll
 │    │    └─ ltkrn13n.dll ─ IAT: 5 entries ─────────► m4fix (patch 1)
 │    └─ m4_snd_rd.dll
 │         └─ DareDSound3D_bin_rd.dll ─ IAT: 4 entries ► m4fix (patch 2)
 └─ (loaded later by m4_thor) m4_ai_global_rd.dll ─ 4 bytes @ 0x1077f (patch 3)
```

## On disk

```diff
  Myst 4/bin/
- CrashRpt.dll          87,040 bytes   original crash reporter
+ CrashRpt.dll          12,800 bytes   m4fix
+ CrashRpt_orig.dll     87,040 bytes   the original, renamed, unchanged
+ m4fix.ini                            settings
  (every other file untouched)
```

Uninstalling renames `CrashRpt_orig.dll` back and deletes `m4fix.ini` / `m4fix.log`.

## Getting loaded: the CrashRpt.dll stand-in

`Myst4.exe` imports three functions from `crashrpt.dll`:

| `Myst4.exe` IAT slot | import |
|---|---|
| `0x3000` | `crashrpt.dll!CrashRpt_Install` |
| `0x3004` | `crashrpt.dll!CrashRpt_AddFile` |
| `0x3008` | `crashrpt.dll!CrashRpt_Uninstall` |

Windows loads whatever `CrashRpt.dll` sits next to the exe, so m4fix takes its
place and exports the same four functions (including
`CrashRpt_GenerateErrorReport`) under the same names and ordinals. Each one
forwards to the original ([`fix/m4fix.c`](../fix/m4fix.c)):

```c
void *CrashRpt_Install(void *getlog, const char *to, const char *subject)
{
    install();                                   // apply patches 1–3 (once)
    f = GetProcAddress(LoadLibrary("CrashRpt_orig.dll"), "CrashRpt_Install");
    return f ? f(getlog, to, subject) : NULL;    // original behaviour
}
```

The patches are applied in m4fix's `DllMain`. By then Windows has mapped every
DLL the game links against, but none of the engine has run yet. If a loader
ever runs `DllMain` earlier than that, the first call to `CrashRpt_Install`
applies them instead. That call happens at the very top of `WinMain`, before the
engine is created. `install()` only runs once.

## Patch 1 — LEADTOOLS bitmap memory (`LeadtoolsHeap`)

Source: [`fix/memfix.c`](../fix/memfix.c)

### What the game does

`ltkrn13n.dll!L_AllocateBitmap` (RVA `0x1790`) backs every decoded image
with a pagefile section. At RVA `0x1b0e` (with the same pattern at `0x11eb`,
`0x1525`, `0x16b7`, `0x1de9` and `0x1e56`):

```
6a 00                push 0                   ; lpName
50                   push eax                 ; size + 31
6a 00                push 0                   ; size high
68 04 00 00 18       push 0x18000004          ; SEC_NOCACHE | SEC_COMMIT | PAGE_READWRITE
6a 00                push 0                   ; security
6a ff                push -1                  ; INVALID_HANDLE_VALUE: no file, pagefile-backed
ff 15 a8 b2 05 20    call [CreateFileMappingA]
...
6a 00 6a 00 6a 00    push 0, 0, 0
6a 02 56             push FILE_MAP_WRITE, handle
ff 15 b8 b2 05 20    call [MapViewOfFile]
```

`L_FreeBitmap` (RVA `0x1ff0`) undoes it at `0x20e2`:

```
ff 15 bc b2 05 20    call [UnmapViewOfFile]
ff 15 b4 b2 05 20    call [CloseHandle]
```

### The patch: 5 import-table entries in `ltkrn13n.dll`

The code above stays byte-for-byte the same. What changes is where those
`call [IAT slot]` instructions land:

```diff
  ltkrn13n.dll IAT        before                              after
- 0x6b2a8                 KERNEL32!CreateFileMappingA     →   
+ 0x6b2a8                                                     m4fix!my_CreateFileMappingA
- 0x6b2b8                 KERNEL32!MapViewOfFile          →
+ 0x6b2b8                                                     m4fix!my_MapViewOfFile
- 0x6b2bc                 KERNEL32!UnmapViewOfFile        →
+ 0x6b2bc                                                     m4fix!my_UnmapViewOfFile
- 0x6b2b4                 KERNEL32!CloseHandle            →
+ 0x6b2b4                                                     m4fix!my_CloseHandle
- 0x6b234                 KERNEL32!DuplicateHandle        →
+ 0x6b234                                                     m4fix!my_DuplicateHandle
```

### Behaviour, before and after

```diff
  CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, 0x18000004, 0, size, NULL)
- → kernel creates an uncached pagefile section object; returns a kernel handle
+ → HeapAlloc(private LFH heap, HEAP_ZERO_MEMORY, size)   // zero-filled, like a section
+   returns a pointer to m4fix's bookkeeping record as the "handle"

  MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, 0)
- → kernel maps the section's pages, uncached (SEC_NOCACHE)
+ → returns the heap block (normal cached memory)

  UnmapViewOfFile(view) + CloseHandle(h)
- → kernel unmaps pages, destroys the section object
+ → HeapFree once both the view and the handle are gone
```

Calls that aren't for this kind of memory go straight to Windows:

```c
if (file != INVALID_HANDLE_VALUE || name || size_high || !size || (prot & SEC_IMAGE))
    return CreateFileMappingA(file, sa, prot & ~SEC_NOCACHE, size_high, size, name);
```

`MapViewOfFile`, `UnmapViewOfFile`, `CloseHandle` and `DuplicateHandle` only
act on handles and views that m4fix itself issued; anything else is passed
through unchanged.

## Patch 2 — sound engine locks (`SoundLocks`)

Source: [`fix/lockfix.c`](../fix/lockfix.c)

### What the game does

`DareDSound3D_bin_rd.dll` creates two kernel mutexes at startup and uses them as
its locks:

```
SND_fn_vInitThreadSnd                        (RVA 0x47a10)
  6a 00 6a 01 6a 00    push 0, TRUE, 0       ; unnamed, bInitialOwner = TRUE
  ff d6                call esi              ; CreateMutexA  → [0x1013a640]
  ...                                        ; second one   → [0x1013a7d4]

_SND_fn_vEnterCriticalSectionThreadSnd_      (RVA 0x47ac0)
  6a ff 50             push INFINITE, h
  ff d6                call esi              ; WaitForSingleObject
SND_fn_vQuitCriticalSectionThreadSnd         (RVA 0x47ba0)
  50                   push h
  ff 15 58 a0 04 10    call [ReleaseMutex]
```

(`_EnterCriticalSectionTool_` at `0x47bc0` and `_QuitCriticalSectionTool` at `0x47ca0` do the same for the second mutex.)

Each of those calls is a kernel transition. The benchmark counted ~40,000 of
them per node change, and 1.3 million over 31 moves.

### The patch: 4 import-table entries in `DareDSound3D_bin_rd.dll`

```diff
  DareDSound3D_bin_rd.dll IAT   before                           after
- 0x4a040                       KERNEL32!CreateMutexA         →
+ 0x4a040                                                        m4fix!my_CreateMutexA
- 0x4a054                       KERNEL32!WaitForSingleObject  →
+ 0x4a054                                                        m4fix!my_WaitForSingleObject
- 0x4a058                       KERNEL32!ReleaseMutex         →
+ 0x4a058                                                        m4fix!my_ReleaseMutex
- 0x4a044                       KERNEL32!CloseHandle          →
+ 0x4a044                                                        m4fix!my_CloseHandle
```

### Behaviour, before and after

```diff
  CreateMutexA(NULL, TRUE, NULL)
- → kernel mutex object, owned by the calling thread
+ → CRITICAL_SECTION, entered once by the calling thread (same initial ownership)

  WaitForSingleObject(mutex, INFINITE)
- → syscall; recursive acquire
+ → EnterCriticalSection (user-mode, recursive); owner/recursion tracked

  WaitForSingleObject(mutex, timeout)
+ → TryEnterCriticalSection, polled until the timeout; WAIT_TIMEOUT on expiry

  ReleaseMutex(mutex)
- → syscall
+ → LeaveCriticalSection; fails with ERROR_NOT_OWNER if the caller doesn't own it, like a mutex

  CloseHandle(mutex)
- → kernel object destroyed
+ → DeleteCriticalSection (or left alone if it's still owned)
```

Named mutexes, and waits on anything that isn't one of these mutexes (the sound
thread handle, events), go straight to Windows.

## Patch 3 — the pre-transition delay (`TransitionDelayMs`)

Source: [`fix/navfix.c`](../fix/navfix.c)

### What the game does

When you click to move, `AiSndTransition::OnRequestContextChange` (RVA `0x10900`)
disables input, then `AiSndTransition::FadeOutSound` (RVA `0x10720`) starts a
fixed timer. Only when that timer fires does the node change begin:

```
m4_ai_global_rd.dll
10770: ff 15 28 e5 04 10    call [ai::Timer::Stop]
10776: 8b ce                mov  ecx, esi
10778: ff 15 2c e5 04 10    call [ai::Timer::Reset]
1077e: 68 26 02 00 00       push 0x226                 ; 550 ms
10783: 8b ce                mov  ecx, esi
10785: ff 15 30 e5 04 10    call [ai::Timer::SetTick]
1078b: 8b ce                mov  ecx, esi
1078d: ff 15 34 e5 04 10    call [ai::Timer::Start]    ; → OnTimer → ChangeContextNow
```

### The patch: 4 bytes

```diff
  m4_ai_global_rd.dll @ RVA 0x1077e
- 68 26 02 00 00       push 0x226        ; 550 ms
+ 68 64 00 00 00       push 0x64         ; 100 ms  (TransitionDelayMs, 1–550)
```

That's the only change to game code anywhere in m4fix.

### Getting to the right moment

`m4_ai_global_rd.dll` isn't linked into the game. The engine loads it at runtime
with `LoadLibraryA`, from `m4_thor_rd.dll` (the AI library list is in
`data.dsc`). So m4fix also redirects one import table entry there:

```diff
  m4_thor_rd.dll IAT     before                     after
- 0x10907c               KERNEL32!LoadLibraryA   →
+ 0x10907c                                          m4fix!my_LoadLibraryA
```

```c
HMODULE my_LoadLibraryA(LPCSTR name)
{
    HMODULE m = LoadLibraryA(name);                      // unchanged behaviour
    if (!patched) try_patch(GetModuleHandleA("m4_ai_global_rd.dll"));
    return m;
}
```

`try_patch` checks that the five bytes at `0x1077e` are exactly
`68 26 02 00 00`. If they are, it writes the new immediate (making the page
writable only for that write) and flushes the instruction cache. If they
aren't, it marks itself done and changes nothing. The `LoadLibraryA` hook stays
in place but just passes calls through.

## Safety checks

- **No guessing about code.** The only code write (patch 3) is
  conditional on an exact 5-byte match. Patches 1 and 2 never touch code.
- **Imports are found by name.** m4fix walks each DLL's import descriptors
  and matches `KERNEL32.dll` and the function name, rather than using
  hard-coded slot addresses. The RVAs above are for reference only.
- **Narrow reach.** Each table patch affects one DLL. For example, `CloseHandle` is only
  redirected for calls made from `ltkrn13n.dll` (patch 1) and
  `DareDSound3D_bin_rd.dll` (patch 2), and even then only for handles m4fix itself created.
- **Each patch is optional.** Set `LeadtoolsHeap=0`, `SoundLocks=0` or
  `TransitionDelayMs=550` and the corresponding patch isn't installed at all.
- **No dependencies.** m4fix is built without a C runtime and imports only
  `KERNEL32` and `USER32`, so it adds nothing for the game to load.
- **Crash reporting still works.** All four CrashRpt functions forward
  to the original. If `CrashRpt_orig.dll` is missing they quietly do nothing,
  which is harmless because the game ignores their return values.

## Checking it yourself

Set `Log=1` in `m4fix.ini` and start the game. `m4fix.log` will contain:

```
m4fix 1.0.0 (C:\Myst 4\bin\m4fix.ini)
LeadtoolsHeap: 5/5 imports patched
SoundLocks: 4/4 imports patched
TransitionDelayMs=100: hook 1
```

With a debugger attached to `Myst4.exe` you can confirm the same things directly:
- the IAT slots listed above point into `CrashRpt.dll` (m4fix);
- once the game has started (the AI libraries load during startup), `m4_ai_global_rd.dll+0x1077e` reads `68 64 00 00 00`.
