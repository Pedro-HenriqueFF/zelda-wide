#include "world.h"
#include <cstdio>
#include <cstring>

namespace {

constexpr size_t kBankSize = 16384;
constexpr size_t kRoomLayoutsOW = 87064;     // PRG offset of dat/RoomLayoutsOW.dat (bins.xml)
constexpr size_t kRoomLayoutsOWLen = 1936;
constexpr size_t kCaveLayoutsLen = 3 * 16;   // three 16-byte cave layouts sit between the layouts and the first heap

// Short fingerprints of tables in the PRG0 ROM, used to find them without hard-coding addresses.
const uint8_t kColumnDirectorySig[] = {0xD8, 0x9B, 0x0D, 0x9C, 0x3E, 0x9C, 0x80, 0x9C};
const uint8_t kPrimarySig[] = {0x24, 0x6F, 0xF3, 0xFA, 0x98, 0x90, 0x8F, 0x95, 0x8E, 0x90, 0x74, 0x76,
                               0xF3, 0x24, 0x26, 0x89, 0x03, 0x04, 0x70, 0xC8, 0xBC, 0x8D, 0x8F, 0x93};
const uint8_t kSecondarySig[] = {0x24, 0x24, 0x24, 0x24, 0x6F, 0x6F, 0x6F, 0x6F,
                                 0xF3, 0xF3, 0xF3, 0xF3, 0xFA, 0xFA, 0xFA, 0xFA};

// Primary squares $E5..$EA are "tile objects" (rock, armos...); the game draws these tiles instead.
const uint8_t kTileObjectPrimary[6] = {0xC8, 0xD8, 0xC4, 0xBC, 0xC0, 0xC0};

constexpr int kPrimaryCount = 0x38;

// Returns the offset of `sig` in `prg`, or npos if it isn't found exactly once.
size_t findUnique(const std::vector<uint8_t>& prg, const uint8_t* sig, size_t len) {
    size_t found = std::string::npos;
    for (size_t i = 0; i + len <= prg.size(); i++) {
        if (std::memcmp(&prg[i], sig, len) == 0) {
            if (found != std::string::npos) return std::string::npos;
            found = i;
        }
    }
    return found;
}

} // namespace

bool OverworldMap::init(const std::vector<uint8_t>& image, std::string& error) {
    prg = nullptr;
    if (image.size() != 8 * kBankSize) {
        error = "PRG size is not 128KB; this is not the expected Zelda ROM.";
        return false;
    }
    size_t dir = findUnique(image, kColumnDirectorySig, sizeof kColumnDirectorySig);
    size_t pri = findUnique(image, kPrimarySig, sizeof kPrimarySig);
    size_t sec = findUnique(image, kSecondarySig, sizeof kSecondarySig);
    if (dir == std::string::npos || pri == std::string::npos || sec == std::string::npos) {
        error = "Overworld tables not found (this build supports the \"(U) (PRG0)\" ROM only).";
        return false;
    }
    size_t heaps[16];
    for (int i = 0; i < 16; i++) {
        unsigned addr = image[dir + i * 2] | (image[dir + i * 2 + 1] << 8);
        if (addr < 0x8000 || addr >= 0xC000) { error = "Column directory looks wrong."; return false; }
        heaps[i] = 5 * kBankSize + (addr - 0x8000);                 // heaps live in bank 5
    }
    // Consistency check: heap 0 must start right after the room layouts and the 3 cave layouts.
    if (heaps[0] != kRoomLayoutsOW + kRoomLayoutsOWLen + kCaveLayoutsLen) {
        error = "Room layout offset doesn't line up with the column heaps.";
        return false;
    }
    initWithOffsets(image, kRoomLayoutsOW, heaps, pri, sec);
    return true;
}

void OverworldMap::initWithOffsets(const std::vector<uint8_t>& image, size_t layouts, const size_t heaps[16],
                                   size_t primary, size_t secondary) {
    prg = &image;
    layoutsOff = layouts;
    primaryOff = primary;
    secondaryOff = secondary;
    for (int i = 0; i < 16; i++) heapOff[i] = heaps[i];
}

