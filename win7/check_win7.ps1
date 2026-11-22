#requires -Version 5.1
<#
.SYNOPSIS
  Says whether a finished "UFO 50 Online" folder can still load on Windows 7.

.DESCRIPTION
  Reads ufo50.exe's and PFO.dll's import and delay-import tables and looks for anything Windows 8
  brought: an api-ms-win-core-* API set (Windows 7 has none; the UCRT's api-ms-win-crt-* ones are
  fine), or one of the functions that arrived with Windows 8, the PathCch family among them.

  The one api-ms-win-core-* name that is fine is api-ms-win-core-path-l1-1-0.dll, provided the
  folder carries that file: it is the stand-in the fixups put next to ufo50.exe for Windows 7, and
  Windows 8 and later ignore it. Its PathCch* functions count as answered by it for the same reason.

  The point is that a future game version or a kit built another way can't quietly lose Windows 7:
  build.ps1 runs this after the fixups and says so if it fails.

  This is a guard, not an OS analyser: it knows the names in the lists below. A brand-new Windows 8+
  API would not be in them, so a failure here is trustworthy but a pass is only as good as the list.

.PARAMETER GameDir
  The folder with ufo50.exe (and PFO.dll from the kit).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File win7\check_win7.ps1 -GameDir "work\windows\UFO 50 Online"
#>
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$GameDir)
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "pe.ps1")

# Functions that are Windows 8 or later. The PathCch family is all of Windows 8's kernel32 path
# handling (what api-ms-win-core-path-l1-1-0 carries on 8+ and what the stand-in provides on 7).
$Win8Functions = @(
    'SetProcessInformation', 'GetProcessInformation', 'SetProcessMitigationPolicy', 'GetProcessMitigationPolicy',
    'GetSystemTimePreciseAsFileTime', 'GetSystemCpuSetInformation', 'SetThreadDescription', 'GetThreadDescription',
    'CreateFile2', 'CopyFile2', 'GetTempPath2W', 'PrefetchVirtualMemory', 'MapViewOfFile3', 'VirtualAlloc2',
    'MFCreateDXGIDeviceManager', 'CreateDXGIFactory2',
    'PathCchAddBackslash', 'PathCchAddBackslashEx', 'PathCchAddExtension', 'PathCchAppend', 'PathCchAppendEx',
    'PathCchCanonicalize', 'PathCchCanonicalizeEx', 'PathCchCombine', 'PathCchCombineEx', 'PathCchFindExtension',
    'PathCchIsRoot', 'PathCchRemoveBackslash', 'PathCchRemoveBackslashEx', 'PathCchRemoveExtension',
    'PathCchRemoveFileSpec', 'PathCchRenameExtension', 'PathCchSkipRoot', 'PathCchStripPrefix', 'PathCchStripToRoot',
    'PathIsUNCEx', 'PathAllocCanonicalize', 'PathAllocCombine'
)

$ShimName = "api-ms-win-core-path-l1-1-0.dll"
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
$hasShim = Test-Path -LiteralPath (Join-Path $GameDir $ShimName)
$problems = @()

foreach ($file in @("ufo50.exe", "PFO.dll")) {
    $path = Join-Path $GameDir $file
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "   NO   ${file}: not in the folder" -ForegroundColor Red
        $problems += "$file is missing"
        continue
    }
    $info = Load-Pe ([System.IO.File]::ReadAllBytes($path))
    if (-not $info) {
        Write-Host "   NO   ${file} $($script:PeError)" -ForegroundColor Red
        $problems += "$file $($script:PeError)"
        continue
    }
    $normal = @(Get-Imports $false)
    $delay = @(Get-Imports $true)
    $dlls = @($normal | ForEach-Object { $_.Dll }) + @($delay | ForEach-Object { $_.Dll })
    $found = 0
    foreach ($pair in @(@{ kind = "imported"; table = $normal }, @{ kind = "delay-loaded"; table = $delay })) {
        foreach ($entry in $pair.table) {
            $isShim = $entry.Dll -ieq $ShimName
            if ($entry.Dll -match '^api-ms-win-core-') {
                if ($isShim -and $hasShim) {
                    # answered by the stand-in in this folder: fine on 7, ignored on 8+
                } elseif ($isShim) {
                    Write-Host ("   NO   {0}: {1} {2}, and the stand-in isn't in the folder" -f $file, $pair.kind, $entry.Dll) -ForegroundColor Red
                    $problems += "$file needs $($entry.Dll) (the stand-in is missing)"
                    $found++
                } else {
                    Write-Host ("   NO   {0}: {1} {2} (an API set name Windows 7 does not have)" -f $file, $pair.kind, $entry.Dll) -ForegroundColor Red
                    $problems += "$file needs $($entry.Dll)"
                    $found++
                }
            }
            foreach ($fn in $entry.Names) {
                if ($Win8Functions -notcontains $fn.Name) { continue }
                if ($isShim -and $hasShim -and $fn.Name -like 'PathCch*') { continue }   # the stand-in has it
                Write-Host ("   NO   {0}: {1} {2}!{3} (Windows 8 or later)" -f $file, $pair.kind, $entry.Dll, $fn.Name) -ForegroundColor Red
                $problems += "$file needs $($entry.Dll)!$($fn.Name)"
                $found++
            }
        }
    }
    if ($found -eq 0) {
        Write-Host ("   ok   {0}: {1} DLLs ({2} delay-loaded), nothing newer than Windows 7" -f $file, $dlls.Count, $delay.Count)
    }
}

if ($problems.Count) {
    Write-Host ""
    Write-Host ("Windows 7 check: this folder does NOT start on Windows 7 ({0} import(s) newer than it)" -f $problems.Count) -ForegroundColor Red
    Write-Host "The same folder still runs on Windows 10 and 11."
    exit 1
}
Write-Host ""
Write-Host "Windows 7 check: OK (nothing in ufo50.exe or PFO.dll is newer than Windows 7)"
exit 0
