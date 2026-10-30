#!/usr/bin/env bash
# Build libPFO.so (PFO 50 netcode + LAN fake-Steam layer) for Android with the NDK.
# Usage: scripts/build_pfo_android.sh [abi ...]   (default: arm64-v8a x86_64)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK="${ANDROID_NDK:-$HOME/AppData/Local/Android/Sdk/ndk/27.2.12479018}"
TC="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
API=24
SRC="${PFO_SRC:-$ROOT/work/src/PFO-50/PFO}"   # PFO 50 source with the Android port applied (build.sh)
ENET="${ENET_SRC:-$ROOT/work/src/enet}"        # ENet 1.3.18 with okplay's relay hooks
OUT="${OUT:-$ROOT/work/native}"
ABIS=("${@:-arm64-v8a x86_64}")
# shellcheck disable=SC2206
ABIS=(${ABIS[@]})

declare -A TRIPLE=([arm64-v8a]=aarch64-linux-android [x86_64]=x86_64-linux-android [armeabi-v7a]=armv7a-linux-androideabi)

for abi in "${ABIS[@]}"; do
  t="${TRIPLE[$abi]}$API"
  o="$OUT/$abi"
  mkdir -p "$o/obj"
  echo "== $abi"
  for c in callbacks compress host list packet peer protocol unix; do
    "$TC/clang.exe" --target="$t" -O2 -fPIC -DHAS_SOCKLEN_T=1 -I"$ENET/include" -c "$ENET/$c.c" -o "$o/obj/enet_$c.o"
  done
  "$TC/clang++.exe" --target="$t" -std=c++20 -O2 -fPIC -fvisibility=hidden -DNDEBUG \
    -Wno-everything -fno-exceptions -fno-rtti \
    -I"$SRC" -I"$ENET/include" \
    -c "$SRC/dllmain.cpp" -o "$o/obj/dllmain.o"
  "$TC/clang++.exe" --target="$t" -shared -static-libstdc++ -Wl,-soname,libPFO.so -Wl,--no-undefined -ldl \
    "$o"/obj/*.o -llog -o "$o/libPFO.so"
  "$TC/llvm-strip.exe" --strip-unneeded "$o/libPFO.so"
  ls -la "$o/libPFO.so"
done
