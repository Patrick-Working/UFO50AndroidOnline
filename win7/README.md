# Windows 7 fixups (for review)

What makes one `UFO 50 Online` folder run on Windows 7 **and** Windows 10/11, instead of two builds.
Everything here is self-contained: `build.ps1` calls the two scripts at the end of section 4w, so
every Windows build comes out 7-ready, and both can be run by hand as well (see
[Wired into the build](#wired-into-the-build)). The steps for the machine itself, the log lines to
look for and when DXVK is worth trying are in
[docs/WINDOWS7.md](../docs/WINDOWS7.md#on-the-windows-7-bench).

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

## Checking a finished folder

`check_win7.ps1` is the guard against a future game version or a `PFO.dll` built another way
quietly losing Windows 7. It reads the import and delay-import tables of `ufo50.exe` and `PFO.dll`
and fails if either still wants something Windows 8 brought: an `api-ms-win-core-*` API set, or a
Windows 8 function such as `SetProcessInformation`, `VirtualAlloc2` or `MFCreateDXGIDeviceManager`
(the PathCch family is in the list too).

```powershell
powershell -ExecutionPolicy Bypass -File win7\check_win7.ps1 -GameDir "work\windows\UFO 50 Online"
```

`api-ms-win-core-path-l1-1-0.dll` is the one API set name that is fine, provided that file is in the
folder: that is the stand-in. So a finished folder — `ufo50.exe` importing `PathCchCombine` from that
name, the stand-in next to it — passes, while the same folder with the import gone (an unpatched
`ufo50.exe`) fails on the two names the fixups rewrite. Both scripts read the tables with the same
code, `pe.ps1`.

Exit status is 0 when the folder can load on Windows 7, 1 when it can't; the lines above the summary
name the file and the import. It is a guard rather than an OS analyser — it knows the names in its
lists, so a failure is trustworthy but a pass is only as good as the list.

A running game doesn't stop it: a loaded program or DLL can keep a plain read out, so it retries
with a shared handle and, if that fails too, says `PFO.dll can't be read (is the game running?
close it and run this again)` and exits 1 — no PowerShell error either way.

## Wired into the build

`build.ps1` runs it at the end of section 4w, once the game data, the art and `PFO.dll` are in
place, so every Windows build comes out 7-ready:

```powershell
    if ($NoWin7) {
        Write-Host "   Windows 7 fixups: skipped (-NoWin7)" -ForegroundColor Yellow
    } else {
        & (Join-Path $Root "win7\patch_win7.ps1") -GameDir $win -CacheDir $Dl
        if ($LASTEXITCODE -ne 0) {
            Write-Host "   Windows 7 fixups: didn't go in (see above) - this folder starts on Windows 10/11 only" -ForegroundColor Yellow
            Write-Host "   (fix it later with: powershell -ExecutionPolicy Bypass -File win7\patch_win7.ps1 -GameDir `"$win`")" -ForegroundColor Yellow
        }
        # and prove it: neither program in the finished folder may still want something Windows 8 has
        & (Join-Path $Root "win7\check_win7.ps1") -GameDir $win
        if ($LASTEXITCODE -ne 0) {
            Write-Host "   Windows 7 check failed: this folder starts on Windows 10/11 only (see above)" -ForegroundColor Yellow
        }
    }
```

`$Dl` is the folder `Fetch` already keeps downloads in, so the shim's zip is cached with the rest.
The script prints its own `ok` lines in the same shape as the build's, and exits non-zero on
anything unexpected (a different `ufo50.exe`, a checksum mismatch, the game running); the build then
says so and **carries on**, because the folder is a perfectly good Windows 10/11 one either way, and
prints the command that fixes it later. `-NoWin7` in `build.ps1` (`--no-win7` in `build.sh`) leaves
the step out from the start, for a folder meant for 10/11 only. `build.ps1` passes neither `-Check`
nor `-Force`: they are this script's own, for running it by hand.

A different UFO 50 version therefore makes the build warn and carry on until the script is given the
new numbers or run with `-Force` — the stamp and image size it checks are the ones the snapshots
check, so it is not a formality. A folder made by a build from before this one can be fixed with the
same command by hand.

**Which game may be running**: only the one in the folder being patched, compared by the process's
path, so the Steam copy (or another build of this one) can be open while you build. If Windows won't
say where a process is from, it is not counted either.

## What it does not do

- It does not make Windows 7's own prerequisites go away: KB2670838 (Direct3D 11) and KB2999226
  (the Universal CRT that `PFO.dll` links against) still have to be on the machine.
- It does not touch `data.win`, `PFO.dll` or the save folder, so the Windows 7 and Windows 10/11
  players are the same build to the netcode (the same `c_BuildWire` either way) and share lobbies.
- It does not decide the presentation question. The program asks for `IDXGIFactory2` first and has
a plain path under it (`IDXGIFactory2 not available, falling back to legacy CreateSwapChain API`),
which is what Windows 7's DXGI 1.1 should take — so DXVK's `dxgi.dll` + `d3d11.dll` (1.10.3) are
likely not needed, but only a 7 machine says so; this script neither helps nor hurts there. The
bench steps in [docs/WINDOWS7.md](../docs/WINDOWS7.md#when-to-try-dxvk) say when to reach for it.
- Snapshots: the runner is recognised by its build stamp, image size and 15 code ranges, none of
  which these two name edits or the extra DLL touch — so they stay on. The log line to check is
  `PFOSTATE snapshots: on (...)`.

Not tested on a Windows 7 machine yet; the import tables of the patched and unpatched programs were
compared instead (same stamp, same image size, all 15 snapshot ranges still match, imports read back
as `GetProcessId`/`MFTGetInfo`).
