"""Two versions of the Windows build can't share a lobby.

  python vergate.py <folder of the other version>

Runs the join twice, boxed in like every test game (wingame.py): the other version hosts and this
one joins, then the other way round. Each time the join has to be refused with the log line that
names the versions, both games have to stay up, and the host has to be alone in its lobby after.
"""
import os, re, sys, time
import wingame as w
import duo

NEW = w.GAME_DIR


def start_from(n, game_dir):
    w.GAME_DIR = game_dir
    try: return w.start(n)
    finally: w.GAME_DIR = NEW


def stop_all(dirs):
    good = True
    for d in dirs:
        w.GAME_DIR = d
        try: good = w.stop() and good
        finally: w.GAME_DIR = NEW
    return good


def alive(pid, game_dir):
    return os.path.normcase(w.exe_of(pid)) == os.path.normcase(os.path.join(game_dir, "ufo50.exe"))


def one(host_dir, join_dir, name, check):
    for n in (1, 2):
        for f in ("rejoin.txt", "ss_cmd"):
            try: os.remove(os.path.join(duo.folder(n), f))
            except OSError: pass
    pa = start_from(1, host_dir); time.sleep(1); pb = start_from(2, join_dir)
    time.sleep(14)
    a, b = duo.Game(1, pa), duo.Game(2, pb)
    duo.to_lobbies(a)
    a.keys("down", 0.5, "z", 6)                       # HOST PRIVATE
    status = duo.menu_status(a)
    m = re.search(r"CODE (\w+)", status)
    if not m: raise RuntimeError("no invite code in the host's menu")
    code = m.group(1)
    duo.to_lobbies(b)
    at = b.mark()
    b.keys("down", 0.4, "down", 0.4, "z", 1.5)       # JOIN BY CODE
    if not b.wait_log(r"menu: open", at, 5): raise RuntimeError("JOIN BY CODE did not open the code box")
    b.keys(*list(code.lower()), 0.3, "enter")
    if not b.wait_log(r"message: CODE \w+ ADDED", at, 5): raise RuntimeError("the code was not taken")
    at = b.mark()
    if not b.wait_log(r"okplay list in \d+ ms: \d+ rooms, [1-9]\d* usable", at, 15): raise RuntimeError("the lobby never showed in the joiner's list")
    ma, mb = a.mark(), b.mark()
    time.sleep(0.8)
    duo.onto_lobby(b)
    b.keys("z", 8)                                    # join it
    host_new = host_dir == NEW
    if host_new:
        said = a.wait_log(r"can't join: another version of the build \(theirs (\d+), ours (\d+)\)", ma, 10)
        check(bool(said), "%s: the host refuses the joiner" % name, said.group(0) if said else "")
    else:
        # (a host from before the version number lets anyone in, and the joiner leaves; one that has
        # a number of its own refuses the joiner itself)
        said = b.wait_log(r"can't join: the host has another version of the build \(theirs (\d+), ours (\d+)\)", mb, 10) or a.wait_log(r"can't join: another version of the build \(theirs (\d+), ours (\d+)\)", ma, 1)
        check(bool(said), "%s: the joiner leaves the host, or the host refuses it" % name, said.group(0) if said else "")
    time.sleep(4)
    a.shot("gate_%s_host" % name); b.shot("gate_%s_join" % name)
    check(alive(pa, host_dir) and alive(pb, join_dir), "%s: both games still running" % name)
    status = duo.menu_status(a)
    check("CODE" in status, "%s: the host still has its lobby" % name, re.sub(r"CODE \w+", "CODE <code>", status))
    for g in (a, b):
        t = g.log(ma if g is a else mb)
        check("STARTING SESSION" not in t and "Assert" not in t, "%s: game %d started no session" % (name, g.n))
        print("   game %d: %s" % (g.n, " | ".join(l[-110:] for l in t.splitlines() if re.search(r"PFO LAN|lobby|Lobby", l))[:900]))
    # the joiner can still use the menu: back to the list and host a lobby of its own
    at = b.mark()
    b.keys("x", 1.5)
    duo.to_lobbies(b) if not b.wait_log(r"okplay list in", at, 6) else None
    stop_all((host_dir, join_dir))


if __name__ == "__main__":
    if len(sys.argv) < 2: print(__doc__); sys.exit(2)
    old = os.path.abspath(sys.argv[1])
    for f in w.STEAM_FILES:
        if os.path.exists(os.path.join(old, f)): raise SystemExit("refusing: %s is in %s" % (f, old))
    check = duo.Checks()
    stop_all((NEW, old))
    try:
        one(old, NEW, "old_hosts", check)
        one(NEW, old, "new_hosts", check)
    finally:
        good = stop_all((NEW, old))
    print("PASSED" if not check.failed and good else "FAILED: " + "; ".join(check.failed))
    sys.exit(1 if check.failed or not good else 0)
