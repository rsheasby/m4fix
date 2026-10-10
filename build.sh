#!/bin/sh
# Build m4fix and package a release.
#
#   ./build.sh            build/CrashRpt.dll, build/m4fix-setup.exe, dist/m4fix-<version>.zip
#   ./build.sh research   also build the research tracer (research/tracer)
#
# Requires the i686 MinGW-w64 toolchain:  brew install mingw-w64  |  apt install gcc-mingw-w64-i686
set -e
cd "$(dirname "$0")"
CC=${CC:-i686-w64-mingw32-gcc}
WINDRES=${WINDRES:-i686-w64-mingw32-windres}
VERSION=$(sed -n 's/^#define M4FIX_VERSION_STR "\(.*\)"/\1/p' fix/version.h)
mkdir -p build dist
grep -q "^VERSION=\"$VERSION\"" installer/mac/m4fix.sh || { echo "installer/mac/m4fix.sh VERSION does not match fix/version.h ($VERSION)" >&2; exit 1; }

# The fix: CrashRpt.dll proxy, no C runtime (kernel32/user32 only).
$WINDRES -I fix fix/m4fix.rc -O coff -o build/m4fix.res
$CC -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -shared -nostdlib -fno-builtin -s \
    -o build/CrashRpt.dll fix/m4fix.c fix/memfix.c fix/lockfix.c fix/navfix.c build/m4fix.res \
    -lkernel32 -luser32 -Wl,-e,_DllMain@12 -Wl,--enable-stdcall-fixup -Wl,--dynamicbase -Wl,--nxcompat

# Windows installer with the DLL and default ini embedded.
(cd installer && $WINDRES setup.rc -O coff -o ../build/setup.res)
$CC -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -mwindows -nostdlib -fno-builtin -s \
    -o build/m4fix-setup.exe installer/setup.c build/setup.res \
    -lkernel32 -luser32 -ladvapi32 -lshell32 -lole32 -Wl,-e,_start@0 -Wl,--dynamicbase -Wl,--nxcompat

# Release zip.
STAGE=build/stage/m4fix-$VERSION
rm -rf build/stage; mkdir -p "$STAGE/Windows" "$STAGE/Mac" "$STAGE/Manual install"
cp build/m4fix-setup.exe "$STAGE/Windows/"
cp installer/mac/m4fix.sh "installer/mac/Install m4fix.command" build/CrashRpt.dll config/m4fix.ini "$STAGE/Mac/"
chmod +x "$STAGE/Mac/m4fix.sh" "$STAGE/Mac/Install m4fix.command"
cp build/CrashRpt.dll config/m4fix.ini "$STAGE/Manual install/"
cp installer/README.txt LICENSE "$STAGE/"
rm -f "dist/m4fix-$VERSION.zip"
(cd build/stage && zip -qrX "../../dist/m4fix-$VERSION.zip" "m4fix-$VERSION")
echo "built m4fix $VERSION: build/CrashRpt.dll build/m4fix-setup.exe dist/m4fix-$VERSION.zip"

if [ "${1:-}" = research ]; then
    $CC -O2 -Wall -Wno-unused-function -masm=intel -D__USE_MINGW_ANSI_STDIO=1 -shared -static-libgcc \
        -o research/tracer/m4trace.dll research/tracer/tracer.c research/tracer/capture.c \
        fix/memfix.c fix/lockfix.c fix/navfix.c -ld3d9
    $CC -O2 -Wall -o research/tracer/m4launch.exe research/tracer/launcher.c
    echo "built research/tracer/m4trace.dll research/tracer/m4launch.exe"
fi
