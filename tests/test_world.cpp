// Unit tests for the overworld decoder, using a tiny synthetic ROM image.
#include "world.h"
#include "widescreen.h"
#include <cstdio>
#include <vector>
#include <cstring>

static int failures = 0, checks = 0;
#define CHECK_EQ(a, b) do { checks++; long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { failures++; std::printf("FAIL %s:%d  %s == %s  (got 0x%llX, want 0x%llX)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

int main() {
    std::vector<uint8_t> prg(128 * 1024, 0);
    const size_t layouts = 0x1000, primary = 0x2000, secondary = 0x2100;
    size_t heaps[16];
    for (int i = 0; i < 16; i++) heaps[i] = 0x3000 + i * 0x100;

    // Primary squares
    prg[primary + 0x10] = 0x60;
    prg[primary + 0x11] = 0x40;
    prg[primary + 0x12] = 0xE9;      // tile object (armos) -> drawn as $C0
    prg[primary + 0x13] = 0xE7;      // tree -> $C4, or stairs ($70) once the secret is found
    prg[primary + 0x14] = 0xE6;      // rock wall -> $D8, or cave once found
    prg[primary + 0x15] = 0xE8;      // gravestone (pushable)
    prg[primary + 0x16] = 0xEA;      // armos over stairs
    // Secondary square 5 and 12
    const uint8_t s5[4] = {1, 2, 3, 4}, s12[4] = {0xF3, 0x24, 0xF3, 0x24};
    std::memcpy(&prg[secondary + 5 * 4], s5, 4);
    std::memcpy(&prg[secondary + 12 * 4], s12, 4);

    // Heap 2: column 0 and column 1.
    uint8_t* h = &prg[heaps[2]];
    //  column 0: [0x11 x2 via repeat] [sec 5] [0x10 x2 via repeat] [0x13 x6]   = 2+1+2+6 = 11 rows
    const uint8_t col0[] = {0x80 | 0x40 | 0x11, 0x05, 0x40 | 0x10, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13};
    //  column 1: [0x14 x11] with a repeat in the middle  -> 0x14 x1, 0x14 repeat (2), then 8 more
    const uint8_t col1[] = {0x80 | 0x14, 0x40 | 0x14, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12};
    std::memcpy(h, col0, sizeof col0);
    std::memcpy(h + sizeof col0, col1, sizeof col1);

    const uint8_t col3[] = {0x80 | 0x15, 0x15, 0x15, 0x15, 0x15, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16};
    std::memcpy(&prg[heaps[3]], col3, sizeof col3);
    uint8_t* lay4 = &prg[layouts + 4 * 16];
    for (int c = 0; c < 16; c++) lay4[c] = 0x30;

    // Layout #3: column 0 -> heap 2 column 0, column 1 -> heap 2 column 1, others -> heap 2 column 0
    uint8_t* lay = &prg[layouts + 3 * 16];
    for (int c = 0; c < 16; c++) lay[c] = 0x20;
    lay[1] = 0x21;

    OverworldMap map;
    map.initWithOffsets(prg, layouts, heaps, primary, secondary);
    uint8_t t[OverworldMap::TILES];
    auto at = [&](int c, int r) { return (int)t[c * OverworldMap::TILE_ROWS + r]; };

    map.decodeRoom(3, false, t);
    // squares 0,1 (repeat of 0x11 -> primary 0x40): tile rows 0..3
    CHECK_EQ(at(0, 0), 0x40); CHECK_EQ(at(0, 1), 0x41); CHECK_EQ(at(1, 0), 0x42); CHECK_EQ(at(1, 1), 0x43);
    CHECK_EQ(at(0, 2), 0x40); CHECK_EQ(at(1, 3), 0x43);
    // square 2: secondary 5 -> tiles 1,2,3,4 at rows 4,5
    CHECK_EQ(at(0, 4), 1); CHECK_EQ(at(0, 5), 2); CHECK_EQ(at(1, 4), 3); CHECK_EQ(at(1, 5), 4);
    // squares 3,4: 0x10 repeated -> primary 0x60 at rows 6..9
    CHECK_EQ(at(0, 6), 0x60); CHECK_EQ(at(1, 7), 0x63); CHECK_EQ(at(0, 8), 0x60); CHECK_EQ(at(1, 9), 0x63);
    // squares 5..10: 0x13 (tree) -> tile-object replacement $C4..$C7, rows 10..21
    CHECK_EQ(at(0, 10), 0xC4); CHECK_EQ(at(0, 11), 0xC5); CHECK_EQ(at(1, 10), 0xC6); CHECK_EQ(at(1, 21), 0xC7);
    CHECK_EQ(at(0, 21), 0xC5);
    // column 1 (tile cols 2,3): 0x14 -> rock wall $D8, repeated: squares 0 and 1 and 2 (1 + repeat 2) ...
    CHECK_EQ(at(2, 0), 0xD8); CHECK_EQ(at(2, 2), 0xD8); CHECK_EQ(at(2, 4), 0xD8);
    // ... then 0x12 (armos -> $C0) for the remaining 8 squares
    CHECK_EQ(at(2, 6), 0xC0); CHECK_EQ(at(3, 7), 0xC3); CHECK_EQ(at(2, 21), 0xC1);

    {   // hidden secrets per tile: tree (burn) = 2, rock wall (bomb) = 1, plain armos none; a found secret is not marked any more
        uint8_t sec[OverworldMap::TILES];
        auto sat = [&](int c, int r) { return (int)sec[c * OverworldMap::TILE_ROWS + r]; };
        map.decodeRoom(3, false, t, sec);
        CHECK_EQ(sat(0, 10), SecretBurn); CHECK_EQ(sat(1, 21), SecretBurn); CHECK_EQ(sat(0, 9), SecretNone);
        CHECK_EQ(sat(2, 0), SecretBomb); CHECK_EQ(sat(3, 5), SecretBomb); CHECK_EQ(sat(2, 6), SecretNone);
        map.decodeRoom(3, true, t, sec);
        int any = 0; for (int i = 0; i < OverworldMap::TILES; i++) any += sec[i];
        CHECK_EQ(any, 0);
        map.decodeRoom(4, false, t, sec);                                        // gravestones (push); $EA is not marked
        CHECK_EQ(sat(0, 0), SecretPush); CHECK_EQ(sat(1, 9), SecretPush); CHECK_EQ(sat(0, 10), SecretNone); CHECK_EQ(sat(1, 21), SecretNone);   // (armos secrets come from the game's room/X table)
        map.decodeRoom(3, false, t);                                             // (the secret output is optional)
    }
    // Secret found: trees become stairs ($70..$73), rock wall becomes a cave (secondary 12), armos -> stairs
    map.decodeRoom(3, true, t);
    CHECK_EQ(at(0, 10), 0x70); CHECK_EQ(at(0, 11), 0x71); CHECK_EQ(at(1, 10), 0x72); CHECK_EQ(at(1, 11), 0x73);
    CHECK_EQ(at(2, 0), 0xF3); CHECK_EQ(at(2, 1), 0x24); CHECK_EQ(at(3, 0), 0xF3); CHECK_EQ(at(3, 1), 0x24);
    CHECK_EQ(at(2, 6), 0xC0);            // armos primary is $E9 (not the "special" $EA), so unaffected

    // ---- Link's world position (camera centre) ----
    {
        ViewSnapshot s; s.room = 0x23; s.linkX = 120; s.linkY = 141;   // room column 3, row 2 -> origin (768, 352)
        int cx, cy;
        linkWorldCenter(s, cx, cy);
        CHECK_EQ(cx, 768 + 120 + 8);
        CHECK_EQ(cy, 352 + (141 + 8 - 64));
        s.room = 0x00; s.linkX = 0; s.linkY = 64;
        linkWorldCenter(s, cx, cy);
        CHECK_EQ(cx, 8); CHECK_EQ(cy, 8);
    }

    // ---- A damaged ROM / layout id the game never uses must not read past the end of the PRG ----
    {
        std::vector<uint8_t> blank(8 * 16384, 0);                    // no column starts anywhere (no descriptor has bit 7)
        size_t heaps[16];
        for (int i = 0; i < 16; i++) heaps[i] = blank.size() - 100;   // heaps that begin right before the end of the image
        OverworldMap bad;
        bad.initWithOffsets(blank, 0, heaps, 0, 0);
        uint8_t out[OverworldMap::TILES];
        bad.decodeRoom(0, false, out);                               // (under AddressSanitizer this used to read out of bounds)
        bool allZero = true;
        for (uint8_t v : out) allZero = allZero && v == 0;
        CHECK_EQ(allZero, true);
        bad.decodeRoom(0x7F, true, out);
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
