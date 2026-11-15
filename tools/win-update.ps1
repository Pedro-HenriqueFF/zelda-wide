# Puts a new milestone zip over this project folder WITHOUT touching the files that did not change, so the next build only
# recompiles what is new (unzipping over the folder gives every file a new date, which makes the compiler rebuild everything).
#   powershell -ExecutionPolicy Bypass -File tools\win-update.ps1 -Zip C:\Downloads\zelda-wide-milestone90.zip
param([Parameter(Mandatory = $true)][string]$Zip)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$tmp = Join-Path $env:TEMP "zw-update"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
Expand-Archive -Path $Zip -DestinationPath $tmp
$src = (Get-ChildItem -Path $tmp -Directory | Select-Object -First 1).FullName
if (-not (Test-Path (Join-Path $src "CMakeLists.txt"))) { throw "this does not look like a Zelda Wide zip (no CMakeLists.txt)" }
$changed = 0; $same = 0; $removed = 0
$newFiles = Get-ChildItem -Path $src -Recurse -File
foreach ($f in $newFiles) {
    $rel = $f.FullName.Substring($src.Length).TrimStart('\')
    $dst = Join-Path $root $rel
    if ((Test-Path $dst) -and ((Get-FileHash $dst).Hash -eq (Get-FileHash $f.FullName).Hash)) { $same++; continue }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    Copy-Item -Force $f.FullName $dst
    $changed++
    Write-Host "  updated $rel"
}
# files this version no longer has (only in the source folders: never the build folder or your own files)
foreach ($dir in @("src", "tests", "tools")) {
    $dstDir = Join-Path $root $dir
    if (-not (Test-Path $dstDir)) { continue }
    foreach ($f in Get-ChildItem -Path $dstDir -Recurse -File) {
        $rel = $f.FullName.Substring($root.Length).TrimStart('\')
        if (-not (Test-Path (Join-Path $src $rel))) { Remove-Item -Force $f.FullName; $removed++; Write-Host "  removed $rel" }
    }
}
Remove-Item -Recurse -Force $tmp
Write-Host "$changed files updated, $same unchanged, $removed removed. Now run tools\win-build.ps1"
