#requires -Version 5.1
<#
.SYNOPSIS
  Makes a "UFO 50 Online" folder from the Windows build run on Windows 7 as well as on Windows 10/11.

.DESCRIPTION
  Two functions in UFO 50's ufo50.exe are Windows 8 or later, and Windows resolves ordinary imports
  before it runs a line of the program, so on Windows 7 the game stops before it starts. This
  rewrites those two names in the import tables and puts the PathCch stand-in next to the program.
  Both changes are harmless on Windows 8 and later (see win7/README.md), so one folder serves both.

  It refuses a ufo50.exe other than UFO 50 1.9.5's (unless -Force), because the offsets it patches
  are only understood for that program.

.PARAMETER GameDir
  The folder with ufo50.exe (build.ps1 makes it at work\windows\UFO 50 Online).

.PARAMETER Check
  Only report what would be done; changes nothing.

.PARAMETER Force
  Patch a ufo50.exe that isn't the one this was written for.

.PARAMETER ShimZip
  Use this copy of the shim's zip instead of the cached/downloaded one.

.PARAMETER CacheDir
  Where the shim's zip is kept (default: work\dl next to this repository).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [switch]$Check,
    [switch]$Force,
    [string]$ShimZip = "",
    [string]$CacheDir = ""
)
$ErrorActionPreference = "Stop"

function Die($t) { Write-Host ""; Write-Host "ERROR: $t" -ForegroundColor Red; exit 1 }
function Ok($t) { Write-Host "   ok   $t" }
function Info($t) { Write-Host "   $t" }

# ---- what this is written for (pinned) -------------------------------------------------------
# UFO 50 1.9.5's ufo50.exe: the runner the snapshots in pfo/win/runner_bridge_win.inc were worked
# out for. The patcher refuses anything else unless -Force: the import names are found by walking
# the import tables, so another program would most likely just be missing them, but a build stamp
# and an image size are the same check the snapshots make, so it costs nothing to make it here too.
$Stamp = 0x6970F195
$ImageSize = 0xA72000

# PathCch for Windows 7. Wine's implementation, packed up by nalexandru/api-ms-win-core-path-HACK.
# Windows 7 has no api-ms-win-core-path-l1-1-0.dll at all; Windows 8 and later answer that name
# from the API set schema (kernelbase.dll) and never look at a copy in the program's folder, so the
# file sitting there is simply unused on 10 and 11.
$Shim = @{
    Url      = "https://github.com/nalexandru/api-ms-win-core-path-HACK/releases/download/0.3.1/api-ms-win-core-path-blender-0.3.1.zip"
    ZipSha   = "2cff5e3dc3b0a5e9241c1091230959cded50e3d2ff543308b68c6fba653a3bb6"
    DllSha   = "a1f02f8f2b90f89d0bfae554d2ebd61d07c7454eabbc53236738143180e030ce"
    DllInZip = "api-ms-win-core-path-blender/x64/api-ms-win-core-path-l1-1-0.dll"
    Name     = "api-ms-win-core-path-l1-1-0.dll"
    ZipName  = "api-ms-win-core-path-blender-0.3.1.zip"
}

# The two names, and what they become. Each replacement is shorter than what it replaces, so it goes
# over the old name in place: the zero byte after the old name ends the new one.
#   SetProcessInformation (Windows 8) is GameMaker asking for a power/memory hint. GetProcessId
#     takes a process handle as well and only answers with a number: a no-op that reports success.
#   MFCreateDXGIDeviceManager (Windows 8) is for video playback. MFTGetInfo exists in Windows 7's
#     MFPlat.dll; GameMaker calls the first with a zero there, so it fails cleanly, and UFO 50
#     doesn't use GameMaker's video_ functions at all (checked: none in its data.win).
$Patches = @(
    @{ Dll = "KERNEL32.dll"; Delay = $false; Old = "SetProcessInformation"; New = "GetProcessId" },
    @{ Dll = "MFPlat.DLL"; Delay = $true; Old = "MFCreateDXGIDeviceManager"; New = "MFTGetInfo" }
)

# ---- reading the program --------------------------------------------------------------------
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
$exe = Join-Path $GameDir "ufo50.exe"
if (-not (Test-Path -LiteralPath $exe)) { Die "no ufo50.exe in $GameDir" }

$script:D = $null
$script:Secs = @()

function U16([int]$at) { [System.BitConverter]::ToUInt16($script:D, $at) }
function U32([int]$at) { [System.BitConverter]::ToUInt32($script:D, $at) }
function U64([int]$at) { [System.BitConverter]::ToUInt64($script:D, $at) }

