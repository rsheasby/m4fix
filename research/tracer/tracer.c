/*
 * m4trace.dll - timing tracer for Myst IV: Revelation.
 *
 * Detours engine functions listed in hooks.h with generic enter/exit stubs
 * (calling-convention agnostic: the return address is swapped for an exit
 * thunk and kept on a per-thread shadow stack) and logs timings to
 * "C:\Myst 4\trace.log".
 *
 * Exit-path code must not touch x87/SSE state: hooked functions may return
 * floats in ST0. Everything here is integer-only.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>
#include "../../fix/memfix.h"
#include "../../fix/lockfix.h"
#include "../../fix/navfix.h"
extern volatile int capture_active;
int capture_install(void);
static LONG64 capture_until;

typedef struct {
    const char *dll; DWORD rva; int steal; int rel; int verbose; int argfmt; const char *name;
    BYTE *target, *tramp;
    volatile LONG64 count, total, bytes, maxt;
} Hook;

#define HOOK(dll, rva, steal, rel, v, a, name) { dll, rva, steal, rel, v, a, name },
static Hook hooks[] = {
#include "hooks.h"
};
#define NHOOKS ((int)(sizeof hooks / sizeof hooks[0]))

static HANDLE logf;
static CRITICAL_SECTION cs;
static LONG64 qpf, t0;
static DWORD tls;
static DWORD main_tid;

typedef struct { int idx; DWORD ret; LONG64 t; } Frame;
typedef struct { int depth; Frame f[256]; } Shadow;

static LONG64 now(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart; }
/* QPC ticks -> microseconds, integer only */
static LONG64 us(LONG64 ticks) { return ticks * 1000000 / qpf; }

static void logw(const char *fmt, ...)
{
    char buf[1024]; va_list ap; DWORD w;
    va_start(ap, fmt); int n = vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 1) n = sizeof buf - 1;
    WriteFile(logf, buf, n, &w, NULL);
}

static Shadow *shadow(void)
{
    Shadow *s = TlsGetValue(tls);
    if (!s) { s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *s); TlsSetValue(tls, s); }
    return s;
}

long long capture_now_ms(void) { return us(now() - t0) / 1000; }
static void fmt_ms(char *out, LONG64 usec) { sprintf(out, "%lld.%03lld", usec / 1000, usec % 1000); }

static void dump_stats(const char *why)
{
    char a[32], b[32], c[32];
    logw("    ---- stats (%s) ----\n", why);
    for (int i = 0; i < NHOOKS; i++) {
        Hook *h = &hooks[i];
        if (h->verbose || !h->count) continue;
        fmt_ms(a, us(h->total)); fmt_ms(b, us(h->total) / h->count); fmt_ms(c, us(h->maxt));
        logw("    %-34s n=%-6lld total=%9sms avg=%7sms max=%8sms", h->name, h->count, a, b, c);
        if (h->bytes) logw(" bytes=%lld", h->bytes);
        logw("\n");
        h->count = h->total = h->bytes = h->maxt = 0;
    }
}

static void describe_args(Hook *h, DWORD *sp, char *out, size_t n)
{
    out[0] = 0;
    void *p = (void *)sp[1];
    if (h->argfmt != 4 && (!p || IsBadReadPtr(p, 4))) return;
    switch (h->argfmt) {
    case 1: snprintf(out, n, " \"%.200s\"", (const char *)p); break;
    case 2: { BYTE *ci = p; snprintf(out, n, " ctx=%02x %02x %02x %02x", ci[0], ci[1], ci[2], ci[3]); break; }
    }
    if (h->argfmt == 4) snprintf(out, n, " (%lu,%lu,%lu)", sp[1], sp[2], sp[3]);
}

extern void exit_thunk(void);

