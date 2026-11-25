# Changelog

## v0.5.0 (2026-11-25)

Highlights since v0.4.1: **more mods** (MultiMadnessMod 1.31, Ultimate Pingolf Roster, Star
Waspir 2P, Hyper Contender+); **several players on one device** (WHO'S PLAYING) on both versions;
**fast rejoin** (back in about a second); **save states and rejoins put the whole game back
exactly** (six kinds of state that went missing or came back different, found by checking every
kind GameMaker holds and comparing two devices frame by frame: 49 of 50 games the same throughout a
rejoin); one look on both versions; tested on a real phone over Wi-Fi. The details:

- **Mods: MultiMadnessMod 1.31, and Ultimate Pingolf Roster 1.0.0, Star Waspir 2P 0.9.0 and Hyper
  Contender+ 1.6.10.** `build.ps1` keeps the mods in one list (`$Pins.Extra`: a name, a download and
  its checksum each) and installs them afresh when it changes. Each was auditioned on a test copy
  before going in (docs/MODS.md): the Mod Loader's merge of the code two mods both patch checked
  (Pingolf's three objects, `scrInit`, the title screen), and each changed game's save states with
  four players and an online fast rejoin compared item by item. Found and fixed on the way: Star
  Waspir 2P stopped one-player games at once ("badEgger1 not set before reading it"; the build
  gives the two variables a start), and MultiMadnessMod 1.31 moved the line one of our screen edits
  is anchored to. The lobby build number goes to 5: v0.5.0 and earlier builds don't share lobbies.
- **Fixed: a load across MultiMadnessMod's split screen** (four players apart in Onion Delivery or
  Rock On! Island) left the views as they were: the screen handler's flag came back as saved while
  this device's three extra cameras and doubled surface didn't, and its picture check failed. The
  restore now switches split screen on or off with the mod's own scripts to match
  (`tests/win/splitscreen.py`: loads both ways, twice, pass; all four failed before).
- **Several players on one device: WHO'S PLAYING.** Every controller is a player of its own: on a
  phone the touch controls and each pad, on Windows the arrow keys, the numpad keys (the game's P2
  keys) and each pad. Before solo play (START SOLO PLAY) and, online, in the lobby (the Players
  panel on a phone, WHO'S PLAYING... in the F1 menu on Windows), each controller joins, picks a slot
  P1-P8 and locks it in; every player shows its controller's icon and, online, its device's name.
  Solo play records every seat, so saves, quick saves, rewind and loads work with several players
  on one device. Online, each device sends the slots its players locked in, the host seats everyone
  (a slot two devices want goes to the one earlier in the lobby), and the match starts with those
  seats on every device; online saves, loads and fast rejoins carry every seat.
  How it works: PFO gave each connection exactly one player; a device can now carry several seats,
  whose inputs travel in the same network messages and replay frames as its first seat. The replay
  labels them by seat (a new chunk type, and a seat table in the header behind a flag bit: older
  replays and saves load as before), and the game reads each seat's own controller. Hot seat is gone
  from the Players panel (its place is WHO'S PLAYING).
- **Snapshots put back what was missing** (found by going through every kind of state the runner
  holds against what the games use: docs/SNAPSHOT_STATE.md):
  - **Fixed: a fast rejoin into Barbuta desynced.** Putting a snapshot back, each instance made
    again takes the next free id, and that could be the id a later record still had on the other
    device: that record took the new instance for its own, and its own was never made (38 cliffs).
    Every instance to reuse is now decided before any is made, as data structures already were.
  - **Instances made again keep the ids they had**, and new ones still come after all others: two
    games decide things by comparing ids (`id < other.id`), and the order used to change.
  - **Paths and motion-planning grids made at run time** (two games' enemies and units walk them,
    library 12 and 50) are carried, with each instance's place on its path: a load or rejoin used
    to leave them standing.
  - **Fixed: after a fast rejoin, switched-off instances could never be switched on again**: the
    runner keeps a cached list of them for `instance_activate_*()`, which a restore didn't mark
    stale (library 12's map stayed off on the rejoined device).
  - References to things gone by then (`noone`, a dead instance, a freed list) stay references
    (they came back as plain numbers), and data structures made again keep the numbers they had
    (a game's stale number for a freed list named a live one on one device and nothing on another).
  - **Fixed: after a fast rejoin, going back to the library desynced.** The rejoin carries the
    sender's save files, but they were looked for in the wrong folder (a plain file open of
    `save1.ufo` from the game's own folder): the rejoiner was told there were none, and its library
    (games played, play time) differed from the others' as soon as a game was left. Save files are
    now read and written through GameMaker's own file functions, as PFO does; that also makes a
    load put your save file back as it was at the save (it never got written before). A save file
    is never deleted by a load.
- **Tests: state tracing.** `trace <frames>` / `tracerejoin` write the whole game state every frame;
  `tests/tracediff.py` names the first item where two devices differ; `tests/win/rejoin_sweep.py`
  rejoins into every game and compares. `WINGAME_BACK=1` keeps the Windows test games' windows
  behind yours. The cross and pause-seat tests read only their own run's Windows log lines, and a
  rejoin through the relay may replay (it does, by design).
- **The menus' font without its black shadow**, on both versions.
- **Fixed: QUICK LOAD in a lobby** started solo play and left the lobby; it isn't offered there now.
- **Tests:** `SOLO_PLAYERS=2 tests/win/sweep.py`: the sweep with P2 playing; the test relay switch
  (`RELAY=1`) is set again after the launch clears old switches (it had stopped taking effect).

- **Windows 7: the Windows version is made to start on 7 too, from the same folder** (not yet tried
  on a Windows 7 PC; on Windows 10 the edited folder passes the save-state and online tests). PFO.dll imported
  `VirtualAlloc2` and `MapViewOfFile3` to build its checksum ring buffer, an API set Windows 10
  1803 brought, so on Windows 7 the DLL did not load at all; the buffer is now mapped the Windows
  2000 way (one page-file section, mapped twice back to back) and the build pins `_WIN32_WINNT` to
  7, so a newer call is a compile error. UFO 50's own `ufo50.exe` needed more than the known
  MFPlat fix: it imports `PathCchCombine` from a Windows 8 API set and `SetProcessInformation`
  outright (both are resolved before the program runs), and `win7/patch_win7.ps1` now rewrites
  those two names in the import tables at build time — the power hint becomes a no-op, whose single
  call site was checked in the disassembly — and puts the `PathCch` stand-in in the folder. All of
  it is harmless on Windows 8 and later (the loader answers `api-ms-*` names from the API set
  schema and ignores the copy), so one folder and one build number serve 7, 10 and 11, and their
  players share lobbies. The quick build gets that DLL too: `kit/` carries it
  (`ufo50-online-windows-v0.4.1.dll`) and `build.ps1` pins it by commit, since it is the one this
  tree builds (it is newer than the v0.4.1 release, whose tag pins the older DLL). A finished folder is now checked for anything newer
  than Windows 7 in either program (`win7/check_win7.ps1`, which the build runs), and
  [docs/WINDOWS7.md](docs/WINDOWS7.md) has the steps for the 7 machine — the lines to look for in
  `pfo_online.log` and when DXVK is worth trying. Windows 7 still needs KB2670838 and KB2999226;
  nothing has been run on a 7 machine yet ([win7/README.md](win7/README.md)).
- **Windows: the online menu in UFO 50's own font.** The build exports the game's menu font from
  your copy into the game folder (as the phone build does for its overlay; nothing is shipped) and
  the menu draws it at whole-pixel sizes. Without the file it falls back to Consolas.
- **One look and one wording on both versions** (the style guide): the attention colour is the
  phones' orange on Windows too (its amber read as P4's yellow); PFO's "Delay: Nf" text over the
  game is gone on Windows as on the phones (the delay is on the menu's session line, catching up
  is a CATCHING UP message); AUTO SAVE WHEN I PAUSE, ENTER AN INVITE CODE and FORGET IT on both.
- **Windows: a short controller rumble when a save or load is done**, as the phones' haptic tick
  (not on an auto save, as on the phones).
- **Fixed: a fast rejoin brought the other device's picture mode along.** A rejoined phone drew the
  game at the Windows window's size. Display settings (scale, fullscreen, borders, scanlines) are
  this device's own: snapshots leave them out, and a load never changes your picture mode either.

- **Saving and loading feel like something happened.** The tap: a light buzz, a ring on the button
  and SAVING / LOADING at once. A save then flashes like a camera shutter and its picture flies into
  the button it's kept under (the disk, or quick save); a load's picture opens out of its button,
  solid, lands on the game and fades into it (a see-through picture on the way doubled everything
  under it, which looked like broken graphics). A firmer buzz when it's done, an error buzz when it's refused. An auto save on
  pause just rings the disk.
- **Fixed: a green frame around the screen** after any key or controller input (Android's focus
  highlight on the game's view, which fills the screen), or after changing the picture mode in the
  game's Video Settings. Turned off, also on views the game makes later.
- **Online saves made before a fast rejoin load again.** A device back by a fast rejoin had no
  snapshots of the match's earlier online saves, so it couldn't take part in loading one. When a
  load of such a save is put to the vote, it now asks a player for their snapshot of it (about half
  a second on a local network) and answers the vote once it's there.
- **A rejoin sends the replay when that's quicker**: early in a match over a slow connection, the
  replay (kilobytes) beats the game as it is (megabytes).
- **A dropped player waited for keeps their seat.** The game's pause menu, online, removes the seats
  of players who aren't connected. A player PFO is still waiting for (their connection dropped, they
  may reconnect or rejoin) now counts as connected until they've left or the match went on without
  them.
- **Tests on a real phone over Wi-Fi.** The online tests take a phone's adb serial in place of an
  emulator port (`cross.py win <serial>`, `SWEEP_PORT=<serial> sweep4.py`), match its screen at the
  game's full-height scale, and press keys only while the game has the screen. `cross.py` passed
  both ways between a OnePlus 6T and the Windows build through the real room server. A file `nofx`
  in the app's files folder turns the save and load pictures off; the tests set it.
- **Tests:** `tests/win/pauseseat.py` (who controls which player through a pause save and rejoins
  during a pause, both kinds of rejoin), `tests/win/trio.py` (three players: a Windows game and both
  emulators), `tests/regress.py` (every test in a row, one table at the end). Checks of a save's
  picture are skipped when the Windows game's window comes back blank (the display asleep).
- **Hot seat for up to 8 players.** The touch pad now follows the turn to any of P1-P8 (it stopped
  at P4: P5-P8 needed a gamepad). Four more touch pads are registered for P5-P8 the first time a
  game needs them. Fixed: the seat row and the turn count came from the library's player count,
  so a Party House party of 6 set up in the game's own settings showed two seats; it's the game's
  own count now.
- **Hot seat holds the turn while the game is paused** or shows a box. The game only reports turns
  while it reads the player's controls, so pausing dropped out of hot seat (the pad went back to
  P1, the seats vanished) and back in on resuming. It ends when the game is left, or after 20 s
  without a turn.
- **Fast rejoin.** A player whose app closed or crashed used to replay the whole match from its
  start to get back in (minutes, for a long match, with everyone else waiting). Now the player
  still in the match sends the game as it is where everyone is waiting: a snapshot of the game,
  PFO's session (players, inputs, input delay), the save files and the match's records. The
  rejoining device puts it back like an online load, checks PFO's checksum for the next frame
  against the sender's, and carries on. Anything wrong, and it replays the match as before (the
  replay comes along in the same message). Android and Windows alike, and between them. Between
  two players for now; with three or more the rejoin replays, and so does one through the room
  server's relay (it carries the replay's kilobytes, not the state's megabytes, in good time). Tests: `tests/win/rejoin.py` (each way,
  in the library and in a game), `duo.py` and `cross.py` check that a rejoin was fast, and
  `SNAPWIRE=1` sends every save's snapshot in the sweeps through bytes and back.
- **Online: your own player chip opens the players panel**, so you can switch your seat between touch
  and a gamepad during a match. Online a device plays one seat: the panel shows OFF and your seat
  (P2, say), with the other columns faded.
- **The online pill stays out of the way**: it shows only while the players' names do (tap the
  chips) or when something happens, and it says the kind of connection: LAN, ONLINE or RELAY, with
  the input delay. The names are on a line under the P1 / P2 chips.
