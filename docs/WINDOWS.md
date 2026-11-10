# The Windows version

The same build, for a PC: your modded copy of UFO 50 with this fork's online play, save states and
rewind in place of PFO 50's Steam networking. It plays with the phones: the same lobbies
(oldkid.net rooms, your own network, the relay), the same invite codes, the same online saves.

It is a second game folder next to your Steam installation, not a change to it.

## What it is and isn't

- **It never talks to Steam.** The folder has no Steam library in it (the build leaves out
  `Steamworks_x64.dll`, `steam_api64.dll` and `steam_appid.txt`, and the game's Steam calls go to
  our `PFO.dll`, which answers them itself, the way the Android build does), so it can't reach your
  Steam account, Steam Cloud, achievements or the Steam version's lobbies. Steam doesn't need to be
  running.
- **It keeps its own saves**, in `%LOCALAPPDATA%\ufo50_online`. The Steam game's are in
  `%LOCALAPPDATA%\ufo50` and are never read or written. To bring your progress along, copy
  `save*.ufo` and `account.ufo` from one folder to the other (copy, don't move).
- **It can't join PFO 50 lobbies on Steam**, and Steam players can't join it. It plays with other
  copies of this build, on Windows or Android, of the same version.
- It contains your copy of the game, like the APK: **keep the folder to yourself.**

## Building it

You need Windows 10 or 11 and UFO 50 1.9.5 from Steam.

**Quick build** (nothing to install): double-click `build.bat` once the usual way if you never have
(the first run opens the UFO 50 Mod Loader, see the README), then run

```
build.bat -Windows
```

It makes `work\windows\UFO 50 Online`. Start `ufo50.exe` in it. The quick build takes the
netcode from the **Windows kit** (`kit/ufo50-online-windows-v<version>.dll`: PFO 50 with this
fork's netcode, no game data), checked against a fixed checksum, copies your modded copy of the
game without the Steam files, and patches the game data (`scripts/windows_patch.csx`: the Steam
calls go to `PFO.dll`, the save folder's name, the lobby menu).

**Full build** (compiles the netcode): in Git Bash,

```bash
bash build.sh --windows --check   # what it will use
bash build.sh --windows           # PFO.dll from source, then the game folder
bash build.sh --windows --kit     # PFO.dll only, into work/kit/
```

It needs Git for Windows and nothing else: none of the Android tools, no GameMaker, no Visual
Studio. The compiler is [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (release 20261104),
which the build downloads into `work/llvm-mingw` (190 MB, checked against a fixed checksum). If you
have it already, set `LLVM_MINGW` in `local.env`. The same sources give the same `PFO.dll`, byte for
byte, so a kit can be checked by building it again.

`build.bat -Windows -WinDll <file>` uses a `PFO.dll` of your own (that is what the full build does
with the one it compiled).

## Playing

Keyboard as in the Steam game (Z, X, arrows, Esc pauses) and gamepads as GameMaker finds them.
What the touch overlay is on a phone is here a menu made for a keyboard and a controller:

| | keyboard | controller |
|---|---|---|
| open or close the online menu | **F1** | **press both sticks in** |
| move, choose, back | arrows, Enter or Z, Esc or X | D-pad or left stick, A, B |
| delete a save (in the list, twice) | Del | X |
| accept a load another player asked for; rejoin your match when the game offers it | **F2** | from the menu |
| decline the load; forget the match | **F3** | from the menu |

While the menu is open the game gets no keys and a controller at rest, so nothing you press in the
menu reaches the game. Typing a name or a join code needs the keyboard.

The game's own lobby menu (PLAY ONLINE on the profile screen) hosts and joins as on the phones:
HOST PUBLIC, HOST PRIVATE, JOIN BY CODE and the lobby list. JOIN BY CODE opens a box to type or
paste (Ctrl+V) the host's code.

The **menu** has, depending on where you are:

- your seat and how you're connected (LAN, DIRECT or RELAY), the lobby's invite code and COPY THE
  INVITE CODE
- **SAVE** and **LOAD A SAVE...**: the list of saves, and beside it the chosen save's picture,
  its game, how far in it was made, who was playing and how long it takes to load. A load that
  takes more than about five seconds asks first (LOAD THIS SAVE?).
- **START SOLO PLAY**, which turns saves and rewind on in single player, and then REWIND 15 S /
  30 S / 60 S with the time each will take
- when another player asks to load a save: ACCEPT / DECLINE; after a crash or a closed game:
  REJOIN YOUR MATCH / FORGET THAT MATCH
- SAVE WHEN THE GAME IS PAUSED (one AUTO save, replaced each time)
- ENTER A JOIN CODE, PLAYER NAME (empty: this computer's name)
- UPNP: whether hosting asks your router to open the port. The forward is taken back when the game
  closes.

Outside the menu the game shows, as the overlay does on a phone:

- **a box when another player wants to load a save**, with that save's picture, what it is, and
  the seconds left to answer (F2 / F3, or the menu)
- **a loading screen** over the game while a save loads or a rewind runs, with the save's picture
  and how far along it is
- short messages (SAVED, LOADED, P2 SAID NO, YOU ARE P2, WAITING FOR P2 ...)

The first time you host or join, Windows Firewall may ask whether to let `ufo50.exe` through.
Without that, players outside your network reach you through the relay only.

Settings are kept in `%LOCALAPPDATA%\ufo50_online\menu.cfg`, saves in `savestates\` there (each
with its picture, `slotN.png`), and a log in `pfo_online.log`.

Controllers: the menu reads XInput controllers (Xbox ones and those that present themselves as
one), and only with the game version the build was made for (below). Any other controller still
plays the game as GameMaker finds it; the menu is then keyboard only. The controller side was
tested with a stand-in for the controller, not with a real one in hand.

## Snapshots

A *snapshot* is a copy of the running game's whole state in memory (see
[SAVE_STATES.md](SAVE_STATES.md)). The Windows build has them as the Android build does:

- a save you made in this run of the game **loads in about a second** (in the tests: all 50 games
  saved, played on, and loaded back, each load checked against PFO 50's checksums for the next 120
  frames)
- rewind starts from the nearest snapshot and replays only the rest
- in a match, an accepted load **puts the save back on every device within a few frames** and
  the match carries on, on Windows as on the phones

Snapshots live in memory. A save from before the game was last started, or one loaded in the two
seconds after it was made, loads by **replaying the session** from its start at high speed (about
4 seconds per minute played on the test PC), checked the same way. The menu says how long each
load will take.

The snapshots depend on the exact game program: they are made for the `ufo50.exe` of UFO 50 1.9.5
and are checked against it at the start (the log says `snapshots: on` or why not). With **any
other `ufo50.exe` they turn themselves off**, and so does setting `PFO_NO_SNAPSHOTS=1` in the
environment. Without snapshots:

- every load and rewind replays the session
- **a match with that player in it has no snapshots on anyone's device.** The game says so when
  the match starts (command 246), and from then on an online save is a file only, on every device,
  and an accepted load **restarts the match from the save**: every device replays to the save's
  frame and they connect again, the way a save from another match is loaded. It takes longer and
  grows with the length of the match.
- a save from a match whose timeline has snapshot loads in it can't be loaded (the menu says so)
- watching a match that uses saves isn't supported: a device with no seat can't send commands, so
  it can't announce that it has no snapshots, and the players would go on loading by snapshot,
  which it can't follow

## Playing with other versions

**Everyone needs the same version.** v0.3.1 and earlier can't play with v0.4.0, and the lobby keeps
the versions apart: a host refuses a joiner of another version, and a joiner leaves a host of
another version (v0.3.1 hosts let anyone in). The game only says the join failed; the reason is in
`pfo_online.log` ("another version of the build"). Lobbies of another version still show in the
list of oldkid.net rooms, and those on your own network don't.

Everything else is the same code as on Android: the room server, LAN discovery, direct connection
with the relay as fallback, drops and rejoining (a dropped player is waited for up to 90 seconds,
also one who has no player in the current game), the save list, votes on loads.

## Testing it without touching your real game

`tests/win/` has the helpers the Windows build was tested with. They never run the test game in
your own user folders: each test game gets a private `LOCALAPPDATA` under `work/win-sandbox/`, and
every run ends by checking that `%LOCALAPPDATA%\ufo50` (the Steam game's saves) is untouched. See
[TESTING.md](TESTING.md).
