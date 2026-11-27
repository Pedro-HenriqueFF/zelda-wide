// The world beyond the 16 x 8 map: which kind of terrain extends past each border, how it fades into the fog, and the text file with the
// hand-made rooms below the map's bottom edge. No emulator in here (widescreen_beyond.cpp builds the tiles from the real screens).
#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace beyond {
constexpr int kMapTilesW = 16 * 32, kMapTilesH = 8 * 22;      // the map in 8x8 tiles
constexpr int kPatchFirstCol = 4, kPatchCount = 8;            // rooms $74..$7B (screen columns 4..11 of the bottom row) get hand-made rooms
constexpr int kPatchCols = 16, kPatchMaxRows = 20;            // a patch is 16 squares wide (16 x 16 px each) and up to 20 squares tall

enum class Zone : uint8_t { Inside, Extrude, Sea, Custom };
// What a tile (tile coordinates, the map's top left is 0,0) beyond the map is made of. Extrude = the border's own terrain repeated outwards
// (mountains, trees); Sea = water; Custom = below rooms $74..$7B: the hand-made patch, and sea where the patch ends.
Zone zoneOf(int tx, int ty);
// How far the extension reaches before it is all fog, in screens (land = the repeated mountains/trees, sea = the sea and the custom rooms);
// 0 = no fade at all (the extension goes on to the end of the band of tiles).
struct Fade { float land, sea; };
constexpr int kFadeModes = 3;
Fade fadeFor(int mode);                // 0: land 0.5 + sea 3.5, 1: land 1 + sea 3.5, 2: no fade
const char* fadeName(int mode);
// Terrain share (0..256) of a pixel beyond the map: 256 at the border, 0 once the fade length is passed.
int fadeVis(int wx, int wy, const Fade& f);

struct Cell { uint8_t id[4] = {0x26, 0x26, 0x26, 0x26}; uint8_t pal = 0; };   // a square's four tiles: top-left, top-right, bottom-left, bottom-right
struct File {
    std::map<char, Cell> legend;                              // built-in entries, replaced by the file's
    std::array<int, kPatchCount> rows{};                      // squares tall (0 = no patch: sea)
    std::array<std::vector<char>, kPatchCount> grid;          // rows * 16 legend characters
};
File defaults();
// Parses the text of the file; false with a message ("line N: ...") on the first error, `out` is then left as it was.
bool parse(const std::string& text, File& out, std::string& err);
std::string templateText();
}
