"""A fast rejoin of a device that has several players: two boxed-in Windows test games; the joiner
seats two players (its arrow keys in its own seat, its numpad keys in P3), the host keeps one (P1:
it drives the library). Then the joiner's game is killed and rejoins, in a game for one player
(game 1: only P1 seated) and in a game for several (game 3: all three seated); after each, a save
with the same frame and checksum on both and no desync.

  python seats_rejoin.py [1|3|both]

Ends with PASSED or FAILED and exit code 0 or 1. Uses wingame.py's sandboxes 1 and 2.
"""
import re, sys, time, subprocess
import duo, cross
import wingame as w

check = duo.Checks()
SEATS = r"seats (\d+), extra inputs (\d+)"


def seats(g):
    at = g.mark(); g.cmd("where")
    r = g.wait_log(SEATS, at, 6)
    return (int(r.group(1)), int(r.group(2))) if r else None


def open_page(g):
    for downs in (0, 2, 4):
        at = g.mark()
        g.keys("f1", 1.0)
        for _ in range(downs): g.keys("down", 0.3)
        g.keys("enter", 1.0)
        if g.wait_log(r"menu: WHO'S PLAYING", at, 1.5): return True
        g.keys("esc", 0.5, "esc", 0.5)
    return False


def setup(a, b):
    """the joiner: its numpad player joins and moves to P3, locks in; DONE"""
    check(open_page(b), "the joiner's WHO'S PLAYING opens")
    at = b.mark()
    b.keys("del", 0.5, "num3", 0.4, "num3", 0.4, "del", 0.5)
    lock = b.wait_log(r"players: NUMPAD locked in P(\d)", at, 4)
    check(bool(lock and lock.group(1) == "3"), "the joiner's numpad player locks in P3", lock.group(0) if lock else "")
    b.keys("enter", 1.0)
    time.sleep(2)


def rejoin(a, b, n):
    subprocess.run(["taskkill", "/F", "/PID", str(b.pid)], capture_output=True)
    time.sleep(6)
    pid = w.start(2); time.sleep(16)
    b2 = duo.Game(2, pid)
    at = b2.mark(); b2.keys("f2")
    r = b2.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)", at, 90)
    check(bool(r and "back in the match" in r.group(0)), "game %d: the joiner (two players) rejoins" % n, r.group(0) if r else "no rejoin line")
    duo.check_rejoin_kind(b2, at, check, "game %d: the joiner's rejoin" % n)
    time.sleep(5)
    for k in ("right", "z", "num3", "del", "left"): a.keys(k, 0.3); b2.keys(k, 0.2)
    cross.save_by(a, b2, 5 + n, check, "game %d: a save after the rejoin" % n)
    for g, name in ((a, "host"), (b2, "joiner")):
        bad = [l for l in g.log().splitlines() if duo.BAD.search(l)]
        check(not bad, "game %d: %s: no desync or lost-connection lines" % (n, name), "; ".join(l.strip()[-140:] for l in bad[:2]))
    return b2


def main(which, plain=False):
    a, b = duo.start(before_ready=None if plain else setup)
    sa, sb = seats(a), seats(b)
    want = 2 if plain else 3
    check(bool(sa and sb and sa[0] == want and sb[0] == want), "%d seats on both" % want, "%s / %s" % (sa, sb))
    for n in ([1, 3] if which == "both" else [int(which)]):
        if n == 3:   # back to the library first (pause menu > back to library)
            a.keys("esc", 1.5, "down", 0.4, "down", 0.4, "z", 1.0, "z", 6)
        moves = ["right", 0.6] * (n - 1)
        a.keys(*moves, "z", 8, "z", 3, "z", 5)
        print("game %d:" % n, a.where(), seats(a), seats(b))
        for k in ("right", "z", "num3", "del"): a.keys(k, 0.3); b.keys(k, 0.2)
        b = rejoin(a, b, n)
    return check


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "both"
    try:
        c = main(which, plain="plain" in sys.argv[2:])   # (plain: one player each, for comparison)
    except Exception as e:
        c = duo.Checks(); c.failed.append(str(e)); print("FAIL  %s" % e)
    print("PASSED" if not c.failed else "FAILED: " + "; ".join(c.failed))
    good = w.stop()
    sys.exit(1 if c.failed or not good else 0)
