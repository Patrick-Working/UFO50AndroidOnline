"""A boxed-in Windows test game and an Android emulator in one match (cross-play).

  python cross.py win [port]       the Windows game hosts a public lobby, the emulator joins
  python cross.py android [port]   the emulator hosts, the Windows game joins
  python cross.py stop             close the Windows game, check the real save folder

The emulator is 127.0.0.1:<port> (default 5565) with the Android build installed, the game's
video scale at 4 and a profile in file 1 (see docs/TESTING.md). The test: both in the online
library; a save asked for by each side (the same frame and checksum on both: the two builds
compute the same game); a two-player game; a load asked for by each side and accepted by the
other (both put the save's snapshot back and carry on); a minute of play with a save at the
end; each side's app killed and rejoining. It ends with PASSED or FAILED and exit code 0 or 1.

With PFO_NO_SNAPSHOTS=1 set the Windows game runs without its snapshots: the match is then one of
files only, and an accepted load restarts it from the save by replay on both.

The Windows game runs in wingame.py's sandbox 1. Pictures go to work/win_duo (private: they show
the game). Log lines that hold addresses are never printed.
"""
import os, re, sys, time
import wingame as w
import duo

HERE = os.path.dirname(os.path.abspath(__file__))
# the Android helpers: tests/mp_test.py in the repository, scripts/savestate_tests in the development tree
for d in (os.path.join(HERE, ".."), os.path.join(HERE, "..", "savestate_tests")):
    if os.path.exists(os.path.join(d, "mp_test.py")): sys.path.insert(0, os.path.abspath(d)); break
import mp_test as m

FRAME_TOL = 120   # frames: the two answer "where" up to about a second apart (adb, and each reads its test command every 30 frames)
WHERE = r"where: game (-?\d+), frame (\d+), clients (\d+)"
ANDROID_KEYS = {"up": "DPAD_UP", "down": "DPAD_DOWN", "left": "DPAD_LEFT", "right": "DPAD_RIGHT", "z": "Z", "x": "X", "esc": "ESCAPE"}
PKG = "com.unofficial.ufo50"


class Droid:
    """the emulator, with the calls duo.Game has"""
    n = "android"

    def __init__(self, port): self.port = port

    def keys(self, *seq):
        for k in seq:
            if isinstance(k, (int, float)): time.sleep(k)
            else: m.key(self.port, ANDROID_KEYS[k])

    def shot(self, name):
        import subprocess
        with open(os.path.join(duo.OUT, "%s_android.png" % name), "wb") as f:
            f.write(subprocess.run([m.ADB, "-s", "127.0.0.1:" + self.port, "exec-out", "screencap", "-p"], capture_output=True).stdout)

    def log(self, since=0):
        text = m.log(self.port)[since:]
        return "\n".join(l for l in text.splitlines() if not duo.PRIVATE.search(l))

    def mark(self): return len(m.log(self.port))
    def cmd(self, text): m.cmd(self.port, text)

    def slots(self):
        out = m.adb(self.port, "shell", "ls %s/savestates" % m.D)
        return sorted(int(x) for x in re.findall(r"slot(\d+)\.pfostate", out))

    def wait_log(self, pattern, since, seconds=20):
        end = time.time() + seconds
        while time.time() < end:
            r = re.search(pattern, self.log(since))
            if r: return r
            time.sleep(0.3)
        return None

    def where(self):
        at = self.mark()
        self.cmd("where")
        r = self.wait_log(WHERE, at, 6)
        return tuple(int(x) for x in r.groups()) if r else None

    def launch(self):
        m.adb(self.port, "shell", "am", "force-stop", PKG)
        m.adb(self.port, "shell", "rm -f %s/rejoin.txt %s/ss_cmd" % (m.D, m.D))
        m.adb(self.port, "logcat", "-G", "16M"); m.adb(self.port, "logcat", "-c")
        m.adb(self.port, "shell", "monkey", "-p", PKG, "-c", "android.intent.category.LAUNCHER", "1")

    def to_lobbies(self):
        """from the start of the app to the lobby list (mp_test.py's way: by reference pictures)"""
        p = self.port
        m.keys(p, "ESCAPE", 4)
        for _ in range(8):
            if m.looks(p, "ref_profiles.png", (1360, 520, 1640, 740)): break
            m.keys(p, "Z", 3.5)
        else: self.shot("no_profiles"); raise RuntimeError("the emulator never reached the profile screen")
        m.keys(p, "DPAD_RIGHT", 1, "Z", 5)
        if not m.looks(p, "ref_lobbies.png", (570, 140, 1340, 230)): self.shot("no_lobbies"); raise RuntimeError("the emulator is not on the lobby screen")

    def sees_a_lobby(self):
        for _ in range(12):
            if not m.looks(self.port, "ref_lobbies.png", (330, 290, 1250, 410), 8): return True
            time.sleep(2)
        return False


