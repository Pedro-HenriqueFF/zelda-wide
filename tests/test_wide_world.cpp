// Widescreen integration tests: the world view, camera, fog of war, zoom, the glide between screens, title/dungeon pictures.
// Sections run in numeric order (see test_wide.cpp) on one shared Ctx: later sections rely on state (fog discovered,
// palettes, RAM) left by earlier ones, so do not reorder them.
#include <string>
#include <memory>
#include <cstdio>
#include "test_wide_common.h"
#include "../src/widescreen_internal.h"

void section01(Ctx& t) {
    SECTION_BINDINGS
    // 1. World view in room $23, Link at (120,141): camera centre = room origin + (128, 85).
    //    So canvas column 0 is world x = ox-128, canvas row 64 is world y = oy-3, and the game's own
    //    screen sits at canvas (128, 67).
    setState(5, 0x23);
    frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240);
    CHECK_EQ(px(128 + 10, 10), 0xFF654321);                  // status bar, centred (x=128..383)
    CHECK_EQ(px(128 + 10, 72), 0xFF123456);                  // the game's own picture
    CHECK_EQ(px(383, 239), 0xFF123456);
    CHECK_EQ(px(127, 100) == 0xFF123456, false);
    CHECK(isFog(px(5, 100)));                                // west neighbour unexplored
    CHECK(isFog(px(128 + 10, 65)));                          // 3 rows of the screen above, unexplored
    CHECK_EQ(px(10, 10), 0xFF000000);                        // status-bar margins stay black
}

