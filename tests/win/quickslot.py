"""Quick save and quick load in the Windows version, by keyboard and by controller, and a load's
picture check, in Barbuta (game 1 in the library, 40 inside the game). Boxed in like every test game
here (wingame.py; WINGAME_DIR names the built folder):

  - F5 before solo play is refused, with the message that says how to start it
  - F5 in solo play saves the QUICK slot; walking right makes the loose block fall; F9 loads the
    quick save from its snapshot with its pictures compared, and the block is gone again (the bug
    that tiles drawn after a save stayed on screen)
  - with the controller alone (both sticks in, down to QUICK LOAD (F9), A) the quick save loads
  - the game shows no Code Error box and the real save folder is untouched

Prints one PASS/FAIL line per check, then "quickslot: PASS" or "quickslot: FAIL" (exit code 0 / 1).

  python quickslot.py [box, default 1]
"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wingame
import sweep
from PIL import ImageChops

BOX = int(sys.argv[1]) if len(sys.argv) > 1 else 1
DIR = os.path.join(wingame.home(BOX), "ufo50_online")
LOG = os.path.join(DIR, "pfo_online.log")
fails = []


def check(ok, what):
    print(("PASS " if ok else "FAIL ") + what, flush=True)
    if not ok: fails.append(what)
    return ok


mark = 0
def since():
    with open(LOG, "rb") as f:
        f.seek(mark)
        return f.read().decode("utf-8", "replace")
def new_mark():
    global mark
    mark = os.path.getsize(LOG) if os.path.exists(LOG) else 0
def wait_for(pattern, timeout=20):
    t = time.time()
    while time.time() - t < timeout:
        m = re.search(pattern, since())
        if m: return m
        time.sleep(0.5)
    return None
def cmd(c):
    with open(os.path.join(DIR, "ss_cmd"), "w") as f: f.write(c + "\n")
def where():
    for _ in range(5):
        new_mark(); cmd("where")
        m = wait_for(r"where: game (-?\d+), frame (\d+)", 4)
        if m: return int(m.group(1))
    return None
def area():
    path = os.path.join(os.environ.get("TEMP", "."), "quickslot_%d.png" % BOX)
    wingame.shot(path, HWND)
    from PIL import Image
    im = Image.open(path).convert("RGB")
    w, h = im.size
    return im.crop((int(w * 0.30), int(h * 0.42), int(w * 0.68), int(h * 0.80)))   # around the arch
def changed(a, b):
    d = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v > 24 else 0)
    return d.histogram()[255] / float(a.width * a.height)


os.makedirs(os.path.join(DIR, "savestates"), exist_ok=True)
for ext in (".pfostate", ".json", ".png"):
    try: os.remove(os.path.join(DIR, "savestates", "slot9999" + ext))
    except OSError: pass
new_mark()
sweep.setup(BOX)   # its own settings: sound off, in a window (a full-screen game can't be photographed)
pid = wingame.start(BOX)
HWND = None   # its own window (other test games may be open: keys must not go to theirs)
try:
    time.sleep(15)
    HWND = wingame.game_window(pid)
    wingame.keys("z wait:4 z wait:4".split(), HWND)   # intro, title: the library
    new_mark()
    wingame.keys(["f5", "wait:1"], HWND)
    check(wait_for(r"message: START SOLO PLAY TO SAVE", 5) is not None, "F5 before solo play: refused, saying to start solo play")
    cmd("solo"); time.sleep(10)
    check(where() == 0, "solo play starts in the library")
    wingame.keys("z wait:5 z wait:6 z wait:6".split(), HWND)
    check(where() == 40, "in Barbuta (game 40)")

    new_mark()
    wingame.keys(["f5", "wait:2"], HWND)
    check(wait_for(r"save slot 9999: frame", 10) is not None, "F5: the quick slot saved")
    check(wait_for(r"message: QUICK SAVED", 5) is not None, "F5: QUICK SAVED shown")
    wait_for(r"snapshot 9999: ready", 10)
    saved = area()
    for _ in range(4): wingame.press("right", HWND, hold=0.8)   # held, not tapped: a busy PC can miss short taps
    time.sleep(1)
    after = area()
    # (a window photographed while the PC is busy can come back blank: then the pictures are no help,
    # and the load's own picture check below is what tells)
    blank = saved.getextrema() == after.getextrema() and all(lo == hi for lo, hi in saved.getextrema())
    fell = not blank and changed(saved, after) > 0.003
    if blank: print("SKIP walking right changed the room: the window couldn't be photographed", flush=True)
    else: check(fell, "walking right changed the room (the loose block fell)")
    new_mark()
    wingame.keys(["f9", "wait:4"], HWND)
    m = wait_for(r"load slot 9999: from its snapshot in (\d+) ms, \d+ frames checked, (\d+) pictures: verified", 20)
    check(m is not None and int(m.group(2)) >= 1, "F9: the quick save loaded from its snapshot, pictures compared" + (" (%s)" % m.group(2) if m else ""))
    check(wait_for(r"message: QUICK LOADED", 5) is not None, "F9: QUICK LOADED shown")
    time.sleep(1)
    if fell: check(changed(saved, area()) < 0.01, "after the load the room is as it was saved (no block left drawn)")

    # the controller alone: both sticks in, down to QUICK LOAD (F9), A
    new_mark()
    wingame.keys(["pad:l3+r3", "wait:1"], HWND)
    check(wait_for(r"menu: open", 5) is not None, "both sticks pressed in open the menu")
    for _ in range(3): wingame.keys(["pad:down", "wait:0.3"], HWND)
    wingame.keys(["pad:a", "wait:3"], HWND)
    check(wait_for(r"menu: QUICK LOAD \(F9\)", 5) is not None, "the menu's QUICK LOAD (F9), chosen with A")
    check(wait_for(r"load slot 9999: from its snapshot in \d+ ms, \d+ frames checked, \d+ pictures: verified", 20) is not None, "...loads the quick save (snapshot, verified)")

    st = wingame.state_of(pid)
    check("'Code Error'" not in st and not st.startswith("the process is gone"), "no Code Error box, the game still running")
finally:
    wingame.close(pid)   # only this test's game: others (a sweep) may be running from the same folder
    wingame.check()
print("quickslot: " + ("PASS" if not fails else "FAIL"), flush=True)
sys.exit(1 if fails else 0)
