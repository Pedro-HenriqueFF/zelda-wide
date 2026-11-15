#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Decodes overworld screens straight from the ROM, the same way the game's own
// LayoutRoomOW routine does (see Z_05.asm), so that neighbouring screens can be drawn
// without the game ever loading them.
//
// A decoded screen is 32 x 22 tiles of 8x8, stored column-major: tiles[col * 22 + row]
// (exactly how the game's PlayAreaTiles buffer at $6530 is laid out).
// Hidden-secret kinds of the overworld, taken from the primary square the layout uses (Z_05.asm LayoutRoomOW):
// $E6 rock wall (bomb -> cave), $E7 tree (burn -> stairs), $E5 rock / $E8 gravestone (pushed -> stairs). The armos that hides
// stairs (SecretArmos) is not in the layout: the game picks it by room and position (Widescreen::roomTiles).
enum Secret : uint8_t { SecretNone = 0, SecretBomb, SecretBurn, SecretArmos, SecretPush };

class OverworldMap {
public:
    static constexpr int TILE_COLS = 32, TILE_ROWS = 22, TILES = TILE_COLS * TILE_ROWS;

    // Locate the data tables inside the PRG image of the "(U) (PRG0)" ROM.
    bool init(const std::vector<uint8_t>& prg, std::string& error);

    // Lower-level setup with explicit PRG offsets (used by the unit tests).
    void initWithOffsets(const std::vector<uint8_t>& prg, size_t layouts, const size_t heaps[16],
                         size_t primary, size_t secondary);

    // layoutId: low 7 bits of LevelBlockAttrsD for the screen.
    // secretFound: bit 7 of the screen's world flags (turns trees/rocks into stairs/caves).
    // secretOut (optional, TILES bytes, same layout): which tiles belong to a secret that is still hidden (Secret enum;
    // all Secret::None once secretFound).
    void decodeRoom(int layoutId, bool secretFound, uint8_t* tilesOut, uint8_t* secretOut = nullptr) const;

    bool ready() const { return prg != nullptr; }

private:
    const std::vector<uint8_t>* prg = nullptr;
    size_t layoutsOff = 0, primaryOff = 0, secondaryOff = 0;
    size_t heapOff[16] = {};
};
