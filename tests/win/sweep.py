"""Save-state sweep of the Windows build (the Windows twin of tests/sweep4.py). Per game: a fresh
boxed-in test game, solo session, open game N through the library (real key presses), mash, save 1,
mash, leave to the library (or on into another game, or stay), load 1 (from its snapshot, checked
against the next 120 frames), mash, save 2, then a replay-only load of save 2 (which replays the
session through the snapshot load).
Every step checks where it is (the game's internal id); a run that isn't where it should be is
retried from a fresh test game (up to 3 tries) instead of being reported as a result.
A game is good when its line says "snapshot ok ..." and "replay through it ok ...". The last line
counts the games that are not; the exit code is 1 when there is one, else 0.

  python sweep.py 1,2,3 out.txt [library|other|same] [box]     these games, in test game <box> (11)
  python sweep.py all out.txt [library|other|same] x5          all 50, five test games at a time

The test games are muted (their own config.json, in their own boxed-in folders) and open behind
whatever window is in front. Nothing here runs in your own user folders (see wingame.py).
"""
import os, re, sys, time, random, shutil, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wingame

# game number in the library -> the game's internal id
ID = {1:40,2:20,3:34,4:49,5:27,6:29,7:15,8:48,9:2,10:5,11:26,12:50,13:11,14:22,15:18,16:4,17:3,18:6,19:28,20:16,21:21,22:10,23:32,24:46,25:36,
      26:43,27:19,28:13,29:7,30:44,31:23,32:1,33:9,34:30,35:38,36:42,37:35,38:24,39:17,40:12,41:33,42:39,43:31,44:37,45:41,46:25,47:14,48:47,49:8,50:45}
MODES = ("library", "other", "same")


def setup(box):
    """the box's own game settings: sound off, in a window"""
    d = os.path.join(wingame.home(box), "ufo50_online")
    os.makedirs(d, exist_ok=True)
    cfg = os.path.join(d, "config.json")
    if not os.path.exists(cfg):
        # the settings another test game wrote (the game writes the file on its first run)
        for other in ("1", "2"):
            src = os.path.join(wingame.SANDBOX, other, "AppData", "Local", "ufo50_online", "config.json")
            if os.path.exists(src): shutil.copyfile(src, cfg); break
    if os.path.exists(cfg):
        s = open(cfg, encoding="utf-8").read()
        s = re.sub(r'("volume"\s*:\s*)[0-9.]+', r'\g<1>0.0', s)
        s = re.sub(r'("fullscreen"\s*:\s*)[0-9.]+', r'\g<1>0.0', s)
        open(cfg, "w", encoding="utf-8").write(s)
    return d


