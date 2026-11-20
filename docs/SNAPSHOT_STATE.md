# What a snapshot has to put back

A save state, an online load and a fast rejoin all work the same way: the game's whole state is
captured (`scripts/pfo_snapshot.gml`, plus the runner's own bookkeeping in
`pfo/android/savestate.inc`, `gmi::`) and put back later, in the same process or, for a fast
rejoin, in a fresh one on another device. Two devices then play on in lockstep, so the state put
back has to be the same in every detail the games read, or they drift apart (a desync).

This page lists every kind of state the GameMaker runner holds, whether a snapshot carries it, and
which games use it. The game list comes from a scan of all 13,882 code entries of UFO 50's
`data.win` (every call and built-in variable, grouped by kind). It is how problems are found
before a test runs into them: a kind that games use and a snapshot doesn't carry is a bug waiting.

`tests/tracediff.py` checks the result: with tracing on (`trace`, `tracerejoin`; see TESTING.md)
two devices write the whole state every frame, and the differ names the first item that differs.
`tests/win/rejoin_sweep.py` does that for a fast rejoin into each game.

## The inventory

Game numbers are UFO 50's internal ids (library order differs: see `tests/win/sweep.py` ID).

| State | Games that use it | Carried | Notes |
| --- | --- | --- | --- |
| Instances: variables, built-ins, alarms | all | yes | made again as event-less shells, then turned into their objects |
| Instance ids | 43 games hold ids in variables; 2 compare them by order in game logic (10, 35), 2 in drawing (07, 47) | yes, exact (2026-11-19) | made again under the ids they had (`pfo_runner_maxid`); new ones come after all others, as on the other device |
| Reuse vs. remake | all | fixed 2026-11-19 | every reuse is decided before anything is made: a new instance took an id a later record still had (Barbuta's 38 cliffs after a fast rejoin) |
| Instance order (the runner's lists) | 48 (`instance_place`, `instance_find`, `with`) | yes (native) | lists, order, creation times put back |
| Collision tree shape | 48 | yes (native) | rebuilt from the lists at every snapshot and restore |
| Switched-off instances | 8 (07, 10, 29, 33, 37, 41, 45, 50) | yes (native) | and the runner's cached list of them marked stale (fixed 2026-11-19: on a fresh device `instance_activate_*` found none, so game 50's map (library 12) never came back on) |
| Refs to things gone (`noone`, a dead instance, a freed list) | most (`noone`: 11 games in the sweep) | yes (2026-11-19) | they came back as plain numbers |
| Instances destroyed this step | all | yes | not reused (`pfo_snap_alive`), counted out of `instance_count` (`instancefix`) |
| The game's save files (save1.ufo, account.ufo) | all (the library reads them when it comes back) | yes (fixed 2026-11-19) | read and written through GameMaker's own functions: a plain file open looked in the wrong folder, so a fast rejoin sent none and the library differed after leaving a game |
| Globals | all | yes | except this device's screen settings (`pfo_snap_device_global`) |
| Arrays, structs, methods | all; struct-heavy: 45 | yes | methods bound to instances follow them |
| ds_list / ds_map / ds_grid | 29 / 4 / 8 games | yes | contents copied, made again where missing, under the numbers they had (2026-11-19: a game's stale ref to a freed list named a live one on one device only) |
| ds_stack / ds_queue | 1 (04) / 4 (07, 10, 35, 45) | yes | |
| Paths made at run time, path following | 45 (guards), 50 (units) | yes (2026-11-19) | path points, each instance's place on its path; `path_start` goes through `pfo_path_start` (what a relative path is measured from) |
| Motion-planning grids | 45, 50 | yes (2026-11-19) | blocked cells; `mp_grid_create` goes through a wrapper (a grid's size can't be read back) |
| Sprites made at run time | 04, 41 | yes | `sprite_duplicate` and its mask go through wrappers |
| Layers, GM1 tiles, backgrounds | 12 games (tiles), 07, 38 (backgrounds) | yes | |
| Cameras and views | 49 | yes | |
| Random generator | all (through the games' own scripts) | yes (native) | the generator's state and index |
| `current_time` | 9 | PFO's | PFO makes it a function of the frame; part of its checksum |
| Wall clock (`current_second`) | 07 (play-time counter) | no | each device's own clock: the counter can differ by a second. Display only |
| Time sources | 06 (a one-frame visibility check), 45 (attract mode) | no | GameMaker can't list them. Only a snapshot taken on the frame one is made would miss it; 06's only changes what's drawn |
| Pixels read back from the screen | 41 (Mini & Max's shrink) | indirectly | the logic reads the picture. After a fast rejoin the picture differs for about ten frames (surfaces start empty): a shrink in those frames could differ |
| Audio state read by logic | 35 (`audio_is_playing` decides whether to play a sound) | no | only sound comes of it |
| Keyboard/gamepad read directly | developer debug hooks only | n/a | the games' input goes through `scrGetInput` (PFO's inputs) |
| Surfaces, buffers, fonts made at run time | shared code, 23, 41 | no (drawing only) | made again by the games when missing |
| Particles, sequences, physics, timelines, anim curves, weak refs | none | n/a | |

## What's still open

- Mini & Max (41) reads the screen back in its logic: a fast rejoin's first frames draw from empty
  surfaces. Not seen to matter; the only difference the sweep found there is a variable its Draw
  event sets.
- The wall clock in game 07 counts play time per device.
- Time sources are invisible to GML: a snapshot on the frame one is made loses it.