- **☰ again closes TOUCH CONTROLS** (keeping the changes, as ACCEPT does).
- **The save list's LOAD and DELETE swapped places** on a phone (DELETE left, LOAD right).
- "SAVED BY ITSELF" is now **GAME SAVED ON PAUSE**.
- **Fixed: loading a save after DISCONNECT crashed** (an error box, `pfo_set_players` asserting in
  `scrSetOnlinePlayers`). A save made in solo play is kept in memory as a snapshot of the running
  session; after DISCONNECT PFO is shut down, and putting the snapshot back left the game thinking
  it was online. Outside solo play a save now loads as it does after a restart: its recording is
  replayed, which starts the session properly. Tests: `tests/disconnect.py`,
  `tests/win/disconnect.py`.
- **The layout editor is a TOUCH CONTROLS panel** in the middle of the screen instead of a strip of
  small buttons: a TOUCH OPACITY slider, AUTO HIDE, PLAYER CHIPS horizontal / vertical, D-PAD 8-way /
  4-way, QUICK SAVE / LOAD on / off, RESET TO DEFAULTS, and a red CANCEL (puts back what you had)
  and a green ACCEPT. The controls around it are moved and resized as before.
- **The quick save and quick load buttons are on opposite sides of the screen** (left and right,
  below the top bar), so a thumb can't bump one for the other. They are touch controls like the
  rest: moved and resized in the editor, and switched off there if you don't want them.