void section02(Ctx& t) {
    SECTION_BINDINGS
    // 2. Visit the west neighbour ($22) and come back: its terrain now shows next to the game's picture.
    //    canvas (127,100) -> world (ox-1, oy+33): room $22 local (255,33): col 31 (odd), row 4 (even) -> tile 5
    //    -> colour 3, outer palette 1 -> palette[7]
    setState(5, 0x22); frames(ws, 2, aspect);
    setState(5, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(px(127, 100), Ppu::nesColor(0x21));
    CHECK(isFog(px(450, 100)));                              // east neighbour ($24) still unexplored: deep fog, away from the border
}

void section03(Ctx& t) {
    SECTION_BINDINGS
    // 3. Fog off reveals everything. canvas (385,100) -> room $24 local (1,33): col 0, row 4 -> tile 3 -> colour 1
    //    -> outer palette 1 -> palette[5]
    ws.fog = false; frames(ws, 1, aspect);
    CHECK_EQ(px(385, 100), Ppu::nesColor(0x2A));
    ws.fog = true;
}

void section04(Ctx& t) {
    SECTION_BINDINGS
    // 4. The camera follows Link: moving him right by 40 pixels shifts the world left by 40 on the canvas.
    //    (The game's picture block moves too; Link's own pixels come from it.)
    nes->ram[0x70] = 160; frames(ws, 2, aspect);
    CHECK_EQ(px(128 - 40 + 10, 72), 0xFF123456);             // block now starts at x = 88
    CHECK_EQ(px(128 - 40 - 1, 100) != 0xFF123456, true);
    nes->ram[0x70] = 120;

    // 4b. Jumps are eased: when the game moves Link by 40 pixels at once, the camera takes a few frames to catch up.
    setState(5, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(px(128 + 10, 72), 0xFF123456);                  // block at x=128
    nes->ram[0x70] = 160;
    syncFb(*nes); ws.render(*nes, aspect, c); ws.render(*nes, aspect, c);  // one frame of lag, then the jump is seen
    CHECK_EQ(px(128 - 10 + 5, 72), 0xFF123456);              // first eased step: camera moved 10 of the 40 pixels
    CHECK_EQ(px(128 - 10 - 1, 100) == 0xFF123456, false);
    for (int k = 0; k < 30; k++) { syncFb(*nes); ws.render(*nes, aspect, c); }
    CHECK_EQ(px(128 - 40 + 5, 72), 0xFF123456);              // converged
    CHECK_EQ(px(128 - 40 - 1, 100) == 0xFF123456, false);
    nes->ram[0x70] = 120;
}

void section05(Ctx& t) {
    SECTION_BINDINGS
    // 5. World edge: beyond room $00's left edge the mountains/trees go on (and fade into the deep fog); without the extension the deep fog is all there is, with F2 on or off.
    setState(5, 0x00); ws.fadeMode = 1; frames(ws, 2, aspect);                                    // (land fades over a screen in setting 1: the pixel is 136 px beyond the edge)
    CHECK(px(0, 100) != 0xFF000000 && !isFog(px(0, 100)));
    ws.extendWorld = false; frames(ws, 2, aspect);
    CHECK(px(0, 100) != 0xFF000000 && isFog(px(0, 100)));
    CHECK(isFog(px(0, HUD_H)) && isFog(px(3, HUD_H + 1)));      // (the corner of the playfield, below the status bar)
    ws.fog = false; frames(ws, 2, aspect);
    CHECK(isFog(px(0, 100)));
    ws.fog = true; ws.extendWorld = true; ws.fadeMode = 0; frames(ws, 2, aspect);
    // The Delete key's dump of the map's borders: 16 + 16 + 8 + 8 screens, each with the ids of its three outermost tile lines.
    {
        const char* path = "zw_edge_dump_test.txt";        // (a relative name: Windows has no /tmp)
        CHECK(ws.dumpEdges(path));
        std::string txt; { FILE* f = std::fopen(path, "r"); CHECK(f != nullptr); if (f) { char b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) txt.append(b, n); std::fclose(f); } }
        int screens = 0; for (size_t p = txt.find("screen column"); p != std::string::npos; p = txt.find("screen column", p + 1)) screens++;
        CHECK_EQ(screens, 48);
        auto ids = [&](const char* header, int line) {                         // the first id of "line N ids:" under a header
            const size_t h = txt.find(header); if (h == std::string::npos) return -1;
            const std::string key = "line " + std::to_string(line) + " ids: ";
            const size_t p = txt.find(key, h); if (p == std::string::npos) return -1;
            return (int)std::strtol(txt.c_str() + p + key.size(), nullptr, 16);
        };
        std::remove(path);
        const int R = OverworldMap::TILE_ROWS;
        const uint8_t* nw = ws.roomTilesFor(*nes, 0x00); const uint8_t* w = ws.roomTilesFor(*nes, 0x10); const uint8_t* e = ws.roomTilesFor(*nes, 0x2F); const uint8_t* so = ws.roomTilesFor(*nes, 0x75);
        CHECK_EQ(ids("NORTH screen column 0 row 0 (room 00)", 0), (int)nw[0 * R + 0]);
        CHECK_EQ(ids("NORTH screen column 0 row 0 (room 00)", 2), (int)nw[0 * R + 2]);
        CHECK_EQ(ids("WEST screen column 0 row 1 (room 10)", 1), (int)w[1 * R + 0]);        // (the second column from the west edge, its first row)
        CHECK_EQ(ids("EAST screen column 15 row 2 (room 2F)", 0), (int)e[31 * R + 0]);
        CHECK_EQ(ids("SOUTH screen column 5 row 7 (room 75)", 1), (int)so[0 * R + (R - 2)]);
    }
}

void section08(Ctx& t) {
    SECTION_BINDINGS
    // 8. Title screen etc.: the plain, untouched frame. Dungeons: the walk-in keeps the overworld size, steady
    //    play uses the big picture, room-to-room scrolls inside keep it, and leaving the dungeon is drawn with
    //    the overworld view as soon as the overworld screen is back.
    setState(0, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240); CHECK_EQ(px(5, 100), 0xFF123456);
    setState(5, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512);
    // Entering a dungeon: Link walks down the entrance stairs while the game already says "level 3", but the PPU
    // still shows the overworld screen -> keep the world around it, at the overworld size.
    setState(5, 0x23); frames(ws, 2, aspect);
    {
        OverworldMap m2; std::string e2; m2.init(nes->cart.prgData(), e2);
        for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
            nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, decoded[col * 22 + row]);   // the PPU shows this screen
    }
    setState(0x10, 0x23, 3); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(px(127, 100), Ppu::nesColor(0x21)); CHECK_EQ(px(128 + 10, 72), 0xFF123456);
    for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
        nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, 0xF4);                          // now the PPU shows something else
    frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(px(5, 100), 0xFF000000); CHECK_EQ(px(128 + 10, 70), 0xFF123456);   // boxed, same size
    setState(2, 0x23, 3); frames(ws, 2, aspect);             // load level: already the big dungeon picture
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    setState(3, 0x23, 3); frames(ws, 2, aspect);             // the curtain opens on the dungeon room: big
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    setState(4, 0x23, 3); frames(ws, 2, aspect);             // Link steps into the dungeon room
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    setState(5, 0x23, 3); frames(ws, 2, aspect);             // steady inside the dungeon
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    setState(7, 0x23, 3); frames(ws, 2, aspect);             // dungeon room scroll
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    setState(0x10, 0x23, 3); frames(ws, 2, aspect);          // taking the exit stairs
    CHECK_EQ(c.w, 256);

    // Leaving a dungeon: the game switches to level 0 + exit type 2, loads (mode 2), opens the curtain on the
    // OVERWORLD screen (mode 3) and Link steps out (mode 4). The overworld view starts with the curtain.
    setState(2, 0x45, 0); nes->ram[0x5A] = 2; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240);
    //   The screen comes from our decode and the camera sits on the spot where Link will appear: attrs A=$01 (x=0) and
    //   attrs F=0 (row 0) -> Link at (0, $4D). Room $45's origin is (ox, oy): canvas column 0 is world x ox-248 and canvas
    //   row 64 is world y oy-67, so the room's top-left corner is at canvas (248, 131).
    setState(3, 0x45, 0); nes->ram[0x5A] = 2; nes->ram[0x70] = 0; nes->ram[0x84] = 0; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512);
    CHECK_EQ(px(248 + 10, 131 + 5), Ppu::nesColor(0x21));    // fully open: scenery all over the wide picture
    CHECK(isFog(px(5, 131 + 5)) || px(5, 131 + 5) == Ppu::nesColor(0x21));
    //   Half open: only the middle of the WHOLE picture (not just of the entrance screen) is visible.
    for (int y = 64; y < 240; y++) for (int x = 0; x < 256; x++)
        nes->ppu.framebuffer[y * 256 + x] = (x >= 100 && x < 156) ? 0xFF123456 : 0xFF000000;     // a 56 px wide opening
    frames(ws, 2, aspect);
    CHECK_EQ(px(256, 136), Ppu::nesColor(0x21));             // inside the opening [200, 312)
    CHECK_EQ(px(150, 136), 0xFF000000); CHECK_EQ(px(330, 136), 0xFF000000); CHECK_EQ(px(5, 136), 0xFF000000);
    {   // at night (and with lights) the closed part of the curtain stays pure black: the lighting only shows where the screen is open
        const bool wasL = ws.lighting, wasF = ws.dayClock.frozen; const double wasP = ws.dayClock.phase;
        ws.lighting = true; ws.dayClock.frozen = true; ws.dayClock.phase = 0.75;
        frames(ws, 3, aspect);
        for (int y = -240; y <= 240; y += 2) for (int x = -300; x <= 300; x += 2)    // sparkles all around Link: none may show on the closed curtain
            ws.sparkles.push_back({ws.lightLinkX + x, ws.lightLinkY + y, 0.0f, ws.dayClock.tick, 1000, 2});
        frames(ws, 1, aspect);
        CHECK_EQ(px(150, 136), 0xFF000000); CHECK_EQ(px(5, 136), 0xFF000000); CHECK_EQ(px(330, 136), 0xFF000000);
        // a black picture while the game loads (mode 2) in its box: the blue halo of an armos tile must not light it
        const std::vector<uint32_t> fbSave(nes->ppu.framebuffer, nes->ppu.framebuffer + Ppu::W * Ppu::H);
        setState(2, 0x45, 0); nes->ram[0x5A] = 2;
        for (int i = 0; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF000000;
        ws.lightSources.clear(); ws.lightSources.push_back({256.0f, 136.0f, 60.0f, 1.0f, 0.5f, 70, 130, 255});
        { Canvas cc; cc.w = 512; cc.h = 240; cc.pixels.assign((size_t)512 * 240, 0xFF000000); ws.applyNightLight(cc, 64, 0, 0);
          CHECK_EQ(cc.pixels[(size_t)136 * 512 + 256], 0xFF000000); }
        std::copy(fbSave.begin(), fbSave.end(), nes->ppu.framebuffer); setState(3, 0x45, 0); nes->ram[0x5A] = 2;
        ws.lighting = wasL; ws.dayClock.frozen = wasF; ws.dayClock.phase = wasP;
    }
    for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF123456;
    setState(4, 0x45, 0); nes->ram[0x5A] = 2; frames(ws, 2, aspect);   // stepping out of the entrance
    CHECK_EQ(c.w, 512);
    setState(5, 0x45, 0); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512);
    //   Starting a game or continuing after death (level 0, exit type 0): the curtain opens the whole wide picture at
    //   the overworld size, with Link already standing at the start position. While the game sets the screen up
    //   (mode 2) there is only black.
    setState(2, 0x77, 0); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240); CHECK_EQ(px(5, 100), 0xFF000000);
    setState(3, 0x77, 0); frames(ws, 2, aspect);             // Link at (120, 141): camera centre = room origin + (128, 85)
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240);
    CHECK_EQ(px(128 + 10, 67 + 5), Ppu::nesColor(0x21));     // scenery all over the picture (the room's corner is at (128, 67))
    for (int y = 64; y < 240; y++) for (int x = 0; x < 256; x++)
        nes->ppu.framebuffer[y * 256 + x] = (x >= 100 && x < 156) ? 0xFF123456 : 0xFF000000;
    frames(ws, 2, aspect);                                   // a 56 px opening -> [200, 312) on the wide picture
    CHECK_EQ(px(256, 72), Ppu::nesColor(0x2A)); CHECK_EQ(px(150, 72), 0xFF000000); CHECK_EQ(px(330, 72), 0xFF000000);
    for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF123456;
    setState(4, 0x77, 0); frames(ws, 2, aspect);             // then Link plays on
    CHECK_EQ(c.w, 512);
    CHECK_EQ(px(128 + 10, 72), 0xFF123456);
    setState(4, 0x23, 0); frames(ws, 2, aspect);             // (a screen we have visited)
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240);
    setState(5, 0x23); ws.enabled = false; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    ws.enabled = true;
    setState(5, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512);

    // 8b. Brief "video off" frames (the game fading while it loads a screen) don't flash black:
    //     the last picture stays up for a few frames.
    nes->ppu.lineMask[100] = 0;
    frames(ws, 1, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(px(128 + 10, 72), 0xFF123456); CHECK_EQ(px(127, 100), Ppu::nesColor(0x21));
    for (int k = 0; k < 10; k++) nes->ppu.lineMask[100] = 0, frames(ws, 1, aspect);
    CHECK_EQ(c.w, 512);                                      // eventually the picture is allowed to go dark, same size
    nes->ppu.lineMask[100] = 0x1E; frames(ws, 2, aspect);
}