class Game:
    def __init__(self, box, out):
        self.box, self.dir, self.out = box, setup(box), out
        self.logpath = os.path.join(self.dir, "pfo_online.log")
        self.pid = self.hwnd = None
        self.at = 0
        self.rnd = random.Random(50)

    def say(self, s):
        print(s, flush=True)
        self.out.write(s + "\n")

    # ---- the test game ----
    def start(self):
        if self.pid: wingame.close(self.pid)
        self.mark()
        self.pid = wingame.start(self.box, front=False)
        t = time.time()
        while time.time() - t < 30:
            if [w for w in wingame.windows_of([self.pid]) if w[1] == "YYGameMakerYY"]: break
            time.sleep(0.5)
        else: return False
        self.hwnd = wingame.game_window(self.pid)
        return True

    def stop(self):
        if self.pid: wingame.close(self.pid)
        self.pid = None

    def key(self, k, wait=0.0):
        wingame.press(k, self.hwnd)
        if wait: time.sleep(wait)

    def mash(self, seconds):
        t = time.time()
        while time.time() - t < seconds:
            self.key(self.rnd.choice(["left", "right", "up", "down", "z", "x", "right", "z"]))

    # ---- its log (it grows from run to run: only what came after mark() is read) ----
    def mark(self):
        self.at = os.path.getsize(self.logpath) if os.path.exists(self.logpath) else 0

    def log(self):
        if not os.path.exists(self.logpath): return ""
        with open(self.logpath, "rb") as f:
            f.seek(self.at)
            return f.read().decode("utf-8", "replace")

    def cmd(self, c):
        """a test command (files/ss_cmd: the game takes it within half a second and removes the file)"""
        path = os.path.join(self.dir, "ss_cmd")
        t = time.time()
        while os.path.exists(path) and time.time() - t < 3: time.sleep(0.1)
        open(path, "w", newline="\n").write(c + "\n")

    def where(self, tries=5):
        for _ in range(tries):
            self.mark(); self.cmd("where")
            t = time.time()
            while time.time() - t < 2.5:
                m = re.findall(r"where: game (-?\d+), frame (\d+), clients \d+, room ([\w:.]*)", self.log())
                if m: self.room = m[-1][2]; return int(m[-1][0]), int(m[-1][1])
                time.sleep(0.2)
        self.room = None
        return None, None

    def menu(self):
        """where the library's own menus are: (state, substate) of the game's oLibrary, or None in a game.
        States: 0-2 the intros, 3 the logo, 4 the profiles, 5 the games, 6 the pause before them."""
        g, _ = self.where()
        m = re.fullmatch(r"rmLibrary:(\d+)\.(-?\d+)", self.room or "")
        return (int(m.group(1)), int(m.group(2))) if m and g == 0 else None

    def to_library(self, timeout=60):
        """from wherever the game's start-up is (intros, logo, profiles) to the list of games"""
        t = time.time(); last = None; still = 0
        while time.time() - t < timeout:
            m = self.menu()
            if m is None: return False
            if m[0] == 5:
                # the list is there once it has stopped moving
                still = still + 1 if m == last else 0
                if still >= 2: return True
            elif m[0] in (0, 1, 2, 3, 4): self.key("z", 0.6)
            last = m
            time.sleep(0.3)
        return False

    def in_library(self):
        m = self.menu()
        return m is not None and m[0] == 5

    def wait_ready(self, timeout=60):
        t = time.time()
        while time.time() - t < timeout:
            if self.where(1)[0] is not None: return True
        return False

    def diag(self, n, what):
        """everything about a run that gave no result: the end of the log, the screen"""
        base = os.path.join(os.path.dirname(os.path.abspath(self.out.name)), "noresult_%d_%s_%s" % (n, what, time.strftime("%H%M%S")))
        try:
            with open(self.logpath, "rb") as f:
                f.seek(max(0, os.path.getsize(self.logpath) - 20000))
                open(base + "_log.txt", "wb").write(f.read())
            open(base + "_state.txt", "w", encoding="utf-8").write(wingame.state_of(self.pid))
            wingame.shot(base + "_screen.png", self.hwnd)
        except Exception: pass
        self.say("   (no result: kept as %s_*)" % os.path.basename(base))

    # ---- the game's own menus ----
    def leave_game(self):   # pause menu > Back to Library > "Really close game?" yes
        self.key("esc", 1.2); self.key("down", 0.3); self.key("down", 0.3); self.key("z", 1.0); self.key("z", 6)

    def open_game(self, n):   # from the library's first game
        for _ in range((n - 1) // 10): self.key("down", 0.3)
        for _ in range((n - 1) % 10): self.key("right", 0.3)
        time.sleep(1); self.key("z", 9)
        for _ in range(3): self.key("z", 2)

    def run_once(self, n, mode):
        if not self.start(): return None, "the test game did not start"
        if not self.wait_ready(): return None, "the test game did not answer"
        if "snapshots: on" not in self.log_all_run(): return None, "no snapshots in this test game"
        if not self.to_library(): return None, "did not reach the library at start (%s)" % self.room
        self.cmd("solo"); time.sleep(3)
        if not self.to_library(20): return None, "not in the library after starting solo play (%s)" % self.room
        self.open_game(n)
        self.mash(6)
        g, f = self.where()
        if g is None: self.diag(n, "where")
        if g != ID[n]: return None, "not in game %d (%s)" % (n, g)
        self.mark()
        self.cmd("save 1"); self.mash(3.5)
        so = self.log()
        if not re.search(r"save slot 1: frame", so):
            self.diag(n, "save1")
            r = re.search(r"save slot 1: (refused[^\n]*)", so)
            return None, "save 1 didn't happen" + (" (" + r.group(1).strip() + ")" if r else "")
        # a save's snapshot is good to load once the 120 frames after it checked out: with the PC busy
        # that takes longer than the wait above (a load before then replays, which isn't what's tested)
        t0 = time.time()
        while "snapshot 1: ready" not in self.log() and time.time() - t0 < 20: self.mash(0.5)
        if mode != "same": self.leave_game()
        if mode == "other":
            self.key("left" if (n - 1) % 10 == 9 else "right", 0.6); self.key("z", 9)
            for _ in range(3): self.key("z", 2)
            self.mash(2)
        g2, f2 = self.where()
        if mode == "same" and g2 != ID[n]: return None, "not in game %d at the load (%s)" % (n, g2)
        if mode == "library" and not (g2 == 0 and (self.room or "").startswith("rmLibrary:5.")): return None, "did not reach the library (%s, %s)" % (g2, self.room)
        if mode == "other" and (g2 is None or g2 <= 0 or g2 == g): return None, "did not reach another game (%s)" % g2
        self.mark()
        self.cmd("load 1")
        t0 = time.time(); snap = "no result"; o = ""
        while time.time() - t0 < 60:
            time.sleep(0.5)
            o = self.log()
            if "from its snapshot in" in o and "verified" in o: snap = "ok " + re.search(r"from its snapshot in (\d+) ms", o).group(1) + " ms"; break
            m = re.search(r"snapshot didn't check out \(([^)]*)\)", o)
            if m and "reached frame" in o: snap = "FAILED (" + m.group(1) + ")"; break
            if "capture failed" in o or "restore failed" in o: snap = "capture/restore error"; break
            # a load straight to the replay (there was no snapshot to use): a result of its own
            m = re.search(r"load slot 1: reached frame .*(verified|MISMATCH)", o)
            if "load slot 1: replaying" in o and m and "didn't check out" not in o: snap = "NO SNAPSHOT (replayed: " + m.group(1) + ")"; break
            m = re.search(r"load slot 1: (refused[^\n]*|no such save)", o)
            if m: snap = "REFUSED (" + m.group(1).strip() + ")"; break
        if snap == "no result": self.diag(n, "load"); return None, "load gave no result"
        if snap.startswith("NO SNAPSHOT") or snap.startswith("REFUSED"): self.diag(n, "load_" + snap.split()[0].lower())
        warns = [l.strip()[:140] for l in o.splitlines() if re.search(r"\bW[ /]+PFOSTATE", l) and "didn't check out" not in l]
        g3, _ = self.where()
        if g3 != ID[n]: return None, "after the load not in game %d (%s)" % (n, g3)
        self.mark()
        self.mash(3); self.cmd("save 2"); self.mash(1.5)
        # the save must have happened before the replay of it is asked for
        t0 = time.time()
        while time.time() - t0 < 10 and not re.search(r"save slot 2: (frame|refused)", self.log()): time.sleep(0.3)
        so = self.log()
        if not re.search(r"save slot 2: frame", so):
            self.diag(n, "save2")
            r = re.search(r"save slot 2: (refused[^\n]*)", so)
            return None, "save 2 didn't happen" + (" (" + r.group(1).strip() + ")" if r else "")
        self.mark(); self.cmd("rload 2")
        t0 = time.time(); rep = None
        while time.time() - t0 < 120:
            time.sleep(1)
            o = self.log()
            m = re.search(r"reached frame (\d+) in (\d+) ms[^,]*, checksum \w+ vs saved \w+, (\d+) replay checksums(.*)", o)
            if m: rep = ("ok" if "verified" in m.group(4) else "FAILED" + m.group(4).strip()[:60]) + " (%s frames in %s ms, %s checks)" % m.group(1, 2, 3); break
            r = re.search(r"load slot 2: (refused[^\n]*|no such save|the replay ended the session[^\n]*)", o)
            if r: self.diag(n, "replay_refused"); return None, "replay " + r.group(1).strip()
        if rep is None: self.diag(n, "replay"); return None, "replay gave no result"
        return "snapshot %s | replay through it %s" % (snap, rep) + ("  | " + warns[0] if warns else ""), None

    def log_all_run(self):
        """the log since this test game started (where() moves the mark)"""
        with open(self.logpath, "rb") as f:
            f.seek(self.run_start)
            return f.read().decode("utf-8", "replace")


def good(line):
    return ": snapshot ok " in line and "| replay through it ok " in line


def worker(games, outpath, mode, box):
    out = open(outpath, "a", buffering=1, encoding="utf-8")
    g = Game(box, out)
    not_good = []
    try:
        for n in games:
            why = []; res = None
            for attempt in range(3):
                g.run_start = os.path.getsize(g.logpath) if os.path.exists(g.logpath) else 0
                res, problem = g.run_once(n, mode)
                if res: break
                why.append(problem)
            tag = "game %2d (internal %d, loaded from %s)" % (n, ID[n], mode)
            line = "%s: %s" % (tag, res) + ("  [retried: %s]" % "; ".join(why) if why else "") if res else "%s: HARNESS could not run it: %s" % (tag, "; ".join(why))
            g.say(line)
            if not good(line): not_good.append(n)
    finally:
        g.stop()
    return not_good


def main():
    if len(sys.argv) < 3 or sys.argv[1] in ("-h", "--help"): raise SystemExit(__doc__)
    games = list(range(1, 51)) if sys.argv[1] == "all" else [int(x) for x in sys.argv[1].split(",") if x]
    outpath = sys.argv[2]
    rest = sys.argv[3:]
    mode = next((a for a in rest if a in MODES), "library")
    many = next((int(a[1:]) for a in rest if re.fullmatch(r"x\d+", a)), 0)
    box = next((int(a) for a in rest if a.isdigit()), 11)
    if many > 1:
        # several test games at a time, each with its own box and its share of the games
        open(outpath, "a").close()
        before = len(open(outpath, encoding="utf-8").read().splitlines())
        procs = [subprocess.Popen([sys.executable, os.path.abspath(__file__), ",".join(str(n) for n in games[k::many]), outpath, mode, str(box + k), "--part"], stdout=subprocess.DEVNULL)
                 for k in range(many) if games[k::many]]
        for p in procs: p.wait()
        lines = [l for l in open(outpath, encoding="utf-8").read().splitlines()[before:] if l.startswith("game ")]
        done = {int(l.split()[1]): good(l) for l in lines}
        not_good = sorted(n for n in games if not done.get(n))
    else:
        not_good = worker(games, outpath, mode, box)
        if "--part" in rest: sys.exit(1 if not_good else 0)
    line = "%d of %d games good (loaded from %s)" % (len(games) - len(not_good), len(games), mode) + (", not good: " + ",".join(str(n) for n in not_good) if not_good else "")
    print(line, flush=True)
    open(outpath, "a", encoding="utf-8").write(line + "\n")
    wingame.check()
    sys.exit(1 if not_good else 0)


if __name__ == "__main__":
    main()
