"""The Windows interface, looked at step by step: the menu by keyboard and by the test controller,
the saves page with its pictures, the confirm page, the loading screen. Pictures of every step go
to the folder given (they show the game: keep them private).

  python uitest.py <folder for the pictures> [box]

Boxed in like every test game here (see wingame.py). Game 1 (BARBUTA), solo play.
"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wingame, sweep


def main():
    out = sys.argv[1]; box = int(sys.argv[2]) if len(sys.argv) > 2 else 16
    os.makedirs(out, exist_ok=True)
    report = open(os.path.join(out, "uitest.txt"), "w", buffering=1, encoding="utf-8")
    g = sweep.Game(box, report)
    bad = []
    n = [0]

    def shot(name):
        n[0] += 1
        wingame.shot(os.path.join(out, "%02d_%s.png" % (n[0], name)), g.hwnd)

    def expect(what, pattern, timeout=6.0):
        t = time.time()
        while time.time() - t < timeout:
            m = re.search(pattern, g.log_all_run())
            if m: g.say("ok   %s: %s" % (what, m.group(0).strip()[:100])); return m
            time.sleep(0.2)
        g.say("FAIL %s (no '%s' in the log)" % (what, pattern)); bad.append(what)
        return None

    def since(pattern, timeout=6.0):
        t = time.time()
        while time.time() - t < timeout:
            m = re.search(pattern, g.log())
            if m: return m
            time.sleep(0.2)
        return None

    def step(what, pattern, timeout=6.0):
        m = since(pattern, timeout)
        g.say(("ok   %s: %s" % (what, m.group(0).strip()[:100])) if m else "FAIL %s (no '%s' in the log)" % (what, pattern))
        if not m: bad.append(what)
        return m

    # old saves and pictures of this box out of the way
    saves = os.path.join(g.dir, "savestates")
    if os.path.isdir(saves):
        for f in os.listdir(saves): os.remove(os.path.join(saves, f))
    # a player name for the pictures (without one the game shows the computer's name), and no UPnP
    os.makedirs(g.dir, exist_ok=True)
    with open(os.path.join(g.dir, "menu.cfg"), "wb") as f: f.write(b"name=TEST1\nupnp=0\nautosave=0\n")
    g.run_start = os.path.getsize(g.logpath) if os.path.exists(g.logpath) else 0
    try:
        if not g.start() or not g.wait_ready(): raise SystemExit("the test game did not start")
        expect("the menu's window is there", r"menu ready \(F1\)", 15)
        expect("the controller hook", r"controller: (ready|not the game version|the game's XInput isn't)[^\n]*", 15)
        # (the game asks for its controllers only with a real one plugged in and its window in front:
        # the line saying it does so through the menu can't be counted on here)
        shot("start")
        if not g.to_library(): raise SystemExit("did not reach the library")
        g.cmd("solo"); time.sleep(3)
        g.to_library(20)
        g.open_game(1); g.mash(4)

        # the menu by keyboard
        g.mark(); g.key("f1", 0.6)
        step("F1 opens the menu", r"menu: open \(([^\n]*)\)"); shot("menu_keys")
        g.mark(); g.key("f1", 0.4)
        step("F1 closes it", r"menu: closed")

        # two saves, each with its picture
        for slot in (1, 2):
            g.mark(); g.cmd("save %d" % slot)
            step("save %d" % slot, r"save slot %d: frame" % slot, 10)
            step("its picture", r"picture: slot %d (saved|has none[^\n]*)" % slot, 10)
            g.mash(3)
        for slot in (1, 2):
            p = os.path.join(saves, "slot%d.png" % slot)
            ok = os.path.exists(p) and os.path.getsize(p) > 2000
            g.say(("ok   " if ok else "FAIL ") + "slot%d.png is there (%d bytes)" % (slot, os.path.getsize(p) if os.path.exists(p) else 0))
            if not ok: bad.append("slot%d.png" % slot)

        # the test controller: both sticks open the menu
        g.mark(); wingame.pad("l3+r3", g.hwnd); time.sleep(0.5)
        step("both sticks open the menu", r"menu: open"); shot("menu_pad")
        expect("a controller is noticed", r"controller: one is connected", 2)
        g.mark(); wingame.pad("down", g.hwnd); wingame.pad("a", g.hwnd); time.sleep(0.6)
        step("down, A: the saves page", r"menu: LOAD A SAVE"); shot("saves_pad")
        wingame.pad("down", g.hwnd); time.sleep(0.4); shot("saves_pad_second")
        wingame.pad("x", g.hwnd); time.sleep(0.4); shot("saves_delete_armed")
        time.sleep(2.8); shot("saves_delete_lapsed")
        g.mark(); wingame.pad("b", g.hwnd); time.sleep(0.4); shot("back_to_menu")
        wingame.pad("b", g.hwnd)
        step("B, B: closed", r"menu: closed")

        # by keyboard: the saves page, load the first save in the list
        g.key("f1", 0.5); g.key("down", 0.3); g.key("enter", 0.6); shot("saves_keys")
        g.mark(); g.key("enter", 0.3)
        step("enter on a save", r"menu: ([^\n]*)")
        cover = since(r"cover: on \(([^\n]*)\)", 3)
        if cover is None:
            # a load that takes a while asks first
            shot("confirm"); g.key("enter", 0.2)
            cover = since(r"cover: on \(([^\n]*)\)", 8)
        g.say(("ok   " if cover else "FAIL ") + "the loading screen" + (" (%s)" % cover.group(1) if cover else ""))
        if not cover: bad.append("cover")
        shot("cover")
        step("the load", r"load slot \d+: [^\n]*(verified|MISMATCH|refused)[^\n]*", 60)
        step("the loading screen goes", r"cover: off", 10)
        time.sleep(0.5); shot("after_load")

        # a replay-only load takes longer: the loading screen's bar
        g.mash(2); g.mark(); g.cmd("rload 2")
        if since(r"cover: on", 10):
            for i in range(3): time.sleep(0.7); shot("cover_replay_%d" % i)
        step("the replay load", r"load slot 2: reached frame[^\n]*", 120)
        step("the loading screen goes", r"cover: off", 10)

        # rewind
        g.mash(3); g.mark(); g.cmd("rewind 2")
        step("rewind", r"(rewind[^\n]*|load slot -?\d+: [^\n]*)", 30)
        step("the rewind's end", r"load slot -2: reached frame[^\n]*", 30)
        time.sleep(2); shot("after_rewind")
        wingame.pad("unplug", g.hwnd)

        # a save that takes a while to load is asked about first. After a restart a save loads by
        # replaying its whole session, so: a long session, a save, the game started again
        t0 = time.time(); f = 0
        while time.time() - t0 < 200:
            g.mash(5); f = g.where()[1] or f
            if f >= 7000: break
        g.mark(); g.cmd("save 3")
        step("save 3 (at frame %d)" % f, r"save slot 3: frame", 10)
        step("its picture", r"picture: slot 3 (saved|has none[^\n]*)", 10)
        g.mash(1)
        if not g.start() or not g.wait_ready(): raise SystemExit("the test game did not start again")
        if not g.to_library(): raise SystemExit("did not reach the library")
        g.cmd("solo"); time.sleep(3)
        g.to_library(20)
        g.key("f1", 0.5); g.key("down", 0.3); g.key("enter", 0.8); shot("saves_after_restart")
        g.mark(); g.key("enter", 0.6)
        asked = since(r"cover: on", 1.5) is None
        g.say(("ok   " if asked else "FAIL ") + "a long load is asked about first")
        if not asked: bad.append("confirm page")
        shot("confirm")
        if asked: g.key("enter", 0.2)
        step("the loading screen", r"cover: on \(([^\n]*)\)", 8)
        for i in range(4): time.sleep(1.0); shot("cover_long_%d" % i)
        step("the long load", r"load slot 3: [^\n]*(verified|MISMATCH|refused)[^\n]*", 180)
        step("the loading screen goes", r"cover: off", 10)
        time.sleep(0.5); shot("after_long_load")

        m = re.search(r"controller: the game reads its controllers through the menu", g.log_all_run())
        g.say("note " + ("the game read its controllers through the menu" if m else "the game didn't ask for its controllers (no real one, or its window not in front)"))
        crashes = re.findall(r"PFOCRASH[^\n]*", g.log_all_run())
        for c in crashes: g.say("FAIL " + c)
        bad.extend(crashes)
    finally:
        g.stop()
    g.say("%d not as expected" % len(bad) if bad else "all as expected")
    wingame.check()
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
