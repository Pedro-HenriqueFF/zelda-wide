// Widescreen integration tests: the HUD overlay (status bar pieces) and the minimap.
// Sections run in numeric order (see test_wide.cpp) on one shared Ctx: later sections rely on state (fog discovered,
// palettes, RAM) left by earlier ones, so do not reorder them.
#include <memory>
#include "test_wide_common.h"

void section12(Ctx& t) {
    SECTION_BINDINGS
    // 12. The status bar as a transparent overlay (default): the world fills the whole canvas, the four pieces of the
    //     bar are separate overlay pictures placed in the corners.
    {
        auto h_h = std::make_unique<Widescreen>(); Widescreen& h = *h_h; h.init(*nes, err); h.unfogAnim = false; h.cloudShadow = false; h.lighting = false; h.viewHeight = 176;
        CHECK(h.hudOverlay);                                  // on by default
        // a status bar with a single white pixel at (180, 45) [life piece], everything else black
        for (int i = 0; i < Ppu::W * 64; i++) nes->ppu.framebuffer[i] = 0xFF000000;
        nes->ppu.framebuffer[45 * 256 + 180] = 0xFFFFFFFF;
        setState(5, 0x23); nes->ram[0x70] = 120;
        frames(h, 2, aspect);
        CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 176);               // no status bar band: just the world view (176 rows)
        // the game's own screen sits at x=128, row 3 (camera centre = room origin + (128, 85), view 176 high)
        CHECK_EQ(px(128 + 10, 3 + 5), 0xFF123456);
        CHECK(isFog(px(5, 100)));
        CHECK_EQ(h.overlay.images.size(), (size_t)4);
        CHECK_EQ(h.overlay.dim, 0.0f);
        const OverlayImage& life = h.overlay.images[0];
        CHECK_EQ(life.w, 66); CHECK_EQ(life.h, 34);           // 64x32 piece + 1 pixel outline margin
        CHECK(life.anchorX < 0.1f && life.anchorY < 0.1f && life.pivotX == 0.0f);    // top left
        CHECK_EQ(life.heightPerScale, 288); CHECK(life.sharpScale);   // 25% bigger than 3x: 3.75x at 1080p
        CHECK_EQ(life.pixels[(45 - 24 + 1) * life.w + (180 - 176 + 1)], 0xFFFFFFFF);  // the pixel itself
        CHECK_EQ(life.pixels[(45 - 24 + 1) * life.w + (179 - 176 + 1)], 0xC0000000);  // dark outline next to it
        CHECK_EQ(life.pixels[(25 - 24 + 1) * life.w + (230 - 176 + 1)], 0x00000000);  // far away: see-through
        const OverlayImage& items = h.overlay.images[1];
        CHECK_EQ(items.w, 50); CHECK_EQ(items.h, 34);
        CHECK(items.anchorX > 0.9f && items.anchorY < 0.1f && items.pivotX == 1.0f);  // top right
        CHECK_EQ(items.pixels[5 * items.w + 5], 0xB0000000);                          // dark panel behind the item boxes
        const OverlayImage& map = h.overlay.images[2];
        CHECK_EQ(map.heightPerScale, 360); CHECK(!map.sharpScale);                      // the map keeps its size (3x)
        CHECK_EQ(map.w, 132); CHECK_EQ(map.h, 52);             // the overworld map is drawn by us (16x8 cells of 8x6, + margins)
        CHECK(map.anchorX < 0.1f && map.anchorY > 0.9f && map.pivotY == 1.0f);        // bottom left
        const OverlayImage& counters = h.overlay.images[3];
        CHECK_EQ(counters.w, 34); CHECK_EQ(counters.h, 34);
        CHECK(counters.anchorX > 0.9f && counters.anchorY > 0.9f);                    // bottom right

        // Same canvas height whatever the zoom: only the world view changes, never the bar.
        h.viewHeight = 448; frames(h, 2, 16.0 / 9.0);
        CHECK_EQ(c.h, 448);
        CHECK_EQ(h.overlay.images.size(), (size_t)4);
        CHECK_EQ(h.overlay.images[0].heightPerScale, 288);

        // Caves and dungeons: the room is centred in the 256x240 picture (rows 32-207) and the bar is an overlay.
        setState(5, 0x23, 3); frames(h, 2, aspect);                       // steady in a dungeon
        CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);
        CHECK_EQ(px(10, 10), 0xFF000000);                                 // nothing where the bar used to be
        CHECK_EQ(px(10, 32 + 5), 0xFF123456); CHECK_EQ(px(10, 207), 0xFF123456); CHECK_EQ(px(10, 208), 0xFF000000);
        CHECK_EQ(h.overlay.images.size(), (size_t)4);
        // The same inside a Boxed picture (a transition at the overworld size)
        setState(5, 0x23); frames(h, 2, aspect);
        setState(4, 0x23, 3); frames(h, 2, aspect);                       // entering the dungeon room... (big)
        // No bar on the menu, the GAME OVER screen, the title screen:
        setState(5, 0x23); frames(h, 2, aspect);
        nes->ram[0xE1] = 8; frames(h, 2, aspect);
        CHECK_EQ(h.overlay.images.size(), (size_t)1);                     // only the inventory box
        nes->ram[0xE1] = 0; frames(h, 2, aspect);
        setState(0, 0x23); frames(h, 2, aspect);
        CHECK_EQ(h.overlay.images.size(), (size_t)0);
        CHECK_EQ(c.w, 256); CHECK_EQ(c.h, 240);                           // title: the plain picture
        // While the game blanks the picture for a moment, the bar stays
        setState(5, 0x23); frames(h, 3, aspect);
        nes->ppu.lineMask[100] = 0;
        frames(h, 1, aspect);
        CHECK_EQ(h.overlay.images.size(), (size_t)4);
        nes->ppu.lineMask[100] = 0x1E;
        // The original bar can be switched back on
        h.hudOverlay = false; frames(h, 2, aspect);
        CHECK_EQ(h.overlay.images.size(), (size_t)0);
        CHECK_EQ(c.h, 448 + 64);                                           // (the view height set above, plus the bar band)
    }
}