function Load-Pe([byte[]]$data) {
    $script:D = $data
    if ($data.Length -lt 0x100 -or $data[0] -ne 0x4D -or $data[1] -ne 0x5A) { Die "$exe is not a program (no MZ header)" }
    $pe = U32 0x3C
    if ($pe + 24 -ge $data.Length -or (U32 $pe) -ne 0x00004550) { Die "$exe is not a PE file" }
    if ((U16 ($pe + 4)) -ne 0x8664) { Die "$exe is not a 64-bit program" }
    $count = U16 ($pe + 6)
    $optSize = U16 ($pe + 20)
    $opt = $pe + 24
    if ((U16 $opt) -ne 0x20B) { Die "$exe is not a 64-bit program" }
    $script:Secs = @()
    for ($i = 0; $i -lt $count; $i++) {
        $o = $opt + $optSize + $i * 40
        $script:Secs += @{ Va = (U32 ($o + 12)); VSize = (U32 ($o + 8)); RawSize = (U32 ($o + 16)); Raw = (U32 ($o + 20)) }
    }
    $script:Dd = $opt + 112   # the data directories, after the 64-bit optional header
    return @{ Stamp = (U32 ($pe + 8)); ImageSize = (U32 ($opt + 56)) }
}

function RvaToOff([uint32]$rva) {
    foreach ($s in $script:Secs) {
        $span = [Math]::Max($s.VSize, $s.RawSize)
        if ($rva -ge $s.Va -and $rva -lt ($s.Va + $span) -and ($rva - $s.Va) -lt $s.RawSize) { return [int]($s.Raw + ($rva - $s.Va)) }
    }
    return -1
}

function CStr([int]$off) {
    $end = $off
    while ($end -lt $script:D.Length -and $script:D[$end] -ne 0) { $end++ }
    if ($end -eq $off) { return "" }
    return [System.Text.Encoding]::ASCII.GetString($script:D, $off, $end - $off)
}

# The import tables (delay = $false) and the delay-load ones ($true): one entry per DLL, each with
# the functions it wants and where their names are in the file.
function Get-Imports([bool]$delay) {
    $dir = 13 * 8
    $step = 32
    if (-not $delay) { $dir = 1 * 8; $step = 20 }
    $rva = U32 ($script:Dd + $dir)
    $entries = @()
    if ($rva -eq 0) { return $entries }
    $off = RvaToOff $rva
    if ($off -lt 0) { return $entries }
    while ($true) {
        $nameRva = U32 ($off + 12)
        $thunkRva = U32 $off
        $iatRva = U32 ($off + 16)
        if ($delay) { $nameRva = U32 ($off + 4); $thunkRva = U32 ($off + 16); $iatRva = U32 ($off + 12) }
        if ($nameRva -eq 0 -and $iatRva -eq 0) { break }
        $dll = CStr (RvaToOff $nameRva)
        if (-not $thunkRva) { $thunkRva = $iatRva }
        $names = @()
        $t = RvaToOff $thunkRva
        if ($t -ge 0) {
            while ($true) {
                $v = U64 $t
                if ($v -eq 0) { break }
                if (($v -band 0x8000000000000000) -eq 0) {
                    $at = RvaToOff ([uint32]($v -band 0x7FFFFFFF))
                    if ($at -lt 0) { break }
                    $names += @{ Name = (CStr ($at + 2)); Off = ($at + 2) }   # + 2: past the hint word
                }
                $t += 8
            }
        }
        $entries += @{ Dll = $dll; Names = $names }
        $off += $step
    }
    return $entries
}

# ---- 1. the program's two Windows 8 imports -------------------------------------------------
$info = Load-Pe ([System.IO.File]::ReadAllBytes($exe))
if ($info.Stamp -ne $Stamp -or $info.ImageSize -ne $ImageSize) {
    $what = "this ufo50.exe is not the one this was written for (built {0:x8}, {1:x} bytes in memory; wanted {2:x8}, {3:x})" -f $info.Stamp, $info.ImageSize, $Stamp, $ImageSize
    if (-not $Force) { Die "$what - use -Force to patch it anyway" }
    Write-Host "   $what (patching anyway: -Force)" -ForegroundColor Yellow
}
$imports = Get-Imports $false
$delayed = Get-Imports $true

$changed = 0
foreach ($p in $Patches) {
    $table = $imports
    if ($p.Delay) { $table = $delayed }
    $dll = $table | Where-Object { $_.Dll -ieq $p.Dll } | Select-Object -First 1
    if (-not $dll) { Die "$($p.Dll) is not imported by ${exe}: this patcher is for UFO 50 1.9.5" }
    $hit = $dll.Names | Where-Object { $_.Name -ceq $p.Old } | Select-Object -First 1
    if (-not $hit) {
        $done = $dll.Names | Where-Object { $_.Name -ceq $p.New } | Select-Object -First 1
        if ($done) { Ok "$($p.Dll): already $($p.New)"; continue }
        Die "$($p.Dll) imports neither $($p.Old) nor $($p.New): this patcher is for UFO 50 1.9.5"
    }
    if ($Check) { Info "would change $($p.Dll): $($p.Old) -> $($p.New)"; continue }
    $oldLen = [System.Text.Encoding]::ASCII.GetByteCount($p.Old)
    $newBytes = [System.Text.Encoding]::ASCII.GetBytes($p.New)
    if ($newBytes.Length -gt $oldLen) { Die "$($p.New) is longer than $($p.Old)" }
    for ($i = 0; $i -lt $oldLen; $i++) {
        $b = [byte]0
        if ($i -lt $newBytes.Length) { $b = $newBytes[$i] }
        $script:D[$hit.Off + $i] = $b
    }
    $changed++
    Ok "$($p.Dll): $($p.Old) -> $($p.New)"
}