- **Quick save and quick load.** One QUICK slot, replaced by each quick save, in solo play: on a
  phone the two new buttons left of the disk, on Windows **F5** and **F9** (also in the F1 menu).
  The save list shows it as QUICK. (Built by `bash build.sh` for now: the quick build uses the
  v0.4.0 kits until they are made again for a release; it gets the tile fix below.)
- **Keyboard and controller on a phone, as on Windows.** F1, or both sticks pressed in, opens the
  save list; arrows / D-pad move a yellow outline, Enter / A chooses, Esc / B goes back, Delete / X
  deletes; F5 / F9 quick save and load; F2 / F3 answer a load vote or the offer to rejoin. The game
  gets every key while the save list is closed. The save list has QUICK SAVE and QUICK LOAD too.
- **Loads check the picture too.** A snapshot load already compared PFO's checksums over the two
  seconds after the save; now it also compares the picture the game draws at three of those
  frames, and replays instead when anything differs. It found the next three fixes.
- **Fixed: game 48 (8 in the library) had a black floor after a load through the library**: its
  floor is tiles drawn one by one, which a room built afresh didn't have.
- **Fixed: after a load from another game, some games drew their score in the other game's
  colour** (games 34 and 46 in the code, 3 and 24 in the library): GameMaker's drawing state
  (colour, alpha, font, text alignment, blend mode) wasn't part of a snapshot, and they draw their
  score without setting a colour. Snapshots keep it now.
