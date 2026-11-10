# Rewind check (solo) on one BlueStacks instance: start a solo session, open Velgress, play with a
# pause, then rewind 15 s and 60 s; each must land on its frame with the replay's checksums matching.
# Then twice: play 2.5 minutes, rewind 60 s, and compare the time it took with its estimate.
# Usage: rewind_test.py [adb port, default 5555]
import os, sys, time, subprocess, re, random
ADB = os.environ.get('ADB') or os.path.expandvars(r'%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe')
P = sys.argv[1] if len(sys.argv) > 1 else '5555'
D = '/sdcard/Android/data/com.unofficial.ufo50/files'
ENV = {**os.environ, 'MSYS_NO_PATHCONV': '1'}
def adb(*a): return subprocess.run([ADB, '-s', '127.0.0.1:' + P, *a], env=ENV, capture_output=True, text=True, errors='replace').stdout
def key(k, w=0.4): adb('shell', 'input', 'keyevent', 'KEYCODE_' + k); time.sleep(w)
def cmd(c): adb('shell', f'echo {c} > {D}/ss_cmd')
def log(): return adb('logcat', '-d', '-s', 'PFOSTATE:*')
rnd = random.Random(11)
def mash(sec):
    t = time.time()
    while time.time() - t < sec: key(rnd.choice(['DPAD_LEFT', 'DPAD_RIGHT', 'Z', 'X', 'DPAD_RIGHT']), 0.15)

adb('shell', 'am', 'force-stop', 'com.unofficial.ufo50'); adb('shell', f'rm -f {D}/rejoin.txt'); adb('logcat', '-c')
adb('shell', 'monkey', '-p', 'com.unofficial.ufo50', '-c', 'android.intent.category.LAUNCHER', '1'); time.sleep(24)
cmd('solo'); time.sleep(14); key('Z'); time.sleep(6)
for _ in range(6): key('DPAD_RIGHT')
key('Z'); time.sleep(9)
for _ in range(3): key('Z'); time.sleep(2)
mash(30); key('ESCAPE'); time.sleep(3); key('ESCAPE'); time.sleep(1); mash(45)
ok = True
for secs in (15, 60):
    adb('logcat', '-c'); cmd(f'rewind {secs}')
    t = time.time(); res = []
    while time.time() - t < 60:
        time.sleep(2)
        res = re.findall(r'load slot -2: reached frame (\d+) .* (verified|MISMATCH)', log())
        if res: break
    start = re.findall(r'rewind to frame (\d+): (.*)', log())
    good = bool(res) and res[-1][1] == 'verified' and start and start[-1][0] == res[-1][0]
    ok = ok and good
    print(f'rewind {secs} s: {"PASS" if good else "FAIL"} {start[-1:]} {res[-1:]}', flush=True)
    mash(8)
# estimates: after a few minutes more play, a long rewind's estimate (in its log line) against the
# time it really took. Too short is what matters (people agree to the wait first).
def timed_rewind(secs):
    adb('logcat', '-c'); cmd(f'rewind {secs}')
    t = time.time(); res = []
    while time.time() - t < 300:
        time.sleep(2)
        res = re.findall(r'load slot -2: reached frame (\d+) in (\d+) ms.* (verified|MISMATCH)', log())
        if res: break
    est = re.findall(r'rewind to frame \d+: .*estimate (\d+) ms', log())
    if not res or not est: return None
    return int(est[-1]), int(res[-1][1]), res[-1][2]
for round_ in (1, 2):
    mash(150)
    r = timed_rewind(60)
    if r is None: print(f'estimate {round_}: FAIL (no result)'); ok = False; continue
    est, took, v = r
    good = v == 'verified' and took <= est * 1.25
    ok = ok and good
    print(f'estimate {round_}: {"PASS" if good else "FAIL"} estimated {est} ms, took {took} ms ({took / max(1, est):.2f}x), {v}', flush=True)
print('replay speeds:', ' | '.join(adb('shell', f'cat {D}/replay_speed.txt').split(chr(10))).strip())
bad = re.search(r'FATAL|SIGSEGV|Assertion failed', adb('logcat', '-d'))
print('rewind:', 'PASS' if ok and not bad else 'FAIL')