void section09(Ctx& t) {
    SECTION_BINDINGS
    // 9. Discovery persists through a file, keyed by player name.
    setState(5, 0x23); frames(ws, 2, aspect);
    const char* path = "test_fog.tmp";
    std::remove(path);
    ws.saveFogIfDirty(path);
    {
        auto probe_h = std::make_unique<Widescreen>(); Widescreen& probe = *probe_h; probe.init(*nes, err);
        CHECK(!probe.loadFog(path));                         // nothing is written before the game itself saves
    }
    {   // malformed fog files are refused (no crash, nothing half-loaded): empty, wrong magic, cut short
        const char* bad = "test_fog_bad.tmp";
        const std::string good = "ZWF1" + std::string(3 * 24, '\x55');
        for (const std::string& content : {std::string(), std::string("XYZW") + good.substr(4), good.substr(0, 4), good.substr(0, 30), good.substr(0, good.size() - 1)}) {
            { std::ofstream o(bad, std::ios::binary | std::ios::trunc); o.write(content.data(), (std::streamsize)content.size()); }
            auto probe_h = std::make_unique<Widescreen>(); Widescreen& probe = *probe_h; probe.init(*nes, err);
            CHECK(!probe.loadFog(bad));
        }
        std::remove(bad);
    }
    // The game's save (mode $0D, submode 2 = done) commits the fog; only then the file is written.
    setState(0x0D, 0x23); nes->ram[0x13] = 2; frames(ws, 2, aspect);
    setState(5, 0x23); nes->ram[0x13] = 0; frames(ws, 2, aspect);
    ws.saveFogIfDirty(path);
    {
        auto ws2_h = std::make_unique<Widescreen>(); Widescreen& ws2 = *ws2_h; ws2.init(*nes, err); ws2.unfogAnim = false; ws2.cloudShadow = false; ws2.lighting = false; ws2.viewHeight = 176; ws2.hudOverlay = false; ws2.caveBox = false;
        CHECK(ws2.loadFog(path));
        frames(ws2, 2, aspect);                              // same name in RAM -> keeps discovered rooms
        CHECK_EQ(px(127, 100), Ppu::nesColor(0x21));         // room $22 still discovered
        nes->ram[0x638] = 'X';                               // different player name = new game -> fog resets
        frames(ws2, 2, aspect);
        CHECK(isFog(px(60, 100)));                           // (deep inside the fogged room)
        nes->ram[0x638] = 0;
    }
    std::remove(path);
}

void section10(Ctx& t) {
    SECTION_BINDINGS
    // 10. Bigger view (default 448 world pixels): canvas = status bar + 448, wide windows get wider.
    {
        auto z_h = std::make_unique<Widescreen>(); Widescreen& z = *z_h; z.init(*nes, err); z.unfogAnim = false; z.cloudShadow = false; z.lighting = false; z.hudOverlay = false; z.caveBox = false;
        CHECK_EQ(z.viewHeight, 448);
        setState(5, 0x23); frames(z, 2, 16.0 / 9.0);
        CHECK_EQ(c.h, 64 + 448);                             // 512
        CHECK_EQ(c.w, 910);                                  // 512 * 16/9, kept even
        const int zl = (c.w - 256) / 2;                      // 327
        CHECK_EQ(px(zl + 10, 10), 0xFF654321);
        // Camera centre = (ox+128, oy+85); viewport top = cy-224 -> the game's block starts at canvas row 64+224-85 = 203
        CHECK_EQ(px(zl + 10, 203 + 5), 0xFF123456);
        CHECK(isFog(px(zl + 10, 150)));                          // the screen above
        CHECK(isFog(px(zl + 10, 505)));                          // the screen below
        // Cave (mode $B, updating) at this size: the big picture. With interiorsBig off: box centred vertically in
        // the 448 px viewport -> starts at row 64 + (448-176)/2 = 200
        setState(0xB, 0x23); frames(z, 2, 16.0 / 9.0);
        CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
        z.interiorsBig = false; frames(z, 2, 16.0 / 9.0);
        CHECK_EQ(c.h, 512);
        CHECK_EQ(px(zl + 10, 205), 0xFF123456);
        z.interiorsBig = true;
        setState(5, 0x23); frames(z, 2, 16.0 / 9.0);

        // Zoom keys
        z.adjustZoom(-16); CHECK_EQ(z.viewHeight, 432);
        z.adjustZoom(-1000); CHECK_EQ(z.viewHeight, 176);
        z.adjustZoom(+5000); CHECK_EQ(z.viewHeight, 704);
        z.viewHeight = 176; z.cycleZoom(); CHECK_EQ(z.viewHeight, 288);
        z.cycleZoom(); CHECK_EQ(z.viewHeight, 448);
        z.cycleZoom(); CHECK_EQ(z.viewHeight, 528);
        z.cycleZoom(); CHECK_EQ(z.viewHeight, 176);

        // After fast-forwarding, the current state is used instead of the stale previous one.
        setState(5, 0x23); frames(z, 2, 16.0 / 9.0);
        setState(5, 0x24); z.resetLag(); z.viewHeight = 176; z.snapCamera(); syncFb(*nes); z.render(*nes, 512.0 / 240.0, c);
        CHECK_EQ(px(128 + 10, 72), 0xFF123456);
    }
}

void section11(Ctx& t) {
    SECTION_BINDINGS
    // 11. Gliding across a screen border: the emulator skipped the scroll and the game put Link 16 px forward
    //     (room $23 x=240 -> room $24 x=0). The compositor shows him walking those 16 px with the camera following.
    {
        auto g_h = std::make_unique<Widescreen>(); Widescreen& g = *g_h; g.init(*nes, err); g.unfogAnim = false; g.cloudShadow = false; g.lighting = false; g.viewHeight = 176; g.hudOverlay = false; g.caveBox = false;
        nes->ppu.palette[0x11] = 0x30;                       // sprite palette 0, colour 1 = white
        for (int i = 0; i < 64; i++) nes->ppu.oam[i * 4] = 0xFF;          // all sprites parked
        setState(5, 0x23); nes->ram[0x70] = 240; frames(g, 3, aspect);   // steady, Link at the right edge
        // (setState + snapshot of the new screen, as the emulator would leave things after skipping)
        setState(5, 0x24); nes->ram[0x70] = 0;
        nes->ppu.oam[0] = 140; nes->ppu.oam[1] = 3; nes->ppu.oam[2] = 0; nes->ppu.oam[3] = 0;   // Link's sprite at (0, 141)
        g.resetLag(); g.startGlide(*nes);
        CHECK(g.gliding());
        std::vector<uint32_t> prevRow;
        const int row = 100;
        int steps = 0;
        while (g.gliding() && steps < 100) {
            syncFb(*nes); g.render(*nes, aspect, c); steps++;
            CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240);
            // Link (an 8 px wide test sprite) stays at the middle of the picture: columns 248..255, row 144
            CHECK_EQ(px(248, 144), Ppu::nesColor(0x30)); CHECK_EQ(px(255, 144), Ppu::nesColor(0x30));
            CHECK(px(247, 144) != Ppu::nesColor(0x30)); CHECK(px(256, 144) != Ppu::nesColor(0x30));
            CHECK_EQ(px(10, 10), 0xFF000000);                // status bar is still centred; margins black
            // the world slides past at one pixel per frame (camera moves right 1 px -> content moves left 1 px)
            std::vector<uint32_t> cur(c.pixels.begin() + row * c.w, c.pixels.begin() + (row + 1) * c.w);
            if (!prevRow.empty()) {
                bool shifted = true;
                for (int x = 0; x < 150; x++) if (cur[x] != prevRow[x + 1]) shifted = false;
                CHECK(shifted);
            }
            prevRow = cur;
        }
        CHECK_EQ(steps, 16);
        CHECK(!g.gliding());
        // Back to normal: the game's own picture is placed so that Link is at the middle (room corner at x=248, row 67)
        syncFb(*nes); g.render(*nes, aspect, c);
        CHECK_EQ(px(248 + 10, 67 + 5), 0xFF123456);

        // No glide when there is nothing to smooth, or when the camera hasn't been anywhere yet.
        auto g2_h = std::make_unique<Widescreen>(); Widescreen& g2 = *g2_h; g2.init(*nes, err); g2.unfogAnim = false; g2.cloudShadow = false; g2.lighting = false; g2.viewHeight = 176; g2.hudOverlay = false; g2.caveBox = false;
        g2.startGlide(*nes);
        CHECK(!g2.gliding());
        setState(5, 0x24); nes->ram[0x70] = 0; frames(g2, 3, aspect);
        g2.startGlide(*nes);                                  // camera already on Link
        CHECK(!g2.gliding());
        setState(5, 0x24, 3);                                 // dungeons never glide
        g2.startGlide(*nes);
        CHECK(!g2.gliding());
    }
}

