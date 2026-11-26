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

It means changing a program this repository does not build, so
[win7/patch_win7.ps1](../win7/patch_win7.ps1) does it to a folder the Windows build has just made:

```powershell
powershell -ExecutionPolicy Bypass -File win7\patch_win7.ps1 -GameDir "work\windows\UFO 50 Online"
```

It checks the program first (build stamp `0x6970f195`, image size `0xa72000` — the same two numbers
the snapshots check — and refuses anything else unless `-Force`), then:

1. rewrites `KERNEL32!SetProcessInformation` to `GetProcessId` (why that is safe: below);
2. rewrites `MFPlat!MFCreateDXGIDeviceManager`, delay-loaded and so only in the way at start-up,
   to `MFTGetInfo` — YellowAfterlife's fix, which costs the game `video_` playback and nothing
   else;
3. puts the `PathCch*` stand-in next to `ufo50.exe` (`api-ms-win-core-path-l1-1-0.dll`, the
   Wine-based
   [nalexandru/api-ms-win-core-path-HACK](https://github.com/nalexandru/api-ms-win-core-path-HACK),
   checked against two pinned checksums and downloaded into `work\dl` like the build's other
   downloads).

`-Check` reports without changing anything, running it twice is safe (it says "already"), and the
folder must not be playing. The names are found by walking the import and delay-import tables, not
by searching the file for the text — `MFCreateDXGIDeviceManager` also appears three times in the
program's own log messages.

The Windows build runs this itself (the end of section 4w in `build.ps1`, see
[win7/README.md](../win7/README.md)); running it by hand is for a folder made before that.

A finished folder is checked as well: `win7\check_win7.ps1 -GameDir <folder>` reads the import
tables of both `ufo50.exe` and `PFO.dll` and fails if either still wants something Windows 8 has, so
a future game version or a kit built another way can't quietly lose Windows 7. The build runs it
right after the fixups and says so if it fails. `api-ms-win-core-path-l1-1-0.dll` passes while the
stand-in is in the folder — that is the finished state.

Both edits are harmless on Windows 8 and later, so this is the same folder for 7 and 10/11: the
loader answers `api-ms-*` names from the API set schema and never looks at the copy in the folder,
`MFTGetInfo` only costs video playback, and the power hint becomes the no-op described next.

Tried on this machine (Windows 10 build 19045, x86_64): the patched program and the untouched one were
started with the same `data.win`, each with a private `LOCALAPPDATA` so the Steam game's saves were
never in reach. Both were still running eight seconds later, both with the window title `UFO 50`,
both answering, both having written `ufo50\config.json` — the import edits cost the game nothing on
10/11.

**VxKex** (the community "newer Windows APIs on 7" shim) is the alternative if you would rather not
change `ufo50.exe` at all: it implements the Windows 8 functions themselves.

### Why `GetProcessId` is a safe stand-in

The program calls `SetProcessInformation` once, and the only reference to that import anywhere in
it is that call (RVA `0x38e76a`; the scan covers `call`, `jmp` and `lea`/`mov` of the import slot in
every section, so it is called, never taken by address):

```
14038e753: call  qword ptr [rip + ...]   ; GetCurrentProcess()
14038e759: mov   r9d, 0xc                ; 12 bytes of state
14038e75f: lea   r8, [rbp + 8]           ; { Version = 1, ControlMask = 4, StateMask = 0 }
14038e763: mov   rcx, rax                ; the process
14038e766: lea   edx, [r9 - 8]           ; ProcessPowerThrottling = 4
14038e76a: call  qword ptr [rip + ...]   ; SetProcessInformation
14038e770: test  eax, eax
14038e772: jne   14038e78c               ; it worked: nothing to say
14038e774: ...                            ; "!!! Couldn't set process power settings\n"
```

That is `SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, 12)` with
`ControlMask = 0x4` — `PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION` — and `StateMask = 0`: "do
not throttle this process's timer resolution", set right after the main thread is raised to
`THREAD_PRIORITY_HIGHEST`, as the start of a game would.

The result is only tested for zero: a failure writes that one line to the log and the program
carries on. So a stand-in that returns a number without touching the process is enough, and
`GetProcessId` is the closest one: it takes the same first argument (a process handle) and answers
with the id, which is not zero for the current process, so the program takes the success path and
never looks at the state it passed. The arguments it does not read — the class, the pointer, the
size — sit in registers a one-argument function ignores on x64. Windows 7 has no timer-resolution
throttling to opt out of (that arrived with the later power management), so the hint dropped is one
Windows 7 would not act on anyway. Had it failed instead, the game would only have logged it.

**Snapshots**: the runner bridge decides whether snapshots work by the program's build stamp,
image size and the 15 code ranges listed above. Import-table edits touch none of those, and the
patched program was checked against them here: stamp, image size and all 15 ranges still match. So
the log should say `snapshots: on (this is the runner they were worked out for, 15 places
checked)`. If it says `snapshots: off, the runner's code at ...` instead, loads and rewinds replay
the session as [WINDOWS.md](WINDOWS.md) describes.

## Trying it

```bash
bash build.sh --windows --kit     # work/kit/ufo50-online-windows-v0.4.1.dll (this build)
bash build.sh --windows           # ...and the game folder, work/windows/UFO 50 Online
powershell -ExecutionPolicy Bypass -File win7\patch_win7.ps1 -GameDir "work/windows/UFO 50 Online"
```

The last line is the Windows 7 part (see above); the build runs it by itself now, so it is only
needed on a folder made before that, or after a build that said the fixups couldn't go in (it warns
and carries on, leaving a folder for 10/11). `-NoWin7` (`bash build.sh --windows --no-win7`) leaves
them out on purpose. Only the game in the folder being patched has to be closed: the Steam copy can
be running.

