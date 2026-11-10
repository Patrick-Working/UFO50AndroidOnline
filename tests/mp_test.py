# Two-device online save/load test on two BlueStacks instances (host 5555, joiner 5565).
# Starts both apps, hosts a public lobby, joins it, readies both, picks a profile, then:
#   save (from the host) -> move on -> propose loading it (host) -> joiner accepts or declines
# and judges both logs: the save has the same frame and checksum on both devices, an accepted
# load ends on the same frame on both (a declined one loads nothing), and no desync or crash
# line is in the log of the whole test, which ends 15 s after the vote. Prints "mp_test: PASS" or "mp_test: FAIL (why)"; the exit
# code is 0 for PASS and 1 for FAIL.
# Usage: mp_test.py [accept|decline] [--no-start]
#   accept|decline   the joiner's vote on the load (default: accept)
#   --no-start       use the match that is already running on both devices
import os, sys, time, re, subprocess
ADB = os.environ.get('ADB') or os.path.expandvars(r'%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe')
H, J = '5555', '5565'
D = '/sdcard/Android/data/com.unofficial.ufo50/files'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out')
os.makedirs(OUT, exist_ok=True)
ENV = {**os.environ, 'MSYS_NO_PATHCONV': '1'}

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

def last(text, pat):
    # the groups of the newest line that matches, or None
    f = re.findall(pat, text)
    return f[-1] if f else None

def main():
    import argparse
    ap = argparse.ArgumentParser(description='Two-device online save/load test (host 5555, joiner 5565).')
    ap.add_argument('answer', nargs='?', choices=('accept', 'decline'), default='accept', help="the joiner's vote on the load (default: accept)")
    ap.add_argument('--no-start', action='store_true', help='use the match that is already running on both devices')
    args = ap.parse_args()
    fails = []
    if not args.no_start: start()
    shot(H, '01_host'); shot(J, '01_join')
    # The log is cleared here and nowhere after, so every check below sees the whole test.
    # A bigger log buffer (if the device allows it) keeps the first lines from being pushed out.
    for p in (H, J): adb(p, 'logcat', '-G', '16M'); adb(p, 'logcat', '-c')
    keys(H, 'DPAD_RIGHT', 'DPAD_RIGHT', 'DPAD_RIGHT', 1)
    cmd(H, 'save 1'); time.sleep(5)
    saves = {p: last(log(p), r'online save \d+: slot (\d+) at frame (\d+): ok') for p in (H, J)}        # (slot, frame)
    sums = {p: last(log(p), r'save slot \d+: frame (\d+), \d+ bytes, checksum (\w+)') for p in (H, J)}  # (frame, checksum)
    print('save  host', saves[H], sums[H]); print('save  join', saves[J], sums[J])
    if not saves[H] or not saves[J]:
        print('mp_test: FAIL (no online save on ' + ' and '.join(n for p, n in ((H, 'the host'), (J, 'the joiner')) if not saves[p]) + ')')
        sys.exit(1)
    if saves[H][1] != saves[J][1]: fails.append(f'online save at frame {saves[H][1]} on the host, {saves[J][1]} on the joiner')
    if not sums[H] or not sums[J]: fails.append('no "save slot" line with a checksum on both devices')
    elif sums[H] != sums[J]: fails.append(f'save frame and checksum differ: host {sums[H]}, joiner {sums[J]}')
    keys(H, 'DPAD_DOWN', 'DPAD_DOWN', 1); keys(J, 'DPAD_LEFT', 1)
    shot(H, '02_moved_host')
    cmd(H, f'load {saves[H][0]}'); time.sleep(3)
    shot(J, '03_vote_join'); shot(H, '03_vote_host')
    cmd(J, args.answer); time.sleep(6)
    shot(H, '04_after_host'); shot(J, '04_after_join')
    done, failed, declined = {}, {}, {}
    for p, n in ((H, 'host'), (J, 'join')):
        l = log(p)
        print(n, 'vote:', re.findall(r'online: P\d command 24\d \(\d+\) at frame \d+', l))
        print(n, 'load:', re.findall(r'online load \d+ \(snapshot \d+, saved at frame \d+\): starting at frame \d+|online load of snapshot \d+: done at frame \d+ in \d+ ms|online load of snapshot \d+ failed.*', l))
        done[p] = last(l, r'online load of snapshot \d+: done at frame (\d+)')
        failed[p] = re.findall(r'online load of snapshot \d+ failed.*', l)
        declined[p] = re.findall(r'online: P\d command 243 ', l)   # 243 is the "decline" command
    if failed[H] or failed[J]: fails.append(f'a load failed: {(failed[H] + failed[J])[0]}')
    if args.answer == 'accept':
        if not done[H] or not done[J]: fails.append(f'the load did not finish on both devices (host {done[H]}, joiner {done[J]})')
        elif done[H] != done[J]: fails.append(f'the load ended at frame {done[H]} on the host, {done[J]} on the joiner')
    else:
        if done[H] or done[J]: fails.append('the load was declined but a snapshot was loaded')
        if not declined[H]: fails.append('the host never got the "decline" command')
    time.sleep(15)
    for p, n in ((H, 'host'), (J, 'join')):
        lines = full(p).splitlines()
        bad = [x for x in lines if re.search(r'(?i)desync|ran out of input|GoingOffline', x)]
        crash = [x for x in lines if re.search(r'FATAL|SIGSEGV|Assertion failed', x)]
        print(n, 'after 15 s:', 'no desync' if not bad else bad[:3])
        if bad: fails.append(f'{n}: {bad[0].strip()[-120:]}')
        if crash: fails.append(f'{n}: crash or failed assertion: {crash[0].strip()[-120:]}')
    shot(H, '05_later_host'); shot(J, '05_later_join')
    print('mp_test: PASS' if not fails else 'mp_test: FAIL (' + '; '.join(fails) + ')')
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