def both_where(a, b):
    ma, mb = a.mark(), b.mark()
    a.cmd("where"); b.cmd("where")
    ra, rb = a.wait_log(WHERE, ma, 6), b.wait_log(WHERE, mb, 6)
    return tuple(tuple(int(x) for x in r.groups()) if r else None for r in (ra, rb))


def start_windows():
    if w.game_pids(): w.stop()
    for f in ("rejoin.txt", "ss_cmd"):
        try: os.remove(os.path.join(duo.folder(1), f))
        except OSError: pass
    return w.start(1)    # the pid (its window comes a few seconds later)


def start(host_is, port):
    """both into one public lobby and on into the online library; returns (host, joiner, windows, android)"""
    d = Droid(port)
    d.launch()
    pid = start_windows()
    time.sleep(24)
    g = duo.Game(1, pid)
    if host_is == "win":
        duo.to_lobbies(g)
        g.keys("z", 6)                                # HOST PUBLIC
        status = duo.menu_status(g)
        if "HOSTING" not in status: g.shot("not_hosting"); raise RuntimeError("the Windows game is not hosting (%s)" % re.sub(r"CODE \w+", "CODE <code>", status))
        d.to_lobbies()
        if not d.sees_a_lobby(): d.shot("no_lobby"); raise RuntimeError("the emulator never saw the lobby")
        mg = g.mark()
        m.keys(port, "Z", 6)                          # join it
        m.keys(port, "Z", 1.5)                        # READY
        g.keys("z")                                   # READY: the session starts
        if not g.wait_log(r"STARTING SESSION", mg, 25): g.shot("no_session"); d.shot("no_session"); raise RuntimeError("the session never started")
        time.sleep(4)
        g.keys("z", 8)                                # SELECT PROFILE: file 1
        host, joiner = g, d
    else:
        d.to_lobbies()
        m.keys(port, "Z", 6)                          # HOST PUBLIC
        duo.to_lobbies(g)
        at = g.mark()
        if not g.wait_log(r"okplay list in \d+ ms: \d+ rooms, [1-9]\d* usable", at, 20): g.shot("no_lobby"); raise RuntimeError("the lobby never showed in the Windows game's list")
        mg = g.mark()
        time.sleep(0.8)
        duo.onto_lobby(g)
        g.keys("z", 7)                                # join it
        g.keys("z", 1.5)                              # READY
        m.key(port, "Z")                              # READY: the session starts
        if not g.wait_log(r"STARTING SESSION", mg, 25): g.shot("no_session"); d.shot("no_session"); raise RuntimeError("the session never started")
        time.sleep(8)
        m.keys(port, "Z", 10)                         # SELECT PROFILE: file 1
        host, joiner = d, g
    wh, wj = both_where(host, joiner)
    print("in the match: host %s, joiner %s (game, frame, players)" % (wh, wj))
    if not (wh and wj and wh[0] == 0 and wj[0] == 0 and wh[2] == 2 and wj[2] == 2):
        g.shot("no_library"); d.shot("no_library"); raise RuntimeError("not both in the online library")
    return host, joiner, g, d


