// Section 30: stairs on the ground: Link comes out on the square he went in by, and cannot go straight back in until he has left it.
#include "test_wide_common.h"
#include "widescreen_internal.h"
#include "zelda_ram.h"

using namespace zelda;
using namespace wd;

void section30(Ctx& t) {
    SECTION_BINDINGS
    const int room = 0x23;
    auto fresh = [&](Widescreen& m) { m.init(*nes, err); m.unfogAnim = false; m.cloudShadow = false; m.lighting = false; m.hudOverlay = false; m.caveBox = false; };
    auto attrA = [&] { return (int)nes->cart.cpuRead(kAttrsA + room); };
    auto attrF = [&] { return (int)nes->cart.cpuRead(kAttrsF + room); };
    auto step = [&](Widescreen& m) { m.prepareFrame(*nes); m.afterFrame(*nes); };        // (a frame of the game is not run: RAM is set by hand)
    auto go = [&](Widescreen& m, int mode, int x, int y) { nes->ram[kGameMode] = (uint8_t)mode; nes->ram[kObjX] = (uint8_t)x; nes->ram[kObjY] = (uint8_t)y; step(m); };
    const uint8_t keepA = nes->cart.cpuRead(kAttrsA + room), keepF = nes->cart.cpuRead(kAttrsF + room);
    nes->cart.cpuWrite(kAttrsA + room, 0x21); nes->cart.cpuWrite(kAttrsF + room, 0x01);   // the game's exit spot: x $20, y $5D (row 1)
    setState(5, room); nes->ram[kUndergroundExitType] = 0;

    {   // walking onto stairs at (x $70, y $9D) -> mode $10; out again: the attributes lent for the game's layout, then given back
        Widescreen m; fresh(m);
        go(m, kModePlay, 0x70, 0x9D);
        CHECK(!m.stairValid);
        go(m, kModeStairs, 0x70, 0x9D);
        CHECK(m.stairValid && m.stairX == 0x70 && m.stairY == 0x9D && m.stairRoom == room);
        nes->ram[kCurLevel] = 0; nes->ram[kUndergroundEntranceTile] = 0x70; nes->ram[kUndergroundExitType] = 1;
        int sx = 0, sy = 0;
        CHECK(m.stairsExitSpot(*nes, room, sx, sy) && sx == 0x70 && sy == 0x9D);
        CHECK(!m.stairsExitSpot(*nes, room + 1, sx, sy));                                   // another screen: the game decides
        nes->ram[kGameMode] = kModeEnter; nes->ram[kObjX] = 0x20; nes->ram[kObjY] = 0x5D;
        m.prepareFrame(*nes);
        CHECK_EQ(attrA() & 0xF0, 0x70); CHECK_EQ(attrF() & 7, 5); CHECK_EQ(attrA() & 3, 1);   // (palette bits untouched)
        m.afterFrame(*nes);
        CHECK_EQ(attrA(), 0x21); CHECK_EQ(attrF(), 0x01);                                    // given back
        // play: the guard against going back in is kept while Link is on the square (the game would clear it after 8 px)
        nes->ram[kUndergroundExitType] = 0; go(m, kModePlay, 0x70, 0x9D);
        CHECK_EQ((int)nes->ram[kUndergroundExitType], 1);
        nes->ram[kUndergroundExitType] = 0; go(m, kModePlay, 0x78, 0x9D); CHECK_EQ((int)nes->ram[kUndergroundExitType], 1);
        nes->ram[kUndergroundExitType] = 0; go(m, kModePlay, 0x70, 0xA5); CHECK_EQ((int)nes->ram[kUndergroundExitType], 1);   // (8 px down: still partly on it)
        nes->ram[kUndergroundExitType] = 0; go(m, kModePlay, 0x70, 0x9D); CHECK_EQ((int)nes->ram[kUndergroundExitType], 1);   // (and back on it)
        nes->ram[kUndergroundExitType] = 1; go(m, kModePlay, 0x80, 0x9D);                      // 16 px away: left completely
        CHECK_EQ((int)nes->ram[kUndergroundExitType], 0);
        nes->ram[kUndergroundExitType] = 0; go(m, kModePlay, 0x70, 0x9D);
        CHECK_EQ((int)nes->ram[kUndergroundExitType], 0);                                    // the guard is over: stepping on the stairs works again
    }
    {   // a cave entrance ($24) keeps the game's walk-out; so do the cellar, an unaligned spot and the file select
        Widescreen m; fresh(m);
        go(m, kModePlay, 0x70, 0x9D); go(m, kModeStairs, 0x70, 0x9D);
        nes->ram[kUndergroundEntranceTile] = 0x24; nes->ram[kUndergroundExitType] = 1;
        int sx, sy;
        CHECK(!m.stairsExitSpot(*nes, room, sx, sy));
        nes->ram[kGameMode] = kModeEnter; m.prepareFrame(*nes); CHECK_EQ(attrA(), 0x21); m.afterFrame(*nes);
        nes->ram[kUndergroundEntranceTile] = 0x71; nes->ram[kCurLevel] = 3;
        CHECK(!m.stairsExitSpot(*nes, room, sx, sy)); nes->ram[kCurLevel] = 0;
        CHECK(m.stairsExitSpot(*nes, room, sx, sy));
        nes->ram[kGameMode] = kModeFileSelect; step(m); CHECK(!m.stairValid && !m.stairsExitSpot(*nes, room, sx, sy));
        Widescreen u; fresh(u);
        go(u, kModePlay, 0x74, 0x9D); go(u, kModeStairs, 0x74, 0x9D); CHECK(!u.stairValid);        // not a square position
        Widescreen v; fresh(v);
        nes->ram[kCurLevel] = 2; go(v, kModePlay, 0x70, 0x9D); go(v, kModeStairs, 0x70, 0x9D); CHECK(!v.stairValid);   // (inside a dungeon: the game's own cellar stairs)
        nes->ram[kCurLevel] = 0;
    }
    {   // the camera while he comes out stands where he will be
        Widescreen m; fresh(m);
        go(m, kModePlay, 0x70, 0x9D); go(m, kModeStairs, 0x70, 0x9D);
        nes->ram[kUndergroundEntranceTile] = 0x70; nes->ram[kUndergroundExitType] = 1;
        setState(kModeEnter, room); nes->ram[kUndergroundExitType] = 1; nes->ram[kObjX] = 0x20; nes->ram[kObjY] = 0x5D;
        frames(m, 3, aspect);
        const int ox = (room & 15) * ROOM_W, oy = (room >> 4) * ROOM_H;
        CHECK_EQ(m.cameraX(), ox + 0x70 + 8); CHECK_EQ(m.cameraY(), oy + 0x9D + 8 - HUD_H);
    }
    nes->cart.cpuWrite(kAttrsA + room, keepA); nes->cart.cpuWrite(kAttrsF + room, keepF);
    nes->ram[kUndergroundExitType] = 0; nes->ram[kUndergroundEntranceTile] = 0;
}
