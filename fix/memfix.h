#pragma once
#include <windows.h>

/* Patch ltkrn13n.dll's imports so LEADTOOLS bitmaps live in cached heap memory. Returns number of IAT slots patched (5 on success). */
int memfix_install(void);
void memfix_stats(long *live, long *peak, long *count);
void *memfix_patch_iat(HMODULE mod, const char *dll, const char *name, void *fn);