bool OverworldMap::squareTiles(int idx, uint8_t out[4]) const {
    if (!prg || idx < 0 || idx >= kPrimaryCount || primaryOff + idx >= prg->size()) return false;
    const std::vector<uint8_t>& rom = *prg;
    uint8_t primary = rom[primaryOff + idx];
    if (primary >= 0xE5 && primary <= 0xEA) primary = kTileObjectPrimary[primary - 0xE5];
    if (idx >= 0x10) { for (int k = 0; k < 4; k++) out[k] = (uint8_t)(primary + k); return true; }      // "type 1": four consecutive tiles
    if (secondaryOff + (size_t)idx * 4 + 3 >= rom.size()) return false;
    for (int k = 0; k < 4; k++) out[k] = rom[secondaryOff + idx * 4 + k];                                 // "type 3": explicit tiles
    return true;
}

void OverworldMap::decodeRoom(int layoutId, bool secretFound, uint8_t* out, uint8_t* secretOut) const {
    std::memset(out, 0, TILES);
    if (secretOut) std::memset(secretOut, 0, TILES);
    if (!prg) return;
    const std::vector<uint8_t>& rom = *prg;
    const size_t layout = layoutsOff + (size_t)(layoutId & 0x7F) * 16;

    auto put = [&](int c, int r, uint8_t t) { out[c * TILE_ROWS + r] = t; };

    int repeat = 0;                                       // the game's [$0C] "repeat once" toggle; it survives across columns
    for (int col = 0; col < 16; col++) {
        const uint8_t desc = rom[layout + col];
        const size_t heap = heapOff[desc >> 4];
        int wanted = desc & 0x0F;

        // Find the start of column `wanted`: columns begin at descriptors that have bit 7 set. (Bounded: a damaged ROM or a
        // layout id the game never uses must not make us read past the end of the PRG.)
        size_t y = 0;
        bool found = false;
        for (; y < 4096 && heap + y < rom.size(); y++)
            if ((rom[heap + y] & 0x80) && --wanted < 0) { found = true; break; }
        if (!found) continue;                                 // leave this column empty
        size_t ptr = heap + y;

        for (int row = 0; row < 11 && ptr < rom.size(); row++) {
            const uint8_t sq = rom[ptr];
            int idx = sq & 0x3F;
            uint8_t primary = idx < kPrimaryCount ? rom[primaryOff + idx] : 0x24;

            uint8_t kind = SecretNone;
            if (!secretFound) {
                if (primary == 0xE6) kind = SecretBomb;
                else if (primary == 0xE7) kind = SecretBurn;
                else if (primary == 0xE5 || primary == 0xE8) kind = SecretPush;
            }
            if (secretFound) {
                if (primary == 0xE7 || primary == 0xEA) { idx = 0x10; primary = 0x70; }   // tree/armos -> stairs
                else if (primary == 0xE6)               { idx = 0x0C; }                   // rock wall -> cave entrance
            }
            if (primary >= 0xE5 && primary <= 0xEA) primary = kTileObjectPrimary[primary - 0xE5];

            uint8_t t0, t1, t2, t3;
            if (idx >= 0x10) {                              // "type 1" square: four consecutive CHR tiles
                t0 = primary; t1 = primary + 1; t2 = primary + 2; t3 = primary + 3;
            } else {                                        // "type 3" square: explicit tiles from the secondary table
                const uint8_t* s = &rom[secondaryOff + idx * 4];
                t0 = s[0]; t1 = s[1]; t2 = s[2]; t3 = s[3];
            }
            put(col * 2,     row * 2,     t0);
            put(col * 2,     row * 2 + 1, t1);
            put(col * 2 + 1, row * 2,     t2);
            put(col * 2 + 1, row * 2 + 1, t3);
            if (secretOut && kind)
                for (int dc = 0; dc < 2; dc++) for (int dr = 0; dr < 2; dr++) secretOut[(col * 2 + dc) * TILE_ROWS + row * 2 + dr] = kind;

            if (sq & 0x40) {                                // "draw this square twice"
                repeat ^= 0x40;
                if (repeat) continue;                       // first time: stay on this descriptor
            }
            ptr++;
        }
    }
}
