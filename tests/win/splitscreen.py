"""Save states across MultiMadnessMod's split screen. In Onion Delivery (library 23) with four
players on this PC (solo play), P1 drives away from the others until the screen splits into four;
then: a save while split, driving back until it's one screen again, a load of that save (the load's
own check compares three pictures of the next frames with the ones taken at the save: a load that
left the screen unsplit, or half the size, fails it); and the other way round, a save while one
screen and a load while split. After each load the screen handler's split flag must be what it was
at the save, and the game must show no error box.

  python splitscreen.py [game number, default 23]

Needs a build with MultiMadnessMod 1.30 or later. Ends with PASSED or FAILED. Uses test game 13.
"""
import os, re, sys, time, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wingame
from sweep import Game, ID

GAME = int(sys.argv[1]) if len(sys.argv) > 1 else 23
failed = []


def check(ok, what, detail=""):
    print(("ok    " if ok else "FAIL  ") + what + (" (%s)" % detail if detail else ""), flush=True)
    if not ok: failed.append(what)


def split_on(g):
    """the screen handler's flag, from a one-frame trace of the whole state"""
    tdir = os.path.join(g.dir, "trace", "split")
    open(os.path.join(g.dir, "tracetag"), "w").write("split")
    # (files are named by frame, and a load goes back to an earlier one: start from an empty folder)
    shutil.rmtree(tdir, ignore_errors=True)
    g.cmd("trace 1")
    t = time.time()
    while time.time() - t < 12:
        time.sleep(0.4)
        new = os.listdir(tdir) if os.path.isdir(tdir) else []
        if new:
            time.sleep(0.5)
            txt = open(os.path.join(tdir, sorted(new)[-1]), encoding="utf-8", errors="replace").read()
            m = re.search(r"oScreenHandler\.splitScreenOn\t\w+\t(\w+) (\S+)", txt)
            print("   (frame %s: splitScreenOn %s)" % (sorted(new)[-1][:-4], m.group(1) + " " + m.group(2) if m else "not in the trace"), flush=True)
            return None if not m else m.group(2) in ("1", "true", "1.00")
    print("   (no trace came)", flush=True)
    return None


def drive(g, key, seconds):
    t = time.time()
    while time.time() - t < seconds: wingame.press(key, g.hwnd, hold=0.5)


def until_split(g, want, key, limit=40):
    t = time.time()
    while time.time() - t < limit:
        drive(g, key, 3)
        s = split_on(g)
        if s == want: return True
    return False


def save_and_ready(g, slot):
    g.mark(); g.cmd("save %d" % slot)
    t = time.time()
    while time.time() - t < 25:
        time.sleep(0.5)
        if "snapshot %d: ready" % slot in g.log(): return True
    return False


def load(g, slot):
    g.mark(); g.cmd("load %d" % slot)
    t = time.time()
    while time.time() - t < 60:
        time.sleep(0.5)
        o = g.log()
        if "from its snapshot in" in o and "verified" in o:
            m = re.search(r"(\d+) pictures: verified", o)
            return "verified, %s pictures" % (m.group(1) if m else "0")
        m = re.search(r"snapshot didn't check out \(([^)]*)\)", o)
        if m: return "FAILED: " + m.group(1)
        if re.search(r"load slot %d: (refused|no such save)" % slot, o): return "refused"
    return "no result"


def main():
    out = open(os.path.join(wingame.OUTDIR, "splitscreen.txt"), "a", encoding="utf-8")
    g = Game(13, out)
    try:
        check(g.start() and g.wait_ready(), "the test game starts")
        check(g.to_library(), "the library")
        g.cmd("solo 4"); time.sleep(3)
        check(g.to_library(20), "solo play with four players")
        # the game for four: its title's 4 PLAYERS, then MultiMadnessMod's options, START at the bottom
        # (four players start apart: the screen is split from the first frame)
        for _ in range((GAME - 1) // 10): g.key("down", 0.3)
        for _ in range((GAME - 1) % 10): g.key("right", 0.3)
        time.sleep(1); g.key("z", 9)
        for _ in range(3): g.key("down", 0.3)
        g.key("z", 2.5)
        for _ in range(7): g.key("down", 0.3)
        g.key("z", 4)
        gw, _ = g.where()
        check(gw == ID[GAME], "in game %d" % GAME, str(gw))
        drive(g, "right", 4)
        check(split_on(g) is True, "four players: the screen is split")
        check(save_and_ready(g, 1), "save 1 while split")
        # back in the library: one screen; a save there
        g.leave_game(); time.sleep(2)
        check(split_on(g) is False, "one screen in the library")
        check(save_and_ready(g, 2), "save 2 in the library")
        for n in (1, 2):
            # one screen now, a save while split: split after the load
            r = load(g, 1)
            check(r.startswith("verified"), "load 1 (split at the save, one screen now)", r)
            time.sleep(1)
            check(split_on(g) is True, "split after the load")
            check(not g.game_error(), "no error box", g.game_error())
            drive(g, "left", 3)
            # split now, a save with one screen: one screen after the load
            r = load(g, 2)
            check(r.startswith("verified"), "load 2 (one screen at the save, split now)", r)
            time.sleep(1)
            check(split_on(g) is False, "one screen after the load")
            check(not g.game_error(), "no error box", g.game_error())
    finally:
        g.stop()
    print("PASSED" if not failed else "FAILED: " + "; ".join(failed))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
