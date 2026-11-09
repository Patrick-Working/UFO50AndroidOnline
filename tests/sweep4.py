# Cross-room save-state sweep with a self-checking harness. Per game: fresh app, solo session, open
# game N through the library (real key presses), mash, save 1, mash, leave to the library (or on into
# another game), load 1 (snapshot, 120-frame check), mash, save 2, replay-only load of save 2.
# Every step checks where it is (the game's internal id); a run that isn't where it should be is
# retried from a fresh app (up to 3 tries) instead of being reported as a result.
# Usage: sweep4.py <games, e.g. 1,2,3> <out.txt> [library|other|same]
import os, sys, time, subprocess, re, random; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import nav
P = '5555'; D = '/sdcard/Android/data/com.unofficial.ufo50/files'; ENV = {**os.environ, 'MSYS_NO_PATHCONV': '1'}
games = [int(x) for x in sys.argv[1].split(',') if x]; out = open(sys.argv[2], 'a', buffering=1)
MODE = sys.argv[3] if len(sys.argv) > 3 else 'library'
# game number in the library -> the game's internal id (from earlier sweeps)
ID = {1:40,2:20,3:34,4:49,5:27,6:29,7:15,8:48,9:2,10:5,11:26,12:50,13:11,14:22,15:18,16:4,17:3,18:6,19:28,20:16,21:21,22:10,23:32,24:46,25:36,26:43,27:19,28:13,29:7,30:44,31:23,32:1,33:9,34:30,35:38,36:42,37:35,38:24,39:17,40:12,41:33,42:39,43:31,44:37,45:41,46:25,47:14,48:47,49:8,50:45}
rnd = random.Random(50)
def cmd(c): subprocess.run([nav.ADB, '-s', '127.0.0.1:' + P, 'shell', f'echo {c} > {D}/ss_cmd'], env=ENV)
def log(): return subprocess.run([nav.ADB, '-s', '127.0.0.1:' + P, 'logcat', '-d', '-s', 'PFOSTATE:*'], capture_output=True, text=True, errors='replace').stdout
def say(s): print(s, flush=True); out.write(s + '\n')
def mash(seconds):
    t = time.time()
    while time.time() - t < seconds:
        nav.key(P, rnd.choice(['DPAD_LEFT', 'DPAD_RIGHT', 'DPAD_UP', 'DPAD_DOWN', 'Z', 'X', 'DPAD_RIGHT', 'Z']))
        time.sleep(0.05)
def where(tries=5):
    for _ in range(tries):
        nav.adb(P, 'logcat', '-c'); cmd('where'); time.sleep(2.0)
        m = re.findall(r'where: game (-?\d+), frame (\d+)', log())
        if m: return int(m[-1][0]), int(m[-1][1])
    return None, None
def wait_ready(timeout=60):   # the app answers test commands once PFO is running
    t = time.time()
    while time.time() - t < timeout:
        g, f = where(1)
        if g is not None: return True
    return False
def leave_game():   # pause menu > Back to Library > "Really close game?" yes
    nav.key(P, 'ESCAPE'); time.sleep(1.2); nav.key(P, 'DPAD_DOWN'); time.sleep(0.3); nav.key(P, 'DPAD_DOWN'); time.sleep(0.3)
    nav.key(P, 'Z'); time.sleep(1.0); nav.key(P, 'Z'); time.sleep(6)
