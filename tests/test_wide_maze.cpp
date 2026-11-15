// Section 28: Lost Woods / Lost Hills: never unfogged in the overworld, on the map only once solved, single screen behind a haze, mist when entering/leaving.
#include "test_wide_common.h"
#include "widescreen_internal.h"
#include "zelda_ram.h"
#include <cmath>
#include <set>

using namespace zelda;
using namespace wd;

void section28(Ctx& t) {
    SECTION_BINDINGS
    auto fresh = [&](Widescreen& m) { m.init(*nes, err); m.unfogAnim = false; m.cloudShadow = false; m.lighting = false; m.setWater(false); m.hudOverlay = false; m.caveBox = false; m.viewHeight = 176; };
    const int slot = nes->ram[0x16] % 3;
    auto run = [&](Widescreen& m, int room, int n) { setState(5, room); frames(m, n, aspect); };
    auto settle = [&](Widescreen& m) { for (int i = 0; i < 420; i++) frames(m, 1, aspect); };      // (until the mist is gone: closing, 2 s covered, lifting)

    CHECK(isMazeRoom(0x61) && isMazeRoom(0x1B) && !isMazeRoom(0x60) && !isMazeRoom(0x0B));

    // The Lost Woods: in from the east, the way out is the west.
    {
        Widescreen m; fresh(m);
        run(m, 0x62, 4);
        CHECK(!m.mazeShown && !m.mazeFading() && m.discovered(slot, 0x62));
        run(m, 0x61, 2);
        CHECK(m.mazeFading());                                       // the mist closes over the frozen picture, the game waits
        int closing = 0; size_t coveredFrames = 0;
        while (m.mazeFading() && closing < 600) {
            frames(m, 1, aspect); closing++;
            if (closing == Widescreen::kMazeRamp + 40) { size_t solid = 0; for (uint32_t p : m.overlay.mist) solid += (p >> 24) >= 250; coveredFrames = m.overlay.mist.empty() ? 0 : solid * 100 / m.overlay.mist.size(); }
        }
        CHECK(coveredFrames >= 95);                                  // (the layer over the whole window, the HUD included, is solid during the 2 s)
        CHECK(closing >= Widescreen::kMazeRamp + Widescreen::kMazeHold + Widescreen::kMazeMistOut - 3);   // (closing, 2 s covered, lifting: the game waits for all of it)
        CHECK(m.mazeShown);
        settle(m);
        CHECK(!m.mazeFading() && m.mazeShown);
        CHECK(c.w == 512 && c.h == FRAME_H);                         // the game's own size (the whole height), the bars beside it part of the picture
        const std::vector<uint32_t> hazeA = c.pixels; frames(m, 40, aspect);
        size_t moved = 0; for (size_t i = 0; i < hazeA.size() && i < c.pixels.size(); i++) moved += hazeA[i] != c.pixels[i];
        CHECK(moved > 2000);                                         // the haze waves and drifts
        CHECK(!m.discovered(slot, 0x61) && !m.mazeSeen(slot, 0x61)); // never explored, not on the map
        run(m, 0x62, 2);                                             // out through the east: allowed at any time, does not solve the maze
        settle(m);
        CHECK(!m.mazeShown && !m.mazeSeen(slot, 0x61));
        run(m, 0x61, 2); settle(m);
        CHECK(m.mazeShown);
        run(m, 0x60, 2);                                             // the way out (west): solved
        CHECK(m.mazeFading());
        settle(m);
        CHECK(!m.mazeShown && m.mazeSeen(slot, 0x61) && !m.discovered(slot, 0x61) && m.discovered(slot, 0x60));
        CHECK(m.mazeSeen(slot, 0x61));
    }
    // The Lost Hills: in from the south, out to the north once solved.
    {
        Widescreen m; fresh(m);
        run(m, 0x2B, 4); run(m, 0x1B, 2); settle(m);
        CHECK(m.mazeShown && !m.mazeSeen(slot, 0x1B));
        run(m, 0x0B, 2); settle(m);
        CHECK(!m.mazeShown && m.mazeSeen(slot, 0x1B) && !m.discovered(slot, 0x1B));
    }
    // A repeat of the room is the game's own scroll (no mist, no skipping); only a scroll between two different screens is skipped.
    {
        Widescreen m; fresh(m);
        run(m, 0x61, 3); settle(m);
        nes->ram[kCurLevel] = 0; nes->ram[kRoomId] = 0x61;
        CHECK(m.keepScroll(*nes));
        nes->ram[kRoomId] = 0x60;
        CHECK(!m.keepScroll(*nes));
        run(m, 0x62, 3); settle(m);
        nes->ram[kRoomId] = 0x61;
        CHECK(!m.keepScroll(*nes));                                  // (entering: skipped, then the mist)
        m.enabled = false; nes->ram[kRoomId] = 0x61; run(m, 0x61, 3);
        CHECK(!m.keepScroll(*nes));
    }

    // No proximity: the mist starts when the screen changes (Link touched the border), not before; then the game waits until the mist is gone.
    {
        Widescreen m; fresh(m);
        run(m, 0x62, 4);
        nes->ram[kCurLevel] = 0; nes->ram[kGameMode] = 5; nes->ram[kObjDir] = 2; nes->ram[kObjY] = 0x8D; nes->ram[kObjX] = 0x04;   // at the border of the Woods, facing it
        frames(m, 5, aspect);
        CHECK(m.mistP == 0.0f && !m.mazeFading() && m.overlay.mistW == 0);
        run(m, 0x61, 2); frames(m, 4, aspect);
        CHECK(m.mazeFading() && m.overlay.mistW > 0);
        for (int i = 0; i < 400 && m.mazeFading(); i++) frames(m, 1, aspect);
        CHECK(!m.mazeFading() && m.overlay.mistW == 0 && m.mistP == 0.0f);
    }
    // Link's sight goes with his sprite while the maze scrolls into itself.
    {
        Widescreen m; fresh(m); m.lighting = true;
        run(m, 0x61, 3); settle(m);
        setState(7, 0x61);
        nes->ppu.oam[kLinkOam0 * 4 + 3] = 100; nes->ppu.oam[kLinkOam0 * 4] = 120;
        frames(m, 4, aspect);
        CHECK(std::fabs(m.lightLinkX - (1 * ROOM_W + 100 + 8)) < 1.0f && std::fabs(m.lightLinkY - (6 * ROOM_H + 120 + 9 - HUD_H)) < 1.0f);
        nes->ppu.oam[kLinkOam0 * 4 + 3] = 40; nes->ppu.oam[kLinkOam0 * 4] = 90; frames(m, 2, aspect);
        CHECK(std::fabs(m.lightLinkX - (1 * ROOM_W + 40 + 8)) < 1.0f);        // (no easing: it is where the sprite is)
    }
    // The mist: patches of different sizes that start at different times, clouds with a texture at the end.
    {
        Mist mi; mi.init();
        auto cov = [&](float p) { int n = 0, tot = 0; for (int y = 0; y < 30; y++) for (int x = 0; x < 50; x++) { tot++; n += mi.patchAlpha((x + 0.5f) / 50, (y + 0.5f) / 30, 50.0f / 30, p) > 0.3f; } return (float)n / tot; };
        const float c1 = cov(0.2f), c2 = cov(0.45f), c3 = cov(0.7f);
        CHECK(c1 < c2 && c2 < c3 && c1 > 0.0f && c2 < 0.95f);       // it grows patch by patch, not all at once
        CHECK(cov(1.0f) > 0.99f);
        std::vector<uint32_t> px;
        mi.layer(px, 200, 120, 1.0f, 5);
        std::set<uint32_t> distinct(px.begin(), px.end());
        { size_t solid = 0; for (uint32_t p : px) solid += (p >> 24) >= 250; CHECK(solid > px.size() * 95 / 100); }
        mi.layer(px, 200, 120, 0.0f, 5); { size_t any = 0; for (uint32_t p : px) any += (p >> 24) != 0; CHECK(any == 0); }
        mi.layer(px, 200, 120, 1.0f, 5);
        CHECK(distinct.size() > 40);                                 // a cloud texture, not one flat gray
        uint32_t mn = 255, mx = 0; for (uint32_t p : px) { mn = std::min(mn, p & 255); mx = std::max(mx, p & 255); }
        CHECK(mx - mn > 25 && mx > 100);                             // (and gray, not blue)
        CHECK(((px[0] >> 16 & 255) + 30 > (px[0] & 255)) && (int)(px[0] & 255) - (int)(px[0] >> 16 & 255) < 30);
    }
}
