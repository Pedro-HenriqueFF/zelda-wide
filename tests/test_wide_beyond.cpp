// Section 35: the world beyond the map's edge (extended terrain, sea, hand-made rooms, fade, waves).
#include <memory>
#include <cstdio>
#include "test_wide_common.h"
#include "widescreen_internal.h"
#include "beyond.h"
#include "zelda_ram.h"

using namespace wd;
using namespace zelda;

void section35(Ctx& t) {
    SECTION_BINDINGS
    constexpr int R = OverworldMap::TILE_ROWS;
    // 35a. The file: legend, patches, comments, and every kind of mistake leaves the old state alone.
    {
        beyond::File f; std::string err;
        CHECK(beyond::parse("# only a comment\n\n", f, err) && f.legend.count('~') && f.legend.count('M') && f.legend.count('T') && f.legend.count('.'));
        CHECK(beyond::parse("legend W = 8E pal 2   # one tile\nlegend X = D8 DA D9 DB pal 1\n# c\nbelow 75 rows 2\nW~..XXXXMMMMTTTT\n~~~~~~~~~~~~~~~~\n", f, err));
        CHECK_EQ(f.rows[1], 2); CHECK_EQ((int)f.grid[1][0], (int)'W'); CHECK_EQ((int)f.grid[1][16], (int)'~');
        CHECK(f.legend['W'].id[0] == 0x8E && f.legend['W'].id[3] == 0x8E && f.legend['W'].pal == 2);
        CHECK(f.legend['X'].id[1] == 0xDA && f.legend['X'].id[2] == 0xD9 && f.legend['X'].pal == 1);
        const char* bad[] = {"frob\n", "legend ab = 26 pal 0\n", "legend Q = 26 27 pal 0\n", "legend Q = 26 pal 4\n", "legend Q = 1FF pal 0\n", "legend Q = 26\n",
                             "below 73 rows 1\n................\n", "below 75 rows 1\n.....\n", "below 75 rows 1\n????????????????\n", "below 75 rows 3\n................\n",
                             "below 75 rows 21\n", "below 7C rows 1\n................\n"};
        for (const char* b : bad) { beyond::File g = f; std::string e; CHECK(!beyond::parse(b, g, e) && !e.empty() && g.rows == f.rows && g.legend.size() == f.legend.size()); }
        std::string e; beyond::File g; CHECK(!beyond::parse("# a\n\nlegend Q = 26 pal 9\n", g, e) && e.rfind("line 3:", 0) == 0);      // (the line number counts comments and blanks)
        CHECK(beyond::parse(beyond::templateText(), g, e) && g.rows[1] == 0);                                                       // (the template is valid and has no patch)
    }
    // 35b. Zones and fade.
    {
        using beyond::Zone;
        const beyond::Fade f1 = beyond::fadeFor(1);
        auto fv = [&](int x, int y) { return beyond::fadeVis(x, y, f1); };
        CHECK(beyond::zoneOf(100, 100) == Zone::Inside && beyond::zoneOf(0, 0) == Zone::Inside && beyond::zoneOf(511, 175) == Zone::Inside);
        CHECK(beyond::zoneOf(-1, -1) == Zone::Extrude && beyond::zoneOf(-1, 5) == Zone::Extrude && beyond::zoneOf(10, -1) == Zone::Extrude && beyond::zoneOf(447, -1) == Zone::Extrude);
        CHECK(beyond::zoneOf(448, -1) == Zone::Sea && beyond::zoneOf(512, -5) == Zone::Sea && beyond::zoneOf(512, 50) == Zone::Sea && beyond::zoneOf(600, 300) == Zone::Sea);
        CHECK(beyond::zoneOf(100, 176) == Zone::Extrude && beyond::zoneOf(-5, 200) == Zone::Extrude && beyond::zoneOf(127, 180) == Zone::Extrude);
        CHECK(beyond::zoneOf(128, 176) == Zone::Custom && beyond::zoneOf(383, 200) == Zone::Custom && beyond::zoneOf(384, 176) == Zone::Sea && beyond::zoneOf(511, 176) == Zone::Sea);
        CHECK(fv(100, 100) == 256 && fv(-1, 500) >= 250 && fv(-256, 500) == 0 && fv(-400, 500) == 0);      // land: gone after one screen
        CHECK(fv(4096 + 100, 600) > 220 && fv(4096 + 896, 600) == 0 && fv(4096 + 600, 600) > 0);                     // sea: 3.5 screens
        int prev = 257; bool mono = true; for (int k = 0; k < 20; k++) { const int v = fv(4096 + k * 48, 600); mono = mono && v <= prev; prev = v; }
        CHECK(mono);
        CHECK(fv(4096 + 300, 1408 + 200) < fv(4096 + 300, 600));                                                                    // (a corner is farther than the side)
        CHECK(fv(300, -100) < fv(300, -20));                                                                                         // north
        // the three configurations: land 0.5 / 1 screens, sea 3.5, and none
        const beyond::Fade f0 = beyond::fadeFor(0), f2 = beyond::fadeFor(2);
        CHECK(beyond::fadeVis(-130, 500, f0) == 0 && beyond::fadeVis(-130, 500, f1) > 0 && beyond::fadeVis(-130, 500, f2) == 256);
        CHECK(beyond::fadeVis(4096 + 700, 600, f0) == beyond::fadeVis(4096 + 700, 600, f1) && beyond::fadeVis(4096 + 700, 600, f0) > 0 && beyond::fadeVis(4096 + 5000, 600, f2) == 256);   // (the sea is 3.5 screens in both, none in the third)
        CHECK(beyond::fadeVis(100, 100, f0) == 256 && beyond::fadeFor(7).land == f1.land && std::string(beyond::fadeName(2)).find("no fade") == 0);
        CHECK(fv(100, 1408 + 100) < 256 && fv(100, 1408 + 100) > 0);
    }
    // 35c. The band of tiles: the border's own tiles repeated in step with their squares, sea in the east, hand-made rooms.
    auto outerOf = [&](int room) { return (int)(nes->cart.cpuRead((uint16_t)(kAttrsA + room)) & 3); };
    auto w_h = std::make_unique<Widescreen>(); Widescreen& w = *w_h; w.init(*nes, err); w.unfogAnim = false; w.cloudShadow = false; w.lighting = false;
    w.fxNes = nes.get(); w.fxRoom = 0; w.fxSlot = 0; w.fxOx = 0; w.fxOy = 0;
    w.ensureBeyond(*nes);
    auto ringId = [&](int tx, int ty) { const uint16_t v = w.ringTileAt(tx, ty); return v == 0xFFFF ? -1 : (int)(v & 0xFF); };
    auto ringPal = [&](int tx, int ty) { const uint16_t v = w.ringTileAt(tx, ty); return v == 0xFFFF ? -1 : (int)(v >> 8); };
    {
        const uint8_t* r00 = w.roomTilesFor(*nes, 0x00); const uint8_t* r70 = w.roomTilesFor(*nes, 0x70);
        CHECK(ringId(-1, 5) == r00[1 * R + 5] && ringId(-2, 5) == r00[0 * R + 5] && ringPal(-1, 5) == outerOf(0x00));                   // west: columns 1, 0, 1, 0 ... of room 00
        CHECK(ringId(-3, 5) == r00[1 * R + 5] && ringId(-40, 8) == r00[0 * R + 8]);
        CHECK(ringId(10, -1) == r00[10 * R + 1] && ringId(10, -2) == r00[10 * R + 0] && ringId(10, -7) == r00[10 * R + 1]);              // north: rows 1, 0, 1 ...
        CHECK(ringId(-1, -1) == r00[1 * R + 1] && ringId(-2, -2) == r00[0 * R + 0]);                                                      // the corner
        CHECK(ringId(10, 176) == r70[10 * R + 20] && ringId(10, 178) == r70[10 * R + 20]);                                                  // south (room 70): rows 20, 21, 20 ...
        CHECK(ringId(10, 177) == r70[10 * R + 21] && ringPal(10, 177) == outerOf(0x70));
        CHECK(ringId(-5, 190) == r70[1 * R + 20] && ringId(-4, 191) == r70[0 * R + 21]);                                                    // the south west corner
        CHECK(ringId(515, 40) == 0x90 && ringId(515, 41) == 0x95 && ringPal(515, 40) == 3);                                               // east: sea, even rows $90, odd rows $95
        CHECK(ringId(515, -4) == 0x90 && ringId(515, 200) == 0x90 && ringId(14 * 32 + 3, -4) == 0x90 && ringId(14 * 32 + 3, -3) == 0x95);  // the corners and above the two top right screens
        CHECK(ringId(13 * 32 + 3, -4) == w.roomTilesFor(*nes, 0x0D)[3 * R + 0]);                                                          // (above column 13: its own terrain goes on, not sea)
        CHECK(ringId(12 * 32 + 3, 180) == 0x90 && ringId(12 * 32 + 3, 181) == 0x95);                                                       // below columns 12-15: sea
        CHECK(ringId(5 * 32 + 3, 190) == 0x90);                                                                                           // below the river screen with no patch: sea
        CHECK(ringId(0, 0) == -1 && ringId(100, 100) == -1 && ringId(-kRingL - 1, 5) == -1 && ringId(GW + kRingR, 5) == -1 && ringId(5, GH + kRingB) == -1);   // (inside the map and past the band: no data)
    }
    // 35d. Hand-made rooms under rooms 75 and 76; a bad file changes nothing.
    {
        const std::string text = "legend X = 11 12 13 14 pal 1\nlegend R = 90 pal 3\nbelow 75 rows 2\nX~~~~~~~~~~~~~~~\n................\nbelow 76 rows 3\n......R.........\n......R.........\n......R.........\n";
        CHECK(w.loadBeyond(text).empty()); w.ensureBeyond(*nes);
        CHECK(ringId(160, 176) == 0x11 && ringId(161, 176) == 0x12 && ringId(160, 177) == 0x13 && ringId(161, 177) == 0x14 && ringPal(160, 176) == 1);   // the first square of room 75's patch
        CHECK(ringId(162, 176) == 0x90 && ringPal(162, 176) == 3 && ringId(162, 177) == 0x95);                                                           // '~'
        CHECK(ringId(160, 178) == 0x26 && ringId(160, 180) == 0x90 && ringId(160, 181) == 0x95);                                                         // '.' ground, then the sea where the patch ends
        CHECK(ringId(5 * 32 + 3 + 32, 176) == 0x26 && ringId(6 * 32 + 12, 176) == 0x90);                                                                 // room 76: ground and a river square
        const uint8_t* r73 = w.roomTilesFor(*nes, 0x73);
        CHECK(ringId(100, 177) == r73[(100 % 32) * R + 21]);                                                                                             // (room 73 is not in the patch range: its terrain goes on)
        CHECK(!w.loadBeyond("below 75 rows 1\nZZZZZZZZZZZZZZZZ\n").empty()); w.ensureBeyond(*nes);
        CHECK(ringId(160, 176) == 0x11);                                                                                                                  // (the old file is kept)
        // water flags: the river is water but narrow; the sea is water and big; the ground is neither
        bool known = false;
        const uint8_t river = w.gridKind(6 * 32 + 12, 176 + 1, &known), sea = w.gridKind(GW + 20, 88, &known), ground = w.gridKind(160, 178);
        CHECK(known && (river & kWater) && !(river & kBig) && (sea & kWater) && (sea & kBig) && !(ground & kWater));
        w.ensureWaterWorld();
        CHECK(w.wwKind.size() == (size_t)GW * GH && (w.gridKind(GW + 1, 5) & kWater) && (w.gridKind(100, 100) == w.wwKind[(size_t)100 * GW + 100]) && w.gridKind(-kRingL - 5, 5, &known) == 0 && !known);
    }
    // 35e. Waves and reflections on the extended sea: the water pass sees the sea beyond the map as big water.
    {
        w.fog = false; w.setWater(true); w.wwReady = false;
        constexpr uint32_t kLand = 0xFF20A020, kSea = 0xFF2040C0;
        Canvas cv; cv.w = 256; cv.h = 200; const int camX = GW * 8 - 64, camY = 40 * 8;
        cv.pixels.assign((size_t)cv.w * cv.h, kLand);
        for (int y = 0; y < cv.h; y++) for (int x = 0; x < cv.w; x++) cv.pixels[(size_t)y * cv.w + x] = (w.gridKind((camX + x) >> 3, (camY + y) >> 3) & kWater) ? kSea : kLand;
        std::vector<uint8_t> mask;
        Widescreen::WaterJob j{cv.pixels.data(), cv.w, cv.h, 0, camX, camY, 0, 0, cv.w, cv.h, &mask, true, false}; w.waterPass(j);
        CHECK_EQ((int)mask[(size_t)100 * cv.w + 200], 2);                                                                                  // deep in the sea beyond the east edge: big water (waves)
        CHECK(mask.size() == (size_t)cv.w * cv.h);
    }
    // 35f. The picture: the extension replaces the fog beyond the border and only there.
    {
        const bool fog0 = ws.fog, cloud0 = ws.cloudShadow, light0 = ws.lighting;
        ws.fog = false; ws.cloudShadow = false; ws.lighting = false;                                      // (what is left of the fog is only what lies beyond the map)
        auto shot = [&](bool ext) { ws.extendWorld = ext; frames(ws, 3, aspect); return c.pixels; };
        setState(5, 0x7F); const std::vector<uint32_t> off = shot(false), on = shot(true);
        const size_t row = (size_t)100 * c.w;
        const int x0 = GW * 8 - (ws.cameraX() - c.w / 2);                                                   // the map's east edge in the canvas (Link in room 7F)
        CHECK(x0 > 32 && x0 + 4 < c.w);
        bool beyondFog = true, shown = true, inside = true;
        for (int x = x0; x < c.w; x++) beyondFog = beyondFog && isFog(off[row + x]);                       // (without the extension everything beyond the edge is deep fog)
        for (int x = x0; x < x0 + 4; x++) shown = shown && on[row + x] != off[row + x] && on[row + x] != 0xFF000000;   // (the fixture's palette is dark: it is the difference that counts)
        for (int x = 0; x < x0 - 16; x++) inside = inside && on[row + x] == off[row + x];                  // (the screen itself is the same; the last tiles by the new shore may differ)
        CHECK(beyondFog && shown && inside);
        setState(5, 0x00); const std::vector<uint32_t> off0 = shot(false), on0 = shot(true);
        CHECK(isFog(off0[row]) && on0[row] != off0[row] && on0[row] != 0xFF000000);                            // west of room 00: mountains / trees go on
        // the toggle: 136 px west of room 00 the land is gone with a half-screen fade, still there with a screen, and not faded at all in the third
        uint32_t px3[3];
        for (int mode = 0; mode < 3; mode++) { ws.fadeMode = mode; frames(ws, 3, aspect); px3[mode] = c.pixels[row]; }
        CHECK(isFog(px3[0]) && px3[1] != off0[row] && px3[2] != off0[row] && px3[2] != px3[1]);
        ws.fadeMode = 1;
        ws.extendWorld = true; ws.fog = fog0; ws.cloudShadow = cloud0; ws.lighting = light0;
    }
}
