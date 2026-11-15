# Runs the three test programs (no ROM or SDL needed) after tools\win-build.ps1 (or any CMake build) and, if test_wide crashes, tells which section does it:
#   powershell -ExecutionPolicy Bypass -File tools\win-test.ps1 [-Config Release]
# Windows counterpart of the test part of tools/check.sh (that one is a bash script: Linux/WSL only).
param([string]$Config = "Release")
$root = Split-Path -Parent $PSScriptRoot
function Find-Exe([string]$name) {
    foreach ($p in @("build\$Config\$name.exe", "build\$name.exe", "build\bin\$Config\$name.exe")) {
        $full = Join-Path $root $p
        if (Test-Path $full) { return $full }
    }
    throw "$name.exe not found under build\ - build first with tools\win-build.ps1"
}
function Show-Code([int]$code) {
    if ($code -eq 0) { return "ok" }
    $hex = "0x{0:X8}" -f ([uint32]([int64]$code -band 0xFFFFFFFF))
    $why = switch ($hex) { "0xC0000005" { " (access violation)" } "0xC00000FD" { " (stack overflow)" } "0xC0000409" { " (stack buffer overrun / abort)" } default { "" } }
    return "exit code $hex$why"
}
$failed = $false
foreach ($t in @("test_core", "test_world", "test_wide")) {
    $exe = Find-Exe $t
    Write-Host "== $t"
    & $exe
    $code = $LASTEXITCODE
    Write-Host ("   " + (Show-Code $code))
    if ($code -ne 0) { $failed = $true }
    if ($code -ne 0 -and $t -eq "test_wide") {
        Write-Host "test_wide failed: which section? (sections run in order and share state; ZW_ONLY runs one alone)"
        $env:ZW_TIME = "1"
        $lines = & $exe 2>&1 | ForEach-Object { "$_" }
        Remove-Item Env:\ZW_TIME
        Write-Host "last lines of the full run (the section after the last timed one is the one that failed):"
        $lines | Select-Object -Last 4 | ForEach-Object { Write-Host "   $_" }
        for ($i = 1; $i -le 40; $i++) {
            $name = "section{0:00}" -f $i
            $env:ZW_ONLY = $name
            & $exe *> $null
            $c = $LASTEXITCODE
            Remove-Item Env:\ZW_ONLY
            if ($c -ne 0) { Write-Host "   $name alone: $(Show-Code $c)" }
        }
    }
}
if ($failed) { Write-Host "SOME TESTS FAILED"; exit 1 } else { Write-Host "all tests passed" }
