"""Build a UFO 50 Android APK from a (modded) Windows game folder.

Usage: py -3 scripts/build_apk.py <game_dir> [--out build/ufo50.apk] [--base <wrapper.apk>]

Takes the GameMaker Android runner APK, swaps in data.win as assets/game.droid,
adds every asset file the game needs, then zipaligns and signs the result.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

import os

ROOT = Path(__file__).resolve().parent.parent
# build.sh passes these; the defaults suit a standard Windows setup.
SDK = Path(os.environ.get("ANDROID_SDK") or Path.home() / "AppData/Local/Android/Sdk")
BUILD_TOOLS = SDK / "build-tools" / os.environ.get("BUILD_TOOLS_VERSION", "35.0.0")
JAVA_HOME = Path(os.environ["JAVA_HOME"]) if os.environ.get("JAVA_HOME") else None
DEFAULT_BASE = ROOT / "work/runner.apk"
KEYSTORE = Path(os.environ.get("KEYSTORE") or ROOT / "work/keys/ufo50-android.jks")
KEY_ALIAS = "ufo50"
KEY_PASS = os.environ.get("KEY_PASS", "ufo50android")  # your own local signing key, personal builds only

# Files in the game folder that are Windows-only and not needed on Android.
SKIP = {"ufo50.exe", "steam_api64.dll", "Steamworks_x64.dll", "data.win"}
SKIP_SUFFIXES = {".exe", ".dll", ".pdb", ".log"}


def run(cmd, **kw):
    print(">", " ".join(str(c) for c in cmd[:6]), "..." if len(cmd) > 6 else "")
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def ensure_keystore():
    if KEYSTORE.exists():
        return
    KEYSTORE.parent.mkdir(parents=True, exist_ok=True)
    run([(JAVA_HOME / "bin/keytool.exe") if JAVA_HOME else "keytool", "-genkeypair", "-keystore", KEYSTORE,
         "-alias", KEY_ALIAS, "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
         "-storepass", KEY_PASS, "-keypass", KEY_PASS,
         "-dname", "CN=UFO50 Android Personal Build"])


def stage_assets(game_dir: Path, stage: Path) -> list[str]:
    """Copy the game's runtime files into stage/assets and return their relative paths."""
    if stage.exists():
        shutil.rmtree(stage)
    assets = stage / "assets"
    assets.mkdir(parents=True)
    shutil.copy2(game_dir / "data.win", assets / "game.droid")
    rel = ["assets/game.droid"]
    for src in sorted(game_dir.rglob("*")):
        if src.is_dir():
            continue
        r = src.relative_to(game_dir)
        if r.parts[0].startswith(".") or src.name in SKIP or src.suffix.lower() in SKIP_SUFFIXES:
            continue
        # The Android runner lowercases asset paths when it looks them up.
        lower = r.as_posix().lower()
        dst = assets / lower
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        rel.append("assets/" + lower)
    return rel


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir", type=Path)
    ap.add_argument("--out", type=Path, default=ROOT / "work/ufo50-online.apk")
    ap.add_argument("--base", type=Path, default=DEFAULT_BASE)
    ap.add_argument("--native-dir", type=Path, default=None,
                    help="folder with <abi>/lib*.so files to add under lib/ (e.g. build/pfo-android)")
    a = ap.parse_args()

    if not (a.game_dir / "data.win").exists():
        sys.exit(f"No data.win in {a.game_dir}")
    work = Path(os.environ.get("BUILD_WORK") or ROOT / "work/apk")
    work.mkdir(parents=True, exist_ok=True)
    stage = work / "stage"
    unsigned = work / "unsigned.apk"
    aligned = work / "aligned.apk"

    files = stage_assets(a.game_dir, stage)
    print(f"Staged {len(files)} asset files")
    if a.native_dir:
        for so in sorted(a.native_dir.glob("*/lib*.so")):
            abi = so.parent.name
            dst = stage / "lib" / abi / so.name
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(so, dst)
            files.append(f"lib/{abi}/{so.name}")
            print(f"Added native lib/{abi}/{so.name}")

    # options.ini: keep the runner's Android sections and append the game's own.
    import zipfile
    with zipfile.ZipFile(a.base) as z:
        runner_ini = z.read("assets/options.ini").decode("utf-8", "replace") if "assets/options.ini" in z.namelist() else ""
        base_entries = set(z.namelist())
    game_ini_path = stage / "assets" / "options.ini"
    game_ini = game_ini_path.read_text("utf-8", "replace") if game_ini_path.exists() else ""
    game_ini_path.write_text(runner_ini.rstrip() + "\n" + game_ini, "utf-8")
    if "assets/options.ini" not in files:
        files.append("assets/options.ini")

    shutil.copy2(a.base, unsigned)
    aapt = BUILD_TOOLS / "aapt.exe"
    # Drop anything we're about to replace (placeholder game, options, native libs), then
    # add ours, stored uncompressed (audio is already Ogg, and the runner maps game.droid).
    replaced = [f for f in files if f in base_entries]
    for i in range(0, len(replaced), 60):
        subprocess.run([str(aapt), "remove", str(unsigned), *replaced[i:i + 60]], check=False, capture_output=True)
    for i in range(0, len(files), 60):
        run([aapt, "add", "-0", "", unsigned, *files[i:i + 60]], cwd=stage, stdout=subprocess.DEVNULL)

    run([BUILD_TOOLS / "zipalign.exe", "-p", "-f", "4", unsigned, aligned])
    ensure_keystore()
    a.out.parent.mkdir(parents=True, exist_ok=True)
    env_java = {"JAVA_HOME": str(JAVA_HOME)} if JAVA_HOME else {}
    run([BUILD_TOOLS / "apksigner.bat", "sign", "--ks", KEYSTORE, "--ks-key-alias", KEY_ALIAS,
         "--ks-pass", f"pass:{KEY_PASS}", "--key-pass", f"pass:{KEY_PASS}",
         "--out", a.out, aligned], env={**os.environ, **env_java})
    print(f"Built {a.out} ({a.out.stat().st_size / 1e6:.0f} MB)")


if __name__ == "__main__":
    main()