static int ridx;
/* ---- sampling profiler: samples the main thread while a frame has run > 30ms ---- */
#define NSTK 48
typedef struct { DWORD eip; DWORD stk[NSTK]; } Sample;
static Sample *samples; static volatile LONG nsamples; static const LONG maxsamples = 400000;
static HANDLE main_thread;
static volatile LONG64 frame_start;    /* QPC at Engine::DoFrame entry, 0 when idle */
typedef struct { DWORD lo, hi; char name[64]; } Mod;
static Mod mods[256]; static int nmods;

static void refresh_mods(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    MODULEENTRY32 me = { sizeof me }; int n = 0;
    if (snap == INVALID_HANDLE_VALUE) return;
    for (BOOL ok = Module32First(snap, &me); ok && n < 256; ok = Module32Next(snap, &me), n++) {
        mods[n].lo = (DWORD)me.modBaseAddr; mods[n].hi = mods[n].lo + me.modBaseSize;
        lstrcpynA(mods[n].name, me.szModule, 64);
    }
    nmods = n; CloseHandle(snap);
}
static int in_mod(DWORD a) { for (int i = 0; i < nmods; i++) if (a >= mods[i].lo && a < mods[i].hi) return 1; return 0; }

static DWORD WINAPI sampler(LPVOID unused)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    LONG64 last_refresh = 0;
    for (;;) {
        Sleep(1);
        LONG64 fs = frame_start, t = now();
        if (!fs || ridx < 1 || t - fs < qpf * 30 / 1000 || nsamples >= maxsamples) continue;
        if (t - last_refresh > qpf * 2) { refresh_mods(); last_refresh = t; }
        if (SuspendThread(main_thread) == (DWORD)-1) continue;
        CONTEXT c; c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(main_thread, &c)) {
            Sample *sm = &samples[nsamples];
            memset(sm, 0, sizeof *sm);
            sm->eip = c.Eip;
            DWORD *sp = (DWORD *)c.Esp; int k = 0;
            for (int i = 0; i < 4096 && k < NSTK; i++) {
                if (IsBadReadPtr(sp + i, 4)) break;
                if (in_mod(sp[i])) sm->stk[k++] = sp[i];
            }
            nsamples++;
        }
        ResumeThread(main_thread);
    }
    return 0;
}

