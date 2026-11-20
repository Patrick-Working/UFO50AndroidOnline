"""Where two traces of the same match first differ.

A trace is a folder of files, one per frame (<frame>.txt): libPFO writes them while tracing
(ss_cmd "trace <frames>", or files/tracerejoin for both ends of a rejoin; see savestate.inc
TraceFrame). Each holds a "frame" line, a "picture" line (the application surface's md5) and then
the whole game state, a line per item: name, fingerprint, short value. Instances are named by their
place in the canonical order: "<n>:<object>.<variable>".

  python tracediff.py <trace folder A> <trace folder B> [--all] [--max N]

Prints, for the first frame both have that differs: which objects' counts differ, then the items
whose fingerprints differ (with both short values), then the items only one side has. --all goes on
through every later frame (a line each: how many items differ). Exit code 0 when the traces agree on
every frame both have, 1 when they don't, 2 when they have no frame in common.
"""
import os, re, sys
from collections import Counter

# per-device items: not part of the game state both devices share
DEVICE = re.compile(r"^global\.(pfo_local_seat|pfo_seat_|pfo_snap|scale$|scaleMax$|scaleFill$|DEFAULT_SCALE$|"
                    r"fullscreen$|dispBordered$|scanlines$|windowHasFocus$|canDoShaders$|bufferSurface$|"
                    # the sounds playing (handles of this device's audio)
                    r"SFX$|SFX_\w+$|BGM$|onlineBackupDefaultLanguage$)"
                    # sound handles, and the always-there managers' animation (counts from the process's start)
                    r"|^\d+:oAudioHandler\.(bgm\w+|image_index)$|^\d+:oAchManager\.image_index$"
                    # the pause menu's own: its sound, how long since this device's key
                    r"|^\d+:oPauseMenu\.(currentSound|inputListenerTimer)$"
                    # the "catching up" notice is this device's own
                    r"|^\d+:oController\.showingCatchupWarning$"
                    # oController's notices ("<name> Disconnected") are each device's own
                    r"|^\d+:oController\.alertMessage"
                    # each device's own: its keys' pressed/released this frame, its window's scale
                    r"|^\d+:oController\.(fire\d\w*|press\w+|hold\w+)$|^\d+:oScreenHandler\.")


def frames(folder):
    out = {}
    for f in os.listdir(folder):
        m = re.match(r"(\d+)\.txt$", f)
        if m: out[int(m.group(1))] = os.path.join(folder, f)
    return out


def read(path):
    head, items = {}, {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f):
            line = line.rstrip("\n")
            if i == 0: head["frame"] = line; continue
            parts = line.split("\t")
            if parts[0] == "picture": head["picture"] = parts[1] if len(parts) > 1 else ""; continue
            if parts[0] == "instancefix": head["fix"] = int(parts[1]); continue
            if len(parts) < 2: continue
            if DEVICE.match(parts[0]): continue
            items[parts[0]] = (parts[1], parts[2] if len(parts) > 2 else "")
    # a restore's step: its dropped instances still count (libPFO's instancefix line says how many)
    c = items.get("builtin.instance_count")
    if c and head.get("fix"):
        n = str(int(c[0]) - head["fix"]); items["builtin.instance_count"] = (n, n + " (corrected by %d)" % head["fix"])
    return head, items


def objects(items):
    c = Counter()
    for k in items:
        m = re.match(r"(\d+):([^.]+)\.x$", k)
        if m: c[m.group(2)] += 1
    return c


LOCAL_VALUE = re.compile(r"^ref (surface|buffer|sound_instance|audio_emitter|camera) ")


def compare(a, b):
    # a surface, buffer or sound is this device's own: its handle can't be compared
    diff = [k for k in a if k in b and a[k][0] != b[k][0] and not (LOCAL_VALUE.match(a[k][1].split(" ", 1)[-1]) and LOCAL_VALUE.match(b[k][1].split(" ", 1)[-1]))]
    only_a = [k for k in a if k not in b]
    only_b = [k for k in b if k not in a]
    return diff, only_a, only_b


def main(argv):
    every = "--all" in argv
    most = 40
    if "--max" in argv: most = int(argv[argv.index("--max") + 1])
    paths = [x for i, x in enumerate(argv) if not x.startswith("--") and (i == 0 or argv[i - 1] != "--max")]
    if len(paths) != 2:
        print(__doc__); return 2
    fa, fb = frames(paths[0]), frames(paths[1])
    common = sorted(set(fa) & set(fb))
    if not common:
        print("no frame in common (A: %s, B: %s)" % (span(fa), span(fb))); return 2
    print("A: %s\nB: %s\nin common: %d frames, %d to %d" % (span(fa), span(fb), len(common), common[0], common[-1]))
    first = None
    for fr in common:
        ha, a = read(fa[fr]); hb, b = read(fb[fr])
        diff, only_a, only_b = compare(a, b)
        same_pic = ha.get("picture") == hb.get("picture")
        bad = diff or only_a or only_b
        if first is None and bad:
            first = fr
            print("\nfirst difference: frame %d (%s / %s)%s" % (fr, ha.get("frame"), hb.get("frame"), "" if same_pic else ", the pictures differ too"))
            oa, ob = objects(a), objects(b)
            counts = sorted(o for o in set(oa) | set(ob) if oa[o] != ob[o])
            if counts:
                print("  objects whose counts differ (A / B):")
                for o in counts[:most]: print("    %-28s %4d / %d" % (o, oa[o], ob[o]))
            if diff:
                print("  %d items differ:" % len(diff))
                for k in diff[:most]: print("    %-44s A: %-34s B: %s" % (k, a[k][1][:34], b[k][1]))
            for name, only, side in (("A", only_a, a), ("B", only_b, b)):
                if only:
                    print("  %d items only in %s:" % (len(only), name))
                    for k in only[:min(most, 12)]: print("    %-44s %s" % (k, side[k][1]))
            if not every: break
        elif every and first is not None:
            print("frame %d: %d differ, %d only in A, %d only in B%s" % (fr, len(diff), len(only_a), len(only_b), "" if same_pic else ", pictures differ"))
        elif not bad and not same_pic and first is None:
            print("frame %d: the same state, but the pictures differ" % fr)
    if first is None:
        print("\nthe same on every frame both have")
        return 0
    return 1


def span(f):
    return "%d frames, %d to %d" % (len(f), min(f), max(f)) if f else "no frames"


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
