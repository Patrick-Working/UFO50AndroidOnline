"""The version gate between an emulator (this version) and another version's Windows build.

  python xgate.py <folder of the other version> [port]

The emulator hosts and the other version joins, then the other way round. Both joins have to be
refused, with the line that names the versions in the emulator's log.
"""
import os, re, sys, time
import wingame as w
import duo, cross
import mp_test as m

NEW = w.GAME_DIR


def start_old(old):
    w.GAME_DIR = old
    try:
        if w.game_pids(): w.stop()
        for f in ("rejoin.txt", "ss_cmd"):
            try: os.remove(os.path.join(duo.folder(1), f))
            except OSError: pass
        return w.start(1)
    finally: w.GAME_DIR = NEW


def stop_old(old):
    w.GAME_DIR = old
    try: return w.stop()
    finally: w.GAME_DIR = NEW


def said_in_logcat(port, pattern, seconds=12):
    """in the whole logcat (the lobby's lines have the runner's tag), without the lines that hold addresses"""
    end = time.time() + seconds
    while time.time() < end:
        text = "\n".join(l for l in m.full(port).splitlines() if not duo.PRIVATE.search(l))
        r = re.search(pattern, text)
        if r: return r
        time.sleep(0.5)
    return None


def run(old, port, check):
    # 1. the emulator hosts, the other version joins
    d = cross.Droid(port); d.launch()
    pid = start_old(old)
    time.sleep(24)
    g = duo.Game(1, pid)
    d.to_lobbies()
    m.keys(port, "Z", 6)                              # HOST PUBLIC
    duo.to_lobbies(g)
    at = g.mark()
    if not g.wait_log(r"okplay list in \d+ ms: \d+ rooms, [1-9]\d* usable", at, 20): raise RuntimeError("the lobby never showed in the Windows game's list")
    md = d.mark(); g0 = g.mark()
    time.sleep(0.8)
    duo.onto_lobby(g)
    g.keys("z", 8)                                    # join it
    said = said_in_logcat(port, r"can't join: another version of the build \(theirs (\d+), ours (\d+)\)")
    check(bool(said), "the emulator hosts: it refuses the other version", said.group(0) if said else "")
    g.shot("xgate_1"); d.shot("xgate_1")
    check(d.where() is not None, "the emulator's app still answers")
    stop_old(old)

    # 2. the other version hosts, the emulator joins
    d.launch()
    pid = start_old(old)
    time.sleep(24)
    g = duo.Game(1, pid)
    duo.to_lobbies(g)
    g.keys("z", 6)                                    # HOST PUBLIC
    d.to_lobbies()
    if not d.sees_a_lobby(): raise RuntimeError("the emulator never saw the lobby")
    md = d.mark(); g0 = g.mark()
    m.keys(port, "Z", 8)                              # join it
    # (a host from before the version number lets anyone in, and the emulator leaves; one that has a
    # number of its own refuses the emulator itself)
    said = said_in_logcat(port, r"can't join: the host has another version of the build \(theirs (\d+), ours (\d+)\)") or re.search(r"can't join: another version of the build \(theirs (\d+), ours (\d+)\)", g.log(g0))
    check(bool(said), "the other version hosts: the emulator leaves, or the host refuses it", said.group(0) if said else "")
    g.shot("xgate_2"); d.shot("xgate_2")
    check(d.where() is not None, "the emulator's app still answers")
    crash = [l for l in m.full(port).splitlines() if re.search(r"Assertion failed|FATAL EXCEPTION|signal \d+ \(SIG", l)]
    check(not crash, "the emulator: no crash or failed assertion", crash[0][-140:] if crash else "")


if __name__ == "__main__":
    if len(sys.argv) < 2: print(__doc__); sys.exit(2)
    old = os.path.abspath(sys.argv[1])
    port = sys.argv[2] if len(sys.argv) > 2 else "5565"
    for f in w.STEAM_FILES:
        if os.path.exists(os.path.join(old, f)): raise SystemExit("refusing: %s is in %s" % (f, old))
    check = duo.Checks()
    try: run(old, port, check)
    except RuntimeError as e: check(False, str(e))
    finally:
        good = stop_old(old)
        m.adb(port, "shell", "am", "force-stop", cross.PKG)
    print("PASSED" if not check.failed and good else "FAILED: " + "; ".join(check.failed))
    sys.exit(1 if check.failed or not good else 0)