void section20(Ctx& t) {
    SECTION_BINDINGS
    // 20. Fog gradient: a fogged room shows terrain next to explored rooms, fading to full fog towards its centre.
    //     (Rooms $22 and $23 are explored, $24 is not; the camera is on $23.)
    frames(ws, 2, aspect);
    CHECK(!isFog(px(385, 100)));                             // 1 px into $24: terrain still visible
    CHECK(isFog(px(450, 100)));                              // 66 px in: full fog
    // The profile only depends on the explored neighbours (bit k = kFogNb[k]).
    using namespace wd;
    const unsigned W = 1u << 3, E = 1u << 4, N = 1u << 1, NW = 1u << 0, S = 1u << 6;
    CHECK_EQ((fogVisibility(0, 5, 5)), 0);
    CHECK((fogVisibility(W, 0, 80)) > 100);                    // at the border of an explored west room
    CHECK((fogVisibility(W, 0, 80)) > (fogVisibility(W, 20, 80)) && (fogVisibility(W, 20, 80)) > (fogVisibility(W, 40, 80)));
    CHECK_EQ((fogVisibility(W, kFogDepth, 80)), 0);            // full fog from the depth on
    CHECK_EQ((fogVisibility(W, 0, 10)), (fogVisibility(W, 0, 150)));   // same along the whole edge
    CHECK_EQ((fogVisibility(E, ROOM_W - 1, 80)), (fogVisibility(W, 0, 80)));   // symmetric
    // A diagonal neighbour alone rounds the corner; with exactly one of the rooms beside it explored it adds nothing; with both it rounds the corner.
    CHECK((fogVisibility(NW, 0, 0)) > 100);                                  // alone: a round corner patch
    CHECK_EQ((fogVisibility(NW, 0, 100)), 0);
    CHECK((fogVisibility(NW, 0, 0)) > (fogVisibility(NW, 20, 20)));
    CHECK_EQ((fogVisibility(NW, 30, 30)), 0);                                  // round: 42 px away diagonally even though 30 px per axis
    CHECK_EQ((fogVisibility(NW | W, 0, 0)), (fogVisibility(W, 0, 0)));      // only one of the two sides: no corner
    CHECK_EQ((fogVisibility(NW | N, 0, 0)), (fogVisibility(N, 0, 0)));
    CHECK((fogVisibility(NW | W | N, 6, 6)) > (fogVisibility(W | N, 6, 6)));
    CHECK((fogVisibility(NW | W | N, 6, 6)) > (fogVisibility(NW | W | N, 20, 20)));
    const unsigned SE = 1u << 7, SW = 1u << 5, NE = 1u << 2;
    CHECK_EQ((fogVisibility(SE | E, ROOM_W - 1, ROOM_H - 1)), (fogVisibility(E, ROOM_W - 1, ROOM_H - 1)));
    CHECK((fogVisibility(SE | E | S, ROOM_W - 7, ROOM_H - 7)) > (fogVisibility(E | S, ROOM_W - 7, ROOM_H - 7)));
    CHECK((fogVisibility(SW | W | S, 6, ROOM_H - 7)) > (fogVisibility(W | S, 6, ROOM_H - 7)));
    CHECK((fogVisibility(NE | E | N, ROOM_W - 7, 6)) > (fogVisibility(E | N, ROOM_W - 7, 6)));
    // Three sides explored (W, N, E): the light bands add up without exceeding full terrain, and blend smoothly in the corners; the room centre is full fog.
    const int corner = (fogVisibility(W | N, 0, 0)), single = (fogVisibility(W, 0, 0));
    CHECK(corner > single && corner <= 256);
    CHECK((fogVisibility(W | N | E, 128, 0)) >= (fogVisibility(N, 128, 0)));
    CHECK_EQ((fogVisibility(W | N | E | S, 128, 88)), 0);
    // Blocked borders: the fog depth per tile along the border (west: 22 tiles). Rows 0-9 blocked (12 px), the others walkable (37).
    FogDepths fd{};
    for (int i = 0; i < 22; i++) fd.d[1][i] = i < 10 ? kFogDepthWall : kFogDepth;
    CHECK_EQ((fogVisibility(W, 20, 4, -1, 1.0, &fd)), 0);                       // blocked part: full fog 20 px in
    CHECK((fogVisibility(W, 20, 160, -1, 1.0, &fd)) > 0);                        // walkable part: still lit
    CHECK_EQ((fogVisibility(W, 20, 160, -1, 1.0, &fd)), (fogVisibility(W, 20, 160)));
    CHECK((fogVisibility(W, 4, 4, -1, 1.0, &fd)) < (fogVisibility(W, 4, 160, -1, 1.0, &fd)));   // steeper in the blocked part
    CHECK((fogVisibility(W, 0, 4, -1, 1.0, &fd)) > 200);                         // but clear at the border
    {   // where the opening ends (tile 10 = y 80) the glow's edge is a quarter ellipse that curves inwards: it fades smoothly, never in a step
        int prev = fogVisibility(W, 16, 100, -1, 1.0, &fd), worstStep = 0;
        for (int y = 96; y >= 20; y -= 4) {
            const int v = fogVisibility(W, 16, y, -1, 1.0, &fd);
            CHECK(v <= prev);                                                        // only ever dimmer towards the blocked part
            worstStep = std::max(worstStep, prev - v);
            prev = v;
        }
        CHECK(worstStep < 60);
        CHECK_EQ((fogVisibility(W, 28, 82, -1, 1.0, &fd)), 0);                       // at the very end the deep glow is cut back to ...
        CHECK((fogVisibility(W, 28, 140, -1, 1.0, &fd)) > 0);                        // ... what it is further in
        CHECK((fogVisibility(W, 4, 82, -1, 1.0, &fd)) > 100);                        // (next to the border it stays bright)
        CHECK_EQ((fogVisibility(W, 16, 70, -1, 1.0, &fd)), 0);                       // and nothing spills into the blocked part
        int firstLit24 = -1, firstLit32 = -1;                                         // the farther from the border, the farther in the glow starts
        for (int y = 80; y < 200 && (firstLit24 < 0 || firstLit32 < 0); y++) {
            if (firstLit24 < 0 && fogVisibility(W, 24, y, -1, 1.0, &fd) > 0) firstLit24 = y;
            if (firstLit32 < 0 && fogVisibility(W, 32, y, -1, 1.0, &fd) > 0) firstLit32 = y;
        }
        CHECK(firstLit24 >= 80 && firstLit32 > firstLit24);
    }
    {   // a corner patch between a blocked (short) and a walkable (long) border is an ellipse
        FogDepths e{};
        for (int c = 0; c < 4; c++) { e.diagX[c] = kFogDepthWall; e.diagY[c] = kFogDepth; }
        CHECK_EQ((fogVisibility(NW, 20, 2, -1, 1.0, &e)), 0);                      // 20 px along the short axis: gone
        CHECK((fogVisibility(NW, 2, 20, -1, 1.0, &e)) > 0);                        // 20 px along the long axis: still there
        CHECK((fogVisibility(NW, 2, 20, -1, 1.0, &e)) < (fogVisibility(NW, 2, 20)));
        for (int c = 0; c < 4; c++) e.diagX[c] = e.diagY[c] = kFogDepth;
        CHECK_EQ((fogVisibility(NW, 9, 14, -1, 1.0, &e)), (fogVisibility(NW, 9, 14)));   // equal depths: the old round patch
    }
    {   // owWalkMask: water is walkable only where its body touches ground
        uint8_t t[704]; bool m[704];
        for (int i = 0; i < 704; i++) t[i] = 0xB0;                              // blocked everywhere
        auto at = [&](int c, int r) -> uint8_t& { return t[c * 22 + r]; };
        for (int c = 2; c < 6; c++) for (int r = 2; r < 5; r++) at(c, r) = 0x90;  // water body next to a bridge tile
        at(1, 3) = 0x8D;                                                          // (special tile = walkable ground)
        for (int c = 10; c < 14; c++) for (int r = 2; r < 5; r++) at(c, r) = 0x90;  // lake enclosed by blocked tiles
        at(20, 10) = 0x24;                                                        // plain ground
        owWalkMask(t, m);
        CHECK(m[1 * 22 + 3]); CHECK(m[2 * 22 + 2]); CHECK(m[5 * 22 + 4]);        // the body touching the bridge: all walkable
        CHECK(!m[10 * 22 + 2] && !m[13 * 22 + 4]);                               // the enclosed lake stays blocked
        CHECK(m[20 * 22 + 10]); CHECK(!m[0]);
    }
}

