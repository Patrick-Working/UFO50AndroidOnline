# Quick save and quick load, the keyboard and controller keys of the overlay, and a load's picture
# check, on one BlueStacks instance, in Barbuta (game 1 in the library, 40 inside the game):
#   - F5 before solo play is refused, with the banner that says how to start it
#   - F5 in solo play saves the QUICK slot; walking right makes the loose block fall; F9 loads the
#     quick save from its snapshot with its pictures compared, and the block is gone again (the bug
#     that tiles drawn after a save stayed on screen)
#   - F1 opens the save list, the arrows move in it, Esc closes it; both sticks pressed in (a
#     controller) opens it, B closes it; with the controller alone a save is picked and loaded
#   - no game-code error, crash or failed assertion anywhere in the run's log
# Prints one PASS/FAIL line per check, then "quickslot: PASS" or "quickslot: FAIL" (exit code 0 / 1).
# Usage: quickslot.py [adb port, default 5555]
import os, sys, time, re, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nav
from PIL import ImageChops

P = sys.argv[1] if len(sys.argv) > 1 else '5555'
D = '/sdcard/Android/data/com.unofficial.ufo50/files'
ENV = {**os.environ, 'MSYS_NO_PATHCONV': '1'}
PROBLEM = r'FATAL|SIGSEGV|Assertion failed|ERROR in action'
fails = []
ERRS = []   # game-code errors and crashes, gathered before every clear of the log


def adb(*a): return subprocess.run([nav.ADB, '-s', '127.0.0.1:' + P, *a], env=ENV, capture_output=True, text=True, errors='replace').stdout
def key(k, pad=False, wait=0.5):
    adb('shell', 'input', *(['gamepad'] if pad else []), 'keyevent', 'KEYCODE_' + k); time.sleep(wait)
def cmd(c): adb('shell', f'echo {c} > {D}/ss_cmd')
def clear():
    ERRS.extend(x.strip() for x in adb('logcat', '-d').splitlines() if re.search(PROBLEM, x))
    adb('logcat', '-c')
def log(): return adb('logcat', '-d', '-s', 'PFOSTATE:*', 'PFOKEYS:*')
def check(ok, what):
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok: fails.append(what)
    return ok
def wait_for(pattern, timeout=20):
    t = time.time()
    while time.time() - t < timeout:
        m = re.search(pattern, log())
        if m: return m
        time.sleep(0.5)
    return None
def where():
    for _ in range(5):
        cmd('where'); time.sleep(2)
        m = re.findall(r'where: game (-?\d+), frame (\d+)', log())
        if m: return int(m[-1][0])
    return None
def area(): return nav.shot(P).crop((600, 380, 1350, 860))   # around the arch, clear of the touch controls
def changed(a, b):   # the share of pixels that differ
    d = ImageChops.difference(a, b).convert('L').point(lambda v: 255 if v > 24 else 0)
    return d.histogram()[255] / float(a.width * a.height)


# a fresh start, without an older quick save
adb('shell', 'am', 'force-stop', nav.PKG)
adb('shell', f'rm -f {D}/savestates/slot9999.*')
adb('shell', f'echo off > {D}/okplay.txt')   # no room server: this test plays alone (mp_test.py takes it away again)
adb('logcat', '-c')  # (before the app starts: nothing of this run to keep)
adb('shell', 'monkey', '-p', nav.PKG, '-c', 'android.intent.category.LAUNCHER', '1')
t = time.time()
while time.time() - t < 60 and where() is None: pass
time.sleep(4)

key('F5')
check(wait_for(r'banner: START SOLO PLAY TO SAVE', 5) is not None, 'F5 before solo play: refused, saying to start solo play')

cmd('solo'); time.sleep(14)
key('Z', wait=6)
check(where() == 0, 'solo play starts in the library')
for _ in range(4): key('Z', wait=5)
in_game = where() == 40
check(in_game, 'in Barbuta (game 40)')

key('F5', wait=2)
check(wait_for(r'save slot 9999: frame', 10) is not None, 'F5: the quick slot saved')
check(wait_for(r'pill: QUICK SAVED', 5) is not None, 'F5: QUICK SAVED shown')
wait_for(r'snapshot 9999: ready', 10)
saved = area()
for _ in range(30): adb('shell', 'input', 'keyevent', 'KEYCODE_DPAD_RIGHT')
time.sleep(2)
fell = changed(saved, area()) > 0.003
check(fell, 'walking right changed the room (the loose block fell)')
clear()
key('F9', wait=4)
m = wait_for(r'load slot 9999: from its snapshot in (\d+) ms, \d+ frames checked, (\d+) pictures: verified', 20)
check(m is not None and int(m.group(2)) >= 1, 'F9: the quick save loaded from its snapshot, pictures compared' + (f' ({m.group(2)})' if m else ''))
check(wait_for(r'pill: QUICK LOADED', 5) is not None, 'F9: QUICK LOADED shown')
time.sleep(1)
if fell: check(changed(saved, area()) < 0.01, 'after the load the room is as it was saved (no block left drawn)')

# the save list by keyboard
clear()
key('F1')
check(wait_for(r'save list: open', 5) is not None, 'F1 opens the save list')
key('DPAD_DOWN')
check(wait_for(r'save list: on -?\d+', 5) is not None, 'an arrow moves the outline')
key('ESCAPE')
check(wait_for(r'save list: closed', 5) is not None, 'Esc closes the save list')

# ...and by controller, down to the quick save and loaded from there
clear()
key('BUTTON_THUMBL', pad=True, wait=0.1); key('BUTTON_THUMBR', pad=True)
check(wait_for(r'save list: open', 5) is not None, 'both sticks pressed in open the save list')
key('BUTTON_B', pad=True)
check(wait_for(r'save list: closed', 5) is not None, 'B closes it')
clear()
key('BUTTON_THUMBL', pad=True, wait=0.1); key('BUTTON_THUMBR', pad=True)
# (from SAVE NOW: down to the rewind row, left along it to QUICK LOAD, down into the list)
for k in ('DPAD_DOWN', 'DPAD_LEFT', 'DPAD_LEFT', 'DPAD_DOWN', 'DPAD_DOWN'):
    if re.search(r'save list: on 9999', log()): break
    key(k, pad=True)
on_row = re.search(r'save list: on 9999', log()) is not None
check(on_row, 'the D-pad reaches the quick save in the list')
if on_row:
    key('BUTTON_A', pad=True, wait=3)
    check(wait_for(r'load slot 9999: from its snapshot in \d+ ms, \d+ frames checked, \d+ pictures: verified', 20) is not None, 'A on it loads it (snapshot, verified)')

clear()
bad = ERRS
check(not bad, 'no game-code error or crash in the log' + (': ' + bad[0].strip()[-140:] if bad else ''))
print('quickslot: ' + ('PASS' if not fails else 'FAIL'), flush=True)
sys.exit(1 if fails else 0)
