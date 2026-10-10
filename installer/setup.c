/*
 * m4fix-setup.exe - installer/uninstaller for m4fix on Windows (and Wine).
 *
 * Double-click: finds the game (GOG, Steam, Ubisoft registry, or the folder the
 * setup is in), asks, and installs/updates/uninstalls.
 *
 * Command line:
 *   m4fix-setup.exe [/install | /uninstall | /status] [/path "<game or bin folder>"] [/quiet]
 * Exit codes: 0 ok, 1 error, 2 game not found, 3 cancelled, 4 game is running.
 *
 * Install: the game's CrashRpt.dll is kept as CrashRpt_orig.dll and replaced by
 * the m4fix proxy; m4fix.ini is created if missing. Uninstall reverses it.
 * Built without the C runtime so it runs on every Windows version the game does.
 */
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include "../fix/version.h"

#define RES_DLL 101
#define RES_INI 102
#define TITLE "m4fix " M4FIX_VERSION_STR " setup"

enum { EXIT_OK, EXIT_ERR, EXIT_NOTFOUND, EXIT_CANCEL, EXIT_RUNNING };
enum { ACT_ASK, ACT_INSTALL, ACT_UNINSTALL, ACT_STATUS };

static int quiet, elevated;
static char msgbuf[2048];

/* ---- no-CRT support: the compiler emits memset for zero-initialised structs ---- */
void *memset(void *d, int c, unsigned n)
{
    volatile unsigned char *p = d;   /* volatile: keep GCC from turning this back into memset */
    while (n--) *p++ = (unsigned char)c;
    return d;
}

/* ---- tiny string helpers ---- */
static int ieq(const char *a, const char *b) { return lstrcmpiA(a, b) == 0; }
static void cat(char *d, const char *s) { lstrcatA(d, s); }
static void path_join(char *out, const char *dir, const char *name)
{
    lstrcpyA(out, dir);
    int n = lstrlenA(out);
    if (n && out[n - 1] != '\\' && out[n - 1] != '/') cat(out, "\\");
    cat(out, name);
}
static int exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

static void say(UINT icon, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); wvsprintfA(msgbuf, fmt, ap); va_end(ap);
    if (!quiet) MessageBoxA(NULL, msgbuf, TITLE, icon | MB_OK);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) {
        DWORD w; WriteFile(out, msgbuf, lstrlenA(msgbuf), &w, NULL); WriteFile(out, "\r\n", 2, &w, NULL);
    }
}

/* ---- file classification ---- */
static int file_contains(const char *path, const char *needle)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD size = GetFileSize(f, NULL), got = 0;
    int found = 0, nl = lstrlenA(needle);
    BYTE *buf = size && size < (64u << 20) ? HeapAlloc(GetProcessHeap(), 0, size) : NULL;
    if (buf && ReadFile(f, buf, size, &got, NULL))
        for (DWORD i = 0; !found && i + nl <= got; i++) {
            int j = 0;
            while (j < nl && buf[i + j] == (BYTE)needle[j]) j++;
            found = j == nl;
        }
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
    CloseHandle(f);
    return found;
}
static int is_m4fix(const char *p) { return file_contains(p, M4FIX_MARKER); }

/* A game bin folder holds Myst4.exe and a CrashRpt (original or ours). */
static int is_bin(const char *dir)
{
    char p[MAX_PATH];
    path_join(p, dir, "Myst4.exe");
    if (!exists(p)) return 0;
    path_join(p, dir, "CrashRpt.dll");
    if (exists(p)) return 1;
    path_join(p, dir, "CrashRpt_orig.dll");
    return exists(p);
}

/* Accept the bin folder itself or the game root containing bin\. */
static int resolve_bin(const char *dir, char *out)
{
    char p[MAX_PATH];
    if (!dir || !*dir) return 0;
    if (is_bin(dir)) { lstrcpyA(out, dir); return 1; }
    path_join(p, dir, "bin");
    if (is_bin(p)) { lstrcpyA(out, p); return 1; }
    return 0;
}

