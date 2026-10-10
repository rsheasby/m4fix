/*
 * memfix.c - replace LEADTOOLS 13 "conventional" bitmap memory with heap memory.
 *
 * ltkrn13n.dll backs every bitmap with an anonymous pagefile section created as
 *     CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, SEC_COMMIT|SEC_NOCACHE|PAGE_READWRITE, 0, size, NULL)
 * and maps a view of it. On Windows SEC_NOCACHE makes the pixels *uncached*
 * memory, so JPEG decoding and the engine's row-by-row readback crawl; under
 * Wine each section is also a wineserver round trip. Myst IV decodes hundreds
 * of small cube tiles per node, so this dominates navigation time.
 *
 * We patch ltkrn13n.dll's import table so that anonymous, unnamed sections are
 * emulated with zero-filled heap blocks behind fake handles. Every other call
 * (real files, named sections, real handles) is forwarded unchanged, minus
 * SEC_NOCACHE.
 */
#include <windows.h>
#include "memfix.h"

#define NBUCKETS 1024

typedef struct Sec {
    struct Sec *next;
    BYTE *mem;
    DWORD size;
    LONG refs;      /* open handles (CreateFileMapping + DuplicateHandle) */
    LONG views;     /* mapped views */
} Sec;

static Sec *buckets[NBUCKETS];
static CRITICAL_SECTION lock;
static HANDLE heap;
static volatile LONG live_bytes, peak_bytes, nsections;

static HANDLE (WINAPI *real_CreateFileMappingA)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCSTR);
static LPVOID (WINAPI *real_MapViewOfFile)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
static BOOL (WINAPI *real_UnmapViewOfFile)(LPCVOID);
static BOOL (WINAPI *real_CloseHandle)(HANDLE);
static BOOL (WINAPI *real_DuplicateHandle)(HANDLE, HANDLE, HANDLE, LPHANDLE, DWORD, BOOL, DWORD);

static unsigned bucket_of(const void *p) { return ((DWORD)p >> 4) % NBUCKETS; }

/* Fake handles are the Sec pointers themselves; they are only ever handed to ltkrn13n. */
static Sec *find_handle(HANDLE h)
{
    for (Sec *s = buckets[bucket_of(h)]; s; s = s->next)
        if ((HANDLE)s == h) return s;
    return NULL;
}

static Sec *find_view(LPCVOID p)
{
    /* Views always start at s->mem (LEADTOOLS maps whole sections at offset 0). */
    for (int i = 0; i < NBUCKETS; i++)
        for (Sec *s = buckets[i]; s; s = s->next)
            if ((const BYTE *)p >= s->mem && (const BYTE *)p < s->mem + s->size) return s;
    return NULL;
}

static void release(Sec *s)
{
    if (s->refs > 0 || s->views > 0) return;
    Sec **pp = &buckets[bucket_of(s)];
    while (*pp != s) pp = &(*pp)->next;
    *pp = s->next;
    InterlockedExchangeAdd(&live_bytes, -(LONG)s->size);
    InterlockedDecrement(&nsections);
    HeapFree(heap, 0, s->mem);
    HeapFree(heap, 0, s);
}

static HANDLE WINAPI my_CreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD prot, DWORD hi, DWORD lo, LPCSTR name)
{
    if (file != INVALID_HANDLE_VALUE || name || hi || !lo || (prot & SEC_IMAGE)) {
        EnterCriticalSection(&lock);
        BOOL fake = file != INVALID_HANDLE_VALUE && find_handle(file);
        LeaveCriticalSection(&lock);
        if (fake) { SetLastError(ERROR_INVALID_HANDLE); return NULL; }
        return real_CreateFileMappingA(file, sa, prot & ~SEC_NOCACHE, hi, lo, name);
    }
    Sec *s = HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof *s);
    if (!s) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    s->mem = HeapAlloc(heap, HEAP_ZERO_MEMORY, lo);   /* sections are zero-filled */
    if (!s->mem) { HeapFree(heap, 0, s); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    s->size = lo; s->refs = 1;
    EnterCriticalSection(&lock);
    unsigned b = bucket_of(s);
    s->next = buckets[b]; buckets[b] = s;
    LeaveCriticalSection(&lock);
    LONG live = InterlockedExchangeAdd(&live_bytes, lo) + lo;
    if (live > peak_bytes) peak_bytes = live;
    InterlockedIncrement(&nsections);
    SetLastError(0);
    return (HANDLE)s;
}

