// Widescreen integration tests: the bomb flash, sprites past the borders, the sword-beam splash, wrapped sprites, Link coming out of a cave.
// Sections run in numeric order (see test_wide.cpp) on one shared Ctx: later sections rely on state (fog discovered,
// palettes, RAM) left by earlier ones, so do not reorder them.
#include <algorithm>
#include "test_wide_common.h"

void section16(Ctx& t) {
    SECTION_BINDINGS
    // 16. Animations: the bomb flash (the game's grayscale bit) is a circle of gray around the bomb that reaches the
    //     neighbouring screens instead of tinting the whole picture; the status bar isn't touched; sprites hanging over
    //     the top/right/bottom of the screen are completed in the surroundings; sprites of the scene don't end up in the
    //     status bar.
    {
        Widescreen fx; fx.init(*nes, err); fx.unfogAnim = false; fx.cloudShadow = false; fx.lighting = false; fx.viewHeight = 448; fx.fog = false; fx.minimapStyle = 0;
        nes->ppu.palette[5] = 0x2A; nes->ppu.palette[7] = 0x21; nes->ppu.palette[11] = 0x16; nes->ppu.palette[9] = 0x2A;
        nes->ppu.lineMask[100] = 0x1E;
        for (int i = 0; i < Ppu::W * 64; i++) nes->ppu.framebuffer[i] = 0xFF000000;
        nes->ppu.framebuffer[45 * 256 + 180] = 0xFFFFFFFF;
        for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF123456;
        for (int i = 0; i < 13; i++) nes->ram[0xAC + i] = 0;
        setState(5, 0x23); nes->ram[0x70] = 120; nes->ram[0x84] = 141;
        const double wide169 = 16.0 / 9.0;                                       // 796 x 448 canvas (no status bar band)
        frames(fx, 3, wide169);
        CHECK_EQ(c.w, 796); CHECK_EQ(c.h, 448);
        const std::vector<uint32_t> calm = c.pixels;
        const OverlayImage calmLife = fx.overlay.images[0];

        // A bomb goes off at x=200 (80 px right of Link): the game's grayscale bit is on for a few frames.
        nes->ram[0xAC + 0x10] = 0x13; nes->ram[0x70 + 0x10] = 200; nes->ram[0x84 + 0x10] = 141;      // (bombs live in object slots $10 and $11)
        nes->ppu.lineMask[100] = 0x1F;
        frames(fx, 2, wide169);
        auto isGray = [](uint32_t p) { int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255; return std::abs(r - g) <= 2 && std::abs(g - b) <= 2; };   // (the NES whites are $FFFEFF)
        // Link is at the middle of the canvas (398, 224); the bomb is 80 px right of him: canvas (478, 224).
        CHECK_EQ(px(478, 224), Ppu::nesColor(0x16 & 0x30));                       // the game's picture at the bomb: gray
        {   // the surroundings flash too: find a coloured scenery pixel 120-150 px from the bomb; during the flash it is gray
            int found = 0;
            for (int x = 598; x <= 628; x++) {
                const uint32_t before = calm[(size_t)224 * c.w + x];
                if (isGray(before)) continue;
                found++;
                CHECK(isGray(px(x, 224)));
                CHECK(px(x, 224) != before);
            }
            CHECK(found > 5);
        }
        CHECK_EQ(px(700, 224), calm[(size_t)224 * c.w + 700]);                    // 222 px away: beyond the 200 px radius, untouched
        CHECK_EQ(px(795, 0), calm[(size_t)0 * c.w + 795]);                         // far corner: untouched
        CHECK_EQ(px(0, 447), calm[(size_t)447 * c.w + 0]);
        // The status bar is unaffected.
        CHECK(fx.overlay.images[0].pixels == calmLife.pixels);
        nes->ppu.lineMask[100] = 0x1E;                                            // flash over
        frames(fx, 2, wide169);
        CHECK(c.pixels == calm);
        nes->ram[0xAC + 0x10] = 0;

        // The second bomb slot ($11) works the same.
        nes->ram[0xAC + 0x11] = 0x13; nes->ram[0x70 + 0x11] = 200; nes->ram[0x84 + 0x11] = 141;
        nes->ppu.lineMask[100] = 0x1F;
        frames(fx, 2, wide169);
        CHECK_EQ(px(478, 224), Ppu::nesColor(0x16 & 0x30));
        nes->ram[0xAC + 0x11] = 0; nes->ppu.lineMask[100] = 0x1E;
        frames(fx, 2, wide169);
        CHECK(c.pixels == calm);

        // A monster slot in state $13 is not a bomb: the circle is around Link (canvas 398,224) rather than around the monster
        // 120 px to his left. Scenery 122-150 px right of Link is inside the circle; it would be 242+ px from the monster.
        nes->ram[0xAC + 3] = 0x13; nes->ram[0x70 + 3] = 0; nes->ram[0x84 + 3] = 141;
        nes->ppu.lineMask[100] = 0x1F;
        frames(fx, 2, wide169);
        {
            int found = 0;
            for (int x = 520; x <= 548; x++) {
                const uint32_t before = calm[(size_t)224 * c.w + x];
                if (isGray(before)) continue;
                found++;
                CHECK(isGray(px(x, 224)));
            }
            CHECK(found > 5);
        }
        nes->ram[0xAC + 3] = 0; nes->ppu.lineMask[100] = 0x1E;
        frames(fx, 2, wide169);
        CHECK(c.pixels == calm);

        // Sprites hanging over the edges of the game's picture. Link is centred, so the picture's corner is at canvas
        // (270, 139): x = 398 - 128, y = 224 - 85 (its row 64 of the game's picture).
        const int roomX = 398 - 128, roomY = 224 - 85;
        nes->ppu.oam[80] = 58; nes->ppu.oam[81] = 1; nes->ppu.oam[82] = 0; nes->ppu.oam[83] = 100;      // OAM 20: a scene sprite
        nes->ppu.sprCol[61 * Ppu::SPR_W + 100] = 0x16; nes->ppu.sprOam[61 * Ppu::SPR_W + 100] = 20;     // above the top edge
        nes->ppu.sprCol[105 * Ppu::SPR_W + 258] = 0x2A; nes->ppu.sprOam[105 * Ppu::SPR_W + 258] = 20;   // past the right edge
        nes->ppu.sprCol[245 * Ppu::SPR_W + 30] = 0x12; nes->ppu.sprOam[245 * Ppu::SPR_W + 30] = 20;     // below the bottom edge
        frames(fx, 2, wide169);
        CHECK_EQ(px(roomX + 100, roomY + (61 - 64)), Ppu::nesColor(0x16));
        CHECK_EQ(px(roomX + 258, roomY + (105 - 64)), Ppu::nesColor(0x2A));
        CHECK(px(roomX + 30, roomY + (245 - 64)) != Ppu::nesColor(0x12));        // (not below the bottom edge: the game parks hidden sprites there)
        // Pixels inside the game's own picture are left to the picture (no double drawing):
        nes->ppu.sprCol[100 * Ppu::SPR_W + 100] = 0x2A; nes->ppu.sprOam[100 * Ppu::SPR_W + 100] = 20;
        frames(fx, 2, wide169);
        CHECK_EQ(px(roomX + 100, roomY + (100 - 64)), 0xFF123456);

        // The status bar's own sprites (OAM entries at the item icons / map marker positions, and sprite 0) belong to the
        // status bar: not drawn into the world, but part of the bar's pieces. Scene sprites never reach the bar.
        nes->ppu.oam[84] = 30; nes->ppu.oam[85] = 1; nes->ppu.oam[86] = 0; nes->ppu.oam[87] = 40;       // OAM 21: map marker at (40,31)
        nes->ppu.sprCol[31 * Ppu::SPR_W + 40] = 0x2A; nes->ppu.sprOam[31 * Ppu::SPR_W + 40] = 21;
        nes->ppu.sprCol[35 * Ppu::SPR_W + 50] = 0x16; nes->ppu.sprOam[35 * Ppu::SPR_W + 50] = 20;       // a scene sprite over the bar
        nes->ppu.sprCol[39 * Ppu::SPR_W + 88] = 0x30; nes->ppu.sprOam[39 * Ppu::SPR_W + 88] = 0;        // the split sprite
        frames(fx, 2, wide169);
        { const OverlayImage& m = fx.overlay.images[2];                           // the game's own map piece (style 0): (16,16,64,40)
          CHECK_EQ(m.pixels[(31 - 16 + 1) * m.w + (40 - 16 + 1)], Ppu::nesColor(0x2A));   // the marker is part of it
          CHECK_EQ(m.pixels[(35 - 16 + 1) * m.w + (50 - 16 + 1)], 0xB0000000);            // the scene sprite is not (just the panel)
        }
        nes->ppu.oam[100] = 29; nes->ppu.oam[101] = 1; nes->ppu.oam[102] = 0; nes->ppu.oam[103] = 30;   // OAM 25: an enemy at (30,30), over the old map
        nes->ppu.sprCol[31 * Ppu::SPR_W + 30] = 0x28; nes->ppu.sprOam[31 * Ppu::SPR_W + 30] = 25;
        frames(fx, 2, wide169);
        CHECK_EQ(px(roomX + 30, roomY + (31 - 64)), Ppu::nesColor(0x28));        // drawn above the screen, not lost
        { const OverlayImage& m2 = fx.overlay.images[2];
          CHECK_EQ(m2.pixels[(31 - 16 + 1) * m2.w + (30 - 16 + 1)], 0xB0000000);   // and not part of the bar
        }
        { const OverlayImage& k = fx.overlay.images[3];                           // counters (88,24,32,32): sprite 0 isn't there either
          CHECK(k.pixels[(39 - 24 + 1) * k.w + (88 - 88 + 1)] != Ppu::nesColor(0x30));
        }
    }
}