def save_by(asker, other, slot, check, what):
    """an online save asked for by `asker`: (its slot at the asker, its slot at the other, frame) or None"""
    ma, mo = asker.mark(), other.mark()
    asker.cmd("save %d" % slot)
    asker.wait_log(r"online save \d+: slot", ma, 8); other.wait_log(r"online save \d+: slot", mo, 8)
    time.sleep(0.5)
    sa, so = duo.save_lines(asker, ma), duo.save_lines(other, mo)
    ok = len(sa) == 1 and len(so) == 1 and sa[0][2] == so[0][2] and sa[0][3] and sa[0][3] == so[0][3] and sa[0][0] == so[0][0]
    check(ok, "%s: the same frame and checksum on both" % what, "%s / %s" % (sa, so))
    return (sa[0][1], so[0][1], sa[0][2]) if ok else None


def load_by(asker, other, slot, frame, check, what):
    """`asker` asks to load its save `slot`; `other` accepts. With snapshots both put the save's state
    back on the same frame; without, both must reach `frame` by replay, verified"""
    ma, mo = asker.mark(), other.mark()
    asker.cmd("load %d" % slot)
    got = other.wait_log(duo.PROPOSE, mo, 8)
    check(bool(got), "%s: the load is put to the other side" % what)
    if isinstance(other, duo.Game):
        box = other.wait_log(r"vote box: shown \(slot (\d+), picture (yes|no)\)", mo, 5)
        check(bool(box and box.group(2) == "yes"), "%s: the Windows game asks in a box with the save's picture" % what, box.group(0) if box else "no box")
        time.sleep(0.4); other.shot("cross_vote")
    time.sleep(1.0)
    if isinstance(other, duo.Game): other.keys("f2")
    else: other.cmd("accept")
    la, lo = duo.loaded(asker, other, ma, mo, 90)
    ok = len(la) == 1 and len(lo) == 1 and la[0] == lo[0] and la[0][1]
    if duo.SNAPS: check(ok, "%s: both put the save's snapshot back on the same frame" % what, "%s / %s" % (la, lo))
    else:
        ok = ok and la[0][0] == frame
        check(ok, "%s: both replayed to frame %d, checksums verified" % (what, frame), "%s / %s" % (la, lo))
    return ok


def play(a, b, rounds=3):
    for _ in range(rounds):
        a.keys("right", "z", "x", 0.3); b.keys("left", "z", "x", 0.3)
        a.keys("left", "z", 0.3); b.keys("right", "z", 0.3)


def together(a, b, check, what, game=None):
    wa, wb = both_where(a, b)
    ok = bool(wa and wb and wa[0] == wb[0] and wa[2] == 2 and wb[2] == 2 and abs(wa[1] - wb[1]) <= FRAME_TOL and (game is None or (wa[0] == 0) == (game == 0)))
    check(ok, what, "%s / %s" % (wa, wb))
    return wa, wb