static LPVOID WINAPI my_MapViewOfFile(HANDLE h, DWORD access, DWORD offhi, DWORD offlo, SIZE_T bytes)
{
    EnterCriticalSection(&lock);
    Sec *s = find_handle(h);
    LPVOID r = NULL;
    if (s) {
        if (offhi == 0 && offlo < s->size) { s->views++; r = s->mem + offlo; }
        else SetLastError(ERROR_INVALID_PARAMETER);
    }
    LeaveCriticalSection(&lock);
    return s ? r : real_MapViewOfFile(h, access, offhi, offlo, bytes);
}

static BOOL WINAPI my_UnmapViewOfFile(LPCVOID p)
{
    EnterCriticalSection(&lock);
    Sec *s = find_view(p);
    if (s) { s->views--; release(s); }
    LeaveCriticalSection(&lock);
    return s ? TRUE : real_UnmapViewOfFile(p);
}

static BOOL WINAPI my_CloseHandle(HANDLE h)
{
    EnterCriticalSection(&lock);
    Sec *s = find_handle(h);
    if (s) { s->refs--; release(s); }
    LeaveCriticalSection(&lock);
    return s ? TRUE : real_CloseHandle(h);
}

static BOOL WINAPI my_DuplicateHandle(HANDLE sp, HANDLE src, HANDLE tp, LPHANDLE out, DWORD access, BOOL inherit, DWORD opts)
{
    EnterCriticalSection(&lock);
    Sec *s = find_handle(src);
    if (s && sp == GetCurrentProcess() && tp == GetCurrentProcess()) {
        s->refs++;
        if (out) *out = src;
        if (opts & DUPLICATE_CLOSE_SOURCE) { s->refs--; release(s); }
    }
    LeaveCriticalSection(&lock);
    if (s) return TRUE;
    return real_DuplicateHandle(sp, src, tp, out, access, inherit, opts);
}

/* Point one IAT slot of `mod` (imported from kernel32) at `fn`; returns the previous target. */
void *memfix_patch_iat(HMODULE mod, const char *dll, const char *name, void *fn)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + dir.VirtualAddress); d->Name; d++) {
        if (lstrcmpiA((char *)base + d->Name, dll)) continue;
        IMAGE_THUNK_DATA *names = (void *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *iat = (void *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (void *)(base + names->u1.AddressOfData);
            if (lstrcmpA((char *)ibn->Name, name)) continue;
            DWORD old;
            void *prev = (void *)iat->u1.Function;
            VirtualProtect(&iat->u1.Function, 4, PAGE_READWRITE, &old);
            iat->u1.Function = (DWORD)fn;
            VirtualProtect(&iat->u1.Function, 4, old, &old);
            return prev;
        }
    }
    return NULL;
}

int memfix_install(void)
{
    HMODULE lt = GetModuleHandleA("ltkrn13n.dll");
    if (!lt) return 0;
    InitializeCriticalSection(&lock);
    heap = HeapCreate(0, 0, 0);
    if (!heap) return 0;
    ULONG lfh = 2;  /* low-fragmentation heap: fast reuse of tile-sized blocks */
    HeapSetInformation(heap, HeapCompatibilityInformation, &lfh, sizeof lfh);
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    real_CreateFileMappingA = (void *)GetProcAddress(k32, "CreateFileMappingA");
    real_MapViewOfFile = (void *)GetProcAddress(k32, "MapViewOfFile");
    real_UnmapViewOfFile = (void *)GetProcAddress(k32, "UnmapViewOfFile");
    real_CloseHandle = (void *)GetProcAddress(k32, "CloseHandle");
    real_DuplicateHandle = (void *)GetProcAddress(k32, "DuplicateHandle");
    int n = 0;
    n += !!memfix_patch_iat(lt, "kernel32.dll", "CreateFileMappingA", my_CreateFileMappingA);
    n += !!memfix_patch_iat(lt, "kernel32.dll", "MapViewOfFile", my_MapViewOfFile);
    n += !!memfix_patch_iat(lt, "kernel32.dll", "UnmapViewOfFile", my_UnmapViewOfFile);
    n += !!memfix_patch_iat(lt, "kernel32.dll", "CloseHandle", my_CloseHandle);
    n += !!memfix_patch_iat(lt, "kernel32.dll", "DuplicateHandle", my_DuplicateHandle);
    return n;
}

void memfix_stats(long *live, long *peak, long *count)
{
    *live = live_bytes; *peak = peak_bytes; *count = nsections;
}
