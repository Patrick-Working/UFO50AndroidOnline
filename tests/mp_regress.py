# Every online save-state scenario on two BlueStacks instances (host 5555, joiner 5565), one after
# another, each judged automatically. Prints one PASS/FAIL line per scenario.
#   1 save + vote accept + load (library)     4 network drop of 20 s, play resumes
#   2 vote declined (nothing loads)           5 joiner's app killed, relaunched, rejoins
#   3 rejoin through an online save + load    6 load a save from an earlier match (both restart from it)
# After each scenario both devices must still be in the same match (see in_sync) and the whole log of
# the scenario must be free of crashes and desync reports (see clean). The exit code is 1 when a
# scenario failed, else 0.
# Usage: mp_regress.py [scenario numbers, e.g. 1,4,5]
import sys, time, re, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mp_test as m

results = []
def say(n, ok, why=''):
    line = f'{n}: {"PASS" if ok else "FAIL"}' + (f' ({why})' if why else '')
    print(line, flush=True); results.append(line)

def clean(p):
    # '' when the device's log has no crash and no desync report, else what was found.
    # The log is cleared only when a scenario starts, so this covers the whole scenario.
    for what, pat in (('crash/assert', r'FATAL|SIGSEGV|Assertion failed'), ('desync', r'(?i)desync detected')):
        hit = [x for x in m.full(p).splitlines() if re.search(pat, x)]
        if hit: return f'{what}: {hit[0].strip()[-100:]}'
    return ''

WHERE = r'where: game (-?\d+), frame (\d+), clients (\d+)'
def where_lines(p): return [x.strip() for x in m.log(p).splitlines() if 'where: game' in x]
def where():
    # Ask both devices where they are: {port: (game, frame, clients)} as numbers, None for a device
    # that did not answer. The log is not cleared for this. An answer is a "where" line that was not
    # in the log before the question (log lines carry their time, so a new one is never equal to an old one).
    old = {p: set(where_lines(p)) for p in (m.H, m.J)}
    for p in (m.H, m.J): m.cmd(p, 'where')
    got = {m.H: None, m.J: None}
    t = time.time()
    while time.time() - t < 6 and not all(got.values()):
        time.sleep(0.5)
        for p in got:
            new = [x for x in where_lines(p) if x not in old[p]]
            f = re.search(WHERE, new[-1]) if new else None
            if f: got[p] = tuple(int(x) for x in f.groups())
    return got

FRAME_TOL = 120   # frames. The devices answer up to about a second apart (they read test commands twice a second).
def in_sync():
    # After both move the cursor (host right, joiner down), ask both devices where they are, twice,
    # about 2 s apart. They count as in sync when: both answer, both say 2 players, both are in the
    # same game, their frame numbers are within FRAME_TOL of each other, the frame number went up
    # between the two answers on both, and the strip of the screen with the picked game's name
    # looks the same on both.
    # Checksums are not compared here. PFO compares them during play; clean() looks for its
    # "Desync detected" line.
    m.key(m.H, 'DPAD_RIGHT'); m.key(m.J, 'DPAD_DOWN'); time.sleep(3)
    w1 = where(); time.sleep(2); w2 = where()
    m.shot(m.H, 'rg_host'); m.shot(m.J, 'rg_join')
    from PIL import Image, ImageChops, ImageStat
    ha = Image.open(os.path.join(m.OUT, 'rg_host.png')).convert('RGB').crop((190, 900, 1730, 975))
    jb = Image.open(os.path.join(m.OUT, 'rg_join.png')).convert('RGB').crop((190, 900, 1730, 975))
    same = sum(ImageStat.Stat(ImageChops.difference(ha, jb)).mean) / 3 < 6
    a, b = w2[m.H], w2[m.J]
    why = []
    if not (a and b and w1[m.H] and w1[m.J]): why.append('no answer to "where"')
    else:
        if a[2] != 2 or b[2] != 2: why.append('not 2 players on both')
        if a[0] != b[0]: why.append('not in the same game')
        if abs(a[1] - b[1]) > FRAME_TOL: why.append(f'frames {abs(a[1] - b[1])} apart')
        if a[1] <= w1[m.H][1] or b[1] <= w1[m.J][1]: why.append('frame not moving')
    if not same: why.append('screens differ')
    return not why, f'host {a}, join {b} (game, frame, players), same game shown {same}' + (': ' + ', '.join(why) if why else '')

