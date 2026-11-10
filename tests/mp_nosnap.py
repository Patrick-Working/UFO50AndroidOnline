# A match without snapshots between two BlueStacks instances (host 5555, joiner 5565): what two
# phones do in a match that has a Windows player in it, without the Windows player.
# The host asks for it with the test command "nosnap" (as a device without snapshots does with
# command 246). Then: an online save (the same frame and checksum on both, no snapshot taken), a
# game for two, a save there, a load proposed by the joiner and accepted by the host (the match
# restarts from the save by replay on both and they connect again), a load back to the first save
# proposed by the host, and a look at both logs for desyncs, crashes and failed assertions.
# Prints "mp_nosnap: PASS" or "mp_nosnap: FAIL (why)"; the exit code is 0 or 1.
# Usage: mp_nosnap.py [--no-start]
import re, sys, time
import mp_test as m

H, J = m.H, m.J
WHERE = r'where: game (-?\d+), frame (\d+), clients (\d+)'


def wait(p, pat, since, seconds=20):
    end = time.time() + seconds
    while time.time() < end:
        r = re.search(pat, m.log(p)[since:])
        if r: return r
        time.sleep(0.4)
    return None


def mark(p): return len(m.log(p))


def where(p):
    at = mark(p); m.cmd(p, 'where')
    r = wait(p, WHERE, at, 6)
    return tuple(int(x) for x in r.groups()) if r else None


def saves(p, since):
    t = m.log(p)[since:]
    sums = dict(re.findall(r'save slot (\d+): frame \d+, \d+ bytes, checksum ([0-9a-f]+)', t))
    return [(int(i), int(s), int(f), sums.get(s)) for i, s, f in re.findall(r'online save (\d+): slot (\d+) at frame (\d+): ok', t)]


def loads(p, since):
    return [(int(s), int(f), v == 'verified') for s, f, v in re.findall(r'load slot (\d+): reached frame (\d+) .*: (verified|MISMATCH)', m.log(p)[since:])]


def save(asker, fails, what):
    mh, mj = mark(H), mark(J)
    m.cmd(asker, 'save 1')
    for p, at in ((H, mh), (J, mj)): wait(p, r'online save \d+: slot \d+ at frame \d+: ok', at, 8)
    time.sleep(1)
    sh, sj = saves(H, mh), saves(J, mj)
    print(what, 'host', sh, 'join', sj, flush=True)
    if not (len(sh) == 1 and len(sj) == 1 and sh[0][0] == sj[0][0] and sh[0][2:] == sj[0][2:] and sh[0][3]):
        fails.append(f'{what}: not the same save on both ({sh} / {sj})'); return None
    for p, at, n in ((H, mh, 'host'), (J, mj, 'joiner')):
        if re.search(r'captur|snapshot', m.log(p)[at:]): fails.append(f'{what}: the {n} took a snapshot')
    return sh[0], sj[0]


def load(asker, other, slot, frame, fails, what):
    mh, mj = mark(H), mark(J)
    m.cmd(asker, f'load {slot}')
    if not wait(other, r'command 245 \(\d+\)', mh if other == H else mj, 8): fails.append(f'{what}: the load never reached the other phone'); return
    time.sleep(1); m.cmd(other, 'accept')
    for p, at in ((H, mh), (J, mj)): wait(p, r'load slot \d+: reached frame', at, 90)
    lh, lj = loads(H, mh), loads(J, mj)
    print(what, 'host', lh, 'join', lj, flush=True)
    if not (len(lh) == 1 and len(lj) == 1 and lh[0][1] == frame and lj[0][1] == frame and lh[0][2] and lj[0][2]):
        fails.append(f'{what}: not replayed to frame {frame} and verified on both ({lh} / {lj})')
    time.sleep(8)
    wh, wj = where(H), where(J)
    print(what, 'after: host', wh, 'join', wj, flush=True)
    if not (wh and wj and wh[0] == wj[0] and wh[2] == 2 and wj[2] == 2 and abs(wh[1] - wj[1]) <= 120 and wh[1] > frame):
        fails.append(f'{what}: not together afterwards ({wh} / {wj})')


def main():
    fails = []
    if '--no-start' not in sys.argv: m.start()
    for p in (H, J): m.adb(p, 'logcat', '-G', '16M'); m.adb(p, 'logcat', '-c')
    m.cmd(H, 'nosnap')
    for p, n in ((H, 'host'), (J, 'joiner')):
        if not wait(p, r"a player's device has no snapshots", 0, 12): fails.append(f'the {n} never heard that the match has no snapshots')
    if fails: return fails
    m.keys(H, 'DPAD_UP', 1.5)                                  # (ends the library's demo reel if it started)
    lib = save(H, fails, 'library save')
    if not lib: return fails
    m.keys(H, 'DPAD_RIGHT', 'DPAD_RIGHT', 'Z', 8, 'Z', 3, 'DPAD_DOWN', 'Z', 6)   # the third game: for two players
    for _ in range(3): m.keys(H, 'DPAD_RIGHT', 'Z', 'X'); m.keys(J, 'DPAD_LEFT', 'Z', 'X')
    wh, wj = where(H), where(J)
    print('in a game: host', wh, 'join', wj, flush=True)
    if not (wh and wj and wh[0] == wj[0] and wh[0] > 0 and wh[2] == 2): fails.append(f'not both in the same game ({wh} / {wj})'); return fails
    game = save(J, fails, "the joiner's save in the game")
    if not game: return fails
    for _ in range(4): m.keys(H, 'DPAD_RIGHT', 'Z', 'X'); m.keys(J, 'DPAD_LEFT', 'Z', 'X')
    load(J, H, game[1][1], game[1][2], fails, "the joiner's load of it")
    for _ in range(3): m.keys(H, 'DPAD_RIGHT', 'Z', 'X'); m.keys(J, 'DPAD_LEFT', 'Z', 'X')
    load(H, J, lib[0][1], lib[0][2], fails, "the host's load of the library save")
    time.sleep(10)
    for p, n in ((H, 'host'), (J, 'joiner')):
        lines = m.full(p).splitlines()
        bad = [x for x in lines if re.search(r'(?i)desync|ran out of input|GoingOffline|MISMATCH', x)]
        crash = [x for x in lines if re.search(r'FATAL|SIGSEGV|Assertion failed', x)]
        if bad: fails.append(f'{n}: {bad[0].strip()[-120:]}')
        if crash: fails.append(f'{n}: crash or failed assertion: {crash[0].strip()[-120:]}')
        if re.search(r'online load of snapshot', m.log(p)): fails.append(f'the {n} loaded by snapshot')
    return fails


if __name__ == '__main__':
    fails = main()
    print('mp_nosnap: PASS' if not fails else 'mp_nosnap: FAIL (' + '; '.join(fails) + ')')
    sys.exit(1 if fails else 0)
