"""Two boxed-in test games on this PC (wingame.py's sandboxes 1 and 2): helpers and a whole test.

  python duo.py start          start both and bring them into one private lobby by its code,
                               ready up, pick the profile: both in the online library
  python duo.py test           (after start) online save, a two-player game, save there, a load
                               that is declined (the box asking shows the save's picture), one
                               that is accepted (both put the save's snapshot back and carry on),
                               a load back to the library, and a look for desyncs and for save
                               files piling up
  python duo.py all            start, test, stop
  python duo.py stop           close both, check the real save folder

With PFO_NO_SNAPSHOTS=1 set the games run without their snapshots, as a build for a game version
the snapshots don't know would: the test then expects a match of files only (an accepted load
restarts the match from the save by replay on both).

Pictures go to work/win_duo (private: they show the game). Log lines that hold addresses are
never printed. The games use oldkid.net's rooms like any other, and hosting asks the router for
a port forward (UPnP) as the game always does.
"""
import os, re, sys, time
import wingame as w

OUT = os.path.join(w.OUTDIR, "win_duo")
# each game reads its test command every 30 frames, so two games on the same frame can answer
# "where" up to 30 frames apart; the rest is for the wait between the two answers
FRAME_TOL = 45
os.makedirs(OUT, exist_ok=True)
PRIVATE = re.compile(r"STUN|UPnP|\d+\.\d+\.\d+\.\d+")   # lines that can hold an address
BAD = re.compile(r"desync|ran out of input|GoingOffline|MISMATCH|didn't land|PFOCRASH|online load of snapshot \d+ failed", re.I)
SNAPS = os.environ.get("PFO_NO_SNAPSHOTS") != "1"
PROPOSE = r"command 24[15] \(\d+\)"   # a save of this match (241), or one sent along as a file (245)


def folder(n): return os.path.join(w.home(n), "ufo50_online")


def log(n, since=0):
    """the game's log from byte `since` on, without the lines that hold addresses"""
    try:
        with open(os.path.join(folder(n), "pfo_online.log"), "rb") as f:
            f.seek(since)
            text = f.read().decode("utf-8", "replace")
    except OSError:
        return ""
    return "\n".join(l for l in text.splitlines() if not PRIVATE.search(l))


def mark(n):
    try: return os.path.getsize(os.path.join(folder(n), "pfo_online.log"))
    except OSError: return 0


def cmd(n, text):
    """a test command for the game (android/savestate.inc ReadTestCommand)"""
    with open(os.path.join(folder(n), "ss_cmd"), "w") as f: f.write(text + "\n")


def slots(n):
    d = os.path.join(folder(n), "savestates")
    if not os.path.isdir(d): return []
    return sorted(int(m.group(1)) for m in (re.match(r"slot(\d+)\.pfostate$", f) for f in os.listdir(d)) if m)


class Game:
    def __init__(self, n, pid):
        self.n, self.pid = n, pid
        self.hwnd = w.game_window(pid)

    def keys(self, *seq):
        for k in seq:
            if isinstance(k, (int, float)): time.sleep(k)
            else: w.press(k, self.hwnd)

    def shot(self, name): w.shot(os.path.join(OUT, "%s_%d.png" % (name, self.n)), self.hwnd)
    def mark(self): return mark(self.n)
    def log(self, since=0): return log(self.n, since)
    def cmd(self, text): cmd(self.n, text)
    def slots(self): return slots(self.n)

    def wait_log(self, pattern, since, seconds=20):
        end = time.time() + seconds
        while time.time() < end:
            m = re.search(pattern, self.log(since))
            if m: return m
            time.sleep(0.3)
        return None

    def where(self):
        """(game, frame, clients) as the game says, or None"""
        at = self.mark()
        self.cmd("where")
        m = self.wait_log(r"where: game (-?\d+), frame (\d+), clients (\d+)", at, 5)
        return tuple(int(x) for x in m.groups()) if m else None


def both_where(a, b):
    """where() of both games, asked at the same moment: asking one after the other puts the first
    one's answer time (up to a second) between the two frame numbers"""
    ma, mb = a.mark(), b.mark()
    a.cmd("where"); b.cmd("where")
    pat = r"where: game (-?\d+), frame (\d+), clients (\d+)"
    ra, rb = a.wait_log(pat, ma, 5), b.wait_log(pat, mb, 5)
    return tuple(tuple(int(x) for x in m.groups()) if m else None for m in (ra, rb))


