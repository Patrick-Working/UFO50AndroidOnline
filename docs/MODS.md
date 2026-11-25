# Mods

The build puts a set of mods into your copy of the game with the
[UFO 50 Mod Loader](https://github.com/phil-macrocheira/UFO-50-MOD-LOADER), always the same files:
each is downloaded from its author's page and checked against a checksum. Nothing from the mods is
stored in this repository.

## In the build (v0.5.0)

| mod | version | by | what it does |
|---|---|---|---|
| [PFO 50](https://gamebanana.com/mods/648717) | 0.10.0 | Blaizer | online multiplayer: the base of this port (its DLL is replaced by ours) |
| [MultiMadnessMod](https://gamebanana.com/mods/605308) | 1.31 | Jonesey13 | 4+ players in many games (1.31: The Big Bell Race for 8, no CRT filter while the screen is split) |
| [Ultimate Pingolf Roster](https://gamebanana.com/mods/679074) | 1.0.0 | Bubba256 | 5 more golfers in Pingolf (12 in all, for 8-player games) |
| [Star Waspir 2P](https://gamebanana.com/mods/698092) | 0.9.0 | ToastGhost26 | Star Waspir for two players (an early playtest version) |
| [Hyper Contender+](https://gamebanana.com/mods/604987) | 1.6.10 | greenhead352 | 4-player Hyper Contender, new modes (terminal codes) and a new character |

The list is the `Mmm` and `Extra` lines of `build.ps1`'s `$Pins`. A change to any of them (a mod
added, removed or updated) makes the build install the mods afresh. Every device in a lobby needs the
same build: different game data can't play in step, so a new set of mods comes with a new
`c_BuildWire` (`pfo/android/fakesteam_impl.inc`), and the lobby refuses the other builds.

### Fixes the build makes to them

- **Star Waspir 2P 0.9.0**: in one-player mode (the default) the game stopped at once with
  "local variable badEgger1 not set before reading it": the mod sets two variables only with two
  players but reads them whenever no egg is touched. The build gives them `noone` to start with
  (`scripts/*_patch.csx`, "Fixed Star Waspir 2P's one-player crash"). Nothing is done if the mod
  ever fixes it itself.
- **MultiMadnessMod** (1.30 and 1.31): Ninpek's game over passes `points[0..7]` to its high-score
  script and crashed in a game for fewer than 8; only the slots that exist are passed.
- **MultiMadnessMod 1.31** changed the line our screen-reporting edit goes in front of (the CRT
  filter's): the edit accepts both versions.

### Split screen (MultiMadnessMod)

With four players apart (Onion Delivery, Rock On! Island) the screen splits into four, with three
more cameras and a drawing surface twice the size; the mod switches that on and off each step. A
load used to leave the views as they were when it crossed between one screen and split (its picture
check failed and it replayed instead): now the restore puts this device's views the way the
snapshot's flag says, with the mod's own scripts (`pfo_snap_split_match` in `scripts/pfo_snapshot.gml`).
`tests/win/splitscreen.py` checks it.

## Auditioning a mod

Before a mod goes in, on a test copy (a git worktree with its own `work\`):

1. **Read what it changes.** A Mod Loader mod is a folder: `code/` (whole code entries replaced),
   `config/code_patch/*.yaml` (find-and-replace patches), `config/new_object/`, textures, audio,
   sometimes `csx/` scripts the loader runs, and sometimes a native `dll/`.
   - A **native DLL** (DirectInput, Discord Rich Presence) exists on Windows only: Android has no
     build of it, and whatever it adds to the game's state would then differ between a PC and a
     phone in the same match. Those need their own plan.
   - **Overlaps**: list the code entries each mod replaces or patches; two mods patching one entry
     are merged by the loader in turn (check the result has both mods' changes), one replacing
     what another patches is a conflict. The loader's own count is in `work\modloader\headless.log`
     (`[conflicts] { blocking, warnings }`).
   - **What state it uses**: run the code scan (docs/SNAPSHOT_STATE.md) over the new code: anything
     snapshots don't carry (time sources, the wall clock, pixels read back, native calls) is a
     risk for save states and online play.
2. **Pin it** in `build.ps1` (URL, SHA-256, file name) and build the test copy. Our patches print
   `FAILED` for any edit whose anchor a mod moved: fix the anchor, never skip it.
3. **Test its games**: `SNAPWIRE=1 SOLO_PLAYERS=4 tests/win/sweep.py <games> out.txt library`
   (save states with four players on one device), `tests/win/rejoin_sweep.py <games>` (an online
   fast rejoin, the whole state compared frame by frame), and a game's own modes (players, options)
   by hand or with a test of its own when the mod adds them (`tests/win/splitscreen.py`). A game
   that stops with GameMaker's error box is a result: read the message (`noresult_*_state.txt`).
4. **Android**: the same build for the phone, and the sweep there (`tests/sweep4.py`).

Looked at, not in the build (2026-11-22): Bushido Ball+ 1.1 (game code and art, made to go with
MultiMadnessMod: a candidate), Audio Mod Support 1.1.0 (only useful with a mod that brings its own
audio), DirectInput 1.2 and Discord Rich Presence 1.2 (native Windows DLLs: see above).
