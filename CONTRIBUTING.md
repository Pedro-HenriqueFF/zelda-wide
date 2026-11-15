# Contributing

Thanks for your interest! Bug reports, ideas and pull requests are welcome.

## Ground rules

- **Never commit or attach a ROM** (or anything that is a copy of game data: dumps of the PRG/CHR, full tile sheets, and so on). Screenshots and
  short videos of the running game for bug reports are fine. The tests must keep working without a ROM.
- Keep the game itself unmodified in spirit: the compositor reads the game's state and draws around and over its picture. The few places that patch
  the game's code (see [FEATURES.md](FEATURES.md)) are deliberate; talk to us before adding another.

## Build and test

```
cmake -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

For day-to-day work `tools/check.sh` is faster (incremental build, `test_core`, `test_world`, `test_wide`, a syntax check of `main.cpp` against a stub
SDL, and a check that CMake lists every source). `tools/check.sh --san` adds AddressSanitizer/UBSan. See [docs/BUILDING.md](docs/BUILDING.md).

## Code

- C++17, no exceptions needed, no external dependencies besides SDL2 (and only in `src/main.cpp`: the core and the compositor build and test headless).
- Names for game memory and modes live in `src/zelda_ram.h`; do not scatter raw addresses. A fact about the game should come from the public
  disassembly (aldonunez/zelda1-disassembly) and be referenced by file and line in a comment.
- New `src/*.cpp` files must be added to `CMakeLists.txt`. Keep files reasonably small (around 500 lines).
- Per-pixel work goes through `par::forRange` (`src/parallel.h`) and must give the same pixels on 1 thread and on several.

## Tests

- Every new behaviour gets a test. The compositor tests (`tests/test_wide_*.cpp`) run on a synthetic ROM with faked game RAM; read the notes at the top of
  `tests/test_wide_common.h`. They run in order on one shared context.
- **Mutation check:** after writing a test, break the fix on purpose and confirm that the test fails. Several tests once passed without testing anything.
- A one-frame lag between a mode change and what the compositor sees is normal in the fixtures: run a frame after changing the state.

## Pull requests

1. Make the change small and focused; describe what changed and why.
2. `tools/check.sh` (or `ctest`) passes; new tests included.
3. Update `FEATURES.md` when behaviour changes (describe the current behaviour, not the history) and `README.md` only if the essentials change.

## Reporting bugs

Open an issue with the version (see the README), your OS and what you did. A screenshot or a short video helps a lot; for water problems press `Home`
and attach `water_debug.txt`, for the neighbour screens `Page Up` and `shadow_debug.txt`; `--trace` prints the game mode per frame.