void section19(Ctx& t) {
    SECTION_BINDINGS
    // 19. Sprite fixes. Common setup: 8x16 sprites, a solid sprite tile ($30: colour 3) in the CHR, palette for the sprites.
    {
        for (int a = 0x300; a < 0x320; a++) { nes->cart.ppuWrite(a, 0xFF); nes->cart.ppuWrite(a + 8, 0xFF); }
        for (int a = 0x308; a < 0x310; a++) nes->cart.ppuWrite(a, 0xFF);
        nes->ppu.palette[0x13] = 0x25; nes->ppu.palette[0x17] = 0x2C;               // sprite palette 0 / 1, colour 3
        for (int i = 0; i < Ppu::H; i++) nes->ppu.lineCtrl[i] = 0x20;
        nes->ppu.palette[5] = 0x2A; nes->ppu.palette[7] = 0x21; nes->ppu.palette[11] = 0x16;
        nes->ppu.lineMask[100] = 0x1E;
        for (int i = 0; i < Ppu::W * 64; i++) nes->ppu.framebuffer[i] = 0xFF654321;
        for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF123456;
        for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
            nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, decoded[col * 22 + row]);
        for (int i = 0; i < 64; i++) nes->ppu.oam[i * 4] = 0xFF;
        for (int i = 0; i < 13; i++) nes->ram[0xAC + i] = 0;
        g_link.on = false; g_extra.clear();
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        auto setOam = [&](int i, int x, int y, int tile, int attr) { nes->ppu.oam[i * 4] = y; nes->ppu.oam[i * 4 + 1] = tile; nes->ppu.oam[i * 4 + 2] = attr; nes->ppu.oam[i * 4 + 3] = x; };
        // a w x h block of sprite pixels in the colour layer and the sprite layer (screen coordinates; x may go up to 263)
        auto block = [&](int oamIdx, int x0, int y0, int w, int h, uint32_t color, uint8_t nesIdx) {
            for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) {
                if (x < 256) g_extra.push_back({x, y, color});
                nes->ppu.sprCol[y * Ppu::SPR_W + x] = nesIdx; nes->ppu.sprOam[y * Ppu::SPR_W + x] = oamIdx;
            }
        };
        auto clearAll = [&]() {
            g_extra.clear(); for (int i = 0; i < 64; i++) nes->ppu.oam[i * 4] = 0xFF;
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            for (int i = 0; i < 0x13; i++) nes->ram[0xAC + i] = 0;       // (all object slots, weapons included)
        };

        // ---- Item 2: coming out of a cave, Link's stale sprites (cave position) are not drawn.
        // Spawn point of room $23 here: room-local centre (8, 21): canvas (256, 88); room origin on the canvas = (248, 67).
        {
            Widescreen w; w.init(*nes, err); w.unfogAnim = false; w.cloudShadow = false; w.lighting = false; w.viewHeight = 176;
            clearAll();
            setState(4, 0x23); nes->ram[0x5A] = 1; nes->ram[0x70] = 100; nes->ram[0x84] = 90;          // Link still at the cave spot
            setOam(18, 100, 89, 3, 0); setOam(19, 108, 89, 3, 0);                                       // his two sprites
            block(18, 104, 100, 4, 4, 0xFFFFFEFF, 0x30);
            block(19, 258, 100, 4, 1, 0xFFFFFEFF, 0x30);                                                 // (a bit past the right edge)
            g_extra.push_back({1, 1, 0});                                                                // (harmless)
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK_EQ(px(248 + 104, 67 + 100 - 64), 0xFF123456);                                         // hidden: the background is there
            CHECK(px(248 + 258, 67 + 100 - 64) != 0xFFFFFEFF);
            // The same sprites with Link close to the spot where the game will put him (he walks up out of the cave, 16 px
            // below it): they stay.
            clearAll();
            setState(4, 0x23); nes->ram[0x5A] = 1; nes->ram[0x70] = 0; nes->ram[0x84] = 0x4D + 16;      // 16 px below the spawn point
            setOam(18, 0, 0x4D + 15, 3, 0);
            block(18, 0, 0x4D + 16, 4, 4, 0xFFFFFEFF, 0x30);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK_EQ(px(248 + 0, 67 + 0x4D + 16 - 64), 0xFFFFFEFF);
            // Normal play: nothing is hidden even though the camera... (exit type 0)
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 100; nes->ram[0x84] = 90;
            setOam(18, 100, 89, 3, 0); block(18, 104, 100, 4, 4, 0xFFFFFEFF, 0x30);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK_EQ(px(252, 90), 0xFFFFFEFF);                                                          // Link centred on (108, 34): his pixel (104,100)
        }

        // ---- Item 4: stabbing at the left border. The sword sprite (OAM X = $F8, i.e. -8) wraps to the right border in the
        // PPU; it is drawn where it belongs instead, in the screen to the left, and the wrapped copy is gone.
        {
            Widescreen w; w.init(*nes, err); w.unfogAnim = false; w.cloudShadow = false; w.lighting = false; w.viewHeight = 176; w.fog = false;
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 4; nes->ram[0x84] = 100; nes->ram[0xAC + 0x0D] = 1;   // Link at the left border, sword out
            setOam(20, 0xF8, 99, 0x30, 0);                                                              // the sword, wrapped
            block(20, 248, 100, 8, 16, 0xFFAA5500, 0x25);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            // Link's centre (12, 44) is the canvas centre (256, 88): room origin (244, 44).
            CHECK_EQ(px(244 + 250, 44 + 36 + 4), 0xFF123456);                                           // nothing at the right border any more
            CHECK_EQ(px(244 - 8, 44 + 36), Ppu::nesColor(0x25));                                        // the sword, outside the left border
            CHECK_EQ(px(244 - 1, 44 + 36 + 15), Ppu::nesColor(0x25));                                   // (all 8x16 of it)
            CHECK(px(244 - 9, 44 + 36) != Ppu::nesColor(0x25)); CHECK(px(244 + 0, 44 + 36) != Ppu::nesColor(0x25));
            // Partly wrapped (X = $FC: true -4): the part that comes back inside the picture is drawn too.
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 4; nes->ram[0x84] = 100; nes->ram[0xAC + 0x0D] = 1;
            setOam(20, 0xFC, 99, 0x30, 0); block(20, 252, 100, 4, 16, 0xFFAA5500, 0x25);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK_EQ(px(244 - 4, 44 + 36), Ppu::nesColor(0x25)); CHECK_EQ(px(244 + 3, 44 + 36), Ppu::nesColor(0x25));
            CHECK(px(244 + 4, 44 + 36) != Ppu::nesColor(0x25));
            CHECK_EQ(px(244 + 253, 44 + 36), 0xFF123456);
            // An enemy standing at the right border while Link is at the left one is NOT mistaken for a wrapped sprite:
            // too far from Link.
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 4; nes->ram[0x84] = 100; nes->ram[0xAC + 0x0D] = 1;
            setOam(20, 240, 99, 0x30, 0); block(20, 240, 100, 8, 16, 0xFFAA5500, 0x25);               // 20 px from Link: too far to be his sword
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK_EQ(px(244 + 244, 44 + 36), 0xFFAA5500);                                               // stays at the right border
            CHECK(px(244 - 16, 44 + 36) != Ppu::nesColor(0x25));
            // Close enough, but no weapon of Link's is out (a monster at the border): it stays too.
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 4; nes->ram[0x84] = 100;
            setOam(20, 248, 99, 0x30, 0); block(20, 248, 100, 8, 16, 0xFFAA5500, 0x25);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK_EQ(px(244 + 250, 44 + 36), 0xFFAA5500);
            CHECK(px(244 - 8, 44 + 36) != Ppu::nesColor(0x25));
            // The arrow / rod beam (object slot $12) counts as a weapon being out too.
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 4; nes->ram[0x84] = 100; nes->ram[0xAC + 0x12] = 1;
            setOam(20, 0xF8, 99, 0x30, 0); block(20, 248, 100, 8, 16, 0xFFAA5500, 0x25);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK_EQ(px(244 - 8, 44 + 36), Ppu::nesColor(0x25));
            // The same at the right border: sword at OAM X = $02 (true 258) while Link is at X = 238.
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 238; nes->ram[0x84] = 100; nes->ram[0xAC + 0x0D] = 1;
            setOam(20, 2, 99, 0x30, 0); block(20, 2, 100, 8, 16, 0xFFAA5500, 0x25);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            // Link's centre (246, 44): room origin (10, 44)
            CHECK_EQ(px(10 + 4, 44 + 36), 0xFF123456);                                                  // gone from the left border
            CHECK_EQ(px(10 + 258, 44 + 36), Ppu::nesColor(0x25));                                       // drawn past the right border
            CHECK_EQ(px(10 + 263, 44 + 36 + 15), Ppu::nesColor(0x25));
        }

        // ---- Item 3: the sword beam's splash is drawn completely, also where the game cuts it off.
        // The shot (slot $E, state $11 = spreading) is at X=0, Y=100; d = 12. The game drew only its right corners:
        // top-right (OAM X = 16, Y = 88, H flip) and bottom-right (16, 112, H+V flip), palette 1.
        {
            Widescreen w; w.init(*nes, err); w.unfogAnim = false; w.cloudShadow = false; w.lighting = false; w.viewHeight = 176; w.fog = false;
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 40; nes->ram[0x84] = 100;                               // Link elsewhere (camera: centre (48, 44), origin (208, 44))
            nes->ram[0xAC + 0x0E] = 0x11; nes->ram[0x70 + 0x0E] = 0; nes->ram[0x84 + 0x0E] = 100;
            setOam(30, 16, 88, 0x30, 0x40 | 1); setOam(31, 16, 112, 0x30, 0xC0 | 1);
            block(30, 16, 89, 8, 16, Ppu::nesColor(0x2C), 0x2C); block(31, 16, 113, 8, 16, Ppu::nesColor(0x2C), 0x2C);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            // top-left corner: X = 0 - 12 + 4 = -8, screen Y = 88 + 1 = 89; bottom-left: Y = 113. Both in the screen to the left.
            CHECK_EQ(px(208 - 8, 44 + 89 - 64), Ppu::nesColor(0x2C));  CHECK_EQ(px(208 - 1, 44 + 89 - 64 + 15), Ppu::nesColor(0x2C));
            CHECK_EQ(px(208 - 8, 44 + 113 - 64), Ppu::nesColor(0x2C)); CHECK_EQ(px(208 - 1, 44 + 113 - 64 + 15), Ppu::nesColor(0x2C));
            CHECK(px(208 - 9, 44 + 89 - 64) != Ppu::nesColor(0x2C));
            CHECK_EQ(px(208 + 16, 44 + 89 - 64), Ppu::nesColor(0x2C));                                  // the corners the game drew are still there
            // No spreading shot: no splash.
            nes->ram[0xAC + 0x0E] = 0;
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK(px(208 - 8, 44 + 89 - 64) != Ppu::nesColor(0x2C));
            // A magic shot (state bit 7) doesn't spread: not drawn.
            nes->ram[0xAC + 0x0E] = 0x91;
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK(px(208 - 8, 44 + 89 - 64) != Ppu::nesColor(0x2C));
            nes->ram[0xAC + 0x0E] = 0;

            // A corner whose X wrapped (shot at X=250, d=8: the top-right corner is at 250+8+4 = 262 -> OAM X 6, next to the left
            // border): the game's copy there is hidden, the corner is drawn at its real place past the right border.
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 200; nes->ram[0x84] = 100;
            nes->ram[0xAC + 0x0E] = 0x11; nes->ram[0x70 + 0x0E] = 250; nes->ram[0x84 + 0x0E] = 100;
            setOam(30, 6, 92, 0x30, 0x40 | 1); block(30, 6, 93, 8, 16, Ppu::nesColor(0x2C), 0x2C);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK(px(48 + 6, 44 + 93 - 64) != Ppu::nesColor(0x2C));                                     // the wrapped copy is gone
            CHECK_EQ(px(48 + 262, 44 + 93 - 64), Ppu::nesColor(0x2C));                                   // drawn where it belongs
            // The splash ended this frame (RAM: state 0), but the picture is still its last frame: same result, no flash.
            nes->ram[0xAC + 0x0E] = 0;
            syncFb(*nes); w.render(*nes, aspect, c);
            CHECK(px(48 + 6, 44 + 93 - 64) != Ppu::nesColor(0x2C));
            CHECK_EQ(px(48 + 262, 44 + 93 - 64), Ppu::nesColor(0x2C));
            for (int k = 0; k < 2; k++) { syncFb(*nes); w.render(*nes, aspect, c); }                    // a frame later it is over

            // A flying beam at the right border (shot slot at X=244): its second half wrapped to OAM X 4 (left border, where Link
            // stands) is hidden there and drawn at 260.
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 200; nes->ram[0x84] = 100;
            nes->ram[0xAC + 0x0E] = 0x10; nes->ram[0x70 + 0x0E] = 244; nes->ram[0x84 + 0x0E] = 100;
            setOam(30, 4, 100, 0x30, 1); block(30, 4, 101, 8, 16, Ppu::nesColor(0x2C), 0x2C);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK(px(48 + 4, 44 + 101 - 64) != Ppu::nesColor(0x2C));
            CHECK_EQ(px(48 + 260, 44 + 101 - 64), Ppu::nesColor(0x2C));
            nes->ram[0xAC + 0x0E] = 0;

            // Link's own sprites (OAM 18, 19) are never taken for a wrapped weapon piece: Link at the left border, a beam at X=244.
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 200; nes->ram[0x84] = 100;
            nes->ram[0xAC + 0x0E] = 0x10; nes->ram[0x70 + 0x0E] = 244; nes->ram[0x84 + 0x0E] = 100;
            setOam(18, 4, 100, 0x30, 1); block(18, 4, 101, 8, 16, Ppu::nesColor(0x2C), 0x2C);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK_EQ(px(48 + 4, 44 + 101 - 64), Ppu::nesColor(0x2C));
            CHECK(px(48 + 260, 44 + 101 - 64) != Ppu::nesColor(0x2C));
            nes->ram[0xAC + 0x0E] = 0;

            // At the right border: shot at X=250, Y=100, d=8: the game's corners with spriteX >= $FC are dropped. It drew only
            // the left ones: TL (X = 250-8+4 = 246, Y 92) and BL (246, 108); the right ones (X = 258+4 -> past $FC) are missing.
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 200; nes->ram[0x84] = 100;                              // camera centre (208, 44): origin (48, 44)
            nes->ram[0xAC + 0x0E] = 0x11; nes->ram[0x70 + 0x0E] = 250; nes->ram[0x84 + 0x0E] = 100;
            setOam(30, 246, 92, 0x30, 0x00); setOam(31, 246, 108, 0x30, 0x80);
            block(30, 246, 93, 8, 16, Ppu::nesColor(0x25), 0x25); block(31, 246, 109, 8, 16, Ppu::nesColor(0x25), 0x25);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            // right corners at screen X = 250 + 8 + 4 = 262 .. 269: in the screen to the right, canvas x = 48 + 262
            CHECK_EQ(px(48 + 262, 44 + 93 - 64), Ppu::nesColor(0x25)); CHECK_EQ(px(48 + 269, 44 + 93 - 64 + 15), Ppu::nesColor(0x25));
            CHECK_EQ(px(48 + 262, 44 + 109 - 64), Ppu::nesColor(0x25));
            // At the bottom: shot at X=100, Y=225, d=20. The bottom corners' Y byte is 245 (>= $EF): the PPU never shows
            // sprites there, so the splash is cut off at the bottom of the picture. The top corners (Y 205) are fine.
            // The bottom ones are drawn below the picture. Link at (100,200): camera centre (108,144): origin (148,-56).
            clearAll();
            setState(5, 0x23); nes->ram[0x70] = 100; nes->ram[0x84] = 200;
            nes->ram[0xAC + 0x0E] = 0x11; nes->ram[0x70 + 0x0E] = 100; nes->ram[0x84 + 0x0E] = 225;
            setOam(30, 84, 205, 0x30, 0x00); setOam(31, 124, 205, 0x30, 0x40);                          // TL and TR, palette 0
            setOam(32, 84, 245, 0x30, 0x80); setOam(33, 124, 245, 0x30, 0xC0);                          // BL, BR: in the OAM, but never displayed
            block(30, 84, 206, 8, 16, Ppu::nesColor(0x25), 0x25); block(31, 124, 206, 8, 16, Ppu::nesColor(0x25), 0x25);
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK_EQ(px(148 + 84, -56 + 246 - 64), Ppu::nesColor(0x25));  CHECK_EQ(px(148 + 91, -56 + 246 - 64 + 15), Ppu::nesColor(0x25));
            CHECK_EQ(px(148 + 124, -56 + 246 - 64), Ppu::nesColor(0x25)); CHECK_EQ(px(148 + 131, -56 + 246 - 64 + 15), Ppu::nesColor(0x25));
            CHECK_EQ(px(148 + 84, -56 + 206 - 64), Ppu::nesColor(0x25));                                  // the top ones as before
            // Something that merely uses tile $30 somewhere else (no spreading shot) isn't touched.
            nes->ram[0xAC + 0x0E] = 0;
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
            CHECK(px(148 + 84, -56 + 246 - 64) != Ppu::nesColor(0x25));
        }
        clearAll();
        for (int i = 0; i < Ppu::H; i++) nes->ppu.lineCtrl[i] = 0;
    }
}

