#!/usr/bin/env bash
# UFO 50 Android Online: build your own APK from your own copy of UFO 50.
#
#   bash build.sh                 check tools, download what's missing, build work/ufo50-online.apk
#   bash build.sh --install       ...and install it on the connected phone (adb)
#   bash build.sh --check         only check the tools and the game, build nothing
#   bash build.sh --clean         forget everything in work/ and start over
#
# Runs in Git Bash on Windows (see README.md for the one-time setup). Everything it downloads or
# builds goes in work/. Paths can be overridden in local.env (see local.env.example).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
WORK="$ROOT/work"; DL="$WORK/dl"
INSTALL=0; CHECK_ONLY=0
for a in "$@"; do
  case "$a" in
    --install) INSTALL=1 ;;
    --check) CHECK_ONLY=1 ;;
    --clean) rm -rf "$WORK" ;;
    -h|--help) sed -n 2,10p "$0"; exit 0 ;;
    *) echo "unknown option $a"; exit 2 ;;
  esac
done
[ -f "$ROOT/local.env" ] && . "$ROOT/local.env"

# ---- what this build is made of (pinned: the patches are written for exactly these) ----------
GAME_VERSION="1.9.5"
GAME_SHA256="8c8fb143faedc8ab1f14d4ab288a4b2cd1f9a286e43de1b5e83ed8ede8266b29"     # data.win
UTMT_URL="https://github.com/UnderminersTeam/UndertaleModTool/releases/download/0.9.2.0/UTMT_CLI_v0.9.2.0-Windows.zip"
UTMT_SHA256="e7573e45d107be34f81f955c6e4afc3c7c8f2628e5a6f307a871e3825b3dfb40"
LOADER_URL="https://github.com/phil-macrocheira/UFO-50-MOD-LOADER/releases/download/v2.3.0/UFO-50-Mod-Loader-win-Portable.zip"
LOADER_SHA256="4b9c5ade275916fe5cb4b05b2e516d1d31187aee2bd16336f1ddd1496f1126a4"
PFO_MOD_URL="https://gamebanana.com/dl/1778793"          # PFO 50 0.10.0 (gamebanana.com/mods/648717)
PFO_MOD_SHA256="fdf134dc776373bd6cb233c7dadf6cae1514174f48538152f72aa0016cfd08b1"
MMM_URL="https://gamebanana.com/dl/1836289"              # MultiMadnessMod 1.30 (gamebanana.com/mods/605308)
MMM_SHA256="9e4f97abcbb2fd04b71ffdfed86feb53c7625590f946b1bc77aced3b38b4ab99"
PFO_SRC_REPO="https://github.com/Blaizer/PFO-50.git"
PFO_SRC_COMMIT="b8c9327dd2bc9b28d47d89bbb11049e2fd03cc22"  # "Updates for 0.10.0 release"
ENET_REPO="https://github.com/lsalzman/enet.git"
ENET_TAG="v1.3.18"
OKPLAY_RAW="https://raw.githubusercontent.com/Patrick-Working/okplay"
OKPLAY_COMMIT="${OKPLAY_COMMIT:-d536388e1714e905f0db448faf610558cb12450a}"
GM_RUNTIME="2024.14.1.253"
NDK_VERSION="27.2.12479018"
BUILD_TOOLS="35.0.0"
ANDROID_PLATFORM="android-35"

# ---- where things are (override in local.env) ----------------------------------------------
UFO50_DIR="${UFO50_DIR:-}"
ANDROID_SDK="${ANDROID_SDK:-${LOCALAPPDATA:-$HOME/AppData/Local}/Android/Sdk}"
GM_RUNTIMES="${GM_RUNTIMES:-C:/ProgramData/GameMakerStudio2/Cache/runtimes}"
GM_USER_DIR="${GM_USER_DIR:-}"
JAVA_HOME="${JAVA_HOME:-}"
PY="${PY:-}"