`build.bat -Windows -WinDll <file>` uses a `PFO.dll` of your own. The **quick build** is 7-ready as
well: the Windows kit in `kit/` is this same DLL (`ufo50-online-windows-v0.4.1.dll`, 2011648 bytes,
pinned by commit and checked against its checksum), so nothing here needs the full build. The older
`kit/ufo50-online-windows-v0.4.0.dll` stays for the v0.4.0 tag's own pin — a build never picks it
by accident, since `build.ps1` looks for the exact pinned name and hash.

## On the Windows 7 bench

A Windows 7 SP1 x64 machine, the folder copied over (the same one as on 10/11), and this order.

### 1. The machine

- **KB2670838** (the Direct3D 11 platform update) and **KB2999226** (the Universal CRT that
  `PFO.dll` links against). Quick check: `C:\Windows\System32\api-ms-win-crt-runtime-l1-1-0.dll`
  exists and `dxdiag` says Direct3D 11.
- A working Direct3D 11 driver: `dxdiag` shows no errors. A 2012-era driver that never installed
  properly will not draw whatever is patched.

### 2. The folder, before starting the game

```powershell
powershell -ExecutionPolicy Bypass -File win7\check_win7.ps1 -GameDir "C:\UFO 50 Online"
```

It has to end with `Windows 7 check: OK`. If it doesn't, it names the file and the import, and the
folder is one for 10/11 only — that is exactly the case the check exists for. The folder needs no
admin rights: saves, settings and the log live in `%LOCALAPPDATA%`.

### 3. Start it, then read the log

Double-click `ufo50.exe` (the first host or join makes Windows Firewall ask about it: allow it on
private networks). `%LOCALAPPDATA%\ufo50_online\pfo_online.log` is written from the first moments;
each line is `MM-DD hh:mm:ss.mmm L tag: text`, where L is `I`, `W` or `E`. What to look for, in
order:

| line | what it means |
|---|---|
| `I yoyo: PFO: extension initialised (Windows, our netcode), interface N bytes` | `PFO.dll` loaded and took over the game's networking: the C runtime and every import are answered. Without it the problem is not the folder — a `0xc0000135` in the event log means a missing DLL (KB2999226). |
| `I PFOSTATE: snapshots: on (this is the runner they were worked out for, 15 places checked)` | the patched `ufo50.exe` is still the program the snapshots know: loads and rewinds restore instead of replaying. |
| `W PFOSTATE: snapshots: off, this isn't the runner they were worked out for (built 6970f195, a72000 bytes in memory)` | the program is another build: everything works, but loads replay the session instead of restoring it. |
| `W PFOSTATE: snapshots: off, the runner's code at ... isn't what they were worked out for` | the same, caught by a code hash — a different game version. |
| `I yoyo: PFO LAN: joined via 192.168.…` | a match connection went through, over the local network. |
| `I yoyo: PFO LAN: game connection to player XXXX: connected` | the two sides are talking: netplay works on 7. |
| `W yoyo: PFO LAN: ... another build (theirs ..., ours ...)` | the other device is another build (another version, other mods or other patches), not a Windows 7 problem: the Windows 7 fixups change only `ufo50.exe`, which isn't part of the identity. |
| `W yoyo: PFO LAN: no direct answer yet - trying the relay too` | the direct connection didn't get through (firewall, NAT) and it fell back to the relay: normal over the internet. |
| `E PFOCRASH: fault ... at ..., ... (thread N)` and `called from: ...` | the runner crashed; those lines carry the address and the frames a report needs. |

### 4. Three things to play, not just watch

1. **Solo:** into a game and back to the menu, **F1** for the online menu, **F5**/**F9** for quick
   save and load. A load that takes about a second means the snapshots are on; one that visibly
   replays the session and takes seconds means they are off (the lines above say why). Both play;
   only the first is fast.
2. **A match with a Windows 10/11 machine:** host on one, join on the other, play a game to the end
   or quit it. This is what proves the netcode the Android and Windows versions share is the same
   one on 7.
3. **A rewind in a match:** the same snapshots under the netcode, where a wrong answer desyncs
   instead of being slow.

### When to try DXVK

`ufo50.exe` asks DXGI for `IDXGIFactory2` first and has a plain path under it. The program's own
messages:

```
Using IDXGIFactory2::CreateSwapChainForHwnd (modern API for windowed mode)
IDXGIFactory2 not available, falling back to legacy CreateSwapChain API
pDXGIFactory->CreateSwapChain (legacy fallback)
```

Windows 7 has DXGI 1.1, so **the fallback lines are the ones to expect**, not a problem, and
`IDXGIFactory5 not available (Windows 10 1803+ required for tearing support)` is normal too: no
variable refresh rate on 7.

Reach for DXVK only if one of these happens:

- the window stays black or white for more than ~15 seconds while the game is clearly running (it
  answers, the log moves on);
- the program reports `CreateSwapChainForHwnd failed with HRESULT 0x…` **and** the legacy fallback
  then draws nothing either;
- `Device lost - reason: 0x…` repeats, or the game dies right after `DirectX11: Using hardware
  device`.

Then: **DXVK 1.10.3**, its `x64\d3d11.dll` and `x64\dxgi.dll` beside `ufo50.exe`. 1.10.4 and later
want Vulkan 1.3 while Windows 7's drivers stop at 1.2 (people running 2024.11 GameMaker games on 7
report 1.10.3 as the last that works). The driver has to offer Vulkan on 7 at all — NVIDIA's last
Windows 7 driver, 475.14, does, and VulkanRT 1.3.290.0 is the last runtime installer for 7. DXVK
writes `d3d11.log` and `dxgi.log` next to the program, which is how you know it is in use. The two
DLLs do nothing on 10/11, but keep them in a copy of the folder rather than the one you ship.

### What to write down

Which of the lines above appeared (above all `extension initialised`, `snapshots: on`, `joined via`,
`connected`), whether the legacy swap-chain fallback appeared, whether DXVK was needed, and how a
rewind in a match felt. That is what turns "not checked yet" into results.

None of this has been run on a 7 machine: these are the steps, not results.
