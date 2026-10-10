/*
 * m4fix - navigation performance fixes for Myst IV: Revelation.
 *
 * Installed as a stand-in for CrashRpt.dll (the original is renamed to
 * CrashRpt_orig.dll and every export is forwarded to it). Myst4.exe imports
 * CrashRpt.dll, so this DLL is loaded with the game without modifying any game
 * binary on disk. All patches are in-memory and verified against the expected
 * original code; anything unexpected is left alone.
 *
 * Fixes (each can be disabled in m4fix.ini next to Myst4.exe):
 *   LeadtoolsHeap      - LEADTOOLS bitmaps in cached heap memory instead of
 *                        uncached (SEC_NOCACHE) pagefile sections   (memfix.c)
 *   SoundLocks         - sound engine mutexes -> critical sections   (lockfix.c)
 *   TransitionDelayMs  - the fixed 550 ms freeze before every node
 *                        change (default here: 100)                 (navfix.c)
 */
#include <windows.h>
#include "memfix.h"
#include "lockfix.h"
#include "navfix.h"
#include "version.h"

__attribute__((used)) static const char marker[] = M4FIX_MARKER " " M4FIX_VERSION_STR;

static HMODULE orig;
static FARPROC orig_fn[4];
static const char *const orig_names[4] = { "CrashRpt_AddFile", "CrashRpt_GenerateErrorReport", "CrashRpt_Install", "CrashRpt_Uninstall" };
static char dir[MAX_PATH], ini[MAX_PATH], logpath[MAX_PATH], origpath[MAX_PATH];
static volatile LONG installed;
static int want_log;

static void m4log(const char *fmt, ...)
{
    if (!want_log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); int n = wvsprintfA(buf, fmt, ap); va_end(ap);
    HANDLE f = CreateFileA(logpath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(f, buf, n, &w, NULL); WriteFile(f, "\r\n", 2, &w, NULL);
    CloseHandle(f);
}

static void install(void)
{
    if (InterlockedExchange(&installed, 1)) return;
    want_log = GetPrivateProfileIntA("m4fix", "Log", 0, ini);
    if (want_log) DeleteFileA(logpath);
    m4log("m4fix " M4FIX_VERSION_STR " (%s)", ini);
    if (GetPrivateProfileIntA("m4fix", "LeadtoolsHeap", 1, ini))
        m4log("LeadtoolsHeap: %d/5 imports patched", memfix_install());
    if (GetPrivateProfileIntA("m4fix", "SoundLocks", 1, ini))
        m4log("SoundLocks: %d/4 imports patched", lockfix_install());
    UINT delay = GetPrivateProfileIntA("m4fix", "TransitionDelayMs", 100, ini);
    if (delay > 550) delay = 550;
    if (delay < 1) delay = 1;
    if (delay != 550)
        m4log("TransitionDelayMs=%u: hook %d", delay, navfix_install(delay));
}

static FARPROC fn(int i)
{
    install();   /* idempotent: also covers loaders that run DllMain before the game DLLs are mapped */
    if (!orig) orig = LoadLibraryA(origpath);
    if (!orig) return NULL;
    if (!orig_fn[i]) orig_fn[i] = GetProcAddress(orig, orig_names[i]);
    return orig_fn[i];
}

/* All CrashRpt entry points are cdecl; forward or degrade to a harmless no-op. */
__declspec(dllexport) void *__cdecl CrashRpt_Install(void *getlog, const char *to, const char *subject)
{
    void *(__cdecl *f)(void *, const char *, const char *) = (void *)fn(2);
    return f ? f(getlog, to, subject) : NULL;
}

__declspec(dllexport) void __cdecl CrashRpt_Uninstall(void *state)
{
    void (__cdecl *f)(void *) = (void *)fn(3);
    if (f && state) f(state);
}

__declspec(dllexport) void __cdecl CrashRpt_AddFile(void *state, const char *file, const char *desc)
{
    void (__cdecl *f)(void *, const char *, const char *) = (void *)fn(0);
    if (f && state) f(state, file, desc);
}

__declspec(dllexport) void __cdecl CrashRpt_GenerateErrorReport(void *state, void *exc)
{
    void (__cdecl *f)(void *, void *) = (void *)fn(1);
    if (f && state) f(state, exc);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    GetModuleFileNameA(inst, dir, MAX_PATH);
    char *slash = dir; for (char *p = dir; *p; p++) if (*p == '\\' || *p == '/') slash = p;
    slash[1] = 0;
    lstrcpyA(ini, dir); lstrcatA(ini, "m4fix.ini");
    lstrcpyA(logpath, dir); lstrcatA(logpath, "m4fix.log");
    lstrcpyA(origpath, dir); lstrcatA(origpath, "CrashRpt_orig.dll");
    if (GetModuleHandleA("ltkrn13n.dll")) install();
    return TRUE;
}
