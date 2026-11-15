# Source layout
- `src/cpu.*`   6502 core (official opcodes, NES has no decimal mode)
- `src/cart.*`  iNES loader, MMC1 mapper, battery RAM
- `src/ppu.*`   PPU: registers, scrolling, background/sprites, sprite-0 hit, NMI; also outputs the layers the compositor needs
- `src/nes.*`   bus, controller, OAM DMA, frame loop; `Nes::cloneFrom` copies a running game (used by the shadow games, whose PPU is lite: no picture, only the sprite-0 hit)
- `src/shadow.*` the shadow games (`ShadowGame` = one copy of the game and its spin-up, `ShadowSet` = the ones around Link, the hand-off); `src/widescreen_shadow.cpp` draws their sprites and water reflections; see FEATURES.md
- `src/world.*` overworld screen decoder (reads the ROM's layout/column/square tables)
- `src/zelda_ram.h` the game's RAM addresses and object-slot numbers used by the front-end and the compositor
- `src/widescreen.h` the compositor class; `src/widescreen_internal.h` constants and helpers shared by its files
- `src/widescreen.cpp` setup, zoom, frame composition (`render`); `src/widescreen_state.cpp` capture of the game state, the decoded-screen cache, fog discovery
- `src/widescreen_world.cpp` overworld drawing (fog, secret marks), the game's room and overhang, the death-scene fade
- `src/widescreen_save.cpp` ROM patches (sword at borders, save-slot hearts), SAVE handling, the fog files (`<rom>.fog`)
- `src/clouds.*` the cloud shadows (shapes, drift, shading)
- `src/lighting.*` the day clock and the lighting pass; `src/widescreen_light.cpp` which things glow, Link's sight, the dungeon torches
- `src/widescreen_sprites.cpp` per-frame sprite fixes, sword-beam splash, Link's sprites
- `src/widescreen_dungeon.cpp` the frozen monsters/items/lights/water of the last frame of play that dungeon transitions replay, and the dungeon water
- `src/widescreen_water.cpp` water reflections and waves (the per-pixel pass, the overworld water grid); `src/widescreen_water_apply.cpp` the water passes for the game's own picture of the screen, the cave backdrop's water and the debug dump; `src/widescreen_stairs.cpp` coming out of stairs on the square Link went in by; `src/parallel.h` the thread pool behind the per-pixel passes; `src/perf.h` the `--perf` timers; `src/widescreen_door.cpp` Link in dungeon doorways, sprites left out at the walls
- `src/mist.*` the maze mist and haze
- `src/widescreen_hud.cpp` overlay HUD, minimap and thumbnails, cave/menu/game-over overlays
- `src/widescreen_glide.cpp` the glide between overworld screens
- `src/main.cpp` SDL window (also draws the overlay pictures at window resolution), input, frame pacing, save files
- `tests/test_wide.cpp` runs the sections in `tests/test_wide_{world,caves,hud,sprites,effects,light,water,maze,door}.cpp` in order on a shared context
  (`tests/test_wide_common.*`); later sections rely on state left by earlier ones
- `tools/check.sh` build + all tests + `main.cpp` syntax check (+ `--zip N`); `tools/sdl_stub/` fake SDL.h for that check;
  `tools/review.sh` strict warnings + dead-code candidates for the periodic code review; `tools/sheet.py` video contact sheets;
  `tools/win-build.ps1`, `tools/win-update.ps1`, `tools/win-sdl.ps1` the Windows build, update and prebuilt-SDL2 helpers
