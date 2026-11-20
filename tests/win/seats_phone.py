"""Several players on a phone, online: the phone (or an emulator) hosts a public lobby and seats two
players on WHO'S PLAYING (files/opensetup): its touch controls in P1 and the debug test pad
(files/testpad, A held by files/testpress) in P2; a Windows test game joins (P3). Then: three seats
on both; the test pad's presses reach both games; in a game for several players, a save asked for
by the phone has the same frame and checksum on both; the Windows game's load of it puts both back
on the same frame; the phone's app killed and back by a rejoin with the seats as they were; a save
after it still the same on both; no desync on either.

  python seats_phone.py <adb serial or BlueStacks port>

Ends with PASSED or FAILED and exit code 0 or 1. Uses wingame.py's sandbox 1.
"""
import re, sys, time
import duo, cross
import wingame as w

m = cross.m
check = duo.Checks()
SEATS = r"seats (\d+), extra inputs (\d+)"


def seats(g):
    at = g.mark(); g.cmd("where")
    r = g.wait_log(SEATS, at, 6)
    return (int(r.group(1)), int(r.group(2))) if r else None


def press_a(port, hold=0.35):
    m.adb(port, "shell", "touch %s/testpress" % m.D); time.sleep(hold)
    m.adb(port, "shell", "rm -f %s/testpress" % m.D); time.sleep(0.7)


def setup(g, d):
    """the phone, in the lobby: the test pad in, WHO'S PLAYING open, the pad joins and locks, DONE"""
    port = d.port
    at = d.mark()
    m.adb(port, "shell", "touch %s/testpad" % m.D); time.sleep(3)
    m.adb(port, "shell", "touch %s/opensetup" % m.D); time.sleep(2)
    full = m.adb(port, "logcat", "-d", "-s", "PFOKEYS:*")
    check("players: opened by files/opensetup" in full, "the phone's WHO'S PLAYING opens in the lobby")
    press_a(port); press_a(port)
    full = m.adb(port, "logcat", "-d", "-s", "PFOKEYS:*")
    lock = re.findall(r"players: pad \d+ locked in P(\d)", full)
    check(bool(lock), "the test pad locks in a slot", "P" + lock[-1] if lock else "no lock line")
    m.adb(port, "shell", "input keyevent KEYCODE_ENTER"); time.sleep(1.5)
    full = m.adb(port, "logcat", "-d", "-s", "PFOKEYS:*")
    sent = re.findall(r"players: slots sent ([P\d ]+)", full)
    check(bool(sent) and sent[-1].count("P") == 2, "the phone sends two players' slots", sent[-1] if sent else "no line")
    g.run0 = max(0, g.mark() - 4000)   # (this run's lines: the match starts after here)
    full = m.adb(port, "logcat", "-d", "-s", "PFOSTATE:*")
    check("lobby seats: " in full, "the phone (the host) works the seats out")
    time.sleep(1.5)


def main(port):
    host, joiner, g, d = cross.start("android", port, before_ready=setup)
    hl = g.wait_log(r"match seats from the lobby: ([^\n]*)", g.run0, 2)
    dl = re.findall(r"match seats from the lobby: ([^\n]*)", d.log())
    check(bool(hl), "Windows: the match takes the lobby's seats", hl.group(1) if hl else "")
    check(bool(dl), "the phone: the match takes the lobby's seats", dl[-1] if dl else "")
    sg, sd = seats(g), seats(d)
    check(bool(sg and sd and sg[0] == 3 and sd[0] == 3), "three seats on both", "%s / %s" % (sg, sd))
    # a game for several players (the third in the library)
    host.keys("right", 0.6, "right", 0.6, "z", 8, "z", 3, "down", 0.6, "z", 5)
    print("in game:", g.where())
    before = (seats(g), seats(d))
    for _ in range(4): press_a(port, 0.4)
    after = (seats(g), seats(d))
    check(bool(all(before) and all(after) and after[0][1] > before[0][1] and after[1][1] > before[1][1]),
          "the test pad's presses reach both games", "%s -> %s" % (before, after))
    s1 = cross.save_by(d, g, 1, check, "save by the phone")
    cross.play(g, d, 2)
    if s1: cross.load_by(g, d, s1[1], s1[2], check, "the Windows game's load")
    time.sleep(3)
    sg, sd = seats(g), seats(d)
    check(bool(sg and sd and sg[0] == 3 and sd[0] == 3), "three seats after the load", "%s / %s" % (sg, sd))
    # the phone's app killed, back by a rejoin
    m.adb(port, "shell", "am", "force-stop", cross.PKG); time.sleep(8)
    m.adb(port, "shell", "monkey", "-p", cross.PKG, "-c", "android.intent.category.LAUNCHER", "1"); time.sleep(26)
    md = d.mark(); d.cmd("rejoin")
    r = d.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)", md, 60)
    check(bool(r and "back in the match" in r.group(0)), "the phone's app killed: it rejoins", r.group(0) if r else "no rejoin line")
    duo.check_rejoin_kind(d, md, check, "the phone's rejoin")
    time.sleep(4)
    sg, sd = seats(g), seats(d)
    check(bool(sg and sd and sg[0] == 3 and sd[0] == 3), "three seats after the rejoin", "%s / %s" % (sg, sd))
    press_a(port); cross.play(g, d, 1)
    cross.save_by(g, d, 3, check, "save after the rejoin")
    bad = [l for l in g.log(g.run0).splitlines() if duo.BAD.search(l)]
    check(not bad, "Windows: no desync or lost-connection lines", "; ".join(l.strip()[-140:] for l in bad[:2]))
    bad = [l for l in d.log().splitlines() if duo.BAD.search(l)]
    check(not bad, "the phone: no desync or lost-connection lines", "; ".join(l.strip()[-140:] for l in bad[:2]))
    crash = [l for l in m.full(port).splitlines() if re.search(r"FATAL|SIGSEGV|Assertion failed", l)]
    check(not crash, "the phone: no crash or failed assertion", crash[0].strip()[-140:] if crash else "")
    return check


if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else "5565"
    try:
        c = main(port)
    except Exception as e:
        c = duo.Checks(); c.failed.append(str(e)); print("FAIL  %s" % e)
    print("PASSED" if not c.failed else "FAILED: " + "; ".join(c.failed))
    good = w.stop()
    m.adb(port, "shell", "rm -f %s/testpad %s/testpress %s/opensetup" % (m.D, m.D, m.D))
    m.adb(port, "shell", "am", "force-stop", cross.PKG)
    sys.exit(1 if c.failed or not good else 0)
