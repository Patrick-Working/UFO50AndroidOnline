"""Fast rejoins into every game, checked item by item: two boxed-in Windows test games in one private
lobby (one player each). For each game: the host opens it from the online library, both play a few
seconds, the joiner's game is killed and comes back by a fast rejoin (the match's state through
bytes, into a fresh process), and both trace the whole game state for the frames after it
(files/tracerejoin). tests/tracediff.py then compares the two traces: a game is good when they are
the same on every frame both have, and neither game logs a desync. Then back to the library and on
to the next game; a match that's lost is started again.

  python rejoin_sweep.py 1,2,3 [out.txt]      these games (library numbers)
  python rejoin_sweep.py all [out.txt]

Each game's traces and the differ's report go to work/rejoin_sweep/g<n>/. The last line counts the
games that are not good; the exit code is 1 when there is one, else 0. Uses wingame.py's sandboxes
1 and 2 (WINGAME_BACK=1 keeps their windows behind yours).
"""
import os, re, sys, time, shutil, subprocess
os.environ.setdefault("TRACE", "60")   # (duo.launch: both trace this many frames from a rejoin)
import duo
import wingame as w
from sweep import ID

HERE = os.path.dirname(os.path.abspath(__file__))
DIFF = os.path.join(os.path.dirname(HERE), "tracediff.py")
OUT = os.path.join(w.OUTDIR, "rejoin_sweep")
LEAVE_FRAMES = int(os.environ.get("TRACE_LEAVE", "200"))   # (frames traced from leaving a game)
MASH = ["z", "x", "left", "right", "up", "down", "z", "right", "x", "left"]


def trace_dir(n, tag): return os.path.join(duo.folder(n), "trace", tag)


def clear_traces():
    for n, tag in ((1, "host"), (2, "joiner")): shutil.rmtree(trace_dir(n, tag), ignore_errors=True)


def mash(a, b, seconds):
    t = time.time(); i = 0
    while time.time() - t < seconds:
        a.keys(MASH[i % len(MASH)], 0.25); b.keys(MASH[(i * 3 + 1) % len(MASH)], 0.15); i += 1


def open_game(a, n, cur):   # from the game the library's cursor is on (it stays where it was)
    dr, dc = (n - 1) // 10 - (cur - 1) // 10, (n - 1) % 10 - (cur - 1) % 10
    for _ in range(abs(dr)): a.keys("down" if dr > 0 else "up", 0.4)
    for _ in range(abs(dc)): a.keys("right" if dc > 0 else "left", 0.4)
    a.keys("z", 8)
    for _ in range(3): a.keys("z", 2)


def leave_game(a):   # pause menu > back to library > yes
    a.keys("esc", 1.5, "down", 0.4, "down", 0.4, "z", 1.0, "z", 6)


def rejoin(a, b):
    """the joiner killed and back by a rejoin: (the new joiner, what happened)"""
    subprocess.run(["taskkill", "/F", "/PID", str(b.pid)], capture_output=True)
    time.sleep(5)
    pid = w.start(2); time.sleep(16)
    b2 = duo.Game(2, pid)
    at = b2.mark(); b2.keys("f2")
    r = b2.wait_log(r"rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)[^\n]*", at, 90)
    if not (r and "back in the match" in r.group(0)): return b2, "no rejoin (%s)" % (r.group(0) if r else "no rejoin line")
    fast = re.search(duo.FAST, b2.log(at))
    return b2, "" if fast else "rejoined, but by replay (not a fast rejoin)"


