# UFO 50 Android Online - quick build.
# Double-click build.bat (or run: powershell -ExecutionPolicy Bypass -File build.ps1).
# Needs only Windows 10/11 and UFO 50 from Steam. Everything else is downloaded into work\.
#
#   -Install         also install the APK on a phone connected with USB debugging (needs adb)
#   -Runner <apk>    use this runner instead of the published runner kit (the full build passes its own)
#   -GamePath <dir>  your UFO 50 folder, if it isn't found by itself
#   -Windows         make the Windows version instead of the APK: a game folder (work\windows\UFO 50 Online)
#                    with the same online play, save states and rewind, which plays with the phones.
#                    It never talks to Steam and keeps its saves apart from the Steam game's.
#   -WinDll <dll>    with -Windows: use this PFO.dll instead of the published one (the full build passes its own)
#   -ManualLoader    open the UFO 50 Mod Loader's window and click Install yourself, instead of the
#                    build running it without a window
[CmdletBinding(PositionalBinding = $false)]
param([switch]$Install, [string]$Runner = "", [string]$GamePath = "", [string]$ModdedDir = "", [switch]$Windows, [string]$WinDll = "", [switch]$ManualLoader,
      [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # Invoke-WebRequest is far faster without the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Root = $PSScriptRoot
$Work = Join-Path $Root "work"
$Dl = Join-Path $Work "dl"

# ---- what this build is made of (pinned: the patches are written for exactly these) ----------
$Pins = @{
    GameVersion  = "1.9.5"
    GameSha      = "8c8fb143faedc8ab1f14d4ab288a4b2cd1f9a286e43de1b5e83ed8ede8266b29"   # data.win
    Utmt         = @("https://github.com/UnderminersTeam/UndertaleModTool/releases/download/0.9.2.0/UTMT_CLI_v0.9.2.0-Windows.zip", "e7573e45d107be34f81f955c6e4afc3c7c8f2628e5a6f307a871e3825b3dfb40", "utmt.zip")
    Loader       = @("https://github.com/phil-macrocheira/UFO-50-MOD-LOADER/releases/download/v2.3.0/UFO-50-Mod-Loader-win-Portable.zip", "4b9c5ade275916fe5cb4b05b2e516d1d31187aee2bd16336f1ddd1496f1126a4", "modloader.zip")
    Pfo          = @("https://gamebanana.com/dl/1778793", "fdf134dc776373bd6cb233c7dadf6cae1514174f48538152f72aa0016cfd08b1", "pfo50-0.10.0.zip")
    Mmm          = @("https://gamebanana.com/dl/1836289", "9e4f97abcbb2fd04b71ffdfed86feb53c7625590f946b1bc77aced3b38b4ab99", "multimadnessmod-1.30.zip")
    # the runner kit: GameMaker's Android runner + the touch overlay + libPFO.so, no game data
    # (made by the full build: bash build.sh --kit)
    Kit          = @("https://raw.githubusercontent.com/Patrick-Working/UFO50AndroidOnline/v0.4.0/kit/ufo50-online-runner-v0.4.0.apk", "a8d0906bb5fabbf89650902c4aa8407ce7f67f13a6df669a05e91063ccc1e4b2", "ufo50-online-runner-v0.4.0.apk")
    # the Windows kit: our PFO.dll alone (made by the full build: bash build.sh --windows --kit)
    WinKit       = @("https://raw.githubusercontent.com/Patrick-Working/UFO50AndroidOnline/v0.4.0/kit/ufo50-online-windows-v0.4.0.dll", "759106f74eb5195c7f84e9aeb7231a7d3c485f53252efda703f23c98a065f602", "ufo50-online-windows-v0.4.0.dll")
}

function Say($t) { Write-Host ""; Write-Host "== $t" -ForegroundColor Cyan }
function Ok($t) { Write-Host "   ok   $t" }
function Die($t) { Write-Host ""; Write-Host "ERROR: $t" -ForegroundColor Red; exit 1 }
function Sha($f) { (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash.ToLower() }

function Fetch($pin) {
    $url, $want, $name = $pin
    $out = Join-Path $Dl $name
    if ((Test-Path $out) -and ((Sha $out) -eq $want)) { return $out }
    Write-Host "   downloading $name"
    $part = "$out.part"
    for ($try = 1; $try -le 3; $try++) {
        try { Invoke-WebRequest -Uri $url -OutFile $part -UseBasicParsing -UserAgent "UFO50AndroidOnline-build"; break }
        catch { if ($try -eq 3) { Die "download failed: $url ($_)" } Start-Sleep -Seconds 2 }
    }
    if ((Sha $part) -ne $want) { Remove-Item $part; Die "$name doesn't match its checksum (the file on the server changed?)" }
    Move-Item -Force $part $out
    return $out
}

function Unzip($zip, $dir) {
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $dir)
}

function Utmt([string[]]$CliArgs, [hashtable]$Env = @{}) {
    # UndertaleModCli asks a few yes/no questions on the way: answer yes. (Its stderr lines must
    # not stop the script, so errors are judged by the exit code and the output instead.)
    $ErrorActionPreference = "Continue"
    foreach ($k in $Env.Keys) { Set-Item "env:$k" $Env[$k] }
    $yes = ("y`n" * 50)
    $out = $yes | & $script:UtmtExe @CliArgs 2>&1 | ForEach-Object { "$_" }
    foreach ($k in $Env.Keys) { Remove-Item "env:$k" }
    if ($LASTEXITCODE -ne 0 -or ($out -match "Script execution failed|CompilationErrorException")) {
        $out | Select-Object -Last 15 | ForEach-Object { Write-Host "   $_" }
        Die "UndertaleModTool step failed"
    }
    return $out
}

$Version = (Get-Content (Join-Path $Root "VERSION") -ErrorAction SilentlyContinue | Select-Object -First 1)
Write-Host "UFO 50 Android Online v$Version - quick build$(if ($Windows) { ' of the Windows version' })" -ForegroundColor Cyan
if ($Rest) { Die "didn't understand: $($Rest -join ' '). A folder name with spaces in it needs quotes around it, like -GamePath ""D:\My Games\UFO 50""" }
# PowerShell reads [ and ] in a path as a pattern, and half of what this script does would miss its files
foreach ($d in @($Root, $GamePath, $ModdedDir)) { if ($d -match '[\[\]]') { Die "the folder $d has [ or ] in its name, which this build can't work with. Move it to a folder without them and run this again." } }
if ($Windows -and $Install) { Die "-Install is for the Android build (the Windows build is a folder: work\windows)" }
if ($Windows -and $Runner) { Die "-Runner is for the Android build (with -Windows, a PFO.dll of your own goes in -WinDll)" }
if ($WinDll -and -not $Windows) { Die "-WinDll goes with -Windows" }

# ---- 1. the game ----------------------------------------------------------------------------
Say "Finding UFO 50"
if (-not $GamePath) {
    $libs = @()
    try { $libs += (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction Stop).SteamPath } catch {}
    $libs += "C:\Program Files (x86)\Steam"
    foreach ($s in @($libs)) {
        $vdf = Join-Path $s "steamapps\libraryfolders.vdf"
        if (Test-Path $vdf) { $libs += (Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' -AllMatches).Matches | ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' } }
    }
    foreach ($l in $libs) { $p = Join-Path $l "steamapps\common\UFO 50"; if (Test-Path (Join-Path $p "data.win")) { $GamePath = $p; break } }
}
if (-not $GamePath -or -not (Test-Path (Join-Path $GamePath "data.win"))) { Die "UFO 50 not found. Install it from Steam, or run: build.bat -GamePath ""D:\path\to\UFO 50""" }
if ((Sha (Join-Path $GamePath "data.win")) -ne $Pins.GameSha) { Die "$GamePath has a different version of UFO 50. This build is made for $($Pins.GameVersion)." }
Ok "UFO 50 $($Pins.GameVersion) ($GamePath)"

# ---- 2. downloads ---------------------------------------------------------------------------
Say "Downloading tools and mods"
New-Item -ItemType Directory -Force $Dl | Out-Null
$utmtZip = Fetch $Pins.Utmt
$script:UtmtExe = Join-Path $Work "utmt\UndertaleModCli.exe"
if (-not (Test-Path $script:UtmtExe)) { Unzip $utmtZip (Join-Path $Work "utmt") }
$loaderZip = Fetch $Pins.Loader
$pfoZip = Fetch $Pins.Pfo
$mmmZip = Fetch $Pins.Mmm
$localKit = Join-Path $Root ("kit\" + $Pins.Kit[2])   # the repository zip carries the kit
$localWinKit = Join-Path $Root ("kit\" + $Pins.WinKit[2])
if ($Windows) {
    if ($WinDll) { $winKit = (Resolve-Path $WinDll).Path; Ok "PFO.dll: $winKit" }
    elseif ((Test-Path $localWinKit) -and ((Sha $localWinKit) -eq $Pins.WinKit[1])) { $winKit = $localWinKit; Ok "Windows kit v$Version" }
    else { $winKit = Fetch $Pins.WinKit; Ok "Windows kit v$Version (downloaded)" }
}
elseif ($Runner) { $kit = (Resolve-Path $Runner).Path; Ok "runner: $kit" }
elseif ((Test-Path $localKit) -and ((Sha $localKit) -eq $Pins.Kit[1])) { $kit = $localKit; Ok "runner kit v$Version" }
else { $kit = Fetch $Pins.Kit; Ok "runner kit v$Version (downloaded)" }
Ok "UndertaleModTool 0.9.2.0, UFO 50 Mod Loader 2.3.0, PFO 50 0.10.0, MultiMadnessMod 1.30"

# ---- 3. mods: the UFO 50 Mod Loader makes a modded copy of the game -------------------------
$loader = Join-Path $Work "modloader"
$modded = if ($ModdedDir) { $ModdedDir } else { Join-Path $loader "workspace\UFO 50 Modded Copy" }
# The Mod Loader copies PFO.dll in before it patches data.win, so a copy whose Install was stopped
# or failed has the DLL and the game's own data.win: the DLL alone doesn't say the mods are in.
function ModdedOk {
    $d = Join-Path $modded "data.win"
    (Test-Path (Join-Path $modded "PFO.dll")) -and (Test-Path $d) -and ((Sha $d) -ne $Pins.GameSha)
}

# The Mod Loader can install with no window (--headless), but 2.3.0 stops with an error before it
# starts: that mode never loads the loader's description of the game (Game.Load). A .NET startup
# hook, compiled here from the few lines below with the compiler Windows carries, makes that one
# call; the loader itself is run as downloaded. Returns $false if anything goes wrong, and the
# window opens instead.
$hookSource = @'
using System;
using System.Reflection;
internal class StartupHook
{
    public static void Initialize()
    {
        if (Array.IndexOf(Environment.GetCommandLineArgs(), "--headless") < 0) return;
        Assembly asm = Assembly.Load(new AssemblyName("UFO 50 Mod Loader"));
        asm.GetType("UFO_50_Mod_Loader.Models.Game", true).GetMethod("Load", BindingFlags.Public | BindingFlags.Static).Invoke(null, null);
    }
}
'@
function LoaderHeadless {
    $log = Join-Path $loader "headless.log"
    try {
        $ws = Join-Path $loader "workspace"
        # the loader patches its own copy of the game's files (its window makes this on the first start)
        $vanilla = Join-Path $ws "UFO 50 Vanilla Copy"
        $vd = Join-Path $vanilla "data.win"
        if (-not ((Test-Path $vd) -and ((Sha $vd) -eq $Pins.GameSha) -and (Test-Path (Join-Path $vanilla "ufo50.exe")))) {
            $list = (Get-Content -Raw (Join-Path $loader "current\Games\UFO 50\hashes.json") | ConvertFrom-Json).($Pins.GameVersion)
            if (-not $list) { throw "the Mod Loader has no file list for UFO 50 $($Pins.GameVersion)" }
            if (Test-Path $vanilla) { Remove-Item -Recurse -Force $vanilla }
            foreach ($n in $list.PSObject.Properties.Name) {
                $src = Join-Path $GamePath $n
                if (-not (Test-Path -LiteralPath $src)) { continue }
                $dst = Join-Path $vanilla $n
                New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
                Copy-Item -LiteralPath $src -Destination $dst
            }
            if ((Sha $vd) -ne $Pins.GameSha) { throw "copying the game's files failed" }
        }
        # OverwriteMode would make the loader write into the game's own folder: always off here
        [ordered]@{ Version = "2.3.0"; GamePath = $GamePath; FirstTimeRun = $false; OverwriteMode = $false
                    CopiedVanillaVersion = $Pins.GameVersion; EnabledMods = @("MultiMadnessMod", "PFO 50")
                    CheckForUpdatesAutomatically = $false } |
            ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $ws "settings.json")
        $hook = Join-Path $loader "headless_hook.dll"
        if (Test-Path $hook) { Remove-Item -Force $hook }   # made afresh each time: it takes a second
        Add-Type -TypeDefinition $hookSource -OutputAssembly $hook -OutputType Library
        $mods = Join-Path $loader "UFO 50 mods"
        $cli = "--headless --mod ""$mods\MultiMadnessMod"" --mod ""$mods\PFO 50"" --ignore-warnings"
        $env:DOTNET_STARTUP_HOOKS = $hook
        try {
            $p = Start-Process -FilePath (Join-Path $loader "current\UFO 50 Mod Loader.exe") -ArgumentList $cli `
                -WorkingDirectory (Join-Path $loader "current") -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput $log -RedirectStandardError "$log.err"
            $null = $p.Handle   # without this, ExitCode is empty afterwards
            if (-not $p.WaitForExit(20 * 60 * 1000)) { $p.Kill(); throw "the Mod Loader didn't finish in 20 minutes" }
            $code = $p.ExitCode
        }
        finally { Remove-Item env:DOTNET_STARTUP_HOOKS }
        if ($code -ne 0) { throw "the Mod Loader ended with code $code" }
        if (-not (ModdedOk)) { throw "the Mod Loader said it was done, but the mods aren't in the copy" }
        return $true
    }
    catch {
        Write-Host "   installing without the window didn't work: $_" -ForegroundColor Yellow
        foreach ($f in @($log, "$log.err")) { if (Test-Path $f) { Get-Content $f -Tail 5 | ForEach-Object { Write-Host "     $_" } } }
        return $false
    }
}
$notDone = "The mods aren't in the modded copy at $modded (its data.win is missing or still the game's own). In the Mod Loader, tick both mods, click Install, and close it only after it says it's done. Then run this again."
if (-not (ModdedOk)) {
    if ($ModdedDir) { Die $notDone }
    Say "Applying the mods with the UFO 50 Mod Loader"
    if (Test-Path $modded) { Remove-Item -Recurse -Force $modded }   # a copy left half done by an earlier run
    if (-not (Test-Path (Join-Path $loader "current\UFO 50 Mod Loader.exe"))) { Unzip $loaderZip $loader }
    $mods = Join-Path $loader "UFO 50 mods"
    New-Item -ItemType Directory -Force $mods, (Join-Path $loader "workspace") | Out-Null
    $tmp = Join-Path $Work "tmp_mods"
    Unzip $pfoZip $tmp
    if (Test-Path "$mods\PFO 50") { Remove-Item -Recurse -Force "$mods\PFO 50" }
    Move-Item (Get-ChildItem $tmp -Directory | Select-Object -First 1 | ForEach-Object { Join-Path $_.FullName "PFO 50" }) "$mods\PFO 50"
    Unzip $mmmZip $tmp
    if (Test-Path "$mods\MultiMadnessMod") { Remove-Item -Recurse -Force "$mods\MultiMadnessMod" }
    Move-Item "$tmp\MultiMadnessMod" "$mods\MultiMadnessMod"
    Remove-Item -Recurse -Force $tmp
    Write-Host "   installing PFO 50 and MultiMadnessMod into a copy of the game (a minute or two)"
    if ($ManualLoader -or -not (LoaderHeadless)) {
        if (Test-Path $modded) { Remove-Item -Recurse -Force $modded }
        $settings = Join-Path $loader "workspace\settings.json"
        if (-not (Test-Path $settings)) {
            @{ GamePath = $GamePath; EnabledMods = @("MultiMadnessMod", "PFO 50"); CheckForUpdatesAutomatically = $false } |
                ConvertTo-Json | Set-Content -Encoding UTF8 $settings
        }
        Write-Host ""
        Write-Host "   The UFO 50 Mod Loader is opening. In it:" -ForegroundColor Yellow
        Write-Host "     1. If it asks for the game folder, choose:  $GamePath"
        Write-Host "     2. Make sure ""PFO 50"" and ""MultiMadnessMod"" are ticked."
        Write-Host "     3. Click Install and wait until it says it's done. Then close the Mod Loader."
        Start-Process -FilePath (Join-Path $loader "current\UFO 50 Mod Loader.exe") -WorkingDirectory (Join-Path $loader "current") -Wait
        if (-not (ModdedOk)) { Die $notDone }
    }
}
Ok "modded copy: $modded"

# ---- 4w. the Windows version: the modded copy with our PFO.dll and a patched data.win --------
if ($Windows) {
    Say "Putting the Windows version together (a minute or two)"
    $steamFiles = @("Steamworks_x64.dll", "steam_api64.dll", "steam_api.dll", "steam_appid.txt")
    $win = Join-Path $Work "windows\UFO 50 Online"
    if (Get-Process -Name ufo50 -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($win, [StringComparison]::OrdinalIgnoreCase) }) { Die "the Windows version is running: close it first" }
    if (Test-Path $win) { Remove-Item -Recurse -Force $win }
    New-Item -ItemType Directory -Force $win | Out-Null
    # everything but the two files we replace and Steam's: the build has no Steam library, so it
    # can't reach Steam, your Steam saves or the Steam version's lobbies
    Get-ChildItem -LiteralPath $modded -Force | Where-Object { $steamFiles -notcontains $_.Name -and $_.Name -ne "data.win" -and $_.Name -ne "PFO.dll" } |
        ForEach-Object { Copy-Item -Recurse -LiteralPath $_.FullName -Destination $win }
    Copy-Item -LiteralPath $winKit -Destination (Join-Path $win "PFO.dll")
    $out = Utmt @("load", (Join-Path $modded "data.win"), "-s", (Join-Path $Root "scripts\windows_patch.csx"), "-o", (Join-Path $win "data.win"), "-f") @{ PFO_SNAPSHOT_GML = (Join-Path $Root "scripts\pfo_snapshot.gml") }
    $out | Where-Object { $_ -match "^(extension|save folder|lobby menu|screen|snapshots|runtime sprites|FAILED)" } | ForEach-Object { Write-Host "   $_" }
    $patched = ($out -match "^windows patch: OK") -and ($out -match "^save folder: game name ufo50 -> ufo50_online") -and (Test-Path (Join-Path $win "data.win"))
    $steamLeft = $steamFiles | Where-Object { Test-Path (Join-Path $win $_) }
    if (-not $patched -or $steamLeft) {
        Remove-Item -Force (Join-Path $win "data.win") -ErrorAction SilentlyContinue
        Die "the Windows version couldn't be put together (the game data patch didn't go in whole)"
    }
    Write-Host ""
    Write-Host "Done: $win" -ForegroundColor Green
    Write-Host "Start ufo50.exe in that folder (F1 opens the online menu). It contains your copy of the game: keep it to yourself."
    Write-Host "Its saves are in %LOCALAPPDATA%\ufo50_online. To bring your Steam saves along, copy save*.ufo and"
    Write-Host "account.ufo there from %LOCALAPPDATA%\ufo50 (copy, don't move: the Steam game keeps its own)."
    exit 0
}

# ---- 4. game data: Android patches, and the overlay's font and icons from your copy ---------
Say "Patching the game for Android (a few minutes)"
$game = Join-Path $Work "game"
if (Test-Path $game) { Remove-Item -Recurse -Force $game }
Copy-Item -Recurse $modded $game
$out = Utmt @("load", (Join-Path $modded "data.win"), "-s", (Join-Path $Root "scripts\android_patch.csx"), "-o", (Join-Path $game "data.win"), "-f") @{ PFO_SNAPSHOT_GML = (Join-Path $Root "scripts\pfo_snapshot.gml") }
$out | Where-Object { $_ -match "hot seat|pad bindings|overlay support|lobby menu|quit:|join timeout|Steam lobby|snapshots|runtime sprites" } | ForEach-Object { Write-Host "   $_" }
$out = Utmt @("load", (Join-Path $game "data.win"), "-s", (Join-Path $Root "scripts\export_overlay_art.csx")) @{ OUT_DIR = $game }
Ok "game data"

# ---- 5. the APK -----------------------------------------------------------------------------
Say "Packing and signing the APK"
$apk = Join-Path $Work "ufo50-online.apk"
$out = Utmt @("load", (Join-Path $game "data.win"), "-s", (Join-Path $Root "scripts\pack_apk.csx")) @{
    RUNNER_APK = $kit; GAME_DIR = $game; OUT_APK = $apk; KEY_FILE = (Join-Path $Work "keys\ufo50-online-key.pfx") }
$out | Where-Object { $_ -match "^APK" } | ForEach-Object { Write-Host "   $_" }
if (-not (Test-Path $apk) -or (Get-Item $apk).Length -lt 100MB) { Die "the APK wasn't made (see the lines above)" }
Write-Host ""
Write-Host "Done: $apk" -ForegroundColor Green
Write-Host "It contains your copy of the game: keep it to yourself."
Write-Host "Copy it to your phone and open it, or run: build.bat -Install"

if ($Install) {
    Say "Installing"
    $adb = (Get-Command adb -ErrorAction SilentlyContinue).Source
    if (-not $adb) { $adb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe" }
    if (-not (Test-Path $adb)) { Die "adb not found: copy the APK to the phone instead, or install Android platform-tools" }
    & $adb install -r $apk
    if ($LASTEXITCODE -ne 0) { Die "adb couldn't install the APK (is the phone connected with USB debugging on? An app of the same name from another build must be uninstalled first)" }
}
