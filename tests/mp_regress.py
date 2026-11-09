# Every online save-state scenario on two BlueStacks instances (host 5555, joiner 5565), one after
# another, each judged automatically. Prints one PASS/FAIL line per scenario.
#   1 save + vote accept + load (library)     4 network drop of 20 s, play resumes
#   2 vote declined (nothing loads)           5 joiner's app killed, relaunched, rejoins
#   3 rejoin through an online save + load    6 load a save from an earlier match (both restart from it)
# Usage: mp_regress.py [scenario numbers, e.g. 1,4,5]
import sys, time, re, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mp_test as m

results = []
def say(n, ok, why=''):
    line = f'{n}: {"PASS" if ok else "FAIL"}' + (f' ({why})' if why else '')
    print(line, flush=True); results.append(line)

def clean(p):
    l = m.full(p)
    if re.search(r'FATAL|SIGSEGV|Assertion failed', l): return 'crash/assert'
    if re.search(r'(?i)desync detected', l): return 'desync'
    return ''

def sel(p):
    m.cmd(p, 'where'); time.sleep(0)
def frames(p):
    m.adb(p, 'logcat', '-c'); m.cmd(p, 'where'); time.sleep(2.5)
    f = re.findall(r'where: game (-?\d+), frame (\d+), clients (\d+)', m.log(p))
    return f[-1] if f else None

def in_sync():
    # both still in the match, frames moving, and the same game picked after both move the cursor
    m.key(m.H, 'DPAD_RIGHT'); m.key(m.J, 'DPAD_DOWN'); time.sleep(3)
    a, b = frames(m.H), frames(m.J)
    m.shot(m.H, 'rg_host'); m.shot(m.J, 'rg_join')
    from PIL import Image, ImageChops, ImageStat
    ha = Image.open(os.path.join(m.OUT, 'rg_host.png')).convert('RGB').crop((190, 900, 1730, 975))
    jb = Image.open(os.path.join(m.OUT, 'rg_join.png')).convert('RGB').crop((190, 900, 1730, 975))
    same = sum(ImageStat.Stat(ImageChops.difference(ha, jb)).mean) / 3 < 6
    return a and b and a[2] == '2' and b[2] == '2' and same, f'host {a}, join {b}, same game shown {same}'

def fresh():
    m.start()
    for p in (m.H, m.J): m.adb(p, 'logcat', '-c')

def s1():
    fresh()
    for k in ('DPAD_RIGHT', 'DPAD_RIGHT'): m.key(m.H, k)
    m.cmd(m.H, 'save 1'); time.sleep(4)
    hs = re.findall(r'online save (\d+): slot (\d+) at frame (\d+)', m.log(m.H)); js = re.findall(r'online save (\d+): slot (\d+) at frame (\d+)', m.log(m.J))
    hc = re.findall(r'checksum (\w+): ok', m.log(m.H)); jc = re.findall(r'checksum (\w+): ok', m.log(m.J))
    if not hs or not js or hs[-1][2] != js[-1][2] or hc[-1:] != jc[-1:]: return say(1, False, f'save {hs} {js} {hc} {jc}')
    m.key(m.H, 'DPAD_DOWN'); time.sleep(1)
    m.cmd(m.H, f'load {hs[-1][1]}'); time.sleep(3); m.cmd(m.J, 'accept'); time.sleep(5)
    hl = re.findall(r'online load of snapshot \d+: done at frame (\d+)', m.log(m.H)); jl = re.findall(r'online load of snapshot \d+: done at frame (\d+)', m.log(m.J))
    ok, why = in_sync()
    bad = clean(m.H) or clean(m.J)
    say(1, bool(hl) and hl == jl and ok and not bad, f'load {hl} {jl}; {why} {bad}')
    return hs[-1][1]

