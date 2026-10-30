# UFO 50 Android Online - quick build.
# Double-click build.bat (or run: powershell -ExecutionPolicy Bypass -File build.ps1).
# Needs only Windows 10/11 and UFO 50 from Steam. Everything else is downloaded into work\.
#
#   -Install         also install the APK on a phone connected with USB debugging (needs adb)
#   -Runner <apk>    use this runner instead of the published runner kit (the full build passes its own)
#   -GamePath <dir>  your UFO 50 folder, if it isn't found by itself
param([switch]$Install, [string]$Runner = "", [string]$GamePath = "", [string]$ModdedDir = "")
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
    Kit          = @("https://raw.githubusercontent.com/Patrick-Working/UFO50AndroidOnline/v0.2/kit/ufo50-online-runner-v0.2.apk", "81930baaeb272b475f0c2559fc611f2ba35ed5187b2cf0446a60af42b9bd1d24", "ufo50-online-runner-v0.2.apk")
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
Write-Host "UFO 50 Android Online v$Version - quick build" -ForegroundColor Cyan

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
if ($Runner) { $kit = (Resolve-Path $Runner).Path; Ok "runner: $kit" }
elseif ((Test-Path $localKit) -and ((Sha $localKit) -eq $Pins.Kit[1])) { $kit = $localKit; Ok "runner kit v$Version" }
else { $kit = Fetch $Pins.Kit; Ok "runner kit v$Version (downloaded)" }
Ok "UndertaleModTool 0.9.2.0, UFO 50 Mod Loader 2.3.0, PFO 50 0.10.0, MultiMadnessMod 1.30"

# ---- 3. mods: the UFO 50 Mod Loader makes a modded copy of the game -------------------------
$loader = Join-Path $Work "modloader"
$modded = if ($ModdedDir) { $ModdedDir } else { Join-Path $loader "workspace\UFO 50 Modded Copy" }
if (-not (Test-Path (Join-Path $modded "PFO.dll"))) {
    Say "Applying the mods with the UFO 50 Mod Loader"
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
    if (-not (Test-Path (Join-Path $modded "PFO.dll"))) { Die "no modded copy at $modded (did Install finish with both mods ticked?)" }
}
Ok "modded copy: $modded"

# ---- 4. game data: Android patches, and the overlay's font and icons from your copy ---------
Say "Patching the game for Android (a few minutes)"
$game = Join-Path $Work "game"
if (Test-Path $game) { Remove-Item -Recurse -Force $game }
Copy-Item -Recurse $modded $game
$out = Utmt @("load", (Join-Path $modded "data.win"), "-s", (Join-Path $Root "scripts\android_patch.csx"), "-o", (Join-Path $game "data.win"), "-f")
$out | Where-Object { $_ -match "hot seat|pad bindings|overlay support|lobby menu|quit:|join timeout|Steam lobby" } | ForEach-Object { Write-Host "   $_" }
$out = Utmt @("load", (Join-Path $game "data.win"), "-s", (Join-Path $Root "scripts\export_overlay_art.csx")) @{ OUT_DIR = $game }
Ok "game data"

# ---- 5. the APK -----------------------------------------------------------------------------
Say "Packing and signing the APK"
$apk = Join-Path $Work "ufo50-online.apk"
$out = Utmt @("load", (Join-Path $game "data.win"), "-s", (Join-Path $Root "scripts\pack_apk.csx")) @{
    RUNNER_APK = $kit; GAME_DIR = $game; OUT_APK = $apk; KEY_FILE = (Join-Path $Work "keys\ufo50-online-key.pfx") }
$out | Where-Object { $_ -match "^APK" } | ForEach-Object { Write-Host "   $_" }
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
}
