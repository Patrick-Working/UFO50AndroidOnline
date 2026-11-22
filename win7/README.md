# Windows 7 fixups (for review)

What makes one `UFO 50 Online` folder run on Windows 7 **and** Windows 10/11, instead of two builds.
Everything here is new and self-contained: nothing in the main build calls it yet, so it can be
reviewed on its own and then wired in with the few lines under [Wiring it in](#wiring-it-in).

Background and the evidence: [docs/WINDOWS7.md](../docs/WINDOWS7.md).

## What it changes, and why one folder is enough

`ufo50.exe` (GameMaker's runner, UFO 50 1.9.5's) has two imports that Windows 8 introduced. Windows
resolves ordinary imports before the program runs, so on 7 the game stops at the first one and
never gets a chance to say anything; the third one is delay-loaded and stops it at start-up
instead. `patch_win7.ps1` rewrites the two names and puts a `PathCch` stand-in in the folder:

| in `ufo50.exe` | becomes | on Windows 7 | on Windows 8 and later |
|---|---|---|---|
| `KERNEL32!SetProcessInformation` | `GetProcessId` | no-op instead of "entry point not found" | the same no-op: a power/memory hint the game works without |
| `MFPlat!MFCreateDXGIDeviceManager` (delay-load) | `MFTGetInfo` | fails cleanly, as GameMaker expects | the same clean failure: video playback, which UFO 50 never uses (`data.win` has no `video_*` function in it) |
| `api-ms-win-core-path-l1-1-0!PathCchCombine` | left alone; a copy of the name is put in the folder | the copy is the only implementation there is | **ignored**: the loader answers `api-ms-*` names from the API set schema (`kernelbase.dll`) and never looks in the folder |

The one call to `SetProcessInformation` was read in the disassembly —
`SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, 12)`, its result only
tested for zero, so `GetProcessId` is enough: see
[docs/WINDOWS7.md](../docs/WINDOWS7.md#why-getprocessid-is-a-safe-stand-in).

The names are found by walking the import and delay-import tables, not by searching the file for the
text: `MFCreateDXGIDeviceManager` also appears three times in the program's own log messages.

## Running it

```powershell
powershell -ExecutionPolicy Bypass -File win7\patch_win7.ps1 -GameDir "work\windows\UFO 50 Online"
```

`-Check` reports without changing anything. It expects UFO 50 1.9.5's runner (build stamp
`0x6970f195`, image size `0xa72000` — the same two numbers the snapshots check) and refuses
anything else unless `-Force`. Running it twice is safe: it says "already" and changes nothing.
The folder must not be playing at the time.

The `PathCch` stand-in is [nalexandru/api-ms-win-core-path-HACK](https://github.com/nalexandru/api-ms-win-core-path-HACK)
0.3.1 (Wine's implementation, LGPL-2.1): its zip is downloaded into `work\dl`, checked against a
fixed checksum, and the x64 DLL inside it against a second one. It is not in this repository. On
Windows 8 and later the file is inert (see the table), so it does no harm being there.

## Wired into the build

`build.ps1` runs it at the end of section 4w, once the game data, the art and `PFO.dll` are in
place, so every Windows build comes out 7-ready:

```powershell
    # Windows 7 fixups: two import names and the PathCch stand-in, so the same folder also starts on 7
    & (Join-Path $Root "win7\patch_win7.ps1") -GameDir $win -CacheDir $Dl
    if ($LASTEXITCODE -ne 0) { Die "the Windows 7 fixups didn't go in (see above)" }
```

`$Dl` is the folder `Fetch` already keeps downloads in, so the shim's zip is cached with the rest.
The script prints its own `ok` lines in the same shape as the build's, and exits non-zero on
anything unexpected (a different `ufo50.exe`, a locked file, a checksum mismatch); the build then
stops with that message rather than shipping a folder that only starts on 10/11. `build.ps1` passes
neither `-Check` nor `-Force`: they are this script's own, for running it by hand.

So a different UFO 50 version stops the Windows build until the script is given the new numbers or
run with `-Force` — the stamp and image size it checks are the ones the snapshots check, so it is
not a formality. A folder made by a build from before this one can be fixed with the same command
by hand.

## What it does not do

- It does not make Windows 7's own prerequisites go away: KB2670838 (Direct3D 11) and KB2999226
  (the Universal CRT that `PFO.dll` links against) still have to be on the machine.
- It does not touch `data.win`, `PFO.dll` or the save folder, so the Windows 7 and Windows 10/11
  players are the same build to the netcode (`c_BuildWire` is 4 either way) and share lobbies.
- If the runner ever needs a flip-model swap chain (Windows 8+), Windows 7 still needs DXVK's
  `dxgi.dll` + `d3d11.dll` (1.10.3) in the same folder. Whether it does is the open question in
  `docs/WINDOWS7.md`; this script neither helps nor hurts there.
- Snapshots: the runner is recognised by its build stamp, image size and 15 code ranges, none of
  which these two name edits or the extra DLL touch — so they stay on. The log line to check is
  `PFOSTATE snapshots: on (...)`.

Not tested on a Windows 7 machine yet; the import tables of the patched and unpatched programs were
compared instead (same stamp, same image size, all 15 snapshot ranges still match, imports read back
as `GetProcessId`/`MFTGetInfo`).
