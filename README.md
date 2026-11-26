# UFO 50 Android Online: PFO 50 + MultiMadnessMod

**v0.5.1** · Build your own Android version of **UFO 50** from your own Steam copy. It includes
online multiplayer from **PFO 50**, 4+ players in many games from **MultiMadnessMod**, touch
controls made for the game, and **save states**, in single player and online. Since v0.4.0 the same
build also makes a **Windows version** that plays online with the phones.

![Velgress co-op online: the host's phone and the joiner's phone](docs/screenshots/online_coop.png)
*The same co-op game on two phones, connected online. The picture is from v0.2, which showed the
connection (here LAN) as a tag under each player chip. Since v0.3 the session pill after the chips
says how you're connected.*

This repository contains no part of UFO 50. The build reads the game from your own Steam
installation, downloads the mods and tools from their authors' pages, and makes an APK only you
have. **Please don't share the APK.** UFO 50 is a paid game by a small studio. Buy it
([Steam](https://store.steampowered.com/app/1147860/UFO_50/)) and have your friends buy it too.

This is a fork of Skyline969's
[UFO50AndroidUnofficial](https://github.com/Skyline969/UFO50AndroidUnofficial), which showed that
UFO 50 runs on Android's GameMaker runner. The fork rebuilds the runner for UFO 50 1.9.5 and adds
the mods, the touch overlay and online play.

> **An early release.** So far it has been tested on emulators (BlueStacks), on one Windows 10 PC
> and on one real phone (an arm64 OnePlus, over Wi-Fi): not yet with a real controller on Windows,
> on a second PC, or with more than two players online. Expect rough edges, and please
> [open an issue](https://github.com/Patrick-Working/UFO50AndroidOnline/issues) with what you
> find, good or bad. What was tested and what wasn't: [docs/TESTING.md](docs/TESTING.md).

**Quick start:** install UFO 50 from Steam, download this repository
([zip](https://github.com/Patrick-Working/UFO50AndroidOnline/archive/refs/tags/v0.5.1.zip)), unzip
it, and double-click **`build-android.bat`** for the APK or **`build-windows.bat`** for the Windows
version. See [Quick build](#quick-build).

## Features at a glance

- **UFO 50 on Android**, built from your own Steam copy: nothing of the game is in this repository.
- **A Windows version** that plays online with the phones, without Steam and with its own saves.
- **A portable Windows copy** of the plain game that runs without Steam, saves kept in its folder.
- **Online multiplayer** (PFO 50): lobbies on your network and over the internet, no port setup.
- **Private lobbies** with a 6-character invite code you can share and paste.
- **Several players on one device** (WHO'S PLAYING): touch, gamepads and keyboard each take a slot, P1-P8.
- **Save states** in single player and online, every device back on the same frame.
- **Online loads by vote**: everyone sees the save's picture and accepts or declines.
- **Quick save and quick load** in one tap (F5 / F9 on a keyboard).
- **Rewind** 15, 30 or 60 seconds to the exact frame, with the time it will take.
- **Auto save when you pause**.
- **Fast rejoin**: an app that closed or crashed is back in the match in about a second.
- **Dropped players are waited for** for 90 seconds, then the match carries on.
- **Touch controls made for the game**: its own key labels and font, and a layout editor.
- **Gamepads and keyboard** drive the menus on phones and on Windows alike.
- **4+ players in many games** (MultiMadnessMod), up to 8.
- **More mods built in**: 12 Pingolf golfers, 2-player Star Waspir, 4-player Hyper Contender.
- **Every game's save states checked**: all 50 games tested, and a rejoin compared frame by frame.
- **Runs on Windows 7**, 10 and 11, from one folder.

## What's new in v0.5.1

Download v0.5.1 ([zip](https://github.com/Patrick-Working/UFO50AndroidOnline/archive/refs/tags/v0.5.1.zip))
and build it. Every device in a lobby needs the same build: v0.5.1 doesn't share lobbies with v0.5.0
or earlier.

- **The builder**: double-click `builder.bat` for the quick build in a window. It lists what the
  build is made with (each mod and tool, its author and the version it downloads), finds your UFO
  50, builds the Android App, Windows (UFO Online) and Windows (UFO Vanilla), can install the app
  on a phone over USB, and says in plain words why a build stopped.
- **Lobbies check the whole build**: every build has an identity (its version and a hash of its
  sources and mods), and only builds with the same one play together. A joiner with another build
  is told so on screen instead of a plain "join failed".

## What's new in v0.5.0

Download v0.5.0 ([zip](https://github.com/Patrick-Working/UFO50AndroidOnline/archive/refs/tags/v0.5.0.zip))
and build it to get these. Every device in a lobby needs the same build: v0.5.0 and earlier versions don't
share lobbies (different game data can't play in step).

**New**
- **More mods**: MultiMadnessMod 1.31 (The Big Bell Race for 8), **Ultimate Pingolf Roster** (12
  golfers), **Star Waspir 2P** and **Hyper Contender+** (4 players, new modes). Each was
  auditioned first: its games' save states with four players and an online rejoin compared
  frame by frame. The build fixes a one-player crash in Star Waspir 2P. See [docs/MODS.md](docs/MODS.md).
- **Several players on one device: WHO'S PLAYING.** Every controller is a player of its own: on a
  phone the touch controls and each gamepad, on Windows the arrow keys, the numpad keys and each
  gamepad. Before solo play and in an online lobby, each controller picks a slot P1-P8 and locks it
  in; every player shows its controller's icon and, online, its device's name. Saves, quick saves,
  rewind and loads work with all of them, and online the match carries every device's players
  through saves, loads and rejoins. (It replaces hot seat.)
- **One look on both versions**: the Windows menu in UFO 50's own font, the same colours and
  wording as the phones, no font shadow, and a short controller rumble on Windows when a save or
  load is done.
- **Saving and loading you can feel**: a buzz, a ring on the button and the save's picture flying
  into it; a load's picture opens out of its button and fades into the game.
- **Fast rejoin**: a player whose app closed gets the game as it is from the others and is back in
  about a second, instead of replaying the whole match. Online saves made before the rejoin load
  for them too.

**Fixed**
- **Save states and rejoins put the whole game back, exactly.** Every kind of state GameMaker
  holds was checked against what the 50 games use ([docs/SNAPSHOT_STATE.md](docs/SNAPSHOT_STATE.md)),
  and the game state of two devices was compared item by item, every frame, after a rejoin into
  each game. That found and fixed:
  - a desync after a fast rejoin into Barbuta (instances made again could take each other's ids);
  - instances and data structures made again under other numbers (two games compare them);
  - enemies and units that walk paths (library 12 and 50) standing still after a load or rejoin;
  - parts of a level that could never switch back on after a rejoin (library 12's map);
  - `noone` and other references to things gone coming back as plain numbers;
  - the save file not reaching a rejoined device, so the library differed after leaving a game
    (and a load now also puts your save file back as it was at the save).
  Result: after a rejoin, 49 of the 50 games are the same on both devices on every frame checked,
  and on the way back to the library (the 50th differs only in something it draws).
- A rejoined phone no longer takes the Windows window's picture size.
- Loading a save across MultiMadnessMod's split screen (four players apart in Onion Delivery or
  Rock On! Island) put the views back wrong; it puts them right now.
- QUICK LOAD in a lobby no longer starts solo play and leaves the lobby.
- No more green frame around the screen after a key or controller press (Android).
- A dropped player who is being waited for keeps their seat in the game's pause menu.

**Windows 7:** UFO 50 itself needs Windows 10 (Steam's requirement) and won't start on 7. This
build's Windows folders run there as well, **one folder for Windows 7, 10 and 11**: the build
rewrites two Windows 8 imports in its own copy of `ufo50.exe` (never the Steam game's) and puts a
`PathCch` stand-in beside it, which changes nothing on 10 and 11. Windows 7 needs SP1 with KB2670838
and KB2999226. See [docs/WINDOWS7.md](docs/WINDOWS7.md).

The full list: [CHANGELOG.md](CHANGELOG.md).

## What's new in v0.4.0

- **A Windows version.** `build-windows.bat` makes a second game folder beside your Steam
  installation: UFO 50 with this build's online play, save states and rewind, for a PC. It joins the
  same lobbies as the phones (your network, oldkid.net rooms, the relay) with the same invite codes
  and online saves. It never talks to Steam and keeps its own saves, so your Steam game and its
  saves are left alone. See [Windows version](#windows-version).
- **A menu for keyboard and controller on Windows.** **F1**, or both sticks pressed in, opens what
  the touch overlay is on a phone: your seat and connection, the invite code, SAVE, the list of
  saves with their pictures, rewind, auto save, your name. A load another player asks for shows in
  a box with the save's picture (**F2** accepts, **F3** declines).
- **Save states work the same on Windows**: a save loads in about a second, and an accepted load
  in a match puts every device, phone or PC, back on the same frame.
- **Everyone in a lobby needs the same version.** v0.4.0 and earlier versions can't join each
  other's lobbies any more (a match between them would go out of step), so update every device.
- **Online fixes.** A dropped player who has no player in the current game (a third player watching
  a 2-player game) is waited for like anyone else, instead of being left behind for good.
  Reconnecting after loading a save from another match no longer stalls for half a minute over the
  relay. A load that restarts the match starts on every device as soon as one device starts it, so
  none is left in the old match. Loading a library save from inside a game no longer stops the host
  with an error.
- **Overlay fixes.** A tap that closes a box no longer presses the control under it; a vote takes
  your first answer; seats P5 to P8 have colours.
- Many smaller fixes from a review of the whole code: see [CHANGELOG.md](CHANGELOG.md).

## What's new in v0.3.1

- **Load and rewind times you can trust.** Estimates now know how fast this device replays each
  game (some replay over twice as fast as others), so a long rewind no longer takes much longer
  than it said: in tests they're within 10%, leaning long. The loading screen shows the time left.
- The ask-first box fits its contents, and REWINDING shows once.

## What's new in v0.3

- **Save states.** Save anywhere in any of the 50 games and load it back in about a second. Tap
  the disk icon (top right) for the save list: SAVE NOW, pick a save to see its picture and
  details, LOAD or DELETE. Tick **AUTO SAVE WHEN I PAUSE** to have pausing save into one AUTO slot.
  See [Save states](#save-states).
- **Rewind.** REWIND 15 S / 30 S / 60 S in the save list takes you back that far, to the exact
  frame. Each button shows how long it will take; anything 5 seconds or more asks first. It
  doesn't add stutters to play: it starts from your saves and from snapshots taken only while the
  game is paused or just starting, and replays your inputs from there.
- **Save states online.** SAVE NOW in an online match saves for everyone at the same moment.
  LOAD asks the other players first ("P1 WANTS TO LOAD", ACCEPT / DECLINE), and everyone loads
  together. Saves from earlier matches load too, if they had the same number of players.
- **Drops don't end the match.** If a player's connection drops, everyone waits up to 90 seconds
  ("WAITING FOR P2 - 1:26") and play resumes when they're back. If their app was closed or crashed,
  they reopen it and tap **REJOIN**: their phone catches up on the match in a couple of seconds.
  This works over the relay too.
- **A tidier overlay.** One chip per player (touch and gamepad icons inside it), a session pill
  that says how you're connected (SOLO: SAVES ON, ONLINE: LAN 2F, CATCHING UP...) and then
  settles to a dot, short banners, and online players' names on a tap. In the layout editor,
  **CHIPS ROW / COLUMN** stacks the chips vertically.
- **A crash fixed.** About one load in forty, and now and then opening a game, could crash the app
  (GameMaker's audio decoding read the game thread's state). Fixed; 40 of 40 test loads ran clean.
- Fixes: even line weight in the disk icon, nothing pokes out from under the save list, and the
  orange warning banners use white text.

The full list is in [CHANGELOG.md](CHANGELOG.md). How save states, rewind and rejoining work:
[docs/SAVE_STATES.md](docs/SAVE_STATES.md). How it was tested, and what hasn't been:
[docs/TESTING.md](docs/TESTING.md).

---

## Screenshots

**New in v0.5.0**

| | |
|---|---|
| ![WHO'S PLAYING on a phone](docs/screenshots/whos_playing_phone.png) | ![WHO'S PLAYING on Windows, in an online lobby](docs/screenshots/whos_playing_windows.png) |
| **WHO'S PLAYING on a phone**: the touch controls took P1, a gamepad P2 | **WHO'S PLAYING in an online lobby** (Windows): this PC's arrow keys in P1 and numpad in P2, the other PC's player in P3 |
| ![Ultimate Pingolf Roster](docs/screenshots/mod_pingolf_roster.png) | ![Hyper Contender+](docs/screenshots/mod_hyper_contender.png) |
| **Ultimate Pingolf Roster**: 12 golfers for 8-player Pingolf | **Hyper Contender+**: up to 4 contenders, new modes and a new character |
| ![Star Waspir 2P](docs/screenshots/mod_star_waspir_2p.png) | ![MultiMadnessMod split screen](docs/screenshots/split_screen.png) |
| **Star Waspir 2P**: two pilots, each with their own score and eggs | **Split screen** (MultiMadnessMod, Onion Delivery with 4): save states now load across it |
| ![The Windows version's menu](docs/screenshots/windows_menu.png) | |
| **Windows: the menu** in UFO 50's own font, the same look and words as on the phones | |

**Earlier**

| | |
|---|---|
| ![Lobby browser with a lobby found](docs/screenshots/lobby_browser.png) | ![In the lobby, both players listed](docs/screenshots/lobby_joined.png) |
| **Online lobby list**: lobbies on your network and on the internet, plus HOST PUBLIC / HOST PRIVATE / JOIN BY CODE | **In the lobby**: both players are listed |
| ![Players panel](docs/screenshots/players_panel.png) | ![Layout editor](docs/screenshots/layout_editor.png) |
| **Players panel**: touch and each gamepad to P1-P4, your name, join code, UPnP, WHO'S PLAYING | **Layout editor**: move, resize and hide controls, opacity, 4-way / 8-way d-pad |
| ![Save states](docs/screenshots/save_states.png) | ![Loading a save online](docs/screenshots/online_load_vote.png) |
| **Save states**: the save list with each save's picture, game, players and time into the game, and the REWIND buttons with how long each takes | **Loading online**: the other players see what you want to load and accept or decline |
| ![Rewind asks first when it will take a while](docs/screenshots/rewind_confirm.png) | ![Rewinding](docs/screenshots/rewinding.png) |
| **Rewind**: a rewind that will take 5 seconds or more asks first, with the estimate | **Rewinding**: your inputs replay at high speed to the exact frame, with the time left |
| ![Waiting for a dropped player](docs/screenshots/waiting_for_player.png) | ![Rejoining a match](docs/screenshots/rejoin.png) |
| **A dropped player**: everyone waits, with a countdown, until they're back | **Rejoining**: reopen the app and tap REJOIN to catch up with the match |
| ![The Windows version's list of saves](docs/screenshots/windows_saves.png) | |
| **Windows: the list of saves** (v0.4.0's font), with the chosen save's picture and how long it takes to load | |

The first four pictures (lobby list, lobby, players panel, layout editor) are from v0.2. The overlay
has changed since: the connection (LAN, DIRECT or RELAY) is no longer a tag under the player chip,
it shows in the session pill after the chips. See *Overlay* in [CHANGELOG.md](CHANGELOG.md).

## Features

**Online multiplayer (PFO 50)**
- Play UFO 50's multiplayer games with people on other phones running this build (or PCs running
  its [Windows version](#windows-version)), in PFO 50's own lobby. Players on the same network are
  found automatically, and anyone else connects over the internet.
- The lobby menu has **HOST PUBLIC**, **HOST PRIVATE** and **JOIN BY CODE**. A private lobby gets a
  6-character code.
  - In the lobby, **SHARE CODE** opens a box with SHARE (Android's share sheet), COPY and CLOSE.
  - The join box has PASTE, which picks the code out of a pasted message.
- Internet lobbies are listed by the [okplay](https://github.com/Patrick-Working/okplay) room
  server at [oldkid.net](https://oldkid.net). See [How online play connects](#how-online-play-connects).
- The pill next to the player chips says how you're connected when it changes, then settles to a
  dot: **LAN** (same network), **DIRECT** (peer to peer over the internet) or **RELAY** (through
  the server), with the input delay in frames. Tap it to see it again. **CATCHING UP** (orange)
  means your phone is behind and running faster to catch up.
- Each player has a chip in their colour: yours shows your input (touch, a gamepad), the others a
  globe. Their names show when the match starts and when you tap the chips.
- You stay connected when you switch apps or the screen turns off. While you're online, a quiet
  notification ("UFO 50 - online") stops Android from freezing the game in the background.
- Your seat is locked while online. Each phone plays its own player, so nobody can take over a
  remote player's controls.
- Set your **player name** in the players panel (NAME).

**Local multiplayer (MultiMadnessMod + the players panel)**
- Up to 4+ players in many games, on one phone, with touch and Bluetooth or USB gamepads.
- **Players panel**: tap your player chip (top left), or use the game's own *Settings > Joypad
  Config > Pad Assign*. Bind the touch controls and each connected gamepad to P1-P4 or OFF. Each pad
  gets a numbered badge.
- **Several players on one device (WHO'S PLAYING)**: every controller is a player of its own, the
  touch controls and each gamepad. Before solo play (START SOLO PLAY) and in an online lobby (the
  Players panel), tap a slot P1-P8 to take it with touch; a pad joins with A, moves with the d-pad,
  locks in with A and backs out with B. Solo play then records every player, so saves and rewind
  work for all of them; online, each device's players get the slots they locked in (the host sorts
  out clashes) and the match carries them through saves, loads and rejoins.
- A classic d-pad (8-way by default, with a 4-way switch), the two action buttons, MENU and BACK.
- Buttons carry the labels the game itself shows: the keys it has bound (Z / X, or DEL / END on
  player 2's side of the keyboard). They follow your rebinds and use UFO 50's own font and colours.
- The layout editor (the ☰ button, top right) opens **TOUCH CONTROLS** in the middle of the
  screen: drag the controls around it to move them, pinch them (or drag their yellow dot) to
  resize them. In the panel: **TOUCH OPACITY** (a slider), **AUTO HIDE** (the touch pad hides while
  a gamepad is connected), **PLAYER CHIPS** horizontal or vertical, the **D-PAD** 8-way or 4-way,
  **QUICK SAVE / LOAD** buttons on or off, and **RESET TO DEFAULTS**. **ACCEPT** keeps the changes,
  **CANCEL** puts back what you had.
- *Settings > Video Settings > Scale* works. 4x leaves room beside the picture for the controls,
  and FILL fills the screen.

<a id="save-states"></a>
**Save states**
- The disk icon (top right) opens the save list. **SAVE NOW** saves; each save keeps a picture,
  the game, who was playing and how far into the game. Saves are kept until you delete them.
- In single player, saving needs a recorded session: tap **START SOLO PLAY** in the save list once
  (it starts you in the library). The pill then says SOLO: SAVES ON.
- **Quick save and quick load**: two buttons on opposite sides of the screen (quick save on the
  left, an arrow down into a tray; quick load on the right, an arrow up out of it, so one can't be
  bumped for the other) save into one QUICK slot, replaced each time, and load it, in one tap. Move
  them, or switch them off, in the layout editor; the
  save list has QUICK SAVE and QUICK LOAD too. With a keyboard: **F5** and **F9** (on Windows as on
  a phone). They work in solo play; in an online match, use SAVE NOW and LOAD.
- **Keyboard and controller on a phone**, as on Windows: **F1**, or both sticks pressed in, opens
  the save list; the arrows or the D-pad move a yellow outline, Enter / A chooses (on a save: loads
  it), Esc / B goes back, Delete / X deletes (press twice). **F2** / **F3** answer a load another
  player asked for, or the offer to rejoin; with a controller, both sticks in first.
- **Loading** takes about a second for a save made since the app started: the game puts its state
  back and checks the next two seconds of play against what was recorded, both PFO's checksums
  and three pictures of what the game draws. Anything different and it replays instead. Older
  saves (after the app restarted) replay the session at high speed, a few seconds per minute of
  play. Either way the result is checked against PFO's own checksums.
- **AUTO SAVE WHEN I PAUSE** (in the save list) saves into one AUTO slot each time you pause,
  replacing the last one. Its picture is from just before the pause.
- Every game works: all 50 were tested loading in the same room, from the library and from inside
  another game, and after a fast rejoin each game's whole state was compared with the other
  device's, frame by frame (see [docs/SNAPSHOT_STATE.md](docs/SNAPSHOT_STATE.md)).
- With several players on one device (WHO'S PLAYING), saves record every player.
- **Rewind** (single player): **REWIND 15 S / 30 S / 60 S** in the save list go back that far, to
  the exact frame, and play carries on from there. Each shows how long it will take, measured on
  your device; if it's 5 seconds or more you're asked first ("REWIND 60 SECONDS? ABOUT 40 S TO
  LOAD"), and LOAD does the same for a save that has to replay. A rewind starts from the newest
  snapshot before that point (your saves, auto saves, and snapshots the game takes while it's
  paused or a game is just starting) and replays your inputs from there, so play never stutters
  for it.
- How it all works: [docs/SAVE_STATES.md](docs/SAVE_STATES.md).

**Save states online**
- **SAVE NOW** in an online match saves on every phone at the same frame, so everyone gets the save
  (the save carries a command in the game's input, like PFO 50's own commands).
- **LOAD** asks the others: they see the save's picture and details and have 15 seconds to
  ACCEPT or DECLINE. When everyone with a seat accepts, every phone loads it at the same frame and
  the match carries on from there. A decline, or no answer, cancels it for everyone.
- Saves from an earlier match load too, if it had the same number of players: your phone sends the
  save to the others first, and everyone restarts the match from it.
- In a match with a player whose device has no snapshots (a Windows game with another `ufo50.exe`
  than the build was made for) every load works that way: everyone replays the match from the save
  and connects again. It takes a few seconds per minute of the save's match instead of about one
  second.
- Players watching without a seat can't save or vote.

**Drops and rejoining**
- When a player's connection drops, the match freezes for everyone with an orange banner:
  **WAITING FOR P2 - 1:26**. The phones keep trying to reconnect, and when they do, play carries on
  where it stopped (**P2 IS BACK**). After 90 seconds the match ends as before.
- That goes for a player who has no player in the current game too (the third player in a 2-player
  game). If they aren't back after 90 seconds, the match carries on without them.
- If a player's app was closed or crashed, reopening it offers **REJOIN YOUR MATCH?**. REJOIN
  takes their old place: the other player sends the game as it is (with every seat that device had
  and the save files), it's put back in about a second and play carries on. (With three or more players, through the relay, or if that doesn't work
  out, the match so far is sent instead and replayed at high speed, which takes longer for a long
  match.) It works over the relay too.
- With three or more players, the ones left share the dropped player's last inputs so they all wait
  at the same point. (Rejoining has been tested with two players.)

**Other fixes**
- Quit in the main menu closes the app.
- A crash in GameMaker's audio decoding (now and then when loading or opening a game) is fixed.
- Runs on arm64 phones and on x86_64 emulators (BlueStacks, Android Studio's emulator).

## How online play connects

The game finds the fastest way to reach the other players, in this order:

1. **Same network (LAN)**: found by broadcast, with no server involved.
2. **Direct (peer to peer)**: the host's lobby is listed on the okplay room server along with its
   addresses. When you join, both phones send to each other at once (hole punching). That gets
   through almost every home router and phone carrier.
3. **UPnP**: a host asks its router to open the game's port, if the router allows it. You can turn
   this off in the players panel (UPNP).
4. **Relay**: if nothing direct gets through within 2.5 seconds, the game falls back to the okplay
   relay on oldkid.net.

**The relay is slower.** Every packet goes through the server, which adds about 0.1-0.2 s each
way. That's fine for lobbies and turn-based games but noticeable in action games. PFO 50 raises
its input delay to compensate: from 6 frames to about 12 in our tests.

In most conditions you'll be **direct**. The relay is for awkward networks (double NAT, strict
firewalls, some emulators). If you see RELAY at home, turning on UPnP on the host's router usually
gets you DIRECT.

![A lobby connected through the relay](docs/screenshots/lobby_relay.png)

The room server matches players and relays packets when it has to. It holds only the open lobbies
(name, player count, addresses), and each one disappears when its lobby closes.

The netcode lives in okplay's [`oknative/`](https://github.com/Patrick-Working/okplay/tree/main/oknative)
folder, so other games can use it too.

## Quick build

You need **Windows 10 or 11** to build it, and **UFO 50 1.9.5 from Steam**. Nothing else to install,
and no admin rights.

**With a window:** double-click **`builder.bat`**. It finds your game, lets you tick what to build
(Android App, Windows (UFO Online), Windows (UFO Vanilla)) and shows the build's steps, then
where the results are. It runs the same build as the steps below.

1. Download this repository
   ([zip](https://github.com/Patrick-Working/UFO50AndroidOnline/archive/refs/tags/v0.5.1.zip)) and
   unzip it anywhere.
2. Double-click **`build-android.bat`** (or `build.bat`, which is the same). It finds UFO 50 in your Steam library and checks the version, then
   downloads the tools and mods into `work\`, each checked against a fixed checksum.
3. **The first time**, the build runs the UFO 50 Mod Loader without a window to install both mods
   (a minute or two). It makes a modded copy of the game and leaves your Steam installation alone.
   If that doesn't work on your PC, the Mod Loader's window opens instead with both mods in place:
   click **Install**, wait until it says it's done, and only then close it. (`-ManualLoader` goes
   straight to the window. The Mod Loader's headless mode has a bug in 2.3.0; the build works
   around it with a small startup hook it compiles on your PC, and runs the loader as downloaded.)
4. A few minutes later you get **`work\ufo50-online.apk`**.

Copy the APK to the phone and open it. You may need to allow your file manager to install apps.
If you get "package appears to be invalid", the copy was cut short: copy it again and check it is
the same size on both ends.

With the phone connected over USB and USB debugging on, `build.bat -Install` installs it directly
(this needs adb). If UFO 50 isn't found, run `build.bat -GamePath "D:\path\to\UFO 50"`.

The APK is signed with a key made for you on the first build (`work\keys\`). Keep that folder:
Android only installs an update over an existing install when it's signed with the same key.

**The plain game:** `build.bat -Vanilla` makes `work\ufo50-vanilla.apk`, your copy of the game as
Steam has it on the Android runner and nothing else: no mods, no online play, no save states and
no touch controls, so it needs a controller or a keyboard. Nothing is patched and no mods are
downloaded. It is the same app as the online build, signed with the same key: installing one
replaces the other and keeps the saves. (The game still asks Steam for things every frame and the
runner notes each one in the Android log; that is harmless.)
`build.bat -Vanilla -Windows` makes `work\windows\UFO 50 Vanilla`, a portable copy of the plain game
that doesn't need Steam: Steam's libraries are left out, and no game data is changed (`ufo50.exe`
only gets the Windows 7 fixups, as the Online folder does). Start
`UFO 50 Portable.bat` in it and the saves stay inside the folder (`saves\`), so the whole folder can
be moved to another PC. (Starting `ufo50.exe` itself uses the PC's save folder, the Steam game's.)

**What the quick build does:** it patches the modded game for Android
(`scripts/android_patch.csx`: hot seat, pad bindings, the lobby menu, Quit). It takes the
overlay's font and button icons from your copy of the game (`scripts/export_overlay_art.csx`).
Then it packs the game into the **runner kit** and signs the result (`scripts/pack_apk.csx`).

The runner kit (`kit/ufo50-online-runner-v0.4.0.apk`) is the part that's the same for everyone:
GameMaker's Android runner with the touch overlay, the online service and PFO 50's netcode, and no
game data. The full build makes it from source.

## Full build

For changing the overlay or the netcode: this builds the runner kit's contents from source, then
does the quick build with it. It runs in Git Bash and needs:

| what | where | notes |
|---|---|---|
| **Git for Windows** | <https://git-scm.com> | Git Bash |
| **JDK 21** (or 17) | `winget install EclipseAdoptium.Temurin.21.JDK` | for GameMaker's Android build |
| **Android SDK** | [Android Studio](https://developer.android.com/studio) or the [command-line tools](https://developer.android.com/studio#command-line-tools-only) | build-tools 35.0.0, platform android-35, NDK 27.2.12479018. With the command-line tools installed, `build.sh` installs the rest after you accept Google's licences. |
| **GameMaker** | <https://gamemaker.io> (free account) | Sign in once, and install runtime **2024.14.1.253** with the Android module (*File > Preferences > Runtime Feeds*). |

```bash
bash build.sh --check     # is everything found?
bash build.sh             # build the runner from source, then the APK
bash build.sh --kit       # build only the runner kit, into work/kit/
```

The quick build does not look in `work/kit/`. It uses the kit in `kit/`, and downloads that file
from the version's tag if the one in `kit/` is missing or has another checksum. To make the quick
build use a kit you built: copy it to `kit/` and put its SHA-256 (`build.sh --kit` prints it) in the
`Kit` line of `build.ps1`.

Paths it can't find go in `local.env` (copy `local.env.example`). The full build fetches the
[PFO 50 source](https://github.com/Blaizer/PFO-50), [ENet](https://github.com/lsalzman/enet) and
okplay's netcode, all at pinned versions. It builds `libPFO.so` with the NDK and the runner
(`android/gm-project`) with GameMaker's Igor, using your GameMaker account the same way the IDE
does when you export a game.

`scripts/make_icon.py` redraws the app icon into `android/gm-project/icons/` (Python 3 + Pillow;
`--preview` also writes `work/icon_preview.png`). You only need it to change the icon.

<a id="windows-version"></a>
## Windows version

Double-click **`build-windows.bat`** (or run `build.bat -Windows`). It makes
`work\windows\UFO 50 Online`: your modded copy of the game with this build's netcode
(`PFO.dll`) in place of PFO 50's Steam networking. The first run installs the mods with the UFO 50 Mod Loader, as in
step 3 of the [quick build](#quick-build). Start `ufo50.exe` in that folder.

- It plays with the phones and with other PCs running it: HOST PUBLIC, HOST PRIVATE, JOIN BY CODE
  and the lobby list work as on Android.
- **It never talks to Steam**: there is no Steam library in the folder, Steam doesn't need to be
  running, and it can't join PFO 50 lobbies on Steam. It keeps its own saves in
  `%LOCALAPPDATA%\ufo50_online`; the Steam game's saves (`%LOCALAPPDATA%\ufo50`) are never read or
  written.
- **F1**, or **both sticks pressed in** on a controller, opens its menu: what the touch overlay
  does on a phone, made for a keyboard and a controller (seat and connection, invite code, SAVE,
  the list of saves with their pictures, START SOLO PLAY and REWIND, auto save, name, UPnP). A load
  another player asks for shows in a box with the save's picture: **F2** accepts, **F3** declines.
  **F5** quick saves and **F9** quick loads (solo play).
- **Saves load from snapshots as on the phones** (about a second; in a match, on every device
  within a few frames). They are made for the `ufo50.exe` of UFO 50 1.9.5: with another one the
  game turns them off, replays instead (about 4 seconds per minute played on the test PC), and a
  match with that player in it restarts from the save on every load.
- **Use the same build on every device.** A lobby only takes players with its own build: the same
  version, the same mods and the same patches (the build's identity, `0.5.1+1a2b3c4d` in the log
  and on the lobby's MODS screen). A joiner with another one is told so on screen.
- The folder contains your copy of the game, like the APK: **keep it to yourself.**

The quick build takes `PFO.dll` from the **Windows kit** (`kit/ufo50-online-windows-v0.4.0.dll`,
no game data). `bash build.sh --windows` compiles it instead, with
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw), which it downloads; it needs none of the
Android tools. Everything else is in [docs/WINDOWS.md](docs/WINDOWS.md).

## Versions

This version (**v0.5.1**) is made for exactly these versions. The patches are written against them,
and the build checks the checksums.

| part | version | from |
|---|---|---|
| UFO 50 | **1.9.5** (Steam) | [Mossmouth](https://50games.fun) |
| PFO 50 | **0.10.0** | [GameBanana](https://gamebanana.com/mods/648717), source [Blaizer/PFO-50](https://github.com/Blaizer/PFO-50) @ `b8c9327` |
| MultiMadnessMod | **1.31** | [GameBanana](https://gamebanana.com/mods/605308) |
| Ultimate Pingolf Roster, Star Waspir 2P, Hyper Contender+ | **1.0.0**, **0.9.0**, **1.6.10** | GameBanana: [679074](https://gamebanana.com/mods/679074), [698092](https://gamebanana.com/mods/698092), [604987](https://gamebanana.com/mods/604987) ([docs/MODS.md](docs/MODS.md)) |
| UFO 50 Mod Loader | **2.3.0** (portable) | [phil-macrocheira/UFO-50-MOD-LOADER](https://github.com/phil-macrocheira/UFO-50-MOD-LOADER) |
| UndertaleModTool CLI | **0.9.2.0** | [UnderminersTeam/UndertaleModTool](https://github.com/UnderminersTeam/UndertaleModTool) |
| GameMaker runtime | **2024.14.1.253** (VM, Android) | [YoYo Games](https://gamemaker.io) |
| ENet | **1.3.18** + okplay's relay hooks | [lsalzman/enet](https://github.com/lsalzman/enet) |
| okplay oknative | `d536388` | [Patrick-Working/okplay](https://github.com/Patrick-Working/okplay/tree/main/oknative) |
| Android NDK / build-tools / platform | 27.2.12479018 / 35.0.0 / android-35 (full build only) | Google |
| llvm-mingw | **20261104** (ucrt, x86_64; full Windows build only) | [mstorsjo/llvm-mingw](https://github.com/mstorsjo/llvm-mingw) |
| App | package `com.unofficial.ufo50`, minimum Android 7.0 (API 24), target API 35 | |

The okplay room server is `https://oldkid.net/aoe2net/okplay.php`. UFO 50 lobbies use their own
app id and wire number there, so they never mix with other okplay games.

## Playing

- **First start**: go to *Settings > Video Settings > Scale*. 4x leaves room for the controls, and
  FILL fills the screen.
- **Move the controls**: ☰ (top right) opens TOUCH CONTROLS. Drag a control to move it, and
  pinch it or drag its yellow dot to resize it. Touch opacity, auto hide, the player chips, the
  4-way / 8-way d-pad and the quick buttons are in the panel. ACCEPT keeps it, CANCEL doesn't.
- **Gamepads**: connect them, then tap your player chip to open the players panel and give each
  pad a player.
- **Several players on one phone**: START SOLO PLAY (or, online, the Players panel's WHO'S PLAYING),
  then each controller picks a slot and locks it in.
- **Online**: choose *Online* from the main menu.
  - HOST PUBLIC to be listed, HOST PRIVATE to get a code, JOIN BY CODE to enter one.
  - Everyone needs the same build (Android or Windows), with the same game version and the same
    mods. A lobby that won't take you says so: CAN'T JOIN: THE HOST HAS ANOTHER BUILD.
- **Names**: players panel > NAME. Change it while you're not in a lobby.
- **Save states**: the disk icon (top right). In single player, START SOLO PLAY first. Online,
  SAVE NOW saves for everyone and LOAD asks the others.
- **Rewind**: the save list's REWIND buttons, in single player (after START SOLO PLAY).
- **Someone dropped**: wait for them (the banner counts down). If your app closed, reopen it and
  tap REJOIN. The others wait 90 seconds for you. The app offers REJOIN for 100 seconds after it
  was last running in the match.

## Troubleshooting

- **The lobby list doesn't show my friend's game.** Give the search a few seconds; internet rooms
  can take a moment on mobile data. Check that you both have the same build. A private lobby only
  shows up with its code.
- **The lobby shows, but joining it fails every time.** Check that both devices have the same
  version of this build: since v0.4.0 a lobby refuses other versions (the log says "another
  version of the build").
- **"Timed out attempting to join lobby".** Try again once. If it keeps happening, the host can
  turn on UPnP on their router. The relay should still connect you, just more slowly.
- **Disconnected after switching apps.** Allow the app's notification (Android 13 and later ask).
  Without it Android may freeze the game in the background.
- **"App not installed" when updating.** The new APK was signed with a different key (a different
  `work\keys` folder). Uninstall the old one first, which deletes its saves, or build with the old
  `work\keys` folder.
- **"COULDN'T REJOIN THE MATCH".** The others stopped waiting (90 seconds after the drop), or
  your phone can't reach them. Start a new match from the lobby.
- **The save list says START SOLO PLAY / rewind isn't there.** Single-player saves and rewind need
  a recorded session: tap START SOLO PLAY once after starting the app. It takes you to the library.
- **A load or rewind is slow.** Saves from before the app restarted, and long rewinds without a
  snapshot nearby, replay the session: a few seconds per minute of play. Pausing now and then
  gives rewind more places to start from.
- **A save won't load online.** It needs the same number of players as the match ("THAT SAVE IS
  FOR 1 PLAYER"), and everyone with a seat has to accept.
- **Build errors.** The quick build prints the failing step. For the full build, run
  `bash build.sh --check`; Igor's log is `work/gm/igor.log`.

**Testing switches** (for developers), as files in
`/sdcard/Android/data/com.unofficial.ufo50/files/`:
- An empty file named `forcerelay` makes that phone join through the relay only.
- An `okplay.txt` file holding a URL points the game at another room server. okplay's
  `rdv_standin.py` runs one locally.
- `ss_cmd` takes test commands (save, load, rewind, netdrop, rejoin...). See
  [docs/TESTING.md](docs/TESTING.md) for those, the development switches and the test scripts.

## What's in this repository

| path | what |
|---|---|
| `build-android.bat`, `build-windows.bat`, `build.bat`, `build.ps1` | the quick build (`build.bat` is the Android one; `-Windows` makes the Windows version) |
| `builder.bat`, `builder.ps1` | the builder: the quick build in a window (`tests/gui/replay_build.ps1` stands in for `build.ps1` in its tests) |
| `build.sh` | the full build (`--windows` for the Windows version) |
| `kit/` | the runner kit and the Windows kit the quick build uses (made by `build.sh --kit` and `build.sh --windows --kit` in `work/kit/`, then copied here; no game data) |
| `android/gm-project/` | the GameMaker runner project: the PFO extension with the touch overlay (`PFO_Android.java`), the online service, and the app icon (original art) |
| `pfo/` | the port of PFO 50: a patch to its `dllmain.cpp`, plus the layer both versions share in `android/` (Steam lobbies and networking over ENet, the bridge to the GameMaker runner, the hot seat and pad builtins, and save states, online saves and rejoining in `savestate.inc`), and what only Windows needs in `win/` (its bridge to the Windows runner, the F1 menu, small stand-ins for the Android calls) |
| `scripts/` | the game patches (`android_patch.csx` with `pfo_snapshot.gml`, the game-side half of save states; `windows_patch.csx`), the overlay art export, the APK packer and signer (UndertaleModTool scripts), and the native builds (`build_pfo_android.sh`, `build_pfo_windows.sh`) |
| `docs/` | [SAVE_STATES.md](docs/SAVE_STATES.md) (how save states, rewind, online saves and rejoining work), [WINDOWS.md](docs/WINDOWS.md) (the Windows version), [TESTING.md](docs/TESTING.md) (how it was tested), and the screenshots on this page |
| `tests/` | the scripts that test save states, rewind and online play on BlueStacks, and in `tests/win/` the Windows version, alone and against BlueStacks (see TESTING.md) |
| `CHANGELOG.md` | what changed in each version |

Nothing from the game is stored here: no game files, art or music. The only game code is the short
snippets the patch script searches for in order to patch them. The screenshots show the game
running.

## Tested on

v0.4.0 was tested on **BlueStacks 5** (Android 9, x86_64), two instances side by side for online
play, and on **Windows 10** for the Windows version: two copies of the game in one match, and one
copy in a match with a BlueStacks instance, hosting each way. Every online scenario passed on the
final sources (save, load, declined load, drop, rejoin, loading an earlier match's save, a match
without snapshots, a join between two versions being refused), and so did rewind. On Windows every
game's save states passed (50 of 50) and the menu was walked through by keyboard and by a stand-in
for a controller. The Android sweep of every game's save states (150 of 150 runs) is from v0.3.1
and wasn't repeated.

v0.5.0: a real phone (a OnePlus 6T, arm64) played online against the Windows version through the
real room server, both ways, and hosted a match with two players of its own. On the final build the
quick regression passes (18 tests: both versions, both emulators, cross-play each way), and so do
the mods' games (save states with four players on Windows and Android, an online rejoin compared
frame by frame) and loads across split screen. A fast rejoin into each of the 50 games was compared
item by item with the other device (49 the same throughout; the 50th differs only in what it
draws). The Windows 7 fixups were tested on Windows 10 (nothing changes there); **no Windows 7 PC
has run it yet**.

**Not tested yet:** a second PC, a real controller on Windows, three or more players online, and a
`ufo50.exe` other than 1.9.5's. If you try any of these, please open an issue with how it went.
Details: [docs/TESTING.md](docs/TESTING.md).

## Credits

- **UFO 50** by [Mossmouth](https://50games.fun). Please buy it.
- **[PFO 50](https://github.com/Blaizer/PFO-50)** by Blaizer
  ([GameBanana](https://gamebanana.com/mods/648717)), MIT licence: the online multiplayer all of
  this is built on. What we use: its game-side code, which the Mod Loader puts in as the mod
  "PFO 50" (about 70 of the game's scripts: the lobby screens, keeping players' inputs in step,
  starting a session, the screen that compares players' mods), and its C++ (`dllmain.cpp`), the
  base of our `PFO.dll` and `libPFO.so`. Our part: the netcode under it (no Steam: LAN, the room
  server, hole punching, the relay), save states, rewind, rejoining, several players per device,
  the Android port and the Windows version's menu (`pfo/`, `scripts/`).

All mods are downloaded at build time from their authors' GameBanana pages and installed with
the Mod Loader; none of them is stored in this repository (`docs/MODS.md`). The builder window
(`builder.bat`) lists all of these, with the versions it downloads, before you build.

- **[MultiMadnessMod](https://gamebanana.com/mods/605308)** by Jonesey13: 4+ players in many games.
- **[Ultimate Pingolf Roster](https://gamebanana.com/mods/679074)** by Bubba256,
  **[Star Waspir 2P](https://gamebanana.com/mods/698092)** by ToastGhost26 and
  **[Hyper Contender+](https://gamebanana.com/mods/604987)** by greenhead352: more golfers, a second
  Star Waspir player, and 4-player Hyper Contender.
- **[UFO50AndroidUnofficial](https://github.com/Skyline969/UFO50AndroidUnofficial)** by Skyline969:
  the original Android build this repository is forked from.
- **[UFO 50 Mod Loader](https://github.com/phil-macrocheira/UFO-50-MOD-LOADER)** by Phil
  Macrocheira, MIT licence: applies the mods.
- **[UndertaleModTool](https://github.com/UnderminersTeam/UndertaleModTool)** by the Underminers
  team, GPL-3.0: reads and patches the game data, and runs the build's scripts.
- **[ENet](https://github.com/lsalzman/enet)** by Lee Salzman, MIT licence: the network transport.
- **[okplay](https://github.com/Patrick-Working/okplay)** and the [oldkid.net](https://oldkid.net)
  room server: the lobby list, hole punching, UPnP and the relay.
- **GameMaker** by YoYo Games: the Android runner.
- **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)** by Martin Storsjö: compiles the Windows
  version's `PFO.dll`.
- **[api-ms-win-core-path-HACK](https://github.com/nalexandru/api-ms-win-core-path-HACK)** by
  nalexandru (Wine's code, LGPL-2.1): the `PathCch` stand-in the Windows 7 fixups put beside the
  game, downloaded at build time. The `MFTGetInfo` swap is the community's known fix for GameMaker
  games on Windows 7 ([YellowAfterlife's patcher](https://yal.cc/gm2024-win7/)).
- Public STUN servers from Cloudflare and Google.

## Licence

The code in this repository is MIT licensed (see `LICENSE`). The tools and mods it downloads keep
their own licences. UFO 50 is © Mossmouth.
