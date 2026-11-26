# Testing

How save states, rewind and online play were tested, the scripts that do it (in `tests/` for
Android, in `tests/win/` for the Windows version), and what hasn't been tested yet.

## Where it was tested

The Android build was tested on **BlueStacks 5** (Android 9, x86_64) on Windows, with two instances
side by side for online play. The Windows version was tested on the same PC (Windows 10, x86_64):
two copies of the game side by side, and one copy with a BlueStacks instance
(see [The Windows version](#the-windows-version) below).

| instance | adb address | role in the online tests |
|---|---|---|
| Pie64 | `127.0.0.1:5555` | host |
| Pie64_1 | `127.0.0.1:5565` | joiner |

Turn on *Settings > Advanced > Android Debug Bridge* in each BlueStacks instance. The scripts use the
Android SDK's adb (`%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe`); set `ADB` to use another.
They need Python 3. All but `rewind_test.py` and `cmd_stress.py` also need Pillow: `mp_test.py`,
`mp_regress.py` and `nav.py` compare screenshots with reference pictures, and `sweep4.py` imports
`nav.py`.

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
| `trace <frames>` | testing: write the whole game state each new frame for that many frames (default 60) to `trace/<tag>/<frame>.txt`; compare two devices' with `tests/tracediff.py` |
| `nosnap` | online, from a player with a seat: tell the match this device has no snapshots, as a Windows game whose snapshots are off does when a match starts. From then on saves are files only and every accepted load restarts the match from the save. It can't be taken back in that match. |
| `rejoin` | rejoin the last match (as REJOIN does) |
| `where` | log which game is running, the frame, and how many players are connected |
| `hash [n]` | development: log a hash of the game state |
| `sleep` | a 550 ms hitch, nothing else (to test that hitches don't matter) |
| `probe`, `snap`, `unsnap` | development: capture and restore checks |

The log lines the scripts look for include `save slot N: frame ...`, `load slot N: from its
snapshot in N ms ... verified`, `load slot N: reached frame N in N ms ... N replay checksums ...:
verified`, `rejoin: back in the match at frame N` and `where: game G, frame F, clients C`. A rewind
logs `rewind to frame N: ... estimate N ms` when it starts. Its result is a load line with slot -2:
`load slot -2: reached frame N ... verified`. A refused request says why
(`save slot 1: refused (...)`).

The Windows version takes the same commands: the file is `%LOCALAPPDATA%\ufo50_online\ss_cmd` (read every 30
frames) and the results are lines in `pfo_online.log` in the same folder. For a test game started
by `tests/win/wingame.py` that folder is in the game's sandbox (`python wingame.py home 1`).

## Switches

Files in `/sdcard/Android/data/com.unofficial.ufo50/files/` that change how the app behaves:

| file | effect |
|---|---|
| `forcerelay` (empty) | join through the relay only |
| `okplay.txt` (a URL) | use another room server |
| `okplay.txt` (`off`) | no room server at all: what the tests that play alone use (the sweeps, quick slot, rewind write it; `mp_test.py` removes it) |
| `snapwire` | testing: every save's snapshot goes through bytes and back as a fast rejoin's does (both its halves: the game's values and the runner's lists), so its load's check tests that; the log says what didn't cross exactly (`snapwire N compared: ...`) |
| `rejoinreplay` | testing: a rejoin asks for the match's replay, as builds before fast rejoins did |
| `snapfail` | testing: every snapshot load fails its check on purpose, so the replay it falls back to runs (a sweep with it on tests the fallback in every game) |
| `picdebug` | development: the pictures a load compares are saved as PNGs (`picdbg_<slot><frame>.png` after a save, `picdbg_<slot>5<frame>.png` in the load's check), and a picture that differs is logged with both fingerprints |
| `snapdebug` | development: a snapshot also remembers the whole game state one frame later, and a load compares against it (logcat `PFOSNAP`) |
| `snapdiffat` (`<frame>` or `<first> <step> <count>`) | development: live play remembers the whole state at those frames, a replay compares there |
| `replay_speed.txt` | not a switch: the replay speed this device has measured in each game (`<game id> <frames a second>`, `-100` overall). Delete it to start over. |
| `inputlog` | development: while a session runs, every frame's input and random-generator position go to `inputlog_live.txt` (live play), `inputlog_replay.txt` (a replay) and `inputlog_check.txt` (the 120-frame check after a snapshot load), to compare them |
| `inputprobe` | development: logs every change in which player keys and gamepad buttons the engine sees held (logcat `PFOPROBE`). The app looks for the file once, when it starts. |
| `speedtest` (`<speed multiplier> <draw 0 or 1>`) | development: sets PFO's game speed multiplier and turns drawing on or off, and logs the game steps per second (logcat `PFOSPEED`). The file must be there when the app starts; its contents are read again every second. |
| `turntest` (`<players>`) | development: as if a turn-based game of that many players reported its turns, a new one every 2.5 s (the hot seat follows); remove the file to stop |
| `hitchlog` | development: a game step more than 50 ms after the one before is logged (logcat `PFOHITCH`) |
| `tracerejoin` (`<frames>`) | testing: a rejoin's sender and the rejoiner each trace that many frames from the rejoin on (as `trace` does); `tracetag` holds the folder name (else `t`). `duo.py` sets both when `TRACE=<frames>` is in the environment. |
| `nofx` | testing: a save or load gives its tick but no picture flying over the game, so screenshots show the game alone. `mp_test.py` and `cross.py` set it when they start the app. |
| `opensetup` | testing: opens WHO'S PLAYING once (the file goes), as the Players panel's button does; with `testpad` and `testpress` a test can seat the stand-in pad. |
| `testpad` | development: adds a stand-in gamepad called "Test Pad (debug)" (the emulator has no gamepads). While a file `testpress` is there too, its A button is held down. |

Remove the development switches after use: they slow saves and loads down.

## What a load checks

A snapshot load puts the state back, then plays the 120 frames that followed the save flat out
(hidden behind the loading screen) and compares two things with what was noted when they were
first played: PFO's checksum of every frame, and the picture the game drew (its 384x216 surface)
at three of them. The picture check catches what the checksums don't cover: anything that only
shows. It is how the tiles some games draw one by one were found (a fallen block stayed on screen
after a load in Barbuta; game 48's floor came back black after a load through the library).
Anything different and the snapshot doesn't count: the load replays the session instead, which is
slower but always right. The log says which frame differed (`snapshot didn't check out (the
picture at frame 21 of 120 differs), replaying instead`), and a good load says how many pictures
it compared (`... 120 frames checked, 3 pictures: verified`).

A snapshot also keeps GameMaker's drawing state (colour, alpha, font, text alignment, blend mode):
some games draw text without setting a colour, and after a load from another game drew it in that
game's last colour.

Before putting the state back, a load waits for the saved game's textures (each game's are loaded
in the background): `load slot N: waited N ms for the game's textures`.

## The scripts

All are in `tests/`; run them with Python 3 from anywhere.

| script | what it checks | usage |
|---|---|---|
| `sweep4.py` | **Every game's save states.** Per game: a fresh app, solo session, opens the game through the library with key presses, plays, saves, leaves to the library (or into another game, or stays), loads (snapshot + its 120-frame check, with the pictures it compared: a load that compared none doesn't count), plays, saves again, then loads that save by replay only. Every step checks which game is running; a run that went astray is retried from a fresh app (up to 3 tries) and kept apart from real failures, with its logcat, screen and files kept as `noresult_*`. A game-code error ("ERROR in action"), a crash or a failed assertion anywhere in a run is a result of its own, `GAME ERROR`, never retried. | `sweep4.py 1,2,3 out.txt [library\|other\|same]` (game numbers as in the library, 1-50; `SWEEP_PORT=5565` for the second instance) |
| `disconnect.py` | **A load after DISCONNECT.** Solo play, a save, back to the library, DISCONNECT; the load must replay the save's recording (not its snapshot), verified, with no game-code error. | `disconnect.py [port]` |
| `quickslot.py` | **Quick save and quick load, and the overlay's keys.** F5 before solo play is refused with the banner saying how to start it; in Barbuta F5 saves the QUICK slot, walking right makes the loose block fall, F9 loads the quick save from its snapshot with its pictures compared, and the room is as it was saved (no block left drawn). F1 opens the save list, an arrow moves the outline, Esc closes it; both sticks pressed in open it, B closes it; with the controller alone the quick save is picked in the list and loaded. No game-code error or crash in the log. | `quickslot.py [port]` |
| `rewind_test.py` | **Rewind and its estimates.** Solo session, opens Velgress, plays with a pause (so there's a keyframe), then rewinds 15 s and 60 s; each must land on its frame with the replay checksums matching. Then twice: plays 2.5 minutes, rewinds 60 s, and compares the time it took with its estimate (it fails if it took over 25% longer). Prints the learned replay speeds. | `rewind_test.py [port]` |
| `mp_test.py` | **One online save and load.** Starts both apps, hosts a public lobby, joins, readies, picks a profile; the host saves, plays on, proposes loading; the joiner accepts or declines. Then it judges both logs: the save has the same frame and checksum on both phones; an accepted load ends on the same frame on both, and a declined one loads nothing; no desync or crash line in the log of the whole test, which ends 15 s after the vote. The last line is `mp_test: PASS` or `mp_test: FAIL (why)`. | `mp_test.py [accept\|decline] [--no-start]` (`--no-start`: use the match that is already running) |
| `mp_regress.py` | **Every online scenario**, one after another, each judged automatically: 1 save + accepted load, 2 declined load, 3 rejoin through an online save and load, 4 a 20-second network drop, 5 the joiner's app killed and rejoining, 6 loading a save from an earlier match. Each scenario checks its own log lines (in 1 the save's frame and checksum and the load's frame are the same on both phones, in 4 the drop really happened, in 3 and 6 the replays are verified). After each one both phones answer `where`: both must say 2 players and the same game, their frame numbers must be within 120 of each other and still going up, and the same game must be picked on both screens. Checksums are not compared at this point: PFO compares them during play, and the script fails a scenario when a "Desync detected" line, a crash or a failed assertion is anywhere in that scenario's log. | `mp_regress.py [1,4,5]` |
| `mp_nosnap.py` | **A match without snapshots between two phones**: what the phones do in a match with a player whose snapshots are off (a Windows game with another `ufo50.exe`), without that player. The host sends `nosnap`; both phones must say the match has no snapshots. Then an online save (the same frame and checksum on both, no snapshot taken), a two-player game, a save there by the joiner, a load of it proposed by the joiner and accepted by the host (both replay to the save's frame, verified, and connect again), and a load back to the first save proposed by the host. No desync, crash, failed assertion or snapshot load may be in either log. | `mp_nosnap.py [--no-start]` |
| `cmd_stress.py` | **The test channel itself**: sends `where` N times and counts lost commands (a harness check). | `cmd_stress.py [port] [count]` |
| `nav.py` | A helper for adb calls, key presses and screenshots. Only `sweep4.py` imports it, for those three things. Run by itself it starts an online session on two instances (host a public lobby, join, ready both), finding each menu by comparing the screen with the pictures in `nav_refs/`. `NAV_SCALE` is the game's scale (default 4). | `nav.py session [host port] [join port]` |

`mp_test.py`, `mp_regress.py` and `nav.py` save their screenshots in `tests/out/`.
`mp_nosnap.py` uses `mp_test.py`'s helpers, so it needs Pillow too.

Every script but `nav.py` ends with exit code 0 when all its checks passed and 1 when one failed
(`sweep4.py`: when a game's snapshot load or replay was not verified, or the game could not be run).
`nav.py` exits with an error message when a screen never shows.

## The Windows version

The helpers in `tests/win/` drive the game folder the build made (`work\windows\UFO 50 Online`).
They need Python 3 with Pillow, and nothing else.

**They never run the test game as your own game.** `wingame.py` starts each test game with a user
profile folder of its own (`work/win-sandbox/<n>`), so its saves, settings and log land there and
not in your `%LOCALAPPDATA%`. It refuses a game folder that has a Steam library or
`steam_appid.txt` in it. It takes a fingerprint of the Steam game's save folder
(`%LOCALAPPDATA%\ufo50`) before the first start, and `check` and `stop` compare it again
("real save folder untouched"). Only processes started from the test folder are looked at, pressed
or closed. Keys are posted to the game's window, so it needn't be in front, and you can use the PC
meanwhile.

The test games use oldkid.net's rooms like any other copy (a test lobby shows in the public list
for a few seconds), and hosting asks the router for a port forward as the game always does. Lines
of the log that hold addresses are never printed. Pictures go to `work/win_duo`; they show the
game, so keep them to yourself.

| script | what it checks | usage |
|---|---|---|
| `wingame.py` | The sandbox, and the helper the others use: start and stop test games, pictures of the window, key presses, and presses of the menu's test controller (a message the build takes as a controller's buttons; no driver). `WINGAME_DIR` names another game folder to test. | `wingame.py start [n]`, `stop`, `check`, `home [n]`, `shot out.png`, `keys z z down z`, `keys pad:l3+r3 pad:a` |
| `sweep.py` | **Save states, game by game** (the Windows twin of `sweep4.py`): save, play on, leave to the library (or into another game, or stay), load from the snapshot with the 120-frame check (and the pictures it compared: none doesn't count), save again, then a load by replay through the snapshot load. A game that shows the Code Error box or closes by itself is a result, `GAME ERROR`, never retried. | `sweep.py 1,2,3 out.txt [library\|other\|same] [box]`, `sweep.py all out.txt library x5` |
| `quickslot.py` | **Quick save and quick load** (the twin of `tests/quickslot.py`): F5 before solo play refused, F5 / F9 in Barbuta with the loose block falling in between, the load verified with its pictures, then the controller alone (both sticks in, down to QUICK LOAD (F9), A). Keys go to its own window only. | `quickslot.py [box]` |
| `rewind.py` | **Rewind and its estimates** (the twin of `tests/rewind_test.py`). | `rewind.py [box]` |
| `disconnect.py` | **A load after DISCONNECT** (the twin of `tests/disconnect.py`). | `disconnect.py [box]` |

`tests/regress.py [quick|full]` runs them all one after another (both sweeps on a 5-game sample, or
all 50 with `full`) and ends with one table (`work/regress/summary.txt`).

`duo.py` and `cross.py` check that a rejoin takes the match as it is (a fast rejoin: "rejoin: the
game put back at frame R, N ms after asking"); with `REJOIN_REPLAY=1` (duo.py) the rejoining game
asks for the replay instead and the check expects that. `SNAPWIRE=1` on `sweep.py` (both) sends
every save's snapshot through bytes and back (`files/snapwire`), so the loads' checks (120 frames
and three pictures) test what a fast rejoin's snapshot goes through.
| `uitest.py` | **The menu, step by step**, by keyboard and by the test controller: every page, the saves list with pictures, the confirm page, the loading screen, with a picture of each step. | `uitest.py <folder for the pictures> [box]` |
| `duo.py` | **Two Windows games in one match.** A private lobby joined by its code; an online save (the same frame and checksum on both, a snapshot and a picture on both); a two-player game; a save there; a load that is declined (the box asking shows the save's picture); a load that is accepted (both put the snapshot back on the same frame and carry on); a library save loaded from inside the game; the joiner killed, started again and rejoining; no desync lines, no error box, and no save files piling up. With `PFO_NO_SNAPSHOTS=1` set it expects a match of files only (every accepted load restarts the match from the save by replay). | `duo.py all` (or `start`, `test`, `stop`) |
| `pauseseat.py` | **Who controls which player** in a Windows + emulator match: before and after the emulator's pause auto save, after the emulator is killed and rejoins while the Windows game is paused, and after it is killed in its own pause menu and rejoins (the input traces on the Windows game). `REJOIN_REPLAY=1` makes the rejoins replay. | `pauseseat.py [port]` |
| `trio.py` | **Three players**: the Windows game hosts, both emulators join; saves, an emulator and then the Windows game killed and rejoining. Not passing yet: the two emulators can't reach each other directly, and through the relay a player can reach the host but not another player (okplay's relay; see below). | `trio.py` |
| `rejoin.py` | **Fast rejoins, each way.** Two Windows games in a match (in the library, or with `game` in a game for two); the joiner's game killed, started again and rejoining by the match's state, then the host's; after each, both in the same place and a save made on the same frame with the same checksum on both; no desync lines, no error box. With `REJOIN_REPLAY=1` the rejoins replay instead. | `rejoin.py [library\|game]` |
| `cross.py` | **A Windows game and an Android emulator in one match.** Both in the online library; a save asked for by each side (the same frame and checksum on both: the two builds compute the same game); a two-player game; a load asked for by each side and accepted by the other; a minute of play with a save at the end; each side's app killed and rejoining. | `cross.py win [port]` (Windows hosts), `cross.py android [port]` (the emulator hosts) |
| `seats_phone.py` | **Several players on a phone, online.** The phone (or an emulator) hosts and seats two players on WHO'S PLAYING (`files/opensetup`, the debug test pad): touch in P1, the pad in P2; a Windows test game joins (P3). Three seats on both; the pad's presses reach both games; a save by the phone and the Windows game's load of it on the same frame on both; the phone's app killed and back by a fast rejoin with the seats; a save after it; no desync. | `seats_phone.py <adb serial or port>` |
| `splitscreen.py` | **Save states across MultiMadnessMod's split screen.** Onion Delivery (library 23) for four players on one device: a save while split (four players start apart), one in the library (one screen), then loads back and forth twice; each load's own picture check must pass and the screen handler's flag must match the save. | `splitscreen.py [game]` |
| `rejoin_sweep.py` | **A fast rejoin into every game, checked item by item.** Two test games in one private lobby; for each game: the host opens it, both play, the joiner is killed and rejoins, and both trace the whole state for 60 frames from the rejoin (`TRACE=<frames>` sets `tracerejoin`); then back to the library, traced too (`TRACE_LEAVE`, 200 frames). `tests/tracediff.py` compares each pair. Per game: `ok`, or the first frame and items that differ. Traces and reports in `work/rejoin_sweep/g<n>/`. `TRACE=0 TRACE_LATE=<s>`: trace from s seconds after the rejoin instead (see the note under the results); `NOREJOIN=1`: the same without the rejoin, for comparison; `WINGAME_BACK=1`: the windows stay behind yours. Don't run it while the emulators run lobby tests (`mp_regress.py`): the host could land in their lobby. | `rejoin_sweep.py 1,2,3\|all` |
| `seats_rejoin.py` | **A fast rejoin of a device with several players**, in a game for one player and in one for three (two Windows test games; `plain`: one player each, for comparison). | `seats_rejoin.py [1\|3\|both] [plain]` |
| `seats_online.py` | **Several players on one PC, online.** Two test games in a private lobby: the host seats two players (WHO'S PLAYING: the arrow keys in P1, the numpad keys in P2), the joiner one. Three seats on both; the numpad player's input reaches both games; a save by the joiner with the same frame and checksum on both; the host's load of it on the same frame on both; the joiner killed and back by a fast rejoin with the seats as they were; a save after it. | `seats_online.py` |
| `vergate.py` | **Two versions of the Windows build can't share a lobby.** The other version hosts and this one joins, then the other way round. Each join must be refused with the log line that names the versions, both games must stay up, and the host must still have its lobby. | `vergate.py <game folder of the other version>` |
| `xgate.py` | **The same between an emulator (this version) and another version's Windows build**, both ways. The app must still answer afterwards, with no crash or failed assertion in its log. | `xgate.py <game folder of the other version> [port]` |
| `stacks.py` | Not a test: where a test game's threads are, for one that stopped answering. It only looks at a game `wingame.py` started. `scripts/build_pfo_windows.sh` writes the names it needs (`PFO.syms`) next to the DLL. | `stacks.py <pid> [PFO.syms]` |

`cross.py` and `xgate.py` use `tests/mp_test.py` for the emulator (default `127.0.0.1:5565`), which
needs the Android build installed, the scale at 4x and a profile in file 1, as above. The tests end
with `PASSED` or `FAILED` (the sweep with a count) and exit code 0 or 1.

A real phone on USB works in place of the emulator: give its adb serial instead of the port
(`cross.py win <serial>`; `adb devices` lists it). The reference pictures are matched with the game
at scale 4 in the middle of the screen, or drawn at the screen's full height (a 2260x1080 phone at
scale 5) shrunk to that size, so the phone's own scale can stay. A phone has other apps that can
come to the front on their own (a sign-in screen after it has been offline, say): on a phone every
key press first checks that the game has the screen, and the test stops rather than press keys into
another app. A phone that has just come back online is busy for a while with updates, backups and
downloads; let it settle (or close those apps) before a timed test.

A test game that stops answering without a crash line in its log is most often showing GameMaker's
"Code Error" box: a failed assertion in `PFO.dll` ends up there, and the game waits for the box to
be closed. `wingame.state_of(pid)` prints the box's text, and `duo.py` checks for it at the end.

For `vergate.py` and `xgate.py` the "other version" is a second game folder of an older release
(another `c_BuildWire`, in `pfo/android/fakesteam_impl.inc`), or a copy of this build with the
identity in its `data.win` changed (the `pfo_set_build_id("...")` string at the start of
`oController`'s Create event): the same number, another identity.

## Results after the picture check (2026-11-14, not yet released)

On the final build (picture check, tile restore by id and by content, the drawing state, the wait
for textures, the fallback that switches the left game's objects off), BlueStacks x86_64 and the
Windows build on one PC:

| test | Windows | Android |
|---|---|---|
| sweep, all 50 games, loaded from the library | 50 / 50 | 50 / 50 |
| sweep, loaded from another game | 50 / 50 | 50 / 50 |
| sweep, loaded in the same game | 50 / 50 (the build before the fallback fix) | 50 / 50 (the same) |
| every snapshot forced to fail (`snapfail`): the fallback replay in games 9, 29, 30, 40, 44, 46 | no error, replays verified | no error, replays verified (29, 40, 44) |
| `quickslot.py` | PASS | PASS |
| rewind (`rewind.py` / `rewind_test.py`) | PASS (estimates 0.76x, 0.85x) | PASS (0.92x, 0.95x) |
| `uitest.py` | all as expected | - |
| `duo.py all` | PASSED | - |
| `mp_regress.py` | - | 6 / 6 |
| `mp_nosnap.py` | - | PASS |
| `cross.py win` / `cross.py android` | PASSED / PASSED | |

Found and fixed on the way (each a load that looked right to the checksums): tiles drawn one by
one left behind or missing (Barbuta's fallen block, game 48's floor), the textures of a game
still loading during a load's check, another game's drawing colour, and an error box when a load
fell back to replaying from another game.

Still open:
- Game 29 (07 in the code) loaded from another game: its snapshot sometimes fails its check at
  frame 3 (seen once on Windows and on Android). The load then replays and comes out right, slower.
- Games 50 and 24 (45 and 46 in the code): a replay-only load sometimes doesn't verify (game 50 in
  about 4 runs of 10, on the build before these changes too; game 24 once in 7).
- `vergate.py` and `xgate.py` were not run (the version gate wasn't changed).

## Results for fast rejoins (2026-11-15, not yet released)

| test | result |
|---|---|
| `tests/win/rejoin.py library` | **pass**: the joiner's game killed and back by state at frame 1211 in 0.7 s, then the host's (frame 1868, 0.8 s, about 1.2 MB sent); after each, a save on the same frame with the same checksum on both |
| `tests/win/rejoin.py game` | **pass** in a game for two: 2.0 s and 2.1 s (3.1 MB, 2,792 instances), saves matching after each |
| `RELAY=1 tests/win/rejoin.py game` | **pass**: through the relay the other game sends the replay instead (about 900 bytes) and both rejoins replay; saves matching after each. (Before that rule the 3 MB state didn't arrive in the rejoiner's 40 seconds.) |
| `tests/win/cross.py win` | the rejoins **pass** both ways by state: the emulator back from the Windows game in 2.6 s, the Windows game back from the emulator in 2.5 s, a save after both with the same frame and checksum. One check failed: the vote box's picture of the save (the PC's display was asleep, so window pictures came out blank; `duo.py` failed its picture checks the same way that night) |
| `tests/win/cross.py win <phone>` (a OnePlus 6T, arm64, Android 11, on Wi-Fi; the room on oldkid.net) | **pass**, every check: the phone found the Windows game's public room through the real room server and joined it over the home network (direct, not the relay); saves with the same frame and checksum on both, loads asked for by each side put back on the same frame on both, verified; the phone's app killed and back by state in 2.9 s, the Windows game killed and back from the phone's state in 3.9 s; no desync, crash or failed assertion. `cross.py android <phone>` (the phone hosting, the Windows game joining; the PC must be unlocked, as the Windows game picks the room by its window's picture) **passes** too, 25 checks: the phone back by state in 2.6 s, the Windows game in 3.3 s. Seen on the way: on the phone a load's picture, opening out see-through over Ninpek, doubled everything under it and looked like broken graphics (the game itself was right, checksums equal); it now stays solid until it lands. Run again on the builds with the Windows menu's game font, the device-only display settings and the rest of the style guide's phase 1: **pass**, 25 checks (rejoins 2.7 s and 6.1 s). |
| several players on a phone, online (2026-11-19) | **pass**: `seats_phone.py <serial>` (a real phone hosting: touch P1, the test pad P2; the Windows game P3) in Ninpek: the pad's presses on both, a save and a load matching, the phone's app killed and back in 3.3 s by state with the three seats, a save after it, no desync. |
| a fast rejoin early in Barbuta (found 2026-11-19, **fixed** the same day) | A rejoin about 8 s into Barbuta desynced 4 frames later: 38 `oCliff` missing. The trace showed it on the first frame: an instance made again took the id a later record still had, and that record reused it (docs/SNAPSHOT_STATE.md). |
| v0.5.1, the release build (2026-11-26) | **pass**: the quick regression 18 of 18 in 92 min on the quick-build outputs made from the new v0.5.1 kits (build identity `0.5.1+91130d92` on both). The builder window: its screens with recorded build logs, and a real Windows (UFO Vanilla) build; full Android and Windows builds through the window and its Install on phone button not tried. The real save folder: only Steam Cloud wrote there during the run (a sync from another device, in Steam's cloud log). |
| v0.5.0, the release build (2026-11-22) | **pass**: the quick regression 18 of 18 on the quick-build outputs (the new kits; the Windows folder with the Windows 7 fixups; MultiMadnessMod 1.31 and the three new mods in the game data); `splitscreen.py`; `rejoin_sweep.py 19,31,36,39,23` (the mods' games: the same on every frame); `sweep4.py 19,23,31,36,39` on an emulator (5 of 5). The Windows 7 fixups on Windows 10: a patched copy passed the save-state sweep and `rejoin.py game`. Not on a Windows 7 PC. |
| The build identity (2026-11-25, not yet released) | **pass**: `vergate.py` against v0.5.0 and against a copy of this build with another identity in its `data.win`, both ways (the join refused, the joiner told on screen, both games up, the host keeps its lobby); `duo.py all`; `cross.py win` and `cross.py android` with an emulator (Android and Windows work out the same identity, `0.5.0+a9c6a08f`, and play together). Found on the way: the first Android build crashed at start (the game sets the identity before the runner hands the extension its interface); fixed. `xgate.py` and the phone not run. |
| the quick regression after the snapshot fixes (2026-11-19) | **18 of 18 pass** (`regress.py quick`, 91 min): the Windows rejoins (library, game, relay, replay), the pause seat, duo, several players online, cross-play both ways, quick save, disconnect and rewind on both, every online scenario on the two emulators, both sweep samples. The real save folder untouched. |
| every game, a fast rejoin and back to the library, traced (2026-11-19) | `rejoin_sweep.py all` on Windows, then again for the games a fix touched: **49 of 50 the same on every frame traced**, after the rejoin and on the way back to the library, once these were fixed: instances made again under other ids, paths and motion-planning grids, the runner's list of switched-off instances, refs to things gone (`noone` among them), save files looked for in the wrong folder (the library differed after a rejoin), data structures under other numbers. Mini & Max (45) differs in a variable its Draw event sets (`o41_Game.yPos`: drawing isn't on at the same moments everywhere). Items each device has its own (sound handles, its window's scale, its keys this frame, the catching-up notice) are left out (`tracediff.py` DEVICE). **Note:** tracing from the very frame of a rejoin can itself put some animated instances one step behind on the rejoiner a frame later (games 4, 9, 28, 37, 42 showed it); traced from 5 s after (`TRACE=0 TRACE_LATE=5`) they're the same. So a difference that is only `image_index` and `xprevious`/`yprevious` from the second frame on is the trace's, not the rejoin's. |
| several players on one device (2026-11-19) | **pass**: solo play with two players on one PC (`SOLO_PLAYERS=2 sweep.py 2,3,24`: P2 on the numpad) and on the phone (touch and the debug test pad, chosen on WHO'S PLAYING): saves, snapshot loads and replay loads with both seats' inputs; `seats_online.py` (two players on the host, one on the joiner): three seats, saves and loads matching on both, a fast rejoin keeping the seats. The Windows regression (rejoins in the library, in a game, through the relay, duo, quick save, rewind, the sweep sample) passes on the same code. |
| `SNAPWIRE=1 tests/win/sweep.py all` | **50 of 50**: every save through bytes and back, its load verified (120 frames, 3 pictures) and the replay through it; every round trip "0 values not sent, 0 sent unbound" (snapshots up to 6.3 MB, about 0.3 s out and 0.2 s back) |
| `SNAPWIRE=1 sweep4.py` (Android) | games 1 and 8 (40 and 48 in the code) pass. Game 3 (34) failed its picture at frame 61 or 101 in three runs (checksums all matched, the replay fallback verified); run by hand through bytes it verified three times, with the comparison showing the snapshot and its copy identical. The sweep's menu keys were unreliable in this mode (each capture holds the game while its pause menu is up), so it wasn't settled. |

Found and fixed on the way: `handle_parse` gives `noone` for an instance that isn't there, so the
other device's instances all became one (an error box in `oLibrary`); the game's 1,363 global
functions are methods bound to the global struct; a rejoined device lacked its own checksum for R
(an assertion when it sent its inputs); `duo.py` pressed into the code box when its window picture
came back blank.

`tests/regress.py quick` on the final sources (the PC locked, so window pictures couldn't be taken
and those checks were noted, not counted): every test passed (the rejoin tests in all four modes,
pauseseat, duo, cross-play with the Windows game hosting, quick save, DISCONNECT and rewind on both,
all six mp_regress scenarios, and both 5-game sweeps through bytes and back) except cross-play with
the emulator hosting, which needs a picture of the Windows game's window to join (no session).

`rejoin.py library` also loads an online save made before the joiner was killed: the rejoined game
fetched the host's snapshot of it in 438 ms and both came back on the same frame. `pauseseat.py`
passed with both kinds of rejoin; the controls mix-up seen in a phone + PC match (after a pause
save, the phone's app crashing) didn't happen in it.

Still open:
- Game 3's picture in the Android sweep with `snapwire` on (above).
- Three players through the relay: a player reaches the host but not another player (the relay's
  mailboxes are made for joiners reaching the host), so a match of three where two players can't
  reach each other directly ends as it starts. Two BlueStacks instances never reach each other
  directly, so `trio.py` can't pass on one PC until the relay handles that.
- Three or more players: a rejoin replays (not tried with a third player; only two emulators).

## Results for v0.4.0

On the final sources of this release (the two emulators and one Windows 10 PC):

| test | result |
|---|---|
| `mp_regress.py` 1-6, with the stricter checks described above | **6 of 6** pass |
| `mp_nosnap.py` | **pass**: the same save on both phones, both loads replayed to the save's frame and verified on both, 2 players and the same game afterwards |
| `rewind_test.py` | **pass**: 15 s and 60 s rewinds verified; the two 60 s rewinds after 2.5 minutes each took 0.93x of their estimate |
| `sweep4.py` (Android) | not repeated on the final sources; a sample of 5 games passed earlier in this release's work, and all 50 in three modes for v0.3 |
| `tests/win/sweep.py`, all 50 games, loaded from the library | **50 of 50** pass: snapshot load verified, replay through it verified. Run before the last fix (which only touches loads in a match). |
| `tests/win/uitest.py` | **pass**: every page by keyboard and by the test controller. Run before the last fix. |
| `tests/win/duo.py all` | **pass**: snapshots and pictures on both, the vote box with the save's picture, the accepted load on the same frame on both, the library save loaded from inside the game, the joiner killed and rejoining |
| `tests/win/duo.py all` with `PFO_NO_SNAPSHOTS=1` | **pass**: the match of files only |
| `tests/win/cross.py win` and `cross.py android` | **pass** both ways, with snapshots on both: saves with the same checksum on Windows and Android, loads asked for by each side put back on the same frame on both, the library save loaded from inside the game, each side killed and rejoining |
| `tests/win/vergate.py` and `xgate.py` | **pass** both ways: the join is refused, both games stay up, the host keeps its lobby. The "other version" was a Windows build of these sources with another version number. One run of `vergate.py` failed because the joiner's key press to join didn't land (no join was tried); the run after it passed. v0.3.1's own APK was not run against v0.4.0. |

Measured on the test PC: the Windows version replays at 750 to 1,000 frames a second, about 4
seconds per minute played; a snapshot load takes about a second.

Three bugs were found by these tests and fixed before the release (see the changelog): a dropped
player without a player in the current game was not waited for, reconnecting after a load that
restarts the match stalled for about 35 seconds over the relay, and a library save loaded from
inside a game stopped the host with a failed assertion (`duo.py`). A fourth came out of a failed run
of scenario 6 during development: a device a frame or two short of such a load could be left behind
in the old match.

## Results for v0.3 and v0.3.1

On the builds of those releases:

| test | result |
|---|---|
| `sweep4.py` all 50 games, loading in the same room, from the library and from inside another game | **150 of 150** pass: snapshot loads verified, replay through them verified |
| 40 single-player save + load runs back to back, after the audio crash fix | **40 of 40** clean, no retries (5 crashes and 7 retries before the fix) |
| `mp_regress.py` 1-6 | **6 of 6** pass, with the script as it was then: each scenario's own log lines, both phones still with 2 players, and the same game picked on both screens. That script did not compare frame numbers, did not check that the drop in scenario 4 happened, and looked for crash and desync lines only in the last seconds of each log. The script checks more now (see above), and was run again for v0.4.0. |
| `rewind_test.py` 15 s and 60 s | **2 of 2** pass, both verified |
| Rewind estimates (v0.3.1, per-game speeds) | 60 s rewinds took 0.89x and 0.96x of their estimate (before: up to 1.6x) |

Measured on BlueStacks: a snapshot load takes about 1 second; replays run at about 400 to over 1,000
frames a second depending on the game, so a replayed load or rewind takes a few seconds per minute
of play.

## Not tested yet

- **The overlay's keys with a real keyboard or controller on a phone.** They were driven by adb
  (`input keyevent`, `input gamepad keyevent`): the D-pad given as keys, not as a hat axis, and
  both sticks as two presses in a row rather than held together.

- **Real phones beyond one.** One arm64 phone was tested (a OnePlus 6T, Android 11): snapshot saves
  and loads (a save 0.3 s, a load 1.8 s, verified), and a cross-play match with the Windows build
  over Wi-Fi through the real room server, rejoins by state included (above). The sweeps of all 50
  games ran on x86_64 BlueStacks only, and the internet relay was not used with the phone (it
  reached the PC directly on the home network).
- **Three or more players online**: with two BlueStacks instances only, rejoining and the shared
  inputs of a dropped player were tested with two players. That includes a player watching a game
  they have no player in: the waiting for them when they drop, and the match going on without
  them after 90 seconds, were not run with a third device.
- **The Windows version on a second PC.** Both Windows games ran on one PC, in sandboxes. Not
  tried: the Windows Firewall question on first hosting, COPY THE INVITE CODE and pasting a code
  (the tests type it, to leave the clipboard alone), and Windows 11.
- **A real controller on Windows.** The menu's controller side was driven by the test controller
  (a message to the game's window), not by a pad in hand: that the sticks' buttons open the menu
  on a real XInput pad, and that the game gets nothing from the pad while the menu is open, were
  not tried.
- **Another `ufo50.exe`** than the one the Windows snapshots are made for: the check that turns
  them off was tested with `PFO_NO_SNAPSHOTS=1`, not with a different game version.
- **The quick build downloading its kits.** The kits were taken from `kit/` in the tests; the
  download from the release's tag can only work once the tag exists.
- **Long matches**: nothing ran longer than a few minutes, so a load by replay late in a long
  match without snapshots (which takes longer the longer the match) was not timed.
- **v0.3.1 and v0.4.0 meeting in a lobby**, with v0.3.1's own APK (see the results above).
- **Online saves over the relay with a slow connection**, beyond the relay rejoin in scenario 5.
- **Hot seat chips** on screen in a hot-seat game (the logic was tested, not the drawing).
- Android 13 and later (the notification permission, background limits).

If you test any of these, please open an issue with what you saw.
