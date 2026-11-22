# Windows 7

What stands between UFO 50 (with this fork's netcode) and Windows 7, and what has been done about
it so far. **None of this has been run on a Windows 7 machine yet**: everything below comes from
the files themselves — their import tables and code — and from the things people who run GameMaker
games on Windows 7 report. It is here so that the next step (a real Windows 7 SP1 PC) has a short
list to check instead of a wall.

Two separate things have to work: the game's own program, `ufo50.exe` (GameMaker's runner, which
this port does not build), and `PFO.dll` (this fork's netcode, which it does).

## What `ufo50.exe` asks Windows for

Read with `llvm-readobj --coff-imports` from UFO 50 1.9.5's runner (`ufo50.exe` 7910400 bytes,
SHA-256 `e65a9c82d19fdcc89aae2723e614bb1107ff9c809f56893f32a0ae4995493a23`). It is the exact
program the snapshots were worked out for: its build stamp (`0x6970f195`), image size (`0xa72000`)
and all 15 code ranges in [pfo/win/runner_bridge_win.inc](../pfo/win/runner_bridge_win.inc) match.

| what it imports | the oldest Windows that has it |
|---|---|
| `api-ms-win-core-path-l1-1-0.dll` → `PathCchCombine` | Windows 8 (API set) |
| `KERNEL32.dll` → `SetProcessInformation` | Windows 8 |
| `MFPlat.DLL` → `MFCreateDXGIDeviceManager` (delay-load) | Windows 8 (video playback) |
| `d3d11.dll` → `D3D11CreateDevice` | Vista; on 7 with the platform update (KB2670838) |
| everything else: `KERNEL32`, `USER32`, `GDI32`, `WINMM`, `WS2_32`, `WINHTTP`, `IPHLPAPI`, `dwmapi`, `AVRT`, `PROPSYS`, `dbghelp`, ... | Windows 7 or older |

The first two are ordinary imports, and Windows resolves those before it runs a single line of the
program: a missing one is not something the game can catch, the loader stops with
"api-ms-win-core-path-l1-1-0.dll is missing" (which is the message [WINDOWS.md](WINDOWS.md)
records) or "The procedure entry point SetProcessInformation could not be located in the dynamic
link library KERNEL32.dll". `MFCreateDXGIDeviceManager` is delay-loaded, so it only stops the game
when the runner first sets up video playback: "The procedure entry point
MFCreateDXGIDeviceManager could not be located in the dynamic link library MFPlat.dll".

**The 2024.11 advice is not enough for this runner.** GameMaker dropped Windows 7 in 2024.11 and
the usual fix — YellowAfterlife's patcher, which rewrites that one MFPlat name to `MFTGetInfo`, a
function Windows 7's `MFPlat.dll` does have and which fails gracefully — was worked out when the
MFPlat call was the only thing in the way. This runner (2024.14) also has the two hard imports
above, so it stops earlier and with a different message.

### What it does not need

There is no DXGI import at all. The only Direct3D call is `D3D11CreateDevice`, and the program's
own strings show it treating the later DXGI features as optional:

```
IDXGIFactory5 not available (Windows 10 1803+ required for tearing support)
DXGI_FEATURE_PRESENT_ALLOW_TEARING is not supported - frame rate may be capped to monitor refresh rate
```

so nothing in the import table asks for DXGI 1.2 or a Direct3D 11.1 interface. Windows 7 with
SP1 and KB2670838 has Direct3D 11.0 and DXGI 1.1.

What is left is how the runner *presents* frames once it can start. Flip-model swap chains
(`DXGI_SWAP_EFFECT_FLIP_*`) are Windows 8 and later, and the reports of GameMaker 2024.11 games
running on 7 needed DXVK 1.10.3 (`dxgi.dll` + `d3d11.dll`, the last release that works with
Vulkan 1.2) to get past it. Which of the two paths the runner takes on 7 is the thing to watch
for on a real machine.

## PFO.dll: the Windows 10 calls are gone

The DLL built before this change imported `api-ms-win-core-memory-l1-1-6.dll` for
`VirtualAlloc2` and `MapViewOfFile3`, which is a Windows 10 1803 API set: on Windows 7 the DLL
did not load at all. It came from PFO 50's own `dllmain.cpp`, which puts its checksum ring buffer
together with `MEM_RESERVE_PLACEHOLDER` and `MEM_REPLACE_PLACEHOLDER` — the API that makes one
buffer appear twice, back to back, so that a write which runs off the end continues at the start
without a copy.