def games():
    """the running test games, by sandbox number (the order they were started in)"""
    state = os.path.join(OUT, "pids.txt")
    pids = [int(x) for x in open(state).read().split()] if os.path.exists(state) else []
    alive = w.game_pids()
    if len(pids) != 2 or any(p not in alive for p in pids): raise SystemExit("start the two test games first: duo.py start")
    return Game(1, pids[0]), Game(2, pids[1])


def set_name(n, name):
    """a player name for the pictures (without one the game shows the computer's name)"""
    p = os.path.join(folder(n), "menu.cfg")
    try: lines = [l for l in open(p, "rb").read().decode("utf-8", "replace").splitlines() if l and not l.startswith("name=")]
    except OSError: lines = []
    os.makedirs(folder(n), exist_ok=True)
    with open(p, "wb") as f: f.write(("\n".join(["name=" + name] + lines) + "\n").encode("utf-8"))


def launch():
    if w.game_pids(): w.stop()
    for n in (1, 2):
        set_name(n, "TEST%d" % n)
        for f in ("rejoin.txt", "ss_cmd"):
            try: os.remove(os.path.join(folder(n), f))
            except OSError: pass
    pids = [w.start(1)]
    time.sleep(1)
    pids.append(w.start(2))
    open(os.path.join(OUT, "pids.txt"), "w").write("%d %d" % tuple(pids))
    time.sleep(14)
    return games()


def to_lobbies(g):
    """from the start of the game to CREATE OR JOIN LOBBY (the lobby list being asked for shows in the log)"""
    for _ in range(10):
        at = g.mark()
        g.keys("right", 0.6, "z", 3.5)
        if g.wait_log(r"okplay list in", at, 3): return
    raise RuntimeError("game %d never reached the lobby list" % g.n)


def onto_lobby(g):
    """put the cursor on the first lobby of the list. It starts there when the list had a lobby as the
    screen opened, and on one of the buttons at the right (drawn yellow then) when the lobby came later"""
    from PIL import Image
    g.shot("cursor")
    img = Image.open(os.path.join(OUT, "cursor_%d.png" % g.n)).convert("RGB")
    wd, ht = img.size
    box = img.crop((int(wd * 0.77), int(ht * 0.21), int(wd * 0.93), int(ht * 0.82)))   # HOST PUBLIC, HOST PRIVATE, JOIN BY CODE
    yellow = sum(1 for r, gr, b in box.getdata() if r > 200 and gr > 200 and b < 130)
    if yellow > 40: g.keys("left", 0.6)


def menu_status(g):
    """open and close our menu: its status line (it is written to the log)"""
    at = g.mark()
    g.keys("f1")
    m = g.wait_log(r"menu: open \((.*)\)", at, 5)
    g.keys("f1", 0.3)
    return m.group(1) if m else ""


def start():
    a, b = launch()
    to_lobbies(a)
    a.keys("down", 0.5, "z", 6)                       # HOST PRIVATE
    status = menu_status(a)
    m = re.search(r"CODE (\w+)", status)
    if not m: a.shot("no_code"); raise RuntimeError("no invite code in the host's menu (%s)" % status)
    code = m.group(1)
    print("host: %s" % status.replace(code, "<code>"))
    to_lobbies(b)
    at = b.mark()
    b.keys("down", 0.4, "down", 0.4, "z", 1.5)       # JOIN BY CODE: our code box
    if not b.wait_log(r"menu: open", at, 5): b.shot("no_code_box"); raise RuntimeError("JOIN BY CODE did not open the code box")
    b.keys(*list(code.lower()), 0.3, "enter")
    if not b.wait_log(r"message: CODE \w+ ADDED", at, 5): b.shot("code_refused"); raise RuntimeError("the code was not taken")
    # the private lobby shows in the list once the next list answer has it; then it can be picked
    at = b.mark()
    if not b.wait_log(r"okplay list in \d+ ms: \d+ rooms, [1-9]\d* usable", at, 15): b.shot("no_lobby"); raise RuntimeError("the lobby never showed in the joiner's list")
    ma, mb = a.mark(), b.mark()
    time.sleep(0.8)
    onto_lobby(b)
    b.keys("z", 7)                                    # join it
    b.keys("z", 1.5)                                  # READY
    a.keys("z")                                       # READY: the session starts
    for g, m0 in ((a, ma), (b, mb)):
        if not g.wait_log(r"STARTING SESSION", m0, 25): a.shot("no_session"); b.shot("no_session"); raise RuntimeError("game %d: the session never started" % g.n)
    time.sleep(4)
    a.keys("z", 6)                                    # SELECT PROFILE: file 1
    wa, wb = both_where(a, b)
    print("in the match: host %s, joiner %s (game, frame, players)" % (wa, wb))
    if not (wa and wb and wa[0] == 0 and wb[0] == 0 and wa[2] == 2 and wb[2] == 2):
        a.shot("no_library"); b.shot("no_library"); raise RuntimeError("not both in the online library")
    return a, b


