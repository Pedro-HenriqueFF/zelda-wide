// Section 26: day/night clock, the lighting pass, light sources, dungeon torches.
#include "test_wide_common.h"
#include "lighting.h"
#include "widescreen_internal.h"
#include <cmath>
#include "zelda_ram.h"
#include <cstring>

using namespace zelda;

static double lumaBlock(const Canvas& c, int x0, int y0, int n) {
    double s = 0;
    for (int y = y0; y < y0 + n; y++) for (int x = x0; x < x0 + n; x++) {
        const uint32_t v = c.pixels[(size_t)y * c.w + x];
        s += ((v >> 16 & 255) + (v >> 8 & 255) + (v & 255)) / 3.0;
    }
    return s / (n * n);
}

void section26(Ctx& t) {
    SECTION_BINDINGS
    // 26a. The clock and the lighting pass on their own.
    {
        using namespace lighting;
        CHECK_EQ(daylightAt(0.25), 1.0f); CHECK_EQ(daylightAt(0.75), 0.0f);
        CHECK(daylightAt(0.56) > 0.0f && daylightAt(0.56) < 1.0f);                           // dusk and dawn are gradual
        CHECK(daylightAt(0.54) > daylightAt(0.58) && daylightAt(0.90) < daylightAt(0.96));
        Clock k; k.phase = 0.999; for (int i = 0; i < 100; i++) k.step();
        CHECK(k.phase < 0.01 && k.tick == 100);                                               // the day wraps
        k.frozen = true; const double ph = k.phase; k.step(); CHECK_EQ(k.phase, ph);
        k.skip(0.25); CHECK(std::fabs(k.phase - (ph + 0.25)) < 1e-9);
        CHECK(overworldAmbient(0) < 0.2f && overworldAmbient(1) == 1.0f);

        const int w = 200, h = 100;
        std::vector<uint32_t> img((size_t)w * h, 0xFF80A0C0u), orig = img;
        Params day; apply(img.data(), w, h, 0, h, 0, 0, day, {});
        CHECK(img == orig);                                                                  // full ambient, no lights: untouched
        Params night; night.ambient = 0.2f; night.nightTint = 1; night.vision = true; night.vx = 50; night.vy = 50; night.visionInner = 20; night.visionOuter = 60;
        apply(img.data(), w, h, 10, h, 0, 0, night, {});
        bool never = true;
        for (size_t i = 0; i < img.size(); i++) never = never && (img[i] >> 16 & 255) <= (orig[i] >> 16 & 255) && (img[i] & 255) <= (orig[i] & 255);
        CHECK(never);                                                                        // it only darkens
        CHECK_EQ(img[5 * w + 50], orig[5 * w + 50]);                                         // rows above `top` are never touched
        CHECK_EQ(img[50 * w + 50] & 255, orig[50 * w + 50] & 255);                           // Link's own spot keeps its blue; red and green lean blue at night
        CHECK((img[50 * w + 50] >> 16 & 255) < (orig[50 * w + 50] >> 16 & 255) && (img[50 * w + 50] >> 16 & 255) > (orig[50 * w + 50] >> 16 & 255) / 2);
        CHECK((img[90 * w + 190] & 255) < (orig[90 * w + 190] & 255) / 3);                   // far away: dark
        CHECK((img[90 * w + 190] >> 16 & 255) <= (img[90 * w + 190] & 255));                 // ... and bluish
        std::vector<uint32_t> lit = orig;
        Source fire; fire.x = 150; fire.y = 80; fire.radius = 30; fire.power = 1; fire.r = 255; fire.g = 150; fire.b = 60;
        Params dark; dark.ambient = 0.2f;
        apply(lit.data(), w, h, 0, h, 0, 0, dark, {fire});
        CHECK((lit[80 * w + 150] >> 16 & 255) > (lit[80 * w + 100] >> 16 & 255) * 2);        // a light brightens its surroundings ...
        CHECK_EQ(lit[80 * w + 100], lit[10 * w + 10]);                                       // ... and nothing beyond its radius
        CHECK((lit[80 * w + 150] >> 16 & 255) > (lit[80 * w + 150] & 255));                  // coloured: the red of a fire stronger than the blue
        std::vector<uint32_t> shifted = orig;
        fire.x = 150 + 1000; apply(shifted.data(), w, h, 0, h, 1000, 0, dark, {fire});       // (camera offset: same light at the same canvas place)
        CHECK(shifted == lit);
    }

    auto fresh = [&](Widescreen& w) {
        w.init(*nes, err); w.unfogAnim = false; w.cloudShadow = false; w.viewHeight = 176; w.fog = false; w.hudOverlay = false; w.caveBox = false;
        w.dayClock.frozen = true;
    };
    std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
    // 26b. Night on the overworld: dark away from Link, his surroundings stay lit, the status bar is untouched; daytime changes nothing.
    {
        Widescreen day, off, night, dusk;
        fresh(day); fresh(off); fresh(night); fresh(dusk); off.lighting = false;
        day.dayClock.phase = 0.25; night.dayClock.phase = 0.75; dusk.dayClock.phase = 0.56;
        setState(5, 0x23); frames(off, 4, aspect);
        const Canvas ref = c;
        frames(day, 4, aspect);  CHECK(c.pixels == ref.pixels);                              // noon = the plain picture
        frames(night, 4, aspect); const Canvas nc = c;
        frames(dusk, 4, aspect);  const Canvas dc = c;
        const int lx = ref.w / 2 - 8, ly = HUD_H + 176 / 2 - 8;                               // Link, the camera's centre
        const double near0 = lumaBlock(ref, lx, ly, 16), nearN = lumaBlock(nc, lx, ly, 16);
        const double far0 = lumaBlock(ref, 4, ref.h - 20, 16), farN = lumaBlock(nc, 4, ref.h - 20, 16), farD = lumaBlock(dc, 4, ref.h - 20, 16);
        CHECK(far0 > 5 && near0 > 5);                                                        // (the fake picture is not black there)
        CHECK(nearN > near0 * 0.9);                                                          // Link sees around him
        CHECK(farN < far0 * 0.3);                                                            // the distance is dark at night
        CHECK(farD > farN && farD < far0);                                                   // dusk is in between
        bool bar = true;
        for (int y = 0; y < HUD_H; y++) for (int x = 0; x < nc.w; x++) bar = bar && nc.pixels[(size_t)y * nc.w + x] == ref.pixels[(size_t)y * ref.w + x];
        CHECK(bar);
        night.lighting = false; frames(night, 2, aspect); CHECK(c.pixels == ref.pixels);       // F1 switches it off
    }

    // 26c. What glows. (Positions: ObjX + 8, ObjY - 64 + 8 in the room's world position.)
    {
        Widescreen w; fresh(w);
        auto clear = [&] { for (int s = 0; s < 0x14; s++) { nes->ram[kObjState + s] = 0; nes->ram[kObjType + s] = 0; } w.sparkles.clear(); };
        auto lights = [&](int room = 0x23, bool armos = false) { ViewSnapshot v; v.room = (uint8_t)room; w.gatherLights(*nes, v, 0, 0, 0, armos); return w.lightSources; };
        std::vector<uint8_t> savedTiles(704);                                                // (the screen's real tiles come back at the end)
        for (int i = 0; i < 704; i++) { savedTiles[i] = nes->cart.cpuRead(0x6530 + i); nes->cart.cpuWrite(0x6530 + i, 0x24); }   // no armos statues on this screen
        clear(); CHECK(lights().empty());
        nes->ram[kObjState + kSlotBombA] = 0x20; nes->ram[kObjX + kSlotBombA] = 100; nes->ram[kObjY + kSlotBombA] = 150;
        auto l = lights();
        CHECK(l.size() == 1 && l[0].x == 108 && l[0].y == 94 && l[0].r > l[0].g && l[0].g > l[0].b);   // fire: orange
        nes->ram[kObjState + kSlotBombA] = 0x11; CHECK(lights().empty());                    // a ticking bomb does not glow ...
        nes->ram[kObjState + kSlotBombA] = kStateBombFlash; l = lights();
        CHECK(l.size() == 1 && l[0].radius > 100);                                           // ... its explosion does, widely
        clear(); nes->ram[kObjState + kSlotShot] = 0x10;
        l = lights(); CHECK(l.size() == 1 && l[0].b > l[0].r);                               // sword shot / splash: cold white
        clear();
        nes->ram[kObjState + 3] = 1; nes->ram[kObjType + 3] = kTypeFireballA; CHECK_EQ(lights().size(), 1u);     // Zora's fireball
        nes->ram[kObjType + 3] = 0x53; CHECK(lights().empty());                              // an octorok's rock does not
        nes->ram[kObjType + 3] = kTypeMonsterSwordShot; CHECK_EQ(lights().size(), 1u);
        nes->ram[kObjType + 3] = kTypeMagicB; l = lights(); CHECK(l.size() == 1 && l[0].b > l[0].r);   // wizzrobe magic: violet
        nes->ram[kObjType + 3] = kTypeStandingFire; CHECK_EQ(lights().size(), 1u);
        nes->ram[kObjState + 3] = 0; nes->ram[kObjType + 3] = kTypeFireballA; CHECK(lights().empty());          // (an empty slot never)
        clear();

        // armos statues are background tiles until touched: blue, the secret one red (live tiles of the current screen)
        auto put = [&](int col, int row, int t0, int t2) { nes->cart.cpuWrite(0x6530 + col * 22 + row, (uint8_t)t0); nes->cart.cpuWrite(0x6530 + (col + 1) * 22 + row, (uint8_t)t2); };
        for (int i = 0; i < 704; i++) nes->cart.cpuWrite(0x6530 + i, 0x24);
        put(10, 5, 0xC0, 0xC2);
        l = lights(0x23, true);
        CHECK(l.size() == 1 && l[0].b > l[0].r && l[0].x == 10 * 8 + 8 && l[0].y == 5 * 8 + 8);                // a statue: blue at its middle
        put(22, 8, 0xC0, 0xC2);                                                                              // (0xB0 / 8, ($80 - 64) / 8) in room $0B: the secret armos
        l = lights(0x0B, true); CHECK(l.size() == 2 && l[1].r > 200 && l[1].b < 100);
        l = lights(0x23, true); CHECK(l.size() == 2 && l[1].b > l[1].r);                                     // (in another room the same place is just a statue)
        put(22, 8, 0x24, 0x24);                                                                              // woken or burnt: the tile is gone, the light too
        l = lights(0x0B, true); CHECK(l.size() == 1);
        put(10, 5, 0x24, 0x24);
        {   // a screen next to it (decoded tiles)
            std::vector<uint8_t> tt(704, 0x24); tt[3 * 22 + 4] = 0xC0; tt[4 * 22 + 4] = 0xC2;
            w.lightSources.clear(); w.gatherArmosTiles(tt.data(), 0x24, 256.0f, 0.0f);
            CHECK(w.lightSources.size() == 1 && w.lightSources[0].x == 256 + 3 * 8 + 8 && w.lightSources[0].b > w.lightSources[0].r);
        }
        const int room = 0x0B;
        // armos monsters: blue, the secret one (in its place) red, and the light follows them
        clear(); nes->ram[kObjState + 2] = 1; nes->ram[kObjType + 2] = kTypeArmos; nes->ram[kObjX + 2] = 0x50; nes->ram[kObjY + 2] = 0x60;
        l = lights(0x23, true);
        CHECK(!l.empty() && l[0].b > l[0].r && l[0].x == 0x58 && l[0].y == 0x60 - HUD_H + 8);      // any armos: blue
        nes->ram[kObjX + 2] = 0x70; l = lights(0x23, true); CHECK(!l.empty() && l[0].x == 0x78);    // ... walking: the light goes with it
        nes->ram[kObjX + 2] = 0xB0; nes->ram[kObjY + 2] = 0x80;
        l = lights(room, true);
        CHECK(!l.empty() && l[0].r > 200 && l[0].b < 100);                                // the secret one, still in place: red
        nes->ram[kObjX + 2] = 0xB4; l = lights(room, true); CHECK(!l.empty() && l[0].b > l[0].r);     // it moved: blue
        clear();
        nes->ram[kObjState + 2] = 1; nes->ram[kObjType + 2] = kTypePondFairy; CHECK_EQ(lights().size(), 1u);
        nes->ram[kObjType + 2] = kTypeItem; CHECK_EQ(lights().size(), 1u);
        clear(); nes->ram[kObjState + 0x0F] = 1; nes->ram[kObjX + 0x0F] = 100; nes->ram[kObjY + 0x0F] = 150;
        w.sparkles.clear(); for (int i = 0; i < 20; i++) lights();
        CHECK(!w.sparkles.empty() && w.sparkles[0].kind == 1);                                // the boomerang leaves a white trail ...
        clear(); nes->ram[kObjState + 0x12] = 1; w.sparkles.clear(); for (int i = 0; i < 20; i++) lights();
        CHECK(!w.sparkles.empty() && w.sparkles[0].kind == 1);                                // ... and so does an arrow
        clear(); nes->ram[kObjState + 3] = kItemTriforce; nes->ram[kObjType + 3] = kTypeItem; w.sparkles.clear(); for (int i = 0; i < 40; i++) lights();
        { bool gold = !w.sparkles.empty(); for (const auto& sp : w.sparkles) gold = gold && sp.kind == 2; CHECK(gold); }   // a triforce shard glitters gold
        clear(); nes->ram[kInvCandle] = 1; nes->ram[kSelectedItem] = 0;
        CHECK(lights().empty());                                                             // owned but not the B item: no light
        nes->ram[kSelectedItem] = kItemSlotCandle;
        l = lights(); CHECK(l.size() == 1 && l[0].b > l[0].r);                               // blue candle held: a small blue light (Link's ObjState is 0 when idle)
        nes->ram[kInvCandle] = 2; l = lights(); CHECK(l.size() == 1 && l[0].r > l[0].b);     // red candle: warm
        nes->ram[kInvCandle] = 0; CHECK(lights().empty());
        nes->ram[kSelectedItem] = 0;
        // objects that live without an ObjState: flames of cave people, armos, the fairy
        clear(); nes->ram[kObjType + 3] = kTypeStandingFire; CHECK_EQ(lights().size(), 1u);
        clear(); nes->ram[kObjType + 3] = kTypeArmos; nes->ram[kObjX + 3] = 0x50; nes->ram[kObjY + 3] = 0x60;
        l = lights(); CHECK(l.size() == 1 && l[0].add > 0 && l[0].b > l[0].r);               // (with a halo that shows in daylight too)
        clear(); nes->ram[kObjState + 3] = kItemFairy; nes->ram[kObjType + 3] = kTypeItem;
        w.sparkles.clear(); for (int i = 0; i < 40; i++) lights();
        CHECK(!w.sparkles.empty() && w.sparkles[0].kind == 0);                               // a dropped fairy throws pink sparkles
        bool pink = true; for (const auto& sp : w.sparkles) pink = pink && std::fabs(sp.x - 0x58) < 20 && std::fabs(sp.y - (0x60 - HUD_H + 8)) < 20;
        CHECK(pink);
        clear(); nes->ram[kObjType + 3] = kTypePondFairy; w.sparkles.clear(); for (int i = 0; i < 40; i++) lights();
        CHECK(!w.sparkles.empty());                                                          // ... and the pond fairy too
        clear(); w.sparkles.clear(); for (int i = 0; i < 40; i++) lights(); CHECK(w.sparkles.empty());
        // a sword that is only swung (no shot: slot $0E in a state below $10) does not light
        clear(); nes->ram[kObjState + kSlotShot] = 3; CHECK(lights().empty());
        clear();
        for (int i = 0; i < 704; i++) nes->cart.cpuWrite(0x6530 + i, savedTiles[i]);
    }

    // 26c2. The clock: hours, the twilight, lightning, the sight that stays put.
    {
        using namespace lighting;
        Clock k; k.phase = 0; CHECK(std::fabs(k.hour() - 6.0) < 1e-9);
        k.phase = 0.25; CHECK(std::fabs(k.hour() - 12.0) < 1e-9);
        k.phase = 0.5;  CHECK(std::fabs(k.hour() - 18.0) < 1e-9);
        CHECK_EQ(twilightAt(0.25), 0.0f);                                                    // noon: none
        CHECK(twilightAt(16.5 / 24 - 0.25) > 0.0f && twilightAt(16.5 / 24 - 0.25) < 0.5f);   // it begins at 16:00 ...
        CHECK_EQ(twilightAt(0.5), 1.0f);                                                     // ... is full at 18:00 ...
        CHECK(twilightAt(0.56) < 1.0f && twilightAt(0.56) > 0.0f);                           // ... fades while the night comes
        CHECK_EQ(twilightAt(0.75), 0.0f);
        const int w = 40, h = 10;
        std::vector<uint32_t> img((size_t)w * h, 0xFF808080u);
        Params tw; tw.warm = 1; apply(img.data(), w, h, 0, h, 0, 0, tw, {});
        CHECK((img[0] >> 16 & 255) > (img[0] >> 8 & 255) && (img[0] >> 8 & 255) > (img[0] & 255));   // orange: red > green > blue
        Clock day; day.frozen = true; day.phase = 0.25; day.nextBolt = 1; for (int i = 0; i < 5; i++) day.step(); CHECK(day.bolt < 0);   // no lightning by day
        Clock nt; nt.frozen = true; nt.phase = 0.75; nt.nextBolt = 1; nt.step(); CHECK(nt.bolt == 0);
        float peak = 0; for (int i = 0; i < 60; i++) { nt.step(); peak = std::max(peak, nt.lightning()); }
        CHECK(peak > 0.8f && nt.bolt < 0 && nt.lightning() == 0.0f);                         // a flash that ends
    }
    {   // Link's sight does not jump with coordinates that are not his overworld ones (cave, dungeon exit)
        Widescreen w; fresh(w); w.dayClock.phase = 0.75;
        setState(5, 0x23); frames(w, 6, aspect);
        const float x0 = w.lightLinkX, y0 = w.lightLinkY;
        CHECK(x0 > 0 && y0 > 0);
        nes->ram[kObjX] = 0x10; nes->ram[kObjY] = 0xE0;                                        // coordinates that jump: not followed at once
        frames(w, 3, aspect);
        CHECK(std::fabs(w.lightLinkX - x0) < 2 && std::fabs(w.lightLinkY - y0) < 2);
        frames(w, 12, aspect);                                                               // ... but once they hold
        CHECK(std::fabs(w.lightLinkX - x0) > 20 || std::fabs(w.lightLinkY - y0) > 20);
    }

    // 26d. Dungeons: torches in the corners of a room that is not dark; unlit and dark at first, lit later; dark rooms are left to the game.
    {
        Widescreen lit, off; fresh(lit); fresh(off); off.lighting = false;
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        setState(5, 0x23, 1); frames(off, 40, aspect); const Canvas ref = c;
        setState(4, 0x23, 1); frames(lit, 8, aspect); const Canvas early = c;                // entering: the room dark, the torches there but unlit
        setState(5, 0x23, 1); frames(lit, 60, aspect); const Canvas lc = c;
        CHECK(ref.w == lc.w && ref.h == lc.h);
        const double mid0 = lumaBlock(ref, 120, HUD_H + 80, 16), midL = lumaBlock(lc, 120, HUD_H + 80, 16), midE = lumaBlock(early, 120, HUD_H + 80, 16);
        const double corner0 = lumaBlock(ref, 40, HUD_H + 40, 12), cornerL = lumaBlock(lc, 40, HUD_H + 40, 12), cornerE = lumaBlock(early, 40, HUD_H + 40, 12);
        CHECK(corner0 > 5 && mid0 > 5);
        CHECK(cornerL > corner0 * 0.7);                                                      // lit: bright beside the torch
        CHECK(midL < mid0 * 0.8);                                                            // ... the middle of the room dimmer
        CHECK(cornerL / corner0 > midL / mid0);
        CHECK(midE < midL * 0.7 && cornerE < cornerL * 0.7);                                 // before the torches light up the room is darker
        auto flames = [&](const Canvas& cv) {                                                // flame-coloured pixels around the four torches
            int n = 0;
            for (int i = 0; i < 4; i++) {
                const int cx = i & 1 ? 240 : 16, cy = HUD_H + (i & 2 ? 160 : 16);
                for (int y = cy - 12; y <= cy + 12; y++) for (int x = cx - 12; x <= cx + 12; x++) { const uint32_t v = cv.pixels[(size_t)y * cv.w + x]; n += (v >> 16 & 255) > 200 && (v >> 8 & 255) > 80 && (v & 255) < 100; }
            }
            return n;
        };
        CHECK(flames(lc) >= 4 * 8);                                                          // lit torches burn
        CHECK(flames(early) == 0);
        int bracket = 0;                                                                     // ... but they are already there, unlit (brownish pixels in the teal room)
        for (int i = 0; i < 4; i++) {
            const int cx = i & 1 ? 240 : 16, cy = HUD_H + (i & 2 ? 160 : 16);
            for (int y = cy - 12; y <= cy + 12; y++) for (int x = cx - 12; x <= cx + 12; x++) { const uint32_t v = early.pixels[(size_t)y * early.w + x]; bracket += (v >> 16 & 255) > (v & 255) + 4; }
        }
        CHECK(bracket >= 4 * 6);
        // the torch follows the corner line: top-left "" the flame points up-left, top-right up-right, bottom-left up-right, bottom-right up-left
        auto flameCentre = [&](int i, int& dx, int& dy) {
            const int cx = i & 1 ? 240 : 16, cy = HUD_H + (i & 2 ? 160 : 16); long sx = 0, sy = 0, n = 0;
            for (int y = cy - 12; y <= cy + 12; y++) for (int x = cx - 12; x <= cx + 12; x++) { const uint32_t v = lc.pixels[(size_t)y * lc.w + x]; if ((v >> 16 & 255) > 200 && (v >> 8 & 255) > 80 && (v & 255) < 100) { sx += x - cx; sy += y - cy; n++; } }
            dx = n ? (int)(sx / n) : 0; dy = n ? (int)(sy / n) : 0;
        };
        int dx, dy;
        flameCentre(0, dx, dy); CHECK(dx < 0 && dy < 0);
        flameCentre(1, dx, dy); CHECK(dx > 0 && dy < 0);
        flameCentre(2, dx, dy); CHECK(dx < 0 && dy > 0);
        flameCentre(3, dx, dy); CHECK(dx > 0 && dy > 0);
        // sliding to the next room: the torches (and their light) go with the picture. Horizontal scroll by 100: the old room's right
        // torches are at x 140, the new room's left ones at 116 (+256 - 100 - 16 ... = 172)
        setState(7, 0x23, 1);
        frames(lit, 1, aspect);                                                              // (the first frame still shows the room at rest)
        for (int i = 0; i < 25; i++) { gScrollX += 4; frames(lit, 1, aspect); }               // (4 px a frame, like the game)
        frames(lit, 1, aspect);
        const Canvas sc = c;
        auto flameCount = [&](int x0, int x1) { int n = 0; for (int y = HUD_H; y < sc.h; y++) for (int x = x0; x < x1; x++) { const uint32_t v = sc.pixels[(size_t)y * sc.w + x]; n += (v >> 16 & 255) > 200 && (v >> 8 & 255) > 80 && (v & 255) < 100; } return n; };
        CHECK(flameCount(130, 150) >= 2 * 6);                                                // the room we leave: its torches are lit and slide away
        CHECK(flameCount(160, 185) == 0 && flameCount(0, 14) == 0 && flameCount(228, 256) == 0);   // the room we enter: its torches are there but unlit
        CHECK(lumaBlock(sc, 176, HUD_H + 60, 12) < lumaBlock(sc, 110, HUD_H + 60, 12) * 0.75);       // ... and the room is dark until Link has entered
        gScrollX = 0; setState(4, 0x24, 1); frames(lit, 10, aspect);
        { int n = 0; for (int y = HUD_H; y < c.h; y++) for (int x = 0; x < 256; x++) { const uint32_t v = c.pixels[(size_t)y * c.w + x]; n += (v >> 16 & 255) > 200 && (v >> 8 & 255) > 80 && (v & 255) < 100; } CHECK(n == 0); }   // after the slide the new room is still unlit (mode 4)
        setState(5, 0x24, 1); frames(lit, 40, aspect);
        { const Canvas& cc = c; int n = 0; for (int y = HUD_H; y < cc.h; y++) for (int x = 0; x < 256; x++) { const uint32_t v = cc.pixels[(size_t)y * cc.w + x]; n += (v >> 16 & 255) > 200 && (v >> 8 & 255) > 80 && (v & 255) < 100; } CHECK(n >= 4 * 8); }   // Link is in: the torches light up
        // sliding up or down: the next room follows 176 px behind
        setState(5, 0x23, 1); frames(lit, 40, aspect);
        setState(7, 0x23, 1); frames(lit, 1, aspect); for (int i = 0; i < 25; i++) { gScrollY += 4; frames(lit, 1, aspect); } frames(lit, 1, aspect);
        { const Canvas& cc = c; auto fl = [&](int y0, int y1) { int n = 0; for (int y = HUD_H + y0; y < HUD_H + y1; y++) for (int x = 0; x < 256; x++) { const uint32_t v = cc.pixels[(size_t)y * cc.w + x]; n += (v >> 16 & 255) > 200 && (v >> 8 & 255) > 80 && (v & 255) < 100; } return n; };
          CHECK(fl(48, 72) >= 2 * 6);                                                        // the leaving room's bottom torches (160 - 100 = 60), lit
          CHECK(fl(84, 100) == 0 && fl(150, 176) == 0);                                      // the entering room's top torches (16 + 76 = 92) are there but unlit
        }
        gScrollY = 0;
        // the end of a slide: the picture is the next room alone, the registers return to the old rest value, the game is still in mode 7 for a frame
        auto slideTo = [&](int& g, int total, int step) { for (int i = 0; i < total / (step < 0 ? -step : step); i++) { g += step; frames(lit, 1, aspect); } };
        setState(5, 0x23, 1); frames(lit, 40, aspect);
        setState(7, 0x23, 1); frames(lit, 1, aspect);
        slideTo(gScrollX, 256, 4);
        gScrollX = 0; frames(lit, 1, aspect);                                                // (the register is back at its old value)
        { int n = 0; for (int y = HUD_H; y < c.h; y++) for (int x = 0; x < 256; x++) { const uint32_t v = c.pixels[(size_t)y * c.w + x]; n += (v >> 16 & 255) > 200 && (v >> 8 & 255) > 80 && (v & 255) < 100; } CHECK(n == 0); }
        CHECK(lumaBlock(c, 120, HUD_H + 80, 16) < lumaBlock(ref, 120, HUD_H + 80, 16) * 0.6);   // the new room is dark, not lit for a frame
        // sliding up: the room we leave stays lit, the room above comes in dark
        setState(5, 0x23, 1); frames(lit, 40, aspect);
        setState(7, 0x23, 1); frames(lit, 1, aspect);
        slideTo(gScrollY, 100, -4);
        { int old = 0, nw = 0; for (int y = HUD_H; y < c.h; y++) for (int x = 0; x < 256; x++) { const uint32_t v = c.pixels[(size_t)y * c.w + x]; const bool f = (v >> 16 & 255) > 200 && (v >> 8 & 255) > 80 && (v & 255) < 100; if (y - HUD_H >= 100) old += f; else nw += f; }
          CHECK(old >= 2 * 6 && nw == 0);                                                    // lit torches below (the old room moved down), none lit above
          CHECK(lumaBlock(c, 120, HUD_H + 150, 12) > lumaBlock(c, 120, HUD_H + 40, 12)); }
        gScrollY = 0;
        gScrollX = 0;
        nes->cart.cpuWrite(0x6A7E + 0x23, 0x80);                                             // a dark room: no torches, no dimming
        Widescreen dk, dko; fresh(dk); fresh(dko); dko.lighting = false;
        setState(5, 0x23, 1); frames(dko, 40, aspect); const Canvas dref = c;
        frames(dk, 40, aspect); CHECK(c.pixels == dref.pixels);
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        Widescreen sw; fresh(sw);                                                            // overworld again: nothing left over from the dungeon
        setState(5, 0x23); frames(sw, 3, aspect);
        CHECK(sw.dungeonDim == 0.0f && sw.dungeonLit == 0.0f);
    }

    // 26d1c. The candle's light around Link stays while a dungeon room scrolls (the game hides Link and its objects then) and goes on with him: from where he left the room
    // towards where the next room's door puts him, as far as the slide has got.
    {
        Widescreen fl; fresh(fl);
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        nes->ram[kInvCandle] = 2; nes->ram[kSelectedItem] = kItemSlotCandle;
        nes->ram[kObjX] = 120; nes->ram[kObjY] = (uint8_t)(HUD_H + 80);                                  // Link's centre (128, 88)
        std::memset(nes->ppu.oam, 0xFF, 256);                                                            // (his sprites are hidden)
        gScrollX = 0; setState(5, 0x23, 1); frames(fl, 60, aspect);
        setState(7, 0x24, 1); frames(fl, 1, aspect);
        for (int k = 0; k < 17; k++) { gScrollX += 4; frames(fl, 1, aspect); }
        CHECK(fl.slideX == 64);
        // the light is at x = 128 + (24 - 128) * 64/256 = 102
        const float at102 = lumaBlock(c, 102, HUD_H + 88, 5), far = lumaBlock(c, 102 + 80, HUD_H + 88, 5);
        CHECK(at102 > far + 1.0f);
        CHECK(at102 > lumaBlock(c, 150, HUD_H + 88, 5) + 1.0f);                                          // (not at his old place any more)
        gScrollX = 0; nes->ram[kInvCandle] = 0; nes->ram[kSelectedItem] = 0;
    }

    // 26d1. A fairy glowing in a dungeon room keeps its light while the room scrolls away: the glow moves with the room (the game hides the fairy itself).
    {
        Widescreen fl; fresh(fl);
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }
        nes->ram[kObjType + 3] = kTypeItem; nes->ram[kObjState + 3] = kItemFairy; nes->ram[kObjX + 3] = 100; nes->ram[kObjY + 3] = 150;   // glow at (108, 94)
        gScrollX = 0; setState(5, 0x23, 1); frames(fl, 60, aspect);
        CHECK(!fl.frozenLights.empty());
        const float still = lumaBlock(c, 108, HUD_H + 94, 5);
        setState(7, 0x24, 1); frames(fl, 1, aspect);                                                     // (the picture lags a frame: still play)
        for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }   // (the room's objects are gone / hidden while it scrolls)
        for (int k = 0; k < 17; k++) { gScrollX += 4; frames(fl, 1, aspect); }
        CHECK(fl.slideX == 64);
        CHECK(lumaBlock(c, 108 - 64, HUD_H + 94, 5) > lumaBlock(c, 108 + 40, HUD_H + 94, 5) + 1.0f);    // the glow went with the room
        (void)still; gScrollX = 0;
        for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }
    }

    // 26d2b0. The food Link drops lies in slot $0F like the boomerang: its white trail is not made while the room scrolls (it hung around the frozen meat)
    {
        Widescreen fl; fresh(fl);
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }
        nes->ram[kObjState + 0x0F] = 1; nes->ram[kObjX + 0x0F] = 100; nes->ram[kObjY + 0x0F] = 150;
        gScrollX = gScrollY = 0; setState(5, 0x23, 1); frames(fl, 40, aspect);
        { bool trail = false; for (const auto& sp : fl.sparkles) trail |= sp.kind == 1; CHECK(trail); }     // (playing: a trail, as before)
        setState(7, 0x24, 1); frames(fl, 1, aspect);
        for (int k = 0; k < 12; k++) { gScrollY += 4; frames(fl, 1, aspect); }
        { bool trail = false; for (const auto& sp : fl.sparkles) trail |= sp.kind == 1; CHECK(!trail); }    // scrolling: none
        nes->ram[kObjState + 0x0F] = 0; gScrollX = gScrollY = 0;
    }

    // 26d2a. A rupee on the floor of a dungeon room: its glitter goes on around it while the room scrolls away (the glitter moves with the room), and no flash is
    // left at its old place when the next room arrives (the game still holds the old room's objects until it starts to play).
    {
        Widescreen fl; fresh(fl);
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }
        nes->ram[kObjType + 3] = kTypeItem; nes->ram[kObjState + 3] = kItemRupee; nes->ram[kObjX + 3] = 100; nes->ram[kObjY + 3] = 150;
        gScrollX = 0; setState(5, 0x23, 1); frames(fl, 60, aspect);
        CHECK(!fl.frozenSparkSpots.empty());
        setState(7, 0x24, 1); frames(fl, 1, aspect);
        for (int k = 0; k < 17; k++) { gScrollX += 4; frames(fl, 1, aspect); }                    // (the item stays in RAM, as in the game)
        CHECK(fl.slideX == 64 && !fl.sparkles.empty());                                             // glitter is still being made
        bool near = true; for (const Widescreen::Sparkle& sp : fl.sparkles) near &= std::fabs(sp.x - 108.0f) < 20 && std::fabs(sp.y - 94.0f) < 20;
        CHECK(near);                                                                                // (in the room's own coordinates: it is drawn shifted by the slide)
        for (int k = 0; k < 50; k++) { gScrollX += 4; frames(fl, 1, aspect); }
        CHECK(fl.sparkles.empty());                                                                 // arrived: nothing flashes at the old place
        setState(4, 0x24, 1); gScrollX = 0; frames(fl, 12, aspect);
        CHECK(fl.sparkles.empty());                                                                 // the new room is not playing yet: the old objects are not used
        setState(5, 0x24, 1); frames(fl, 30, aspect);
        CHECK(!fl.sparkles.empty());                                                                // (playing: things glitter again)
        for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }
    }

    // 26d2b. Climbing out of a cellar (modes $A, $10 or 4 are all used for the exit in this code): the room's torches are not drawn over the cellar's picture while it
    // fades, and the picture is the cellar's own (with its light) until it is black.
    {
        Widescreen cf; fresh(cf); std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);            // (no sprites left over from other sections)
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }
        gScrollX = 0; setState(5, 0x23, 1); frames(cf, 40, aspect);
        const int exits[3] = {kModeLeaveCave, kModeStairs, 4};
        for (int e = 0; e < 3; e++) {
            setState(9, 0x23, 1); frames(cf, 1, aspect);
            for (size_t i = 0; i < (size_t)Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF000000;
            frames(cf, 3, aspect);                                                                  // black: the cellar is next
            for (size_t i = 0; i < (size_t)Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF808080;
            frames(cf, 60, aspect);                                                                 // (the cellar's own light has settled)
            const Canvas inCellar = c;
            setState(exits[e], 0x23, 1); frames(cf, 2, aspect);
            size_t diff = 0; for (int y = HUD_H + 140; y < HUD_H + 176; y++) for (int x = 0; x < 256; x++) diff += c.pixels[(size_t)y * c.w + x] != inCellar.pixels[(size_t)y * c.w + x];
            CHECK_EQ(diff, (size_t)0);                                                              // the bottom corners show no torches
            for (size_t i = 0; i < (size_t)Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF000000;
            frames(cf, 3, aspect); CHECK(cf.frozenDone);                                            // black: the room comes
        }
        for (int i = 0; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = i < Ppu::W * 64 ? 0xFF654321 : 0xFF123456;     // (the fixture's picture again)
        setState(5, 0x23, 1); gScrollX = 0;
    }

    // 26d2c. Climbing out of a cellar: the game has moved Link already while the cellar's picture fades; the cellar's light stays around where he stood
    {
        Widescreen lf; fresh(lf); std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }
        gScrollX = 0; setState(5, 0x23, 1); frames(lf, 40, aspect);
        setState(9, 0x23, 1); nes->ram[kObjX] = 60; nes->ram[kObjY] = 100; frames(lf, 1, aspect);
        for (size_t i = 0; i < (size_t)Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF000000;
        frames(lf, 3, aspect);
        for (size_t i = 0; i < (size_t)Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF808080;
        frames(lf, 60, aspect);
        const Canvas inCellar = c;
        setState(kModeLeaveCave, 0x23, 1); nes->ram[kObjX] = 120; nes->ram[kObjY] = 150;            // (where the game puts him for the room)
        for (int k = 0; k < 2; k++) {                                                                 // (the first frame still shows the cellar: the game's memory is a frame ahead of the picture)
            frames(lf, 1, aspect);
            size_t diff = 0; for (int y = HUD_H + 36; y < HUD_H + 52; y++) for (int x = 60; x < 76; x++) diff += c.pixels[(size_t)y * c.w + x] != inCellar.pixels[(size_t)y * c.w + x];
            CHECK_EQ(diff, (size_t)0);
        }
        for (size_t i = 0; i < (size_t)Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF000000;
        frames(lf, 3, aspect);
        for (int i = 0; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = i < Ppu::W * 64 ? 0xFF654321 : 0xFF123456;
        setState(5, 0x23, 1); gScrollX = 0; nes->ram[kObjX] = 120; nes->ram[kObjY] = 100;
    }

    // 26d3. The torches follow the game's fade of the picture: on the stairs the flames dim with the palette instead of burning on a dark picture
    {
        Widescreen tf; fresh(tf);
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        for (int s2 = 1; s2 <= 12; s2++) { nes->ram[kObjType + s2] = 0; nes->ram[kObjState + s2] = 0; }
        gScrollX = 0;
        setState(5, 0x23, 1); for (int i = 0; i < 16; i++) nes->ppu.palette[i] = 0x30; frames(tf, 60, aspect);
        auto orange = [&] {                                                                // flame-coloured pixels around the four torches
            int n = 0;
            for (int i = 0; i < 4; i++) {
                const int cx = i & 1 ? 240 : 16, cy = HUD_H + (i & 2 ? 160 : 16);
                for (int y = cy - 12; y <= cy + 12; y++) for (int x = cx - 12; x <= cx + 12; x++) { const uint32_t v = c.pixels[(size_t)y * c.w + x]; n += (v >> 16 & 255) > 200 && (v >> 8 & 255) > 80 && (v & 255) < 100; }
            }
            return n;
        };
        const int lit = orange();
        CHECK(tf.dunRefLuma > 0);
        setState(0x10, 0x24, 1); frames(tf, 1, aspect);                                      // (the picture lags a frame: still play)
        for (int i = 0; i < 16; i++) nes->ppu.palette[i] = 0x0F;                             // the game's fade has taken the palette to black
        frames(tf, 4, aspect);
        const int faded = orange();
        CHECK(lit > 0 && faded < lit / 4);
        for (int i = 0; i < 16; i++) nes->ppu.palette[i] = 0x30;
    }

    // 26d2. A cellar (mode 9) has no torches but a little light around Link; the death scene keeps the torches lit.
    {
        Widescreen cl; fresh(cl);
        nes->cart.cpuWrite(0x6A7E + 0x23, 0);
        setState(5, 0x23, 1); frames(cl, 60, aspect);
        CHECK(cl.dungeonLit > 0.99f);
        setState(0x11, 0x23, 1); frames(cl, 20, aspect);
        CHECK(cl.dungeonLit > 0.99f && cl.dungeonDim > 0.99f);                               // dying in a dungeon: the lighting stays
        setState(9, 0x23, 1); nes->ram[kObjX] = 0x78; nes->ram[kObjY] = 0xA0;
        frames(cl, 4, aspect);                                                               // (the old room still fades: dungeon lighting, torches stay)
        CHECK(cl.dungeonLit > 0.9f && cl.dungeonDim > 0.9f && !cl.cellarBlackSeen);
        { std::vector<uint32_t> keep(nes->ppu.framebuffer, nes->ppu.framebuffer + Ppu::W * Ppu::H);
          std::fill(nes->ppu.framebuffer, nes->ppu.framebuffer + Ppu::W * Ppu::H, 0xFF000000u); frames(cl, 3, aspect);   // the black screen between the rooms
          std::copy(keep.begin(), keep.end(), nes->ppu.framebuffer); }
        frames(cl, 30, aspect);
        CHECK(cl.dungeonLit == 0.0f && cl.dungeonDim == 0.0f && cl.caveDim > 0.9f);          // the cellar: dim, lit by Link only
        setState(5, 0x23, 1); frames(cl, 30, aspect);
        CHECK(cl.caveDim == 0.0f && cl.dungeonLit > 0.9f);                                   // ... and back
    }

    // 26d3. Going down the stairs: the next room's id is already set while the picture shows this room, which must stay lit.
    {
        Widescreen st; fresh(st);
        nes->cart.cpuWrite(0x6A7E + 0x23, 0); nes->cart.cpuWrite(0x6A7E + 0x24, 0x80);          // (the room below is a dark one)
        setState(5, 0x23, 1); frames(st, 60, aspect);
        CHECK(st.dungeonLit > 0.99f);
        setState(0x10, 0x24, 1); frames(st, 20, aspect);
        CHECK(st.dungeonLit > 0.9f && st.dungeonDim > 0.9f);                                 // torches and shade stay through the stairs
        nes->cart.cpuWrite(0x6A7E + 0x24, 0);
    }

    // 26e. Caves are dim, with a circle of sight around Link and the flames inside lighting their surroundings.
    {
        Widescreen cv; fresh(cv);
        for (int s = 0; s < 0x14; s++) { nes->ram[kObjState + s] = 0; nes->ram[kObjType + s] = 0; }
        nes->ram[kObjX] = 0x78; nes->ram[kObjY] = 0xC0; nes->ram[kInvCandle] = 0;
        std::vector<uint32_t> img((size_t)Ppu::W * ROOM_H, 0xFFC0C0C0u);
        for (int i = 0; i < 30; i++) { img.assign(img.size(), 0xFFC0C0C0u); cv.caveLight(*nes, img.data(), Ppu::W, ROOM_H, 0, ROOM_H); }
        auto at = [&](int x, int y) { return (int)(img[(size_t)y * Ppu::W + x] & 255); };
        CHECK(at(0x78 + 8, 0xC0 - HUD_H + 8) == 0xC0);                                       // Link's own spot: as bright as the picture
        CHECK(at(10, 10) < 0xC0 * 6 / 10);                                                   // far away: dim
        nes->ram[kObjState + 3] = 1; nes->ram[kObjType + 3] = kTypeStandingFire; nes->ram[kObjX + 3] = 0x30; nes->ram[kObjY + 3] = 0x70;   // an old man's flame
        const int before = at(0x30 + 8, 0x70 - HUD_H + 8 + 18);
        img.assign(img.size(), 0xFFC0C0C0u); cv.caveLight(*nes, img.data(), Ppu::W, ROOM_H, 0, ROOM_H);
        CHECK(at(0x30 + 8, 0x70 - HUD_H + 8 + 18) > before);                                 // the flame lights its neighbourhood
        nes->ram[kObjState + 3] = 0; nes->ram[kObjType + 3] = 0;
    }

    // 26f. The candle's fire at a border: tracked beyond the game's 8-bit coordinates, the game's object kept on the screen.
    {
        Widescreen w; fresh(w);
        for (int s = 0; s < 0x14; s++) { nes->ram[kObjState + s] = 0; nes->ram[kObjType + s] = 0; }
        nes->ram[kObjState] = 1; nes->ram[kObjX] = 0x07; nes->ram[kObjY] = 0x90; nes->ram[kObjDir] = 2;            // Link at the left border facing left
        nes->ram[kObjState + kSlotBombA] = 0x21; nes->ram[kObjX + kSlotBombA] = 0xF7; nes->ram[kObjY + kSlotBombA] = 0x90;   // the game wrapped it to 247
        w.afterFrame(*nes);
        CHECK(w.fireTrack[0].overX[0] == -256 && w.fireTrack[0].tx == -9);                  // true X: 9 px left of the picture
        CHECK(nes->ram[kObjX + kSlotBombA] == 0);                                           // the object stays inside the screen
        nes->ram[kObjX + kSlotBombA] = 1;                                                    // the game walks it one pixel right ... from 0
        w.afterFrame(*nes);
        CHECK(w.fireTrack[0].tx == -8 && nes->ram[kObjX + kSlotBombA] == 0);
        // downwards: Y beyond the rows the PPU shows
        w.fireTrack[0] = {};
        nes->ram[kObjX] = 0x78; nes->ram[kObjY] = 0xD6; nes->ram[kObjDir] = 4; nes->ram[kObjX + kSlotBombA] = 0x78; nes->ram[kObjY + kSlotBombA] = 0xE6;
        w.afterFrame(*nes);
        CHECK(w.fireTrack[0].ty == 0xE6 && w.fireTrack[0].overY[0] == 0);
        nes->ram[kObjY + kSlotBombA] = 0xF6; w.afterFrame(*nes);                             // it walked 16 px on: past the last row
        CHECK(w.fireTrack[0].ty == 0xF6 && w.fireTrack[0].overY[0] == 0 && nes->ram[kObjY + kSlotBombA] == 0xE6);
        nes->ram[kObjY + kSlotBombA] = 0xE6 + 4; w.afterFrame(*nes);
        CHECK(w.fireTrack[0].ty == 0xFA && w.fireTrack[0].overY[0] == 16);     // (the game drew it at 0xEA, it is at 0xFA)
        // its sprites are drawn at the true place
        std::memset(nes->ppu.oam, 0xFF, sizeof nes->ppu.oam);
        nes->ppu.oam[30 * 4] = 0xE6 + 3; nes->ppu.oam[30 * 4 + 1] = 0x40; nes->ppu.oam[30 * 4 + 3] = 0x78;
        ViewSnapshot v; v.room = 0x23; v.mode = 5;
        w.planSpriteFixes(*nes, v, false);
        bool found = false; for (int i = 0; i < w.trueSpriteCount; i++) found = found || (w.trueSprites[i].oam == 30 && w.trueSprites[i].sy == 0xFA);
        CHECK(found && w.hideSprite[30]);
        nes->ram[kObjState + kSlotBombA] = 0; w.afterFrame(*nes); CHECK(!w.fireTrack[0].on);   // the fire burns out
        for (int s = 0; s < 0x14; s++) nes->ram[kObjState + s] = 0;
    }

    // 26g. The candle is not usable within 2.5 tiles of the border Link faces.
    {
        Widescreen w; fresh(w);
        nes->ram[kGameMode] = 5; nes->ram[kCurLevel] = 0; nes->ram[kSelectedItem] = kItemSlotCandle; nes->ram[kInvCandle] = 1;
        auto pressed = [&](int x, int y, int dir) { nes->ram[kObjX] = (uint8_t)x; nes->ram[kObjY] = (uint8_t)y; nes->ram[kObjDir] = (uint8_t)dir; nes->pad1 = BTN_B | BTN_LEFT; w.prepareFrame(*nes); return (nes->pad1 & BTN_B) != 0; };
        CHECK(!pressed(0x07, 0x90, 2) && !pressed(0x1F, 0x90, 2));                           // left: the flame stops 32 px from Link, so he needs X >= 32
        CHECK(pressed(0x20, 0x90, 2));
        CHECK(pressed(0x07, 0x90, 1));                                                       // facing away: allowed
        CHECK(!pressed(0xD1, 0x90, 1) && pressed(0xD0, 0x90, 1));                            // right: X + 32 + 16 must fit in 256
        CHECK(!pressed(0x78, 0x45 + 20, 8) && pressed(0x78, 64 + 32, 8));                    // top: 32 px below the play area's top
        CHECK(!pressed(0x78, 0xC1 + 0, 4) && pressed(0x78, 64 + 176 - 48, 4));               // bottom
        nes->ram[kSelectedItem] = 0; CHECK(pressed(0x07, 0x90, 2));                          // another item selected: untouched
        nes->ram[kSelectedItem] = kItemSlotCandle; nes->ram[kGameMode] = 7; CHECK(pressed(0x07, 0x90, 2));
        nes->ram[kGameMode] = 5; nes->ram[kInvCandle] = 0; nes->ram[kSelectedItem] = 0;
    }

    // 26h. A new start (continuing after death) puts Link's sight on him at once; the armos halo shows in daylight; sparkles are drawn.
    {
        Widescreen w; fresh(w); w.dayClock.phase = 0.25;
        setState(5, 0x23); frames(w, 6, aspect);
        const float x0 = w.lightLinkX;
        nes->ram[kObjX] = 0xD0;                                                              // (Link far away: a new spawn place)
        setState(8, 0x23); frames(w, 1, aspect);                                             // the continue screen ...
        setState(5, 0x23); nes->ram[kObjX] = 0xD0; frames(w, 2, aspect);                     // ... and play again: the sight is there at once
        CHECK(std::fabs(w.lightLinkX - x0) > 40);
        std::vector<uint32_t> img(60 * 60, 0xFF808080u);
        lighting::Params day; lighting::Source halo; halo.x = 30; halo.y = 30; halo.radius = 20; halo.power = 0.8f; halo.add = 0.3f; halo.r = 70; halo.g = 130; halo.b = 255;
        lighting::apply(img.data(), 60, 60, 0, 60, 0, 0, day, {halo});
        CHECK((img[30 * 60 + 30] & 255) > 0x80 && img[0] == 0xFF808080u);                    // full daylight: a halo is still there, only near the light
        w.dayClock.phase = 0.25; w.sparkles.clear();
        w.sparkles.push_back({100, 90, 0, w.dayClock.tick, 100, 0});
        setState(5, 0x23); frames(w, 1, aspect);
        const Canvas a = c; w.sparkles.clear(); frames(w, 1, aspect);
        CHECK(a.pixels != c.pixels);                                                         // the sparkle pixels are part of the picture
    }

    // 26i. Trails under the sprites, an arrow's head, hearts and rupees, the room item, the pond fairy's first frames, the candle.
    {
        Widescreen w; fresh(w);
        std::vector<uint8_t> savedTiles(704);
        for (int i = 0; i < 704; i++) { savedTiles[i] = nes->cart.cpuRead(0x6530 + i); nes->cart.cpuWrite(0x6530 + i, 0x24); }
        auto clear = [&] { for (int s = 0; s < 0x14; s++) { nes->ram[kObjState + s] = 0; nes->ram[kObjType + s] = 0; } w.sparkles.clear(); };
        auto gather = [&](int n) { ViewSnapshot v; v.room = 0x23; for (int i = 0; i < n; i++) w.gatherLights(*nes, v, 0, 0, 0, true); };
        clear(); nes->ram[kObjState + 0x12] = 1; nes->ram[kObjX + 0x12] = 100; nes->ram[kObjY + 0x12] = 150; nes->ram[kObjDir + 0x12] = 1;
        gather(30);
        { bool head = !w.sparkles.empty(); for (const auto& sp : w.sparkles) head = head && std::fabs(sp.x - (100 + 8 + 4)) < 5; CHECK(head); }   // an arrow flying right: its trail starts at the head
        clear(); nes->ram[kObjState + 0x12] = 1; nes->ram[kObjX + 0x12] = 100; nes->ram[kObjY + 0x12] = 150; nes->ram[kObjDir + 0x12] = 8;
        gather(30);
        { bool head = !w.sparkles.empty(); for (const auto& sp : w.sparkles) head = head && std::fabs(sp.y - (150 - HUD_H + 8 - 4)) < 5; CHECK(head); }   // ... up: the head is at the top
        for (const auto& want : {std::pair<int, int>{kItemHeart, 3}, {kItemRupee, 4}, {kItemRupee5, 5}}) {
            clear(); nes->ram[kObjState + 3] = (uint8_t)want.first; nes->ram[kObjType + 3] = kTypeItem; gather(40);
            bool ok = !w.sparkles.empty(); for (const auto& sp : w.sparkles) ok = ok && sp.kind == want.second; CHECK(ok);   // red hearts, amber rupees, blue rupees
            CHECK(!w.lightSources.empty() && w.lightSources[0].power < 0.4f);              // the glow itself is weaker (the live sparkles add their own lights)
        }
        // the room item: a triforce shard lying in the room glitters gold until it is taken
        clear(); nes->ram[kObjState + 0x13] = 0; nes->ram[kRoomItemId] = kItemTriforce; nes->ram[kObjX + 0x13] = 0x78; nes->ram[kObjY + 0x13] = 0x8D;
        nes->cart.cpuWrite(0x6BAF, 0x00); nes->cart.cpuWrite(0x6BB0, 0x67); nes->cart.cpuWrite(0x6700 + nes->ram[kRoomId], 0);
        gather(40); { bool gold = !w.sparkles.empty(); for (const auto& sp : w.sparkles) gold = gold && sp.kind == 2; CHECK(gold); }
        clear(); nes->cart.cpuWrite(0x6700 + nes->ram[kRoomId], 0x10); gather(40); CHECK(w.sparkles.empty());     // taken
        nes->cart.cpuWrite(0x6700 + nes->ram[kRoomId], 0); clear(); nes->ram[kObjState + 0x13] = 0x80; gather(40); CHECK(w.sparkles.empty());   // not activated
        {   // a room scroll (mode 7): no new glitter and no light for the (hidden) objects, whose RAM positions do not scroll; the dungeon replays frozenLights instead
            clear(); nes->ram[kObjState + 0x13] = 0; nes->ram[kRoomItemId] = kItemTriforce; nes->cart.cpuWrite(0x6700 + nes->ram[kRoomId], 0);
            nes->ram[kObjState + 3] = kItemTriforce; nes->ram[kObjType + 3] = kTypeItem;
            gather(40); CHECK(!w.sparkles.empty());
            const size_t n0 = w.sparkles.size();
            for (int i = 0; i < 5; i++) { ViewSnapshot v; v.room = 0x23; v.mode = 7; w.gatherLights(*nes, v, 0, 0, 0, false); CHECK(w.sparkles.size() <= n0); }   // (nothing new spawns, no light of a hidden object; the old sparkles stay)
            nes->ram[kObjState + 3] = 0; nes->ram[kObjType + 3] = 0;
        }
        nes->ram[kObjState + 0x13] = 0; nes->ram[kRoomItemId] = 0x3F;
        // the pond fairy's first frames on a screen: nothing, until she has settled
        clear(); nes->ram[kObjType + 3] = kTypePondFairy; nes->ram[kObjX + 3] = 0x78; nes->ram[kObjY + 3] = 0x80; w.roomPlayAge = 0;
        gather(7); CHECK(w.sparkles.empty());
        gather(40); CHECK(!w.sparkles.empty());
        // a trail runs under sprites: no trail pixel where a sprite pixel is
        {
            std::vector<uint32_t> buf((size_t)256 * 240, 0xFF202020u);
            clear(); w.sparkOx = 0; w.sparkOy = 0; w.dayClock.tick = 100;
            w.sparkles.push_back({50, 40, 0, 100, 20, 1}); w.sparkles.push_back({80, 40, 0, 100, 20, 1});
            std::memset(nes->ppu.sprCol, 0xFF, sizeof nes->ppu.sprCol); nes->ppu.sprCol[(40 + HUD_H) * Ppu::SPR_W + 50] = 0x16;     // a sprite over the first one
            w.drawSparkles(buf.data(), 256, 240, HUD_H, 0, 0, nes->ppu);
            CHECK(buf[(size_t)(40 + HUD_H) * 256 + 50] == 0xFF202020u && buf[(size_t)(40 + HUD_H) * 256 + 80] != 0xFF202020u);
            std::memset(nes->ppu.sprCol, 0xFF, sizeof nes->ppu.sprCol);
        }
        // the candle a night frame knows about
        Widescreen nw; fresh(nw); nw.dayClock.phase = 0.75;
        nes->ram[kSelectedItem] = kItemSlotCandle; nes->ram[kInvCandle] = 2;
        for (int i = 0; i < 704; i++) nes->cart.cpuWrite(0x6530 + i, savedTiles[i]);
        setState(5, 0x23); frames(nw, 3, aspect);
        CHECK(nw.candleHeld == 2);
        nes->ram[kSelectedItem] = 0; nes->ram[kInvCandle] = 0; frames(nw, 2, aspect); CHECK(nw.candleHeld == 0);
        clear();
    }

    // 26j. Heart containers (room item, dropped, in a cave), the triforce shard while it is held up, the end of a level, far-away statues.
    {
        Widescreen w; fresh(w);
        std::vector<uint8_t> savedTiles(704);
        for (int i = 0; i < 704; i++) { savedTiles[i] = nes->cart.cpuRead(0x6530 + i); nes->cart.cpuWrite(0x6530 + i, 0x24); }
        auto clear = [&] { for (int s = 0; s < 0x14; s++) { nes->ram[kObjState + s] = 0; nes->ram[kObjType + s] = 0; } w.sparkles.clear(); w.lightSources.clear(); };
        auto gather = [&](int n, int mode = 5) { ViewSnapshot v; v.room = 0x23; v.mode = mode; for (int i = 0; i < n; i++) w.gatherLights(*nes, v, 0, 0, 0, true); };
        auto allKind = [&](int k) { bool ok = !w.sparkles.empty(); for (const auto& sp : w.sparkles) ok = ok && sp.kind == k; return ok; };
        nes->cart.cpuWrite(0x6BAF, 0x00); nes->cart.cpuWrite(0x6BB0, 0x67); nes->cart.cpuWrite(0x6700 + nes->ram[kRoomId], 0);
        clear(); nes->ram[kRoomItemId] = kItemHeartContainer; nes->ram[kObjX + 0x13] = 0x78; nes->ram[kObjY + 0x13] = 0x8D;
        gather(80); CHECK(allKind(3));                                                       // a heart container lying in a room: red glitter
        clear(); nes->ram[kRoomItemId] = 0x3F;
        nes->ram[kObjState + 3] = kItemHeartContainer; nes->ram[kObjType + 3] = kTypeItem; gather(80); CHECK(allKind(3));      // ... or dropped
        clear();
        nes->ram[kCaveFlags] = 4; nes->ram[kCaveItemIds + 1] = kItemHeartContainer; nes->ram[kCaveItemIds + 0] = 0x3F; nes->ram[kCaveItemIds + 2] = 0x3F;
        for (int i = 0; i < 80; i++) { w.lightSources.clear(); w.gatherWareLights(*nes, 0, 0, true); }
        CHECK(allKind(3));                                                                   // in a cave (the old man's gift, a ware)
        // the money of a secret cave / a gift: amber rupee, blue five rupees, red heart; a taken ware ($FF) and the price rupee give nothing
        nes->ram[kCaveFlags] = 4;
        for (int id : {kItemRupee, kItemRupee5, kItemHeart}) {
            clear(); nes->ram[kCaveItemIds + 1] = id; nes->ram[kCaveItemIds + 0] = 0x3F; nes->ram[kCaveItemIds + 2] = 0x3F;
            for (int i = 0; i < 80; i++) { w.lightSources.clear(); w.gatherWareLights(*nes, 0, 0, true); }
            CHECK(allKind(id == kItemRupee ? 4 : id == kItemRupee5 ? 5 : 3));
            CHECK(!w.lightSources.empty());
        }
        clear(); nes->ram[kCaveItemIds + 1] = 0xFF;
        for (int i = 0; i < 80; i++) { w.lightSources.clear(); w.gatherWareLights(*nes, 0, 0, true); }
        CHECK(w.sparkles.empty() && w.lightSources.empty());
        nes->ram[kCaveFlags] = 8; nes->ram[kCaveItemIds + 1] = kItemRupee; clear();
        for (int i = 0; i < 80; i++) { w.lightSources.clear(); w.gatherWareLights(*nes, 0, 0, true); }
        CHECK(w.sparkles.empty());                                                           // prices only: the price rupee is not a ware
        nes->ram[kCaveFlags] = 0; w.sparkles.clear(); w.gatherWareLights(*nes, 0, 0, true); CHECK(w.sparkles.empty());
        // a shard taken: still glittering while Link holds it up and in the end-of-level mode
        clear(); nes->ram[kRoomItemId] = kItemTriforce; nes->cart.cpuWrite(0x6700 + nes->ram[kRoomId], 0x10);
        gather(40); CHECK(w.sparkles.empty());
        // (the real game: the room item's slot is $FF once taken, and Link lifts the item 16 px above him for $80 frames)
        nes->ram[kObjState + 0x13] = 0xFF; nes->ram[kItemTypeToLift] = kItemTriforce; nes->ram[kObjState] = 0x40; nes->ram[kObjX + 0x13] = 0x78; nes->ram[kObjY + 0x13] = 0x7D;
        gather(40); CHECK(allKind(2));                                                      // Link lifting the shard: gold glitter around it
        { bool near = true; for (const auto& sp : w.sparkles) near = near && std::abs(sp.x - (0x78 + 8)) < 20 && std::abs(sp.y - (0x7D - HUD_H + 8)) < 20; CHECK(near); }
        nes->ram[kItemTypeToLift] = 0; nes->ram[kObjState + 0x13] = 0; w.sparkles.clear(); gather(40); CHECK(w.sparkles.empty());   // lift over, item gone
        nes->ram[kObjState] = 0;
        nes->ram[kObjState] = 0; w.sparkles.clear(); gather(40, 0x12); CHECK(allKind(2));
        clear(); nes->ram[kRoomItemId] = 0x3F; nes->cart.cpuWrite(0x6700 + nes->ram[kRoomId], 0);
        // the torches stay lit through the end-of-level animation
        { Widescreen d; fresh(d); nes->cart.cpuWrite(0x6A7E + 0x23, 0);
          for (int i = 0; i < 704; i++) nes->cart.cpuWrite(0x6530 + i, savedTiles[i]);
          setState(5, 0x23, 1); frames(d, 60, aspect); setState(0x12, 0x23, 1); frames(d, 20, aspect);
          CHECK(d.dungeonLit > 0.9f && d.dungeonDim > 0.9f); }
        for (int i = 0; i < 704; i++) nes->cart.cpuWrite(0x6530 + i, savedTiles[i]);
        clear();
    }
}