- **Fixed: a "Code Error" box (Windows) or a game-code error (Android) when a load from another
  game fell back to replaying** ("Unable to find instance for object index ..." in
  o07__eSpikes, o12_mapAnimal, o37_buSmallBench): leaving the game before the replay left its
  objects running a step without their controller. They are switched off first now. The fallback
  is rare, but the picture check makes it run more often, which is how it came up.
- **Loads wait for the saved game's textures** before putting the state back (they load in the
  background), so the first frames after a load through the library are drawn right.
- **Tests:** `tests/quickslot.py` and `tests/win/quickslot.py` (quick slot, the keys, the fallen
  block), `tests/win/rewind.py` (rewind on Windows). The sweeps count a game-code error or crash
  as a result (GAME ERROR) instead of retrying it, and a load that compared no pictures as not good.
  The tests that play alone (the sweeps, quick slot, rewind) switch the room server off
  (`okplay.txt`: `off`), so hundreds of test games don't ask oldkid.net for lobbies; the online
  tests switch it on again. The Windows tests press keys in their own game's window only and close
  only their own game, so they can run beside a sweep.
- **Fixed: a load could leave tiles a game had drawn since the save.** Some games draw one-off
  tiles at run time (in Barbuta a loose block lands as three of them, beside the blocks that make
  it solid): a load took the solid blocks away but left the tiles, so a fallen block still showed,
  with nothing solid there. Snapshots now keep those tiles too (games 07, 13, 21, 29, 33, 40, 41,
  45 and 48 in the code's numbering make them).
- `build.bat -Vanilla`: an APK of the game as Steam has it (`work\ufo50-vanilla.apk`), with none of
  this port's additions: no mods, no online play, no save states, no touch controls (it needs a
  controller or a keyboard). The packer leaves the port's native library out and the runner starts
  no extension. With `-Windows` it makes a portable copy of the plain game that doesn't need Steam
  (`work\windows\UFO 50 Vanilla`): Steam's libraries are left out, and `UFO 50 Portable.bat` keeps the
  saves inside the folder.

