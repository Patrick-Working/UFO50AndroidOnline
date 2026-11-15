"""A save made in solo play, loaded after DISCONNECT (the library's menu), in the Windows version.

The snapshot of a save is the running session's state; after DISCONNECT PFO's side is shut down,
and loading the snapshot then asserted in pfo_set_players (a Code Error box). A load outside solo
play must replay the save's recording instead, verified, with no error box. Boxed in like every
test game here (wingame.py; WINGAME_DIR names the built folder).

Prints PASS/FAIL lines and "disconnect: PASS" or "disconnect: FAIL" (exit code 0 / 1).

  python disconnect.py [box, default 1]
"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wingame
import sweep

BOX = int(sys.argv[1]) if len(sys.argv) > 1 else 1
DIR = os.path.join(wingame.home(BOX), "ufo50_online")
LOG = os.path.join(DIR, "pfo_online.log")
fails = []


def check(ok, what):
    print(("PASS " if ok else "FAIL ") + what, flush=True)
    if not ok: fails.append(what)
def cmd(c):
    with open(os.path.join(DIR, "ss_cmd"), "w") as f: f.write(c + "\n")
def size(): return os.path.getsize(LOG) if os.path.exists(LOG) else 0
def since(mark):
    with open(LOG, "rb") as f:
        f.seek(mark)
        return f.read().decode("utf-8", "replace")
def wait_for(pattern, mark, timeout):
    t = time.time()
    while time.time() - t < timeout:
        m = re.search(pattern, since(mark))
        if m: return m
        time.sleep(0.5)
    return None


sweep.setup(BOX)
pid = wingame.start(BOX)
try:
    time.sleep(15)
    h = wingame.game_window(pid)
    wingame.keys("z wait:4 z wait:4".split(), h)
    cmd("solo"); time.sleep(10)
    wingame.keys("z wait:5 z wait:6 z wait:6".split(), h)
    mark = size(); cmd("save 1")
    check(wait_for(r"save slot 1: frame", mark, 10) is not None, "save 1 in solo play")
    wait_for(r"snapshot 1: ready", mark, 10)
    # pause menu > Back to Library > yes; the library's menu > Disconnect > yes
    wingame.keys("esc wait:1.2 down wait:0.3 down wait:0.3 z wait:1 z wait:7".split(), h)
    wingame.keys("esc wait:1.5 down wait:0.3 down wait:0.3 z wait:1.5 z wait:6".split(), h)
    mark = size(); cmd("where")
    m = wait_for(r"where: game (-?\d+), frame (\d+), clients (\d+)", mark, 6)
    check(m is not None and m.group(3) == "0", "disconnected (no session)" + (" (%s players)" % m.group(3) if m else ""))
    mark = size(); cmd("load 1")
    check(wait_for(r"load slot 1: not in solo play, so by replay", mark, 10) is not None, "the load replays (not the snapshot)")
    check(wait_for(r"load slot 1: reached frame \d+[^\n]*verified", mark, 60) is not None, "the replay reached the save, verified")
    time.sleep(2)
    st = wingame.state_of(pid)
    check("'Code Error'" not in st and not st.startswith("the process is gone"), "no Code Error box, the game still running")
finally:
    wingame.close(pid)
    wingame.check()
print("disconnect: " + ("PASS" if not fails else "FAIL"), flush=True)
sys.exit(1 if fails else 0)
