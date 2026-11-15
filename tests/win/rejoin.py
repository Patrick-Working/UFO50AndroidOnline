"""Rejoins in a match of two Windows test games (wingame.py's sandboxes 1 and 2), each way.

  python rejoin.py [library|game]     the match in the online library, or in a game for two

Starts both games in one private lobby (duo.py start), plays a little, then kills the joiner's game,
starts it again and rejoins (F2), then the same with the host's game. Each rejoin must take the
match as it is (a fast rejoin: "rejoin: the game put back at frame R, N ms after asking") unless
REJOIN_REPLAY=1 (the rejoining game asks for the replay), be back in the match (RELAY=1: all of it
through the room server's relay), and the two must
then be together: the same game, frames within a step of each other, and a save asked for after it
made on the same frame with the same checksum on both (PFO's checksum covers the session and the
game's state). No desync lines, no "Code Error" box. Ends with PASSED or FAILED (exit code 0 / 1).
"""
import os, sys, time, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wingame as w
import duo

WHERE = sys.argv[1] if len(sys.argv) > 1 else "library"
# RELAY=1: the joiner skips the direct addresses (files/forcerelay), so the match and both rejoins
# go through the room server's relay
RELAY = os.environ.get("RELAY") == "1"


def rejoin(g, other, check, who):
    """kill game g, start it again, F2: the rejoin and the checks after it; the restarted Game"""
    n = g.n
    mo, mg = other.mark(), g.mark()   # (the log goes on in the same file after the restart)
    subprocess.run(["taskkill", "/F", "/PID", str(g.pid)], capture_output=True)
    time.sleep(6)
    rr = os.path.join(duo.folder(n), "rejoinreplay")
    if duo.REJOIN_REPLAY: open(rr, "w").close()
    elif os.path.exists(rr): os.remove(rr)
    pid = w.start(n)
    pids = [other.pid, pid] if n == 2 else [pid, other.pid]
    open(os.path.join(duo.OUT, "pids.txt"), "w").write("%d %d" % tuple(pids))
    time.sleep(16)
    g = duo.Game(n, pid)
    check(bool(g.wait_log(r"a match can be rejoined", mg, 10)), who + ": started again, it offers the match")
    mg = g.mark()
    g.keys("f2")
    r = g.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)[^\n]*", mg, 90)
    check(bool(r and "back in the match" in r.group(0)), who + ": F2, it rejoins", r.group(0) if r else "no rejoin line")
    duo.check_rejoin_kind(g, mg, check, who, duo.SNAPS and not duo.REJOIN_REPLAY and not RELAY)
    if RELAY: check(bool(other.wait_log(r"is reached through the relay", mo, 2)), who + ": the other game saw it come through the relay")
    sent = other.wait_log(r"rejoin: client \d asked for the match: sent (\d+) bytes[^\n]*", mo, 2)
    if sent: print("note  %s: the other game sent %s bytes%s" % (who, sent.group(1), " (the game as it is and its replay)" if "as it is" in sent.group(0) else ""))
    time.sleep(4)
    return g


def together(a, b, check, what):
    wa, wb = duo.both_where(a, b)
    check(bool(wa and wb and wa[0] == wb[0] and wa[2] == 2 and wb[2] == 2 and abs(wa[1] - wb[1]) <= duo.FRAME_TOL), what + ": together", "%s / %s" % (wa, wb))
    ma, mb = a.mark(), b.mark()
    a.cmd("save 1")
    a.wait_log(r"online save \d+: slot", ma, 8); b.wait_log(r"online save \d+: slot", mb, 8)
    time.sleep(0.5)
    sa, sb = duo.save_lines(a, ma), duo.save_lines(b, mb)
    check(len(sa) == 1 and len(sb) == 1 and sa[0][2:] == sb[0][2:] and bool(sa[0][3]), what + ": a save, the same frame and checksum on both", "%s / %s" % (sa, sb))


def test(a, b):
    check = duo.Checks()
    s0 = {1: a.mark(), 2: b.mark()}
    lib = a.where()
    if lib and lib[0] != 0: a.keys("up", 1.5)
    if WHERE == "game":
        a.keys("right", 0.5, "right", 0.5, "z", 8, "z", 3, "down", 0.4, "z", 5)   # the third in the library: 2 PLAYERS
        duo.play(a, b)
    wa, wb = duo.both_where(a, b)
    check(bool(wa and wb and wa[0] == wb[0] and (wa[0] > 0) == (WHERE == "game")), "both in the " + WHERE, "%s / %s" % (wa, wb))
    a.keys("right", 0.5, "left", 0.5, "down", 0.5, "up", 6)
    b = rejoin(b, a, check, "the joiner's game killed")
    together(a, b, check, "after the joiner's rejoin")
    a.keys("right", 0.5, "left", 0.5, 4)
    a = rejoin(a, b, check, "the host's game killed")
    together(a, b, check, "after the host's rejoin")
    for g in (a, b):
        bad = [l for l in g.log(s0[g.n]).splitlines() if duo.BAD.search(l)]
        check(not bad, "game %d: no desync or lost-connection lines" % g.n, "; ".join(bad[:3]))
        st = w.state_of(g.pid)
        check("Code Error" not in st, "game %d: no error box" % g.n, " ".join(st.split())[:400])
    return check


if __name__ == "__main__":
    fr = os.path.join(duo.folder(2), "forcerelay")
    if RELAY: open(fr, "w").close()
    elif os.path.exists(fr): os.remove(fr)
    try:
        c = test(*duo.start())
    except Exception as e:
        c = duo.Checks()
        c.failed.append(str(e))
    print("PASSED" if not c.failed else "FAILED: " + "; ".join(c.failed))
    w.stop()
    w.check()
    if os.path.exists(fr): os.remove(fr)
    sys.exit(1 if c.failed else 0)