def one(a, b, n, cur):
    """one game: (verdict line, the joiner now, whether the match is still on)"""
    open_game(a, n, cur)
    g = a.where()
    for _ in range(3):   # (a game can take a while to open: ask again)
        if g and g[0] == ID[n]: break
        time.sleep(3); g = a.where()
    if not g or g[0] != ID[n]:
        return "NOT RUN    game %d: not in it (%s)" % (n, g), b, False   # (a new match: the cursor is lost)
    mash(a, b, 6)
    clear_traces()
    ma, mb0 = a.mark(), duo.mark(2)
    if os.environ.get("NOREJOIN"):   # (for comparison: no rejoin, both traced from here)
        a.cmd("trace %s" % os.environ["TRACE"]); b.cmd("trace %s" % os.environ["TRACE"]); why = ""
    else:
        b, why = rejoin(a, b)
        if not why and os.environ.get("TRACE_LATE"):   # (traced from a few seconds after the rejoin, not at it)
            mash(a, b, float(os.environ["TRACE_LATE"]))
            ma, mb0 = a.mark(), duo.mark(2)
            a.cmd("trace 60"); b.cmd("trace 60")
    if why: return "REJOIN     game %d (id %d): %s" % (n, ID[n], why), b, False
    # tracing slows both down (the whole state, every frame): play on until both have written theirs
    t = time.time()
    while time.time() - t < 240 and not all(re.search(r"trace: done", g_.log(m)) for g_, m in ((a, ma), (b, mb0))):
        mash(a, b, 3)
    bad = [l.strip()[-120:] for g_, m in ((a, ma), (b, mb0)) for l in g_.log(m).splitlines() if duo.BAD.search(l)]
    keep = os.path.join(OUT, "g%02d" % n)
    shutil.rmtree(keep, ignore_errors=True); os.makedirs(keep)
    for k, (gn, tag) in enumerate(((1, "host"), (2, "joiner"))):
        if os.path.isdir(trace_dir(gn, tag)): shutil.move(trace_dir(gn, tag), os.path.join(keep, tag))
    r = subprocess.run([sys.executable, DIFF, os.path.join(keep, "host"), os.path.join(keep, "joiner"), "--max", "25"], capture_output=True, text=True)
    open(os.path.join(keep, "diff.txt"), "w", encoding="utf-8").write(r.stdout + r.stderr)
    first = re.search(r"first difference: frame (\d+)[^\n]*\n((?:  .*\n){0,4})", r.stdout)
    span = re.search(r"in common: (\d+) frames", r.stdout)
    if r.returncode == 2: verdict = "NO TRACE   game %d (id %d): %s" % (n, ID[n], r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-200:])
    elif r.returncode == 1: verdict = "DIFFERS    game %d (id %d): from frame %s: %s" % (n, ID[n], first.group(1) if first else "?", " | ".join(x.strip() for x in (first.group(2) if first else "").splitlines()))
    elif bad: verdict = "DESYNC     game %d (id %d): traces the same (%s frames) but: %s" % (n, ID[n], span.group(1) if span else "?", bad[0])
    else: verdict = "ok         game %d (id %d): the same on all %s frames traced after the rejoin" % (n, ID[n], span.group(1) if span else "?")
    if bad and r.returncode == 1: verdict += " (and a desync line)"
    # then back to the library, traced too (after a rejoin, that's where a game's own state is
    # left behind and the library's taken up again)
    clear_traces()
    ma, mb = a.mark(), b.mark()
    a.cmd("trace %d" % LEAVE_FRAMES); b.cmd("trace %d" % LEAVE_FRAMES)
    time.sleep(1.5)
    leave_game(a)
    t = time.time()
    while time.time() - t < 240 and not all(re.search(r"trace: done", g_.log(m)) for g_, m in ((a, ma), (b, mb))):
        time.sleep(3)
    for gn, tag in ((1, "host"), (2, "joiner")):
        if os.path.isdir(trace_dir(gn, tag)): shutil.move(trace_dir(gn, tag), os.path.join(keep, "leave_" + tag))
    r2 = subprocess.run([sys.executable, DIFF, os.path.join(keep, "leave_host"), os.path.join(keep, "leave_joiner"), "--max", "25"], capture_output=True, text=True)
    open(os.path.join(keep, "leave_diff.txt"), "w", encoding="utf-8").write(r2.stdout + r2.stderr)
    lbad = [l.strip()[-120:] for g_, m in ((a, ma), (b, mb)) for l in g_.log(m).splitlines() if duo.BAD.search(l)]
    first2 = re.search(r"first difference: frame (\d+)[^\n]*\n((?:  .*\n){0,4})", r2.stdout)
    if r2.returncode == 1:
        verdict += "\n           LEAVE DIFFERS from frame %s: %s" % (first2.group(1) if first2 else "?", " | ".join(x.strip() for x in (first2.group(2) if first2 else "").splitlines()))
        if verdict.startswith("ok"): verdict = "DIFFERS" + verdict[2:]
    elif lbad:
        verdict += "\n           LEAVE DESYNC: %s" % lbad[0]
        if verdict.startswith("ok"): verdict = "DESYNC " + verdict[2:]
    back = a.where()
    return verdict, b, bool(back and back[0] == 0) and not lbad


def main():
    games = list(range(1, 51)) if sys.argv[1] == "all" else [int(x) for x in sys.argv[1].split(",")]
    outpath = sys.argv[2] if len(sys.argv) > 2 else os.path.join(OUT, "results.txt")
    os.makedirs(OUT, exist_ok=True)
    out = open(outpath, "a", encoding="utf-8")
    def say(s): print(s, flush=True); out.write(s + "\n"); out.flush()
    say("---- rejoin sweep %s: games %s" % (time.strftime("%Y-%m-%d %H:%M"), sys.argv[1]))
    a = b = None; lines = []; cur = 1
    for n in games:
        try:
            if a is None: a, b = duo.start(); cur = 1
            verdict, b, on = one(a, b, n, cur)
            cur = n
        except Exception as e:
            verdict, on = "ERROR      game %d: %s" % (n, str(e)[:200]), False
        say(verdict); lines.append(verdict)
        if not on:
            w.stop(); a = b = None
    w.stop()
    notgood = [l for l in lines if not l.startswith("ok")]
    say("%d of %d games not good" % (len(notgood), len(lines)))
    return 1 if notgood else 0


if __name__ == "__main__":
    sys.exit(main())