def open_game(n):   # from the library's first game
    for _ in range((n - 1) // 10): nav.key(P, 'DPAD_DOWN'); time.sleep(0.3)
    for _ in range((n - 1) % 10): nav.key(P, 'DPAD_RIGHT'); time.sleep(0.3)
    time.sleep(1); nav.key(P, 'Z'); time.sleep(9)
    for _ in range(3): nav.key(P, 'Z'); time.sleep(2)

def diag(n, what):
    # everything about a run that gave no result: the app's logs, the screen, the command file
    stamp = time.strftime('%H%M%S')
    base = os.path.join(os.path.dirname(os.path.abspath(sys.argv[2])), f'noresult_{n}_{what}_{stamp}')
    full = subprocess.run([nav.ADB, '-s', '127.0.0.1:' + P, 'logcat', '-d'], capture_output=True, text=True, errors='replace').stdout
    open(base + '_logcat.txt', 'w', encoding='utf-8').write(full)
    left = subprocess.run([nav.ADB, '-s', '127.0.0.1:' + P, 'shell', f'cat {D}/ss_cmd 2>/dev/null; ls {D}/savestates | tail -5'], env=ENV, capture_output=True, text=True, errors='replace').stdout
    open(base + '_files.txt', 'w', encoding='utf-8').write(left)
    try: nav.shot(P).save(base + '_screen.png')
    except Exception: pass
    say(f'   (no result: logs kept as {os.path.basename(base)}_*)')

def run_once(n):
    nav.adb(P, 'shell', 'am', 'force-stop', nav.PKG)
    if subprocess.run([nav.ADB, '-s', '127.0.0.1:' + P, 'shell', 'pidof', nav.PKG], capture_output=True, text=True).stdout.strip():
        return None, 'app did not stop'
    nav.adb(P, 'logcat', '-c')
    nav.adb(P, 'shell', 'monkey', '-p', nav.PKG, '-c', 'android.intent.category.LAUNCHER', '1')
    if not wait_ready(): return None, 'app did not start'
    time.sleep(6)
    cmd('solo'); time.sleep(14)
    nav.key(P, 'Z'); time.sleep(6)
    g0, _ = where()
    if g0 != 0: return None, f'not in the library at start ({g0})'
    open_game(n)
    mash(6)
    g, f = where()
    if g is None: diag(n, 'where')
    if g != ID[n]: return None, f'not in game {n} ({g})'
    nav.adb(P, 'logcat', '-c')
    cmd('save 1'); mash(3.5)
    so = log()
    if not re.search(r'save slot 1: frame', so):
        diag(n, 'save1'); return None, 'save 1 didn\'t happen' + (' (' + re.search(r'save slot 1: (refused[^\n]*)', so).group(1) + ')' if 'save slot 1: refused' in so else '')
    if MODE != 'same': leave_game()
    if MODE == 'other':
        nav.key(P, 'DPAD_LEFT' if (n - 1) % 10 == 9 else 'DPAD_RIGHT'); time.sleep(0.6); nav.key(P, 'Z'); time.sleep(9)
        for _ in range(3): nav.key(P, 'Z'); time.sleep(2)
        mash(2)
    g2, f2 = where()
    if MODE == 'same' and g2 != ID[n]: return None, f'not in game {n} at the load ({g2})'
    if MODE == 'library' and g2 != 0: return None, f'did not reach the library ({g2})'
    if MODE == 'other' and (g2 is None or g2 <= 0 or g2 == g): return None, f'did not reach another game ({g2})'
    nav.adb(P, 'logcat', '-c')
    cmd('load 1')
    t0 = time.time(); snap = 'no result'; o = ''
    while time.time() - t0 < 60:
        time.sleep(1.5)
        o = log()
        if 'from its snapshot in' in o and 'verified' in o: snap = 'ok ' + re.search(r'from its snapshot in (\d+) ms', o).group(1) + ' ms'; break
        m = re.search(r"snapshot didn't check out \(([^)]*)\)", o)
        if m and 'reached frame' in o: snap = 'FAILED (' + m.group(1) + ')'; break
        if 'capture failed' in o or 'restore failed' in o: snap = 'capture/restore error'; break
        # a load straight to the replay (there was no snapshot to use): a result of its own, not "nothing"
        if 'load slot 1: replaying' in o and re.search(r'load slot 1: reached frame .*(verified|MISMATCH)', o) and "didn't check out" not in o:
            snap = 'NO SNAPSHOT (replayed: ' + re.search(r'load slot 1: reached frame .*(verified|MISMATCH)', o).group(1) + ')'; break
        m = re.search(r'load slot 1: (refused[^\n]*|no such save)', o)
        if m: snap = 'REFUSED (' + m.group(1).strip() + ')'; break
    if snap == 'no result': diag(n, 'load'); return None, 'load gave no result'
    if snap.startswith('NO SNAPSHOT') or snap.startswith('REFUSED'): diag(n, 'load_' + snap.split()[0].lower())
    g3, _ = where()
    if g3 != ID[n]: return None, f'after the load not in game {n} ({g3})'
    warns = [l[l.find('PFOSTATE'):][:140] for l in o.splitlines() if ' W PFOSTATE' in l and "didn't check out" not in l]
    nav.adb(P, 'logcat', '-c')
    mash(3); cmd('save 2'); mash(1.5)
    # the save must have happened before the replay of it is asked for (a command still waiting in
    # files/ss_cmd would be overwritten by the next one)
    t0 = time.time()
    while time.time() - t0 < 10 and not re.search(r'save slot 2: (frame|refused)', log()): time.sleep(0.5)
    so = log()
    if not re.search(r'save slot 2: frame', so):
        diag(n, 'save2'); return None, 'save 2 didn\'t happen' + (' (' + re.search(r'save slot 2: (refused[^\n]*)', so).group(1) + ')' if 'save slot 2: refused' in so else '')
    nav.adb(P, 'logcat', '-c'); cmd('rload 2')
    t0 = time.time(); rep = None
    while time.time() - t0 < 90:
        time.sleep(2)
        o = log()
        m = re.search(r'reached frame (\d+) in (\d+) ms[^,]*, checksum \w+ vs saved \w+, (\d+) replay checksums(.*)', o)
        if m: rep = ('ok' if 'verified' in m.group(4) else 'FAILED' + m.group(4)[:60]) + f' ({m.group(1)} frames in {m.group(2)} ms, {m.group(3)} checks)'; break
        r = re.search(r'load slot 2: (refused[^\n]*|no such save|the replay ended the session[^\n]*)', o)
        if r: diag(n, 'replay_refused'); return None, 'replay ' + r.group(1).strip()
    if rep is None: diag(n, 'replay'); return None, 'replay gave no result'
    return f'snapshot {snap} | replay through it {rep}' + ('  | ' + ' / '.join(warns[:1]) if warns else ''), None

subprocess.run([nav.ADB, '-s', '127.0.0.1:' + P, 'shell', f'rm -f {D}/snapdebug {D}/snapdiffat {D}/testpad'], env=ENV)
for n in games:
    why = []
    for attempt in range(3):
        res, problem = run_once(n)
        if res: break
        why.append(problem)
    tag = f'game {n:2d} (internal {ID[n]}, loaded from {MODE})'
    say(f'{tag}: {res}' + (f'  [retried: {"; ".join(why)}]' if why and res else '') if res else f'{tag}: HARNESS could not run it: {"; ".join(why)}')
