# Are test commands ever lost? Sends "where" N times through files/ss_cmd and counts the ones that
# never got an answer in the log. The exit code is 1 when a command was lost, else 0.
# Usage: cmd_stress.py [port] [count]
import os, sys, time, subprocess, re
ADB = os.environ.get('ADB') or os.path.expandvars(r'%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe')
P = sys.argv[1] if len(sys.argv) > 1 else '5555'
N = int(sys.argv[2]) if len(sys.argv) > 2 else 150
D = '/sdcard/Android/data/com.unofficial.ufo50/files'
ENV = {**os.environ, 'MSYS_NO_PATHCONV': '1'}
def adb(*a): return subprocess.run([ADB, '-s', '127.0.0.1:' + P, *a], env=ENV, capture_output=True, text=True, errors='replace').stdout
lost = 0
for i in range(N):
    adb('logcat', '-c')
    adb('shell', f'echo where > {D}/ss_cmd')
    t = time.time(); ok = False
    while time.time() - t < 4:
        time.sleep(0.4)
        if re.search(r'where: game', adb('logcat', '-d', '-s', 'PFOSTATE:*')): ok = True; break
    if not ok:
        lost += 1
        left = adb('shell', f'ls {D}/ss_cmd 2>/dev/null').strip()
        print(f'{i}: no answer (command file still there: {bool(left)})', flush=True)
print(f'{N} commands, {lost} lost')
sys.exit(1 if lost else 0)