# ---- the test -------------------------------------------------------------------------------

class Checks:
    def __init__(self): self.failed = []
    def __call__(self, ok, what, detail=""):
        print("%s  %s%s" % ("ok  " if ok else "FAIL", what, (" (%s)" % detail) if detail else ""))
        if not ok: self.failed.append(what)
        return ok


def save_lines(g, since):
    """(save number, our slot, frame, checksum) of the online saves logged since"""
    t = g.log(since)
    sums = dict(re.findall(r"save slot (\d+): frame \d+, \d+ bytes, checksum ([0-9a-f]+): ok", t))
    return [(int(i), int(s), int(f), sums.get(s)) for i, s, f in re.findall(r"online save (\d+): slot (\d+) at frame (\d+): ok", t)]


def load_lines(g, since):
    """(our slot, frame, verified) of the loads that finished since"""
    return [(int(s), int(f), v == "verified") for s, f, v in re.findall(r"load slot (\d+): reached frame (\d+) .*: (verified|MISMATCH)", g.log(since))]


def loaded(a, b, ma, mb, seconds):
    """wait for a load to end on both games: each one's [(frame, good)] since the marks (one entry
    each when all is well). With snapshots that is the frame the state was put back on (the match
    carries on from there); without, the save's frame, reached by replay"""
    if SNAPS:
        pat = r"online load of snapshot \d+(?:: done at frame (\d+)| failed)"
        a.wait_log(pat, ma, seconds); b.wait_log(pat, mb, seconds)
        return tuple([(int(f or 0), bool(f)) for f in re.findall(pat, g.log(m0))] for g, m0 in ((a, ma), (b, mb)))
    pat = r"load slot \d+: reached frame"
    a.wait_log(pat, ma, seconds); b.wait_log(pat, mb, seconds)
    return tuple([(f, v) for _, f, v in load_lines(g, m0)] for g, m0 in ((a, ma), (b, mb)))


def covers(g, since):
    """how often the loading screen came and went"""
    t = g.log(since)
    return len(re.findall(r"cover: on", t)), len(re.findall(r"cover: off", t))


def play(a, b, rounds=3):
    for _ in range(rounds):
        a.keys("right", "z", "x"); b.keys("left", "z", "x")


