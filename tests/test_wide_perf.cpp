// Section 31: work that is only done when it is needed (speed): the cave box's backdrop is kept only around a change of what is on screen.
#include "test_wide_common.h"
#include "widescreen_internal.h"
#include "zelda_ram.h"

using namespace zelda;

void section31(Ctx& t) {
    SECTION_BINDINGS
    const int room = 0x23;
    Widescreen m; m.init(*nes, err); m.unfogAnim = false; m.cloudShadow = false; m.lighting = false; m.hudOverlay = false; m.caveBox = true;
    for (int i = 0; i < OverworldMap::TILES; i++) nes->cart.cpuWrite(0x6530 + i, decoded[i]);        // (a normal overworld screen, whatever the sections before left)
    nes->cart.cpuWrite(0x67F + room, 0); nes->cart.cpuWrite(0x6AFE + room, 0);
    setState(5, room); nes->ram[kMenuState] = 0; nes->ram[kCurLevel] = 0; nes->ram[kUndergroundExitType] = 0;
    frames(m, 3, aspect);
    CHECK(m.backdropKept());                                                                           // (the first frames keep one)
    const int camX0 = m.backdropCameraX();
    for (int k = 0; k < 4; k++) { nes->ram[kObjX] = (uint8_t)(nes->ram[kObjX] + 16); frames(m, 1, aspect); }
    CHECK_EQ(m.backdropCameraX(), camX0);                                                                 // plain play: nothing is copied, the old backdrop stays
    nes->ram[kObjX] = (uint8_t)(nes->ram[kObjX] + 16); nes->ram[kGameMode] = kModeStairs;            // a frame ahead the game is going into a cave or down stairs: the picture before it is kept
    frames(m, 1, aspect);
    CHECK(m.backdropCameraX() != camX0);
    nes->ram[kGameMode] = kModePlay; frames(m, 2, aspect);
    const int camX2 = m.backdropCameraX();                                                                // back to plain play
    nes->ram[kObjX] = (uint8_t)(nes->ram[kObjX] + 16); frames(m, 1, aspect);
    CHECK_EQ(m.backdropCameraX(), camX2);
    nes->ram[kMenuState] = 8; nes->ram[kObjX] = (uint8_t)(nes->ram[kObjX] + 16); frames(m, 1, aspect);   // the menu opens a frame ahead: kept as well
    CHECK(m.backdropCameraX() != camX2);
    nes->ram[kMenuState] = 0;
    {   // the NES's own flicker and slowdown are off while widescreen is on (Insert brings them back) and never touched when it is off
        Widescreen n; n.init(*nes, err); n.unfogAnim = false; n.cloudShadow = false; n.lighting = false; n.hudOverlay = false;
        nes->ppu.unlimitedSprites = false; nes->lagFree = false;
        n.prepareFrame(*nes); CHECK(nes->ppu.unlimitedSprites); CHECK(nes->lagFree);
        n.nesLimits = true; n.prepareFrame(*nes); CHECK(!nes->ppu.unlimitedSprites); CHECK(!nes->lagFree);
        n.nesLimits = false; n.enabled = false; n.prepareFrame(*nes); CHECK(!nes->ppu.unlimitedSprites); CHECK(!nes->lagFree);
        nes->ppu.unlimitedSprites = false; nes->lagFree = false;
    }
}
