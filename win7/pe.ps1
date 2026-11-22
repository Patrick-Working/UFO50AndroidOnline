# A minimal read-only reader for a 64-bit PE file's import and delay-import tables, shared by
# patch_win7.ps1 (which rewrites two names in them) and check_win7.ps1 (which looks for anything
# newer than Windows 7). Dot-source it, then Load-Pe, then Get-Imports.
#
# Load-Pe sets the bytes everything else reads and returns the two numbers that identify a program
# (build stamp, image size), or $null with $script:PeError saying why not.

$script:D = $null
$script:Secs = @()
$script:Dd = 0
$script:PeError = $null

function U16([int]$at) { [System.BitConverter]::ToUInt16($script:D, $at) }
function U32([int]$at) { [System.BitConverter]::ToUInt32($script:D, $at) }
function U64([int]$at) { [System.BitConverter]::ToUInt64($script:D, $at) }

function Load-Pe([byte[]]$data) {
    $script:D = $data
    $script:PeError = $null
    if ($data.Length -lt 0x100 -or $data[0] -ne 0x4D -or $data[1] -ne 0x5A) { $script:PeError = "is not a program (no MZ header)"; return $null }
    $pe = U32 0x3C
    if ($pe + 24 -ge $data.Length -or (U32 $pe) -ne 0x00004550) { $script:PeError = "is not a PE file"; return $null }
    if ((U16 ($pe + 4)) -ne 0x8664) { $script:PeError = "is not a 64-bit program"; return $null }
    $count = U16 ($pe + 6)
    $optSize = U16 ($pe + 20)
    $opt = $pe + 24
    if ((U16 $opt) -ne 0x20B) { $script:PeError = "is not a 64-bit program"; return $null }
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
