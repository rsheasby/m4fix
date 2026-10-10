#pragma once

/* Patch DareDSound3D_bin_rd.dll so its unnamed mutexes become critical sections. Returns IAT slots patched (4 on success). */
int lockfix_install(void);
long lockfix_ops(void);