void section13(Ctx& t) {
    SECTION_BINDINGS
    // 13. Details of the overlay HUD: the original-style map has no panel above the map itself, the minimap styles, the
    //     bar being hidden while a game starts, staying during the death sequence, and the fog fading with the picture.
    {
        auto h_h = std::make_unique<Widescreen>(); Widescreen& h = *h_h; h.init(*nes, err); h.unfogAnim = false; h.cloudShadow = false; h.lighting = false; h.viewHeight = 176;
        for (int i = 0; i < Ppu::W * 64; i++) nes->ppu.framebuffer[i] = 0xFF000000;      // an all-black status bar
        setState(5, 0x22); nes->ram[0x70] = 120; frames(h, 2, aspect);              // see screen $22 first
        setState(5, 0x23); nes->ram[0x70] = 120;

        // Style 0 (the game's own map): the dark panel covers only the map rows (24-55), not the 8 rows above it.
        h.minimapStyle = 0; frames(h, 2, aspect);
        { const OverlayImage& m = h.overlay.images[2];
          CHECK_EQ(m.w, 66); CHECK_EQ(m.h, 42);
          CHECK_EQ(m.pixels[(3) * m.w + 10], 0x00000000);          // fb row 18: nothing
          CHECK_EQ(m.pixels[(8 + 1) * m.w + 10], 0xB0000000);      // fb row 24: panel starts here
        }
        // Style 1: grey cells, blue for the screens seen, green for the current one.
        h.minimapStyle = 1; frames(h, 2, aspect);
        { const OverlayImage& m = h.overlay.images[2];
          CHECK_EQ(m.w, 16 * 4 + 4); CHECK_EQ(m.h, 8 * 4 + 4);
          auto cell = [&](int room) { return m.pixels[(size_t)(2 + (room >> 4) * 4) * m.w + 2 + (room & 15) * 4]; };
          CHECK_EQ(cell(0x23), Ppu::nesColor(0x2A));               // current: green
          CHECK_EQ(cell(0x22), Ppu::nesColor(0x12));               // seen before: blue
          CHECK_EQ(cell(0x77), Ppu::nesColor(0x00));               // never seen: grey
        }
        // Style 2: a picture of each screen seen, fog for the rest, a white frame around the current screen.
        h.minimapStyle = 2; frames(h, 4, aspect);
        { const OverlayImage& m = h.overlay.images[2];
          CHECK_EQ(m.w, 132); CHECK_EQ(m.h, 52);
          auto at = [&](int x, int y) { return m.pixels[(size_t)y * m.w + x]; };
          const int cx = 2 + 3 * 8, cy = 2 + 2 * 6;                // cell of room $23
          CHECK_EQ(at(cx - 1, cy - 1), 0xFFFCFCFC); CHECK_EQ(at(cx + 8, cy + 6), 0xFFFCFCFC);   // frame corners
          CHECK_EQ(at(cx - 1, cy + 2), 0xFFFCFCFC); CHECK_EQ(at(cx + 3, cy - 1), 0xFFFCFCFC);
          CHECK(at(cx + 3, cy + 2) != 0xFFFCFCFC);                 // inside the frame: the screen's own picture
          CHECK(at(cx + 3, cy + 2) != 0xFF10131F);                 // (not fog)
          const int fx = 2 + 7 * 8, fy = 2 + 7 * 6;                // room $77: never seen -> fog
          CHECK_EQ(at(fx + 3, fy + 2), 0xFF10131F);
          const int dx = 2 + 2 * 8, dy = 2 + 2 * 6;                // room $22: seen -> same layout/palette as the current one
          CHECK_EQ(at(dx + 3, dy + 2), at(cx + 3, cy + 2));
        }
        // Dungeons keep the game's own map.
        setState(5, 0x23, 3); frames(h, 2, aspect);
        CHECK_EQ(h.overlay.images[2].w, 66);
        setState(5, 0x23); h.minimapStyle = 2; frames(h, 2, aspect);

        // Starting a game: no bar at all while the game loads and the curtain opens (it builds the bar piece by piece).
        setState(2, 0x77, 0); frames(h, 2, aspect);
        CHECK_EQ(h.overlay.images.size(), (size_t)0);
        CHECK_EQ(c.h, 176);                                       // and no band reserved for the original bar either
        setState(3, 0x77, 0); frames(h, 2, aspect);
        CHECK_EQ(h.overlay.images.size(), (size_t)0);
        setState(4, 0x77, 0); frames(h, 2, aspect);
        CHECK_EQ(h.overlay.images.size(), (size_t)4);             // the bar is there once play starts
        // Leaving a dungeon (exit type 2): the bar was already there, so it stays during the curtain.
        setState(3, 0x45, 0); nes->ram[0x5A] = 2; frames(h, 2, aspect);
        CHECK_EQ(h.overlay.images.size(), (size_t)4);

        // The death sequence keeps the overlay bar (never the original one) until the GAME OVER text appears.
        setState(5, 0x23); frames(h, 2, aspect);
        for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF000000;
        setState(0x11, 0x23); nes->ram[0x13] = 0xB; frames(h, 2, aspect);
        CHECK_EQ(h.overlay.images.size(), (size_t)4);
        CHECK_EQ(c.h, 176);                                       // still the world view, no original bar band
        nes->ppu.framebuffer[120 * 256 + 100] = 0xFFFFFFFF;
        setState(0x11, 0x23); nes->ram[0x13] = 0xC; frames(h, 2, aspect);
        CHECK_EQ(h.overlay.images.size(), (size_t)1);             // only the text
        for (int i = Ppu::W * 64; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF123456;

        // The fog fades with the picture: with an all-black palette during a transition it is black too.
        setState(5, 0x23); frames(h, 3, aspect);
        CHECK(isFog(px(c.w - 5, 100)) && px(c.w - 5, 100) != 0xFF000000);   // screen $24 was never seen: fog
        for (int i = 0; i < 32; i++) nes->ppu.palette[i] = 0x0F;
        setState(0x10, 0x23); frames(h, 2, aspect);               // stairs animation, still the overworld view
        CHECK_EQ(px(c.w - 5, 100), 0xFF000000);
    }
}
