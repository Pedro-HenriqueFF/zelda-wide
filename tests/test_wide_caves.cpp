// Widescreen integration tests: caves and shops (boxed and replacing), the inventory, the stairs into a dungeon, the cave backdrop.
// Sections run in numeric order (see test_wide.cpp) on one shared Ctx: later sections rely on state (fog discovered,
// palettes, RAM) left by earlier ones, so do not reorder them.
#include "test_wide_common.h"

void section06(Ctx& t) {
    SECTION_BINDINGS
    // 6. Entering a cave (mode $B). Its init phase is a transition that still shows the overworld screen: the world
    //    stays around the game's picture at the overworld size, and the camera stays put even though Link's
    //    coordinates jump to the cave's entrance position.
    setState(5, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512);
    setState(0xB, 0x23); nes->ram[0x11] = 0; nes->ram[0x70] = 0x70; nes->ram[0x84] = 0xDD;
    frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240);
    CHECK_EQ(px(128 + 10, 72), 0xFF123456);                  // the game's picture stays where the screen is
    CHECK_EQ(px(127, 100), Ppu::nesColor(0x21));             // ...with the world still drawn around it
    //    The cave scene is on screen with Link in it from submode 7: the big picture starts there, not later.
    setState(0xB, 0x23); nes->ram[0x11] = 0; nes->ram[0x13] = 6; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512);
    setState(0xB, 0x23); nes->ram[0x11] = 0; nes->ram[0x13] = 7; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    setState(5, 0x23); frames(ws, 2, aspect); CHECK_EQ(c.w, 512);             // (back outside for the next step)
    setState(0xB, 0x23); nes->ram[0x11] = 0; nes->ram[0x70] = 0x70; nes->ram[0x84] = 0xDD; frames(ws, 2, aspect);
    //    Once the walk-in is over the game is "updating": steady interior -> the big picture.
    setState(0xB, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    CHECK_EQ(px(10, 10), 0xFF654321); CHECK_EQ(px(10, 100), 0xFF123456);
    ws.interiorsBig = false; frames(ws, 2, aspect);          // option: keep the overworld view size inside caves
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240);
    CHECK_EQ(px(128 + 10, 70), 0xFF123456); CHECK_EQ(px(5, 100), 0xFF000000);
    ws.interiorsBig = true; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 256);
    //    Inventory inside the cave: whole picture at the current (big) size.
    nes->ram[0xE1] = 2; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 256); CHECK_EQ(px(10, 100), 0xFF123456);
    nes->ram[0xE1] = 0;
    //    Leaving: mode $A (fade + redraw of the overworld screen) keeps the big picture...
    setState(0xA, 0x23); nes->ram[0x11] = 0; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    //    ...but as soon as the PPU holds the overworld screen again, the overworld view takes over (no frame of the
    //    big picture with the overworld screen in it).
    for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
        nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, decoded[col * 22 + row]);
    //    (but not while the picture is still black: switching then would only make the HUD flash)
    for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF000000;
    setState(0xA, 0x23); nes->ram[0x11] = 0; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF123456;   // it fades in
    frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(px(127, 100), Ppu::nesColor(0x21));
    for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
        nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, 0);
    //    ...and walking out of the cave (mode 4, on the overworld screen) is drawn with the overworld view again.
    setState(4, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240);
    CHECK_EQ(px(128 + 10, 72), 0xFF123456); CHECK_EQ(px(127, 100), Ppu::nesColor(0x21));

    // 6b. Walking off the edge of a screen: mode 6 happens for a frame before the scroll. It must keep the world
    //     around it (this used to flash the centred box).
    setState(5, 0x23); frames(ws, 2, aspect);
    setState(6, 0x23); frames(ws, 1, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(px(127, 100), Ppu::nesColor(0x21)); CHECK_EQ(px(128 + 10, 72), 0xFF123456);

    // 6c. A screen we can't decode (live tiles don't match): safe fallback = status bar + room centred, same size.
    for (int i = 0; i < OverworldMap::TILES; i++) nes->cart.cpuWrite(0x6530 + i, 0xF4);
    setState(5, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240);
    CHECK_EQ(px(128 + 10, 70), 0xFF123456); CHECK_EQ(px(5, 100), 0xFF000000);
    for (int i = 0; i < OverworldMap::TILES; i++) nes->cart.cpuWrite(0x6530 + i, decoded[i]);
    setState(5, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(px(127, 100), Ppu::nesColor(0x21));
}

void section07(Ctx& t) {
    SECTION_BINDINGS
    // 7. Inventory screen (MenuState != 0): the canvas is left exactly as it was (the game is frozen while the menu is
    //    open, so nothing changes size) and the game's 256x240 picture goes into a framed box on top of it.
    setState(5, 0x23); frames(ws, 2, aspect); CHECK_EQ(c.w, 512);
    CHECK(!ws.overlay.active());
    const std::vector<uint32_t> sceneBefore = c.pixels;
    nes->ram[0xE1] = 8; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 240);
    CHECK(c.pixels == sceneBefore);
    CHECK(ws.overlay.dim > 0.0f);
    CHECK_EQ(ws.overlay.images.size(), (size_t)1);
    {
        const OverlayImage& img = ws.overlay.images[0];
        CHECK_EQ(img.w, 256 + 8); CHECK_EQ(img.h, 238 + 8);                 // minus the stray bottom rows
        CHECK_EQ(img.pixels[0], 0xFFFCFCFC);                               // frame
        CHECK_EQ(img.pixels[2 * img.w + 2], 0xFF000000);                   // padding
        CHECK_EQ(img.pixels[(4 + 10) * img.w + 4 + 10], 0xFF654321);       // the game's picture (status bar rows here)
        CHECK_EQ(img.pixels[(4 + 100) * img.w + 4 + 100], 0xFF123456);
        CHECK(img.maxHeightFrac < 1.0f && img.maxWidthFrac < 1.0f);        // smaller than the window
    }
    nes->ram[0xE1] = 0; frames(ws, 2, aspect);                             // closed again: no overlay, same view
    CHECK(!ws.overlay.active());
    CHECK_EQ(c.w, 512);

    // 7b. Game over: the death animation keeps the overworld view; the black GAME OVER screen replaces the picture
    //     with plain black and a large text overlay, but only once the text is on screen (switching while it is still
    //     black would flash the status bar).
    setState(0x11, 0x23); nes->ram[0x13] = 7; frames(ws, 2, aspect);             // Link spinning
    CHECK_EQ(c.w, 512); CHECK(!ws.overlay.active());
    setState(0x11, 0x23); nes->ram[0x13] = 0xA; frames(ws, 2, aspect);           // the spark
    CHECK_EQ(c.w, 512);
    for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF000000;
    setState(0x11, 0x23); nes->ram[0x13] = 0xB; frames(ws, 2, aspect);           // text requested, still black
    CHECK_EQ(c.w, 512); CHECK(!ws.overlay.active());
    for (int y = 120; y < 128; y++) for (int x = 96; x < 160; x++) nes->ppu.framebuffer[y * 256 + x] = 0xFFFFFFFF;   // text
    setState(0x11, 0x23); nes->ram[0x13] = 0xC; frames(ws, 2, aspect);
    CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    CHECK_EQ(px(10, 10), 0xFF000000); CHECK_EQ(px(100, 124), 0xFF000000);        // no status bar, text isn't in the canvas
    CHECK_EQ(ws.overlay.images.size(), (size_t)1);
    CHECK_EQ(ws.overlay.dim, 0.0f);
    CHECK_EQ(ws.overlay.images[0].w, 64); CHECK_EQ(ws.overlay.images[0].h, 8);   // just the text, cropped
    CHECK_EQ(ws.overlay.images[0].pixels[0], 0xFFFFFFFF);
    for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF123456;
    setState(8, 0x23); frames(ws, 2, aspect);                                    // continue question: plain, big
    CHECK_EQ(c.w, 256); CHECK(!ws.overlay.active());
    setState(5, 0x23); frames(ws, 2, aspect);
    CHECK_EQ(c.w, 512);
}

