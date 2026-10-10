/*
 * lockfix.c - replace the sound engine's kernel mutexes with critical sections.
 *
 * DareDSound3D_bin_rd.dll guards its state with unnamed Win32 mutexes
 * (CreateMutexA / WaitForSingleObject(INFINITE) / ReleaseMutex) and takes them
 * thousands of times while loading and unloading sound banks on every node
 * change. Each operation is a kernel transition on Windows and a wineserver
 * round trip under Wine.
 *
 * We patch that DLL's import table so unnamed mutexes become recursive
 * CRITICAL_SECTIONs with the same ownership semantics (initial owner, owner-only
 * release, recursion). Waits on any other handle (threads, events) and named
 * mutexes are forwarded unchanged.
 */
#include <windows.h>
#include "memfix.h"
#include "lockfix.h"

#define MUTEX_MAGIC 0x584d544c  /* 'LTMX' */

typedef struct FakeMutex {
    DWORD magic;
    struct FakeMutex *next;
    CRITICAL_SECTION cs;
    DWORD owner;        /* thread id, 0 when free */
    LONG recursion;
} FakeMutex;

static FakeMutex *all;
static CRITICAL_SECTION list_lock;
static volatile LONG ops;

static HANDLE (WINAPI *real_CreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
static DWORD (WINAPI *real_WaitForSingleObject)(HANDLE, DWORD);
static BOOL (WINAPI *real_ReleaseMutex)(HANDLE);
static BOOL (WINAPI *real_CloseHandle)(HANDLE);

static FakeMutex *lookup(HANDLE h)
{
    FakeMutex *r = NULL;
    EnterCriticalSection(&list_lock);
    for (FakeMutex *m = all; m; m = m->next)
        if ((HANDLE)m == h) { r = m; break; }
    LeaveCriticalSection(&list_lock);
    return r;
}

static void acquire(FakeMutex *m)
{
    EnterCriticalSection(&m->cs);
    m->owner = GetCurrentThreadId();
    m->recursion++;
}

static HANDLE WINAPI my_CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL initial_owner, LPCSTR name)
{
    if (name) return real_CreateMutexA(sa, initial_owner, name);
    FakeMutex *m = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *m);
    if (!m) return real_CreateMutexA(sa, initial_owner, name);
    m->magic = MUTEX_MAGIC;
    InitializeCriticalSection(&m->cs);
    if (initial_owner) acquire(m);
    EnterCriticalSection(&list_lock);
    m->next = all; all = m;
    LeaveCriticalSection(&list_lock);
    SetLastError(0);
    return (HANDLE)m;
}

static DWORD WINAPI my_WaitForSingleObject(HANDLE h, DWORD ms)
{
    FakeMutex *m = lookup(h);
    if (!m) return real_WaitForSingleObject(h, ms);
    InterlockedIncrement(&ops);
    if (ms == INFINITE) { acquire(m); return WAIT_OBJECT_0; }
    DWORD start = GetTickCount();
    for (;;) {
        if (TryEnterCriticalSection(&m->cs)) {
            m->owner = GetCurrentThreadId(); m->recursion++;
            return WAIT_OBJECT_0;
        }
        if (GetTickCount() - start >= ms) return WAIT_TIMEOUT;
        Sleep(1);
    }
}

static BOOL WINAPI my_ReleaseMutex(HANDLE h)
{
    FakeMutex *m = lookup(h);
    if (!m) return real_ReleaseMutex(h);
    InterlockedIncrement(&ops);
    if (m->owner != GetCurrentThreadId() || m->recursion <= 0) { SetLastError(ERROR_NOT_OWNER); return FALSE; }
    if (--m->recursion == 0) m->owner = 0;
    LeaveCriticalSection(&m->cs);
    return TRUE;
}

static BOOL WINAPI my_CloseHandle(HANDLE h)
{
    FakeMutex *m = lookup(h);
    if (!m) return real_CloseHandle(h);
    EnterCriticalSection(&list_lock);
    for (FakeMutex **pp = &all; *pp; pp = &(*pp)->next)
        if (*pp == m) { *pp = m->next; break; }
    LeaveCriticalSection(&list_lock);
    /* An abandoned (still owned) mutex is simply leaked, as deleting an owned CS is undefined. */
    if (m->recursion == 0) { DeleteCriticalSection(&m->cs); HeapFree(GetProcessHeap(), 0, m); }
    return TRUE;
}

int lockfix_install(void)
{
    HMODULE snd = GetModuleHandleA("DareDSound3D_bin_rd.dll");
    if (!snd) return 0;
    InitializeCriticalSection(&list_lock);
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    real_CreateMutexA = (void *)GetProcAddress(k32, "CreateMutexA");
    real_WaitForSingleObject = (void *)GetProcAddress(k32, "WaitForSingleObject");
    real_ReleaseMutex = (void *)GetProcAddress(k32, "ReleaseMutex");
    real_CloseHandle = (void *)GetProcAddress(k32, "CloseHandle");
    int n = 0;
    n += !!memfix_patch_iat(snd, "kernel32.dll", "CreateMutexA", my_CreateMutexA);
    n += !!memfix_patch_iat(snd, "kernel32.dll", "WaitForSingleObject", my_WaitForSingleObject);
    n += !!memfix_patch_iat(snd, "kernel32.dll", "ReleaseMutex", my_ReleaseMutex);
    n += !!memfix_patch_iat(snd, "kernel32.dll", "CloseHandle", my_CloseHandle);
    return n;
}

long lockfix_ops(void) { return ops; }
