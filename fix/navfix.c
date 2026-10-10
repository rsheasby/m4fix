/*
 * navfix.c - shorten the fixed pre-transition delay on every navigation click.
 *
 * AiSndTransition::FadeOutSound (m4_ai_global_rd.dll) disables input, starts
 * the ambient sound fade and then waits on a hard-coded 550 ms timer before the
 * node change even begins:
 *
 *     push 0x226            ; 550 ms
 *     call ai::Timer::SetTick
 *
 * The sound fade itself is 2 s and keeps running across the change, so the
 * timer only adds dead time with a frozen picture. The AI library is loaded at
 * runtime by m4_thor_rd.dll, so we hook that DLL's LoadLibraryA and patch the
 * immediate once m4_ai_global_rd.dll is mapped. The patch is only applied if the
 * original bytes match exactly.
 */
#include <windows.h>
#include "memfix.h"
#include "navfix.h"

#define SITE_RVA 0x1077e
static const BYTE expect[5] = { 0x68, 0x26, 0x02, 0x00, 0x00 };   /* push 0x226 */

static DWORD delay_ms = 550;
static int patched;
static HMODULE (WINAPI *real_LoadLibraryA)(LPCSTR);

static void try_patch(HMODULE m)
{
    if (!m || patched) return;
    BYTE *site = (BYTE *)m + SITE_RVA;
    for (int i = 0; i < (int)sizeof expect; i++)
        if (site[i] != expect[i]) { patched = -1; return; }
    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD *)(site + 1) = delay_ms;
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    patched = 1;
}

static HMODULE WINAPI my_LoadLibraryA(LPCSTR name)
{
    HMODULE m = real_LoadLibraryA(name);
    if (m && !patched) try_patch(GetModuleHandleA("m4_ai_global_rd.dll"));
    return m;
}

int navfix_install(DWORD ms)
{
    delay_ms = ms;
    HMODULE ai = GetModuleHandleA("m4_ai_global_rd.dll");
    if (ai) { try_patch(ai); return patched; }
    HMODULE thor = GetModuleHandleA("m4_thor_rd.dll");
    if (!thor) return 0;
    real_LoadLibraryA = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    return memfix_patch_iat(thor, "kernel32.dll", "LoadLibraryA", my_LoadLibraryA) ? 1 : 0;
}

int navfix_state(void) { return patched; }