void section14(Ctx& t) {
    SECTION_BINDINGS
    // 14. Caves in a box. The overworld stays on screen exactly as it was when Link went in (no fade to black, no black
    //     screen); once the cave's picture is there, a dimmed backdrop and the framed box appear together, with the
    //     status bar on top. On the way out the box goes with the cave and the overworld is simply back.
    {
        Widescreen b; b.init(*nes, err); b.unfogAnim = false; b.cloudShadow = false; b.lighting = false; b.viewHeight = 176;
        CHECK(b.caveBox);
        nes->ppu.palette[5] = 0x2A; nes->ppu.palette[7] = 0x21; nes->ppu.palette[11] = 0x16;   // (earlier tests left it black)
        for (int i = 0; i < Ppu::W * 64; i++) nes->ppu.framebuffer[i] = 0xFF000000;
        nes->ppu.framebuffer[45 * 256 + 180] = 0xFFFFFFFF;                       // something in the status bar
        auto playRows = [&](uint32_t color) { for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = color; };
        auto ntShows = [&](bool overworld) {
            for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
                nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, overworld ? decoded[col * 22 + row] : 0xF4);
        };
        playRows(0xFF123456);
        setState(5, 0x23); ntShows(true); frames(b, 3, aspect);
        CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 176);
        CHECK_EQ(b.overlay.images.size(), (size_t)4);                           // just the status bar
        const std::vector<uint32_t> steady = c.pixels;                          // the overworld just before Link goes in
        // Link goes in: while the PPU still holds the overworld screen, the picture stays as it was.
        setState(0xB, 0x23); nes->ram[0x11] = 0; frames(b, 2, aspect);
        CHECK(c.pixels == steady); CHECK_EQ(b.overlay.images.size(), (size_t)4); CHECK_EQ(b.overlay.dim, 0.0f);
        // The game blanks the screen for a few frames: no black screen is shown, the overworld stays.
        playRows(0xFF000000); nes->ram[0x13] = 3; frames(b, 3, aspect);
        CHECK(c.pixels == steady); CHECK_EQ(b.overlay.dim, 0.0f); CHECK_EQ(b.overlay.images.size(), (size_t)4);
        // The cave's picture appears: dimmed backdrop (the overworld as it was) + box at the same moment.
        playRows(0xFF123456); ntShows(false); nes->ram[0x13] = 5; frames(b, 2, aspect);
        CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 176);
        CHECK(c.pixels == steady);
        CHECK(b.overlay.dim > 0.0f);
        CHECK_EQ(b.overlay.images.size(), (size_t)5);                           // the box, then the four status bar pieces
        { const OverlayImage& box = b.overlay.images[0];
          CHECK_EQ(box.w, 256 + 8); CHECK_EQ(box.h, 176 + 8);
          CHECK_EQ(box.pixels[0], 0xFFFCFCFC);                                  // frame
          CHECK_EQ(box.pixels[(4 + 5) * box.w + 4 + 10], 0xFF123456);           // the game's play area (rows 64..239)
          CHECK(box.maxWidthFrac < 1.0f && box.maxHeightFrac < 1.0f);
          CHECK_EQ(b.overlay.images[1].heightPerScale, 288);                    // the status bar is drawn after (on top of) the dim
        }
        // Standing in the cave (steady): the same.
        setState(0xB, 0x23); frames(b, 2, aspect);
        CHECK(c.pixels == steady); CHECK_EQ(b.overlay.images.size(), (size_t)5);
        // The inventory inside the cave: its box on top of the cave's box (dimmed) on top of the overworld; no status bar.
        nes->ram[0xE1] = 8; frames(b, 2, aspect);
        CHECK_EQ(b.overlay.images.size(), (size_t)2); CHECK(c.pixels == steady);
        CHECK_EQ(b.overlay.images[0].w, 256 + 8); CHECK_EQ(b.overlay.images[0].h, 176 + 8);       // the cave's box, still there
        CHECK(b.overlay.images[0].pixels[(4 + 5) * b.overlay.images[0].w + 4 + 10] != 0xFF123456); // (dimmed)
        CHECK_EQ(b.overlay.images[1].h, (238 - 37) + 8);                                          // the inventory on top (first lit row 45, minus 8 rows of margin)
        nes->ram[0xE1] = 0; frames(b, 2, aspect);
        CHECK_EQ(b.overlay.images.size(), (size_t)5);
        // Leaving: the cave fades out (the box stays while its picture is there)...
        setState(0xA, 0x23); nes->ram[0x11] = 0; frames(b, 2, aspect);
        CHECK_EQ(b.overlay.images.size(), (size_t)5);
        // ...and when the cave has faded away, box and cave are gone together and the overworld is simply there. No black.
        playRows(0xFF000000); frames(b, 2, aspect);
        CHECK(c.pixels == steady); CHECK_EQ(b.overlay.dim, 0.0f); CHECK_EQ(b.overlay.images.size(), (size_t)4);
        // The game redraws the overworld screen; as soon as it is visible the normal world view takes over.
        ntShows(true); playRows(0xFF123456); frames(b, 2, aspect);
        CHECK_EQ(c.w, 512); CHECK_EQ(b.overlay.images.size(), (size_t)4); CHECK_EQ(b.overlay.dim, 0.0f);
        setState(4, 0x23); frames(b, 2, aspect);                                // walking out
        CHECK_EQ(c.w, 512); CHECK_EQ(b.overlay.images.size(), (size_t)4);
        // A mode-$A frame that has nothing to do with a cave (no cave session) is not boxed.
        ntShows(false);
        setState(0xA, 0x23); nes->ram[0x11] = 0; frames(b, 2, aspect);
        CHECK_EQ(b.overlay.dim, 0.0f);

        // With the option off, caves replace the picture as before (big picture once Link is in the cave).
        b.caveBox = false;
        setState(5, 0x23); ntShows(true); frames(b, 3, aspect);
        setState(0xB, 0x23); frames(b, 2, aspect);
        CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
    }
}

