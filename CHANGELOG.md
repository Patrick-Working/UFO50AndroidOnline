# Changelog

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
