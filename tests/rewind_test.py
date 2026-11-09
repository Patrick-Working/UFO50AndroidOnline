# Rewind check (solo) on one BlueStacks instance: start a solo session, open Velgress, play with a
# pause, then rewind 15 s and 60 s; each must land on its frame with the replay's checksums matching.
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
bad = re.search(r'FATAL|SIGSEGV|Assertion failed', adb('logcat', '-d'))
print('rewind:', 'PASS' if ok and not bad else 'FAIL')