void section15(Ctx& t) {
    SECTION_BINDINGS
    // 15. The overworld map stays while Link walks down the stairs into a dungeon (the game already says "dungeon", but the
    //     overworld is still on screen); in a dungeon the game's own map is used.
    {
        Widescreen s; s.init(*nes, err); s.unfogAnim = false; s.cloudShadow = false; s.lighting = false; s.viewHeight = 176; s.caveBox = true;
        setState(5, 0x23); frames(s, 3, aspect);
        for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
            nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, decoded[col * 22 + row]);
        setState(0x10, 0x23, 3); frames(s, 2, aspect);
        CHECK_EQ(s.overlay.images[2].w, 132);                                   // still our overworld map...
        { const OverlayImage& m = s.overlay.images[2];
          const int cx = 2 + 3 * 8 - 1, cy = 2 + 2 * 6 - 1;
          CHECK_EQ(m.pixels[(size_t)cy * m.w + cx], 0xFFFCFCFC);               // ...with the frame on the current screen
        }
        for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
            nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, 0xF4);
        frames(s, 2, aspect);
        CHECK_EQ(s.overlay.images[2].w, 66);                                    // the dungeon's own map
    }
}

void section17(Ctx& t) {
    SECTION_BINDINGS
    // 17. The inventory box drops the unused black rows above the title (keeping 20 for the SAVE AND QUIT hint); the
    //     backdrop behind a cave's box doesn't contain Link (he went in).
    {
        Widescreen m; m.init(*nes, err); m.unfogAnim = false; m.cloudShadow = false; m.lighting = false; m.viewHeight = 176;
        for (int i = 0; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF000000;
        for (int y = 30; y < 40; y++) for (int x = 70; x < 200; x++) nes->ppu.framebuffer[y * 256 + x] = 0xFFB53120;   // the title
        for (int y = 200; y < 232; y++) for (int x = 20; x < 100; x++) nes->ppu.framebuffer[y * 256 + x] = 0xFF666666; // the map at the bottom
        setState(5, 0x23); frames(m, 3, aspect);
        nes->ram[0xE1] = 8; frames(m, 2, aspect);
        const OverlayImage& box = m.overlay.images.back();
        CHECK_EQ(box.h, (238 - 10) + 8);                                          // first lit row 30 -> keep from row 10
        CHECK_EQ(box.pixels[(4 + 20) * box.w + 4 + 100], 0xFFB53120);             // the title is 20 rows below the top of the picture
        CHECK_EQ(box.pixels[(4 + 19) * box.w + 4 + 100], 0xFF000000);
        nes->ram[0xE1] = 0;

        Widescreen b; b.init(*nes, err); b.unfogAnim = false; b.cloudShadow = false; b.lighting = false; b.viewHeight = 176;
        nes->ppu.palette[5] = 0x2A; nes->ppu.palette[7] = 0x21; nes->ppu.palette[11] = 0x16;
        for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF123456;
        for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
            nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, decoded[col * 22 + row]);
        for (int i = 0; i < 64; i++) nes->ppu.oam[i * 4] = 0xFF;
        // Link: sprite 18 at (124, 141), white pixels in the picture's colour layer, tagged in the sprite layer
        g_link.on = true; g_link.x = 124; g_link.y = 141;
        nes->ppu.oam[72] = 140; nes->ppu.oam[73] = 3; nes->ppu.oam[74] = 0; nes->ppu.oam[75] = 124;
        for (int y = 141; y < 145; y++) for (int x = 124; x < 128; x++) { nes->ppu.sprCol[y * Ppu::SPR_W + x] = 0x30; nes->ppu.sprOam[y * Ppu::SPR_W + x] = 18; }
        setState(5, 0x23); nes->ram[0x70] = 120; nes->ram[0x84] = 141; frames(b, 3, aspect);
        // Link's pixels in the picture: canvas (128 + 124, 3 + (141 - 64)) = (252, 80)
        CHECK_EQ(px(252, 80), 0xFFFFFEFF);                                        // he is there on the overworld
        // He walks into the cave and the cave appears: the backdrop is the overworld without him.
        setState(0xB, 0x23); nes->ram[0x11] = 0;
        for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++) nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, 0xF4);
        g_link.on = false;
        frames(b, 3, aspect);
        CHECK(b.overlay.dim > 0.0f);
        CHECK_EQ(px(252, 80), 0xFF123456);                                        // the background where he stood
        CHECK_EQ(px(255, 83), 0xFF123456);
        g_link.on = false;
    }
}

