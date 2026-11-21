# Building

## Windows (native) — recommended if you want to run it on Windows
1. Install **CMake** (cmake.org) and a C++ compiler: either *Visual Studio Build Tools* ("Desktop development with C++") or MSYS2/MinGW.
2. In a terminal in this folder: `powershell -ExecutionPolicy Bypass -File tools\win-build.ps1` (or by hand: `cmake -B build` and
   `cmake --build build --config Release --parallel`). The first configure downloads and builds SDL2 (a minute or two); after that only the
   files that changed are compiled (MSVC compiles several files at once, and the standard headers are precompiled).
3. Run: `build\Release\zelda_wide.exe path\to\zelda.nes` (MinGW: `build\zelda_wide.exe ...`)

**Updating to a new version without a full rebuild.** Keep one project folder and its `build` folder. Do not unzip a new version over it
(every file would get a new date and everything would be recompiled); run
`powershell -ExecutionPolicy Bypass -File tools\win-update.ps1 -Zip C:\path\to\zelda-wide-milestoneNN.zip`
instead: it copies only the files whose content changed (and removes the ones the new version no longer has), then `tools\win-build.ps1`
compiles just those.

**Optional AVX2 build.** `tools\win-build.ps1 -Avx2` compiles the per-pixel loops for CPUs with AVX2 (any PC since about 2015; it will not run on older CPUs);
`-NoAvx2` goes back (CMake option `ZW_AVX2`, Linux/MinGW use `-mavx2`). Compare with `--perf` to see whether it helps on your machine.

**Prebuilt SDL2 (no SDL build at all).** `powershell -ExecutionPolicy Bypass -File tools\win-sdl.ps1` downloads the official SDL2 package for
Visual C++ into `third_party\SDL2-2.30.9`; `tools\win-build.ps1` then uses it (it configures the build folder again once) and the build copies
`SDL2.dll` next to `zelda_wide.exe`. By hand: unzip `SDL2-devel-2.30.9-VC.zip` anywhere and configure with
`cmake -B build -DSDL2_DIR=C:/path/to/SDL2-2.30.9/cmake`.

## WSL / Ubuntu
```
sudo apt install build-essential cmake libsdl2-dev
cmake -B build
cmake --build build -j
./build/zelda_wide /path/to/zelda.nes
```
On Windows 11 (WSLg) the window appears on your desktop. Audio/input latency is usually better when built natively on Windows.

## Tests (no ROM or SDL needed)

**Windows:** `powershell -ExecutionPolicy Bypass -File tools\win-test.ps1` runs `test_core`, `test_world` and `test_wide` from the CMake build and, if `test_wide`
crashes, says which section does it. (`tools/check.sh` is a bash script: Linux/WSL only.) With CMake alone: `ctest --test-dir build -C Release --output-on-failure`.
The test executables are linked with a 32 MB stack (Windows' default 1 MB is too small for the compositor tests).

**Linux / WSL:** `tools/check.sh` builds everything (a fast `-O2` build by default, about 20 s for `test_wide`; with `--san`, and always with `--zip N`, with
AddressSanitizer/UBSan), runs `test_core` (CPU, MMC1, PPU, shadow games), `test_world` (overworld decoder, camera) and `test_wide` (renderer, fog, HUD, caves,
sprites; on a synthetic ROM with faked game RAM), syntax-checks `main.cpp` against a stub SDL and checks that CMake lists every source. It only recompiles what
changed (a no-change run takes a few seconds). `tools/check.sh --zip N` also packages the project into `$ZW_ZIP_DIR` (default `./dist`; only project files: never `build/`, `third_party/`, `dist/`, `docs/images/`, `.git`, ROMs, saves or earlier zips); `--zip-only N` packages
without running the checks (for documentation-only changes). With CMake: `cmake --build build && ctest --test-dir build --output-on-failure`.

## Updating the project folder from a zip

Unzipping over the folder gives every file a new date and makes the next build recompile everything. These scripts copy only the files that changed (and remove
the ones the new version no longer has in `src`, `tests`, `tools`, `docs`):

- Windows: `powershell -ExecutionPolicy Bypass -File tools\win-update.ps1 -Zip C:\path\to\zelda-wide-milestoneNN.zip`
- Linux / WSL: `tools/linux-update.sh ~/Downloads/zelda-wide-milestoneNN.zip` (add `--build` to build and run the tests afterwards)