void section21(Ctx& t) {
    SECTION_BINDINGS
    // 21. Unfogging animation: entering the unexplored room $24 (Link at X=120, Y=141) reveals it with a clear patch that
    //     starts around Link and grows to cover the room; meanwhile the rest of the room stays fogged.
    //     Room origin on the canvas: (128, 64) (the original status bar is on top); Link's pixel (120, 141) -> canvas (248, 142).
    ws.unfogAnim = true;
    setState(5, 0x24); frames(ws, 2, aspect);
    CHECK(ws.unfogging()); CHECK_EQ(ws.unfogFrame(), 0);
    CHECK(!isFog(px(128 + 120, 142)));                       // the area around Link is visible at once
    CHECK(isFog(px(128 + 250, 64 + 4)));                     // the far corner is still fogged
    auto clearCount = [&]() { int n = 0; for (int y = 64; y < 240; y += 4) for (int x = 128; x < 384; x += 4) n += !isFog(px(x, y)); return n; };
    int prev = clearCount(), mid = 0;
    bool grows = true;
    for (int i = 1; i <= kUnfogFrames - 1; i++) {
        frames(ws, 1, aspect);
        const int n = clearCount();
        grows = grows && n >= prev; prev = n;
        if (i == kUnfogFrames / 2) mid = n;
    }
    CHECK(grows);                                            // the patch only grows
    CHECK(mid > 0 && mid < clearCount() + 0 && mid < 64 * 44);   // half-way: part of the room (64 x 44 samples) is clear
    CHECK(ws.unfogging());                                   // still running on the last frame before the end
    frames(ws, 1, aspect);
    CHECK(!ws.unfogging());
    CHECK(!isFog(px(128 + 250, 64 + 4)));                    // the whole room is revealed
    // The animation is off when asked: a new room appears at once.
    ws.unfogAnim = false; ws.cloudShadow = false; ws.lighting = false;
    setState(5, 0x25); frames(ws, 2, aspect);
    CHECK(!ws.unfogging());
    CHECK(!isFog(px(128 + 250, 64 + 4)));
}