## v0.4.1 (2026-11-11)

A fix for the quick build. The game side is unchanged: the runner kit and the Windows kit are
v0.4.0's, and v0.4.1 and v0.4.0 play together.

- **The quick build no longer needs a click.** It runs the UFO 50 Mod Loader without a window to
  install the mods (the loader's window still opens if that fails, or with `-ManualLoader`).
- A modded copy whose Install was stopped half way is noticed and made again, instead of failing
  later with "PFO extension not found".
- `build-android.bat` and `build-windows.bat`: one double-click each.
- The build says so plainly when a folder name with spaces was given without quotes, and when its
  own folder has `[` or `]` in its name (which it can't work in).

## v0.4.0 (2026-11-10)

### A Windows version
- **`build.bat -Windows`** (or `bash build.sh --windows`) makes a second game folder,
  `work\windows\UFO 50 Online`: your modded copy of UFO 50 with this fork's online play, save
  states and rewind in place of PFO 50's Steam networking. It plays with the phones: the same
  lobbies (oldkid.net rooms, your own network, the relay), invite codes and online saves.
- **It never talks to Steam** and never touches the Steam game's saves: the folder has no Steam
  library in it, and it keeps its own saves in `%LOCALAPPDATA%\ufo50_online`. It can't join PFO 50
  lobbies on Steam.
- **A menu for keyboard and controller** in place of the touch overlay: **F1**, or both sticks
  pressed in, opens it (arrows / D-pad, Enter or Z / A, Esc or X / B). It has the seat and
  connection, the invite code, SAVE, the list of saves with each one's picture, details and
  loading time, START SOLO PLAY and REWIND, auto save, player name and UPnP. A load another player
  asks for shows in a box with the save's picture and a countdown (**F2** accepts, **F3**
  declines); a loading screen covers the game while a save loads; a match you dropped out of is
  offered again (F2 rejoins, F3 forgets it). The controller side reads XInput controllers.
- **Snapshots on Windows**: saves load in about a second, rewind starts from the nearest snapshot,
  and an accepted load in a match puts the save back on every device within a few frames, as on
  the phones. The snapshots are made for the `ufo50.exe` of UFO 50 1.9.5 and are checked against
  it at the start; with any other they turn themselves off and loads replay the session (about 4
  seconds per minute played on the test PC).
- **A match with a player whose device has no snapshots has none on anyone's device.** That game
  says so when the match starts (a new command, 246). From then on an online save is a file only,
  and an accepted load restarts the match from the save on every device, the way a save from
  another match loads. v0.3.1 and earlier don't know the command, so everyone needs v0.4.0.
- The netcode (`PFO.dll`) is compiled with llvm-mingw, which the full build downloads. The quick
  build takes it from a **Windows kit** in `kit/`, like the runner kit. Details:
  [docs/WINDOWS.md](docs/WINDOWS.md).

### Online
- **A dropped player who has no player in the current game is waited for too** (a third player
  watching a 2-player game, the second player in a 1-player game). Before, the match ran on without
  them, and they could never catch up or rejoin. Now everyone waits up to 90 seconds and they can
  reconnect or REJOIN. If they don't come back, the match carries on without them (or ends if
  nobody else is left).