def test(a, b):
    check = Checks()
    start_a = a.log().rfind("STARTING SESSION"); start_b = b.log().rfind("STARTING SESSION")
    nosnap = "a player's device has no snapshots"
    said = (nosnap in a.log()[start_a:], nosnap in b.log()[start_b:])
    if SNAPS: check(not any(said), "the match has snapshots (nobody asked for one without)")
    else: check(all(said), "both know the match has no snapshots (command 246)")

    # (left alone, the library plays its demo reel, and `where` then names the game on show; the
    # first key only ends the reel)
    lib = a.where()
    if lib and lib[0] != 0: a.keys("up", 1.5); lib = a.where()
    check(bool(lib and lib[0] == 0), "in the library, the demo reel not playing", "%s" % (lib,))

    # 1. a save in the library, asked for by the host
    ma, mb = a.mark(), b.mark()
    a.cmd("save 1")
    a.wait_log(r"online save \d+: slot", ma, 6); b.wait_log(r"online save \d+: slot", mb, 6)
    sa, sb = save_lines(a, ma), save_lines(b, mb)
    ok = len(sa) == 1 and len(sb) == 1 and sa[0][2:] == sb[0][2:] and sa[0][0] == sb[0][0]
    check(ok, "library save: both saved the same frame and checksum", "%s / %s" % (sa, sb))
    if not ok: return check
    lib_a = sa[0][1]
    if SNAPS: check(bool(a.wait_log(r"snapshot \d+: taken at frame", ma, 4) and b.wait_log(r"snapshot \d+: taken at frame", mb, 4)), "the save made a snapshot on both")
    else: check("snapshot" not in a.log(ma) and "captur" not in a.log(ma), "the save made no snapshot")
    check(bool(a.wait_log(r"picture: slot %d saved" % lib_a, ma, 6) and b.wait_log(r"picture: slot %d saved" % sb[0][1], mb, 6)), "both kept a picture of it")

    # 2. a game for two (the third in the library: 2 PLAYERS on its title)
    a.keys("right", 0.5, "right", 0.5, "z", 8, "z", 3, "down", 0.4, "z", 5)
    play(a, b)
    wa, wb = both_where(a, b)
    check(bool(wa and wb and wa[0] == wb[0] and wa[0] > 0 and wa[2] == 2), "both in the same game", "%s / %s" % (wa, wb))
    seat = menu_status(b)
    check("YOU ARE P2" in seat, "the joiner has a seat there", seat)

    # 3. the joiner saves
    ma, mb = a.mark(), b.mark()
    b.cmd("save 1")
    a.wait_log(r"online save \d+: slot", ma, 6); b.wait_log(r"online save \d+: slot", mb, 6)
    sa, sb = save_lines(a, ma), save_lines(b, mb)
    ok = len(sa) == 1 and len(sb) == 1 and sa[0][2:] == sb[0][2:] and sa[0][0] == sb[0][0]
    check(ok, "in-game save by the joiner: the same frame and checksum on both", "%s / %s" % (sa, sb))
    if not ok: return check
    game_b, frame = sb[0][1], sb[0][2]
    play(a, b, 4)
    before = (a.slots(), b.slots())

    # 4. the joiner asks to load it; the host says no (F3)
    ma, mb = a.mark(), b.mark()
    b.cmd("load %d" % game_b)
    got = a.wait_log(PROPOSE, ma, 6)
    check(bool(got), "the load is put to the host", got.group(0) if got else "")
    box = a.wait_log(r"vote box: shown \(slot (\d+), picture (yes|no)\)", ma, 5)
    check(bool(box and box.group(2) == "yes"), "the host is asked in a box with the save's picture", box.group(0) if box else "no box")
    time.sleep(0.4)
    a.shot("vote")
    time.sleep(1.0)
    a.keys("f3")
    check(bool(a.wait_log(r"command 243", ma, 6) and b.wait_log(r"command 243", mb, 6)), "F3: declined on both")
    check(bool(b.wait_log(r"message: P1 SAID NO", mb, 4)), "the joiner is told")
    time.sleep(1.5)
    check(not load_lines(a, ma) and not load_lines(b, mb), "nothing was loaded")

    # 5. again; the host says yes (F2): both restart the match from the save
    ma, mb = a.mark(), b.mark()
    b.cmd("load %d" % game_b)
    a.wait_log(PROPOSE, ma, 6)
    time.sleep(1.0)
    a.keys("f2")
    la, lb = loaded(a, b, ma, mb, 90)
    ok = len(la) == 1 and len(lb) == 1 and la[0] == lb[0] and la[0][1]
    if SNAPS: check(ok and la[0][0] > frame, "F2: both put the save's snapshot back on the same frame", "%s / %s, saved at %d" % (la, lb, frame))
    else: check(ok and la[0][0] == frame, "F2: both replayed to the save's frame, checksums verified", "%s / %s, saved at %d" % (la, lb, frame))
    time.sleep(4)
    for g, m0 in ((a, ma), (b, mb)):
        on, off = covers(g, m0)
        # (a snapshot is back within a few frames: the loading screen may not get to show)
        if SNAPS: print("note  game %d: the loading screen showed %d time(s)" % (g.n, on))
        else: check(on == 1 and off == 1, "game %d: the loading screen came and went" % g.n, "%d on, %d off" % (on, off))
        if SNAPS and on != off: check(False, "game %d: the loading screen went again" % g.n, "%d on, %d off" % (on, off))
    play(a, b)
    wa, wb = both_where(a, b)
    check(bool(wa and wb and wa[0] == wb[0] and wa[2] == 2 and wb[2] == 2 and abs(wa[1] - wb[1]) <= FRAME_TOL and wa[1] > frame), "both carry on together from there", "%s / %s" % (wa, wb))
    check((a.slots(), b.slots()) == before, "the loads left no extra save files", "%s -> %s" % (before, (a.slots(), b.slots())))
    for g, m0 in ((a, ma), (b, mb)):
        said = re.findall(r"message: (.*)", g.log(m0))
        check("LOADED" in said and "SAVED" not in said and not any(x.startswith("YOU ARE") for x in said), "game %d says LOADED, and nothing about saving or seats" % g.n, ", ".join(said))
    if not SNAPS: check(any("we have that save already" in l for l in a.log(ma).splitlines()), "the host used its own copy of the save")

    # 6. back to the library save, from inside the game (the host asks, the joiner answers)
    ma, mb = a.mark(), b.mark()
    a.cmd("load %d" % lib_a)
    if b.wait_log(PROPOSE, mb, 6):
        box = b.wait_log(r"vote box: shown \(slot (\d+), picture (yes|no)\)", mb, 5)
        check(bool(box and box.group(2) == "yes"), "the joiner is asked in a box with the save's picture", box.group(0) if box else "no box")
        time.sleep(0.4); b.shot("vote_library"); time.sleep(0.6)
        b.cmd("accept")
    la, lb = loaded(a, b, ma, mb, 60)
    check(len(la) == 1 and len(lb) == 1 and la[0] == lb[0] and la[0][1], "load of the library save from inside the game: %s on both" % ("its snapshot back on the same frame" if SNAPS else "verified"), "%s / %s" % (la, lb))
    if os.environ.get("DUO_UNTIL") == "6": return check   # (development: stop here, the games as they are)
    time.sleep(4)
    wa, wb = both_where(a, b)
    check(bool(wa and wb and wa[0] == 0 and wb[0] == 0 and wa[2] == 2), "both back in the library", "%s / %s" % (wa, wb))
    check((a.slots(), b.slots()) == before, "still no extra save files", "%s" % ((a.slots(), b.slots()),))

    # 7. a while later: still together, nothing went wrong on the way
    a.keys("right", 0.5, "left", 0.5, "down", 0.5, "up", 8)
    w1, w2 = both_where(a, b)
    check(bool(w1 and w2 and w1[2] == 2 and w2[2] == 2 and w1[1] > wa[1] + 300 and abs(w1[1] - w2[1]) <= FRAME_TOL), "frames keep moving on both", "%s / %s" % (w1, w2))

    # 8. the joiner's game is killed and started again: it is offered the match, F2 takes it back in
    # (it replays the match from its start, the saves and the loads in it too)
    import subprocess
    mb = b.mark()
    subprocess.run(["taskkill", "/F", "/PID", str(b.pid)], capture_output=True)
    time.sleep(6)
    pid = w.start(2)
    open(os.path.join(OUT, "pids.txt"), "w").write("%d %d" % (a.pid, pid))
    time.sleep(16)
    b = Game(2, pid)
    check(bool(b.wait_log(r"a match can be rejoined", mb, 10)), "the joiner's game killed: started again, it offers the match")
    b.shot("rejoin_offer")
    b.keys("f2")
    r = b.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)[^\n]*", mb, 90)
    check(bool(r and "back in the match" in r.group(0)), "F2: it rejoins", r.group(0) if r else "no rejoin line")
    time.sleep(4)
    w1, w2 = both_where(a, b)
    check(bool(w1 and w2 and w1[0] == w2[0] and w1[2] == 2 and w2[2] == 2 and abs(w1[1] - w2[1]) <= FRAME_TOL), "together after the rejoin", "%s / %s" % (w1, w2))
    ma, mb = a.mark(), b.mark()
    a.cmd("save 1")
    a.wait_log(r"online save \d+: slot", ma, 8); b.wait_log(r"online save \d+: slot", mb, 8)
    time.sleep(0.5)
    sa, sb = save_lines(a, ma), save_lines(b, mb)
    check(len(sa) == 1 and len(sb) == 1 and sa[0][2:] == sb[0][2:] and bool(sa[0][3]), "a save after the rejoin: the same frame and checksum on both", "%s / %s" % (sa, sb))

    for g, s0 in ((a, start_a), (b, start_b)):
        bad = [l for l in g.log()[s0:].splitlines() if BAD.search(l)]
        check(not bad, "game %d: no desync or lost-connection lines" % g.n, "; ".join(bad[:3]))
        # (a failed check inside PFO.dll stops the game in the runner's "Code Error" box: no log line)
        st = w.state_of(g.pid)
        check("Code Error" not in st, "game %d: no error box" % g.n, " ".join(st.split())[:400])
    a.shot("end"); b.shot("end")
    return check


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else ""
    if what == "start": start()
    elif what == "test":
        c = test(*games())
        print("PASSED" if not c.failed else "FAILED: " + "; ".join(c.failed))
        sys.exit(1 if c.failed else 0)
    elif what == "all":
        c = test(*start())
        print("PASSED" if not c.failed else "FAILED: " + "; ".join(c.failed))
        good = w.stop()
        sys.exit(1 if c.failed or not good else 0)
    elif what == "stop": sys.exit(0 if w.stop() else 1)
    else: print(__doc__)