if ($changed -gt 0 -and -not $Check) {
    if (Get-Process -Name ufo50 -ErrorAction SilentlyContinue) { Die "ufo50.exe is running: close the game first" }
    [System.IO.File]::WriteAllBytes($exe, $script:D)
    # read it back: what the file now says is what matters, not what was written
    $none = Load-Pe ([System.IO.File]::ReadAllBytes($exe))
    foreach ($p in $Patches) {
        $table = Get-Imports $false
        if ($p.Delay) { $table = Get-Imports $true }
        $names = ($table | Where-Object { $_.Dll -ieq $p.Dll }).Names.Name
        if ($names -contains $p.Old) { Die "$($p.Dll) still imports $($p.Old) after patching" }
        if (-not ($names -contains $p.New)) { Die "$($p.Dll) does not import $($p.New) after patching" }
    }
    if ($none.Stamp -ne $Stamp -or $none.ImageSize -ne $ImageSize) { Die "patching changed the program's build stamp or image size" }
    Ok "ufo50.exe: the two names are in place, build stamp and image size unchanged"
}

# ---- 2. the PathCch stand-in ----------------------------------------------------------------
$shimTarget = Join-Path $GameDir $Shim.Name
$haveShim = (Test-Path -LiteralPath $shimTarget) -and ((Get-FileHash -Algorithm SHA256 -LiteralPath $shimTarget).Hash.ToLower() -eq $Shim.DllSha)
if ($haveShim) {
    Ok "$($Shim.Name): already there"
} elseif ($Check) {
    Info "would put $($Shim.Name) in the folder"
} else {
    if (-not $CacheDir) { $CacheDir = Join-Path (Split-Path -Parent $PSScriptRoot) "work\dl" }
    if (-not (Test-Path -LiteralPath $CacheDir)) { New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null }
    $zip = $ShimZip
    if (-not $zip) { $zip = Join-Path $CacheDir $Shim.ZipName }
    if (-not (Test-Path -LiteralPath $zip) -or ((Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLower() -ne $Shim.ZipSha)) {
        Write-Host "   downloading $($Shim.ZipName)"
        $zip = Join-Path $CacheDir $Shim.ZipName
        $part = "$zip.part"
        for ($try = 1; $try -le 3; $try++) {
            try { Invoke-WebRequest -Uri $Shim.Url -OutFile $part -UseBasicParsing -UserAgent "UFO50AndroidOnline-build"; break }
            catch { if ($try -eq 3) { Die "download failed: $($Shim.Url) ($_)" } Start-Sleep -Seconds 2 }
        }
        Move-Item -Force $part $zip
    }
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLower() -ne $Shim.ZipSha) { Die "$($Shim.ZipName) doesn't match its checksum (the file on the server changed?)" }
    $tmp = Join-Path $CacheDir "shim-tmp"
    if (Test-Path -LiteralPath $tmp) { Remove-Item -Recurse -Force $tmp }
    Expand-Archive -LiteralPath $zip -DestinationPath $tmp -Force
    $dll = Join-Path $tmp ($Shim.DllInZip -replace "/", "\")
    if (-not (Test-Path -LiteralPath $dll)) { Die "$($Shim.ZipName) doesn't hold $($Shim.DllInZip)" }
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash.ToLower() -ne $Shim.DllSha) { Die "$($Shim.Name) from the zip doesn't match its checksum" }
    Copy-Item -LiteralPath $dll -Destination $shimTarget -Force
    Remove-Item -Recurse -Force $tmp
    Ok "$($Shim.Name) -> $GameDir"
}

if ($Check) { Write-Host ""; Write-Host "(only checked: nothing was changed)"; exit 0 }
Write-Host ""
Write-Host "Done: $GameDir starts on Windows 7, 10 and 11 alike." -ForegroundColor Green
Write-Host "Windows 7 also needs KB2670838 (Direct3D 11) and KB2999226 (the C runtime PFO.dll uses)."
# an explicit 0: build.ps1 calls this and checks $LASTEXITCODE, which a script that just returns
# does not set (it would still hold whatever the last native program left there)
exit 0