def s2():
    fresh()
    m.cmd(m.H, 'save 1'); time.sleep(4)
    hs = re.findall(r'online save (\d+): slot (\d+) at frame (\d+)', m.log(m.H))
    m.cmd(m.H, f'load {hs[-1][1]}'); time.sleep(3); m.cmd(m.J, 'decline'); time.sleep(4)
    loaded = re.findall(r'online load of snapshot', m.log(m.H) + m.log(m.J))
    declined = re.findall(r'P2 command 243', m.log(m.H))
    ok, why = in_sync()
    bad = clean(m.H) or clean(m.J)
    say(2, not loaded and bool(declined) and ok and not bad, f'loaded {len(loaded)}, declined {len(declined)}; {why} {bad}')

def kill_and_rejoin():
    m.adb(m.J, 'shell', 'am', 'force-stop', 'com.unofficial.ufo50'); time.sleep(10)
    m.adb(m.J, 'logcat', '-c')
    m.adb(m.J, 'shell', 'monkey', '-p', 'com.unofficial.ufo50', '-c', 'android.intent.category.LAUNCHER', '1'); time.sleep(26)
    m.cmd(m.J, 'rejoin')
    t = time.time()
    while time.time() - t < 45:
        time.sleep(3)
        if re.search(r'rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)', m.log(m.J)): break
    return re.findall(r'rejoin: .*', m.log(m.J))

def s3():
    fresh()
    m.cmd(m.H, 'save 1'); time.sleep(4)
    hs = re.findall(r'online save (\d+): slot (\d+) at frame (\d+)', m.log(m.H))
    m.key(m.H, 'DPAD_DOWN'); time.sleep(1)
    m.cmd(m.H, f'load {hs[-1][1]}'); time.sleep(3); m.cmd(m.J, 'accept'); time.sleep(5)
    r = kill_and_rejoin()
    verified = any('verified' in x for x in re.findall(r'load slot -1: reached.*', m.log(m.J)))
    ok, why = in_sync()
    bad = clean(m.H) or clean(m.J)
    say(3, any('back in the match' in x for x in r) and verified and ok and not bad, f'{r[-1:] }, replay verified {verified}; {why} {bad}')

def s4():
    fresh()
    m.cmd(m.J, 'netdrop 20'); time.sleep(40)
    ok, why = in_sync()
    bad = clean(m.H) or clean(m.J)
    say(4, ok and not bad, f'{why} {bad}')

def s5():
    fresh()
    m.key(m.H, 'DPAD_RIGHT'); time.sleep(3)
    r = kill_and_rejoin()
    ok, why = in_sync()
    bad = clean(m.H) or clean(m.J)
    say(5, any('back in the match' in x for x in r) and ok and not bad, f'{r[-1:]}; {why} {bad}')

def s6():
    fresh()
    for k in ('DPAD_RIGHT', 'DPAD_RIGHT', 'DPAD_RIGHT'): m.key(m.H, k)
    m.cmd(m.H, 'save 1'); time.sleep(4)
    hs = re.findall(r'online save (\d+): slot (\d+) at frame (\d+)', m.log(m.H))
    fresh()   # another match (new identities)
    m.key(m.H, 'DPAD_LEFT'); time.sleep(1)
    m.cmd(m.H, f'load {hs[-1][1]}'); time.sleep(4); m.cmd(m.J, 'accept')
    t = time.time()
    while time.time() - t < 40:
        time.sleep(3)
        if len(re.findall(r'load from another match: at frame', m.log(m.H) + m.log(m.J))) >= 2: break
    time.sleep(6)
    ver = re.findall(r'load slot \d+: reached frame \d+ .* (verified|MISMATCH)', m.log(m.H) + m.log(m.J))
    ok, why = in_sync()
    bad = clean(m.H) or clean(m.J)
    say(6, ver == ['verified', 'verified'] and ok and not bad, f'{ver}; {why} {bad}')

pick = [int(x) for x in sys.argv[1].split(',')] if len(sys.argv) > 1 else [1, 2, 3, 4, 5, 6]
for n in pick:
    try: [None, s1, s2, s3, s4, s5, s6][n]()
    except SystemExit as e: say(n, False, str(e))
    except Exception as e: say(n, False, repr(e))
print('\n'.join(results))
