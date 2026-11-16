"""A player who drops while the other has the game's pause menu open keeps their seat.

The game's pause menu, online, removes the seats of players that aren't connected (a command the
whole match runs). In UFO 50 a dropped player was gone for good; here they come back (the wait,
a rejoin), and came back to find their seat gone: their controls did nothing, the other device's
did. libPFO reports a player it's still waiting for as connected.

  python pauseseat.py [port]     the Windows game hosts, the emulator (default 5565) joins

Both into a game for two; the emulator's presses must move P2 (input traces on both). The Windows
game pauses; the emulator's app is killed, started again and rejoins; the Windows game unpauses.
Then the emulator's presses must still move P2, and the two must be together. PASSED / FAILED.
"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wingame as w
import duo
import cross

PORT = sys.argv[1] if len(sys.argv) > 1 else "5565"


def moves(g, since, player):
    """the input changes the trace shows for a player since the mark"""
    return re.findall(r"PFOINPUT: frame=\d+ player=%d input=([0-9a-f]+)" % player, g.log(since))


def drives_p2(host, d, check, what):
    mh = host.mark()
    for _ in range(4): d.keys("right", 0.4, "left", 0.4)
    time.sleep(2)
    ins = [x for x in moves(host, mh, 1) if x != "0"]
    check(len(ins) >= 2, what + ": the emulator's presses move P2 (seen on the Windows game)", "%d input changes" % len(ins))


def drives_p1(g, d, check, what):
    # (the trace is the same on both devices; the emulator's log as read here has PFOSTATE only)
    mg = g.mark()
    for _ in range(4): g.keys("right", 0.4, "left", 0.4)
    time.sleep(2)
    ins = [x for x in moves(g, mg, 0) if x != "0"]
    check(len(ins) >= 2, what + ": the Windows game's presses move P1", "%d input changes" % len(ins))
    stray = [x for x in moves(g, mg, 1) if x != "0"]
    check(not stray, what + ": and not P2", "%d P2 input changes" % len(stray))


def test(host, joiner, g, d):
    check = duo.Checks()
    if (host.where() or (0,))[0] != 0: host.keys("up", 1.5)
    host.keys("right", 0.6, "right", 0.6, "z", 8, "z", 3, "down", 0.6, "z", 5)   # the third game: 2 PLAYERS
    cross.play(host, joiner)
    cross.together(host, joiner, check, "both in the same game", game=1)
    drives_p2(host, d, check, "before")
    drives_p1(g, d, check, "before")
    # the emulator pauses and makes the pause's auto save (as SAVE ON PAUSE does), then unpauses
    mh, md = host.mark(), d.mark()
    d.keys("esc", 2)
    d.cmd("save -2")
    sa = d.wait_log(r"online save \d+: slot \d+ at frame \d+: \w+", md, 10)
    sh = host.wait_log(r"online save \d+: slot \d+ at frame \d+: \w+", mh, 10)
    check(bool(sa and sh), "the emulator's pause auto save, on both", "%s / %s" % (sa.group(0) if sa else None, sh.group(0) if sh else None))
    time.sleep(2)
    d.keys("esc", 2)
    cross.together(host, joiner, check, "together after the pause save", game=1)
    drives_p2(host, d, check, "after the pause save")
    drives_p1(g, d, check, "after the pause save")
    # the Windows game pauses, and the emulator drops
    g.keys("esc", 2)
    md = d.mark()
    cross.m.adb(d.port, "shell", "am", "force-stop", cross.PKG)
    # (REJOIN_REPLAY=1: the emulator asks for the replay, as builds before fast rejoins did)
    cross.m.adb(d.port, "shell", ("touch %s/rejoinreplay" if duo.REJOIN_REPLAY else "rm -f %s/rejoinreplay") % cross.m.D)
    time.sleep(10)
    cross.m.adb(d.port, "shell", "monkey", "-p", cross.PKG, "-c", "android.intent.category.LAUNCHER", "1"); time.sleep(26)
    md = d.mark(); d.cmd("rejoin")
    r = d.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)", md, 60)
    check(bool(r and "back in the match" in r.group(0)), "the emulator's app killed during the pause: it rejoins", r.group(0) if r else "no rejoin line")
    time.sleep(3)
    g.keys("esc", 2)   # unpause
    cross.together(host, joiner, check, "together after the rejoin", game=1)
    drives_p2(host, d, check, "after the rejoin")
    drives_p1(g, d, check, "after the rejoin")
    # as reported: the emulator pauses, its pause auto save, its app dies with the pause menu open,
    # it rejoins and unpauses
    mh, md = host.mark(), d.mark()
    d.keys("esc", 2)
    d.cmd("save -2")
    sa = d.wait_log(r"online save \d+: slot \d+ at frame \d+: \w+", md, 10)
    check(bool(sa), "the emulator's second pause auto save")
    time.sleep(2)
    cross.m.adb(d.port, "shell", "am", "force-stop", cross.PKG)
    # (REJOIN_REPLAY=1: the emulator asks for the replay, as builds before fast rejoins did)
    cross.m.adb(d.port, "shell", ("touch %s/rejoinreplay" if duo.REJOIN_REPLAY else "rm -f %s/rejoinreplay") % cross.m.D)
    time.sleep(10)
    cross.m.adb(d.port, "shell", "monkey", "-p", cross.PKG, "-c", "android.intent.category.LAUNCHER", "1"); time.sleep(26)
    md = d.mark(); d.cmd("rejoin")
    r = d.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)", md, 60)
    check(bool(r and "back in the match" in r.group(0)), "the emulator's app killed in its own pause menu: it rejoins", r.group(0) if r else "no rejoin line")
    time.sleep(3)
    d.keys("esc", 2)   # the emulator unpauses (its pause menu)
    cross.together(host, joiner, check, "together after that rejoin", game=1)
    drives_p2(host, d, check, "after that rejoin")
    drives_p1(g, d, check, "after that rejoin")
    for x, name in ((g, "the Windows game"), (d, "the emulator")):
        bad = [l for l in x.log().splitlines() if duo.BAD.search(l)]
        check(not bad, name + ": no desync or lost-connection lines", "; ".join(bad[:3]))
    return check


if __name__ == "__main__":
    try:
        c = test(*cross.start("win", PORT))
    except Exception as e:
        c = duo.Checks(); c.failed.append(str(e)); print("FAILED: " + str(e))
    print("PASSED" if not c.failed else "FAILED: " + "; ".join(c.failed))
    cross.m.adb(PORT, "shell", "rm -f %s/rejoinreplay" % cross.m.D)
    w.stop()
    w.check()
    sys.exit(1 if c.failed else 0)
