# Stands in for build.ps1 when testing the builder window (builder.ps1 -BuildScript): takes the
# same switches and plays back a recorded build log instead of building.
#   $env:GUI_REPLAY_DIR   folder with android.txt, windows.txt, plain.txt (lines build.ps1 printed)
#   $env:GUI_REPLAY_FAIL  "android", "windows" or "plain": play <kind>_fail.txt instead and exit 1
#   $env:GUI_REPLAY_MS    delay per line (default 60 ms)
[CmdletBinding(PositionalBinding = $false)]
param([switch]$Install, [string]$GamePath = "", [switch]$Windows, [switch]$Vanilla, [switch]$NoWin7)
$kind = if ($Windows -and $Vanilla) { "plain" } elseif ($Windows) { "windows" } else { "android" }
$fail = $env:GUI_REPLAY_FAIL -eq $kind
$file = Join-Path $env:GUI_REPLAY_DIR ($kind + $(if ($fail) { "_fail" } else { "" }) + ".txt")
$ms = if ($env:GUI_REPLAY_MS) { [int]$env:GUI_REPLAY_MS } else { 60 }
foreach ($l in Get-Content -LiteralPath $file) {
    if ($Install -and $l -match "^Copy it to your phone") { Write-Host ""; Write-Host "== Installing"; Start-Sleep -Milliseconds 800; Write-Host "Success"; continue }
    Write-Host $l
    Start-Sleep -Milliseconds $(if ($l -match "^== ") { $ms * 12 } else { $ms })
}
if ($fail) { exit 1 }
exit 0
