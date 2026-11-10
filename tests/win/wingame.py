"""Drive the Windows build for tests: start it boxed in, find its window, take a picture, press keys.

The test game is the built folder only (work/windows/UFO 50 Online), and it never runs as your own game:
  - it is refused if a Steam library or steam_appid.txt is in its folder;
  - it is started with its own user profile folder (work/win-sandbox/<n>), so whatever name the
    game saves under, its files land there and not in the real %LOCALAPPDATA%;
  - the real save folder (%LOCALAPPDATA%/ufo50) is fingerprinted at start and compared by
    "check" and "stop".
Only processes started from that folder are ever looked at, pressed or closed.

  python wingame.py start [n]               start test game n (1 by default) boxed in
  python wingame.py stop                    close the test games, then check the real saves
  python wingame.py check                   compare the real save folder with its fingerprint
  python wingame.py home [n]                the boxed-in %LOCALAPPDATA% of test game n
  python wingame.py shot out.png            picture of the game window with our menu over it
                                            (works when covered)
  python wingame.py keys z z down z         press keys one after another (posted to the window,
                                            so it needn't be in front); wait:0.5 pauses
  python wingame.py title                   the window's title
Keys: up down left right z x a s enter esc space f1..f12 tab del backspace minus, or one letter/digit.
pad:l3+r3 (or pad:a, pad:b, pad:x, pad:up, pad:down) presses buttons of the menu's test controller;
pad:unplug takes it away.
"""
import ctypes, ctypes.wintypes as wt, hashlib, json, os, sys, time, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
# where the build put the game: work/ in the repository (build.sh --windows, build.bat -Windows),
# build/ in the development tree
if os.path.exists(os.path.join(ROOT, "build.ps1")):
    OUTDIR = os.path.join(ROOT, "work"); GAME_DIR = os.path.join(OUTDIR, "windows", "UFO 50 Online")
else:
    OUTDIR = os.path.join(ROOT, "build"); GAME_DIR = os.path.join(OUTDIR, "windows-game")
# (a second copy of the test game, built apart from the first: WINGAME_DIR names its folder)
GAME_DIR = os.environ.get("WINGAME_DIR") or GAME_DIR
SANDBOX = os.path.join(OUTDIR, "win-sandbox")
REAL_SAVES = os.path.join(os.environ.get("LOCALAPPDATA", ""), "ufo50")
STEAM_FILES = ("Steamworks_x64.dll", "steam_api64.dll", "steam_api.dll", "steam_appid.txt")

user32, gdi32, kernel32 = ctypes.windll.user32, ctypes.windll.gdi32, ctypes.windll.kernel32
user32.SetProcessDPIAware()

VK = {"up": 0x26, "down": 0x28, "left": 0x25, "right": 0x27, "enter": 0x0D, "esc": 0x1B, "space": 0x20, "tab": 0x09,
      "shift": 0x10, "ctrl": 0x11, "backspace": 0x08, "del": 0x2E, "minus": 0xBD}
for i in range(1, 13): VK["f%d" % i] = 0x6F + i


def exe_of(pid):
    h = kernel32.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
    if not h: return ""
    buf = ctypes.create_unicode_buffer(1024)
    n = wt.DWORD(1024)
    ok = kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n))
    kernel32.CloseHandle(h)
    return buf.value if ok else ""


def game_pids(name="ufo50.exe"):
    """the test games only: a ufo50.exe started from anywhere else (Steam's) is not ours to touch"""
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq " + name, "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    pids = [int(line.split('","')[1]) for line in out.splitlines() if line.lower().startswith('"' + name)]
    ours = os.path.normcase(os.path.join(GAME_DIR, name))
    return [p for p in pids if os.path.normcase(exe_of(p)) == ours]


def fingerprint(folder=REAL_SAVES):
    out = {}
    for base, _, files in os.walk(folder):
        for f in files:
            path = os.path.join(base, f)
            st = os.stat(path)
            out[os.path.relpath(path, folder)] = [st.st_size, int(st.st_mtime), hashlib.sha1(open(path, "rb").read()).hexdigest()]
    return out


