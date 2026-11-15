// Section 29: Link in dungeon doorways (widescreen_door.cpp): behind the frame / the wall around a hole, in front of the passage, nothing for closed doors.
#include "test_wide_common.h"
#include "widescreen_internal.h"
#include "zelda_ram.h"
#include <cstring>
#include <functional>

using namespace zelda;
using namespace wd;

void section29(Ctx& t) {
    SECTION_BINDINGS
    constexpr uint32_t kWallC = 0xFF806040, kFloorC = 0xFF30A030, kBlack = 0xFF000000;
    const uint32_t kLinkC = Ppu::nesColor(0x16);
    Widescreen dw; dw.init(*nes, err); dw.unfogAnim = false; dw.cloudShadow = false; dw.lighting = false; dw.fog = false; dw.hudOverlay = false; dw.caveBox = false;
    dw.dayClock.frozen = true;
    nes->cart.cpuWrite(0x6A7E + 0x23, 0);
    std::vector<uint32_t> room((size_t)256 * 176);
    auto clearRoom = [&] { for (int y = 0; y < 176; y++) for (int x = 0; x < 256; x++) room[(size_t)y * 256 + x] = (x >= 32 && x < 224 && y >= 32 && y < 144) ? kFloorC : kWallC; };
    auto black = [&](int x0, int y0, int w, int h) { for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) room[(size_t)y * 256 + x] = kBlack; };
    // the picture as the game draws it: Link in front everywhere, except where `hidden(x, y)` says the game put him behind the background
    auto show = [&](int lx, int ly, auto hidden, int mode = 5, std::function<void()> pre = nullptr) {
        for (int y = 0; y < Ppu::H; y++) for (int x = 0; x < Ppu::W; x++) nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = y < 64 ? kBlack : room[(size_t)(y - 64) * 256 + x];
        gBg.assign(nes->ppu.framebuffer, nes->ppu.framebuffer + (size_t)Ppu::W * Ppu::H);
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        for (int y = ly; y < ly + 16; y++) for (int x = lx; x < lx + 16; x++) {
            if (x < 0 || x >= 256 || y < 0 || y >= 176) continue;
            nes->ppu.sprCol[(size_t)(64 + y) * Ppu::SPR_W + x] = 0x16; nes->ppu.sprOam[(size_t)(64 + y) * Ppu::SPR_W + x] = (uint8_t)(x < lx + 8 ? kLinkOam0 : kLinkOam1);
            if (!hidden(x, y)) nes->ppu.framebuffer[(size_t)(64 + y) * Ppu::W + x] = kLinkC;
        }
        nes->ppu.oam[kLinkOam0 * 4] = (uint8_t)(ly + 63); nes->ppu.oam[kLinkOam0 * 4 + 3] = (uint8_t)lx; nes->ppu.oam[kLinkOam1 * 4] = (uint8_t)(ly + 63); nes->ppu.oam[kLinkOam1 * 4 + 3] = (uint8_t)(lx + 8);
        if (pre) pre();
        t.setState(mode, 0x23, 1); t.frames(dw, 6, aspect);
    };
    auto at = [&](int x, int y) { return c.pixels[(size_t)(64 + y) * c.w + x]; };
    auto bg = [&](int x, int y) { return room[(size_t)y * 256 + x]; };
    auto never = [](int, int) { return false; };

    // 29a. regular top door (black passage y 16..24, floor of the doorway y 24..32): the lintel covers Link's head, the passage and the floor of the doorway do not
    clearRoom(); black(120, 16, 16, 8);
    show(116, 12, never);
    CHECK(at(125, 13) == bg(125, 13));                    // over the frame above the passage: covered
    CHECK(at(125, 20) == kLinkC);                         // in the black passage: shown
    CHECK(at(125, 27) == kLinkC);                         // over the floor of the doorway: in front
    CHECK(at(117, 20) == bg(117, 20));                    // beside the passage (the trapezoid wall): covered, it is in the frame
    // not lined up with the door (x 60): the game's picture is kept
    show(60, 14, never);
    CHECK(at(65, 20) == kLinkC);
    // a closed door (no black passage): kept, even lined up
    clearRoom(); show(120, 14, never);
    CHECK(at(125, 18) == kLinkC);

    // 29b. blasted hole in the top wall (black y 16..32): the wall above it covers Link, inside the hole he is shown
    clearRoom(); black(120, 16, 16, 16);
    show(120, 14, never);
    CHECK(at(125, 14) == bg(125, 14));                    // above the hole: covered
    CHECK(at(125, 22) == kLinkC);                         // inside
    show(120, 24, never);
    CHECK(at(125, 31) == kLinkC && at(125, 36) == kLinkC);  // entering from below
    show(100, 20, never);                                  // (x 100..115: not over the hole's columns, not lined up)
    CHECK(at(105, 24) == kLinkC);

    // 29c. bottom: regular door (floor of the doorway y 144..152, black passage y 152..160, the frame's bar below), and a hole
    clearRoom(); black(120, 152, 16, 8);
    show(120, 148, never);
    CHECK(at(125, 149) == kLinkC);                        // the floor of the doorway
    CHECK(at(125, 155) == kLinkC);                        // the passage
    CHECK(at(125, 162) == bg(125, 162));                  // the bar below the passage covers his feet
    clearRoom(); black(120, 144, 16, 16);
    show(120, 150, never);
    CHECK(at(125, 155) == kLinkC && at(125, 163) == bg(125, 163));

    // 29d. left regular door (black passage x 16..24, floor x 24..32): the game hid the left half behind the background (x < 8 + ...): the passage wall is covered by Link,
    // the frame bar / outer wall (x < 16) still covers him
    clearRoom(); black(16, 80, 8, 16);
    show(12, 80, [](int x, int) { return x < 20; });      // (the game: the left half behind the background)
    CHECK(at(18, 88) == kLinkC);                          // x 16..19: in front again (the passage)
    CHECK(at(13, 88) == bg(13, 88));                      // x < 16: behind the frame
    CHECK(at(25, 88) == kLinkC);
    // the right door mirrored (black x 232..240, floor x 224..232, frame x >= 240)
    clearRoom(); black(232, 80, 8, 16);
    show(232, 80, [](int x, int) { return x >= 236; });
    CHECK(at(237, 88) == kLinkC && at(245, 88) == bg(245, 88) && at(233, 88) == kLinkC);

    // 29e. left hole (black x 19..32, y 82..94; the wall around it is lighter): everything non-black covers Link; above a covered pixel of the column he is covered too
    clearRoom(); black(19, 82, 13, 12); black(26, 79, 1, 2);   // (a black speck in the wall above the hole: x 26, y 79..80)
    show(24, 78, never);
    CHECK(at(28, 79) == bg(28, 79));                      // wall above the hole: covered
    CHECK(at(30, 88) == kLinkC);                          // inside the hole
    CHECK(at(26, 79) == bg(26, 79) && at(26, 80) == bg(26, 80));   // the specks above a covered pixel of the column: covered (not Link)
    CHECK(at(30, 94) == bg(30, 94));                      // below the hole's edge: wall covers
    // Link beside the hole (x 33+) is in front
    show(34, 80, never);
    CHECK(at(40, 80) == kLinkC);
    // right hole mirrored
    clearRoom(); black(224, 82, 13, 12); black(229, 79, 1, 2);
    show(220, 78, never);
    CHECK(at(232, 79) == bg(232, 79) && at(229, 79) == bg(229, 79) && at(229, 80) == bg(229, 80));
    CHECK(at(228, 88) == kLinkC);

    // 29f. a scroll: the new room's hole, still sliding in, covers Link like any other (the room slides left by 128 px: its left hole is at x 147..160)
    clearRoom(); black(147, 82, 13, 12);
    gScrollX = 0;
    show(140, 78, never);
    t.setState(7, 0x24, 1); t.frames(dw, 2, aspect);                                  // (the picture lags a frame)
    for (int k = 0; k < 32; k++) { gScrollX += 4; t.frames(dw, 1, aspect); }
    CHECK(dw.slideX == 128);
    CHECK(at(145, 79) == bg(145, 79));                    // (the wall above the hole in the new room)
    CHECK(at(152, 88) == kLinkC);
    gScrollX = 0;

    // 29g. Link 6 px beside the hole's columns (he cannot enter it): his head over the wall is left to the game
    clearRoom(); black(120, 16, 16, 16);
    show(114, 24, never);
    CHECK(at(116, 28) == kLinkC);

    // 29h. the sword (slot $0D, sprite entry 30) pointing up into the hole: hidden where the wall covers it, shown over the black
    clearRoom(); black(120, 16, 16, 16);
    show(120, 20, never, 5, [&] {
        nes->ram[kObjState + kSlotSword] = 1; nes->ram[kObjX + kSlotSword] = 120; nes->ram[kObjY + kSlotSword] = (uint8_t)(4 + 63);
        for (int y = 4; y < 20; y++) for (int x = 124; x < 132; x++) {
            nes->ppu.sprCol[(size_t)(64 + y) * Ppu::SPR_W + x] = 0x27; nes->ppu.sprOam[(size_t)(64 + y) * Ppu::SPR_W + x] = 30;
            nes->ppu.framebuffer[(size_t)(64 + y) * Ppu::W + x] = Ppu::nesColor(0x27);
        }
    });
    CHECK(at(126, 8) == bg(126, 8));                      // over the wall above the hole
    CHECK(at(126, 18) == Ppu::nesColor(0x27));            // over the black hole
    nes->ram[kObjState + kSlotSword] = 0;

    // 29i. sprites past the room's top/bottom (fire, smoke) are drawn into the margin when the status bar is an overlay; Link and the status bar's sprites are not
    {
        Widescreen dm; dm.init(*nes, err); dm.unfogAnim = false; dm.cloudShadow = false; dm.lighting = false; dm.fog = false; dm.caveBox = false; dm.dayClock.frozen = true;
        clearRoom();
        auto put = [&](int py, int px, uint8_t oamIdx) { nes->ppu.sprCol[(size_t)py * Ppu::SPR_W + px] = 0x27; nes->ppu.sprOam[(size_t)py * Ppu::SPR_W + px] = oamIdx; };
        for (int y = 0; y < Ppu::H; y++) for (int x = 0; x < Ppu::W; x++) nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = y < 64 ? kBlack : room[(size_t)(y - 64) * 256 + x];
        gBg.assign(nes->ppu.framebuffer, nes->ppu.framebuffer + (size_t)Ppu::W * Ppu::H);
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        for (int i = 0; i < 64; i++) nes->ppu.oam[i * 4] = 0xFF;
        put(58, 100, 30); put(245, 100, 30); put(58, 110, kLinkOam0); put(58, 120, 0);
        t.setState(5, 0x23, 1); t.frames(dm, 6, aspect);
        const auto& cc = c;
        const int rt = (FRAME_H - ROOM_H) / 2;
        CHECK(cc.pixels[(size_t)(rt + 58 - 64) * cc.w + 100] == Ppu::nesColor(0x27));    // above the room
        CHECK(cc.pixels[(size_t)(rt + 245 - 64) * cc.w + 100] == Ppu::nesColor(0x27));   // below it
        CHECK(cc.pixels[(size_t)(rt + 58 - 64) * cc.w + 110] != Ppu::nesColor(0x27));    // Link's
        CHECK(cc.pixels[(size_t)(rt + 58 - 64) * cc.w + 120] != Ppu::nesColor(0x27));    // entry 0 (the status bar's)
    }

    // 29j. sprites the PPU left out (more than 8 on a line: the corner torches' rows at the top/bottom walls) are drawn in a dungeon, over the wall; not the status bar's
    clearRoom();
    {
        auto put = [&](int py, int px, uint8_t col, uint8_t oamIdx) { nes->ppu.dropCol[(size_t)py * Ppu::SPR_W + px] = col; nes->ppu.dropOam[(size_t)py * Ppu::SPR_W + px] = oamIdx; };
        std::memset(nes->ppu.dropCol, 0xFF, sizeof nes->ppu.dropCol); std::memset(nes->ppu.dropOam, 0xFF, sizeof nes->ppu.dropOam);
        for (int i = 0; i < 64; i++) nes->ppu.oam[i * 4] = 0xFF;
        put(64 + 160, 100, 0x27, 30); put(64 + 20, 100, 0x27, 30); put(64 + 160, 101, 0x27, 0);   // (entry 0 is not the status bar's: its sprites are at y 31)
        nes->ppu.oam[0] = 0x1F; nes->ppu.oam[3] = 0x7C; put(64 + 160, 102, 0x27, 0);
        show(60, 60, never);
        put(64 + 160, 100, 0x27, 30); put(64 + 20, 100, 0x27, 30); put(64 + 160, 102, 0x27, 0);
        nes->ppu.oam[0] = 0x1F; nes->ppu.oam[3] = 0x7C; nes->ppu.oam[1] = 0; nes->ppu.oam[2] = 0;
        t.frames(dw, 3, aspect);
        CHECK(at(100, 160) == Ppu::nesColor(0x27) && at(100, 20) == Ppu::nesColor(0x27));   // bottom and top wall
        CHECK(at(102, 160) != Ppu::nesColor(0x27));                                          // a status bar sprite
        nes->ppu.sprCol[(size_t)(64 + 20) * Ppu::SPR_W + 100] = 0x16; nes->ppu.sprOam[(size_t)(64 + 20) * Ppu::SPR_W + 100] = 31;   // one the PPU did draw there: stays
        nes->ppu.framebuffer[(size_t)(64 + 20) * Ppu::W + 100] = kLinkC;
        t.frames(dw, 3, aspect);
        CHECK(at(100, 20) == kLinkC);
        std::memset(nes->ppu.dropCol, 0xFF, sizeof nes->ppu.dropCol); std::memset(nes->ppu.dropOam, 0xFF, sizeof nes->ppu.dropOam);
    }

    // 29k. Link's pixels the PPU left out (more than 8 sprites on a line) go through the same rules: covered by the lintel, shown in the passage, and drawn at all
    clearRoom(); black(120, 16, 16, 8);
    show(116, 12, [](int, int) { return true; });
    for (int y = 12; y < 28; y++) for (int x = 116; x < 132; x++) {
        const size_t id = (size_t)(64 + y) * Ppu::SPR_W + x;
        nes->ppu.dropCol[id] = nes->ppu.sprCol[id]; nes->ppu.dropOam[id] = nes->ppu.sprOam[id]; nes->ppu.sprCol[id] = 0xFF; nes->ppu.sprOam[id] = 0xFF;
    }
    t.frames(dw, 3, aspect);
    CHECK(at(125, 13) == bg(125, 13));                    // the lintel covers him
    CHECK(at(125, 20) == kLinkC);                         // in the passage he is drawn although the PPU left him out
    CHECK(at(125, 27) == kLinkC);
    std::memset(nes->ppu.dropCol, 0xFF, sizeof nes->ppu.dropCol); std::memset(nes->ppu.dropOam, 0xFF, sizeof nes->ppu.dropOam);


    // 29l. the sprites the PPU left out (top/bottom wall flames) are replayed with the room while it scrolls (the game hides every sprite then), sideways and up/down
    for (int dir = 0; dir < 2; dir++) {
        clearRoom();
        std::memset(nes->ppu.dropCol, 0xFF, sizeof nes->ppu.dropCol); std::memset(nes->ppu.dropOam, 0xFF, sizeof nes->ppu.dropOam);
        const int sx = 100, sy = dir == 0 ? 20 : 150;
        nes->ppu.dropCol[(size_t)(64 + sy) * Ppu::SPR_W + sx] = 0x27; nes->ppu.dropOam[(size_t)(64 + sy) * Ppu::SPR_W + sx] = 30;
        gScrollX = gScrollY = 0;
        show(60, 60, never);
        CHECK(at(sx, sy) == Ppu::nesColor(0x27));
        std::memset(nes->ppu.dropCol, 0xFF, sizeof nes->ppu.dropCol); std::memset(nes->ppu.dropOam, 0xFF, sizeof nes->ppu.dropOam);
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        t.setState(7, 0x24, 1); t.frames(dw, 2, aspect);
        for (int k = 0; k < 8; k++) { if (dir == 0) gScrollX += 4; else gScrollY += 4; t.frames(dw, 1, aspect); }
        const int mx = dir == 0 ? dw.slideX : 0, my = dir == 1 ? dw.slideY : 0;
        CHECK(dir == 0 ? mx == 32 : my == 32);
        CHECK(at(sx - mx, sy - my) == Ppu::nesColor(0x27));        // it moved with the room
        gScrollX = gScrollY = 0;
    }


    // 29m. the PPU eases the up/down scroll of dungeons (smoothV) only with the widescreen on, in a dungeon, and when the option is on
    {
        const uint8_t lv = nes->ram[kCurLevel];
        nes->ram[kCurLevel] = 1; dw.prepareFrame(*nes); CHECK(nes->ppu.smoothV);
        dw.smoothScroll = false; dw.prepareFrame(*nes); CHECK(!nes->ppu.smoothV);
        dw.smoothScroll = true; dw.enabled = false; dw.prepareFrame(*nes); CHECK(!nes->ppu.smoothV);
        dw.enabled = true; nes->ram[kCurLevel] = 0; dw.prepareFrame(*nes); CHECK(!nes->ppu.smoothV);
        nes->ram[kCurLevel] = lv; nes->ppu.smoothV = false;
    }


    // 29n. Reflections while a dungeon room scrolls up/down: the old room keeps its reflection until its water has left the screen (its wall is off the top by then: the
    // recorded picture is the source), the room that scrolls in shows its reflection as soon as its water start is on screen (going up the game has not laid it out)
    {
        constexpr uint32_t kMoat = 0xFF2040C0;
        Widescreen sw; sw.init(*nes, err); sw.unfogAnim = false; sw.cloudShadow = false; sw.lighting = false; sw.fog = false; sw.hudOverlay = false; sw.caveBox = false; sw.dayClock.frozen = true;
        auto setTile = [&](int col, int row, uint8_t id) { nes->cart.cpuWrite((uint16_t)(0x6530 + col * 22 + row), id); };
        auto tileAt = [&](int col, int row) { const bool fl = col >= 4 && col < 28 && row >= 4 && row < 18; const bool m = fl && !(col >= 14 && col < 18 && row < 7); return m ? 0xF4 : fl ? 0x24 : 0xE0; };
        bool poison = false;                                                                   // (the wall's colour changes while it scrolls: what its reflection shows must not)
        auto colorOf = [&](int id, int x, int y, bool old) -> uint32_t { return id == 0xF4 ? kMoat : id == 0x24 ? ((x / 8 + y / 8) % 2 ? 0xFF30A030 : 0xFF28902A) : (poison && old) ? 0xFF10E0E0 : ((y / 8) % 2 ? 0xFFD03020 : 0xFFB02818); };
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        auto tinted = [&](uint32_t p) { return p != kMoat && (p >> 16 & 255) > 0x30 && (p >> 8 & 255) < 0x60; };      // blue with the wall's red mixed in
        for (int dirn = 0; dirn < 2; dirn++) {                                                  // 0 = up, 1 = down
            for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++) setTile(col, row, (uint8_t)tileAt(col, row));
            gScrollX = gScrollY = 0; std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            auto paint = [&](int dy) {                                                          // the picture with the room shifted by dy (dy < 0: the next room above)
                for (int y = 0; y < Ppu::H; y++) for (int x = 0; x < Ppu::W; x++) {
                    uint32_t p = 0xFF000000;
                    if (y >= 64) { int ry = y - 64 + dy; const bool old = ry >= 0 && ry < 176; if (ry < 0) ry += 176; else if (ry >= 176) ry -= 176; p = colorOf(tileAt(x / 8, ry / 8), x, ry, old); }
                    nes->ppu.framebuffer[(size_t)y * Ppu::W + x] = nes->ppu.bgColor[(size_t)y * Ppu::W + x] = p;
                }
                gBg.assign(nes->ppu.framebuffer, nes->ppu.framebuffer + (size_t)Ppu::W * Ppu::H);
            };
            t.setState(5, 0x23, 1); paint(0);
            nes->ram[kObjX] = 60; nes->ram[kObjY] = 64 + 13;                                  // Link on the wall's edge above the water: his reflection is in it
            for (int y = 14; y < 30; y++) for (int x = 60; x < 76; x++) { nes->ppu.sprCol[(size_t)(64 + y) * Ppu::SPR_W + x] = 0x30; nes->ppu.sprOam[(size_t)(64 + y) * Ppu::SPR_W + x] = kLinkOam0; nes->ppu.framebuffer[(size_t)(64 + y) * Ppu::W + x] = Ppu::nesColor(0x30); }
            t.frames(sw, 8, aspect);
            const int left = (c.w - 256) / 2;
            const Canvas rest = c;
            t.setState(7, 0x24, 1); nes->ram[kObjX] = 60; nes->ram[kObjY] = 64 + 13;             // (setState moves Link)
            t.frames(sw, 1, aspect);                                                              // (the picture lags a frame: change it after)
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);   // (the game hides Link in a scroll)
            paint(0); t.frames(sw, 1, aspect);
            const int sgn = dirn == 0 ? -1 : 1;
            bool atStart = false, mid = false; int differ = -1;
            poison = true;                                                                    // (from the first moving frame on: the frames before it are still the last frames of play)
            for (int k = 1; k <= 80; k++) {
                gScrollY += 2 * sgn; paint(gScrollY); t.frames(sw, 1, aspect);
                const int dy = sw.slideY;
                CHECK(dy == gScrollY);
                if (k == 10 && dirn == 1) {                                     // Link's reflection does not go on alone (the game hides him in a scroll): whitish pixels under his place at rest, none while scrolling
                    int whiteRest = 0, whiteNow = 0;
                    for (int ry = 32; ry < 42; ry++) for (int x = 60; x < 76; x++) {
                        whiteRest += (rest.pixels[(size_t)(64 + ry) * c.w + left + x] >> 16 & 255) > 0x70;
                        const int yc = ry - dy; if (yc >= 0 && yc < 176) whiteNow += (c.pixels[(size_t)(64 + yc) * c.w + left + x] >> 16 & 255) > 0x70;
                    }
                    CHECK(whiteRest > 60); CHECK_EQ(whiteNow, 0);
                }
                if (k == 30) {                                                  // nothing of the water on the bars above and below the playable screen
                    int spill = 0;
                    for (int y = std::max(0, 64 - 12); y < 64; y++) for (int x = 40; x < 216; x++) spill += c.pixels[(size_t)y * c.w + left + x] != rest.pixels[(size_t)y * c.w + left + x];
                    for (int y = 64 + 176; y < std::min(c.h, 64 + 176 + 12); y++) for (int x = 40; x < 216; x++) spill += c.pixels[(size_t)y * c.w + left + x] != rest.pixels[(size_t)y * c.w + left + x];
                    CHECK_EQ(spill, 0);
                }
                if (k == 10) {                                                  // the room that scrolls away keeps the water exactly as it looked at rest (waves and all)
                    differ = 0;
                    for (int yc = 0; yc < 176; yc++) { const int ry = yc + dy; if (ry < 40 || ry >= 120) continue; for (int x = 40; x < 216; x++) if (x < 50 || x >= 86) differ += c.pixels[(size_t)(64 + yc) * c.w + left + x] != rest.pixels[(size_t)(64 + ry) * c.w + left + x]; }
                }
                // water at x 56..64, room y 40..: its wall (base y 32) is above the screen when dy > 32
                if (dirn == 1 && dy == 48) { for (int yc = 2; yc < 22; yc++) mid |= tinted(c.pixels[(size_t)(64 + yc) * c.w + left + 60]); }
                // the next room above: its water start (room y 32) is at screen row 32 - 176 - dy... on screen from dy = -148 on
                if (dirn == 0 && dy == -156) { for (int yc = 14; yc < 24; yc++) atStart |= tinted(c.pixels[(size_t)(64 + yc) * c.w + left + 60]); }
            }
            CHECK_EQ(differ, 0);
            if (dirn == 1) CHECK(mid);                       // (the old room's reflection, its wall is 16 px above the screen)
            if (dirn == 0) CHECK(atStart);                   // (the new room's reflection, the wall it mirrors is not on the screen yet)
            gScrollX = gScrollY = 0; poison = false;
        }
    }

    std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
    gBg.clear();
}
