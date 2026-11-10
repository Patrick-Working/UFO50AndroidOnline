#!/usr/bin/env bash
# UFO 50 Android Online - full build: also builds the runner (touch overlay, online service,
# libPFO.so) from source instead of downloading the runner kit. For changing the overlay or the
# netcode; to just play, use the quick build (build.bat).
#
#   bash build.sh                 build the runner, then the APK (work/ufo50-online.apk)
#   bash build.sh --install       ...and install it on the connected phone (adb)
#   bash build.sh --kit           build the runner kit only, into work/kit/ (it says how the quick build gets it)
#   bash build.sh --windows       the Windows build instead: PFO.dll from source (downloads the
#                                 llvm-mingw compiler into work/), then the game folder
#                                 work/windows/UFO 50 Online. Needs none of the Android tools.
#   bash build.sh --windows --kit build PFO.dll only, into work/kit/
#   bash build.sh --check         only check the tools (with --windows: the Windows build's)
#   bash build.sh --clean         forget everything in work/ and start over
#
# Runs in Git Bash on Windows (see README.md). Paths can be overridden in local.env.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
WORK="$ROOT/work"
VERSION="$(head -1 VERSION)"
INSTALL=0; CHECK_ONLY=0; KIT=0; WINDOWS=0
for a in "$@"; do
  case "$a" in
    --install) INSTALL=1 ;;
    --check) CHECK_ONLY=1 ;;
    --kit) KIT=1 ;;
    --windows) WINDOWS=1 ;;
    --clean) rm -rf "$WORK" ;;
    -h|--help) sed -n 2,16p "$0"; exit 0 ;;
    *) echo "unknown option $a"; exit 2 ;;
  esac
done
[ -f "$ROOT/local.env" ] && . "$ROOT/local.env"

# ---- pinned sources -------------------------------------------------------------------------
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
# the compiler for the Windows PFO.dll (github.com/mstorsjo/llvm-mingw)
MINGW_NAME="llvm-mingw-20261104-ucrt-x86_64"
MINGW_URL="https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/$MINGW_NAME.zip"
MINGW_SHA="552191b5754c49a5a20723276a6fc56541ea1922c4c50b7b669022333fc1b40f"