void section23(Ctx& t) {
    SECTION_BINDINGS
    // 23a. The glow of an unfogging room on its fogged neighbour follows the reveal front instead of waiting for the end:
    //      room $27 is entered (Link at X=120), its east neighbour $28 starts at canvas x = 384.
    ws.unfogAnim = true;
    setState(5, 0x27); frames(ws, 2, aspect);
    CHECK(ws.unfogging());
    CHECK(isFog(px(385, 100)));                              // the front is far from the east edge: still a dark line there
    for (int i = 0; i < kUnfogFrames / 2 - 1; i++) frames(ws, 1, aspect);
    CHECK(ws.unfogging());
    CHECK(!isFog(px(385, 100)));                             // the front has reached it: the glow is already there
    for (int i = 0; i < kUnfogFrames; i++) frames(ws, 1, aspect);
    CHECK(!ws.unfogging()); CHECK(!isFog(px(385, 100)));
    ws.unfogAnim = false; ws.cloudShadow = false; ws.lighting = false;

    // 23b. Discoveries that were not saved are dropped when a file is loaded (leaving the file-select screen).
    setState(5, 0x2A); frames(ws, 2, aspect);
    setState(5, 0x29); frames(ws, 2, aspect);
    CHECK(!isFog(px(450, 100)));                             // $2A, east of $29, was explored
    setState(1, 0x29); frames(ws, 2, aspect);                // file select
    setState(5, 0x29); frames(ws, 2, aspect);
    CHECK(isFog(px(450, 100)));                              // never saved: gone

    // 23c. A sword beam fired at a border is removed (it would splash at once); elsewhere it stays.
    setState(5, 0x29); frames(ws, 2, aspect); ws.prepareFrame(*nes);
    auto fire = [&](int dir, int x, int y) { nes->ram[0xAC + 0x0E] = 0; ws.afterFrame(*nes); nes->ram[0xAC + 0x0E] = 0x10; nes->ram[0x98] = dir; nes->ram[0x70] = x; nes->ram[0x84] = y; ws.afterFrame(*nes); return nes->ram[0xAC + 0x0E]; };
    // Less than 1.5 tiles (24 px) from the border to the centre of Link's sprite: x < $10, x >= $E0, y < $50, y >= $D0.
    CHECK_EQ(fire(4, 120, 0xD0), 0);                         // down
    CHECK_EQ(fire(4, 120, 0xCF), 0x10);                      // 1 px further: stays
    CHECK_EQ(fire(2, 0x0F, 100), 0);                         // left
    CHECK_EQ(fire(2, 0x10, 100), 0x10);
    CHECK_EQ(fire(1, 0xE0, 100), 0);                         // right
    CHECK_EQ(fire(1, 0xDF, 100), 0x10);
    CHECK_EQ(fire(8, 120, 0x4F), 0);                         // up
    CHECK_EQ(fire(8, 120, 0x50), 0x10);
    CHECK_EQ(fire(4, 0x40, 0xB0), 0x10);
    // The game's own horizontal check (MakeSwordShot) is moved to the same distance: shot X (Link +- 16) < $00 / >= $F0.
    {
        const std::vector<uint8_t>& prg = nes->cart.prgData();
        size_t o = 0;
        for (size_t i = 0; i + 9 <= prg.size() && !o; i++) if (prg[i] == 0xB5 && prg[i + 1] == 0x70 && prg[i + 2] == 0xC9 && prg[i + 4] == 0x90 && prg[i + 6] == 0xC9 && prg[i + 8] == 0xB0) o = i;
        CHECK(o != 0);
        if (o) { CHECK_EQ(prg[o + 3], 0x00); CHECK_EQ(prg[o + 7], 0xF0); }
    }
    CHECK_EQ(fire(4, 120, 100), 0x10);                       // in the open: stays
    nes->ram[0xAC + 0x0E] = 0;
    setState(5, 0x29, 1); frames(ws, 2, aspect); ws.prepareFrame(*nes);   // dungeon: the game's rules apply
    CHECK_EQ(fire(4, 120, 0xC0), 0x10);
    nes->ram[0xAC + 0x0E] = 0;
    setState(5, 0x29); frames(ws, 2, aspect);

    // 23d. SAVE AND QUIT: SELECT in the open inventory; the hint text sits in the unused rows above the title; a save started
    //      from the inventory closes it (an open MenuState would freeze the timers the loading curtain waits for).
    setState(5, 0x29); frames(ws, 2, aspect);
    CHECK(!Widescreen::menuOpenForSave(*nes));
    nes->ram[0xE1] = 8; CHECK(Widescreen::menuOpenForSave(*nes));
    nes->ram[0xE1] = 7; CHECK(!Widescreen::menuOpenForSave(*nes));
    nes->ram[0x10] = 1; CHECK(Widescreen::menuOpenForSave(*nes));                 // dungeon: open = 7
    nes->ram[0x10] = 0; nes->ram[0xE1] = 8;
    {
        nes->ppu.ctrl &= (uint8_t)~0x10;                                           // the game's font: letter tiles solid for the test
        for (int t : {0x0A, 0x0E, 0x0F, 0x10, 0x12, 0x13, 0x15, 0x18, 0x1A, 0x1B, 0x1D, 0x62}) for (int y = 0; y < 8; y++) nes->cart.ppuWrite((uint16_t)(t * 16 + y), 0xFF);
        for (int i = 0; i < 40 * 256; i++) nes->ppu.framebuffer[i] = 0xFF000000;   // blank rows above the picture's content
        for (int i = 40 * 256; i < 240 * 256; i++) nes->ppu.framebuffer[i] = 0xFF123456;
        ws.render(*nes, aspect, c); ws.render(*nes, aspect, c);
        CHECK_EQ(ws.overlay.images.size(), (size_t)1);
        const OverlayImage& img = ws.overlay.images[0];
        int lit = 0;
        for (int y = 5; y < 5 + 20; y++) for (int x = 4; x < img.w - 4; x++) lit += img.pixels[(size_t)y * img.w + x] == 0xFFFCFCFC;
        CHECK(lit > 400);                                                         // the text is there (8x8 tiles of the game's font)
        nes->ram[0x12] = 4;                                                       // not playing: no hint
        ws.render(*nes, aspect, c); ws.render(*nes, aspect, c);
        nes->ram[0x12] = 5;
        // the confirmation box: SELECT opens it (NO highlighted), directions pick, A confirms, NO / B close it, YES returns true
        CHECK(!ws.saveConfirm);
        CHECK(!ws.saveConfirmInput(*nes, BTN_A) && !ws.saveConfirm);                         // nothing without SELECT
        CHECK(!ws.saveConfirmInput(*nes, BTN_SELECT) && ws.saveConfirm && !ws.saveConfirmYes);
        ws.render(*nes, aspect, c); ws.render(*nes, aspect, c);
        CHECK_EQ(ws.overlay.images.size(), (size_t)2);                                      // the inventory and the question box
        const OverlayImage yesNo = ws.overlay.images[1];
        CHECK(yesNo.heightPerScale > 0 && yesNo.w > 100 && yesNo.h > 40);
        CHECK(!ws.saveConfirmInput(*nes, BTN_A) && !ws.saveConfirm);                         // A on NO (the default): closes, no save
        ws.saveConfirmInput(*nes, BTN_SELECT); CHECK(ws.saveConfirm);
        ws.saveConfirmInput(*nes, BTN_LEFT); CHECK(ws.saveConfirmYes);
        ws.render(*nes, aspect, c); ws.render(*nes, aspect, c);
        CHECK(ws.overlay.images[1].pixels != yesNo.pixels);                                 // the cursor moved to YES
        ws.saveConfirmInput(*nes, BTN_UP); ws.saveConfirmInput(*nes, BTN_DOWN); CHECK(ws.saveConfirmYes);   // (up/down do nothing)
        ws.saveConfirmInput(*nes, BTN_RIGHT); CHECK(!ws.saveConfirmYes);
        ws.saveConfirmInput(*nes, BTN_LEFT); CHECK(ws.saveConfirmYes);
        ws.saveConfirmInput(*nes, BTN_RIGHT); CHECK(!ws.saveConfirmYes);
        ws.saveConfirmInput(*nes, BTN_LEFT);
        CHECK(ws.saveConfirmInput(*nes, BTN_A) && !ws.saveConfirm && !ws.saveConfirmYes);   // A on YES: confirmed, the box is gone
        ws.saveConfirmInput(*nes, BTN_SELECT); CHECK(!ws.saveConfirmInput(*nes, BTN_B) && !ws.saveConfirm);   // B closes it too
        ws.saveConfirmInput(*nes, BTN_SELECT); nes->ram[0xE1] = 0;                           // the inventory closes: the box goes with it
        CHECK(!ws.saveConfirmInput(*nes, BTN_A) && !ws.saveConfirm);
        nes->ram[0xE1] = 8; ws.render(*nes, aspect, c); ws.render(*nes, aspect, c);
        CHECK_EQ(ws.overlay.images.size(), (size_t)1);
    }
    nes->ram[0xE0] = 1; nes->ram[0x12] = 5;
    nes->ram[0x5A] = 2; nes->ram[0x11] = 1; nes->ram[0x66F] = 0x30; nes->ram[0x670] = 0x40; nes->ram[0xAC] = 5; nes->ram[0x16] = 1; nes->ram[0x10] = 0;
    ws.requestSave(*nes);
    CHECK_EQ(nes->ram[0xE1], 0); CHECK_EQ(nes->ram[0xE0], 0); CHECK_EQ(nes->ram[0x12], 0x0D);
    CHECK_EQ(nes->ram[0x11], 0); CHECK_EQ(nes->ram[0x5A], 0); CHECK_EQ(nes->ram[0xAC], 0);             // as after the game's own SAVE
    CHECK_EQ(nes->ram[0x66F], 0x30); CHECK_EQ(nes->ram[0x670], 0x40);                                 // the hearts are kept
    nes->ram[0x66F] = 0x33; nes->ram[0x670] = 0xFF; nes->ram[0x13] = 1;                               // what the game's save does at submode 0
    ws.afterFrame(*nes);
    CHECK_EQ(nes->ram[0x66F], 0x30); CHECK_EQ(nes->ram[0x670], 0x40);
    CHECK_EQ(nes->ram[0x650 + 2], 0x30); CHECK_EQ(nes->ram[0x651 + 2], 0x40);                          // the slot (1) shows the real hearts
    // Saved inside dungeon 3 (entered from room $55): loading that slot starts in the dungeon, entered from that room.
    nes->ram[0x12] = 5; nes->ram[0x10] = 3; nes->ram[0x526] = 0x55; nes->ram[0x16] = 1;
    ws.requestSave(*nes);
    nes->ram[0x12] = 2; nes->ram[0x10] = 0; nes->ram[0x526] = 0xFF; nes->ram[0x5A] = 0;
    ws.prepareFrame(*nes);
    CHECK_EQ(nes->ram[0x526], 0x55); CHECK_EQ(nes->ram[0x10], 3); CHECK_EQ(nes->ram[0x5A], 0);
    nes->ram[0x12] = 3; nes->ram[0x13] = 5; nes->ram[0x526] = 0xFF; ws.prepareFrame(*nes);              // (the room is only needed before mode 3 sub 1)
    CHECK_EQ(nes->ram[0x526], 0xFF);
    nes->ram[0x12] = 4; ws.prepareFrame(*nes); nes->ram[0x10] = 0; nes->ram[0x12] = 2; ws.prepareFrame(*nes);
    CHECK_EQ(nes->ram[0x10], 0);                                                                       // done once the room is entered
    {   // the file-select hearts show the saved HeartValues as they are
        const std::vector<uint8_t>& prg = nes->cart.prgData();
        size_t o = 0;
        for (size_t i = 0; i + 11 <= prg.size() && !o; i++) if (prg[i] == 0x29 && prg[i + 2] == 0x85 && prg[i + 3] == 0x0C && prg[i + 8] == 0x05) o = i;
        CHECK(o != 0);
        if (o) { CHECK_EQ(prg[o + 1], 0xFF); CHECK_EQ(prg[o + 4], 0xEA); CHECK_EQ(prg[o + 7], 0xEA); }
    }
    nes->ram[0x10] = 0; nes->ram[0x16] = 0;
    nes->ram[0x12] = 5; nes->ram[0x13] = 0;
    setState(5, 0x29); frames(ws, 2, aspect);
}

