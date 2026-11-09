# Testing

How save states, rewind and online play were tested, the scripts that do it (in `tests/`), and
what hasn't been tested yet.

## Where it was tested

Everything was tested on **BlueStacks 5** (Android 9, x86_64) on Windows, with two instances side by
side for online play:

| instance | adb address | role in the online tests |
|---|---|---|
| Pie64 | `127.0.0.1:5555` | host |
| Pie64_1 | `127.0.0.1:5565` | joiner |

Turn on *Settings > Advanced > Android Debug Bridge* in each BlueStacks instance. The scripts use the
Android SDK's adb (`%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe`); set `ADB` to use another.
They need Python 3 with Pillow (`nav.py` reads the screen to find its way through the menus).

**Set the game's scale to 4x on every instance** (*Settings > Video Settings > Scale*) before
running the scripts: they find menu items by position, and these positions are for 4x. A fresh
install starts at a different scale.

## Test commands

The app listens for a few test commands, so scripts can drive it without
tapping: write a line to `files/ss_cmd` (the app reads it about twice a second, then deletes it),
and read the result from logcat, tag `PFOSTATE`.

```bash
adb -s 127.0.0.1:5555 shell "echo save 1 > /sdcard/Android/data/com.unofficial.ufo50/files/ss_cmd"
adb -s 127.0.0.1:5555 logcat -s PFOSTATE:*
```

| command | what it does |
|---|---|
| `solo` | START SOLO PLAY |
| `save <n>` | save into slot n (online: SAVE NOW, for everyone) |
| `load <n>` | load slot n: snapshot if there is one, else replay (online: propose the load) |
| `rload <n>` | load slot n by replay only, dropping its snapshot |
| `replay <n>` | the same, keeping the snapshot |
| `rewind <s>` | rewind s seconds (single player) |
| `accept` / `decline` | online: answer the load vote |
| `netdrop <s>` | cut this phone off the network for s seconds |
| `rejoin` | rejoin the last match (as REJOIN does) |
| `where` | log which game is running, the frame, and how many players are connected |
| `hash [n]` | development: log a hash of the game state |
| `sleep` | a 550 ms hitch, nothing else (to test that hitches don't matter) |
| `probe`, `snap`, `unsnap` | development: capture and restore checks |

The log lines the scripts look for include `save slot N: frame ...`, `load slot N: ... from its
snapshot in N ms ... verified`, `reached frame N in N ms ... N replay checksums verified`,
`rewind: ... verified`, `rejoin: back in the match at frame N` and `where: game G, frame F,
clients C`. A refused request says why (`save slot 1: refused (...)`).

## Switches

Files in `/sdcard/Android/data/com.unofficial.ufo50/files/` that change how the app behaves:

| file | effect |
|---|---|
| `forcerelay` (empty) | join through the relay only |
| `okplay.txt` (a URL) | use another room server |
| `snapdebug` | development: a snapshot also remembers the whole game state one frame later, and a load compares against it (logcat `PFOSNAP`) |
| `snapdiffat` (`<frame>` or `<first> <step> <count>`) | development: live play remembers the whole state at those frames, a replay compares there |
| `inputlog` | development: every frame's input and random-generator position go to `inputlog_live.txt` / `inputlog_replay.txt`, to compare the two |

Remove the development switches after use: they slow saves and loads down.

## The scripts

All are in `tests/`; run them with Python 3 from anywhere.

| script | what it checks | usage |
|---|---|---|
| `sweep4.py` | **Every game's save states.** Per game: a fresh app, solo session, opens the game through the library with key presses, plays, saves, leaves to the library (or into another game, or stays), loads (snapshot + its 120-frame check), plays, saves again, then loads that save by replay only. Every step checks which game is running; a run that went astray is retried from a fresh app (up to 3 tries) and kept apart from real failures, with its logcat, screen and files kept as `noresult_*`. | `sweep4.py 1,2,3 out.txt [library\|other\|same]` (game numbers as in the library, 1-50) |
| `rewind_test.py` | **Rewind.** Solo session, opens Velgress, plays with a pause (so there's a keyframe), then rewinds 15 s and 60 s; each must land on its frame with the replay checksums matching. | `rewind_test.py [port]` |
| `mp_test.py` | **One online save and load.** Starts both apps, hosts a public lobby, joins, readies, picks a profile; the host saves, plays on, proposes loading; the joiner accepts or declines. Checks both logs: same save frame and checksum, same load frame, no desync after. | `mp_test.py [accept\|decline] [--no-start]` |
| `mp_regress.py` | **Every online scenario**, one after another, each judged automatically: 1 save + accepted load, 2 declined load, 3 rejoin through an online save and load, 4 a 20-second network drop, 5 the joiner's app killed and rejoining, 6 loading a save from an earlier match. | `mp_regress.py [1,4,5]` |
| `cmd_stress.py` | **The test channel itself**: sends `where` N times and counts lost commands (a harness check). | `cmd_stress.py [port] [count]` |
| `nav.py` | The helper the others use: adb calls, key presses, screenshots, and finding menus by comparing with the pictures in `nav_refs/`. | |

`mp_test.py` writes its logs and screenshots to `tests/out/`.

## Results for v0.3

On the build in this release:

| test | result |
|---|---|
| `sweep4.py` all 50 games, loading in the same room, from the library and from inside another game | **150 of 150** pass: snapshot loads verified, replay through them verified |
| 40 single-player save + load runs back to back, after the audio crash fix | **40 of 40** clean, no retries (5 crashes and 7 retries before the fix) |
| `mp_regress.py` 1-6 | **6 of 6** pass: both phones on the same game, frame and checksum afterwards |
| `rewind_test.py` 15 s and 60 s | **2 of 2** pass, both verified |

Measured on BlueStacks: a snapshot load takes about 1 second; replays run at about 550-1,100 frames
a second, so a replayed load or rewind takes a few seconds per minute of play.

## Not tested yet

- **A real phone, and arm64.** The kit builds `libPFO.so` for arm64-v8a and x86_64, but all testing
  above ran on x86_64 BlueStacks. Save states read the GameMaker runner's internal structures, and
  those were worked out and checked on x86_64 only.
- **Three or more players online**: with two BlueStacks instances only, rejoining and the shared
  inputs of a dropped player were tested with two players.
- **Online saves over the relay with a slow connection**, beyond the relay rejoin in scenario 5.
- **Hot seat chips** on screen in a hot-seat game (the logic was tested, not the drawing).
- Android 13 and later (the notification permission, background limits).

If you test any of these, please open an issue with what you saw.