- **Reconnecting after a load that restarts the match no longer stalls over the relay.** The device
  that dialled wasn't listening on the relay for the answer, so the connection took about 35
  seconds (three dials) instead of one or two. This affected loading a save from another match
  between two devices connected by relay in v0.3 and v0.3.1.
- **A load that restarts the match starts on every device as soon as one device starts it.** Each
  device used to start it only when its own game reached the load's frame. A device that was a
  frame or two short when the others closed their connections, and never got their last inputs,
  could be left behind, and then connect to a device already in the restarted match (possible in
  v0.3 and v0.3.1 when loading a save from another match on a poor connection). Now the device that
  starts says so as it closes the connection, the others start at once, and a device still in the
  old match is refused a game connection until it has loaded too.
- **Loading a library save from inside a game no longer stops the host with an error.** The load
  goes to the library first, and the library, made anew, sent its settings to the others a second
  time before the save was put back; PFO 50 refuses that with a failed assertion (an error box on
  Windows). Nothing is sent in those steps now. Found on Windows; the code is shared with Android.
- **Two versions of the build can't share a lobby any more.** A host refuses a joiner of another
  version, and a joiner leaves a host of another version, so v0.3.1 (or earlier) and v0.4.0 can't
  play together: a match between them would go out of step at the first load with a player without
  snapshots in it. The game shows its usual failed-join message; the log says "another version of the
  build". Lobbies on your own network are only listed for the same version; lobbies of another
  version on oldkid.net are still listed but can't be joined.
- A player who leaves on purpose (their session ended) isn't waited for.
- A dial that gets no answer in 8 seconds is dropped and made again, instead of waiting out the
  transport's 30 seconds.
- A lobby member that connects again with the same id (its app restarted) replaces its old
  connection. It is refused while the original is still talking from another address.
- Lobby lists and addresses are only accepted from the room server's own address.
- Match data sent outside the game's input (a dropped player's inputs, a rejoin's catch-up) is only
  accepted from a player this device has a game connection with.
- The others wait longer for a player whose device is replaying a long match to rejoin.
- Any player still in the match answers a rejoining player, not only the host.
- A dropped player's last inputs are only shared among players who are connected.

### Save states
- An online save picked outside a match is refused with a message instead of loading as a solo
  session.
- A rewind checks the snapshot it starts from before using it; a load that fails cleans up after
  itself.
- Time in the game, keyframe timing and the progress percentage are right after a rewind.
- A save file with the wrong size is refused.

### Overlay
- A tap that closes a box (a vote, the save list) no longer presses the game control under it, and
  controls that were held are let go when a box or the loading screen comes up by itself.
- A vote that arrives while you're moving the controls around closes the layout editor (keeping the
  changes) so you can answer.
