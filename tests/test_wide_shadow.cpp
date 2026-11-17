// Section 32: the neighbouring screens running (shadow games) are drawn into the margins.
#include <memory>
#include "test_wide_common.h"
#include "shadow.h"
#include "zelda_ram.h"
#include "widescreen_internal.h"

using namespace zelda;

void section32(Ctx& t) {
    SECTION_BINDINGS
    const int room = 0x23, east = 0x24;
    auto m_h = std::make_unique<Widescreen>(); Widescreen& m = *m_h; m.init(*nes, err); m.unfogAnim = false; m.cloudShadow = false; m.lighting = false; m.hudOverlay = false; m.caveBox = true; m.setWater(false);
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
    set.pool[0].adopt(*nes, east); monster(set.pool[0], zelda::kLinkOam0, 100, 100); set.active = {&set.pool[0]};
    CHECK_EQ(differing(base, picture(&set)), (size_t)0);
    set.pool[0].adopt(*nes, east); monster(set.pool[0], 6, 100, 20); set.active = {&set.pool[0]};
    CHECK_EQ(differing(base, picture(&set)), (size_t)0);
    set.pool[0].adopt(*nes, east); monster(set.pool[0], 21, 30, 50); set.active = {&set.pool[0]};             // the map marker (OAM 21) pokes out below the status bar: not a monster
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
    {   // between the glide and the hand-off, and while Link walks into the screen (mode 4: the game draws no monsters then), the bridge shadow of that screen is drawn over the picture
        setState(4, room); frames(m, 2, aspect);
        set.pool[1].adopt(*nes, room); monster(set.pool[1], 5, 100, 100); set.active = {&set.pool[1]};
        set.bridge = nullptr; const std::vector<uint32_t> enter = picture(&set);
        set.bridge = &set.pool[1]; CHECK(differing(enter, picture(&set)) > 100);
        set.bridge = nullptr; CHECK_EQ(differing(enter, picture(&set)), (size_t)0);
        set.bridge = &set.pool[1]; setState(5, room); frames(m, 2, aspect);          // (in play the game's own picture shows its monsters: the bridge is not drawn)
        const std::vector<uint32_t> play = picture(nullptr); CHECK_EQ(differing(play, picture(&set)), (size_t)0);
        set.bridge = &set.pool[1]; set.bridgeHold = ShadowSet::kBridgeHold - 1; CHECK(differing(play, picture(&set)) > 100);   // ... except for the first picture of play: the game's monsters are drawn one frame later
        set.bridgeHold = ShadowSet::kBridgeHold; CHECK_EQ(differing(play, picture(&set)), (size_t)0); set.bridgeHold = 0;
        set.bridge = nullptr;
    }
    {   // the monsters of a neighbouring screen go over everything: a monster that stands at the edge of its screen is not cut off by the game's own room
        const int west = room - 1; m.markExplored(slot, west);
        setState(5, room); frames(m, 2, aspect);
        const std::vector<uint32_t> b0 = picture(nullptr);
        set.pool[0].adopt(*nes, west); monster(set.pool[0], 5, 252, 100); set.active = {&set.pool[0]};
        const std::vector<uint32_t> w1 = picture(&set);
        int x0 = 1 << 30, x1 = -1;
        for (size_t i = 0; i < b0.size(); i++) if (b0[i] != w1[i]) { const int x = (int)(i % t.c.w); x0 = std::min(x0, x); x1 = std::max(x1, x); }
        CHECK(x1 - x0 == 7);                                                                                    // (4 of the 8 px lie over Link's room and are drawn: with the game's room on top only 4 px would show)
        set.active.clear();
    }
    {   // the sprites fade with the game's picture (stairs, caves, cellars fade by palette)
        Canvas cv; cv.w = 64; cv.h = 64; cv.pixels.assign(64 * 64, 0xFF202020u);
        set.pool[0].adopt(*nes, east); monster(set.pool[0], 5, 0, 0); Nes& sn = set.pool[0].nes();
        m.drawFadedSprite(sn, cv, 0x20, 10, HUD_H + 10, 256); const uint32_t full = cv.pixels[10 * 64 + 10];
        cv.pixels.assign(64 * 64, 0xFF202020u); m.drawFadedSprite(sn, cv, 0x20, 10, HUD_H + 10, 128);
        CHECK(full != 0xFF202020u); CHECK_EQ(cv.pixels[10 * 64 + 10], mixColor(0xFF000000u, full, 128));
        cv.pixels.assign(64 * 64, 0xFF202020u); m.drawFadedSprite(sn, cv, 0x20, 10, HUD_H + 10, 0); CHECK_EQ(cv.pixels[10 * 64 + 10], 0xFF000000u);
    }
    {   // the items, fairies and flames of the neighbouring screens glow and glitter like the ones of Link's own screen
        m.fog = false; m.lighting = true; set.enabled = true;
        set.pool[0].adopt(*nes, east); Nes& sn = set.pool[0].nes(); for (int sl = 1; sl <= 12; sl++) sn.ram[kObjType + sl] = 0;
        set.active = {&set.pool[0]}; m.shadows = &set;
        const float ex = (float)((east & 15) * ROOM_W), ey = (float)((east >> 4) * ROOM_H);
        m.lightSources.clear(); m.sparkles.clear(); m.gatherShadowLights(room, slot, true); CHECK(m.lightSources.empty());       // (nothing lies there)
        sn.ram[kObjType + 3] = kTypeItem; sn.ram[kObjState + 3] = kItemHeart; sn.ram[kObjX + 3] = 100; sn.ram[kObjY + 3] = 150;
        m.gatherShadowLights(room, slot, true);
        CHECK_EQ(m.lightSources.size(), (size_t)1);
        if (!m.lightSources.empty()) { CHECK_EQ((int)m.lightSources[0].x, (int)(ex + 108)); CHECK_EQ((int)m.lightSources[0].y, (int)(ey + 150 - HUD_H + 8)); CHECK(m.lightSources[0].r > 200 && m.lightSources[0].b < 140); }   // (a red glow at the heart)
        for (int i = 0; i < 80; i++) m.gatherShadowLights(room, slot, true);
        CHECK(!m.sparkles.empty());
        for (const auto& sp : m.sparkles) CHECK(std::fabs(sp.x - (ex + 108)) < 20 && sp.kind == 3);                          // (red glitter around it)
        const size_t before = m.sparkles.size(); m.lightSources.clear(); m.gatherShadowLights(room, slot, false); CHECK_EQ(m.sparkles.size(), before); CHECK_EQ(m.lightSources.size(), (size_t)1);   // (a glow without glitter)
        m.lightSources.clear(); m.gatherShadowLights(east, slot, true); CHECK(m.lightSources.empty());                       // (the screen Link is in is the game's own)
        sn.ram[kObjType + 3] = 0; sn.ram[kObjState + 0x13] = 0; sn.ram[kRoomItemId] = kItemHeartContainer; sn.ram[kObjX + 0x13] = 100; sn.ram[kObjY + 0x13] = 150;     // a heart container (the room item, slot $13)
        m.lightSources.clear(); m.sparkles.clear(); m.gatherShadowLights(room, slot, true); CHECK_EQ(m.lightSources.size(), (size_t)1);
        if (!m.lightSources.empty()) CHECK_EQ((int)m.lightSources[0].x, (int)(ex + 108));
        for (int i = 0; i < 80; i++) m.gatherShadowLights(room, slot, true);
        CHECK(!m.sparkles.empty());
        sn.ram[kObjState + 0x13] = 0x80; m.lightSources.clear(); m.gatherShadowLights(room, slot, false); CHECK(m.lightSources.empty());                            // (taken: dark)
        sn.ram[kObjState + 0x13] = 0; sn.ram[kRoomItemId] = 0x3F; m.lightSources.clear(); m.gatherShadowLights(room, slot, false); CHECK(m.lightSources.empty()); sn.ram[kRoomItemId] = 0;
        sn.ram[kObjType + 3] = kTypePondFairy; sn.ram[kObjState + 3] = 0; m.lightSources.clear(); m.gatherShadowLights(room, slot, false); CHECK_EQ(m.lightSources.size(), (size_t)1);
        sn.ram[kObjType + 3] = kTypeGuardFire; m.lightSources.clear(); m.gatherShadowLights(room, slot, false); CHECK_EQ(m.lightSources.size(), (size_t)1);
        sn.ram[kObjType + 3] = kTypeItem; sn.ram[kObjState + 3] = kItemHeart; m.fog = true; m.lightSources.clear(); m.gatherShadowLights(room, slot, false); CHECK_EQ(m.lightSources.size(), (size_t)1);   // (explored)
        m.fog = false; m.lighting = false; m.sparkles.clear(); m.lightSources.clear();
    }
    {   // the overworld goes on behind a cave: the monsters of the screens around it keep moving over the kept backdrop
        m.shadows = nullptr; m.fog = true; setState(5, room); nes->ram[kMenuState] = 0; frames(m, 3, aspect);
        setState(0xB, room); frames(m, 2, aspect);
        set.pool[0].adopt(*nes, east); monster(set.pool[0], 5, 100, 100); set.active = {&set.pool[0]};
        const std::vector<uint32_t> cave = picture(nullptr), live = picture(&set);
        CHECK(differing(cave, live) > 100);
        set.pool[0].nes().ppu.oam[5 * 4 + 3] = 140; set.pool[0].nes().ppu.buildSpriteLayer();
        const std::vector<uint32_t> moved = picture(&set);
        CHECK(differing(live, moved) > 100);                                                                  // (it moves while the cave box stands)
        set.enabled = false; CHECK_EQ(differing(cave, picture(&set)), (size_t)0); set.enabled = true;
        setState(5, room); frames(m, 2, aspect);
    }
    {   // the walk into stairs, a cave or a dungeon (mode $10): the neighbouring screens go on being drawn; the screen Link stands on shows the game's own monsters
        m.shadows = nullptr; m.fog = true; setState(5, room); nes->ram[kMenuState] = 0; frames(m, 3, aspect);
        setState(kModeStairs, room); frames(m, 2, aspect);
        set.pool[0].adopt(*nes, east); monster(set.pool[0], 5, 100, 100); set.pool[1].adopt(*nes, room); monster(set.pool[1], 5, 100, 100); set.active = {&set.pool[0], &set.pool[1]};
        set.base = room;
        const std::vector<uint32_t> walk = picture(nullptr), walkLive = picture(&set);
        CHECK_EQ(differing(walk, walkLive), (size_t)128);                                                      // (the east screen's monster only: the shadow of Link's own screen stays hidden)
        nes->ram[kRoomId] = 0x7F; const std::vector<uint32_t> stairs = picture(&set);                          // (stairs: RoomId is already the cellar's; the shadow of the screen he stands on stays hidden)
        CHECK(differing(walk, stairs) <= 128 + 128); nes->ram[kRoomId] = room;
        setState(5, room); frames(m, 2, aspect);
    }
    m.shadows = nullptr;
}