void section24(Ctx& t) {
    SECTION_BINDINGS
    // 24a. The markers themselves. Bomb: one dark pixel per crack row; burn: yellowish, more on the lower half, never on the
    //      backdrop; armos: only blue turns red; push: outline only, pulsing.
    const uint32_t green = 0xFF20A020;
    CHECK_EQ(secretMark(green, SecretNone, 5, 5, false, 256), green);
    CHECK_EQ(secretMark(green, SecretBomb, 8, 5, false, 256) != green, true);           // the crack runs through (8,5)
    CHECK_EQ(secretMark(green, SecretBomb, 2, 5, false, 256), green);
    CHECK_EQ(secretMark(green, SecretBomb, 0, 0, false, 256), green);                   // rows without a crack
    CHECK(((secretMark(green, SecretBomb, 8, 5, false, 256) >> 8) & 255) < 0x30);       // dark
    const uint32_t upper = secretMark(green, SecretBurn, 4, 2, false, 256), lower = secretMark(green, SecretBurn, 4, 12, false, 256);
    CHECK((lower >> 16 & 255) > (upper >> 16 & 255));                                 // a gradient: yellower further down
    CHECK((secretMark(green, SecretBurn, 4, 8, false, 256) >> 16 & 255) > (upper >> 16 & 255) && (secretMark(green, SecretBurn, 4, 8, false, 256) >> 16 & 255) < (lower >> 16 & 255));
    CHECK_EQ(secretMark(green, SecretBurn, 4, 12, true, 256), green);
    CHECK_EQ(secretMark(green, SecretArmos, 4, 4, false, 256), green);                  // armos: only the blue (the eyes) turns red
    const uint32_t blue = 0xFF3050E0, red = secretMark(blue, SecretArmos, 4, 4, false, 256);
    CHECK((red >> 16 & 255) > 150 && (red & 255) < 80);
    CHECK_EQ(secretMark(0xFFF0D090, SecretArmos, 4, 4, true, 256), 0xFFF0D090);
    CHECK_EQ(secretMark(green, SecretPush, 7, 7, true, 256), green);                  // push: a gray tint growing downwards, never on the ground
    const uint32_t gTop = secretMark(green, SecretPush, 7, 1, false, 256), gBottom = secretMark(green, SecretPush, 7, 14, false, 256);
    CHECK(gTop != green && (gBottom >> 16 & 255) > (gTop >> 16 & 255));
    CHECK(((gBottom >> 8 & 255) - (gBottom >> 16 & 255)) * 10 < (0xA0 - 0x20) * 4);      // a green rock turns clearly gray at the bottom (under 40% of its greenness left)
    CHECK(secretMark(0xFFB06028, SecretPush, 7, 14, false, 256) != 0xFFB06028u);         // (a brown one keeps the gentle tint)
    CHECK_EQ(secretMark(0xFF000000, SecretBurn, 4, 12, false, 0), 0xFF000000u);        // the marks fade out with the picture (death scene)
    CHECK_EQ(secretMark(0xFF000000, SecretPush, 4, 12, false, 0), 0xFF000000u);
    CHECK_EQ(secretMark(0xFF000000 | 0x000050, SecretArmos, 4, 4, false, 0) & 0xFFFFFF, 0u);
    CHECK((secretMark(0xFF30C030, SecretBurn, 4, 12, false, 128) >> 16 & 255) < (secretMark(0xFF30C030, SecretBurn, 4, 12, false, 256) >> 16 & 255));
    {   // a tint is never brighter than the (faded) scenery under it, so a dark pixel stays dark whatever the fade estimate says
        auto mx = [](uint32_t c) { return std::max({(int)(c >> 16 & 255), (int)(c >> 8 & 255), (int)(c & 255)}); };
        bool ok = true;
        for (uint32_t px : {0xFF080C08u, 0xFF103010u, 0xFF004000u, 0xFF301808u})
            for (int k : {SecretBurn, SecretPush})
                for (int f : {256, 200, 128, 64, 0})
                    for (int sy = 0; sy < 16; sy += 3) ok = ok && mx(secretMark(px, k, 4, sy, false, f)) <= mx(px);
        CHECK(ok);
        CHECK(mx(secretMark(0xFF003000, SecretBurn, 4, 14, false, 256)) <= 0x30);
    }

    // 24b. In the game's own picture (blitRoom): make the fake ROM's squares rock walls ($E6 -> tiles $D8..), fog off. A hidden
    //      secret gets the crack, one that belongs to the other quest does not. (drawWorld uses the same secretMark per tile.)
    for (int id = 0xD8; id <= 0xDB; id++) t.tile(id, 0xFF, 0x00);
    nes->ppu.palette[5] = 0x2A;
    auto snap = [&](Widescreen& w, std::vector<uint32_t>& out) {
        std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);             // no sprite anywhere
        setState(5, 0x23);
        {   // the game's play area must match what the (patched) ROM decodes, or the screen is taken for a cave
            OverworldMap m; std::string e; m.init(nes->cart.prgData(), e);
            uint8_t tt[OverworldMap::TILES]; m.decodeRoom(0, false, tt);
            for (int i = 0; i < OverworldMap::TILES; i++) nes->cart.cpuWrite(0x6530 + i, tt[i]);
        }
        for (int k = 0; k < 4; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
        out.clear();
        for (int y = 80; y < 160; y++) for (int x = 160; x < 350; x++) out.push_back(px(x, y));
    };
    auto fresh = [&](Widescreen& w) { w.init(*nes, err); w.unfogAnim = false; w.cloudShadow = false; w.lighting = false; w.fog = false; w.viewHeight = 176; w.hudOverlay = false; w.caveBox = false; };
    auto diffs = [&](const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) { int n = 0; for (size_t i = 0; i < a.size(); i++) n += a[i] != b[i]; return n; };
    std::vector<uint32_t> before, hidden, found, other;
    { auto w_h = std::make_unique<Widescreen>(); Widescreen& w = *w_h; fresh(w); snap(w, before); }
    nes->cart.cpuWrite(0x6AFE + 0x23, 0);                                             // secret of both quests
    nes->ram[0x67F + 0x23] &= 0x7F;                                                   // not found yet
    // (the squares use index $10, which is part of the primary table's signature: use index $18 instead, = $E6)
    const size_t heap0 = 5 * 16384 + (0x9BD8 - 0x8000);
    auto useSquare = [&](int idx) { nes->cart.patchPrg(heap0, 0x80 | idx); for (int i = 1; i < 11; i++) nes->cart.patchPrg(heap0 + i, (uint8_t)idx); };
    nes->cart.patchPrg(0x218, 0xE6); useSquare(0x18);
    { auto w_h = std::make_unique<Widescreen>(); Widescreen& w = *w_h; fresh(w); snap(w, hidden); }
    CHECK(diffs(before, hidden) > 40);                                               // (one crack per 16 rows, the rest is the new tile colour)
    // compare with the same tiles without the marker: the found state decodes differently, so use the other quest instead
    nes->cart.cpuWrite(0x6AFE + 0x23, 0x80);                                          // secret of the second quest only
    nes->ram[0x62D + nes->ram[0x16]] = 0;                                             // ... and this is the first quest
    { auto w_h = std::make_unique<Widescreen>(); Widescreen& w = *w_h; fresh(w); snap(w, other); }
    CHECK(diffs(hidden, other) > 0);                                                 // no crack here
    int dark = 0; for (size_t i = 0; i < hidden.size(); i++) if (hidden[i] != other[i] && (hidden[i] & 0xFF) < (other[i] & 0xFF)) dark++;
    CHECK_EQ(dark, diffs(hidden, other));                                            // and every difference is the (darker) crack
    nes->ram[0x62D + nes->ram[0x16]] = 1;                                             // second quest: the secret applies again
    { auto w_h = std::make_unique<Widescreen>(); Widescreen& w = *w_h; fresh(w); snap(w, other); }
    CHECK_EQ(diffs(hidden, other), 0);
    nes->cart.cpuWrite(0x6AFE + 0x23, 0);
    useSquare(0x10);                                                                  // back to the fake ROM's normal tiles
    nes->cart.patchPrg(0x218, 0);
    nes->ram[0x62D + nes->ram[0x16]] = 0;

    // 24c. The armos that hides stairs is picked by the game by room and X (Z_04 SecretArmosRoomIds/Xs, Y = $80): its blue
    //      pixels turn red in the game's picture, in that room only. Other armos in the room (other X) stay blue.
    auto armosRoom = [&](int room, int armosX) {
        std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        setState(5, room);
        { OverworldMap m; std::string e; m.init(nes->cart.prgData(), e); uint8_t tt[OverworldMap::TILES]; m.decodeRoom(0, false, tt);
          for (int i = 0; i < OverworldMap::TILES; i++) nes->cart.cpuWrite(0x6530 + i, tt[i]); }
        nes->ram[0x67F + room] &= 0x7F; nes->cart.cpuWrite(0x6AFE + room, 0);
        auto w_h = std::make_unique<Widescreen>(); Widescreen& w = *w_h; fresh(w);
        for (int k = 0; k < 4; k++) {
            syncFb(*nes);
            for (int y = 66; y < 78; y++) for (int x = armosX + 2; x < armosX + 12; x++) {
                nes->ppu.fbColor[(HUD_H + y) * Ppu::W + x] = 0xFF2040E0; nes->ppu.bgColor[(HUD_H + y) * Ppu::W + x] = 0xFF2040E0;
            }
            w.snapCamera(); w.render(*nes, aspect, c);
        }
        int red = 0, blue = 0;
        for (int y = 0; y < c.h; y++) for (int x = 0; x < c.w; x++) { const uint32_t v = px(x, y); red += v == 0xFFC83020; blue += v == 0xFF2040E0; }
        return std::make_pair(red, blue);
    };
    auto secretOne = armosRoom(0x0B, 0xB0), otherX = armosRoom(0x0B, 0x40), otherRoom = armosRoom(0x0C, 0xB0);
    CHECK(secretOne.first > 50 && secretOne.second == 0);
    CHECK(otherX.first == 0 && otherX.second > 50);
    CHECK(otherRoom.first == 0 && otherRoom.second > 50);

    // 24d. Seam between a lit and a dark fogged room: no step, the two sides meet at the average and the blend ends kSeam pixels away.
    CHECK(std::fabs(seamBlend(200, 0, 0.5) - seamBlend(0, 200, 0.5)) < 8.0);            // equal on both sides of the seam
    CHECK_EQ(seamBlend(200, 0, kSeam), 200); CHECK_EQ(seamBlend(0, 200, kSeam + 5), 0);
    CHECK(seamBlend(0, 200, 0.5) > 90 && seamBlend(0, 200, 0.5) < 101);
    CHECK(seamBlend(200, 0, 4) > seamBlend(200, 0, 0.5) && seamBlend(200, 0, 12) > seamBlend(200, 0, 4));   // a smooth ramp

    // 24e. During the unfogging animation the secret armos is red from the very first frame it shows (never blue first).
    {
        const int room = 0x0B;
        std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        setState(5, room);
        { OverworldMap m; std::string e; m.init(nes->cart.prgData(), e); uint8_t tt[OverworldMap::TILES]; m.decodeRoom(0, false, tt);
          for (int i = 0; i < OverworldMap::TILES; i++) nes->cart.cpuWrite(0x6530 + i, tt[i]); }
        nes->ram[0x67F + room] &= 0x7F; nes->cart.cpuWrite(0x6AFE + room, 0);
        auto w_h = std::make_unique<Widescreen>(); Widescreen& w = *w_h; w.init(*nes, err); w.cloudShadow = false; w.lighting = false; w.hudOverlay = false; w.caveBox = false; w.viewHeight = 176;      // fog on, animation on
        int blueFrames = 0, redFrames = 0, animFrames = 0;
        for (int k = 0; k < 45; k++) {
            syncFb(*nes);
            for (int y = 66; y < 78; y++) for (int x = 0xB0 + 2; x < 0xB0 + 12; x++) {
                nes->ppu.fbColor[(HUD_H + y) * Ppu::W + x] = 0xFF2040E0; nes->ppu.bgColor[(HUD_H + y) * Ppu::W + x] = 0xFF2040E0;
            }
            w.snapCamera(); w.render(*nes, aspect, c);
            int red = 0, blue = 0;
            for (int y = 0; y < c.h; y++) for (int x = 0; x < c.w; x++) { const uint32_t v = px(x, y); red += v == 0xFFC83020; blue += v == 0xFF2040E0; }
            if (blue) blueFrames++;
            if (red) redFrames++;
            if (k < 30 && red) animFrames++;
        }
        CHECK_EQ(blueFrames, 0);
        CHECK(redFrames > 5);
    }

    // 24f. A corner patch counts only when both orthogonal rooms are explored or neither is, and it sits on two of the room's four seams.
    CHECK(fogDiagActive(1u << 7, 7));                                                  // SE alone: neither S (bit 6) nor E (bit 4) explored
    CHECK(!fogDiagActive((1u << 7) | (1u << 6), 7));                                   // only the room to the south is explored
    CHECK(fogDiagActive((1u << 7) | (1u << 6) | (1u << 4), 7));
    CHECK(!fogDiagActive(0, 7));
    CHECK(fogDiagOnSeam(7, 0, false) && fogDiagOnSeam(7, 1, false) && !fogDiagOnSeam(7, 0, true) && !fogDiagOnSeam(7, 1, true));
    CHECK(fogDiagOnSeam(0, 0, true) && fogDiagOnSeam(0, 1, true) && !fogDiagOnSeam(0, 1, false));
    CHECK(fogDiagOnSeam(2, 0, false) && fogDiagOnSeam(2, 1, true) && fogDiagOnSeam(5, 0, true) && fogDiagOnSeam(5, 1, false));

    // 24g. Ground colour of a square; a dark corner is blended only when both rooms beside it are lit.
    CHECK_EQ(squareGround(5, 5, 7, 5), 5); CHECK_EQ(squareGround(1, 2, 2, 9), 2); CHECK_EQ(squareGround(1, 2, 3, 4), 1);
    CHECK(fogCornerLit(kFogDepth, kFogDepth) && !fogCornerLit(kFogDepth, kFogDepthWall) && !fogCornerLit(0, kFogDepth));

    // 24h. Beside a dark (shallow) corner the glow curves inwards: past the end of the border it continues with the corner's depth.
    {
        FogDepths f{}; for (int s = 0; s < 4; s++) for (int q = 0; q < 32; q++) f.d[s][q] = kFogDepth;
        const double plain = fogBorderStrength(&f, 0, 32, 4.0, 28.0);                        // deep glow right at the end of the border
        f.endLow[0][0] = kFogDepthWall;
        const double bitten = fogBorderStrength(&f, 0, 32, 4.0, 28.0);
        CHECK(plain > 0.2 && bitten == 0.0);                                                 // cut back near the dark corner ...
        CHECK_EQ(fogBorderStrength(&f, 0, 32, 120.0, 28.0), plain);                          // ... unchanged further along
        CHECK(fogBorderStrength(&f, 0, 32, 16.0, 28.0) < plain && fogBorderStrength(&f, 0, 32, 16.0, 28.0) > bitten);   // a smooth curve
        CHECK_EQ(fogBorderStrength(&f, 0, 32, 252.0, 28.0), plain);                          // the other end has no corner information
    }
}
