# Changelog

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
