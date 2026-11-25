# Says whether a finished "UFO 50 Online" folder can still load on Windows 7: reads ufo50.exe's and
# PFO.dll's import and delay-import tables and fails if either still wants something Windows 8
# brought. build.ps1 runs it after the fixups, so a future game version or a PFO.dll built another
# way can't quietly lose Windows 7.
#
# The one api-ms-win-core-* name that is fine is api-ms-win-core-path-l1-1-0.dll, provided that file
# is in the folder: it is the stand-in the fixups put there, and its PathCch functions are answered
# from it.
#
# It is a guard, not an OS analyser: it knows the names below, so a failure is trustworthy but a pass
# is only as good as that list.

# -NoPfo: a folder of the plain game (build.ps1 -Vanilla), which has no PFO.dll to check
param([Parameter(Mandatory = $true)][string]$GameDir, [switch]$NoPfo)
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "pe.ps1")

$ShimName = "api-ms-win-core-path-l1-1-0.dll"

# Windows 8's own names, the ones these two programs could want. The PathCch family is a prefix
# because it is all of Windows 8's path handling (19 functions, what the stand-in provides).
$Win8Exactly = @(
    'SetProcessInformation', 'GetProcessInformation', 'SetProcessMitigationPolicy', 'GetProcessMitigationPolicy',
    'GetSystemTimePreciseAsFileTime', 'GetSystemCpuSetInformation', 'SetThreadDescription', 'GetThreadDescription',
    'CreateFile2', 'CopyFile2', 'GetTempPath2W', 'PrefetchVirtualMemory',
    'VirtualAlloc2', 'MapViewOfFile3',
    'MFCreateDXGIDeviceManager', 'CreateDXGIFactory2'
)
$Win8Prefix = '^(PathCch|PathAlloc|PathIsUNC)'

if (-not (Test-Path -LiteralPath $GameDir)) {
    Write-Host "   NO   no such folder: $GameDir" -ForegroundColor Red
    exit 1
}
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
$hasShim = Test-Path -LiteralPath (Join-Path $GameDir $ShimName)
$bad = 0

# Reads a file, also while the game is running: a loaded program or DLL can keep ReadAllBytes out,
# and a second try with more sharing usually gets in. $null means it really could not be read.
function Read-Whole([string]$path) {
    try { return ,[System.IO.File]::ReadAllBytes($path) } catch { }
    try {
        $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            $bytes = New-Object byte[] $fs.Length
            [void]$fs.Read($bytes, 0, $bytes.Length)
            return ,$bytes
        } finally { $fs.Close() }
    } catch { return $null }
}

$files = @("ufo50.exe")
if (-not $NoPfo) { $files += "PFO.dll" }
foreach ($file in $files) {
    $path = Join-Path $GameDir $file
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "   NO   ${file} is not in the folder" -ForegroundColor Red
        $bad++
        continue
    }
    $bytes = Read-Whole $path
    if ($null -eq $bytes) {
        Write-Host "   NO   ${file} can't be read (is the game running? close it and run this again)" -ForegroundColor Red
        $bad++
        continue
    }
    if (-not (Load-Pe $bytes)) {
        Write-Host "   NO   ${file} $($script:PeError)" -ForegroundColor Red
        $bad++
        continue
    }
    $before = $bad
    $dlls = 0
    foreach ($delay in $false, $true) {
        $kind = "imports"
        if ($delay) { $kind = "delay-loads" }
        foreach ($entry in (Get-Imports $delay)) {
            if (-not $delay) { $dlls++ }
            $fromShim = ($entry.Dll -ieq $ShimName) -and $hasShim
            $fns = @()
            foreach ($fn in $entry.Names) {
                if ($fromShim -and $fn.Name -like 'PathCch*') { continue }   # the stand-in has it
                if ($Win8Exactly -contains $fn.Name -or $fn.Name -match $Win8Prefix) { $fns += $fn.Name }
            }
            $msg = ""
            if ($entry.Dll -match '^api-ms-win-core-' -and -not $fromShim) {
                if ($entry.Dll -ieq $ShimName) { $msg = "$($entry.Dll) (the stand-in isn't in the folder" }
                else { $msg = "$($entry.Dll) (an API set Windows 7 does not have" }
                if ($fns.Count) { $msg += "; wants " + ($fns -join ', ') }
                $msg += ")"
            } elseif ($fns.Count) {
                $msg = (($fns | ForEach-Object { "$($entry.Dll)!$_" }) -join ', ') + " (Windows 8 or later)"
            }
            if ($msg) {
                Write-Host ("   NO   {0} {1} {2}" -f $file, $kind, $msg) -ForegroundColor Red
                $bad++
            }
        }
    }
    if ($bad -eq $before) { Write-Host ("   ok   {0}: {1} DLLs, nothing newer than Windows 7" -f $file, $dlls) }
}

if ($bad) {
    Write-Host ""
    Write-Host "Windows 7 check: this folder does NOT start on Windows 7 (the imports above are why)" -ForegroundColor Red
    Write-Host "The same folder still runs on Windows 10 and 11."
    exit 1
}
Write-Host ""
Write-Host ("Windows 7 check: OK (nothing in " + ($files -join " or ") + " is newer than Windows 7)")
exit 0