say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
ok()   { printf '   ok   %s\n' "$*"; }
miss() { printf '   MISSING  %s\n' "$*"; MISSING=$((MISSING + 1)); }
die()  { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
win()  { cygpath -w "$1"; }
mixed(){ cygpath -m "$1"; }
sha()  { sha256sum "$1" | cut -d' ' -f1; }
upath(){ [ -n "$1" ] && cygpath -u "$1" || true; }   # Windows-style path -> /c/...

# paths may come in Windows form (local.env, the environment): use /c/... inside this script
ANDROID_SDK="$(upath "$ANDROID_SDK")"; GM_RUNTIMES="$(upath "$GM_RUNTIMES")"; GM_USER_DIR="$(upath "$GM_USER_DIR")"
UFO50_DIR="$(upath "$UFO50_DIR")"; JAVA_HOME="$(upath "$JAVA_HOME")"; JAVA_HOME="${JAVA_HOME%/}"
MODDED_DIR="$(upath "${MODDED_DIR:-}")"; OKPLAY_DIR="$(upath "${OKPLAY_DIR:-}")"

fetch() {  # fetch <url> <file> <sha256>
  local url="$1" out="$2" want="$3"
  if [ -f "$out" ] && [ "$(sha "$out")" = "$want" ]; then return; fi
  echo "   downloading $(basename "$out")"
  curl -fL --retry 3 -o "$out.part" "$url" || die "download failed: $url"
  [ "$(sha "$out.part")" = "$want" ] || die "$(basename "$out"): checksum doesn't match (the file on the server changed?)"
  mv "$out.part" "$out"
}

unzip_to() {  # unzip_to <zip> <dir>
  rm -rf "$2"; mkdir -p "$2"
  "$PY" -c "import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$(win "$1")" "$(win "$2")"
}

# ---- 1. tools and the game ------------------------------------------------------------------
say "Checking tools"
MISSING=0
command -v git >/dev/null && ok "git" || miss "git (Git for Windows: https://git-scm.com)"
command -v curl >/dev/null && ok "curl" || miss "curl (comes with Git for Windows)"
if [ -z "$PY" ]; then
  if command -v py >/dev/null && py -3 -c "" 2>/dev/null; then PY="py -3"; elif command -v python >/dev/null; then PY=python; fi
fi
if [ -n "$PY" ]; then
  PY_BIN=$($PY -c "import sys; print(sys.executable)" | tr -d '\r'); PY="$(cygpath -u "$PY_BIN")"
  ok "Python ($PY_BIN)"
  "$PY" -c "import PIL" 2>/dev/null && ok "Pillow" || miss "Pillow (run: $PY -m pip install pillow)"
else
  miss "Python 3 (https://www.python.org, tick 'Add to PATH')"
fi

if [ -z "$JAVA_HOME" ]; then
  for j in "/c/Program Files/Eclipse Adoptium"/jdk-21* "/c/Program Files/Eclipse Adoptium"/jdk-17* "/c/Program Files/Java"/jdk-21* "/c/Program Files/Java"/jdk-17* "/c/Program Files/Android/Android Studio/jbr"; do
    [ -x "$j/bin/java.exe" ] && { JAVA_HOME="$j"; break; }
  done
fi
[ -n "$JAVA_HOME" ] && [ -x "$JAVA_HOME/bin/java.exe" ] && ok "JDK ($(win "$JAVA_HOME"))" || miss "JDK 17 or 21 (winget install EclipseAdoptium.Temurin.21.JDK)"
JAVA_U="$JAVA_HOME"; [ -n "$JAVA_U" ] && export JAVA_HOME="$(win "$JAVA_U")"   # Windows form for the Java tools

SDK_OK=1
for p in "platform-tools/adb.exe" "build-tools/$BUILD_TOOLS/aapt.exe" "platforms/$ANDROID_PLATFORM/android.jar" "ndk/$NDK_VERSION/toolchains/llvm"; do
  [ -e "$ANDROID_SDK/$p" ] || SDK_OK=0
done
if [ $SDK_OK = 1 ]; then ok "Android SDK ($(win "$ANDROID_SDK"))"; else
  SDKM=$(ls "$ANDROID_SDK"/cmdline-tools/*/bin/sdkmanager.bat 2>/dev/null | head -1 || true)
  if [ -n "$SDKM" ] && [ $CHECK_ONLY = 0 ] && [ -n "$JAVA_HOME" ]; then
    echo "   installing Android SDK parts (you'll be asked to accept Google's licences)"
    "$SDKM" --licenses
    "$SDKM" "platform-tools" "build-tools;$BUILD_TOOLS" "platforms;$ANDROID_PLATFORM" "ndk;$NDK_VERSION"
    ok "Android SDK ($ANDROID_SDK)"
  else
    miss "Android SDK parts in $ANDROID_SDK: platform-tools, build-tools $BUILD_TOOLS, $ANDROID_PLATFORM, NDK $NDK_VERSION (README: 'Android SDK')"
  fi
fi

GM_RT="$GM_RUNTIMES/runtime-$GM_RUNTIME"
[ -x "$GM_RT/bin/igor/windows/x64/Igor.exe" ] && ok "GameMaker runtime $GM_RUNTIME" || miss "GameMaker runtime $GM_RUNTIME (README: 'GameMaker')"
if [ -z "$GM_USER_DIR" ]; then
  GM_USER_DIR=$(ls -d "${APPDATA:-$HOME/AppData/Roaming}"/GameMakerStudio2/*/ 2>/dev/null | while read -r d; do [ -f "$d/licence.plist" ] && echo "${d%/}"; done | head -1 || true)
fi
[ -n "$GM_USER_DIR" ] && [ -f "$GM_USER_DIR/licence.plist" ] && ok "GameMaker sign-in ($(basename "$GM_USER_DIR"))" || miss "GameMaker sign-in (open GameMaker once and log in)"

if [ -z "$UFO50_DIR" ]; then
  for lib in "/c/Program Files (x86)/Steam" $(grep -o '"path"[[:space:]]*"[^"]*"' "/c/Program Files (x86)/Steam/steamapps/libraryfolders.vdf" 2>/dev/null | sed 's/.*"\([^"]*\)"$/\1/; s/\\\\/\//g' | xargs -I{} cygpath -u "{}" 2>/dev/null); do
    [ -f "$lib/steamapps/common/UFO 50/data.win" ] && { UFO50_DIR="$lib/steamapps/common/UFO 50"; break; }
  done
fi
if [ -n "$UFO50_DIR" ] && [ -f "$UFO50_DIR/data.win" ]; then
  if [ "$(sha "$UFO50_DIR/data.win")" = "$GAME_SHA256" ]; then ok "UFO 50 $GAME_VERSION ($(win "$UFO50_DIR"))"
  else miss "UFO 50 $GAME_VERSION: $UFO50_DIR/data.win is a different version (this build is made for $GAME_VERSION)"; fi
else
  miss "UFO 50 (buy it on Steam and install it, or set UFO50_DIR in local.env)"
fi

[ $MISSING = 0 ] || die "$MISSING thing(s) missing - see above and README.md"
[ $CHECK_ONLY = 1 ] && { echo; echo "Everything is in place."; exit 0; }
mkdir -p "$DL"

# ---- 2. downloads ---------------------------------------------------------------------------
say "Downloading tools, mods and sources"
fetch "$UTMT_URL" "$DL/utmt.zip" "$UTMT_SHA256"
fetch "$LOADER_URL" "$DL/modloader.zip" "$LOADER_SHA256"
fetch "$PFO_MOD_URL" "$DL/pfo50-0.10.0.zip" "$PFO_MOD_SHA256"
fetch "$MMM_URL" "$DL/multimadnessmod-1.30.zip" "$MMM_SHA256"
[ -x "$WORK/utmt/UndertaleModCli.exe" ] || unzip_to "$DL/utmt.zip" "$WORK/utmt"
UTMT="$WORK/utmt/UndertaleModCli.exe"

SRC="$WORK/src"; mkdir -p "$SRC"
if [ ! -d "$SRC/PFO-50/.git" ]; then
  git clone -q "$PFO_SRC_REPO" "$SRC/PFO-50"
fi
( cd "$SRC/PFO-50" && git -c advice.detachedHead=false checkout -q -f "$PFO_SRC_COMMIT" && git clean -qfd PFO/android 2>/dev/null; git apply "$ROOT/pfo/pfo50-android.patch" )
mkdir -p "$SRC/PFO-50/PFO/android"
cp -r "$ROOT/pfo/android/." "$SRC/PFO-50/PFO/android/"
if [ ! -d "$SRC/enet/.git" ]; then git -c advice.detachedHead=false clone -q --branch "$ENET_TAG" "$ENET_REPO" "$SRC/enet"; fi
( cd "$SRC/enet" && git -c advice.detachedHead=false checkout -q -f "$ENET_TAG" )
# okplay's native netcode (room list, hole punching, UPnP, relay): github.com/Patrick-Working/okplay/oknative
for f in src/netplus.inc src/okplay_client.inc enet/enet-1.3.18-relay-hooks.patch; do
  if [ -n "${OKPLAY_DIR:-}" ]; then cp "$OKPLAY_DIR/oknative/$f" "$SRC/$(basename "$f")"
  else curl -fsSL --retry 3 -o "$SRC/$(basename "$f")" "$OKPLAY_RAW/$OKPLAY_COMMIT/oknative/$f" || die "couldn't fetch oknative/$f from okplay"; fi
done
cp "$SRC/netplus.inc" "$SRC/okplay_client.inc" "$SRC/PFO-50/PFO/android/"
( cd "$SRC/enet" && git apply "$SRC/enet-1.3.18-relay-hooks.patch" )
ok "PFO 50 source + Android port, ENet $ENET_TAG + relay hooks, okplay netcode"

# ---- 3. mods: the UFO 50 Mod Loader makes a modded copy of the game -------------------------
LOADER="$WORK/modloader"
MODDED="${MODDED_DIR:-$LOADER/workspace/UFO 50 Modded Copy}"
if [ ! -f "$MODDED/data.win" ] || [ ! -f "$MODDED/PFO.dll" ]; then
  say "Applying the mods (PFO 50 + MultiMadnessMod) with the UFO 50 Mod Loader"
  [ -x "$LOADER/UFO 50 Mod Loader.exe" ] || unzip_to "$DL/modloader.zip" "$LOADER"
  MODS="$LOADER/UFO 50 mods"; mkdir -p "$MODS" "$LOADER/workspace"
  T="$WORK/tmp_mods"; unzip_to "$DL/pfo50-0.10.0.zip" "$T"; rm -rf "$MODS/PFO 50"; mv "$T"/*/"PFO 50" "$MODS/PFO 50"
  unzip_to "$DL/multimadnessmod-1.30.zip" "$T"; rm -rf "$MODS/MultiMadnessMod"; mv "$T/MultiMadnessMod" "$MODS/MultiMadnessMod"; rm -rf "$T"
  if [ ! -f "$LOADER/workspace/settings.json" ]; then
    "$PY" - "$(win "$UFO50_DIR")" "$(win "$LOADER/workspace/settings.json")" <<'EOF'
import json, sys
json.dump({"GamePath": sys.argv[1], "EnabledMods": ["MultiMadnessMod", "PFO 50"],
           "CheckForUpdatesAutomatically": False}, open(sys.argv[2], "w"), indent=2)
EOF
  fi
  cat <<EOF

   The UFO 50 Mod Loader is opening. In it:
     1. If it asks for the game folder, choose:  $(win "$UFO50_DIR")
     2. Make sure "PFO 50" and "MultiMadnessMod" are ticked.
     3. Click Install and wait until it says it's done. Then close the Mod Loader.
EOF
  ( cd "$LOADER" && "./UFO 50 Mod Loader.exe" ) || true
  [ -f "$MODDED/data.win" ] || { read -rp "   Press Enter once the Mod Loader has finished installing... " _; }
  [ -f "$MODDED/data.win" ] && [ -f "$MODDED/PFO.dll" ] || die "no modded copy at $MODDED (did Install finish with both mods ticked?)"
fi
ok "modded copy: $MODDED"

# ---- 4. libPFO.so: PFO 50's netcode for Android ---------------------------------------------
say "Building libPFO.so (NDK)"
NATIVE="$WORK/native"
ANDROID_NDK="$ANDROID_SDK/ndk/$NDK_VERSION" PFO_SRC="$SRC/PFO-50/PFO" ENET_SRC="$SRC/enet" OUT="$NATIVE" \
  bash "$ROOT/scripts/build_pfo_android.sh" >/dev/null
GM="$WORK/gm"; rm -rf "$GM/project"; mkdir -p "$GM"
cp -r "$ROOT/android/gm-project" "$GM/project"
for abi in x86_64 arm64-v8a; do mkdir -p "$GM/project/extensions/PFO/AndroidSource/libs/$abi"; cp "$NATIVE/$abi/libPFO.so" "$GM/project/extensions/PFO/AndroidSource/libs/$abi/"; done
ok "arm64-v8a + x86_64"

# ---- 5. the GameMaker Android runner (Igor, with the touch overlay and the online service) ---
KEYS="$WORK/keys"; mkdir -p "$KEYS"
KEYSTORE="$KEYS/ufo50-android.jks"; KEY_PASS="ufo50android"   # your own signing key, made on first build
[ -f "$KEYSTORE" ] || "$JAVA_U/bin/keytool.exe" -genkeypair -keystore "$(win "$KEYSTORE")" -alias ufo50 -keyalg RSA -keysize 2048 \
  -validity 10000 -storepass "$KEY_PASS" -keypass "$KEY_PASS" -dname "CN=UFO 50 Android personal build" >/dev/null
RUNNER="$WORK/runner.apk"
if [ ! -f "$RUNNER" ] || [ -n "$(find "$GM/project" -newer "$RUNNER" -type f -print -quit)" ]; then
  say "Building the Android runner with Igor (takes a few minutes)"
  U="$GM/user"; rm -rf "$U"; mkdir -p "$U"; cp "$GM_USER_DIR/licence.plist" "$U/"
  "$PY" - "$(win "$U/local_settings.json")" "$(win "$ANDROID_SDK")" "$(win "$JAVA_U")" "$NDK_VERSION" "$(win "$KEYSTORE")" "$KEY_PASS" "$(win "$GM_RT")" <<'EOF'
import json, sys
out, sdk, jdk, ndk, ks, pw, rt = sys.argv[1:]
P = "machine.Platform Settings.Android."
json.dump({
    P + "Paths.sdk_location": sdk, P + "Paths.ndk_location": sdk + "\\ndk\\" + ndk, P + "Paths.jdk_location": jdk,
    P + "Packaging.full_install": True, P + "Keystore.filename": ks, P + "Keystore.alias": "ufo50",
    P + "Keystore.keystore_password": pw, P + "Keystore.keystore_alias_password": pw,
    P + "max_heap_size": 2, P + "use_debug_runner": False, P + "runner_path": rt + "\\android\\runner",
    "version": "1.1"}, open(out, "w"), indent=2)
EOF
  rm -rf "$GM/cache/Android/Default/com.unofficial.ufo50/build/outputs"
  "$GM_RT/bin/igor/windows/x64/Igor.exe" -j=8 --project="$(win "$GM/project/runner.yyp")" --user="$(win "$U")" \
    --runtimePath="$(win "$GM_RT")" --runtime=VM --cache="$(win "$GM/cache")" --temp="$(win "$GM/temp")" \
    --of="$(win "$GM/out.apk")" -- Android Package > "$GM/igor.log" 2>&1 || true
  BUILT="$GM/cache/Android/Default/com.unofficial.ufo50/build/outputs/apk/debug/com.unofficial.ufo50-debug.apk"
  if [ ! -f "$BUILT" ]; then grep -E 'error:|What went wrong|Exception' -A3 "$GM/igor.log" | head -30 || true; die "runner build failed (see $GM/igor.log)"; fi
  cp "$BUILT" "$RUNNER"
fi
ok "runner"

# ---- 6. game data: Android patches, overlay font and icons from your copy -------------------
say "Patching the game for Android"
GAME="$WORK/game"; rm -rf "$GAME"; mkdir -p "$GAME"
cp -r "$MODDED/." "$GAME/"
utmt() { (yes y | head -50) | "$UTMT" "$@"; }
utmt load "$(win "$MODDED/data.win")" -s "$(win "$ROOT/scripts/android_patch.csx")" -o "$(win "$GAME/data.win")" -f 2>&1 \
  | grep -aE 'PFO|Removed|rror|hot seat|pad bindings|overlay support|lobby menu|quit:|join timeout' || true
rm -rf "$WORK/font_raw" "$WORK/icons_raw"
FONT_SPRITES=sFontDefault OUT_DIR="$(mixed "$WORK/font_raw")" utmt load "$(win "$GAME/data.win")" -s "$(win "$ROOT/scripts/export_font.csx")" 2>&1 | grep -aE 'rror' || true
SPRITES=sKeyIcons,sVirtualInputs OUT_DIR="$(mixed "$WORK/icons_raw")" utmt load "$(win "$GAME/data.win")" -s "$(win "$ROOT/scripts/export_sprites.csx")" 2>&1 | grep -aE 'rror' || true
"$PY" "$(win "$ROOT/scripts/make_font.py")" "$(win "$WORK/font_raw")" sFontDefault "$(win "$GAME/pfo_font.png")"
"$PY" "$(win "$ROOT/scripts/make_strip.py")" "$(win "$WORK/icons_raw/sKeyIcons")" "$(win "$GAME/pfo_keyicons.png")"
"$PY" "$(win "$ROOT/scripts/make_strip.py")" "$(win "$WORK/icons_raw/sVirtualInputs")" "$(win "$GAME/pfo_vinputs.png")"
ok "game data"

# ---- 7. the APK -----------------------------------------------------------------------------
say "Packing the APK"
APK="$WORK/ufo50-online.apk"
ANDROID_SDK="$(win "$ANDROID_SDK")" BUILD_TOOLS_VERSION="$BUILD_TOOLS" KEYSTORE="$(win "$KEYSTORE")" KEY_PASS="$KEY_PASS" BUILD_WORK="$(win "$WORK/apk")" \
  "$PY" "$(win "$ROOT/scripts/build_apk.py")" "$(win "$GAME")" --base "$(win "$RUNNER")" --native-dir "$(win "$NATIVE")" --out "$(win "$APK")" | tail -1
echo
echo "Done: $(win "$APK")"
echo "It contains your copy of the game: keep it to yourself (see README.md)."

if [ $INSTALL = 1 ]; then
  say "Installing"
  "$ANDROID_SDK/platform-tools/adb.exe" install -r "$(win "$APK")"
fi