/* ---- game discovery ---- */
static int reg_str(HKEY root, const char *key, const char *val, REGSAM view, char *out, DWORD n)
{
    HKEY k;
    if (RegOpenKeyExA(root, key, 0, KEY_READ | view, &k)) return 0;
    DWORD type, len = n;
    LONG r = RegQueryValueExA(k, val, NULL, &type, (BYTE *)out, &len);
    RegCloseKey(k);
    return !r && (type == REG_SZ || type == REG_EXPAND_SZ) && len > 1;
}

static int try_reg(HKEY root, const char *key, const char *val, char *out)
{
    char v[MAX_PATH];
    REGSAM views[2] = { KEY_WOW64_32KEY, KEY_WOW64_64KEY };
    for (int i = 0; i < 2; i++)
        if (reg_str(root, key, val, views[i], v, sizeof v) && resolve_bin(v, out)) return 1;
    return 0;
}

/* Steam: SteamPath + every library in steamapps\libraryfolders.vdf. */
static int try_steam(char *out)
{
    char steam[MAX_PATH], vdf[MAX_PATH], lib[MAX_PATH], game[MAX_PATH];
    if (!reg_str(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", 0, steam, sizeof steam)) return 0;
    for (char *c = steam; *c; c++) if (*c == '/') *c = '\\';
    path_join(game, steam, "steamapps\\common\\Myst IV Revelation");
    if (resolve_bin(game, out)) return 1;
    path_join(vdf, steam, "steamapps\\libraryfolders.vdf");
    HANDLE f = CreateFileA(vdf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    static char text[65536]; DWORD got = 0;
    ReadFile(f, text, sizeof text - 1, &got, NULL); CloseHandle(f);
    text[got] = 0;
    /* lines look like:   "path"   "D:\\SteamLibrary"   */
    for (char *s = text; *s; s++) {
        if (CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, s, 6, "\"path\"", 6) != CSTR_EQUAL) continue;
        char *q = s + 6;
        while (*q && *q != '"') q++;
        if (!*q) break;
        q++;
        int n = 0;
        while (*q && *q != '"' && n < MAX_PATH - 1) {
            if (q[0] == '\\' && q[1] == '\\') q++;   /* vdf escapes backslashes */
            lib[n++] = *q++;
        }
        lib[n] = 0;
        path_join(game, lib, "steamapps\\common\\Myst IV Revelation");
        if (resolve_bin(game, out)) return 1;
        s = q;
        if (!*s) break;
    }
    return 0;
}

static int find_game(char *out)
{
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    char *slash = self; for (char *c = self; *c; c++) if (*c == '\\') slash = c;
    *slash = 0;
    if (resolve_bin(self, out)) return 1;
    if (try_reg(HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games\\1956555724", "path", out)) return 1;
    if (try_reg(HKEY_LOCAL_MACHINE, "SOFTWARE\\UBISOFT\\Myst IV - Revelation\\1", "System_Path", out)) return 1;
    if (try_reg(HKEY_LOCAL_MACHINE, "SOFTWARE\\UBISOFT\\Myst IV - Revelation\\1", "Product_Path", out)) return 1;
    if (try_steam(out)) return 1;
    static const char *defaults[] = {
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Myst IV Revelation",
        "C:\\Program Files\\Steam\\steamapps\\common\\Myst IV Revelation",
        "C:\\GOG Games\\Myst 4", "C:\\Program Files (x86)\\GOG Galaxy\\Games\\Myst 4", "C:\\Myst 4",
    };
    for (int i = 0; i < (int)(sizeof defaults / sizeof *defaults); i++)
        if (resolve_bin(defaults[i], out)) return 1;
    return 0;
}

static int browse(char *out)
{
    char disp[MAX_PATH];
    BROWSEINFOA bi = { 0 };
    bi.lpszTitle = "Select the Myst IV: Revelation folder (the one containing Myst4.exe, or its parent):";
    bi.pszDisplayName = disp;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    for (;;) {
        PIDLIST_ABSOLUTE id = SHBrowseForFolderA(&bi);
        if (!id) return 0;
        char dir[MAX_PATH];
        BOOL ok = SHGetPathFromIDListA(id, dir);
        CoTaskMemFree(id);
        if (ok && resolve_bin(dir, out)) return 1;
        if (MessageBoxA(NULL, "Myst4.exe was not found in that folder.\nTry again?", TITLE, MB_RETRYCANCEL | MB_ICONWARNING) != IDRETRY)
            return 0;
    }
}

static int game_running(void)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof pe };
    int found = 0;
    if (s == INVALID_HANDLE_VALUE) return 0;
    for (BOOL ok = Process32First(s, &pe); ok && !found; ok = Process32Next(s, &pe))
        found = ieq(pe.szExeFile, "Myst4.exe");
    CloseHandle(s);
    return found;
}

/* ---- install / uninstall ---- */
static DWORD write_resource(int id, const char *dest)
{
    HRSRC r = FindResourceA(NULL, MAKEINTRESOURCEA(id), (LPCSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    if (!g) return ERROR_RESOURCE_DATA_NOT_FOUND;
    char tmp[MAX_PATH]; lstrcpyA(tmp, dest); cat(tmp, ".m4fix-new");
    HANDLE f = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD size = SizeofResource(NULL, r), w = 0;
    BOOL ok = WriteFile(f, LockResource(g), size, &w, NULL) && w == size;
    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(f);
    if (!ok) { DeleteFileA(tmp); return err; }
    if (!MoveFileExA(tmp, dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { err = GetLastError(); DeleteFileA(tmp); return err; }
    return 0;
}

static DWORD do_install(const char *bin, int *was_update)
{
    char dll[MAX_PATH], orig[MAX_PATH], ini[MAX_PATH];
    path_join(dll, bin, "CrashRpt.dll");
    path_join(orig, bin, "CrashRpt_orig.dll");
    path_join(ini, bin, "m4fix.ini");
    *was_update = exists(dll) && is_m4fix(dll);
    /* Keep the genuine CrashRpt.dll. If a game update/verify restored it, refresh the backup. */
    if (exists(dll) && !*was_update && !CopyFileA(dll, orig, FALSE)) return GetLastError();
    DWORD e = write_resource(RES_DLL, dll);
    if (e) return e;
    if (!exists(ini) && (e = write_resource(RES_INI, ini))) return e;
    return 0;
}

static DWORD do_uninstall(const char *bin)
{
    char dll[MAX_PATH], orig[MAX_PATH], p[MAX_PATH];
    path_join(dll, bin, "CrashRpt.dll");
    path_join(orig, bin, "CrashRpt_orig.dll");
    if (exists(dll) && is_m4fix(dll)) {
        if (!exists(orig)) return ERROR_FILE_NOT_FOUND;
        if (!MoveFileExA(orig, dll, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return GetLastError();
    } else if (exists(orig) && !is_m4fix(orig)) {
        DeleteFileA(orig);   /* game already has its own CrashRpt.dll back; drop the stale backup */
    }
    path_join(p, bin, "m4fix.ini"); DeleteFileA(p);
    path_join(p, bin, "m4fix.log"); DeleteFileA(p);
    return 0;
}

/* Re-run ourselves elevated with an explicit action and path; returns the child's exit code. */
static int relaunch_elevated(int act, const char *bin)
{
    char self[MAX_PATH], args[MAX_PATH + 64];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    lstrcpyA(args, act == ACT_INSTALL ? "/install" : "/uninstall");
    cat(args, quiet ? " /quiet /elevated /path \"" : " /elevated /path \"");
    cat(args, bin); cat(args, "\"");
    SHELLEXECUTEINFOA sei = { sizeof sei };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "runas"; sei.lpFile = self; sei.lpParameters = args; sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei) || !sei.hProcess) return EXIT_CANCEL;
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = EXIT_ERR; GetExitCodeProcess(sei.hProcess, &code); CloseHandle(sei.hProcess);
    return (int)code;
}

static int run(int act, const char *bin)
{
    while (game_running()) {
        if (quiet || MessageBoxA(NULL, "Myst IV is running. Please quit the game, then press Retry.", TITLE, MB_RETRYCANCEL | MB_ICONWARNING) != IDRETRY) {
            if (quiet) say(MB_ICONERROR, "Myst IV is running; quit it first.");
            return EXIT_RUNNING;
        }
    }
    int update = 0;
    DWORD e = act == ACT_INSTALL ? do_install(bin, &update) : do_uninstall(bin);
    if (e == ERROR_ACCESS_DENIED && !elevated) return relaunch_elevated(act, bin);
    if (e) {
        char err[256] = "";
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, err, sizeof err, NULL);
        if (act == ACT_UNINSTALL && e == ERROR_FILE_NOT_FOUND)
            say(MB_ICONERROR, "Can't uninstall: the original CrashRpt_orig.dll is missing from\n%s\n\nUse Steam/GOG \"verify files\" to restore it.", bin);
        else
            say(MB_ICONERROR, "%s failed in\n%s\n\n%s", act == ACT_INSTALL ? "Install" : "Uninstall", bin, err);
        return EXIT_ERR;
    }
    if (act == ACT_INSTALL)
        say(MB_ICONINFORMATION, "m4fix %s %s in\n%s\n\nSettings: m4fix.ini in that folder.", M4FIX_VERSION_STR, update ? "updated" : "installed", bin);
    else
        say(MB_ICONINFORMATION, "m4fix removed; the original CrashRpt.dll is restored in\n%s", bin);
    return EXIT_OK;
}

/* ---- command line ---- */
static char *next_arg(char **s, char *buf)
{
    char *p = *s, *o = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return NULL;
    int q = 0;
    while (*p && (q || (*p != ' ' && *p != '\t'))) {
        if (*p == '"') q = !q; else *o++ = *p;
        p++;
    }
    *o = 0; *s = p;
    return buf;
}

static int app(void)
{
    char *cl = GetCommandLineA(), a[MAX_PATH], given[MAX_PATH] = "", bin[MAX_PATH] = "";
    int act = ACT_ASK;
    next_arg(&cl, a);   /* program name */
    while (next_arg(&cl, a)) {
        if (ieq(a, "/install") || ieq(a, "-install")) act = ACT_INSTALL;
        else if (ieq(a, "/uninstall") || ieq(a, "-uninstall")) act = ACT_UNINSTALL;
        else if (ieq(a, "/status") || ieq(a, "-status")) act = ACT_STATUS;
        else if (ieq(a, "/quiet") || ieq(a, "/q") || ieq(a, "/s") || ieq(a, "-quiet")) quiet = 1;
        else if (ieq(a, "/elevated")) elevated = 1;
        else if ((ieq(a, "/path") || ieq(a, "-path")) && next_arg(&cl, a)) lstrcpyA(given, a);
    }
    if (*given) {
        if (!resolve_bin(given, bin)) { say(MB_ICONERROR, "Myst4.exe not found in\n%s", given); return EXIT_NOTFOUND; }
    } else if (!find_game(bin) && (quiet || act == ACT_STATUS || !browse(bin))) {
        say(MB_ICONERROR, "Myst IV: Revelation was not found. Run with /path \"<game folder>\".");
        return EXIT_NOTFOUND;
    }

    char dll[MAX_PATH]; path_join(dll, bin, "CrashRpt.dll");
    int installed = exists(dll) && is_m4fix(dll);
    if (act == ACT_STATUS) {
        say(MB_ICONINFORMATION, "Game: %s\nm4fix: %s", bin, installed ? "installed" : "not installed");
        return installed ? EXIT_OK : EXIT_ERR;
    }
    if (act == ACT_ASK) {
        int r;
        if (installed) {
            wsprintfA(msgbuf, "m4fix is installed in:\n%s\n\nYes  -  reinstall / update to %s\nNo  -  uninstall (restore the original game files)\nCancel  -  do nothing", bin, M4FIX_VERSION_STR);
            r = MessageBoxA(NULL, msgbuf, TITLE, MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDCANCEL) return EXIT_CANCEL;
            act = r == IDYES ? ACT_INSTALL : ACT_UNINSTALL;
        } else {
            for (;;) {
                wsprintfA(msgbuf, "Install m4fix %s into:\n%s\n\nYes  -  install\nNo  -  choose a different folder\nCancel  -  do nothing", M4FIX_VERSION_STR, bin);
                r = MessageBoxA(NULL, msgbuf, TITLE, MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDCANCEL) return EXIT_CANCEL;
                if (r == IDYES) break;
                if (!browse(bin)) return EXIT_CANCEL;
            }
            act = ACT_INSTALL;
        }
    }
    return run(act, bin);
}

void __stdcall start(void)
{
    AttachConsole(ATTACH_PARENT_PROCESS);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ExitProcess(app());
}
