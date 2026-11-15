"""Rewind in the Windows version (the Windows twin of tests/rewind_test.py): solo play, Velgress (game
7 in the library), play with a pause, then rewind 15 s and 60 s; each must land on its frame with the
replay's checksums matching. Then twice: play 2.5 minutes, rewind 60 s, and compare the time it took
with its estimate (too slow by more than a quarter is a FAIL: people agree to the wait first).
A Code Error box, or the game closing by itself, anywhere in the run is a FAIL too. Boxed in like
every test game here (wingame.py; WINGAME_DIR names the built folder).

The last line is "rewind: PASS" or "rewind: FAIL"; the exit code is 0 for PASS and 1 for FAIL.

  python rewind.py [box, default 1]
"""
import os, random, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wingame
import sweep

BOX = int(sys.argv[1]) if len(sys.argv) > 1 else 1
DIR = os.path.join(wingame.home(BOX), "ufo50_online")
LOG = os.path.join(DIR, "pfo_online.log")
rnd = random.Random(11)


def cmd(c):
    with open(os.path.join(DIR, "ss_cmd"), "w") as f: f.write(c + "\n")
def size(): return os.path.getsize(LOG) if os.path.exists(LOG) else 0
def since(mark):
    with open(LOG, "rb") as f:
        f.seek(mark)
        return f.read().decode("utf-8", "replace")
def mash(sec):
    t = time.time()
    while time.time() - t < sec: wingame.press(rnd.choice(["left", "right", "z", "x", "right"]), HWND); time.sleep(0.1)
def alive(pid):
    st = wingame.state_of(pid)
    return "'Code Error'" not in st and not st.startswith("the process is gone"), st


ok = True
sweep.setup(BOX)   # its own settings: sound off, in a window (a full-screen game can't be photographed)
pid = wingame.start(BOX)
HWND = None   # its own window (other test games may be open: keys must not go to theirs)
try:
    time.sleep(15)
    HWND = wingame.game_window(pid)
    wingame.keys("z wait:4 z wait:4".split(), HWND)
    cmd("solo"); time.sleep(10)
    wingame.keys(["right"] * 6 + ["z", "wait:9", "z", "wait:2", "z", "wait:2", "z", "wait:2"], HWND)
    mash(30); wingame.keys("esc wait:3 esc wait:1".split(), HWND); mash(45)
    for secs in (15, 60):
        mark = size(); cmd("rewind %d" % secs)
        t = time.time(); res = []
        while time.time() - t < 60:
            time.sleep(2)
            res = re.findall(r"load slot -2: reached frame (\d+) .* (verified|MISMATCH)", since(mark))
            if res: break
        start = re.findall(r"rewind to frame (\d+): (.*)", since(mark))
        good = bool(res) and res[-1][1] == "verified" and bool(start) and start[-1][0] == res[-1][0]
        ok = ok and good
        print("rewind %d s: %s %s %s" % (secs, "PASS" if good else "FAIL", start[-1:], res[-1:]), flush=True)
        mash(8)

    def timed_rewind(secs):
        mark = size(); cmd("rewind %d" % secs)
        t = time.time(); res = []
        while time.time() - t < 300:
            time.sleep(2)
            res = re.findall(r"load slot -2: reached frame (\d+) in (\d+) ms.* (verified|MISMATCH)", since(mark))
            if res: break
        est = re.findall(r"rewind to frame \d+: .*estimate (\d+) ms", since(mark))
        if not res or not est: return None
        return int(est[-1]), int(res[-1][1]), res[-1][2]

    for round_ in (1, 2):
        mash(150)
        r = timed_rewind(60)
        if r is None: print("estimate %d: FAIL (no result)" % round_); ok = False; continue
        est, took, v = r
        good = v == "verified" and took <= est * 1.25
        ok = ok and good
        print("estimate %d: %s estimated %d ms, took %d ms (%.2fx), %s" % (round_, "PASS" if good else "FAIL", est, took, took / max(1, est), v), flush=True)
    fine, st = alive(pid)
    if not fine: print("game error: " + " ".join(st.split())[:200]); ok = False
finally:
    wingame.close(pid)   # only this test's game: others (a sweep) may be running from the same folder
    wingame.check()
print("rewind: " + ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
