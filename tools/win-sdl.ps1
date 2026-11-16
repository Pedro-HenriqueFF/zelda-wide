# Downloads the prebuilt SDL2 for Visual C++ (from the official SDL releases) into third_party\ so the build does not have to build SDL2 itself.
#   powershell -ExecutionPolicy Bypass -File tools\win-sdl.ps1
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"            # (the progress bar makes Windows PowerShell's download very slow)
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$root = Split-Path -Parent $PSScriptRoot
$ver = "2.30.9"
$dir = Join-Path $root "third_party"
$target = Join-Path $dir "SDL2-$ver"
if (Test-Path (Join-Path $target "cmake\sdl2-config.cmake")) { Write-Host "SDL2 $ver is already in $target"; exit 0 }
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$zip = Join-Path $env:TEMP "SDL2-devel-$ver-VC.zip"
$url = "https://github.com/libsdl-org/SDL/releases/download/release-$ver/SDL2-devel-$ver-VC.zip"
Write-Host "Downloading $url ..."
Invoke-WebRequest -Uri $url -OutFile $zip
Expand-Archive -Path $zip -DestinationPath $dir -Force
Remove-Item $zip
if (-not (Test-Path (Join-Path $target "cmake\sdl2-config.cmake"))) { throw "the download did not contain SDL2-$ver\cmake\sdl2-config.cmake" }
Write-Host "SDL2 $ver is in $target. Run tools\win-build.ps1: it uses it from now on (the build folder is configured again once)."
