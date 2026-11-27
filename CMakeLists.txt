cmake_minimum_required(VERSION 3.16)
project(zelda_wide CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---- SDL2: use the system copy if there is one, otherwise download and build it ----
find_package(Threads)
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
  message(STATUS "SDL2 not found on the system - fetching it (needs internet on the first configure)")
  include(FetchContent)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)        # (less to build: no test library, no second static copy)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
  FetchContent_MakeAvailable(SDL2)
endif()

# ---- Emulator core (no SDL dependency, so it can be unit-tested headless) ----
add_library(nescore STATIC
  src/cpu.cpp src/cart.cpp src/ppu.cpp src/nes.cpp src/world.cpp
  src/widescreen.cpp src/widescreen_state.cpp src/widescreen_hud.cpp src/widescreen_world.cpp src/widescreen_sprites.cpp src/widescreen_glide.cpp src/widescreen_save.cpp src/widescreen_stairs.cpp src/clouds.cpp src/lighting.cpp src/widescreen_light.cpp src/widescreen_water.cpp src/widescreen_water_apply.cpp src/widescreen_beyond.cpp src/beyond.cpp src/beyond_default.cpp src/widescreen_dungeon.cpp src/widescreen_door.cpp src/mist.cpp src/shadow.cpp src/widescreen_shadow.cpp)
target_include_directories(nescore PUBLIC src)
# Faster builds: the std headers are parsed once per target (precompiled), and MSVC compiles several files at the same time.
target_precompile_headers(nescore PRIVATE <algorithm> <cmath> <cstdint> <cstring> <functional> <memory> <string> <vector>)
if(MSVC)
  target_compile_options(nescore PRIVATE /MP)
endif()
# Optional: let the compiler use AVX2 (any PC since about 2015) for the per-pixel loops. Does not run on older CPUs, so it is off by default.
option(ZW_AVX2 "Compile for CPUs with AVX2 (faster per-pixel loops; will not run on older CPUs)" OFF)
if(ZW_AVX2)
  if(MSVC)
    target_compile_options(nescore PRIVATE /arch:AVX2)
  else()
    target_compile_options(nescore PRIVATE -mavx2)
  endif()
endif()

# ---- The game ----
if(Threads_FOUND)
  target_link_libraries(nescore PUBLIC Threads::Threads)
endif()
add_executable(zelda_wide src/main.cpp)
if(MSVC)
  target_compile_options(zelda_wide PRIVATE /MP)
endif()
target_link_libraries(zelda_wide PRIVATE nescore)
if(TARGET SDL2::SDL2main)
  target_link_libraries(zelda_wide PRIVATE SDL2::SDL2main)
endif()
if(TARGET SDL2::SDL2)
  target_link_libraries(zelda_wide PRIVATE SDL2::SDL2)
else()
  target_include_directories(zelda_wide PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(zelda_wide PRIVATE ${SDL2_LIBRARIES})
endif()
# On Windows the SDL2 DLL has to sit next to the exe.
if(WIN32 AND TARGET SDL2::SDL2)
  add_custom_command(TARGET zelda_wide POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:zelda_wide>)
endif()

# ---- Tests ----
option(BUILD_TESTS "Build the headless core tests" ON)
if(BUILD_TESTS)
  enable_testing()
  add_executable(test_core tests/test_core.cpp)
  target_link_libraries(test_core PRIVATE nescore)
  add_test(NAME core COMMAND test_core)
  add_executable(test_world tests/test_world.cpp)
  target_link_libraries(test_world PRIVATE nescore)
  add_test(NAME world COMMAND test_world)
  add_executable(test_wide tests/test_wide.cpp tests/test_wide_common.cpp tests/test_wide_world.cpp
    tests/test_wide_caves.cpp tests/test_wide_hud.cpp tests/test_wide_sprites.cpp tests/test_wide_effects.cpp tests/test_wide_light.cpp tests/test_wide_water.cpp tests/test_wide_beyond.cpp tests/test_wide_maze.cpp tests/test_wide_door.cpp tests/test_wide_stairs.cpp tests/test_wide_perf.cpp tests/test_wide_shadow.cpp)
  target_link_libraries(test_wide PRIVATE nescore)
  add_test(NAME wide COMMAND test_wide)
  # The compositor objects are big and the tests are deep: Windows' default 1 MB stack is not enough (a crash, no message).
  foreach(t test_core test_world test_wide)
    if(MSVC)
      target_link_options(${t} PRIVATE /STACK:33554432)
    elseif(WIN32)
      target_link_options(${t} PRIVATE -Wl,--stack,33554432)
    endif()
  endforeach()
endif()