say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
ok()   { printf '   ok   %s\n' "$*"; }
miss() { printf '   MISSING  %s\n' "$*"; MISSING=$((MISSING + 1)); }
die()  { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
win()  { cygpath -w "$1"; }
upath(){ [ -n "$1" ] && cygpath -u "$1" || true; }   # Windows-style path -> /c/...

ANDROID_SDK="$(upath "${ANDROID_SDK:-${LOCALAPPDATA:-$HOME/AppData/Local}/Android/Sdk}")"
GM_RUNTIMES="$(upath "${GM_RUNTIMES:-C:/ProgramData/GameMakerStudio2/Cache/runtimes}")"
GM_USER_DIR="$(upath "${GM_USER_DIR:-}")"
JAVA_HOME="$(upath "${JAVA_HOME:-}")"; JAVA_HOME="${JAVA_HOME%/}"
OKPLAY_DIR="$(upath "${OKPLAY_DIR:-}")"
LLVM_MINGW="$(upath "${LLVM_MINGW:-}")"; LLVM_MINGW="${LLVM_MINGW%/}"

if [ $WINDOWS = 1 ]; then echo "UFO 50 Android Online v$VERSION - full build of the Windows version"
else echo "UFO 50 Android Online v$VERSION - full build"; fi
[ $WINDOWS = 0 ] || [ $INSTALL = 0 ] || die "--install is for the Android build (the Windows build is a folder: work/windows)"

# ---- 1. tools -------------------------------------------------------------------------------
say "Checking tools"
MISSING=0
command -v git >/dev/null && ok "git" || miss "git (Git for Windows: https://git-scm.com)"
if [ $WINDOWS = 1 ]; then
  [ -n "$LLVM_MINGW" ] || LLVM_MINGW="$WORK/llvm-mingw/$MINGW_NAME"
  if [ -x "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++.exe" ]; then ok "llvm-mingw ($(win "$LLVM_MINGW"))"
  elif [ $CHECK_ONLY = 1 ]; then echo "   --     llvm-mingw: not here yet, the build downloads it (190 MB) into work/llvm-mingw"
  else
    echo "   downloading the llvm-mingw compiler (190 MB)"
    mkdir -p "$WORK/dl" "$WORK/llvm-mingw"
    Z="$WORK/dl/$MINGW_NAME.zip"
    [ -f "$Z" ] && [ "$(sha256sum "$Z" | cut -d' ' -f1)" = "$MINGW_SHA" ] || curl -fL --retry 3 -o "$Z" "$MINGW_URL" || die "couldn't download $MINGW_URL"
    [ "$(sha256sum "$Z" | cut -d' ' -f1)" = "$MINGW_SHA" ] || die "$MINGW_NAME.zip doesn't match its checksum"
    rm -rf "$WORK/llvm-mingw/$MINGW_NAME"
    if command -v unzip >/dev/null; then unzip -q "$Z" -d "$WORK/llvm-mingw"; else tar.exe -xf "$(win "$Z")" -C "$(win "$WORK/llvm-mingw")"; fi
    LLVM_MINGW="$WORK/llvm-mingw/$MINGW_NAME"
    [ -x "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++.exe" ] && ok "llvm-mingw ($(win "$LLVM_MINGW"))" || miss "llvm-mingw (unpacking $MINGW_NAME.zip left no compiler in $(win "$LLVM_MINGW"))"
  fi
else
if [ -z "$JAVA_HOME" ]; then
  for j in "/c/Program Files/Eclipse Adoptium"/jdk-21* "/c/Program Files/Eclipse Adoptium"/jdk-17* "/c/Program Files/Java"/jdk-21* "/c/Program Files/Java"/jdk-17* "/c/Program Files/Android/Android Studio/jbr"; do
    [ -x "$j/bin/java.exe" ] && { JAVA_HOME="$j"; break; }
  done
fi
[ -n "$JAVA_HOME" ] && [ -x "$JAVA_HOME/bin/java.exe" ] && ok "JDK ($(win "$JAVA_HOME"))" || miss "JDK 17 or 21 (winget install EclipseAdoptium.Temurin.21.JDK)"
SDK_OK=1
for p in "build-tools/$BUILD_TOOLS/aapt.exe" "platforms/$ANDROID_PLATFORM/android.jar" "ndk/$NDK_VERSION/toolchains/llvm"; do
  [ -e "$ANDROID_SDK/$p" ] || SDK_OK=0
done
if [ $SDK_OK = 1 ]; then ok "Android SDK ($(win "$ANDROID_SDK"))"; else
  SDKM=$(ls "$ANDROID_SDK"/cmdline-tools/*/bin/sdkmanager.bat 2>/dev/null | head -1 || true)
  if [ -n "$SDKM" ] && [ $CHECK_ONLY = 0 ] && [ -n "$JAVA_HOME" ]; then
    echo "   installing Android SDK parts (you'll be asked to accept Google's licences)"
    JAVA_HOME="$(win "$JAVA_HOME")" "$SDKM" --licenses
    JAVA_HOME="$(win "$JAVA_HOME")" "$SDKM" "platform-tools" "build-tools;$BUILD_TOOLS" "platforms;$ANDROID_PLATFORM" "ndk;$NDK_VERSION"
    ok "Android SDK"
  else
    miss "Android SDK in $(win "$ANDROID_SDK"): build-tools $BUILD_TOOLS, $ANDROID_PLATFORM, NDK $NDK_VERSION (README: 'Full build')"
  fi
fi
GM_RT="$GM_RUNTIMES/runtime-$GM_RUNTIME"
[ -x "$GM_RT/bin/igor/windows/x64/Igor.exe" ] && ok "GameMaker runtime $GM_RUNTIME" || miss "GameMaker runtime $GM_RUNTIME with the Android module (README: 'Full build')"
if [ -z "$GM_USER_DIR" ]; then
  GM_USER_DIR=$(ls -d "$(upath "${APPDATA:-$HOME/AppData/Roaming}")"/GameMakerStudio2/*/ 2>/dev/null | while read -r d; do [ -f "$d/licence.plist" ] && echo "${d%/}"; done | head -1 || true)
fi
[ -n "$GM_USER_DIR" ] && [ -f "$GM_USER_DIR/licence.plist" ] && ok "GameMaker sign-in ($(basename "$GM_USER_DIR"))" || miss "GameMaker sign-in (open GameMaker once and log in)"
fi
[ $MISSING = 0 ] || die "$MISSING thing(s) missing - see above and README.md"
[ $CHECK_ONLY = 1 ] && { echo; echo "Everything is in place."; exit 0; }

# ---- 2. sources -----------------------------------------------------------------------------
say "Getting the sources"
SRC="$WORK/src"; mkdir -p "$SRC"
G="git -c advice.detachedHead=false"
[ -d "$SRC/PFO-50/.git" ] || $G clone -q "$PFO_SRC_REPO" "$SRC/PFO-50"
( cd "$SRC/PFO-50" && $G checkout -q -f "$PFO_SRC_COMMIT" && git clean -qfdx PFO/android PFO/win >/dev/null \
  && git apply "$ROOT/pfo/pfo50-android.patch" ) || die "couldn't check out PFO 50 @ ${PFO_SRC_COMMIT:0:7} and apply pfo/pfo50-android.patch"
mkdir -p "$SRC/PFO-50/PFO/android" "$SRC/PFO-50/PFO/win"
cp -r "$ROOT/pfo/android/." "$SRC/PFO-50/PFO/android/"; cp -r "$ROOT/pfo/win/." "$SRC/PFO-50/PFO/win/"
[ -d "$SRC/enet/.git" ] || $G clone -q --branch "$ENET_TAG" "$ENET_REPO" "$SRC/enet"
( cd "$SRC/enet" && $G checkout -q -f "$ENET_TAG" )
# okplay's native netcode (room list, hole punching, UPnP, relay): github.com/Patrick-Working/okplay/oknative
for f in src/netplus.inc src/okplay_client.inc enet/enet-1.3.18-relay-hooks.patch; do
  if [ -n "$OKPLAY_DIR" ]; then cp "$OKPLAY_DIR/oknative/$f" "$SRC/$(basename "$f")"
  else curl -fsSL --retry 3 -o "$SRC/$(basename "$f")" "$OKPLAY_RAW/$OKPLAY_COMMIT/oknative/$f" || die "couldn't fetch oknative/$f from okplay"; fi
done
cp "$SRC/netplus.inc" "$SRC/okplay_client.inc" "$SRC/PFO-50/PFO/android/"
( cd "$SRC/enet" && git apply "$SRC/enet-1.3.18-relay-hooks.patch" )
ok "PFO 50 @ ${PFO_SRC_COMMIT:0:7} + our port, ENet $ENET_TAG + relay hooks, okplay oknative @ ${OKPLAY_COMMIT:0:7}"

# ---- W. the Windows build: PFO.dll, then the quick build puts the game folder together ------
if [ $WINDOWS = 1 ]; then
  say "Building PFO.dll (llvm-mingw)"
  NATIVE="$WORK/native-windows"
  LLVM_MINGW="$LLVM_MINGW" PFO_SRC="$SRC/PFO-50/PFO" ENET_SRC="$SRC/enet" OUT="$NATIVE" \
    bash "$ROOT/scripts/build_pfo_windows.sh" > "$NATIVE.log" 2>&1 || { tail -20 "$NATIVE.log"; die "PFO.dll didn't build (see $(win "$NATIVE.log"))"; }
  ok "$(grep 'functions exported' "$NATIVE.log")"
  if [ $KIT = 1 ]; then
    KITDIR="$WORK/kit"; mkdir -p "$KITDIR"
    KITDLL="$KITDIR/ufo50-online-windows-v$VERSION.dll"
    cp "$NATIVE/PFO.dll" "$KITDLL"
    echo; echo "Windows kit: $(win "$KITDLL")"; echo "SHA-256:     $(sha256sum "$KITDLL" | cut -d' ' -f1)"
    echo "The quick build (build.bat -Windows) does not look in work/kit/. It uses the file in kit/ of"
    echo "this repository, and downloads that file from the version's tag if the one in kit/ is missing"
    echo "or has another checksum. To make it use this one: copy the file to kit/ and put the SHA-256"
    echo "above in the WinKit line of build.ps1 (and the new file name and tag there, if the version changed)."
    exit 0
  fi
  PS=(powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(win "$ROOT/build.ps1")")
  ARGS=(-Windows -WinDll "$(win "$NATIVE/PFO.dll")")
  if [ -n "${MODDED_DIR:-}" ]; then
    MODDED="$(upath "$MODDED_DIR")"
    ARGS+=(-ModdedDir "$(win "${MODDED%/}")")
  fi
  NOPAUSE=1 "${PS[@]}" "${ARGS[@]}"
  exit $?
fi

# ---- 3. libPFO.so ---------------------------------------------------------------------------
say "Building libPFO.so (NDK $NDK_VERSION)"
NATIVE="$WORK/native"
ANDROID_NDK="$ANDROID_SDK/ndk/$NDK_VERSION" PFO_SRC="$SRC/PFO-50/PFO" ENET_SRC="$SRC/enet" OUT="$NATIVE" \
  bash "$ROOT/scripts/build_pfo_android.sh" >/dev/null
ok "arm64-v8a + x86_64"

# ---- 4. the GameMaker Android runner --------------------------------------------------------
say "Building the Android runner with Igor (takes a few minutes)"
GM="$WORK/gm"; rm -rf "$GM/project"; mkdir -p "$GM"
cp -r "$ROOT/android/gm-project" "$GM/project"
for abi in x86_64 arm64-v8a; do mkdir -p "$GM/project/extensions/PFO/AndroidSource/libs/$abi"; cp "$NATIVE/$abi/libPFO.so" "$GM/project/extensions/PFO/AndroidSource/libs/$abi/"; done
U="$GM/user"; rm -rf "$U"; mkdir -p "$U"; cp "$GM_USER_DIR/licence.plist" "$U/"
esc() { win "$1" | sed 's/\\/\\\\/g'; }
# Igor's settings: where the SDK, NDK and JDK are, and a keystore its Gradle build insists on
# (only for the runner; the final APK is signed by scripts/pack_apk.csx). Igor reads the
# passwords as base64, so they must be a multiple of 4 characters from the base64 alphabet.
IGOR_KS="$WORK/keys/igor.jks"; mkdir -p "$WORK/keys"
if [ ! -f "$IGOR_KS" ]; then
  KT_OUT=$("$JAVA_HOME/bin/keytool.exe" -genkeypair -keystore "$(win "$IGOR_KS")" -alias igor -keyalg RSA -keysize 2048 \
    -validity 10000 -storepass ufo50android -keypass ufo50android -dname "CN=runner build" 2>&1) \
    || { printf '%s\n' "$KT_OUT" >&2; die "keytool couldn't make the keystore for the runner build ($(win "$IGOR_KS"))"; }
fi
cat > "$U/local_settings.json" <<EOF
{
  "machine.Platform Settings.Android.Keystore.filename": "$(esc "$IGOR_KS")",
  "machine.Platform Settings.Android.Keystore.alias": "igor",
  "machine.Platform Settings.Android.Keystore.keystore_password": "ufo50android",
  "machine.Platform Settings.Android.Keystore.keystore_alias_password": "ufo50android",
  "machine.Platform Settings.Android.Paths.sdk_location": "$(esc "$ANDROID_SDK")",
  "machine.Platform Settings.Android.Paths.ndk_location": "$(esc "$ANDROID_SDK/ndk/$NDK_VERSION")",
  "machine.Platform Settings.Android.Paths.jdk_location": "$(esc "$JAVA_HOME")",
  "machine.Platform Settings.Android.Packaging.full_install": true,
  "machine.Platform Settings.Android.max_heap_size": 2,
  "machine.Platform Settings.Android.use_debug_runner": false,
  "machine.Platform Settings.Android.runner_path": "$(esc "$GM_RT/android/runner")",
  "version": "1.1"
}
EOF
rm -rf "$GM/cache/Android/Default/com.unofficial.ufo50/build/outputs"
"$GM_RT/bin/igor/windows/x64/Igor.exe" -j=8 --project="$(win "$GM/project/runner.yyp")" --user="$(win "$U")" \
  --runtimePath="$(win "$GM_RT")" --runtime=VM --cache="$(win "$GM/cache")" --temp="$(win "$GM/temp")" \
  --of="$(win "$GM/out.apk")" -- Android Package > "$GM/igor.log" 2>&1 || true
BUILT="$GM/cache/Android/Default/com.unofficial.ufo50/build/outputs/apk/debug/com.unofficial.ufo50-debug.apk"
if [ ! -f "$BUILT" ]; then grep -E 'error:|What went wrong|Exception' -A3 "$GM/igor.log" | head -30 || true; die "runner build failed (see $(win "$GM/igor.log"))"; fi
cp "$BUILT" "$WORK/runner.apk"
ok "runner"

# ---- 5a. the runner kit (the file the quick build uses) -------------------------------------
if [ $KIT = 1 ]; then
  say "Packing the runner kit"
  KITDIR="$WORK/kit"; mkdir -p "$KITDIR"
  KITAPK="$KITDIR/ufo50-online-runner-v$VERSION.apk"
  UTMT="$WORK/utmt/UndertaleModCli.exe"
  [ -x "$UTMT" ] || die "run a build once first (it downloads UndertaleModTool), then --kit"
  ANY_DATA=$(ls "$WORK"/game/data.win 2>/dev/null | head -1 || true)
  [ -n "$ANY_DATA" ] || die "run a build once first (the packer runs inside UndertaleModTool and needs a data file to start)"
  # A kit left from an earlier run must not pass for this one.
  PACKLOG="$KITDIR/pack.log"; rm -f "$KITAPK" "$PACKLOG"
  set +e
  (yes y | head -50) | RUNNER_APK="$(win "$WORK/runner.apk")" GAME_DIR="" OUT_APK="$(win "$KITAPK")" KEY_FILE="$(win "$WORK/keys/kit-key.pfx")" \
    "$UTMT" load "$(win "$ANY_DATA")" -s "$(win "$ROOT/scripts/pack_apk.csx")" > "$PACKLOG" 2>&1
  PACK_RC=${PIPESTATUS[1]}   # the packer's own exit code (the first part of the pipe only answers its prompts)
  set -e
  grep -aE "^APK|rror|xception" "$PACKLOG" || true
  # the same test build.ps1 uses for a failed UndertaleModTool script
  if [ "$PACK_RC" != 0 ] || grep -aqE "Script execution failed|CompilationErrorException" "$PACKLOG" || [ ! -f "$KITAPK" ]; then
    die "kit packing failed (exit code $PACK_RC, see $(win "$PACKLOG"))"
  fi
  echo; echo "Runner kit: $(win "$KITAPK")"; echo "SHA-256:    $(sha256sum "$KITAPK" | cut -d' ' -f1)"
  echo "The quick build does not look in work/kit/. It uses the file in kit/ of this repository, and"
  echo "downloads that file from the version's tag if the one in kit/ is missing or has another checksum."
  echo "To make it use this kit: copy the file to kit/ and put the SHA-256 above in the Kit line"
  echo "of build.ps1 (and the new file name and tag there, if the version changed)."
  exit 0
fi

# ---- 5b. the rest is the quick build, with this runner --------------------------------------
# The command line is built as arrays, so that paths with spaces stay whole.
PS=(powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(win "$ROOT/build.ps1")")
ARGS=(-Runner "$(win "$WORK/runner.apk")")
if [ -n "${MODDED_DIR:-}" ]; then
  MODDED="$(upath "$MODDED_DIR")"
  ARGS+=(-ModdedDir "$(win "${MODDED%/}")")   # for example "...\UFO 50 Modded Copy"
fi
if [ $INSTALL = 1 ]; then ARGS+=(-Install); fi
NOPAUSE=1 "${PS[@]}" "${ARGS[@]}"
