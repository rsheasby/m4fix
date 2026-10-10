/* m4launch.exe - start Myst4.exe suspended and make its entry point load a DLL first.
 * usage: m4launch.exe [dll ...]   (default: m4trace.dll) */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define ENTRY 0x004020a6u   /* Myst4.exe AddressOfEntryPoint (fixed base 0x400000, no relocs) */
#define CAVE  0x0040b800u   /* inside .vsec (RWX, 0x1000 bytes, unused at runtime) */

int main(int argc, char **argv)
{
    STARTUPINFOA si = { sizeof si }; PROCESS_INFORMATION pi;
    char cmd[] = "Myst4.exe";
    if (!CreateProcessA("Myst4.exe", cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        printf("CreateProcess failed %lu\n", GetLastError()); return 1;
    }
    BYTE orig[5]; SIZE_T n;
    ReadProcessMemory(pi.hProcess, (void *)ENTRY, orig, 5, &n);
    DWORD loadlib = (DWORD)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    DWORD vprot = (DWORD)GetProcAddress(GetModuleHandleA("kernel32.dll"), "VirtualProtect");
    /* cave layout: [0x00] code, [0x200] dll names, [0x3f0] scratch dword, [0x3f8] orig bytes */
    BYTE code[0x400] = { 0 }; int p = 0, s = 0x200;
    for (int i = 0; i < (argc > 1 ? argc - 1 : 1); i++) {
        const char *dll = argc > 1 ? argv[i + 1] : "m4trace.dll";
        strcpy((char *)code + s, dll);
        code[p++] = 0x68; *(DWORD *)(code + p) = CAVE + s; p += 4;          /* push name */
        code[p++] = 0xb8; *(DWORD *)(code + p) = loadlib; p += 4;           /* mov eax, LoadLibraryA */
        code[p++] = 0xff; code[p++] = 0xd0;                                  /* call eax */
        s += strlen(dll) + 1;
    }
    /* restore entry bytes: VirtualProtect(ENTRY,5,RWX,&scratch); memcpy; then jmp ENTRY */
    code[p++] = 0x68; *(DWORD *)(code + p) = CAVE + 0x3f0; p += 4;
    code[p++] = 0x6a; code[p++] = 0x40;
    code[p++] = 0x6a; code[p++] = 0x05;
    code[p++] = 0x68; *(DWORD *)(code + p) = ENTRY; p += 4;
    code[p++] = 0xb8; *(DWORD *)(code + p) = vprot; p += 4;
    code[p++] = 0xff; code[p++] = 0xd0;
    memcpy(code + 0x3f8, orig, 5);
    code[p++] = 0xbe; *(DWORD *)(code + p) = CAVE + 0x3f8; p += 4;           /* mov esi, src */
    code[p++] = 0xbf; *(DWORD *)(code + p) = ENTRY; p += 4;                  /* mov edi, ENTRY */
    code[p++] = 0xb9; *(DWORD *)(code + p) = 5; p += 4;                      /* mov ecx, 5 */
    code[p++] = 0xf3; code[p++] = 0xa4;                                      /* rep movsb */
    code[p++] = 0xb8; *(DWORD *)(code + p) = ENTRY; p += 4;                  /* mov eax, ENTRY */
    code[p++] = 0xff; code[p++] = 0xe0;                                      /* jmp eax */
    DWORD old;
    VirtualProtectEx(pi.hProcess, (void *)CAVE, sizeof code, PAGE_EXECUTE_READWRITE, &old);
    WriteProcessMemory(pi.hProcess, (void *)CAVE, code, sizeof code, &n);
    BYTE jmp[5] = { 0xe9 }; *(LONG *)(jmp + 1) = (LONG)(CAVE - (ENTRY + 5));
    VirtualProtectEx(pi.hProcess, (void *)ENTRY, 5, PAGE_EXECUTE_READWRITE, &old);
    WriteProcessMemory(pi.hProcess, (void *)ENTRY, jmp, 5, &n);
    FlushInstructionCache(pi.hProcess, NULL, 0);
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code_; GetExitCodeProcess(pi.hProcess, &code_);
    printf("Myst4.exe exited with %lu\n", code_);
    return 0;
}