def check():
    """has anything in the real save folder changed since the first boxed-in start?"""
    mark = os.path.join(SANDBOX, "real_saves.json")
    if not os.path.exists(mark): raise SystemExit("no fingerprint yet (start a test game first)")
    was, now = json.load(open(mark)), fingerprint()
    changed = sorted(k for k in set(was) | set(now) if was.get(k) != now.get(k))
    if changed:
        print("REAL SAVE FOLDER CHANGED: " + ", ".join(changed))
        return False
    print("real save folder untouched (%d files)" % len(now))
    return True


def home(n=1):
    return os.path.join(SANDBOX, str(n), "AppData", "Local")


def start(n=1, front=True):
    """front=False: the window opens behind whatever is in front and doesn't take the keyboard"""
    for f in STEAM_FILES:
        if os.path.exists(os.path.join(GAME_DIR, f)): raise SystemExit("refusing to start: %s is in %s" % (f, GAME_DIR))
    profile = os.path.join(SANDBOX, str(n))
    for d in (home(n), os.path.join(profile, "AppData", "Roaming")): os.makedirs(d, exist_ok=True)
    mark = os.path.join(SANDBOX, "real_saves.json")
    if not game_pids(): json.dump(fingerprint(), open(mark, "w"))
    env = dict(os.environ)
    # SHGetFolderPath (what the runner asks) follows USERPROFILE; our own code reads LOCALAPPDATA
    env.update(USERPROFILE=profile, LOCALAPPDATA=home(n), APPDATA=os.path.join(profile, "AppData", "Roaming"))
    for k in [k for k in env if k.lower().startswith("steam")]: del env[k]
    si = subprocess.STARTUPINFO()
    if not front: si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4   # SW_SHOWNOACTIVATE
    p = subprocess.Popen([os.path.join(GAME_DIR, "ufo50.exe")], cwd=GAME_DIR, env=env, startupinfo=si)
    print("started test game %d, pid %d, files in %s" % (n, p.pid, home(n)))
    return p.pid


def close(pid):
    """close one test game"""
    if pid not in game_pids(): return
    for hwnd, _, _ in windows_of([pid], "YYGameMakerYY"): user32.PostMessageW(hwnd, 0x0010, 0, 0)   # WM_CLOSE
    for _ in range(40):
        if pid not in game_pids(): return
        time.sleep(0.25)
    subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
    time.sleep(0.5)


def stop():
    for pid in game_pids():
        for hwnd, _, _ in windows_of([pid], "YYGameMakerYY"): user32.PostMessageW(hwnd, 0x0010, 0, 0)   # WM_CLOSE
    for _ in range(40):
        if not game_pids(): break
        time.sleep(0.25)
    for pid in game_pids(): subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
    return check()


def windows_of(pids, cls=None):
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd, _):
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in pids and user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, buf, 256)
            title = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, title, 256)
            if cls is None or buf.value == cls: found.append((hwnd, buf.value, title.value))
        return True
    user32.EnumWindows(each, 0)
    return found


def state_of(pid):
    """for a test game that stopped answering: is it still running, which windows it has, and what
    any message box of its says (GameMaker shows a game-code error in one)"""
    if pid not in game_pids(): return "the process is gone"
    out = ["the process is running"]
    for hwnd, cls, title in windows_of([pid]):
        out.append("window %s '%s'" % (cls, title))
        if cls != "#32770": continue
        @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        def each(child, _):
            buf = ctypes.create_unicode_buffer(8192)
            got = ctypes.c_size_t()
            user32.SendMessageTimeoutW(child, 0x000D, 8192, buf, 2, 500, ctypes.byref(got))   # WM_GETTEXT, SMTO_ABORTIFHUNG
            if buf.value.strip(): out.append("   " + " ".join(buf.value.split()))
            return True
        user32.EnumChildWindows(hwnd, each, 0)
    return chr(10).join(out)


def game_window(pid=None):
    pids = [pid] if pid else game_pids()
    wins = [w for w in windows_of(pids) if w[1] == "YYGameMakerYY"]
    if not wins: raise SystemExit("no test game window (start one with: wingame.py start)")
    return wins[0][0]


