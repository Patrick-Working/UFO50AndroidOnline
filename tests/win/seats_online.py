"""Several players on one PC, online: two boxed-in Windows test games in one private lobby; the host
seats two players (WHO'S PLAYING in its menu: the arrow keys in P1, the numpad keys in P2), the
joiner one (P3, without setting anything up). Then: three seats on both; the numpad player's input
reaches both games; a save asked for by the joiner has the same frame and checksum on both; a load
of it, asked for by the host, puts both back on the same frame; the joiner's game killed and back
by a fast rejoin with the seats as they were; a save after it still the same on both.

  python seats_online.py

Ends with PASSED or FAILED and exit code 0 or 1. Uses wingame.py's sandboxes 1 and 2.
"""
import re, sys, time, subprocess
import duo
import wingame as w

check = duo.Checks()
SEATS = r"seats (\d+), extra inputs (\d+)"


def seats(g):
    at = g.mark(); g.cmd("where")
    r = g.wait_log(SEATS, at, 6)
    return (int(r.group(1)), int(r.group(2))) if r else None


def setup(a, b):
    """the host: F1, WHO'S PLAYING..., the numpad player joins and locks in P2, DONE"""
    for downs in (0, 2, 4):
        at = a.mark()
        a.keys("f1", 1.0)
        for _ in range(downs): a.keys("down", 0.3)
        a.keys("enter", 1.0)
        if a.wait_log(r"menu: WHO'S PLAYING", at, 1.5): break
        a.keys("esc", 0.5, "esc", 0.5)
    else:
        raise RuntimeError("the host's menu has no WHO'S PLAYING")
    at = a.mark()
    a.keys("del", 0.5, "del", 0.5)
    lock = a.wait_log(r"players: NUMPAD locked in P(\d)", at, 4)
    check(bool(lock and lock.group(1) == "2"), "the host's numpad player locks in P2", lock.group(0) if lock else "")
    a.keys("enter", 1.0)
    plan = a.wait_log(r"lobby seats: ([\d,-]+)", at, 6)
    check(bool(plan), "the host works out the seats", plan.group(1).count(",") and "8 slots" if plan else "no plan line")
    time.sleep(1.5)


def main():
    m0 = {1: duo.mark(1), 2: duo.mark(2)}   # (this run's lines only: the logs keep earlier runs')
    a, b = duo.start(before_ready=setup)
    a.run0, b.run0 = m0[1], m0[2]
    for g, name in ((a, "host"), (b, "joiner")):
        line = g.wait_log(r"match seats from the lobby: ([^\n]*)", g.run0, 2)
        check(bool(line), "%s: the match takes the lobby's seats" % name, line.group(1) if line else "")
    sa, sb = seats(a), seats(b)
    check(bool(sa and sb and sa[0] == 3 and sb[0] == 3), "three seats on both", "%s / %s" % (sa, sb))
    # the library: in a game for several players
    a.keys("right", 0.6, "right", 0.6, "z", 8, "z", 3, "down", 0.6, "z", 5)
    wa = a.where(); print("in game:", wa)
    before = (seats(a), seats(b))
    for k in ("num3", "num1", "del", "num3", "end", "num1"): a.keys(k, 0.35)
    after = (seats(a), seats(b))
    check(bool(after[0] and after[1] and before[0] and before[1] and after[0][1] > before[0][1] and after[1][1] > before[1][1]),
          "P2's keys reach both games", "%s -> %s" % (before, after))
    # a save asked for by the joiner, a load of it asked for by the host
    import cross
    s1 = cross.save_by(b, a, 1, check, "save by the joiner")
    for k in ("right", "z", "num3", "del", "left"): a.keys(k, 0.3)
    if s1: cross.load_by(a, b, s1[1], s1[2], check, "the host's load")
    time.sleep(3)
    sa, sb = seats(a), seats(b)
    check(bool(sa and sb and sa[0] == 3 and sb[0] == 3), "three seats after the load", "%s / %s" % (sa, sb))
    # the joiner's game killed, back by a fast rejoin
    subprocess.run(["taskkill", "/F", "/PID", str(b.pid)], capture_output=True)
    time.sleep(6)
    b2run0 = duo.mark(2)
    pid = w.start(2); time.sleep(16)
    b2 = duo.Game(2, pid)
    b2.run0 = b2run0
    at = b2.mark(); b2.keys("f2")
    r = b2.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)", at, 90)
    check(bool(r and "back in the match" in r.group(0)), "the joiner's game killed: it rejoins", r.group(0) if r else "no rejoin line")
    duo.check_rejoin_kind(b2, at, check, "the joiner's rejoin")
    time.sleep(4)
    sa, sb = seats(a), seats(b2)
    check(bool(sa and sb and sa[0] == 3 and sb[0] == 3), "three seats after the rejoin", "%s / %s" % (sa, sb))
    for k in ("num3", "z", "num1", "del"): a.keys(k, 0.3)
    cross.save_by(a, b2, 3, check, "save after the rejoin")
    for g, name in ((a, "host"), (b2, "joiner")):
        bad = [l for l in g.log(g.run0).splitlines() if duo.BAD.search(l)]
        check(not bad, "%s: no desync or lost-connection lines" % name, "; ".join(l.strip()[-140:] for l in bad[:2]))
        st = w.state_of(g.pid)
        check("Code Error" not in st, "%s: no error box" % name, " ".join(st.split())[:300])
    return check


if __name__ == "__main__":
    try:
        c = main()
    except Exception as e:
        c = duo.Checks(); c.failed.append(str(e)); print("FAIL  %s" % e)
    print("PASSED" if not c.failed else "FAILED: " + "; ".join(c.failed))
    good = w.stop()
    sys.exit(1 if c.failed or not good else 0)
