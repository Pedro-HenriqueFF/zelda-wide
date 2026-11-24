// Section 27: water reflections and waves.
#include <memory>
#include <algorithm>
#include "test_wide_common.h"
#include "widescreen_internal.h"
#include "parallel.h"
#include "perf.h"
#include "lighting.h"
#include "zelda_ram.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <set>
#include <thread>
#include <mutex>

using namespace wd;
using namespace zelda;

void section27(Ctx& t) {
    SECTION_BINDINGS
    constexpr int R = OverworldMap::TILE_ROWS;
    constexpr uint32_t kLand = 0xFF20A020, kSea = 0xFF2040C0, kRed = 0xFFE02020, kDot = 0xFFE08020, kBlack = 0xFF000000;
    CHECK(isWaterTile(0x95) && isWaterTile(0x89) && isWaterTile(0x98) && !isWaterTile(0x88) && !isWaterTile(0x99));
    auto w_h = std::make_unique<Widescreen>(); Widescreen& w = *w_h; w.init(*nes, err); w.unfogAnim = false; w.cloudShadow = false; w.lighting = false; w.fog = false;
    for (int id = 0; id < 128; id++) { w.roomTiles(*nes, id); std::memset(w.cache[id].tiles, 0x24, OverworldMap::TILES); }
    auto put = [&](int room, int c0, int c1, int r0, int r1, uint8_t id) { for (int c = c0; c <= c1; c++) for (int r = r0; r <= r1; r++) w.cache[room].tiles[c * R + r] = id; };
    put(0, 4, 27, 4, 21, 0x95); put(16, 4, 27, 0, 17, 0x95);        // a big lake across two screens (rooms 0 and 16)
    put(1, 4, 9, 6, 11, 0x95);                                       // a pond in room 1
    put(0, 8, 8, 3, 3, 0xC4);                                        // a tree above the lake (reflects whole)
    put(0, 17, 20, 0, 3, 0xC4);                                      // a wall (2 squares high) at the shore over the rounded corner test
    put(0, 22, 23, 0, 3, 0x74);                                      // a ladder (2 squares high) at the shore, next to the corner wall's twin
    put(0, 26, 27, 0, 1, 0xC4); put(0, 27, 27, 2, 3, 0xC4); put(0, 26, 26, 2, 3, 0x24);   // a wall whose last square at the shore is curved: ground tiles on its left
    put(14, 4, 9, 4, 9, 0x95); put(13, 4, 9, 4, 9, 0x95);            // a pond on a screen that always waves (row 0, column 14), one that does not (column 13)
    // ponds with walls over them (room 1): the wall's base at the shore (B), 1 square away (A), 2 away (P2), 3 away (P3), and two stacked trees at the shore (C)
    put(1, 14, 19, 14, 19, 0x95); put(1, 22, 27, 18, 21, 0x95);
    put(1, 4, 5, 0, 3, 0xC4);                                        // A: wall of 2 squares, one ground square between it and the water
    put(1, 6, 7, 2, 5, 0xC4);                                        // B: wall of 2 squares right at the shore
    put(1, 8, 9, 2, 5, 0xC4);                                        // C: two stacked trees at the shore
    put(1, 14, 15, 4, 9, 0xC4);                                      // P2: a wall of 3 squares, two ground squares between it and the water
    put(1, 22, 23, 4, 11, 0xC4);                                     // P3: a wall of 4 squares, three ground squares between it and the water
    w.fxNes = nes.get(); w.fxRoom = 0; w.fxSlot = 0; w.fxOx = 0; w.fxOy = 0; w.setWater(true); w.wwReady = false;
    Canvas cv; cv.w = 512; cv.h = 240;
    std::vector<uint8_t> mask;
    auto tileId = [&](int wx, int wy) { return w.cache[(wy / ROOM_H) * 16 + wx / ROOM_W].tiles[((wx % ROOM_W) >> 3) * R + ((wy % ROOM_H) >> 3)]; };
    auto paint = [&] {
        cv.pixels.assign((size_t)cv.w * cv.h, kLand);
        for (int y = 0; y < cv.h; y++) for (int x = 0; x < cv.w; x++) cv.pixels[(size_t)y * cv.w + x] = isWaterTile(tileId(x, y)) ? kSea : kLand;
        for (int y = 24; y < 32; y++) for (int x = 64; x < 72; x++) cv.pixels[(size_t)y * cv.w + x] = kRed;        // the tree
        for (int y = 0; y < 32; y++) for (int x = 136; x < 168; x++) cv.pixels[(size_t)y * cv.w + x] = kRed;        // a wall over the rounded corner below
        auto redTiles = [&](int room, int c0, int c1, int r0, int r1) { for (int y = (room >> 4) * ROOM_H + r0 * 8; y < (room >> 4) * ROOM_H + r1 * 8 + 8; y++) for (int x = (room & 15) * ROOM_W + c0 * 8; x < (room & 15) * ROOM_W + c1 * 8 + 8; x++) cv.pixels[(size_t)y * cv.w + x] = kRed; };
        for (int y = 0; y < 32; y++) for (int x = 176; x < 192; x++) cv.pixels[(size_t)y * cv.w + x] = kRed;           // the ladder
        for (int y = 0; y < 32; y++) for (int x = 208; x < 224; x++) cv.pixels[(size_t)y * cv.w + x] = kRed;           // the curved square's wall ...
        for (int y = 16; y < 32; y++) for (int x = 208; x < 216; x++) cv.pixels[(size_t)y * cv.w + x] = kLand;         // ... whose bottom left is ground-coloured
        for (int y = 32; y < 40; y++) for (int x = 160; x < 168; x++) cv.pixels[(size_t)y * cv.w + x] = kLand;         // a second rounded corner: its water starts 8 px lower ...
        for (int y = 43; y < 48; y++) for (int x = 160; x < 168; x++) cv.pixels[(size_t)y * cv.w + x] = kLand;         // ... and is only 3 px high before the rim goes on
        redTiles(1, 4, 5, 0, 3); redTiles(1, 6, 7, 2, 5); redTiles(1, 8, 9, 2, 5); redTiles(1, 14, 15, 4, 9); redTiles(1, 22, 23, 4, 11);
        for (int y = 32; y < 40; y++) for (int x = 144; x < 152; x++) cv.pixels[(size_t)y * cv.w + x] = kLand;     // the lake's corner: its water starts 8 px lower here
        for (int y = 24; y < 32; y++) for (int x = 112; x < 120; x++) cv.pixels[(size_t)y * cv.w + x] = kRed;      // something on plain ground
        cv.pixels[(size_t)100 * cv.w + 150] = kDot;                                                                // a dot inside the water
        cv.pixels[(size_t)33 * cv.w + 40] = kBlack;                                                                // a black pixel on the lake's edge
        cv.pixels[(size_t)32 * cv.w + 40] = kBlack;
    };
    auto run = [&](bool objects = false) {
        if (!w.wwReady) {                                             // (the fake ROM's tiles say nothing about trees: only the stacked trees of C are "open" tiles)
            w.ensureWaterWorld(); for (auto& k : w.wwKind) k &= (uint8_t)~16;
            for (int c = 8; c <= 9; c++) for (int r = 2; r <= 5; r++) w.wwKind[(size_t)r * 512 + 32 + c] |= 16;
        }
        Widescreen::WaterJob j{cv.pixels.data(), cv.w, cv.h, 0, 0, 0, 0, 0, cv.w, cv.h, &mask, true, objects}; w.waterPass(j);
    };
    auto Rch = [&](int x, int y) { return (int)(cv.pixels[(size_t)y * cv.w + x] >> 16 & 255); };
    auto at = [&](int x, int y) { return cv.pixels[(size_t)y * cv.w + x]; };
    std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
    w.lightPpu = &nes->ppu; w.waterTick = 100;
    paint(); run();
    // 27a. Big bodies: the lake (two screens) yes, the pond (one screen) no.
    CHECK(w.wwKind[(size_t)10 * 512 + 10] & 2); CHECK(!(w.wwKind[(size_t)(8) * 512 + 32 + 6] & 2)); CHECK(w.wwKind[(size_t)(8) * 512 + 32 + 6] & 1);
    CHECK(w.wwKind[(size_t)6 * 512 + 14 * 32 + 6] & 2); CHECK(!(w.wwKind[(size_t)6 * 512 + 13 * 32 + 6] & 2));   // 27a2: the listed screens always wave, the one beside them does not
    // 27b. Reflections: a tree (whole), ground (only the bottom quarter of its tile), none far from the shore.
    CHECK(Rch(68, 33) > Rch(200, 33) + 25);                          // right under the shore the tree is mirrored ...
    CHECK(Rch(68, 33) > Rch(68, 52) + 8);                            // ... fading with depth
    CHECK(std::abs(Rch(116, 32) - Rch(200, 32)) < 10);               // ground and docks at the shore do not reflect
    CHECK(std::abs(Rch(116, 40) - Rch(200, 40)) < 14);               // ... the rest of the tile does not
    CHECK(std::abs(Rch(147, 44) - Rch(156, 44)) < 8);                // a rounded corner continues the reflection of its neighbours
    // 27b2. Walls reflect from their BASE: at the shore 40 px, one square away the next 24 px (the base square skipped), two away the next 8, three away nothing;
    //       a tree only itself and only at the shore.
    CHECK(Rch(311, 50) > Rch(200, 50) + 15); CHECK(Rch(311, 76) > Rch(200, 76) + 6);              // B: a 2 squares high wall at the shore: reflected 32 px (its height)
    CHECK(std::abs(Rch(311, 90) - Rch(200, 90)) < 10);                                                // ... and not beyond its height
    CHECK(Rch(295, 50) > Rch(200, 50) + 10);                                                          // A: the part above its base square is reflected under the shore ...
    CHECK(std::abs(Rch(295, 68) - Rch(200, 68)) < 10);                                                // ... for the 16 px that exist
    CHECK(Rch(327, 50) > Rch(200, 50) + 15); CHECK(std::abs(Rch(327, 68) - Rch(200, 68)) < 10);       // C: the front tree only (16 px), not the one behind it
    CHECK(Rch(375, 114) > Rch(200, 114) + 4); CHECK(std::abs(Rch(375, 124) - Rch(200, 124)) < 10);    // P2: 8 px, from two squares above the base
    CHECK(std::abs(Rch(437, 146) - Rch(200, 146)) < 10);                                              // P3: too far, nothing
    // 27b3. Stairs reflect like walls, up to their height; a square with ground tiles at its end still counts as the base; ground-coloured pixels are not reflected;
    //       the 3 px rim bit of water reflects; trees (both bottom tiles open) are the only ones that are short.
    CHECK(Rch(184, 52) > Rch(200, 52) + 10); CHECK(std::abs(Rch(184, 74) - Rch(200, 74)) < 10);     // the ladder: 32 px, its height
    CHECK(w.wwKind[(size_t)2 * 512 + 26] & 4);                                                       // the ground tile of the curved square is wall too
    CHECK(Rch(220, 34) > Rch(200, 34) + 15); CHECK(Rch(212, 50) > Rch(200, 50) + 8);                // the curved square's wall reflects on both halves, from the same base
    CHECK(Rch(163, 41) > Rch(200, 41) + 10);                                                         // the rim's 3 px of water at the rounded corner reflects
    {
        for (int gy = 2; gy <= 3; gy++) for (int gx = 22; gx <= 23; gx++) w.wwKind[(size_t)gy * 512 + gx] |= 16;   // even "open" ladder tiles are no trees
        paint(); w.waterTick = 100; run(); CHECK(Rch(184, 52) > Rch(200, 52) + 10);
    }
    // 27b4. The pond's top corner tile ($91) is water and reflects; stray dots do not make fake shores.
    {
        put(0, 27, 27, 4, 4, 0x91); w.wwReady = false; paint();
        cv.pixels[(size_t)40 * cv.w + 219] = kDot; cv.pixels[(size_t)40 * cv.w + 220] = kDot; cv.pixels[(size_t)41 * cv.w + 219] = kDot;   // a blob of dots: 2-3 non-blue pixels in a column
        w.waterTick = 100; run();
        CHECK(Rch(220, 38) > Rch(200, 38) + 12);
        CHECK(Rch(219, 44) > Rch(200, 44) + 8);                                                // no new "shore" under the blob: the reflection goes on as before
        put(0, 27, 27, 4, 4, 0x95); w.wwReady = false;
    }
    // 27c. Waves on the big lake only; a dot is in the water, an outline pixel on the edge is not.
    std::set<uint32_t> shades; for (int y = 90; y < 130; y++) for (int x = 60; x < 100; x++) shades.insert(at(x, y));
    CHECK(shades.size() > 6);
    CHECK(at(150, 100) != kDot);                                     // (a dot gets the waves too)
    CHECK_EQ(at(40, 33), kBlack);                                    // the outline stays
    CHECK_EQ(at(256 + 6 * 8 + 4, 92), kSea);               // a pond: flat (deeper than the reflection)
    { const uint32_t before = at(80, 100); bool moved = false;
      for (int i = 0; i < 60 && !moved; i++) { paint(); w.waterTick++; run(); moved = at(80, 100) != before; }
      CHECK(moved); }
    {   // 27c2. The two switches are independent: waves off = flat lake, reflections stay; reflections off = no mirror image, waves stay.
        auto shadeCount = [&] { std::set<uint32_t> sh; for (int y = 90; y < 130; y++) for (int x = 60; x < 100; x++) sh.insert(at(x, y)); return sh.size(); };
        w.waterWaves = false; paint(); w.waterTick++; run();
        CHECK_EQ(shadeCount(), (size_t)1); CHECK(Rch(220, 34) > Rch(200, 34) + 15);
        w.waterWaves = true; w.waterReflections = false; paint(); w.waterTick++; run();
        CHECK(shadeCount() > 6); CHECK(Rch(220, 34) < Rch(200, 34) + 5);
        w.waterReflections = true; paint(); w.waterTick++; run();                        // (back to both on)
        CHECK(shadeCount() > 6); CHECK(Rch(220, 34) > Rch(200, 34) + 15);
    }
    // 27d. Sprites standing in the water stay as they are; the status bar's sprites (map dot, A/B items) do not block the waves.
    {
        paint(); w.waterTick = 100; run(); const std::vector<uint32_t> ref = cv.pixels;
        paint(); nes->ppu.sprCol[(100 + 64) * Ppu::SPR_W + 80] = 1; w.waterTick = 100; run(true);
        CHECK_EQ(at(80, 100), kSea);                                  // the sprite pixel itself: unchanged
        CHECK_EQ(ref[(size_t)101 * cv.w + 80], cv.pixels[(size_t)101 * cv.w + 80]);   // its neighbours are as without it
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        w.fxOx = 0; w.fxOy = ROOM_H;                                  // pretend the current screen is room 16: sprite rows < 64 are the bar
        nes->ppu.sprCol[31 * Ppu::SPR_W + 100] = 1;
        paint(); w.waterTick = 100; run(true); CHECK_EQ(cv.pixels[(size_t)143 * cv.w + 100], ref[(size_t)143 * cv.w + 100]);
        w.fxOy = 0; std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
    }
    // 27e. Link reflects about his FEET: the head shows under the shore, and nothing when he is further away.
    {
        auto link = [&](int objY) {
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            nes->ram[kObjX] = 100; nes->ram[kObjY] = (uint8_t)objY; nes->ram[kObjState] = 0;
            paint();
            for (int py = objY + 1; py <= objY + 16; py++) for (int px = 100; px < 116; px++) {
                nes->ppu.sprCol[py * Ppu::SPR_W + px] = 0x16;
                const int y = py - HUD_H; if (y >= 0 && y < cv.h) cv.pixels[(size_t)y * cv.w + px] = kRed;
            }
            w.waterTick = 100; run(true);
        };
        link(71);                                                     // feet at world y 24: the mirror hangs from 24 to 40, the water starts at 32
        CHECK(Rch(105, 34) > Rch(200, 34) + 20);                      // the top of his head, mirrored, in the water
        CHECK(std::abs(Rch(105, 60) - Rch(200, 60)) < 14);            // nothing further down
        link(55);                                                     // 16 px further from the shore: no reflection at all
        CHECK(std::abs(Rch(105, 34) - Rch(200, 34)) < 14);
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); nes->ram[kObjX] = 0; nes->ram[kObjY] = 0;
    }
    // 27f. Waves fade out over a river/pond (no waves) instead of stopping at the border.
    {
        cv.pixels.assign((size_t)cv.w * cv.h, kSea); mask.assign((size_t)cv.w * cv.h, 1);
        for (int y = 0; y < cv.h; y++) for (int x = 0; x < 256; x++) mask[(size_t)y * cv.w + x] = 2;
        w.waterTick = 100;
        Widescreen::WaterJob j{cv.pixels.data(), cv.w, cv.h, 0, 0, 0, 0, 0, cv.w, cv.h, &mask, false, false}; w.waterPass(j);
        int farVar = 0, nearVar = 0, farRight = 0;
        for (int y = 60; y < 120; y++) { for (int x = 100; x < 140; x++) farVar += at(x, y) != kSea; for (int x = 256; x < 268; x++) nearVar += at(x, y) != kSea; for (int x = 272; x < 300; x++) farRight += at(x, y) != kSea; }
        CHECK(farVar > 200); CHECK(nearVar > 20); CHECK_EQ(farRight, 0);
    }
    // 27f2. Outside the screen the game drew (clip), the reflection's source is the plain terrain, not the fogged picture.
    {
        paint(); const std::vector<uint32_t> plain = cv.pixels;
        for (int y = 16; y < 48; y++) for (int x = 304; x < 320; x++) cv.pixels[(size_t)y * cv.w + x] = 0xFF101010;       // the fog-darkened wall B
        Widescreen::WaterJob j{cv.pixels.data(), cv.w, cv.h, 0, 0, 0, 0, 52, cv.w, cv.h, &mask, true, false, nullptr, true, &plain};
        w.waterTick = 100; w.waterPass(j);
        CHECK(Rch(311, 54) > Rch(200, 54) + 8);
    }
    // 27f3. The camera does not matter: terrain above the picture (the margin) is read too. The picture starts at world y 40, wall B (y 16-47) is mostly above it.
    {
        const int M = 96, top = 96, camY = 40;
        std::vector<uint32_t> ext((size_t)cv.w * (cv.h + M), kLand);
        auto world = [&](int wy) { return (size_t)(wy - camY + top) * cv.w; };
        for (int wy = camY - M; wy < camY + cv.h; wy++) for (int x = 0; x < cv.w; x++) {
            if (wy < 0 || wy >= 2 * ROOM_H) continue;
            ext[world(wy) + x] = isWaterTile(tileId(x, wy)) ? kSea : kLand;
            if (x >= 304 && x < 320 && wy >= 16 && wy < 48) ext[world(wy) + x] = kRed;
        }
        std::vector<uint32_t> noMargin = ext; std::vector<uint8_t> m2;
        Widescreen::WaterJob j1{ext.data(), cv.w, cv.h + M, top, 0, camY, 0, top, cv.w, cv.h + M, &mask, true, false, nullptr, true, nullptr, 0};
        w.waterTick = 100; w.waterPass(j1);
        Widescreen::WaterJob j2{noMargin.data(), cv.w, cv.h + M, top, 0, camY, 0, top, cv.w, cv.h + M, &m2, true, false};
        w.waterTick = 100; w.waterPass(j2);
        const auto R2 = [&](const std::vector<uint32_t>& v, int wy, int x) { return (int)(v[world(wy) + x] >> 16 & 255); };
        CHECK(R2(ext, 70, 311) > R2(ext, 70, 200) + 6);                    // with the margin the wall's upper half is there to be reflected (d = 22)
        CHECK(std::abs(R2(noMargin, 70, 311) - R2(noMargin, 70, 200)) < 8);   // without it (the old behaviour) only the visible 8 rows were
    }
    // 27g. The ladder/stairs tile ($8D/$91) is never water, even where its stripes are blue; a black (fading-in) picture has no water at all.
    {
        put(0, 20, 20, 8, 8, 0x8D); w.wwReady = false; paint(); run();                                    // $8D/$91 are the top corners of a pond: water
        CHECK(w.wwKind[(size_t)8 * 512 + 20] & 1); CHECK(w.wwKind[(size_t)8 * 512 + 20] & 4);
        put(0, 20, 20, 8, 8, 0x95); w.wwReady = false;
        cv.pixels.assign((size_t)cv.w * cv.h, kBlack); run(); CHECK_EQ(at(80, 100), kBlack); CHECK_EQ(at(100, 50), kBlack);
    }
    // 27g1. The ladder's own tiles ($74-$77) are never water either, though they are not in the water range.
    {
        put(0, 20, 20, 8, 8, 0x75); w.wwReady = false; paint();
        for (int y = 64; y < 72; y++) for (int x = 160; x < 168; x++) cv.pixels[(size_t)y * cv.w + x] = kSea;
        w.waterTick = 100; run(); CHECK_EQ(at(163, 66), kSea); CHECK(w.wwKind[(size_t)8 * 512 + 20] & 4);
        put(0, 20, 20, 8, 8, 0x95); w.wwReady = false;
    }
    // 27g2. A water-id tile with white in it (the striped ladder) is scenery too, whatever its id.
    {
        put(0, 20, 20, 8, 8, 0x96); w.wwReady = false; paint();
        for (int y = 64; y < 72; y++) for (int x = 160; x < 168; x++) cv.pixels[(size_t)y * cv.w + x] = (y & 1) ? 0xFFFCFCFC : kSea;
        w.waterTick = 100; run(); CHECK_EQ(at(163, 66), kSea); CHECK_EQ(at(163, 65), 0xFFFCFCFC);
        // ... and its bare end tile (blue only) below it, between walls, is scenery too; the water below that is not.
        put(0, 20, 20, 9, 9, 0x96); put(0, 19, 19, 9, 9, 0xC4); put(0, 21, 21, 9, 9, 0xC4); w.wwReady = false; paint();
        for (int y = 64; y < 72; y++) for (int x = 160; x < 168; x++) cv.pixels[(size_t)y * cv.w + x] = (y & 1) ? 0xFFFCFCFC : kSea;
        for (int y = 72; y < 80; y++) for (int x = 160; x < 168; x++) cv.pixels[(size_t)y * cv.w + x] = kSea;
        w.waterTick = 100; run(); CHECK_EQ(at(163, 75), kSea);
        put(0, 20, 20, 8, 9, 0x95); put(0, 19, 19, 9, 9, 0x95); put(0, 21, 21, 9, 9, 0x95); w.wwReady = false;
    }
    // 27h-1. Docks ($8D/$91 planks) at the shore do not reflect; Link at the shore reflects about his feet also on stairs, and higher up on the stairs as part of them.
    {
        put(0, 12, 13, 10, 10, 0x76); put(0, 12, 13, 11, 11, 0x77); w.wwReady = false; paint();         // a dock across the lake ($76/$77, like the real one)
        for (int y = 80; y < 96; y++) for (int x = 96; x < 112; x++) cv.pixels[(size_t)y * cv.w + x] = kRed;
        w.waterTick = 100; run(); CHECK(std::abs(Rch(104, 98) - Rch(200, 98)) < 10); CHECK(!(w.wwKind[(size_t)10 * 512 + 12] & 4));
        put(0, 12, 13, 10, 11, 0x95); w.wwReady = false;
        auto linkOn = [&](int objY) {
            paint(); const std::vector<uint32_t> plain = cv.pixels;
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            nes->ram[kObjX] = 176; nes->ram[kObjY] = (uint8_t)objY; nes->ram[kObjState] = 0;
            for (int py = objY + 1; py <= objY + 16; py++) for (int px = 176; px < 192; px++) { nes->ppu.sprCol[py * Ppu::SPR_W + px] = 0x16; cv.pixels[(size_t)(py - HUD_H) * cv.w + px] = 0xFF20E020; }
            Widescreen::WaterJob j{cv.pixels.data(), cv.w, cv.h, 0, 0, 0, 0, 0, cv.w, cv.h, &mask, true, true, nullptr, true, &plain};
            w.waterTick = 100; w.waterPass(j);
            nes->ram[kObjX] = 0; nes->ram[kObjY] = 0; std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        };
        auto Gch = [&](int x, int y) { return (int)(cv.pixels[(size_t)y * cv.w + x] >> 8 & 255); };
        linkOn(79); CHECK(Gch(184, 36) > Gch(200, 36) + 20);          // at the shore (feet at y 32): his mirror image hangs under his feet
        linkOn(63); CHECK(Gch(184, 56) > Gch(200, 56) + 10); CHECK(Gch(184, 52) > Gch(200, 52) + 30);   // 16 px higher on the ladder: part of the ladder's reflection (d 16-31), clearly visible
    }
    // 27h0. A sprite in front of a wall's base does not move the base: the reflection stays and shows the terrain behind the sprite.
    {
        paint(); const std::vector<uint32_t> plain = cv.pixels;
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        nes->ram[kObjType + 1] = 1; nes->ram[kObjX + 1] = 152; nes->ram[kObjY + 1] = 79;                     // a monster in front of the corner wall's base: rows 80-95 = world y 16-31, its feet at y 32
        for (int py = 80; py <= 95; py++) for (int px = 152; px < 168; px++) { nes->ppu.sprCol[py * Ppu::SPR_W + px] = 0x16; cv.pixels[(size_t)(py - HUD_H) * cv.w + px] = kBlack; }
        Widescreen::WaterJob j{cv.pixels.data(), cv.w, cv.h, 0, 0, 0, 0, 0, cv.w, cv.h, &mask, true, true, nullptr, true, &plain};
        w.waterTick = 100; w.waterPass(j);
        CHECK(Rch(156, 44) > Rch(200, 44) + 8);                                                          // d = 12 under the wall: needs the base at the wall's real bottom
        nes->ram[kObjType + 1] = 0; std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
    }
    // 27h1. An item that fills only the top half of its 16 rows (a dropped heart) reflects about its lowest row, not about ObjY + 17: the reflection starts right under it.
    {
        auto runItem = [&](bool item) {
            paint(); const std::vector<uint32_t> plain = cv.pixels;
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            if (item) {
                nes->ram[kObjType + 1] = kTypeItem; nes->ram[kObjX + 1] = 152; nes->ram[kObjY + 1] = 87;         // rows 88-95 = world y 24-31, just above the water (y 32); ObjY + 17 would be y 40
                for (int py = 88; py <= 95; py++) for (int px = 152; px < 160; px++) { nes->ppu.sprCol[py * Ppu::SPR_W + px] = 0x16; cv.pixels[(size_t)(py - HUD_H) * cv.w + px] = kRed; }
            }
            Widescreen::WaterJob j{cv.pixels.data(), cv.w, cv.h, 0, 0, 0, 0, 0, cv.w, cv.h, &mask, true, true, nullptr, true, &plain};
            w.waterTick = 100; w.waterPass(j);
            nes->ram[kObjType + 1] = 0; std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            return cv.pixels;
        };
        const std::vector<uint32_t> with = runItem(true), without = runItem(false);
        int changed = 0; for (int y = 32; y < 40; y++) for (int x = 152; x < 160; x++) changed += with[(size_t)y * cv.w + x] != without[(size_t)y * cv.w + x];
        CHECK(changed > 8);                                                                              // (the 8 rows under the item: with the old axis its image would start 8 rows lower)
    }
    // 27h. Ground reflects only the bottom quarter of the tile right above the shore (not the ones further up); shots are copied, not mirrored.
    {
        paint(); for (int y = 16; y < 24; y++) for (int x = 100; x < 108; x++) cv.pixels[(size_t)y * cv.w + x] = kRed;   // ground two tiles above the shore
        w.waterTick = 100; run(); CHECK(std::abs(Rch(104, 40) - Rch(200, 40)) < 14);
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        nes->ram[kObjState + 0x12] = 1; nes->ram[kObjX + 0x12] = 100; nes->ram[kObjY + 0x12] = 79; nes->ram[kObjX] = 0; nes->ram[kObjY] = 0;     // an arrow: feet at world y 32
        paint();
        for (int py = 80; py <= 81; py++) for (int px = 100; px < 108; px++) { nes->ppu.sprCol[py * Ppu::SPR_W + px] = 0x16; cv.pixels[(size_t)(py - HUD_H) * cv.w + px] = kRed; }   // its top rows: world y 16, 17
        w.waterTick = 100; run(true);
        CHECK(Rch(104, 33) > Rch(200, 33) + 20);                      // copied: the top rows hang right under the feet (y 32, 33) ...
        CHECK(std::abs(Rch(104, 46) - Rch(200, 46)) < 14);            // ... not mirrored down at y 46/47
        nes->ram[kObjState + 0x12] = 0; std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
    }
    // 27i. Extra objects (Link during the glide) reflect without touching the scenery.
    {
        paint(); const std::vector<uint32_t> ref = cv.pixels;
        for (int x = 100; x < 116; x++) for (int y = 8; y < 24; y++) cv.pixels[(size_t)y * cv.w + x] = kRed;
        std::vector<Widescreen::WaterObj> link; for (int x = 100; x < 116; x++) for (int y = 8; y < 24; y++) link.push_back({x, y, 24, true});
        const std::vector<uint32_t> before = cv.pixels;
        Widescreen::WaterJob j{cv.pixels.data(), cv.w, cv.h, 0, 0, 0, 0, 0, cv.w, cv.h, &mask, true, false, &link, false}; w.waterTick = 100; w.waterPass(j);
        CHECK(Rch(105, 34) > Rch(200, 34) + 20); CHECK_EQ(at(200, 100), ref[(size_t)100 * cv.w + 200]);   // (no waves or scenery in this pass)
    }
    {   // The water grid is built again when the game's screen data changes (another level's attributes in the PRG-RAM, a loaded save): it used to be built once.
        auto w3_h = std::make_unique<Widescreen>(); Widescreen& w3 = *w3_h; w3.init(*nes, err); w3.fxNes = nes.get();
        const uint8_t old = nes->cart.cpuRead(kAttrsD + 9);
        w3.ensureWaterWorld(); const uint64_t sig1 = w3.wwSig; w3.ensureWaterWorld(); CHECK_EQ(w3.wwSig, sig1);
        nes->cart.cpuWrite(kAttrsD + 9, (uint8_t)((old & 0x7F) == 5 ? 6 : 5));
        w3.ensureWaterWorld(); CHECK(w3.wwSig != sig1);
        nes->cart.cpuWrite(kAttrsD + 9, old);
    }
    {   // The water pass spreads its rows and columns over several threads: the picture must be the same as with one (walls, big water, waves)
        auto wt_h = std::make_unique<Widescreen>(); Widescreen& wt = *wt_h; wt.init(*nes, err); wt.unfogAnim = false; wt.cloudShadow = false; wt.lighting = false; wt.fog = false;
        for (int id = 0; id < 128; id++) { wt.roomTiles(*nes, id); std::memset(wt.cache[id].tiles, 0x24, OverworldMap::TILES); }
        for (int id = 0; id < 8; id++) for (int c = 0; c < 32; c++) {
            for (int r = 2; r < 4; r++) wt.cache[id].tiles[c * R + r] = 0xD8;                         // a mountain wall above the sea: it reflects
            for (int r = 4; r < 22; r++) wt.cache[id].tiles[c * R + r] = 0x95;
        }
        wt.fxNes = nes.get(); wt.fxRoom = 3; wt.fxSlot = 0; wt.fxOx = 768; wt.fxOy = 0; wt.setWater(true); wt.wwReady = false; wt.lightPpu = &nes->ppu;
        const uint32_t kWall = 0xFF904820u;
        Canvas c1; c1.w = 1024; c1.h = 400; c1.pixels.assign((size_t)c1.w * c1.h, kLand);
        for (int y = 0; y < 400; y++) for (int x = 0; x < 1024; x++) c1.pixels[(size_t)y * 1024 + x] = y >= 32 && y < 176 ? kSea : (y >= 16 && y < 32 ? kWall : kLand);
        Canvas c2 = c1; std::vector<uint8_t> m1, m2;
        const int keepThreads = par::maxThreads();
        auto run = [&](Canvas& c, std::vector<uint8_t>& m, int threads, int tick) {
            par::maxThreads() = threads; wt.waterTick = tick;
            Widescreen::WaterJob jt{c.pixels.data(), c.w, c.h, 0, 0, 0, 0, 0, c.w, c.h, &m, true, false}; wt.waterPass(jt);
        };
        run(c1, m1, 1, 100); run(c2, m2, 4, 100);
        par::maxThreads() = keepThreads;
        Canvas orig; orig.w = 1024; orig.h = 400; orig.pixels.resize(c1.pixels.size());
        for (int y = 0; y < 400; y++) for (int x = 0; x < 1024; x++) orig.pixels[(size_t)y * 1024 + x] = y >= 32 && y < 176 ? kSea : (y >= 16 && y < 32 ? kWall : kLand);
        int touched = 0; for (size_t i = 0; i < c1.pixels.size(); i++) touched += c1.pixels[i] != orig.pixels[i];
        CHECK(touched > 20000);                                                                       // (the pass did something: reflections and waves)
        CHECK(c1.pixels == c2.pixels); CHECK(m1 == m2);
        {   // An objects-only job (a neighbour screen's reflections) works on the canvas buffer itself, which does not hold the margin rows above the picture: a sprite pixel in the margin has no
            // picture to read, so it makes no reflection and nothing is read before the buffer (on Windows that read was an access violation). Sentinel rows sit right before the buffer.
            const int R0 = 8, W = 1024, H = 400, guard = 4;
            std::vector<uint32_t> store((size_t)(guard + H - R0) * W, 0xFF00FF00u);                       // (green sentinel rows, then the canvas rows R0..H-1)
            uint32_t* buf = store.data() + (size_t)guard * W;
            for (int y = R0; y < H; y++) for (int x = 0; x < W; x++) buf[(size_t)(y - R0) * W + x] = y >= 32 && y < 176 ? kSea : kLand;
            std::vector<Widescreen::WaterObj> ex = {{100, 5, 40, true}, {300, 20, 40, true}};              // one pixel above the canvas (row 5 < 8), one on it (row 20)
            Widescreen::WaterJob jo{buf, W, H, 0, 0, 0, 0, 32, W, 176, &m1, false, true, &ex, false, nullptr, -1, R0};
            wt.waterTick = 100; wt.waterPass(jo);
            auto changed = [&](int row, int x) { bool any = false; for (int dx = -2; dx <= 2; dx++) any = any || buf[(size_t)(row - R0) * W + x + dx] != kSea; return any; };
            CHECK(!changed(74, 100));                                                                       // (the pixel in the margin: no reflection, no sentinel read)
            CHECK(changed(59, 300));                                                                        // (the pixel on the canvas: reflected)
            CHECK(std::all_of(store.begin(), store.begin() + (size_t)guard * W, [](uint32_t v) { return v == 0xFF00FF00u; }));   // (nothing written before the buffer either)
        }
        {   // the thread pool: every chunk runs exactly once, on several threads, and forRange returns only when all of them are done
            std::vector<int> hit(64, 0); std::set<std::thread::id> ids; std::mutex mu;
            par::maxThreads() = 4;
            par::forRange(0, 64, 1, [&](int a, int b) {
                for (int i = a; i < b; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); hit[i]++; }
                std::lock_guard<std::mutex> l(mu); ids.insert(std::this_thread::get_id());
            });
            par::maxThreads() = keepThreads;
            bool once = true; for (int h : hit) once = once && h == 1;
            CHECK(once);
#ifndef ZW_NO_THREADS
            CHECK(ids.size() >= 2);
#endif
            int sum = 0; par::maxThreads() = 3;                                                        // (and again: the pool is reused, with fewer helpers)
            std::vector<int> v2(40, 0);
            par::forRange(0, 40, 4, [&](int a, int b) { for (int i = a; i < b; i++) v2[i] = i; });
            par::maxThreads() = keepThreads;
            for (int i = 0; i < 40; i++) sum += v2[i];
            CHECK_EQ(sum, 780);
        }
        {   // --perf: the stage timers count while it is on, and nothing while it is off
            Canvas c3 = orig;
            perf::enabled = true; for (double& d : perf::total) d = 0;
            run(c3, m1, 2, 101); CHECK(perf::total[perf::kWaterA] > 0.0);
            perf::enabled = false; for (double& d : perf::total) d = 0;
            run(c3, m1, 2, 102); CHECK(perf::total[perf::kWaterA] == 0.0);
        }
        {   // the same for the light: a dim picture with a vision disc and two lights
            lighting::Params lp; lp.ambient = 0.2f; lp.vision = true; lp.vx = 300; lp.vy = 120; lp.visionInner = 40; lp.visionOuter = 120; lp.warm = 0.3f; lp.nightTint = 0.5f;
            std::vector<lighting::Source> ls; lighting::Source a{}; a.x = 600; a.y = 150; a.radius = 90; a.power = 1.0f; a.add = 0.5f; a.r = 255; a.g = 160; a.b = 60; ls.push_back(a);
            a.x = 100; a.y = 300; a.radius = 60; a.add = 0; ls.push_back(a);
            std::vector<uint32_t> l1(c1.pixels.size()); for (size_t i = 0; i < l1.size(); i++) l1[i] = 0xFF000000u | (uint32_t)((i * 2654435761u) >> 8);
            std::vector<uint32_t> l2 = l1;
            par::maxThreads() = 1; lighting::apply(l1.data(), 1024, 400, 32, 400, 5, 7, lp, ls);
            par::maxThreads() = 4; lighting::apply(l2.data(), 1024, 400, 32, 400, 5, 7, lp, ls);
            par::maxThreads() = keepThreads;
            CHECK(l1 == l2);
            std::vector<uint32_t> l0(l1.size()); for (size_t i = 0; i < l0.size(); i++) l0[i] = 0xFF000000u | (uint32_t)((i * 2654435761u) >> 8);
            CHECK(l1 != l0);                                                                           // (it did darken)
        }
    }
    if (std::getenv("ZW_TIME")) {                                    // benchmark: water on a canvas as big as a zoomed-out window (2048 x 900), twice a frame
        auto wb_h = std::make_unique<Widescreen>(); Widescreen& wb = *wb_h; wb.init(*nes, err); wb.unfogAnim = false; wb.cloudShadow = false; wb.lighting = false; wb.fog = false;
        for (int id = 0; id < 128; id++) { wb.roomTiles(*nes, id); std::memset(wb.cache[id].tiles, 0x24, OverworldMap::TILES); }
        for (int id = 0; id < 8; id++) for (int c = 0; c < 32; c++) for (int r = 4; r < 22; r++) wb.cache[id].tiles[c * R + r] = 0x95;
        wb.fxNes = nes.get(); wb.fxRoom = 3; wb.fxSlot = 0; wb.fxOx = 768; wb.fxOy = 0; wb.setWater(true); wb.wwReady = false; wb.lightPpu = &nes->ppu;
        Canvas cb; cb.w = 2048; cb.h = 900; cb.pixels.assign((size_t)cb.w * cb.h, kLand);
        for (int y = 0; y < 900; y++) for (int x = 0; x < 2048; x++) if (y >= 32 && y < 176) cb.pixels[(size_t)y * 2048 + x] = kSea;
        std::vector<uint8_t> mb;
        const auto tb = std::chrono::steady_clock::now(); for (int i = 0; i < 5; i++) { wb.wwReady = false; wb.ensureWaterWorld(); }
        std::fprintf(stderr, "water grid build: %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tb).count() / 5);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; i++) { Widescreen::WaterJob jb{cb.pixels.data(), cb.w, cb.h, 0, 0, 0, 0, 0, cb.w, cb.h, &mb, true, false}; wb.waterTick = 100 + i; wb.waterPass(jb); }
        const auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; i++) { Widescreen::WaterJob jb{cb.pixels.data(), cb.w, cb.h, 0, 0, 0, 768, 0, 1024, 176, &mb, false, true}; wb.waterTick = 100 + i; wb.waterPass(jb); }
        const auto t2 = std::chrono::steady_clock::now();
        const auto t3 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; i++) { Widescreen::WaterJob jb{cb.pixels.data(), cb.w, cb.h, 0, 0, 0, 800, 110, 832, 142, &mb, false, true, nullptr, false}; wb.waterTick = 100 + i; wb.waterPass(jb); }   // a window of 32 x 32 px, objects only: what a shadow with one monster needs
        const auto t4 = std::chrono::steady_clock::now();
        { const auto te = std::chrono::steady_clock::now(); for (int i = 0; i < 50; i++) wb.ensureWaterWorld();
          std::fprintf(stderr, "water bench: ensureWaterWorld() when nothing changed: %.3f ms per call\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - te).count() / 50); }
        const auto t5 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; i++) { Widescreen::WaterJob jb{cb.pixels.data(), cb.w, cb.h, 0, 0, 0, 768, 0, 1024, 176, &mb, false, true, nullptr, false}; wb.waterTick = 100 + i; wb.waterPass(jb); }   // a whole screen, objects only: what a shadow pass is today
        std::fprintf(stderr, "water bench: shadow-style pass (whole screen, objects only) %.2f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t5).count() / 20);
        std::fprintf(stderr, "water bench: pass B on a 32 x 32 window %.2f ms (the fixed cost of a call)\n", std::chrono::duration<double, std::milli>(t4 - t3).count() / 20);
        std::fprintf(stderr, "water bench 2048x900: pass A %.1f ms, pass B (one screen) %.1f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count() / 5, std::chrono::duration<double, std::milli>(t2 - t1).count() / 5);
    }
    if (std::getenv("ZW_TIME")) {                                    // benchmark: a whole frame (all features on) at the default and at a zoomed-out size
        for (int vh : {448, 900}) {
            auto wf_h = std::make_unique<Widescreen>(); Widescreen& wf = *wf_h; wf.init(*nes, err); wf.unfogAnim = false; wf.viewHeight = vh;
            setState(5, 0x23); frames(wf, 4, aspect);
            perf::enabled = true; for (double& d : perf::total) d = 0;
            const auto t0 = std::chrono::steady_clock::now(); frames(wf, 10, aspect);
            std::fprintf(stderr, "frame bench viewHeight %d: %.1f ms per frame (emulation included)\n", vh, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10);
            perf::enabled = false;
            std::fprintf(stderr, "   stages (ms per frame): world %.1f (waterA %.1f) room %.1f (water %.1f) waterB %.1f clouds %.1f light %.1f prep %.1f backdrop %.1f\n",
                         perf::total[perf::kWorld] / 10, perf::total[perf::kWaterA] / 10, perf::total[perf::kRoom] / 10, perf::total[perf::kRoomWater] / 10, perf::total[perf::kWaterB] / 10,
                         perf::total[perf::kClouds] / 10, perf::total[perf::kLight] / 10, perf::total[perf::kPrep] / 10, perf::total[perf::kBackdrop] / 10);
            wf.setWater(false);
            const auto t1 = std::chrono::steady_clock::now(); frames(wf, 10, aspect);
            std::fprintf(stderr, "   ... without water: %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / 10);
        }
    }
    // 27z. Which tiles are "open" (trees) comes from the pattern table; the mountain wall ids never are, and the grid follows the patterns.
    {
        const uint16_t pb = (nes->ppu.lineCtrl[100] & 0x10) ? 0x1000 : 0;
        auto setPatB = [&](int id, uint8_t lo) { for (int r = 0; r < 8; r++) { nes->cart.ppuWrite((uint16_t)(pb + id * 16 + r), lo); nes->cart.ppuWrite((uint16_t)(pb + id * 16 + r + 8), 0); } };
        put(2, 4, 4, 4, 4, 0xB0); put(2, 5, 5, 4, 4, 0xD8); put(2, 6, 6, 4, 4, 0xCE);
        const size_t gB = (size_t)4 * 512 + 2 * 32 + 4;
        setPatB(0xB0, 0xF0); setPatB(0xD8, 0xF0); setPatB(0xCE, 0xF0);                  // 32 of 64 pixels are background in all three
        w.ensureWaterWorld();
        CHECK(w.wwKind[gB] & 16); CHECK(!(w.wwKind[gB + 1] & 16)); CHECK(!(w.wwKind[gB + 2] & 16));   // a bush: open; the wall tiles: never
        setPatB(0xB0, 0xFF);                                                               // the tile is solid now (another pattern table loaded) ...
        w.ensureWaterWorld();                                                              // ... and the grid follows without any screen data changing
        CHECK(!(w.wwKind[gB] & 16));
        setPatB(0xB0, 0x00); setPatB(0xD8, 0x00); setPatB(0xCE, 0x00);
    }
    // 27y. The fog glow of the rooms is kept between frames: same picture as without the cache, and it follows a newly explored room.
    {
        auto a_h = std::make_unique<Widescreen>(); Widescreen& a = *a_h;
        auto b_h = std::make_unique<Widescreen>(); Widescreen& b = *b_h;
        for (Widescreen* z : {&a, &b}) { z->init(*nes, err); z->unfogAnim = false; z->cloudShadow = false; z->lighting = false; z->setWater(false); z->hudOverlay = false; z->caveBox = false; z->viewHeight = 440; }
        b.fogCache = false;
        const int slot = nes->ram[0x16] % 3;
        setState(5, 0x23);
        auto same = [&](bool expect) { frames(a, 3, aspect); const std::vector<uint32_t> pa = c.pixels; frames(b, 3, aspect); size_t diff = 0; for (size_t i = 0; i < pa.size() && i < c.pixels.size(); i++) diff += pa[i] != c.pixels[i]; return (diff == 0) == expect; };
        CHECK(same(true));
        frames(a, 2, aspect); const std::vector<uint32_t> before = c.pixels;
        a.fogBits[slot][0x24 >> 3] |= (uint8_t)(1 << (0x24 & 7)); b.fogBits[slot][0x24 >> 3] |= (uint8_t)(1 << (0x24 & 7));   // another room explored
        CHECK(same(true));
        frames(a, 1, aspect); size_t moved = 0; for (size_t i = 0; i < before.size() && i < c.pixels.size(); i++) moved += before[i] != c.pixels[i];
        CHECK(moved > 100);                                          // (the cache did not keep the old glow)
    }
    // 27w. Pass B can be run on any picture of the screen (the cave box's backdrop gets the room's reflections too): scenery over water is mirrored in it.
    {
        paint(); const std::vector<uint32_t> before = cv.pixels;
        run();                                                       // pass A over the whole fixture: its mask and the plain terrain
        const int M = Widescreen::kWaterMargin;
        w.terrainRaw.assign((size_t)cv.w * (cv.h + M), kLand); std::copy(before.begin(), before.end(), w.terrainRaw.begin() + (size_t)M * cv.w);
        w.terrainMask.assign((size_t)cv.w * (cv.h + M), 0); std::copy(mask.begin(), mask.end(), w.terrainMask.begin() + (size_t)M * cv.w);
        w.terrainW = cv.w; w.terrainH = cv.h; w.topBand = 0; w.fxOx = 0; w.fxOy = 0;
        Canvas pic; pic.w = cv.w; pic.h = cv.h; pic.pixels = before;
        w.roomWater(pic, 0, 0, false);
        size_t changed = 0; for (int y = 0; y < ROOM_H; y++) for (int x = 0; x < ROOM_W; x++) changed += pic.pixels[(size_t)y * pic.w + x] != before[(size_t)y * pic.w + x];
        CHECK(changed > 100);                                        // reflections and waves inside the screen's rectangle
        size_t outside = 0; for (int y = ROOM_H; y < pic.h; y++) for (int x = 0; x < pic.w; x++) outside += pic.pixels[(size_t)y * pic.w + x] != before[(size_t)y * pic.w + x];
        CHECK(outside == 0);                                         // and nothing beyond it
    }
    // 27v. Through the real render path: the overworld frames before a cave record what the backdrop needs, and in the cave the water behind the box moves.
    {
        auto m_h = std::make_unique<Widescreen>(); Widescreen& m = *m_h; m.init(*nes, err); m.unfogAnim = false; m.cloudShadow = false; m.lighting = false; m.hudOverlay = false; m.caveBox = true; m.setWater(true); m.fog = false;
        m.viewHeight = 440;
        setState(5, 0); frames(m, 8, aspect);
        const bool recorded = m.backdropWaterOk;
        setState(0xB, 0); nes->ram[0x11] = 0; nes->ram[0x13] = 6; frames(m, 3, aspect);
        CHECK(recorded && m.caveSession);
        CHECK(m.backdropRoomRect[2] > m.backdropRoomRect[0] && !m.backdropRoomPre.empty());                   // (the room's picture before pass B is kept too)
        Canvas b1 = m.backdrop; m.redoBackdropWater(b1);                                              // (no big water in this room: only checks that the redo runs on the real backdrop)
        CHECK(b1.w == m.backdrop.w && b1.h == m.backdrop.h);
    }
    // 27v2. The game's own picture of the screen gets the water pass (waves and reflections) when ANY of the two switches is on: End (reflections off) must not stop the waves there.
    {
        auto recordedWith = [&](bool refl, bool waves) {
            auto m_h = std::make_unique<Widescreen>(); Widescreen& m = *m_h; m.init(*nes, err); m.unfogAnim = false; m.cloudShadow = false; m.lighting = false; m.hudOverlay = false; m.caveBox = true; m.fog = false; m.viewHeight = 440;
            m.waterReflections = refl; m.waterWaves = waves;
            setState(5, 0); frames(m, 8, aspect);
            return m.backdropWaterOk;
        };
        CHECK(recordedWith(true, true)); CHECK(recordedWith(false, true)); CHECK(recordedWith(true, false)); CHECK(!recordedWith(false, false));
    }
    // 27x. The water behind a cave's box (waves, ripples, reflections) is made again every frame from the terrain and room picture of the moment before the water:
    // it equals what the pass makes at the later moment, and does nothing without the recorded data or with the water off.
    {
        const int M = Widescreen::kWaterMargin;
        Widescreen& a = w;
        paint(); const std::vector<uint32_t> pre = cv.pixels;
        a.waterTick = 100; run(); const std::vector<uint8_t> m0 = mask; const std::vector<uint32_t> at100 = cv.pixels;
        a.terrainMask.assign((size_t)cv.w * (cv.h + M), 0); std::copy(m0.begin(), m0.end(), a.terrainMask.begin() + (size_t)M * cv.w);
        a.backdropRawPre.assign((size_t)cv.w * (cv.h + M), kLand); std::copy(pre.begin(), pre.end(), a.backdropRawPre.begin() + (size_t)M * cv.w);
        a.backdropVis.assign((size_t)cv.w * (cv.h + M), 256); a.backdropFogScale = 256; a.backdropWaterOk = true; a.backdropRoomPre.clear();
        a.terrainW = cv.w; a.terrainH = cv.h; a.terrainCamX = a.backdropCamX = 0; a.terrainCamY = a.backdropCamY = 0; a.backdropTop = 0;
        for (int n : {1, 5, 30}) {
            paint(); a.waterTick = 100 + n; run(); const std::vector<uint32_t> truth = cv.pixels;
            a.waterTick = 100 + n - 1; Canvas c; c.w = cv.w; c.h = cv.h; c.pixels = at100; a.redoBackdropWater(c);
            long bad = 0, wet = 0;
            for (size_t i = 0; i < c.pixels.size(); i++) {
                if (m0[i] == 0) continue;
                wet++;
                int d = 0; for (int sh = 0; sh < 24; sh += 8) d = std::max(d, std::abs((int)(c.pixels[i] >> sh & 255) - (int)(truth[i] >> sh & 255)));
                bad += d > 6;
            }
            CHECK(bad * 200 < wet);                                                                       // (under 0.5% of the water pixels differ)
        }
        Canvas still; still.w = cv.w; still.h = cv.h; still.pixels = at100; a.waterTick = 100 + 49; a.redoBackdropWater(still);
        size_t moved = 0; for (size_t i = 0; i < still.pixels.size(); i++) moved += still.pixels[i] != at100[i]; CHECK(moved > 500);
        size_t landMoved = 0; for (size_t i = 0; i < still.pixels.size(); i++) landMoved += m0[i] == 0 && still.pixels[i] != at100[i]; CHECK(landMoved == 0);   // land is untouched
        a.setWater(false); Canvas c3; c3.w = cv.w; c3.h = cv.h; c3.pixels = at100; a.redoBackdropWater(c3); CHECK(c3.pixels == at100); a.setWater(true);
        a.backdropWaterOk = false; Canvas c5; c5.w = cv.w; c5.h = cv.h; c5.pixels = at100; a.redoBackdropWater(c5); CHECK(c5.pixels == at100);
    }

    // 27w. Water in a dungeon room: the moat tiles ($F4) reflect the back wall from its base, Link (and monsters, items) about their feet; no moat = untouched.
    {
        constexpr uint32_t kWallC = 0xFFD03020, kFloorC = 0xFF30A030, kMoat = 0xFF2040C0;
        auto dw_h = std::make_unique<Widescreen>(); Widescreen& dw = *dw_h; dw.init(*nes, err); dw.unfogAnim = false; dw.cloudShadow = false; dw.lighting = false; dw.fog = false; dw.hudOverlay = false; dw.caveBox = false;
        dw.dayClock.frozen = true;
        auto setTile = [&](int col, int row, uint8_t id) { nes->cart.cpuWrite((uint16_t)(0x6530 + col * 22 + row), id); };
        auto paintRoom = [&](bool moat) {
            for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++) {
                const bool floor = col >= 4 && col < 28 && row >= 4 && row < 18;
                const bool isMoat = moat && floor && !(col >= 14 && col < 18 && row < 7);          // a small island at the top
                setTile(col, row, isMoat ? 0xF4 : floor ? 0x24 : 0xE0);
                for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
                    nes->ppu.framebuffer[(size_t)(64 + row * 8 + y) * Ppu::W + col * 8 + x] = nes->ppu.bgColor[(size_t)(64 + row * 8 + y) * Ppu::W + col * 8 + x] = isMoat ? kMoat : floor ? kFloorC : kWallC;
            }
            for (int y = 0; y < Ppu::H; y++) for (int x = 0; x < Ppu::W; x++) if (y < 64) nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = 0xFF000000;
            gBg.assign(nes->ppu.framebuffer, nes->ppu.framebuffer + (size_t)Ppu::W * Ppu::H);      // (the room without sprites: what the PPU's background layer holds)
        };
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        auto isRed = [&](uint32_t p) { return p != kMoat && (p >> 16 & 255) > 0x30; };   // (a tinted blue: the wall's red mixed into it)
        auto canvasMoat = [&](Canvas& cc, int x0, int y0) { return cc.pixels[(size_t)(y0) * cc.w + x0]; };
        // no moat: the picture is exactly what the game drew
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        t.setState(5, 0x23, 1); paintRoom(false); t.frames(dw, 8, aspect);
        Canvas dry = c;
        // with the moat: below the wall's base the water shows the wall's reflection (reddish), far from the wall it stays blue
        t.setState(5, 0x23, 1); paintRoom(true); t.frames(dw, 8, aspect);
        const int left = (c.w - Ppu::W) / 2;
        size_t changed = 0; for (size_t i = 0; i < c.pixels.size(); i++) changed += c.pixels[i] != dry.pixels[i];
        CHECK(changed > 500);
        const int baseY = 64 + 32;                                                                // the first water row (the wall's base above it)
        bool nearRed = false, farRed = false;
        for (int y = baseY; y < baseY + 24; y++) for (int x = left + 40; x < left + 100; x++) nearRed |= isRed(canvasMoat(c, x, y));
        for (int y = baseY + 60; y < baseY + 100; y++) for (int x = left + 40; x < left + 100; x++) farRed |= isRed(canvasMoat(c, x, y));
        CHECK(nearRed && !farRed);                                                                // the reflection reaches a short way (40 px at most) below the base
        CHECK(c.pixels[(size_t)(baseY - 2) * c.w + left + 60] == kWallC);                         // the wall itself is not touched
        // Link on the island reflects about his feet: red below the island's bottom edge, in the water
        t.setState(5, 0x23, 1); nes->ram[kObjX] = 120; nes->ram[kObjY] = 100;
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        for (int y = 100; y < 116; y++) for (int x = 120; x < 136; x++) { nes->ppu.sprCol[y * Ppu::SPR_W + x] = 1; nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = 0xFFF0F0F0; }
        t.frames(dw, 4, aspect);
        bool white = false; for (int y = 64 + 56; y < 64 + 56 + 14; y++) for (int x = left + 124; x < left + 132; x++) { const uint32_t p = c.pixels[(size_t)y * c.w + x]; white |= (p >> 16 & 255) > 120 && (p >> 8 & 255) > 120 && p != kMoat; }
        CHECK(white);                                                                              // his mirror image hangs below the island
        {   // the sword swung: Link's reflection stays (the sword's own pixels reflect about Link's feet too)
            t.setState(5, 0x23, 1); nes->ram[kObjX] = 120; nes->ram[kObjY] = 100;
            nes->ram[kObjState + kSlotSword] = 1; nes->ram[kObjX + kSlotSword] = 120; nes->ram[kObjY + kSlotSword] = 116;
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            for (int y = 100; y < 116; y++) for (int x = 120; x < 136; x++) { nes->ppu.sprCol[y * Ppu::SPR_W + x] = 1; nes->ppu.sprOam[y * Ppu::SPR_W + x] = 18; nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = 0xFFF0F0F0; }
            for (int y = 116; y < 132; y++) for (int x = 127; x < 129; x++) { nes->ppu.sprCol[y * Ppu::SPR_W + x] = 1; nes->ppu.sprOam[y * Ppu::SPR_W + x] = 30; nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = 0xFFF0F0F0; }
            t.frames(dw, 4, aspect);
            bool w2 = false; for (int y = 64 + 56; y < 64 + 56 + 14; y++) for (int x = left + 121; x < left + 126; x++) { const uint32_t p = c.pixels[(size_t)y * c.w + x]; w2 |= (p >> 16 & 255) > 70 && p != kMoat; }
            bool w3 = false; for (int y = 64 + 60; y < 64 + 60 + 10; y++) for (int x = left + 120; x < left + 126; x++) { const uint32_t p = c.pixels[(size_t)y * c.w + x]; w3 |= (p >> 16 & 255) > 120 && (p >> 8 & 255) > 120 && p != kMoat; }
            (void)w3; CHECK(w2);   // (beside the sword's pixels)
            // the sword is copied, not mirrored (like a shot): the blade that points down is drawn again below Link's mirror image (it fades quickly) instead of folding up behind him
            bool w4 = false; for (int y = 64 + 70; y < 64 + 73; y++) for (int x = left + 126; x < left + 130; x++) { const uint32_t p = c.pixels[(size_t)y * c.w + x]; w4 |= (p >> 16 & 255) > 70 && (p >> 8 & 255) > 70 && p != kMoat; }
            CHECK(w4);
            nes->ram[kObjState + kSlotSword] = 0;
        }
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        // the overworld's water grid is not disturbed by the dungeon pass
        CHECK(!dw.wwDungeon);
        // A monster on the island (room x 64..79, y 36..51 = ObjY 100): its pixels are recorded on the last frame of play
        {
            const uint32_t kMon = Ppu::nesColor(0x16);
            t.setState(5, 0x23, 1); paintRoom(true);
            for (int y = 0; y < 22; y++) for (int x = 0; x < 32; x++) {}                       // (the island at the top: columns 14-17, rows 4-6 = x 112..143; the monster stands on it)
            for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }
            nes->ram[kObjType + 1] = 5; nes->ram[kObjState + 1] = 1; nes->ram[kObjX + 1] = 120; nes->ram[kObjY + 1] = 100;
            auto monster = [&] { std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
                for (int y = 100; y < 116; y++) for (int x = 120; x < 136; x++) { nes->ppu.sprCol[y * Ppu::SPR_W + x] = 0x16; nes->ppu.sprOam[y * Ppu::SPR_W + x] = 5; } };
            monster(); gScrollX = 0; t.frames(dw, 3, aspect);
            CHECK(dw.frozenOn && dw.frozenSpr.size() == 256);
            const int top = HUD_H;
            // the game hides the monster while the room scrolls right (mode 7): the old room slides left by 64 px, the monster goes with it
            t.setState(7, 0x24, 1); t.frames(dw, 1, aspect);                                   // (the picture lags a frame: this one still shows play)
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            t.frames(dw, 1, aspect);
            for (int k = 0; k < 16; k++) { gScrollX += 4; t.frames(dw, 1, aspect); }
            CHECK(dw.slideX == 64);
            CHECK(c.pixels[(size_t)(top + 40) * c.w + 120 - 64 + 4] == kMon);                  // ... frozen, scrolled with the room
            CHECK(c.pixels[(size_t)(top + 40) * c.w + 120 + 4] != kMon);                       // (not where it stood)
            bool refl = false; for (int y = top + 56; y < top + 56 + 14; y++) for (int x = 120 - 64 + 4; x < 120 - 64 + 12; x++) { const uint32_t p = c.pixels[(size_t)y * c.w + x]; refl |= p != kMoat && (p >> 16 & 255) > 0x40 && p != kWallC && p != kFloorC; }
            CHECK(refl);                                                                       // its reflection scrolls with it too
            const size_t sparkBefore = dw.sparkles.size(); (void)sparkBefore;
            // scrolled all the way out: nothing left of it
            for (int k = 0; k < 40; k++) { gScrollX += 4; t.frames(dw, 1, aspect); }
            bool any = false; for (int y = top; y < top + 176; y++) for (int x = 0; x < 256; x++) any |= c.pixels[(size_t)y * c.w + x] == kMon;
            CHECK(!any);
            // the stairs: the frozen monster stays where it was until the picture fades to black, then it is gone for good
            gScrollX = 0; t.setState(5, 0x23, 1); paintRoom(true); monster(); t.frames(dw, 3, aspect);
            t.setState(0x10, 0x24, 1); t.frames(dw, 1, aspect);
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            t.frames(dw, 2, aspect);
            CHECK(c.pixels[(size_t)(top + 40) * c.w + 120 + 4] == kMon);
            for (int y = 0; y < Ppu::H; y++) for (int x = 0; x < Ppu::W; x++) nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = 0xFF000000;
            t.frames(dw, 2, aspect);
            CHECK(dw.frozenDone);
            paintRoom(true); t.frames(dw, 2, aspect);                                         // the next room comes: no ghost of the monster
            { bool g = false; for (int y = top; y < top + 176; y++) for (int x = 0; x < 256; x++) g |= c.pixels[(size_t)y * c.w + x] == kMon; CHECK(!g); }
            auto hasColor = [&](uint32_t col) { for (int y = top; y < top + 176; y++) for (int x = 0; x < 256; x++) if (c.pixels[(size_t)y * c.w + x] == col) return true; return false; };
            auto clearSpr = [&] { std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H); };
            auto black = [&] { for (int y = 0; y < Ppu::H; y++) for (int x = 0; x < Ppu::W; x++) nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = 0xFF000000; gBg.assign(nes->ppu.framebuffer, nes->ppu.framebuffer + (size_t)Ppu::W * Ppu::H); };
            // mode 6 (the game hides the sprites a frame before the scroll): the monster stays, no one-frame gap
            t.setState(5, 0x23, 1); paintRoom(true); monster(); t.frames(dw, 3, aspect);
            t.setState(6, 0x23, 1); t.frames(dw, 1, aspect); clearSpr(); t.frames(dw, 2, aspect);
            CHECK(hasColor(kMon));
            // one empty frame of play (the game hiding its sprites early) does not wipe what was recorded; a real absence does after two frames
            t.setState(5, 0x23, 1); paintRoom(true); monster(); t.frames(dw, 3, aspect); clearSpr(); t.frames(dw, 1, aspect);
            CHECK(dw.frozenSpr.size() == 256);
            t.frames(dw, 3, aspect); CHECK(dw.frozenSpr.empty());
            // entering a cellar (mode 9): the old room fades with its monster; after the black the cellar's own bats are recorded and stay while it fades when Link climbs out (mode $A)
            t.setState(5, 0x23, 1); paintRoom(true); monster(); t.frames(dw, 3, aspect);
            t.setState(9, 0x23, 1); t.frames(dw, 1, aspect); clearSpr(); t.frames(dw, 2, aspect);
            CHECK(hasColor(kMon));
            black(); t.frames(dw, 3, aspect);                                                   // (black: the cellar comes)
            paintRoom(false); t.frames(dw, 2, aspect); CHECK(!hasColor(kMon));
            const uint32_t kBat = Ppu::nesColor(0x21);
            for (int y = 100; y < 108; y++) for (int x = 64; x < 72; x++) { nes->ppu.sprCol[y * Ppu::SPR_W + x] = 0x21; nes->ppu.sprOam[y * Ppu::SPR_W + x] = 7; nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = kBat; }
            t.frames(dw, 3, aspect);                                                            // (the cellar at rest: its bats are recorded)
            t.setState(0x0A, 0x23, 1); t.frames(dw, 1, aspect); clearSpr(); t.frames(dw, 2, aspect);
            CHECK(hasColor(kBat));                                                              // the bats stay while the cellar fades
            for (int y = 0; y < Ppu::H; y++) for (int x = 0; x < Ppu::W; x++) nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = 0xFF0A0A0A;
            gBg.assign(nes->ppu.framebuffer, nes->ppu.framebuffer + (size_t)Ppu::W * Ppu::H);
            t.frames(dw, 3, aspect); CHECK(!dw.frozenDone);                                      // (a nearly black picture is not black yet: the replay goes on)
            black(); for (int y = 70; y < 230; y += 9) for (int x = 10; x < 250; x += 9) nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = 0xFF0C0C0C;   // a few faint bricks: the picture is still there
            gBg.assign(nes->ppu.framebuffer, nes->ppu.framebuffer + (size_t)Ppu::W * Ppu::H);
            t.frames(dw, 3, aspect); CHECK(!dw.frozenDone);                                      // (not black: the bats are still part of it, however faint)
            black(); t.frames(dw, 3, aspect); CHECK(dw.frozenDone);
            paintRoom(true); t.frames(dw, 2, aspect); CHECK(!hasColor(kBat) && !hasColor(kMon));
            t.setState(5, 0x23, 1); for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }
            clearSpr(); gScrollX = 0;
        }
        dw.setWater(false); t.setState(5, 0x23, 1); paintRoom(true); t.frames(dw, 4, aspect);
        CHECK(c.pixels[(size_t)(baseY + 8) * c.w + left + 60] == kMoat);                          // waterFx off: nothing
        gBg.clear(); nes->ram[kCurLevel] = 0;
    }
    w.lightPpu = nullptr;
}
