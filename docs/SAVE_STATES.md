# How save states work

This is the technical side of save states, rewind, online saves and rejoining: what happens, why
it's built this way, and where the code is. For how to use them, see the [README](../README.md).

The code is in three places:

| file | what |
|---|---|
| `pfo/android/savestate.inc` | the native side: saving, loading, snapshots, rewind, online commands, rejoining |
| `scripts/pfo_snapshot.gml` | the game side: copying and restoring the game's own state (added to the game by `scripts/android_patch.csx`) |
| `android/gm-project/extensions/PFO/AndroidSource/Java/PFO_Android.java` | the overlay: the save list, vote, rejoin and confirm boxes, banners |

## The foundation: a session that records itself

PFO 50 runs online matches in lockstep: every phone simulates the whole game from the same inputs,
so all copies stay identical, and only inputs travel over the network. It records each match as a
**replay**: every player's input changes, the "reliable messages" (the random seed and the save
files the match started from) and a checksum every 255 frames.

Single player uses the same machinery: **START SOLO PLAY** starts a PFO session with one player. The
game plays exactly as usual, but everything is recorded. That recording is what makes save states
possible.

## A save

A save (`savestates/slot<N>.pfostate`) holds:

- a header: the frame it was taken at, PFO's checksum for that frame, the replay's size;
- the replay from the session's start up to that frame;
- the list of "touch frames" (below).

Next to it, `slot<N>.json` holds what the save list shows (game, players, time into the game) and
`slot<N>.png` its picture.

## Two ways to load

**1. Replay (always works).** Start the session over and play the recorded inputs at high speed
(drawing switched off, the game speed raised) until the saved frame, then carry on live. The
replay's own checksums are compared along the way, and the final one with the save's. Speed on
BlueStacks: about 550-1,100 frames a second, so a few seconds per minute of play.

**2. Snapshot (about a second).** When a save is made, the game's whole state is also copied in
memory: every global variable, every instance with its built-in and own variables, the data
structures (lists, maps, grids...), layers, tilemaps and cameras (`pfo_snapshot.gml`), plus PFO's
session, GameMaker's random generator, and the runner's own bookkeeping of instances (below).
Loading puts all of that back. Snapshots live in memory only, so they're lost when the app closes;
after that, loads replay.

A snapshot load is **checked before it's trusted**: for the 120 frames after the save, the
checksum PFO computes for each frame was noted. The load puts the snapshot back, plays those 120
frames at high speed comparing every checksum, then puts the snapshot back once more and hands
over. Any difference, and the load replays instead. In the final test sweep all 50 games passed
this check loading in the same room, from the library and from inside another game.

## What it took to make snapshots exact

GameMaker keeps a lot of state that GML can't see, and a restore has to put that back too, or the
game drifts from what a replay would do. The native side reads and writes the runner's own
structures (`gmi::` in `savestate.inc`), with offsets for GameMaker runtime 2024.14.1.253:

- **Which instances are switched off** (deactivated), and the runner's active and inactive lists.
  Reading the game switches everything on, so the exact on/off state is noted first and put back
  after.
- **The order of the instance lists.** Events run in list order; creating or re-activating an
  instance puts it at the end. The lists are relinked into the snapshot's order.
- **Creation counters**, so remade instances run their first events when the original did.
- **The animation pass** that PFO's own update runs inside: the runner remembers which instance
  comes next, so the restore lines that up.
- **Pending `instance_change`** and **collision trees** (rebuilt after every restore).

Some lessons that apply to every game:

- **Draw events must run during fast replays** (only showing the picture is skipped): UFO 50's
  audio code plays sounds from Draw.
- **Leaving a game before a session restart** (muting, unloading its data, its own cleanup), or
  objects from it survive into the replay.
- **A capture changes the game a little.** Reading every instance means switching them all on and
  back. The frames where that happened are saved as "touch frames", and replays do the same at the
  same frames.
- **Loading into another room**: the room is entered with the snapshot's globals already set, its
  data structures and persistent objects made first, and sprites the game made while running
  (Mini & Max, Camouflage) remade from a registry.
