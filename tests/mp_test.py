# Two-device online save/load test on two BlueStacks instances (host 5555, joiner 5565).
# Starts both apps, hosts a public lobby, joins it, readies both, picks a profile, then:
#   save (from the host) -> move on -> propose loading it (host) -> joiner accepts or declines
# and checks both logs: same save frame and checksum, same load frame, no desync after.
# Usage: mp_test.py [accept|decline] [--no-start]
import os, sys, time, re, subprocess
ADB = os.environ.get('ADB') or os.path.expandvars(r'%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe')
H, J = '5555', '5565'
D = '/sdcard/Android/data/com.unofficial.ufo50/files'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out')
os.makedirs(OUT, exist_ok=True)
ENV = {**os.environ, 'MSYS_NO_PATHCONV': '1'}
answer = sys.argv[1] if len(sys.argv) > 1 else 'accept'

def adb(p, *a, **k): return subprocess.run([ADB, '-s', '127.0.0.1:' + p, *a], env=ENV, capture_output=True, text=True, errors='replace', **k).stdout
def key(p, k): adb(p, 'shell', 'input', 'keyevent', 'KEYCODE_' + k)
def keys(p, *seq):
    for k in seq:
        if isinstance(k, (int, float)): time.sleep(k)
        else: key(p, k); time.sleep(0.6)
def cmd(p, c): adb(p, 'shell', f'echo {c} > {D}/ss_cmd')
def shot(p, name):
    with open(os.path.join(OUT, name + '.png'), 'wb') as f:
        f.write(subprocess.run([ADB, '-s', '127.0.0.1:' + p, 'exec-out', 'screencap', '-p'], capture_output=True).stdout)
def log(p): return adb(p, 'logcat', '-d', '-s', 'PFOSTATE:*')
def full(p): return adb(p, 'logcat', '-d')

def start_once():
    for p in (H, J):
        adb(p, 'shell', 'am', 'force-stop', 'com.unofficial.ufo50')
        adb(p, 'shell', f'rm -f {D}/rejoin.txt')   # no "rejoin your match?" over the menus
        adb(p, 'logcat', '-c')
        adb(p, 'shell', 'monkey', '-p', 'com.unofficial.ufo50', '-c', 'android.intent.category.LAUNCHER', '1')
    time.sleep(24)
    # skip intro, then Z until the profile screen shows (the boot animation comes and goes), then PLAY ONLINE
    def to_lobbies(p):
        keys(p, 'ESCAPE', 4)
        for _ in range(8):
            if looks(p, 'ref_profiles.png', (1360, 520, 1640, 740)): break
            keys(p, 'Z', 3.5)
        else: raise RuntimeError(f'{p}: never reached the profile screen')
        keys(p, 'DPAD_RIGHT', 1, 'Z', 5)
        if not looks(p, 'ref_lobbies.png', (570, 140, 1340, 230)): raise RuntimeError(f'{p}: not on the lobby screen')
    to_lobbies(H)
    keys(H, 'Z', 6)                 # HOST PUBLIC
    to_lobbies(J)
    for _ in range(10):             # the host's lobby in the joiner's list (the entry box is drawn)
        if not looks(J, 'ref_lobbies.png', (330, 290, 1250, 410), 8): break
        time.sleep(2)
    else: raise RuntimeError('the joiner never saw the lobby')
    keys(J, 'Z', 6)                 # join it
    key(J, 'Z'); time.sleep(1.5); key(H, 'Z'); time.sleep(12)   # READY, READY: the match starts
    key(H, 'Z'); time.sleep(10)     # host picks the profile: library

HERE = os.path.dirname(os.path.abspath(__file__))

def start(tries=3):
    # a fresh match on both devices; a menu that didn't go as expected is tried again from the start
    for t in range(tries):
        try:
            start_once()
            for p in (H, J):
                adb(p, 'logcat', '-c'); cmd(p, 'where')
            time.sleep(2.5)
            if all(re.search(r'where: game 0, frame \d+, clients 2', log(p)) for p in (H, J)):
                return
            raise RuntimeError('not both in the online library')
        except RuntimeError as e:
            print(f'start, try {t + 1}: {e}', flush=True)
    sys.exit('could not start a match')
def looks(p, ref, box, limit=18):
    # does the screen match a reference screenshot in this box (mean colour difference)?
    from PIL import Image, ImageChops, ImageStat
    import io
    cur = Image.open(io.BytesIO(subprocess.run([ADB, '-s', '127.0.0.1:' + p, 'exec-out', 'screencap', '-p'], capture_output=True).stdout)).convert('RGB').crop(box)
    r = Image.open(os.path.join(HERE, ref)).convert('RGB').crop(box)
    d = sum(ImageStat.Stat(ImageChops.difference(cur, r)).mean) / 3
    return d < limit

def frames(text, pat): return [m.groups() for m in re.finditer(pat, text)]

def main():
    if '--no-start' not in sys.argv: start()
    shot(H, '01_host'); shot(J, '01_join')
    for p in (H, J): adb(p, 'logcat', '-c')
    keys(H, 'DPAD_RIGHT', 'DPAD_RIGHT', 'DPAD_RIGHT', 1)
    cmd(H, 'save 1'); time.sleep(5)
    hs = frames(log(H), r'online save (\d+): slot (\d+) at frame (\d+)')
    js = frames(log(J), r'online save (\d+): slot (\d+) at frame (\d+)')
    sums = {p: frames(log(p), r'save slot \d+: frame (\d+), (\d+) bytes, checksum (\w+)') for p in (H, J)}
    print('save  host', hs, sums[H]); print('save  join', js, sums[J])
    if not hs or not js: sys.exit('no online save')
    keys(H, 'DPAD_DOWN', 'DPAD_DOWN', 1); keys(J, 'DPAD_LEFT', 1)
    shot(H, '02_moved_host')
    cmd(H, f'load {hs[-1][1]}'); time.sleep(3)
    shot(J, '03_vote_join'); shot(H, '03_vote_host')
    cmd(J, answer); time.sleep(6)
    shot(H, '04_after_host'); shot(J, '04_after_join')
    for p, n in ((H, 'host'), (J, 'join')):
        l = log(p)
        print(n, 'vote:', [x for x in re.findall(r'online: P\d command 24\d \(\d+\) at frame \d+', l)])
        print(n, 'load:', re.findall(r'online load \d+ \(snapshot \d+, saved at frame \d+\): starting at frame \d+|online load of snapshot \d+: done at frame \d+ in \d+ ms|online load of snapshot \d+ failed.*', l))
    time.sleep(15)
    for p, n in ((H, 'host'), (J, 'join')):
        bad = [x for x in full(p).splitlines() if re.search(r'(?i)desync|ran out of input|GoingOffline', x)]
        print(n, 'after 15 s:', 'no desync' if not bad else bad[:3])
    shot(H, '05_later_host'); shot(J, '05_later_join')

if __name__ == '__main__':
    main()
