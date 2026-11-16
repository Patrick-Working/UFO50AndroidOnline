"""Three players: a Windows game (wingame.py's sandbox 1) and both emulators in one match.

  python trio.py

The Windows game hosts a public lobby, emulators 5555 and 5565 join. Then: a save asked for by
the host (the same frame and checksum on all three); emulator 5565's app killed, started again and
rejoining (with three players a rejoin replays the match; the two left share the lost player's last
inputs so they wait at the same frame); a save after it on all three; then the Windows game killed
and rejoining (F2), and a save again. No desync lines, no error box, no crash. PASSED / FAILED.
"""
import os, re, sys, time, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wingame as w
import duo
import cross
m = cross.m


def all_where(gs):
    marks = [g.mark() for g in gs]
    for g in gs: g.cmd("where")
    out = []
    for g, at in zip(gs, marks):
        r = g.wait_log(cross.WHERE, at, 8)
        out.append(tuple(int(x) for x in r.groups()) if r else None)
    return out


def together(gs, check, what):
    ws = all_where(gs)
    ok = all(ws) and len(set(x[0] for x in ws)) == 1 and all(x[2] == 3 for x in ws) and max(x[1] for x in ws) - min(x[1] for x in ws) <= cross.FRAME_TOL
    check(ok, what + ": all three together (game, frame, players)", " / ".join(str(x) for x in ws))


def save_all(asker, gs, check, what):
    marks = [g.mark() for g in gs]
    asker.cmd("save 1")
    for g, at in zip(gs, marks): g.wait_log(r"online save \d+: slot", at, 10)
    time.sleep(1)
    lines = [duo.save_lines(g, at) for g, at in zip(gs, marks)]
    ok = all(len(l) == 1 for l in lines) and len(set(l[0][2:] for l in lines)) == 1 and bool(lines[0][0][3])
    check(ok, what + ": the same frame and checksum on all three", " / ".join(str(l) for l in lines))


def start():
    d1, d2 = cross.Droid("5555"), cross.Droid("5565")
    # (two BlueStacks instances can't reach each other directly, each behind its own virtual
    # network: they go through the room server's relay, files/forcerelay)
    # (and on the room server: the tests that play alone switch it off with okplay.txt)
    for d in (d1, d2): m.adb(d.port, "shell", "touch %s/forcerelay; rm -f %s/okplay.txt" % (m.D, m.D))
    d1.launch(); d2.launch()
    pid = cross.start_windows()
    time.sleep(24)
    g = duo.Game(1, pid)
    duo.to_lobbies(g)
    g.keys("z", 6)                                    # HOST PUBLIC
    status = duo.menu_status(g)
    if "HOSTING" not in status: g.shot("not_hosting"); raise RuntimeError("the Windows game is not hosting")
    mg = g.mark()
    for d in (d1, d2):
        d.to_lobbies()
        if not d.sees_a_lobby(): d.shot("no_lobby"); raise RuntimeError("emulator %s never saw the lobby" % d.port)
        m.keys(d.port, "Z", 6)                        # join it
        m.keys(d.port, "Z", 2)                        # READY
    g.keys("z")                                       # READY: the session starts
    if not g.wait_log(r"STARTING SESSION", mg, 25): g.shot("no_session"); raise RuntimeError("the session never started")
    time.sleep(4)
    g.keys("z", 8)                                    # SELECT PROFILE: file 1
    ws = all_where([g, d1, d2])
    print("in the match: %s (game, frame, players)" % " / ".join(str(x) for x in ws))
    if not all(x and x[2] == 3 for x in ws): raise RuntimeError("not all three in the match")
    return g, d1, d2


def test(g, d1, d2):
    check = duo.Checks()
    if (g.where() or (0,))[0] != 0: g.keys("up", 1.5)
    together([g, d1, d2], check, "in the library")
    save_all(g, [g, d1, d2], check, "a save by the host")
    # an emulator drops for good: killed, started again, rejoining
    m.adb(d2.port, "shell", "am", "force-stop", cross.PKG); time.sleep(8)
    m.adb(d2.port, "shell", "monkey", "-p", cross.PKG, "-c", "android.intent.category.LAUNCHER", "1"); time.sleep(26)
    md = d2.mark(); d2.cmd("rejoin")
    r = d2.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)[^\n]*", md, 90)
    check(bool(r and "back in the match" in r.group(0)), "emulator 5565 killed: it rejoins", r.group(0) if r else "no rejoin line")
    time.sleep(4)
    together([g, d1, d2], check, "after its rejoin")
    save_all(d1, [g, d1, d2], check, "a save by the other emulator")
    # the host's game goes
    subprocess.run(["taskkill", "/F", "/PID", str(g.pid)], capture_output=True)
    time.sleep(6)
    pid = w.start(1); time.sleep(16)
    g = duo.Game(1, pid)
    mg = g.mark(); g.keys("f2")
    r = g.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)[^\n]*", mg, 90)
    check(bool(r and "back in the match" in r.group(0)), "the Windows game killed: it rejoins", r.group(0) if r else "no rejoin line")
    time.sleep(4)
    together([g, d1, d2], check, "after the host's rejoin")
    save_all(g, [g, d1, d2], check, "a save by the host again")
    for x, name in ((g, "the Windows game"), (d1, "emulator 5555"), (d2, "emulator 5565")):
        bad = [l for l in x.log().splitlines() if duo.BAD.search(l)]
        check(not bad, name + ": no desync or lost-connection lines", "; ".join(bad[:3]))
    st = w.state_of(g.pid)
    check("Code Error" not in st, "the Windows game: no error box", " ".join(st.split())[:300])
    for d in (d1, d2):
        crash = [l for l in m.full(d.port).splitlines() if re.search(r"FATAL|SIGSEGV|Assertion failed|ERROR in action", l)]
        check(not crash, "emulator %s: no crash or failed assertion" % d.port, crash[0][-160:] if crash else "")
    return check


if __name__ == "__main__":
    try:
        c = test(*start())
    except Exception as e:
        c = duo.Checks(); c.failed.append(str(e)); print("FAILED: " + str(e))
    print("PASSED" if not c.failed else "FAILED: " + "; ".join(c.failed))
    for port in ("5555", "5565"): m.adb(port, "shell", "rm -f %s/forcerelay" % m.D)
    w.stop()
    w.check()
    sys.exit(1 if c.failed else 0)