- A vote takes your answer at once: a second tap can't change it.
- With the save list open, another player's save or an auto save no longer changes which save LOAD
  and DELETE mean, and DELETE doesn't stay armed for a save that just arrived.
- The loading screen of an accepted load shows the save that is loading.
- An online seat from P5 to P8 has its own colour (the overlay only knew four, and drawing a
  fifth seat's controls failed).
- The seat banner doesn't flash through old seats while a load replays the match.
- Swiping the app away from the recent apps removes the "UFO 50 - online" notification.

### Build, scripts and documentation
- The game patch stops the build when one of its edits can't be applied, instead of printing a line
  and going on.
- `scripts/make_icon.py` writes into `android/gm-project/icons/` wherever it is run from; the
  preview is opt-in (`--preview`).
- Test scripts: `mp_regress.py` and `mp_test.py` compare frame numbers between the two phones, check
  that the drop really happened, and look for desync and crash lines in the whole log. New:
  `mp_nosnap.py` (a match without snapshots between two phones), and the Windows tests in
  `tests/win/` (two PCs' worth of games on one PC, PC with emulator, and two versions refusing each
  other), which never run in your own user folders.
- Documentation corrected where it didn't match the code (replay speeds, the rejoin window, log
  lines, which scripts need Pillow), and a new [docs/WINDOWS.md](docs/WINDOWS.md).

## v0.3.1 (2026-11-09)

- **Load and rewind estimates are per game.** Replay speed differs a lot between games (the
  library replays over twice as fast as some games), so a single speed made long rewinds take up to
  60% longer than their estimate (13 s shown, 21 s taken). The app now notes which game each stretch
  of play was in and learns this device's replay speed in each game, kept between runs. In tests
  the estimate is now within 10%, leaning long.
- **The loading screen shows the time left** ("37% - ABOUT 8 S LEFT"), and its bar counts the
  frames actually being replayed (a rewind from a snapshot used to start part way along).
- The ask-first box fits its contents (no empty gap), and REWINDING shows once.

## v0.3 (2026-11-09)

### Save states (single player)
- **Save anywhere, in any of the 50 games.** The disk icon (top right) opens the save list:
  SAVE NOW, and for each save its picture, the game, who was playing, how far into the game, and
  how long it will take to load. LOAD and DELETE (tap DELETE twice).
- **Loads take about a second** for saves made since the app started: the game's state is put back
  directly (an in-memory snapshot), then checked against the next two seconds of recorded play. A
  save from before the app restarted replays the session at high speed instead (a few seconds per
  minute of play). Either way the result is checked against PFO 50's own checksums.
- **Every game verified** loading in the same room, from the library and from inside another game
  (150 of 150 in the final sweep).
- **AUTO SAVE WHEN I PAUSE** (a checkbox in the save list): pausing saves into one AUTO slot,
  replacing the last one. Its picture is taken from just before the pause.
- Single player saves need a recorded session: START SOLO PLAY in the save list (once per app
  start). The pill next to the player chips then says SOLO: SAVES ON.

### Rewind (single player)
- **REWIND 15 S / 30 S / 60 S** in the save list, each showing how long it will take.
- A rewind starts from the newest snapshot before that point and replays the recorded inputs at
  high speed to exactly that moment, then play carries on from there.
- **No new stutters in play.** Besides saves and auto saves, the game keeps a few rewind
  "keyframes", taken only when a quarter-second pause can't be felt: while the game is paused, and
  two seconds into a game that has just started.
- **You're asked first when a load is slow.** A rewind, or a LOAD that has to replay the session,
  estimated at 5 seconds or more asks before it starts ("REWIND 60 SECONDS? ABOUT 40 S TO LOAD").
  Estimates use the replay speed measured on your device.

### Save states online
- **SAVE NOW in an online match saves for everyone**, on the same frame (it travels as a command in
  the game's input, like PFO 50's own commands). Every player gets the save in their list.