def grab(hwnd, flags=3):
    from PIL import Image
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = r.right, r.bottom
    dc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(dc)
    bmp = gdi32.CreateCompatibleBitmap(dc, w, h)
    gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, flags)   # 3: PW_CLIENTONLY | PW_RENDERFULLCONTENT (what the game needs)
    class BMI(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD), ("biBitCount", wt.WORD),
                    ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD), ("a", wt.LONG), ("b", wt.LONG), ("c", wt.DWORD), ("d", wt.DWORD)]
    bmi = BMI(ctypes.sizeof(BMI), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bmi), 0)
    gdi32.DeleteObject(bmp); gdi32.DeleteDC(mem); user32.ReleaseDC(hwnd, dc)
    return Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")


def shot(path, hwnd=None):
    """the game's picture with our menu window (class UFO50OnlineMenu) laid over it, as the screen shows them"""
    from PIL import Image, ImageChops
    hwnd = hwnd or game_window()
    img = grab(hwnd)
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    for panel, _, _ in windows_of([pid.value], "UFO50OnlineMenu"):
        # magenta is the panel's see-through colour. Asked for plainly (PW_CLIENTONLY alone) the panel
        # gives what it drew, magenta included; "as shown" would give those pixels as black, which a
        # save's picture has too
        over = grab(panel, 1)
        if over.size != img.size: continue
        mask = ImageChops.difference(over, Image.new("RGB", over.size, (255, 0, 255))).convert("L").point(lambda v: 255 if v else 0)
        img.paste(over, (0, 0), mask)
    img.save(path)
    return img.size


def press(key, hwnd=None, hold=0.06):
    hwnd = hwnd or game_window()
    vk = VK.get(key.lower()) or ord(key.upper())
    scan = user32.MapVirtualKeyW(vk, 0)
    user32.PostMessageW(hwnd, 0x0100, vk, 1 | (scan << 16))
    time.sleep(hold)
    user32.PostMessageW(hwnd, 0x0101, vk, 1 | (scan << 16) | (3 << 30))
    time.sleep(0.12)


# the menu's test controller: a registered message the build takes as a controller's buttons
PAD = {"up": 0x0001, "down": 0x0002, "l3": 0x0040, "r3": 0x0080, "a": 0x1000, "b": 0x2000, "x": 0x4000}


def pad(buttons, hwnd=None, hold=0.1):
    """press these controller buttons together (names: up down l3 r3 a b x) and let go;
    pad("unplug") takes the test controller away again"""
    hwnd = hwnd or game_window()
    msg = user32.RegisterWindowMessageW("UFO50OnlineMenuPad")
    if buttons == "unplug": user32.PostMessageW(hwnd, msg, 0, 1); time.sleep(0.2); return
    bits = 0
    for b in buttons.replace("+", " ").split(): bits |= PAD[b.lower()]
    user32.PostMessageW(hwnd, msg, bits, 0)
    time.sleep(hold)
    user32.PostMessageW(hwnd, msg, 0, 0)
    time.sleep(0.15)


def keys(seq, hwnd=None):
    hwnd = hwnd or game_window()
    for k in seq:
        if k.startswith("wait:"): time.sleep(float(k[5:]))
        elif k.startswith("pad:"): pad(k[4:], hwnd)
        else: press(k, hwnd)


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "shot": print(shot(sys.argv[2]))
    elif cmd == "keys": keys(sys.argv[3:] if sys.argv[2].isdigit() else sys.argv[2:], game_window(int(sys.argv[2])) if sys.argv[2].isdigit() else None)
    elif cmd == "start": start(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    elif cmd == "stop": sys.exit(0 if stop() else 1)
    elif cmd == "check": sys.exit(0 if check() else 1)
    elif cmd == "home": print(home(int(sys.argv[2]) if len(sys.argv) > 2 else 1))
    elif cmd == "pids": print(game_pids())
    elif cmd == "title":
        for w in windows_of(game_pids()): print(w)
    else: print(__doc__)
