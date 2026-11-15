# Builds the game on Windows, as fast as the machine allows: configures once, then only compiles what changed.
#   powershell -ExecutionPolicy Bypass -File tools\win-build.ps1 [-Avx2 | -NoAvx2]
#   -Avx2 compiles for CPUs with AVX2 (any PC since about 2015: maybe faster, will not run on older CPUs; recompiles everything once), -NoAvx2 goes back.
param([string]$Config = "Release", [switch]$Avx2, [switch]$NoAvx2)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$timer = [System.Diagnostics.Stopwatch]::StartNew()
$cache = "build\CMakeCache.txt"

# A build folder copied or renamed from another project folder cannot be used (CMake remembers where it was made): start it again.
if (Test-Path $cache) {
    $line = Select-String -Path $cache -Pattern "^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$" | Select-Object -First 1
    if ($line) {
        $madeFor = $line.Matches[0].Groups[1].Value.TrimEnd('/')
        $here = ($root -replace '\\', '/').TrimEnd('/')
        if ($madeFor -ine $here) {
            Write-Host "The build folder was made for another project folder ($madeFor): starting it again ..."
            Remove-Item -Recurse -Force build
        }
    }
}

# The prebuilt SDL2 from tools\win-sdl.ps1, if it is there (otherwise CMake downloads and builds SDL2 itself).
$sdl = Join-Path $root "third_party\SDL2-2.30.9\cmake"
$haveSdl = Test-Path (Join-Path $sdl "sdl2-config.cmake")
$usesSdl = (Test-Path $cache) -and (Select-String -Path $cache -Pattern "SDL2_DIR:PATH=.*third_party" -Quiet)
if ((-not (Test-Path $cache)) -or ($haveSdl -and -not $usesSdl)) {
    # (-Wno-deprecated: SDL's own CMake file is old and makes CMake complain about it; nothing to do with this project)
    $cfg = @("-B", "build", "-Wno-deprecated", "-Wno-dev")
    if ($haveSdl) { $cfg += "-DSDL2_DIR=$($sdl -replace '\\', '/')"; Write-Host "Configuring with the prebuilt SDL2 ..." }
    else { Write-Host "Configuring (SDL2 is downloaded and built: a minute or two; tools\win-sdl.ps1 avoids that) ..." }
    cmake @cfg
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
}
if ($Avx2 -or $NoAvx2) {
    $flag = if ($Avx2) { "ON" } else { "OFF" }
    Write-Host "Switching AVX2 $flag (everything is compiled again) ..."
    cmake -B build -Wno-deprecated -Wno-dev "-DZW_AVX2=$flag"
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
}
cmake --build build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }
$exe = Get-ChildItem -Path build -Recurse -Filter zelda_wide.exe | Select-Object -First 1
Write-Host ("Done in {0:N0} s: {1}" -f $timer.Elapsed.TotalSeconds, $exe.FullName)