def fresh():
    # a new match. The log is cleared here and not again until the next one (but see kill_and_rejoin).
    # A bigger log buffer (if the device allows it) keeps the first lines from being pushed out.
    m.start()
    for p in (m.H, m.J): m.adb(p, 'logcat', '-G', '16M'); m.adb(p, 'logcat', '-c')

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
    # Returns the joiner's "rejoin:" lines, and what clean() found in its log up to the kill
    # (that log is cleared here, so the lines read after belong to the relaunched app).
    before = clean(m.J)
    m.adb(m.J, 'shell', 'am', 'force-stop', 'com.unofficial.ufo50'); time.sleep(10)
    m.adb(m.J, 'logcat', '-c')
    m.adb(m.J, 'shell', 'monkey', '-p', 'com.unofficial.ufo50', '-c', 'android.intent.category.LAUNCHER', '1'); time.sleep(26)
    m.cmd(m.J, 'rejoin')
    t = time.time()
    while time.time() - t < 45:
        time.sleep(3)
        if re.search(r'rejoin: back in the match|rejoin: (no answer|can.t|nobody|bad|the replay)', m.log(m.J)): break
    return re.findall(r'rejoin: .*', m.log(m.J)), before

def s3():
    fresh()
    m.cmd(m.H, 'save 1'); time.sleep(4)
    hs = re.findall(r'online save (\d+): slot (\d+) at frame (\d+)', m.log(m.H))
    m.key(m.H, 'DPAD_DOWN'); time.sleep(1)
    m.cmd(m.H, f'load {hs[-1][1]}'); time.sleep(3); m.cmd(m.J, 'accept'); time.sleep(5)
    r, before = kill_and_rejoin()
    verified = any('verified' in x for x in re.findall(r'load slot -1: reached.*', m.log(m.J)))
    ok, why = in_sync()
    bad = before or clean(m.H) or clean(m.J)
    say(3, any('back in the match' in x for x in r) and verified and ok and not bad, f'{r[-1:] }, replay verified {verified}; {why} {bad}')

def s4():
    fresh()
    m.cmd(m.J, 'netdrop 20'); time.sleep(40)
    dropped = 'network dropped for 20 s' in m.log(m.J)   # the joiner really got the command
    ok, why = in_sync()
    bad = clean(m.H) or clean(m.J)
    say(4, dropped and ok and not bad, f'network dropped {dropped}; {why} {bad}')

def s5():
    fresh()
    m.key(m.H, 'DPAD_RIGHT'); time.sleep(3)
    r, before = kill_and_rejoin()
    ok, why = in_sync()
    bad = before or clean(m.H) or clean(m.J)
    say(5, any('back in the match' in x for x in r) and ok and not bad, f'{r[-1:]}; {why} {bad}')

def s6():
    fresh()
    for k in ('DPAD_RIGHT', 'DPAD_RIGHT', 'DPAD_RIGHT'): m.key(m.H, k)
    m.cmd(m.H, 'save 1'); time.sleep(4)
    hs = re.findall(r'online save (\d+): slot (\d+) at frame (\d+)', m.log(m.H))
    before = clean(m.H) or clean(m.J)   # the first match's log goes away with the next line
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
    bad = before or clean(m.H) or clean(m.J)
    say(6, ver == ['verified', 'verified'] and ok and not bad, f'{ver}; {why} {bad}')

pick = [int(x) for x in sys.argv[1].split(',')] if len(sys.argv) > 1 else [1, 2, 3, 4, 5, 6]
for n in pick:
    try: [None, s1, s2, s3, s4, s5, s6][n]()
    except SystemExit as e: say(n, False, str(e))
    except Exception as e: say(n, False, repr(e))
print('\n'.join(results))
sys.exit(1 if any(': FAIL' in x for x in results) else 0)