- **LOAD asks the other players**: they see the save's picture and details, with ACCEPT / DECLINE and
  15 seconds to answer. When everyone with a seat accepts, every phone loads it on the same frame and
  the match carries on. A decline or no answer cancels it for everyone.
- **Saves from earlier matches load online too**, if they had the same number of players: your phone
  sends the save to the others, and everyone restarts the match from it.
- **AUTO SAVE WHEN I PAUSE works online**: one synchronized save per pause into everyone's AUTO slot.
- Players watching without a seat can't save or vote.

### Drops and rejoining
- **A dropped player no longer ends the match.** Everyone waits up to 90 seconds behind an orange
  banner (WAITING FOR P2 - 1:26), the phones keep reconnecting, and play carries on where it stopped
  (P2 IS BACK). After 90 seconds the match ends as before (P2 DIDN'T COME BACK - MATCH ENDED).
- **Rejoin after the app closed or crashed**: reopening it offers REJOIN YOUR MATCH?. REJOIN takes the
  player's old place: another player sends the match so far (a few KB of inputs), the phone replays
  it in a couple of seconds and joins back in. Works over the relay too.
- With three or more players, the ones left share the dropped player's last inputs so they all wait
  at the same point. (Rejoining has been tested with two players.)

### Overlay
- **One chip per player**: your input (touch, gamepads) shows inside your seat's chip; online, other
  players show a globe. Names appear when the match starts, when seats change, and when you tap the
  chips.
- **Session pill** after the chips: says what happened for a few seconds (SOLO: SAVES ON,
  ONLINE: LAN 2F, SAVED, LOADED, REWOUND, AUTO SAVED), then settles to a dot. CATCHING UP stays
  while your phone is behind.
- **CHIPS ROW / COLUMN** in the layout editor stacks the chips vertically.
- Hot seat: every player has a chip; the one whose turn it is is solid and pulses when the turn
  changes (replaces the yellow HS block).
- Banners and buttons follow one set of rules: player colours only for players, one green primary
  button per screen, a red outline for DELETE, orange (with white text) only for things that need
  attention.
- Fixes: nothing shows past the edges of the save list; the disk icon's lines are even.

### Stability
- **Fixed a crash in GameMaker's audio decoding.** GameMaker logs a message for each sound it
  decodes, from its decoding thread, and that message reads the game thread's state. When the game
  thread was busy (a load replaying at high speed while a game's music decoded) this occasionally
  crashed the app: about one load in forty, and now and then when opening a game. The audio log now
  goes straight to Android's log. 40 of 40 test loads ran clean afterwards (5 crashes before).

### Build
- The game patch adds `scripts/pfo_snapshot.gml` (the game side of save states); `build.ps1` passes
  it to UndertaleModTool. The overlay art export now also takes the game icons for the save list.
- `tests/`: the scripts used to test save states, rewind and online play on BlueStacks (see
  [docs/TESTING.md](docs/TESTING.md)).
- Documentation: [docs/SAVE_STATES.md](docs/SAVE_STATES.md) explains how save states, rewind,
  online saves and rejoining work.

## v0.2 (2026-10-29)
- **Quick build** (`build.bat`): needs only Windows and UFO 50 from Steam. It downloads
  UndertaleModTool, the Mod Loader and the mods, and uses the runner kit in `kit/` (GameMaker
  runner + touch overlay + libPFO.so, no game data). The overlay art and the APK packing and
  signing run as UndertaleModTool scripts, so no Python, Java or Android SDK is needed.
- **Full build** (`build.sh`): builds the runner from source and can make the kit (`--kit`).
- README: screenshots, versions table, both builds.

## v0.1 (2026-10-29)
- First release: rebuilds the Android version for UFO 50 1.9.5 from your own Steam copy, with
  PFO 50 (online play) and MultiMadnessMod (4+ players), the touch overlay, the online lobby menu
  (HOST PUBLIC / HOST PRIVATE / JOIN BY CODE) and okplay's netcode (LAN, direct, UPnP, relay).
  No game files in the repository.