- **Cameras**: some games (Waldorf's Journey) set what the camera follows while running; the
  snapshot saves the follow target, borders, speed and angle.

## Rewind

The save list's **REWIND 15 S / 30 S / 60 S** goes back that far in the current session:

1. The session's recording is cut at the target frame (`CutReplay`).
2. The newest snapshot at or before that frame is picked, if it belongs to the current timeline
   (its recorded prefix matches the session's byte for byte; after a load or an earlier rewind,
   older snapshots may come from an abandoned branch). Snapshots come from saves, auto saves and
   rewind keyframes.
3. That snapshot is put back and the cut recording replays from there to the target frame. With no
   usable snapshot, the whole cut recording replays from the session's start.
4. Play carries on from the target frame, recording on from there.

**Keyframes** are snapshots taken just for rewinding. A capture freezes the game for about a
quarter of a second, so they're only taken when that can't be felt: while the game is paused, and
two seconds into a game that has just started (at most one every ten seconds; four are kept).

**Estimates**: the time a rewind or a load will take is estimated from the frames to replay and the
replay speed measured on this device by its previous loads. Anything estimated at 5 seconds or more
asks first.

## Online saves

Online, saving and loading must happen **on every phone on the same frame**: a capture changes the
game slightly, and a load changes it completely. So they travel as **commands in the game's input**,
the same way PFO 50 sends its own commands. Bits 16-31 of a seated player's input hold a command and
its parameter:

| command | number | parameter |
|---|---|---|
| SAVE | 240 | a counter (255: an auto save) |
| PROPOSE LOAD | 241 | the match's save number |
| ACCEPT | 242 | the save number |
| DECLINE | 243 | the save number |
| CANNOT (this phone lacks the save) | 244 | the save number |
| PROPOSE LOAD (another match's save) | 245 | the offer's tag |

PFO 50's own commands use 1-30. Commands are recorded in the replay like any input, so a replay
does exactly what the live match did.

**Saving**: on a SAVE command every phone saves at the start of the next step. Each keeps the save
in its own list, tagged with a match id (a hash of the players' ids) and the match's save number.
An auto save goes into the AUTO slot; if several players have auto save on, only the first SAVE per
pause counts.

**Loading goes forward, not back.** PFO's lockstep can't rewind: message frame numbers,
acknowledgements and input buffers only move forward. So once everyone accepts, every phone puts
the saved game state back **on the current frame** (the snapshot, without PFO's session) and the
match carries on; the frame counter keeps counting. The 120-frame self-check is skipped online
(it would need to run the network backwards); PFO's per-frame checksum comparison between phones
catches a bad restore instead.

**Saves from another match** can't be put back forward (they're from a different timeline). The
proposer sends the save (state, details, picture) to everyone over the transport's control channel
first; each phone keeps it as a save of its own; when everyone accepts, every phone restarts the
match from it: replays it to its frame and reconnects. Only saves made with the same number of
players qualify.

## Drops and rejoining

**A drop**: when a player's connection goes, PFO 50 used to end the match as soon as it ran out of
their buffered inputs. Now everyone waits up to 90 seconds; the phone with the higher player number
redials the lower one every 2 seconds (the lower one still accepts). The transport keeps message
numbers counting across reconnections, and a reconnection rolls acknowledgements back to the other
side's last input, so whatever was lost is sent again. Play resumes where it stopped.

While frozen, the game thread is inside PFO's input wait (it can't return early: the game would run
the same frame twice). The overlay runs on Android's UI thread and never waits on the game
thread's network lock; it reads published copies of what it shows.

**A rejoin** (the app was closed or crashed): while in a match, each phone keeps
`files/rejoin.txt`: the players' ids and addresses, its own player number, its own id, and the relay
room and mailbox key if the match goes through the relay. A relaunched app finds it (if it's under
about 100 seconds old) and offers REJOIN YOUR MATCH?. Rejoining:

1. takes back its old id, so the others recognise it;
2. asks a player still in the match for the match so far: their replay up to the frame they're
   frozen at (transport control messages, not recorded in any replay);
3. replays it at high speed, like a load;
4. reconnects, and tells the others "caught up to frame R"; they send their inputs from R again and
   lockstep resumes.

With three or more players, the ones left may have received different amounts of the lost player's
last inputs; while someone is lost, they share those inputs among themselves so they all wait at
the same frame. (Tested with two players only.)

## A crash fixed along the way

GameMaker's audio library logs a message for every sound it decodes ("Decoding bgm15_shop..."),
from its decoding thread. Its default logger labels the message with the script the **game thread**
is running, read with no locking. When the game thread is busy, as during a load replaying at high
speed while a game's music decodes, that read occasionally lands mid-change and the app crashes
(a null pointer in `YYWarning`). It showed up as about one load in forty failing, and now and then
a crash when opening a game. The audio library logs through one function pointer
(`yyal::log::_log`); the app now points it at a logger that writes to Android's log (tag `yyal`)
and reads nothing of the game's.

## Limits

- Snapshots are in memory: after the app restarts, loads replay (slower for long sessions).
- Rewind is single player only.
- Online loads need everyone with a seat to accept; players without a seat can't save or vote.
- The runner offsets are for GameMaker runtime 2024.14.1.253, which the build pins.
- Tested on BlueStacks (Android 9, x86_64). See [TESTING.md](TESTING.md) for what was and wasn't
  tested.