def test(host, joiner, g, d):
    check = duo.Checks()
    nosnap = "a player's device has no snapshots"
    said = (nosnap in g.log()[g.log().rfind("STARTING SESSION"):], nosnap in d.log())
    if duo.SNAPS: check(not any(said), "the match has snapshots (nobody asked for one without)")
    else: check(all(said), "both know the match has no snapshots (command 246)")
    lib = host.where()
    if lib and lib[0] != 0: host.keys("up", 1.5)       # the library's demo reel was on show

    # 1. a save in the library, asked for by the host
    md = d.mark()
    s1 = save_by(host, joiner, 1, check, "library save by the host")
    if not s1: return check
    if duo.SNAPS: check(bool(d.wait_log(r"snapshot \d+: taken at frame", md, 4)), "the emulator took a snapshot for it")
    else: check("captur" not in d.log(md) and "snapshot" not in d.log(md), "the emulator took no snapshot for it")

    # 2. a game for two (the third in the library), then a save asked for by the joiner
    host.keys("right", 0.6, "right", 0.6, "z", 8, "z", 3, "down", 0.6, "z", 5)
    play(host, joiner)
    wa, _ = together(host, joiner, check, "both in the same game", game=1)
    s2 = save_by(joiner, host, 2, check, "in-game save by the joiner")
    if not s2: return check
    play(host, joiner, 4)
    before = (g.slots(), d.slots())

    # 3. the joiner asks to load its save, the host accepts: both restart the match from the save
    if load_by(joiner, host, s2[0], s2[2], check, "the joiner's load"):
        time.sleep(3)
        together(host, joiner, check, "both carry on together from there", game=1)
    play(host, joiner, 2)

    # 4. the host asks to load the library save, the joiner accepts
    if load_by(host, joiner, s1[0], s1[2], check, "the host's load of the library save"):
        time.sleep(4)
        together(host, joiner, check, "both back in the library", game=0)
    check((g.slots(), d.slots()) == before, "the loads left no extra save files", "%s -> %s" % (before, (g.slots(), d.slots())))

    # 5. a minute in the game with keys from both sides, then a save: still the same game state
    host.keys("z", 8, "z", 3, "down", 0.6, "z", 5)
    for _ in range(6): play(host, joiner, 3)
    together(host, joiner, check, "a minute on: both in the same game", game=1)
    s3 = save_by(host, joiner, 3, check, "save after a minute of play")

    # 6. the emulator's app is killed and rejoins
    md = None
    m.adb(d.port, "shell", "am", "force-stop", PKG); time.sleep(8)
    m.adb(d.port, "shell", "monkey", "-p", PKG, "-c", "android.intent.category.LAUNCHER", "1"); time.sleep(26)
    md = d.mark(); d.cmd("rejoin")
    r = d.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)", md, 60)
    check(bool(r and "back in the match" in r.group(0)), "the emulator's app killed: it rejoins", r.group(0) if r else "no rejoin line")
    time.sleep(4)
    together(host, joiner, check, "together after the emulator's rejoin")

    # 7. the Windows game is killed and rejoins (F2 at its start)
    import subprocess
    subprocess.run(["taskkill", "/F", "/PID", str(g.pid)], capture_output=True)
    time.sleep(6)
    pid = w.start(1)
    time.sleep(16)
    g2 = duo.Game(1, pid)
    mg = g2.mark(); g2.keys("f2")
    r = g2.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)", mg, 90)
    check(bool(r and "back in the match" in r.group(0)), "the Windows game killed: it rejoins", r.group(0) if r else "no rejoin line")
    time.sleep(4)
    h2, j2 = (g2, d) if host is g else (d, g2)
    together(h2, j2, check, "together after the Windows game's rejoin")
    play(h2, j2, 3)
    # (asked by the host: this game is for one player, and a device without a seat can't ask)
    if s3: save_by(h2, j2, 4, check, "save after both rejoins")

    for x, name in ((g2, "the Windows game"), (d, "the emulator")):
        bad = [l for l in x.log().splitlines() if duo.BAD.search(l)]
        check(not bad, "%s: no desync or lost-connection lines" % name, "; ".join(l.strip()[-140:] for l in bad[:3]))
    crash = [l for l in m.full(d.port).splitlines() if re.search(r"FATAL|SIGSEGV|Assertion failed", l)]
    check(not crash, "the emulator: no crash or failed assertion", crash[0].strip()[-140:] if crash else "")
    g2.shot("cross_end"); d.shot("cross_end")
    return check


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else ""
    port = sys.argv[2] if len(sys.argv) > 2 else "5565"
    if what in ("win", "android"):
        try: c = test(*start(what, port))
        except RuntimeError as e:
            print("FAILED: %s" % e); w.stop(); sys.exit(1)
        print("PASSED" if not c.failed else "FAILED: " + "; ".join(c.failed))
        good = w.stop()
        m.adb(port, "shell", "am", "force-stop", PKG)
        sys.exit(1 if c.failed or not good else 0)
    elif what == "stop": sys.exit(0 if w.stop() else 1)
    else: print(__doc__)