The same thing with calls Windows 7 has is now in
[pfo/win/compat_win.h](../pfo/win/compat_win.h) (`pfo_win::AllocateRingBuffer`), and PFO 50's
`#else` branch calls it when `PFO_WINDOWS_OURS` is defined
([pfo/pfo50-android.patch](../pfo/pfo50-android.patch)):

- reserve `2 × size`, then give the range back;
- create one page-file section of `size` bytes (`CreateFileMappingW`);
- map it twice with `MapViewOfFileEx` — once at the start of the range, once at `size` past it,
  so the two views are the same memory and exactly next to each other;
- `MapViewOfFileEx` takes the address as a hint and fails if something else took the range in
  between, so try again (up to 100 times; at startup it takes one).

`AllocateLargeBuffer` stays a plain `VirtualAlloc`. The build also passes
`-D_WIN32_WINNT=0x0601 -DWINVER=0x0601` ([scripts/build_pfo_windows.sh](../scripts/build_pfo_windows.sh)),
so that a Windows 8+ call is a compile error from now on rather than a DLL that will not load.

Checked so far:

- it builds, and exports the same 90 functions;
- the DLL's imports are now `KERNEL32` (nothing newer than Windows 7 in the list),
  `USER32`, `GDI32`, `WINMM`, `WS2_32`, `WINHTTP`, `IPHLPAPI` and the UCRT;
- a throwaway test program (not part of the repository) calls the new function and checks that
  both views are the same memory, that they are adjacent, and that a 16-byte write which starts
  8 bytes before the end lands at the start; it passes.

**Still needed on Windows 7: the Universal CRT.** llvm-mingw links against it, so the DLL imports
`api-ms-win-crt-*.dll`. Windows 7 has those with KB2999226 (the "Update for Universal C Runtime",
in Windows Update since 2015). Without it the DLL's own error is
"api-ms-win-crt-runtime-l1-1-0.dll is missing"; the alternative to the update is to copy the UCRT
DLLs next to `ufo50.exe`, which Microsoft supports.

## Getting the game itself to start on Windows 7

Not done here — it means changing a program this repository does not build. Two ways, both from
outside this project:

- **VxKex** (the community "newer Windows APIs on 7" shim) is the one-step route: it implements
  the Windows 8+ functions, including the two hard imports above, without touching `ufo50.exe`.
- **Three narrow patches to `ufo50.exe`**, if you would rather not run a shim:
  1. put the `PathCch*` stand-in DLL (`api-ms-win-core-path-l1-1-0.dll`, the Wine-based
     [nalexandru/api-ms-win-core-path-HACK](https://github.com/nalexandru/api-ms-win-core-path-HACK))
     next to `ufo50.exe`;
  2. rewrite the imported name `SetProcessInformation` in the import table to a Windows 7
     `KERNEL32` function that does nothing harmful — `GetProcessId` takes the same first
     argument and only returns a number, so the call is a no-op that reports success;
  3. rewrite `MFCreateDXGIDeviceManager` to `MFTGetInfo` (YellowAfterlife's patcher, or the same
     edit by hand), which costs the game its `video_` playback and nothing else.

The names sit in the import table, NUL-terminated, so a replacement must be no longer than the
original and be followed by a zero byte.

**Snapshots**: the runner bridge decides whether snapshots work by the program's build stamp,
image size and the 15 code ranges listed above. Import-table and string edits touch none of those,
so snapshots should stay on — the log says `snapshots: on (this is the runner they were worked out
for, 15 places checked)`. That has not been tried with patched imports; if it says `snapshots: off,
the runner's code at ...` instead, loads and rewinds replay the session as
[WINDOWS.md](WINDOWS.md) describes.

## Trying it

```bash
bash build.sh --windows --kit     # work/kit/ufo50-online-windows-v0.4.1.dll (this build)
bash build.sh --windows           # ...and the game folder, work/windows/UFO 50 Online
```

`build.bat -Windows -WinDll <file>` uses a `PFO.dll` of your own. The file in `kit/` is still the
one built before this change, and `build.ps1` pins its checksum: it was left alone.

On the Windows 7 PC, in order:

1. `ufo50.exe` starts (or says which import it is missing — see above);
2. `%LOCALAPPDATA%\ufo50_online\pfo_online.log` has a `PFOSTATE snapshots: ...` line and the
   netcode's own start-up lines;
3. the game draws (the presentation question above), then the menu opens with **F1** and the
   game's own lobby menu hosts and joins as on Windows 10.

Not checked at all: whether any of this runs, how it performs, and the two graphics paths. A
Windows 7 SP1 machine with KB2670838 and KB2999226 is what the next round needs.
