"""Screen-aware navigation for testing online play on two BlueStacks instances.

Usage: py -3 scripts/nav.py session [host_port] [join_port]
Waits for each screen by checking a few pixels before pressing keys, so it doesn't
depend on how long intros and fades take. Needs tools/rendezvous_server.py running.
"""
import io
import subprocess
import os
import sys
import time
from pathlib import Path

from PIL import Image

ADB = os.environ.get("ADB") or str(Path.home() / "AppData/Local/Android/Sdk/platform-tools/adb.exe")   # or set ADB
PKG = "com.unofficial.ufo50"


def adb(port, *args, capture=False):
    cmd = [ADB, "-s", f"127.0.0.1:{port}", *args]
    if capture:
        return subprocess.run(cmd, capture_output=True).stdout
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def shot(port):
    return Image.open(io.BytesIO(adb(port, "exec-out", "screencap", "-p", capture=True))).convert("RGB")


def key(port, name):
    adb(port, "shell", "input", "keyevent", f"KEYCODE_{name}")
    time.sleep(0.5)


def white(p): return p[0] > 220 and p[1] > 220 and p[2] > 220
def yellow(p): return p[0] > 200 and p[1] > 200 and p[2] < 120


# Screens are recognised by comparing a region with a reference crop (scripts/nav_refs).
from PIL import ImageChops, ImageStat
REFS = {}
BOXES = {
    'lobby_list': (480, 40, 1440, 160), 'list_button': (1460, 225, 1790, 500), 'in_lobby': (1460, 225, 1790, 500),
    'profiles': (240, 40, 1200, 160), 'boot': (360, 915, 740, 1040), 'online_sel': (1480, 560, 1650, 760), 'title': (700, 780, 1220, 930),
}

# The reference crops were taken with the game filling the 1920x1080 screen (5x). At a smaller
# Video Settings scale (NAV_SCALE=4) the same picture is centred and 4/5 the size, so map each
# box into it and blow the crop back up before comparing.
SCALE = float(os.environ.get("NAV_SCALE", "5"))
SHOTS = os.environ.get("NAV_SHOTS")   # folder to save showcase screenshots of each step


def game_box(box):
    k = SCALE / 5.0
    ox, oy = (1920 - 1920 * k) / 2, (1080 - 1080 * k) / 2
    return tuple(int(round(v * k + (ox if i % 2 == 0 else oy))) for i, v in enumerate(box))


def crop(im, box):
    c = im.crop(game_box(box))
    return c if SCALE == 5 else c.resize((box[2] - box[0], box[3] - box[1]), Image.NEAREST)


def matches(im, name, tol=8):
    if name not in REFS:
        REFS[name] = Image.open(Path(__file__).parent / 'nav_refs' / (name + '.png')).convert('RGB')
    diff = ImageChops.difference(crop(im, BOXES[name]), REFS[name])
    return sum(ImageStat.Stat(diff).mean) / 3 < tol

def red_count(im, box):
    im = crop(im, box)
    return sum(1 for x in range(0, im.width, 6) for y in range(0, im.height, 6) if (lambda p: p[0] > 150 and p[1] < 80 and p[2] < 80)(im.getpixel((x, y))))

def is_boot(im): return matches(im, 'boot', 12) and red_count(im, BOXES['boot']) > 20
def is_title(im): return matches(im, 'title')
def is_profiles(im): return matches(im, 'profiles')
def online_selected(im): return matches(im, 'online_sel')
def is_lobby_list(im): return matches(im, 'lobby_list') and not matches(im, 'in_lobby')
def in_lobby(im): return matches(im, 'in_lobby')

def has_lobby_entry(im):   # the list row's white border appears on the left
    row = crop(im, (170, 228, 1320, 232))
    return sum(white(row.getpixel((x, 2))) for x in range(0, row.width, 10)) > 80


def wait_for(port, test, what, timeout=60, nudge=None, nudge_every=12):
    end = time.time() + timeout
    next_nudge = time.time() + nudge_every
    while time.time() < end:
        im = shot(port)
        if test(im):
            return im
        if nudge and time.time() >= next_nudge:  # e.g. skip an intro, but not too often
            key(port, nudge)
            next_nudge = time.time() + nudge_every
        time.sleep(0.7)
    shot(port).save(f"build/nav_fail_{port}.png")
    raise SystemExit(f"[{port}] timed out waiting for {what} (see build/nav_fail_{port}.png)")


def to_lobby_list(port):
    adb(port, "shell", "am", "force-stop", PKG)
    adb(port, "logcat", "-c")
    adb(port, "shell", "monkey", "-p", PKG, "-c", "android.intent.category.LAUNCHER", "1")
    time.sleep(12)
    # boot screen -> Z, intro -> Escape, title -> Z, until the profile screen shows
    end = time.time() + 120
    last_esc = 0
    while True:
        im = shot(port)
        if is_profiles(im):
            break
        if time.time() > end:
            im.save(f"build/nav_fail_{port}.png")
            raise SystemExit(f"[{port}] never reached the profile screen")
        if is_boot(im) or is_title(im):
            key(port, "Z")
            time.sleep(2.5)
        elif time.time() - last_esc > 6:
            key(port, "ESCAPE")
            last_esc = time.time()
        time.sleep(0.7)
    for _ in range(5):
        if online_selected(shot(port)):
            break
        key(port, "DPAD_RIGHT")
    wait_for(port, online_selected, "PLAY ONLINE selected", 10)
    key(port, "Z")
    wait_for(port, is_lobby_list, "lobby list", 30)
    print(f"[{port}] at lobby list", flush=True)


def session(host, join):
    import threading
    errors = []
    def run(p):
        try:
            to_lobby_list(p)
        except BaseException as e:  # SystemExit from wait_for
            errors.append(str(e))
    t = [threading.Thread(target=run, args=(p,)) for p in (host, join)]
    [x.start() for x in t]
    [x.join() for x in t]
    if errors:
        raise SystemExit("; ".join(errors))
    time.sleep(3)  # the first lobby search ignores input until it finishes
    for _ in range(4):  # CREATE PUBLIC LOBBY
        key(host, "Z")
        try:
            wait_for(host, in_lobby, "host lobby", 6)
            break
        except SystemExit:
            pass
    wait_for(host, in_lobby, "host lobby", 5)
    print("host lobby created", flush=True)
    if SHOTS: shot(host).save(f"{SHOTS}/11_online_host_lobby.png")
    wait_for(join, has_lobby_entry, "lobby in joiner's list", 40)
    if SHOTS: time.sleep(5); shot(join).save(f"{SHOTS}/12_online_lobby_browser.png")
    key(join, "DPAD_LEFT")
    key(join, "Z")
    wait_for(join, in_lobby, "joined lobby", 20)
    print("joined", flush=True)
    time.sleep(2)
    if SHOTS: shot(join).save(f"{SHOTS}/13_online_joined_lobby_p2.png"); shot(host).save(f"{SHOTS}/13_online_joined_lobby_p1.png")
    key(join, "Z")   # READY
    key(host, "Z")   # READY -> session starts
    time.sleep(10)
    shot(host).save("build/s_h.png")
    shot(join).save("build/s_j.png")
    print("session started (check build/s_h.png, build/s_j.png)", flush=True)


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "session"
    h = sys.argv[2] if len(sys.argv) > 2 else "5555"
    j = sys.argv[3] if len(sys.argv) > 3 else "5565"
    if cmd == "session":
        session(h, j)