static void dump_samples(void)
{
    refresh_mods();
    HANDLE f = CreateFileA("C:\\Myst 4\\samples.bin", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w, n = nsamples, nm = nmods;
    WriteFile(f, &nm, 4, &w, NULL); WriteFile(f, mods, sizeof(Mod) * nm, &w, NULL);
    WriteFile(f, &n, 4, &w, NULL); WriteFile(f, samples, sizeof(Sample) * n, &w, NULL);
    CloseHandle(f);
    logw("profiler: wrote %lu samples\n", n);
}

/* ---- bench mode: drive navigation from the main thread at the start of each Engine::DoFrame ---- */
static int frame_idx = -1, activate_idx = -1;
static int route[256][3], nroute;
static LONG64 bench_next;              /* QPC time of next allowed hop */
static int bench_armed;
static int capture_enabled;                /* set once the first node context has activated */
static void (__cdecl *ai_ContextChange)(DWORD, DWORD, DWORD);
static BOOL (__cdecl *ai_IsChanging)(void);

static void bench_load(void)
{
    FILE *f = fopen("C:\\Myst 4\\bench.txt", "r");
    if (!f) return;
    while (nroute < 256 && fscanf(f, "%d %d %d", &route[nroute][0], &route[nroute][1], &route[nroute][2]) == 3) nroute++;
    fclose(f);
    HMODULE ai = GetModuleHandleA("m4_aisdk_rd.dll");
    ai_ContextChange = (void *)GetProcAddress(ai, "?ContextChange@ai@@YAXKKK@Z");
    ai_IsChanging = (void *)GetProcAddress(ai, "?ContextIsChangingContext@ai@@YA_NXZ");
    logw("bench: %d hops loaded\n", nroute);
}

static void bench_tick(void)
{
    if (!nroute || ridx > nroute || !bench_armed) return;
    LONG64 t = now();
    if (capture_active && t > capture_until) capture_active = 0;
    if (t < bench_next || ai_IsChanging()) return;
    if (ridx == nroute) {
        long live, peak, cnt; memfix_stats(&live, &peak, &cnt);
        logw("memfix stats: live=%ld peak=%ld sections=%ld\n", live, peak, cnt);
        logw("lockfix: %ld mutex ops emulated\n", lockfix_ops());
        dump_samples(); logw("BENCH DONE\n"); ridx++; return;
    }
    if (ridx == 0) logw("navfix state at bench start: %d\n", navfix_state());
    int *r = route[ridx++];
    char ts[32]; fmt_ms(ts, us(t - t0));
    logw("%10s BENCH hop %d -> w%d z%d n%d\n", ts, ridx, r[0], r[1], r[2]);
    {
        static BYTE *(__cdecl *snd_mgr)(void);
        if (!snd_mgr) snd_mgr = (void *)GetProcAddress(GetModuleHandleA("m4_snd_rd.dll"), "?GetSingletonInstance@Manager@snd@@SAAAV12@XZ");
        BYTE *m = snd_mgr ? snd_mgr() : NULL;
        if (m) { DWORD bits = *(DWORD *)(m + 0x220 + 0x18); logw("           snd transition time bits=%08lx\n", bits); }
    }
    bench_next = t + 4 * qpf;
    if (capture_enabled && (ridx == 4 || ridx == 5 || ridx == 8)) { capture_active = 1; capture_until = t + qpf * 5 / 2; }
    ai_ContextChange(r[0], r[1], r[2]);
}

/* Called from enter stub: sp points at the return address slot. Returns trampoline. */
__attribute__((used, cdecl)) static BYTE *on_enter(int idx, DWORD *sp)
{
    Hook *h = &hooks[idx];
    if (idx == frame_idx) { bench_tick(); frame_start = now(); }
    Shadow *s = shadow();
    if (s->depth < 256) {
        Frame *f = &s->f[s->depth++];
        f->idx = idx; f->ret = sp[0]; f->t = now();
        sp[0] = (DWORD)exit_thunk;
        if (h->argfmt == 3) InterlockedAdd64(&h->bytes, sp[2]);
        if (h->verbose) {
            char a[256], t[32];
            describe_args(h, sp, a, sizeof a);
            fmt_ms(t, us(f->t - t0));
            EnterCriticalSection(&cs);
            logw("%10s [%c] %*s> %s%s\n", t, GetCurrentThreadId() == main_tid ? 'M' : 'w', (s->depth - 1) * 2, "", h->name, a);
            LeaveCriticalSection(&cs);
        }
    }
    return h->tramp;
}

/* Called from exit thunk. Returns original return address. */
__attribute__((used, cdecl)) static DWORD on_exit(void)
{
    Shadow *s = shadow();
    Frame *f = &s->f[--s->depth];
    Hook *h = &hooks[f->idx];
    LONG64 dt = now() - f->t;
    EnterCriticalSection(&cs);
    h->count++; h->total += dt; if (dt > h->maxt) h->maxt = dt;
    if (f->idx == frame_idx) frame_start = 0;
    if (f->idx == frame_idx && dt * 1000 / qpf >= 20) {
        char t[32], d[32]; fmt_ms(t, us(now() - t0)); fmt_ms(d, us(dt));
        logw("%10s     frame %sms\n", t, d);
    }
    if (h->verbose) {
        char t[32], d[32];
        fmt_ms(t, us(now() - t0)); fmt_ms(d, us(dt));
        logw("%10s [%c] %*s< %s  %sms\n", t, GetCurrentThreadId() == main_tid ? 'M' : 'w', s->depth * 2, "", h->name, d);
        if (s->depth == 0) dump_stats(h->name);
        if (f->idx == activate_idx && !bench_armed) { bench_armed = 1; bench_next = now() + 5 * qpf; }
    }
    LeaveCriticalSection(&cs);
    return f->ret;
}

/* Stack on entry to common_enter: [esp]=idx, [esp+4]=caller return address, args... */
__attribute__((naked)) static void common_enter(void)
{
    __asm__ volatile(
        "pushfd\n\t"
        "pushad\n\t"
        "lea ecx, [esp+40]\n\t"
        "push ecx\n\t"
        "push dword ptr [esp+40]\n\t"
        "call _on_enter\n\t"
        "add esp, 8\n\t"
        "mov [esp+36], eax\n\t"
        "popad\n\t"
        "popfd\n\t"
        "ret\n\t");
}

__attribute__((naked)) void exit_thunk(void)
{
    __asm__ volatile(
        "push 0\n\t"
        "pushfd\n\t"
        "pushad\n\t"
        "call _on_exit\n\t"
        "mov [esp+36], eax\n\t"
        "popad\n\t"
        "popfd\n\t"
        "ret\n\t");
}

static int install(Hook *h, int idx)
{
    HMODULE m = GetModuleHandleA(h->dll);
    if (!m) m = LoadLibraryA(h->dll);
    if (!m) { logw("!! %s not loaded for %s\n", h->dll, h->name); return 0; }
    h->target = (BYTE *)m + h->rva;
    /* tramp: stolen bytes + jmp back;  stub: push idx; jmp common_enter */
    BYTE *mem = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    h->tramp = mem;
    memcpy(mem, h->target, h->steal);
    if (h->rel) {
        LONG d = *(LONG *)(mem + h->rel);
        *(LONG *)(mem + h->rel) = d + (LONG)(h->target - mem);
    }
    mem[h->steal] = 0xe9;
    *(LONG *)(mem + h->steal + 1) = (LONG)((h->target + h->steal) - (mem + h->steal + 5));
    BYTE *stub = mem + 32;
    stub[0] = 0x68; *(DWORD *)(stub + 1) = idx;
    stub[5] = 0xe9; *(LONG *)(stub + 6) = (LONG)((BYTE *)common_enter - (stub + 10));
    DWORD old;
    VirtualProtect(h->target, 5, PAGE_EXECUTE_READWRITE, &old);
    h->target[0] = 0xe9;
    *(LONG *)(h->target + 1) = (LONG)(stub - (h->target + 5));
    VirtualProtect(h->target, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), h->target, 5);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpf = f.QuadPart; t0 = now();
    InitializeCriticalSection(&cs);
    tls = TlsAlloc();
    main_tid = GetCurrentThreadId();
    logf = CreateFileA("C:\\Myst 4\\trace.log", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    int ok = 0;
    for (int i = 0; i < NHOOKS; i++) {
        ok += install(&hooks[i], i);
        if (!strcmp(hooks[i].name, "Engine::DoFrame")) frame_idx = i;
        if (!strcmp(hooks[i].name, "LoaderManager::ActivateContext")) activate_idx = i;
    }
    bench_load();
    if (GetFileAttributesA("C:\\Myst 4\\memfix.on") != INVALID_FILE_ATTRIBUTES)
        logw("memfix: patched %d imports\n", memfix_install());
    if (GetFileAttributesA("C:\\Myst 4\\capture.on") != INVALID_FILE_ATTRIBUTES)
        logw("capture: hook %d\n", capture_enabled = capture_install());
    if (GetFileAttributesA("C:\\Myst 4\\navfix.on") != INVALID_FILE_ATTRIBUTES)
        logw("navfix: install %d\n", navfix_install(100));
    if (GetFileAttributesA("C:\\Myst 4\\lockfix.on") != INVALID_FILE_ATTRIBUTES)
        logw("lockfix: patched %d imports\n", lockfix_install());
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    samples = VirtualAlloc(NULL, sizeof(Sample) * maxsamples, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (nroute) CreateThread(NULL, 0, sampler, NULL, 0, NULL);
    logw("m4trace: installed %d/%d hooks\n", ok, NHOOKS);
    return TRUE;
}
