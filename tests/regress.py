"""Every test, one after another, with one PASS/FAIL table at the end.

  python tests/regress.py [quick|full] [--only name,name] [--skip name,name]

quick (the default): the Windows rejoin tests (library, in a game, through the relay), the pause
seat test, the two-game match (duo.py), cross-play both ways, quick save and DISCONNECT on both,
rewind on both, every online scenario on the two emulators (mp_regress.py), and a 5-game sample of both sweeps with every save through bytes and back. full: the same with
both sweeps over all 50 games. Needs both BlueStacks instances (5555, 5565) with the Android build
installed, and the Windows build in work/windows (tests/win/wingame.py; nothing runs in your own
folders). Takes a few hours; each test's own output goes to work/regress/<name>.txt.

(trio.py, three players, isn't in it: it can't pass on one PC yet, see docs/TESTING.md.)

Each test runs as its own process; it passes when it exits with 0. The table goes to the screen
and to work/regress/summary.txt. The exit code is 1 when anything failed.
"""
import os, sys, time, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "work", "regress")
WIN = os.path.join(HERE, "win")
SAMPLE = "1,3,8,24,40"

mode = "full" if "full" in sys.argv[1:] else "quick"
def arg(flag):
    return sys.argv[sys.argv.index(flag) + 1].split(",") if flag in sys.argv else None
only, skip = arg("--only"), arg("--skip") or []

# name, folder, command, extra environment, time limit (minutes)
TESTS = [
    ("rejoin-library",  WIN,  ["rejoin.py", "library"], {}, 30),
    ("rejoin-game",     WIN,  ["rejoin.py", "game"], {}, 30),
    ("rejoin-relay",    WIN,  ["rejoin.py", "game"], {"RELAY": "1"}, 40),
    ("rejoin-replay",   WIN,  ["rejoin.py", "game"], {"REJOIN_REPLAY": "1"}, 40),
    ("pauseseat",       WIN,  ["pauseseat.py", "5565"], {}, 40),
    ("duo",             WIN,  ["duo.py", "all"], {}, 40),
    ("seats-online",    WIN,  ["seats_online.py"], {}, 40),
    ("cross-win",       WIN,  ["cross.py", "win", "5565"], {}, 50),
    ("cross-android",   WIN,  ["cross.py", "android", "5565"], {}, 50),
    ("win-quickslot",   WIN,  ["quickslot.py"], {}, 20),
    ("win-disconnect",  WIN,  ["disconnect.py"], {}, 20),
    ("win-rewind",      WIN,  ["rewind.py"], {}, 20),
    ("quickslot",       HERE, ["quickslot.py"], {}, 20),
    ("disconnect",      HERE, ["disconnect.py"], {}, 20),
    ("rewind",          HERE, ["rewind_test.py"], {}, 20),
    ("mp-regress",      HERE, ["mp_regress.py"], {}, 90),
    ("win-sweep",       WIN,  ["sweep.py", "all" if mode == "full" else SAMPLE, os.path.join(OUT, "win-sweep-results.txt"), "library"] + (["x5"] if mode == "full" else []), {"SNAPWIRE": "1"}, 240 if mode == "full" else 40),
    ("android-sweep",   HERE, ["sweep4.py", ",".join(str(n) for n in range(1, 51)) if mode == "full" else SAMPLE, os.path.join(OUT, "android-sweep-results.txt"), "library"], {}, 600 if mode == "full" else 80),
]

os.makedirs(OUT, exist_ok=True)
rows = []
t_all = time.time()
for name, folder, cmd, env, limit in TESTS:
    if (only and name not in only) or name in skip: continue
    log = os.path.join(OUT, name + ".txt")
    t0 = time.time()
    print("%-16s ..." % name, end="", flush=True)
    with open(log, "w", encoding="utf-8", errors="replace") as f:
        try:
            r = subprocess.run([sys.executable, "-u"] + cmd, cwd=folder, env={**os.environ, **env}, stdout=f, stderr=subprocess.STDOUT, timeout=limit * 60)
            result = "PASS" if r.returncode == 0 else "FAIL"
        except subprocess.TimeoutExpired:
            result = "TIMEOUT"
    mins = (time.time() - t0) / 60
    # the test's own last line says what failed
    last = ""
    try:
        lines = [l.strip() for l in open(log, encoding="utf-8", errors="replace") if l.strip()]
        fails = [l for l in lines if l.startswith("FAIL")]
        last = (fails[0] if fails else lines[-1] if lines else "")[:150]
    except OSError: pass
    print("\r%-16s %-7s %5.1f min  %s" % (name, result, mins, "" if result == "PASS" else last), flush=True)
    rows.append((name, result, mins, last))

bad = [r for r in rows if r[1] != "PASS"]
summary = ["%-16s %-7s %5.1f min  %s" % (n, res, m, "" if res == "PASS" else l) for n, res, m, l in rows]
summary.append("%d of %d passed in %.0f min (%s)" % (len(rows) - len(bad), len(rows), (time.time() - t_all) / 60, mode))
with open(os.path.join(OUT, "summary.txt"), "w", encoding="utf-8") as f: f.write("\n".join(summary) + "\n")
print(summary[-1])
sys.exit(1 if bad else 0)
