# A save made in solo play, loaded after DISCONNECT (the library's menu), on one BlueStacks instance.
# The snapshot of a save is the running session's state; after DISCONNECT PFO's side is shut down,
# and loading the snapshot then asserted in pfo_set_players (an error box). A load outside solo play
# must replay the save's recording instead, verified, with no game-code error or crash.
# Prints PASS/FAIL lines and "disconnect: PASS" or "disconnect: FAIL" (exit code 0 / 1).
# Usage: disconnect.py [adb port, default 5555]
import os, sys, time, re, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nav

P = sys.argv[1] if len(sys.argv) > 1 else '5555'
D = '/sdcard/Android/data/com.unofficial.ufo50/files'
ENV = {**os.environ, 'MSYS_NO_PATHCONV': '1'}
PROBLEM = r'FATAL|SIGSEGV|Assertion failed|ERROR in action'
fails = []


def adb(*a): return subprocess.run([nav.ADB, '-s', '127.0.0.1:' + P, *a], env=ENV, capture_output=True, text=True, errors='replace').stdout
def key(k, wait=0.8): adb('shell', 'input', 'keyevent', 'KEYCODE_' + k); time.sleep(wait)
def cmd(c): adb('shell', f'echo {c} > {D}/ss_cmd')
def log(): return adb('logcat', '-d', '-s', 'PFOSTATE:*')
def check(ok, what):
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok: fails.append(what)
def wait_for(pattern, timeout):
    t = time.time()
    while time.time() - t < timeout:
        m = re.search(pattern, log())
        if m: return m
        time.sleep(0.5)
    return None


adb('shell', 'am', 'force-stop', nav.PKG)
adb('shell', f'echo off > {D}/okplay.txt')   # no room server: this test plays alone (mp_test.py takes it away again)
adb('logcat', '-c')
adb('shell', 'monkey', '-p', nav.PKG, '-c', 'android.intent.category.LAUNCHER', '1')
time.sleep(26)
cmd('solo'); time.sleep(14)
key('Z', 6)
for _ in range(4): key('Z', 5)
cmd('save 1')
check(wait_for(r'save slot 1: frame', 10) is not None, 'save 1 in solo play')
wait_for(r'snapshot 1: ready', 10)
# pause menu > Back to Library > yes; the library's menu > Disconnect > yes
for k, w in (('ESCAPE', 1.5), ('DPAD_DOWN', 0.4), ('DPAD_DOWN', 0.4), ('Z', 1), ('Z', 7), ('ESCAPE', 1.5), ('DPAD_DOWN', 0.4), ('DPAD_DOWN', 0.4), ('Z', 1.5), ('Z', 6)):
    key(k, w)
adb('logcat', '-c'); cmd('where')
m = wait_for(r'where: game (-?\d+), frame (\d+), clients (\d+)', 8)
check(m is not None and m.group(3) == '0', 'disconnected (no session)' + (f' ({m.group(3)} players)' if m else ''))
cmd('load 1')
check(wait_for(r'load slot 1: not in solo play, so by replay', 10) is not None, 'the load replays (not the snapshot)')
check(wait_for(r'load slot 1: reached frame \d+[^\n]*verified', 60) is not None, 'the replay reached the save, verified')
time.sleep(2)
bad = [x.strip() for x in adb('logcat', '-d').splitlines() if re.search(PROBLEM, x)]
check(not bad, 'no game-code error or crash' + (': ' + bad[0][-140:] if bad else ''))
check(bool(adb('shell', 'pidof', nav.PKG).strip()), 'the app still running')
print('disconnect: ' + ('PASS' if not fails else 'FAIL'), flush=True)
sys.exit(1 if fails else 0)
