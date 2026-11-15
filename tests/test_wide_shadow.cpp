// Section 32: the neighbouring screens running (shadow games) are drawn into the margins.
#include "test_wide_common.h"
#include "shadow.h"
#include "zelda_ram.h"
#include "widescreen_internal.h"

using namespace zelda;

void section32(Ctx& t) {
    SECTION_BINDINGS
    const int room = 0x23, east = 0x24;
    Widescreen m; m.init(*nes, err); m.unfogAnim = false; m.cloudShadow = false; m.lighting = false; m.hudOverlay = false; m.caveBox = true; m.setWater(false);
    for (int i = 0; i < OverworldMap::TILES; i++) nes->cart.cpuWrite(0x6530 + i, decoded[i]);
    nes->cart.cpuWrite(0x67F + room, 0); nes->cart.cpuWrite(0x6AFE + room, 0);
    setState(5, room); nes->ram[kMenuState] = 0; nes->ram[kCurLevel] = 0; nes->ram[kUndergroundExitType] = 0;
    frames(m, 3, aspect);
    const int slot = nes->ram[kCurSaveSlot];
    ShadowSet set; set.enabled = true;
    auto monster = [&](ShadowGame& g, int oamIdx, int x, int y) {                 // a monster: a 8x16 sprite of solid colour 1 in palette 0 (red) at (x, y)
        Nes& n = g.nes(); n.ppu.lineCtrl[100] = 0x20; n.ppu.ctrl |= 0x20; n.ppu.mask |= 0x10; n.ppu.palette[0x11] = 0x16;
        for (int r = 0; r < 32; r++) n.ppu.vramWrite((uint16_t)(0x200 + r), (r & 8) ? 0x00 : 0xFF);     // (low plane solid, high plane empty: colour 1; top half then bottom half)
        uint8_t* o = &n.ppu.oam[oamIdx * 4]; o[0] = (uint8_t)y; o[1] = 0x20; o[2] = 0; o[3] = (uint8_t)x;
    };
    auto picture = [&](const ShadowSet* s) { m.shadows = s; frames(m, 1, aspect); return t.c.pixels; };
    auto differing = [&](const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) { size_t n = 0; for (size_t i = 0; i < a.size() && i < b.size(); i++) n += a[i] != b[i]; return n; };
    set.pool[0].adopt(*nes, east); set.active = {&set.pool[0]};
    monster(set.pool[0], 5, 100, 100);
    const std::vector<uint32_t> none = picture(nullptr);
    // fog: the screen east is not explored: nothing living shows there
    m.fog = true;
    CHECK_EQ(differing(none, picture(&set)), (size_t)0);
    m.markExplored(slot, east);
    const std::vector<uint32_t> base = picture(nullptr), with = picture(&set);
    CHECK(differing(base, with) > 100);                                          // (8x16 = 128 pixels)
    {   // only the monster's 8x16 changed
        int x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
        for (size_t i = 0; i < base.size(); i++) if (base[i] != with[i]) { const int x = (int)(i % t.c.w), y = (int)(i / t.c.w); x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
        CHECK(x1 - x0 == 7 && y1 - y0 == 15);
    }
    // not drawn: the screen Link is in, the stand-in Link (OAM 18/19), the status bar's icons, a disabled set, a screen that is not ready
    set.pool[1].adopt(*nes, room); monster(set.pool[1], 5, 100, 100); set.active = {&set.pool[1]};
    CHECK_EQ(differing(base, picture(&set)), (size_t)0);
    set.armHandOff(); CHECK(differing(base, picture(&set)) > 100);                // (between the glide and the hand-off the screen Link entered shows its shadow's monsters over the game's picture)
    set.handOffArmed = false; CHECK_EQ(differing(base, picture(&set)), (size_t)0);
    set.pool[0].adopt(*nes, east); monster(set.pool[0], zelda::kLinkOam0, 100, 100); set.active = {&set.pool[0]};
    CHECK_EQ(differing(base, picture(&set)), (size_t)0);
    set.pool[0].adopt(*nes, east); monster(set.pool[0], 6, 100, 20); set.active = {&set.pool[0]};
    CHECK_EQ(differing(base, picture(&set)), (size_t)0);
    set.pool[0].adopt(*nes, east); monster(set.pool[0], 6, 100, 100); set.enabled = false;
    CHECK_EQ(differing(base, picture(&set)), (size_t)0);
    set.enabled = true; set.pool[0].reset();
    CHECK_EQ(differing(base, picture(&set)), (size_t)0);
    // a lower OAM index is in front
    set.pool[0].adopt(*nes, east); monster(set.pool[0], 6, 100, 100); set.pool[0].nes().ppu.oam[2 * 4] = 100; set.pool[0].nes().ppu.oam[2 * 4 + 1] = 0x20; set.pool[0].nes().ppu.oam[2 * 4 + 2] = 1; set.pool[0].nes().ppu.oam[2 * 4 + 3] = 100;
    set.pool[0].nes().ppu.palette[0x15] = 0x2A;
    const std::vector<uint32_t> two = picture(&set);
    auto count = [](const std::vector<uint32_t>& v, uint32_t c) { int n = 0; for (uint32_t p : v) n += p == c; return n; };
    CHECK_EQ(count(with, Ppu::nesColor(0x16)) - count(base, Ppu::nesColor(0x16)), 128);                 // (the monster: 8x16 pixels of its own colour)
    CHECK(count(two, Ppu::nesColor(0x2A)) - count(base, Ppu::nesColor(0x2A)) == 128);
    {   // the monster reflects in the water of its screen (the screen east gets a lake below it); not without monsters, not without reflections
        m.setWater(true); picture(nullptr);
        const int gx0 = (east & 15) * 32, gy0 = (east >> 4) * 22;
        for (int gy = 6; gy < 22; gy++) for (int gx = 8; gx < 26; gx++) m.wwKind[(size_t)(gy0 + gy) * (16 * 32) + gx0 + gx] = 1;
        picture(nullptr);                                                            // (pass A now sees the lake)
        // where the water is (pass A's mask): the 8 pixels of a row with the most water; the monster's feet go 4 rows above it
        const int rx = (east & 15) * 256 - m.terrainCamX, ry = 64 + (east >> 4) * 176 - m.terrainCamY; int wx = -1, wy = -1;
        int best = 0;
        for (int y = 70; y < 160; y++) for (int x = 8; x < 230; x++) {
            int n = 0; for (int k = 0; k < 8; k++) { const int cx = rx + x + k, cy = ry + y + Widescreen::kWaterMargin; n += cx >= 0 && cx < m.terrainW && cy >= 0 && m.terrainMask[(size_t)cy * m.terrainW + cx] == 1; }
            if (n > best) { best = n; wx = x; wy = y; }
        }
        CHECK(best >= 3);
        const int objY = wy - 4 + HUD_H - 17;
        set.pool[0].adopt(*nes, east); monster(set.pool[0], 5, wx, objY);
        Nes& sn = set.pool[0].nes(); for (int s = 1; s <= 12; s++) sn.ram[kObjType + s] = 0;
        sn.ram[kObjType + 1] = 0x10; sn.ram[kObjX + 1] = (uint8_t)wx; sn.ram[kObjY + 1] = (uint8_t)objY; sn.ppu.buildSpriteLayer();
        set.active = {&set.pool[0]};
        const std::vector<uint32_t> dry = picture(nullptr), wet = picture(&set);
        CHECK(differing(dry, wet) > 128);                                            // (the monster's 128 pixels and its mirror image)
        sn.ram[kObjType + 1] = 0; const std::vector<uint32_t> noMonster = picture(&set);
        CHECK_EQ(differing(dry, noMonster), (size_t)128);                            // (no object in the slots: the sprite is drawn, nothing is mirrored)
        sn.ram[kObjType + 1] = 0x10; m.waterReflections = false;
        const std::vector<uint32_t> dry2 = picture(nullptr), wet2 = picture(&set);
        CHECK_EQ(differing(dry2, wet2), (size_t)128);                                // (reflections off)
        m.waterReflections = true;
    }
    m.shadows = nullptr;
}