void section18(Ctx& t) {
    SECTION_BINDINGS
    // 18. The backdrop behind a cave's box holds no sprites at all (no Link, enemies, projectiles, effects, overhanging
    //     parts, flash circle): it is exactly the picture of a calm overworld frame with nothing moving in it.
    {
        nes->ppu.palette[5] = 0x2A; nes->ppu.palette[7] = 0x21; nes->ppu.palette[11] = 0x16;
        nes->ppu.lineMask[100] = 0x1E;
        for (int i = 0; i < Ppu::W * 64; i++) nes->ppu.framebuffer[i] = 0xFF654321;
        for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF123456;
        for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
            nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, decoded[col * 22 + row]);
        for (int i = 0; i < 13; i++) nes->ram[0xAC + i] = 0;
        g_link.on = false; g_extra.clear();
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        auto goInCave = [&](Widescreen& w) {
            setState(0xB, 0x23); nes->ram[0x11] = 0;
            for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++) nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, 0xF4);
            frames(w, 3, aspect);
        };
        auto overworldAgain = [&]() {
            for (int col = 0; col < 32; col++) for (int row = 0; row < 22; row++)
                nes->ppu.vramWrite(0x2000 + (8 + row) * 32 + col, decoded[col * 22 + row]);
        };

        // The reference: a calm overworld frame, then the cave.
        Widescreen cl; cl.init(*nes, err); cl.unfogAnim = false; cl.cloudShadow = false; cl.lighting = false; cl.viewHeight = 176;
        setState(5, 0x23); frames(cl, 3, aspect);
        const std::vector<uint32_t> calm18 = c.pixels;
        goInCave(cl);
        CHECK(cl.overlay.dim > 0.0f); CHECK(c.pixels == calm18);                 // (the backdrop is that frame)
        overworldAgain();

        // The same, with things moving: an enemy and an orange projectile in the game's picture, Link's sprite, a sprite part
        // past the right edge and one over the top (those two are completed in the surroundings on normal frames).
        // Room origin on the canvas: (128, 3).
        g_extra.push_back({60, 100, 0xFFAA5500}); g_extra.push_back({61, 100, 0xFFAA5500});   // enemy
        g_extra.push_back({200, 90, 0xFFE45C10}); g_extra.push_back({201, 90, 0xFFE45C10});   // projectile
        g_link.on = true; g_link.x = 124; g_link.y = 141;                                   // Link
        nes->ppu.sprCol[105 * Ppu::SPR_W + 258] = 0x2A; nes->ppu.sprOam[105 * Ppu::SPR_W + 258] = 20;
        nes->ppu.sprCol[61 * Ppu::SPR_W + 100] = 0x16;  nes->ppu.sprOam[61 * Ppu::SPR_W + 100] = 20;
        Widescreen b; b.init(*nes, err); b.unfogAnim = false; b.cloudShadow = false; b.lighting = false; b.viewHeight = 176;
        setState(5, 0x23); frames(b, 3, aspect);
        CHECK_EQ(px(128 + 60, 3 + 36), 0xFFAA5500);                              // on a normal frame they are all there...
        CHECK_EQ(px(128 + 200, 3 + 26), 0xFFE45C10);
        CHECK_EQ(px(128 + 124, 3 + 77), 0xFFFFFEFF);
        CHECK_EQ(px(128 + 258, 3 + 41), Ppu::nesColor(0x2A));
        CHECK_EQ(px(128 + 100, 3 - 3), Ppu::nesColor(0x16));
        CHECK(c.pixels != calm18);
        // ...and Link goes into the cave: the enemies are still in the game's picture for the first frames (the PPU hasn't
        // changed yet), the backdrop must still be the calm picture, identical to the reference.
        goInCave(b);
        CHECK(b.overlay.dim > 0.0f);
        CHECK(c.pixels == calm18);
        CHECK_EQ(px(128 + 60, 3 + 36), 0xFF123456); CHECK_EQ(px(128 + 200, 3 + 26), 0xFF123456); CHECK_EQ(px(128 + 124, 3 + 77), 0xFF123456);
        CHECK(px(128 + 258, 3 + 41) != Ppu::nesColor(0x2A)); CHECK(px(128 + 100, 0) != Ppu::nesColor(0x16));

        // A bomb flash just before going in doesn't leave its gray circle in the backdrop either.
        overworldAgain();
        Widescreen f; f.init(*nes, err); f.unfogAnim = false; f.cloudShadow = false; f.lighting = false; f.viewHeight = 176;
        setState(5, 0x23); frames(f, 3, aspect);
        nes->ram[0xAC + 3] = 0x13; nes->ram[0x70 + 3] = 200; nes->ram[0x84 + 3] = 141;
        nes->ppu.lineMask[100] = 0x1F; frames(f, 2, aspect);
        CHECK(c.pixels != calm18);                                               // (the flash is there)
        nes->ram[0xAC + 3] = 0; nes->ppu.lineMask[100] = 0x1E;
        goInCave(f);
        CHECK(c.pixels == calm18);

        // With the original status bar on top (HUD overlay off) the bar is part of the backdrop, sprites still aren't.
        overworldAgain();
        Widescreen o; o.init(*nes, err); o.unfogAnim = false; o.cloudShadow = false; o.lighting = false; o.viewHeight = 176; o.hudOverlay = false;
        setState(5, 0x23); frames(o, 3, aspect);
        CHECK_EQ(c.h, 240);
        CHECK_EQ(px(128 + 10, 5), 0xFF654321);
        goInCave(o);
        CHECK_EQ(px(128 + 10, 5), 0xFF654321);                                   // the bar is there
        CHECK_EQ(px(128 + 60, 64 + 100 - 64 + 0), 0xFF123456);                   // the enemy isn't
        CHECK_EQ(px(128 + 200, 64 + 90 - 64), 0xFF123456);                       // the projectile isn't

        g_link.on = false; g_extra.clear();
        std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
    }
}
