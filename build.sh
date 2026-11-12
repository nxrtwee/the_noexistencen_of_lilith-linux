#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
WL=${LILITH_WIN_LIBDIR:-}
if [ -z "$WL" ]; then
 for p in /usr/lib/wine/x86_64-windows /usr/lib64/wine/x86_64-windows /usr/lib/x86_64-linux-gnu/wine/x86_64-windows; do
  if [ -f "$p/libkernel32.a" ]; then WL=$p; break; fi
 done
fi
INC=${LILITH_WINE_INCLUDE:-/usr/include/wine}
if [ ! -f "$INC/windows/windows.h" ] || [ -z "$WL" ]; then
 echo 'Wine development headers/import libraries required. Set LILITH_WINE_INCLUDE and LILITH_WIN_LIBDIR if installed in a nonstandard location.' >&2
 exit 1
fi
"${CLANG:-clang}" --target=x86_64-w64-windows-gnu -O2 -shared -nostdlib -ffreestanding -fno-stack-protector -mno-stack-arg-probe \
 -I"$INC/windows" -I"$INC/msvcrt" -D_WIN64 -DWINE_NO_TRACE_MSGS -Wno-ignored-attributes \
 -fuse-ld=lld -Wl,--entry,DllMain -Wl,--subsystem,windows \
 src/proxy/version.c src/proxy/version.def -L"$WL" -lkernel32 -luser32 -lgdi32 -lucrtbase -o build/version.dll
if command -v llvm-readobj >/dev/null 2>&1; then llvm-readobj --coff-exports build/version.dll | grep -c 'Name:'; fi
