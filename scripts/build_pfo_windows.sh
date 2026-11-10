#!/usr/bin/env bash
# Build PFO.dll for Windows with our netcode in place of Steam's (the same sources as the Android
# libPFO.so: pfo/android/fakesteam_impl.inc, savestate.inc; the Windows pieces are in pfo/win).
# Compiler: llvm-mingw (https://github.com/mstorsjo/llvm-mingw); LLVM_MINGW is its folder (build.sh
# downloads it into work/llvm-mingw).
# Usage: scripts/build_pfo_windows.sh   (build.sh --windows runs it)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${PFO_SRC:-$ROOT/work/src/PFO-50/PFO}"   # PFO 50 source with our port applied (build.sh)
ENET="${ENET_SRC:-$ROOT/work/src/enet}"        # ENet 1.3.18 with okplay's relay hooks
OUT="${OUT:-$ROOT/work/native-windows}"

MINGW="${LLVM_MINGW:-}"
if [ -z "$MINGW" ]; then
  for d in "$ROOT"/work/llvm-mingw/llvm-mingw-*; do [ -d "$d/bin" ] && MINGW="$d"; done
fi
TC="$MINGW/bin"
[ -x "$TC/x86_64-w64-mingw32-clang++.exe" ] || [ -x "$TC/x86_64-w64-mingw32-clang++" ] || { echo "llvm-mingw not found (work/llvm-mingw, or set LLVM_MINGW)" >&2; exit 1; }
CC="$TC/x86_64-w64-mingw32-clang"
CXX="$TC/x86_64-w64-mingw32-clang++"

mkdir -p "$OUT/obj"
rm -f "$OUT"/obj/*.o
# ENet: win32.c has no relay hooks (the patch adds them to unix.c only), so its send and receive
# are renamed and win/enet_hooks_win.c puts the hooks in front of them.
for c in callbacks compress host list packet peer protocol; do
  "$CC" -O2 -I"$ENET/include" -c "$ENET/$c.c" -o "$OUT/obj/enet_$c.o"
done
"$CC" -O2 -I"$ENET/include" -Denet_socket_send=enet_socket_send_real -Denet_socket_receive=enet_socket_receive_real \
  -c "$ENET/win32.c" -o "$OUT/obj/enet_win32.o"
"$CC" -O2 -I"$ENET/include" -c "$SRC/win/enet_hooks_win.c" -o "$OUT/obj/enet_hooks_win.o"

"$CXX" -std=c++20 -O2 -DNDEBUG -DPFO_WINDOWS_OURS -DUNICODE -D_UNICODE \
  -ffile-prefix-map="$(cygpath -m "$SRC/..")"=. \
  -Wno-everything -fno-exceptions -fno-rtti \
  -I"$SRC/win/posix" -I"$SRC" -I"$ENET/include" \
  -c "$SRC/dllmain.cpp" -o "$OUT/obj/dllmain.o"

# (no timestamp in the file: the same sources give the same PFO.dll, so its checksum can be pinned)
"$CXX" -shared -static -Wl,--no-undefined -Wl,--no-insert-timestamp "$OUT"/obj/*.o \
  -lkernel32 -luser32 -lgdi32 -lcomctl32 -lws2_32 -lwinmm -liphlpapi -lwinhttp -lpthread -lmincore \
  -o "$OUT/PFO.dll"
# where each function is, for tests/win/stacks.py (the DLL itself ships without names)
base=$("$TC/llvm-readobj" --file-headers "$OUT/PFO.dll" | grep -a ImageBase | sed -E 's/.*0x//')
"$TC/llvm-nm" -n --defined-only -C "$OUT/PFO.dll" | awk -v b="$base" 'BEGIN { bb = strtonum("0x" b) }
  $2 ~ /^[tT]$/ { a = strtonum("0x" $1); n = $3; for (i = 4; i <= NF; i++) n = n " " $i; printf "%x %s" ORS, a - bb, n }' > "$OUT/PFO.syms"
"$TC/llvm-strip" --strip-unneeded "$OUT/PFO.dll" 2>/dev/null || "$TC/llvm-strip.exe" --strip-unneeded "$OUT/PFO.dll"

# every function the game looks up must be exported under its MSVC name
n=$("$TC/llvm-readobj" --coff-exports "$OUT/PFO.dll" | grep -c '^ *Name: ?')
echo "PFO.dll: $n functions exported"
[ "$n" -ge 80 ] || { echo "expected at least 80 exported functions" >&2; exit 1; }
ls -la "$OUT/PFO.dll"
