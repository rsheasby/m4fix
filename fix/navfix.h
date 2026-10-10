#pragma once
#include <windows.h>

/* Arrange for the 550 ms pre-transition timer to become `ms`. Returns nonzero if the hook/patch is in place. */
int navfix_install(DWORD ms);
/* 1 = patched, 0 = not yet (AI library not loaded), -1 = unexpected bytes, left alone. */
int navfix_state(void);