void section22(Ctx& t) {
    SECTION_BINDINGS
    // 22a. The sword may be swung next to any border on the overworld: the game's border limits for the A button
    //      (third BorderBounds set) are opened there and restored elsewhere.
    {
        const std::vector<uint8_t>& prg = nes->cart.prgData();
        const uint8_t sig[8] = {0xD6, 0x45, 0xE9, 0x07, 0xC6, 0x55, 0xD9, 0x17};
        const size_t off = (size_t)(std::search(prg.begin(), prg.end(), sig, sig + 8) - prg.begin()) + 8;
        auto bounds = [&]() { return prg[off] | (prg[off + 1] << 8) | (prg[off + 2] << 16) | ((unsigned)prg[off + 3] << 24); };
        const unsigned original = 0x1FD154BE, open = 0x00FF00FF;
        auto outer = [&]() { return prg[off - 8] | (prg[off - 7] << 8) | (prg[off - 6] << 16) | ((unsigned)prg[off - 5] << 24); };
        CHECK_EQ(outer(), 0x07E945D6u);
        CHECK_EQ(bounds(), original);
        setState(5, 0x25); frames(ws, 2, aspect);               // an overworld frame was rendered
        ws.prepareFrame(*nes);
        CHECK_EQ(outer(), 0x00FF00FFu);                         // the outer set no longer clears the buttons facing away from a border
        CHECK_EQ(bounds(), open);                               // down/right >= $FF, up/left < $00: never masked
        ws.enabled = false; ws.prepareFrame(*nes);
        CHECK_EQ(bounds(), original);                           // widescreen off: the game's own limits
        CHECK_EQ(outer(), 0x07E945D6u);
        ws.enabled = true; ws.prepareFrame(*nes);
        CHECK_EQ(bounds(), open);
        setState(5, 0x25, 1); frames(ws, 2, aspect); ws.prepareFrame(*nes);   // a dungeon
        CHECK_EQ(bounds(), original);
        setState(5, 0x25); frames(ws, 2, aspect); ws.prepareFrame(*nes);
        CHECK_EQ(bounds(), open);
    }
    // 22b. The sword next to the top or bottom border: the parts in the status bar rows / below the picture are drawn
    //      whole at their real place (the neighbouring screen) instead of being cut.
    {
        for (int a = 0x300; a < 0x320; a++) { nes->cart.ppuWrite(a, 0xFF); nes->cart.ppuWrite(a + 8, 0xFF); }
        nes->ppu.palette[0x13] = 0x25;
        for (int i = 0; i < Ppu::H; i++) nes->ppu.lineCtrl[i] = 0x20;
        Widescreen w; w.init(*nes, err); w.unfogAnim = false; w.cloudShadow = false; w.lighting = false; w.viewHeight = 176; w.fog = false;
        auto run = [&](int linkY, int oamY, bool linkInFront = false) {
            for (int i = 0; i < 64; i++) nes->ppu.oam[i * 4] = 0xFF;
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            for (int i = 0; i < 0x13; i++) nes->ram[0xAC + i] = 0;
            setState(5, 0x23); nes->ram[0x70] = 120; nes->ram[0x84] = linkY; nes->ram[0xAC + 0x0D] = 1;
            nes->ppu.oam[80] = oamY; nes->ppu.oam[81] = 0x30; nes->ppu.oam[82] = 0; nes->ppu.oam[83] = 120;
            for (int y = oamY + 1; y < oamY + 17 && y < 240; y++) for (int x = 120; x < 128; x++) {
                nes->ppu.sprCol[y * Ppu::SPR_W + x] = 0x25; nes->ppu.sprOam[y * Ppu::SPR_W + x] = 20;
            }
            if (linkInFront) { nes->ppu.sprCol[70 * Ppu::SPR_W + 124] = 0x16; nes->ppu.sprOam[70 * Ppu::SPR_W + 124] = 18; }   // Link's sprite (lower OAM index) covers this pixel
            for (int k = 0; k < 3; k++) { syncFb(*nes); w.snapCamera(); w.render(*nes, aspect, c); }
        };
        // Link at the top (Y = $45), sword up: sprite rows 56..71, the first eight in the status bar rows.
        // Link's centre (128, 13) is the canvas centre (256, 88): room origin (128, 75).
        run(0x45, 55);
        CHECK_EQ(px(128 + 124, 75 - 6), Ppu::nesColor(0x25));    // above the room's top edge
        CHECK_EQ(px(128 + 124, 75 + 6), Ppu::nesColor(0x25));    // and inside it
        // Where Link's own sprite (lower OAM index) is in front of the sword, the redrawn sword does not cover it.
        run(0x45, 55, true);
        CHECK(px(128 + 124, 75 - 6) == Ppu::nesColor(0x25)); CHECK(px(128 + 124, 75 + 6) != Ppu::nesColor(0x25));
        CHECK_EQ(px(128 + 125, 75 + 6), Ppu::nesColor(0x25));
        // Link at the bottom (Y = $D6), sword down: rows 226..241, the last two below the picture.
        // Link's centre (128, 158): room origin (128, 88 - 158) = (128, -70).
        run(0xD6, 225);
        CHECK_EQ(px(128 + 124, -70 + 176 + 1), Ppu::nesColor(0x25));   // room row 177: past the bottom edge
        for (int i = 0; i < 64; i++) nes->ppu.oam[i * 4] = 0xFF;
        for (int i = 0; i < 0x13; i++) nes->ram[0xAC + i] = 0;
        for (int i = 0; i < Ppu::H; i++) nes->ppu.lineCtrl[i] = 0;
    }
}
