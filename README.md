# UFO 50 Android Online: PFO 50 + MultiMadnessMod

Build your own Android version of **UFO 50** from your own Steam copy. It includes online
multiplayer from **PFO 50**, 4+ players in many games from **MultiMadnessMod**, and touch
controls made for the game.

This repository contains no part of UFO 50. `build.sh` reads the game from your own Steam
installation, downloads the mods and tools from their authors' pages, and builds an APK only you
have. **Please don't share the APK.** UFO 50 is a paid game by a small studio. Buy it
([Steam](https://store.steampowered.com/app/1147860/UFO_50/)) and have your friends buy it too.

This is a fork of Skyline969's
[UFO50AndroidUnofficial](https://github.com/Skyline969/UFO50AndroidUnofficial), which showed that
UFO 50 runs on Android's GameMaker runner. The fork rebuilds the runner for UFO 50 1.9.5 and adds
the mods, the touch overlay and online play.

---

## Features

**Online multiplayer (PFO 50)**
- Play UFO 50's multiplayer games with people on other phones running this build, in PFO 50's own
  lobby. Players on the same network are found automatically, and anyone else connects over the
  internet.
- The lobby menu has **HOST PUBLIC**, **HOST PRIVATE** and **JOIN BY CODE**. A private lobby gets a
  6-character code.
  - In the lobby, **SHARE CODE** opens a box with SHARE (Android's share sheet), COPY and CLOSE.
  - The join box has PASTE, which picks the code out of a pasted message.
- Internet lobbies are listed by the [okplay](https://github.com/Patrick-Working/okplay) room
  server at [oldkid.net](https://oldkid.net). See [How online play connects](#how-online-play-connects).
- A tag under your player chip shows how you're connected:
  - **LAN** (green): same network.
  - **DIRECT** (blue): peer to peer over the internet.
  - **RELAY** (orange): through the server.

  The host's tag shows the slowest link among its players.
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
- **Hot seat**: in turn-based games, the touch controls follow whoever's turn it is. This covers
  Party House, Bug Hunter, Mortol II, Lords of Diskonia, Avianos, Hot Foot and Grimstone. A yellow
  **HS** block marks the active player and blinks when the turn changes. Turn it on or off from the
  player chip's menu.

**Touch controls**
- A classic d-pad (8-way by default, with a 4-way switch), the two action buttons, MENU and BACK.
- Buttons carry the labels the game itself shows: the keys it has bound (Z / X, or DEL / END on
  player 2's side of the keyboard). They follow your rebinds and use UFO 50's own font and colours.
- The layout editor (the ☰ button, top right) lets you move, resize and hide controls and change
  their opacity.
- *Settings > Video Settings > Scale* works. 4x leaves room beside the picture for the controls,
  and FILL fills the screen.

**Other fixes**
- Quit in the main menu closes the app.
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

The room server matches players and relays packets when it has to. It holds only the open lobbies
(name, player count, addresses), and each one disappears when its lobby closes.

The netcode lives in okplay's [`oknative/`](https://github.com/Patrick-Working/okplay/tree/main/oknative)
folder, so other games can use it too. The build downloads it from there.

## Building

You need a Windows PC. The build runs in Git Bash.

### One-time setup

| what | where | notes |
|---|---|---|
| **UFO 50 1.9.5** | [Steam](https://store.steampowered.com/app/1147860/UFO_50/) | Install it normally. The build checks the version. |
| **Git for Windows** | <https://git-scm.com> | Gives you Git Bash and curl. |
| **Python 3** + Pillow | <https://www.python.org> | Tick "Add to PATH", then run `py -3 -m pip install pillow`. |
| **JDK 21** (or 17) | `winget install EclipseAdoptium.Temurin.21.JDK` | Any JDK 17 or 21 works. |
| **Android SDK** | [Android Studio](https://developer.android.com/studio) or the [command-line tools](https://developer.android.com/studio#command-line-tools-only) | Needs platform-tools, build-tools 35.0.0, platform android-35 and NDK 27.2.12479018. If the command-line tools are installed, `build.sh` installs the rest after you accept Google's licences. |
| **GameMaker** | <https://gamemaker.io> (free account) | Install it and sign in once. Make sure runtime **2024.14.1.253** is installed with the Android module (*File > Preferences > Runtime Feeds*). |

The build uses your GameMaker account to build the game's Android runner, the same thing the
GameMaker IDE does when you export a game for Android.

Then check that everything is found:

```bash
bash build.sh --check
```

If it can't find something, set the path in `local.env` (copy `local.env.example`).

### Build

```bash
bash build.sh
```

What it does:

1. **Downloads** these, each checked against a fixed checksum:
   - [UndertaleModTool CLI](https://github.com/UnderminersTeam/UndertaleModTool) 0.9.2.0
   - [UFO 50 Mod Loader](https://github.com/phil-macrocheira/UFO-50-MOD-LOADER) 2.3.0
   - [PFO 50](https://gamebanana.com/mods/648717) 0.10.0 and
     [MultiMadnessMod](https://gamebanana.com/mods/605308) 1.30, from GameBanana
   - the [PFO 50 source](https://github.com/Blaizer/PFO-50)
   - [ENet](https://github.com/lsalzman/enet) 1.3.18
   - okplay's netcode
2. **Opens the UFO 50 Mod Loader** with both mods in place. Click **Install**, wait until it's
   done, and close it. It makes a modded copy of the game and leaves your Steam installation alone.
   You only do this on the first build.
3. **Builds PFO 50's netcode for Android** (`libPFO.so`) with the NDK, from the PFO 50 source plus
   this repository's Android port (`pfo/`).
4. **Builds the GameMaker Android runner** with GameMaker's Igor. The runner project
   (`android/gm-project`) holds the touch overlay, the players panel and the online service.
5. **Patches the modded game for Android** (`scripts/android_patch.csx`: hot seat, pad bindings,
   the lobby menu, Quit). It also takes the overlay's font and button icons from your copy of the
   game.
6. **Packs and signs** `work/ufo50-online.apk`, using a key made for you on the first build.

The first build takes 10-20 minutes. Later builds reuse what they can.

### Install

With the phone connected over USB and USB debugging on:

```bash
bash build.sh --install
```

Or copy `work/ufo50-online.apk` to the phone and open it. You may need to allow your file manager
to install apps. If you get "package appears to be invalid", the copy was cut short: copy the file
again and check it is the same size on both ends.

## Playing

- **First start**: go to *Settings > Video Settings > Scale*. 4x leaves room for the controls, and
  FILL fills the screen.
- **Move the controls**: ☰ (top right) opens the layout editor. Drag a control to move it, and
  pinch it or drag its handle to resize it. The 4-way / 8-way d-pad switch is there too. DONE saves.
- **Gamepads**: connect them, then tap your player chip to open the players panel and give each
  pad a player.
- **Hot seat**: tap the player chip and turn HOT SEAT on, then pass the phone around.
- **Online**: choose *Online* from the main menu.
  - HOST PUBLIC to be listed, HOST PRIVATE to get a code, JOIN BY CODE to enter one.
  - Everyone needs this same build, with the same game version and the same mods.
- **Names**: players panel > NAME. Change it while you're not in a lobby.

## Troubleshooting

- **The lobby list doesn't show my friend's game.** Give the search a few seconds; internet rooms
  can take a moment on mobile data. Check that you both have the same build. A private lobby only
  shows up with its code.
- **"Timed out attempting to join lobby".** Try again once. If it keeps happening, the host can
  turn on UPnP on their router. The relay should still connect you, just more slowly.
- **Disconnected after switching apps.** Allow the app's notification (Android 13 and later ask).
  Without it Android may freeze the game in the background.
- **Build errors.** Run `bash build.sh --check` first. Igor's log is `work/gm/igor.log`.

**Testing switches** (for developers), as files in
`/sdcard/Android/data/com.unofficial.ufo50/files/`:
- An empty file named `forcerelay` makes that phone join through the relay only.
- An `okplay.txt` file holding a URL points the game at another room server. okplay's
  `rdv_standin.py` runs one locally.

## What's in this repository

| path | what |
|---|---|
| `build.sh` | the whole build, from the checks to the APK |
| `android/gm-project/` | the GameMaker runner project: the PFO extension with the touch overlay (`PFO_Android.java`), the online service, and the app icon (original art) |
| `pfo/` | the Android port of PFO 50: a patch to its `dllmain.cpp`, plus the Android layer in `android/` (Steam lobbies and networking over ENet, the bridge to the GameMaker runner, the hot seat and pad builtins) |
| `scripts/` | the game patch (an UndertaleModTool script), the font and icon export, the native build, and APK packing |

Nothing from the game is stored here: no game files, art or music. The only game code is the short
snippets the patch script searches for in order to patch them.

## Credits

- **UFO 50** by [Mossmouth](https://50games.fun). Please buy it.
- **[PFO 50](https://github.com/Blaizer/PFO-50)** by Blaizer
  ([GameBanana](https://gamebanana.com/mods/648717)), MIT licence: the online multiplayer all of
  this is built on.
- **[MultiMadnessMod](https://gamebanana.com/mods/605308)** by Jonesey13: 4+ players in many games.
- **[UFO50AndroidUnofficial](https://github.com/Skyline969/UFO50AndroidUnofficial)** by Skyline969:
  the original Android build this repository is forked from.
- **[UFO 50 Mod Loader](https://github.com/phil-macrocheira/UFO-50-MOD-LOADER)** by Phil
  Macrocheira, MIT licence: applies the mods.
- **[UndertaleModTool](https://github.com/UnderminersTeam/UndertaleModTool)** by the Underminers
  team, GPL-3.0: reads and patches the game data.
- **[ENet](https://github.com/lsalzman/enet)** by Lee Salzman, MIT licence: the network transport.
- **[okplay](https://github.com/Patrick-Working/okplay)** and the [oldkid.net](https://oldkid.net)
  room server: the lobby list, hole punching, UPnP and the relay.
- **GameMaker** by YoYo Games: the Android runner, built with your own GameMaker account.
- Public STUN servers from Cloudflare and Google.

## Licence

The code in this repository is MIT licensed (see `LICENSE`). The tools and mods it downloads keep
their own licences. UFO 50 is © Mossmouth.
