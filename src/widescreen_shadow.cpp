// The neighbouring screens running (shadow games, shadow.h): what lives on them (monsters, great fairies, items on the ground) is drawn into the
// margins of the widescreen view from the shadow's OAM, with the shadow's own graphics and palette.
#include "widescreen_internal.h"
#include "widescreen.h"
#include "shadow.h"
#include "zelda_ram.h"
#include "perf.h"

// curRoom: the screen Link is in (its shadow is not drawn: the game's own picture shows it), -1 in the glide. onlyCurrent: draw only that screen's shadow (over the game's picture,
// for the few frames between the end of the glide and the hand-off).
// A reflection hangs below the sprite's feet by at most its height (16 px) and only shows where it falls on water: a screen of the shadow set needs the water pass for its objects only if some
// sprite of its OAM (not Link's, not the status bar's) has water (a tile flagged water in the grid) in the 16 px column under its feet or under it.
bool Widescreen::spritesNearWater(const Nes& n, int room) const {
    const int gx0 = (room & 15) * 32, gy0 = (room >> 4) * 22;
    for (int i = 0; i < 64; i++) {
        if (i == zelda::kLinkOam0 || i == zelda::kLinkOam1) continue;
        const uint8_t* o = &n.ppu.oam[i * 4];
        const int sy = o[0] + 1;
        if (o[0] >= 0xEF || sy + 16 <= HUD_H || hudSpriteEntry(n.ppu.oam, i)) continue;
        const int ry = sy - HUD_H, x0 = o[3] >> 3, x1 = std::min(31, (o[3] + 15) >> 3), y0 = std::max(0, (ry + 8) >> 3), y1 = std::min(21, (ry + 16 + 16 + 7) >> 3);
        for (int gy = y0; gy <= y1; gy++) for (int gx = x0; gx <= x1; gx++) if (wwKind[(size_t)(gy0 + gy) * GW + gx0 + gx] & 3) return true;
    }
    return false;
}

void Widescreen::drawShadowSprites(Canvas& out, int camX, int camY, int topBand, int curRoom, int slot, bool onlyCurrent) {
    perf::Scope perfScope(perf::kShadowDraw);
    if (!shadows || !shadows->enabled || !enabled) return;
    const FlashEffect noFlash;
    for (ShadowGame* g : shadows->games()) {
        if (!g->visible()) continue;
        if (onlyCurrent ? g != shadows->bridgeGame() : g->room() == curRoom) continue;
        if (fog && !discovered(slot, g->room())) continue;                       // (a screen under the fog shows nothing living)
        const int rx = (g->room() & 15) * ROOM_W - camX, ry = topBand + (g->room() >> 4) * ROOM_H - camY;
        if (rx + ROOM_W + 16 <= 0 || rx - 16 >= out.w || ry + ROOM_H + 16 <= topBand || ry - 16 >= out.h) continue;     // (not on the canvas)
        Nes& n = g->nes();
        for (int i = 63; i >= 0; i--) {                                          // (the lower the OAM index, the further in front)
            if (i == zelda::kLinkOam0 || i == zelda::kLinkOam1) continue;       // the stand-in Link is not drawn
            const uint8_t* o = &n.ppu.oam[i * 4];
            const int sy = o[0] + 1;
            if (o[0] >= 0xEF || sy + 16 <= HUD_H || hudSpriteEntry(n.ppu.oam, i)) continue;   // (parked, or the status bar's icons and the map marker, which pokes out below the bar)
            drawSpriteAt(n, out, o[1], o[2], o[3], sy, rx, ry, topBand, noFlash, 0, 0);
        }
        // The monsters reflect in the water of their screen (the bridging shadow too: its reflection must not blink while the game's picture has no monsters) (pass B on that screen, objects only: the scenery's water was done with the terrain).
        if (!waterDone || !waterReflections || terrainW != out.w || terrainH != out.h || terrainCamX != camX || terrainCamY != camY) continue;
        if (!spritesNearWater(n, g->room())) continue;                          // (the pass costs ~0.3 ms a screen: only where a reflection could fall on water)
        Nes* sN = fxNes; const Ppu* sP = lightPpu; const int sOx = fxOx, sOy = fxOy, sRoom = fxRoom;
        fxNes = &n; lightPpu = &n.ppu; fxOx = (g->room() & 15) * ROOM_W; fxOy = (g->room() >> 4) * ROOM_H; fxRoom = g->room(); fxIsShadow = true;
        roomWater(out, camX, camY, true, false); perf::shadowPasses++;
        fxNes = sN; lightPpu = sP; fxOx = sOx; fxOy = sOy; fxRoom = sRoom; fxIsShadow = false;
    }
}

// What glows on the neighbouring screens: dropped items and fairies glow and glitter like the ones of Link's own screen, the guard fires and the flames of cave people
// glow. Light-space = the overworld's world pixels (the same as gatherLights with the screen's origin).
void Widescreen::gatherShadowLights(int curRoom, int slot, bool sparkle, bool withBridge) {
    perf::Scope perfScope(perf::kShadowLight);
    if (!shadows || !shadows->enabled || !enabled) return;
    const unsigned t = dayClock.tick;
    for (ShadowGame* g : shadows->games()) {
        if (!g->visible() || (g->room() == curRoom && !(withBridge && g == shadows->bridgeGame()))) continue;     // (the bridging shadow of the screen Link is in lights it while the game has no objects of its own)
        if (fog && !discovered(slot, g->room())) continue;
        const int ox = (g->room() & 15) * ROOM_W, oy = (g->room() >> 4) * ROOM_H;
        Nes& n = g->nes();
        // The room item (slot $13: a heart container, a triforce shard) glows and glitters too.
        if (!(n.ram[zelda::kObjState + 0x13] & 0x80) && !roomItemTaken(n)) {
            const int id = n.ram[zelda::kRoomItemId];
            const float x = (float)(ox + n.ram[zelda::kObjX + 0x13] + 8), y = (float)(oy + n.ram[zelda::kObjY + 0x13] - HUD_H + 8);
            if (id == zelda::kItemHeartContainer) { lightSources.push_back({x, y, 32, 0.4f, 0.0f, 255, 90, 100}); if (sparkle) spawnSparkles(x, y, 3); }
            else if (id == zelda::kItemTriforce) { lightSources.push_back({x, y, 44, 0.6f, 0.0f, 255, 215, 90}); if (sparkle) spawnSparkles(x, y, 2); }
        }
        // What Link had out when he left the screen goes on in its shadow, with the light and the trail it had: the sword's shot, the boomerang and arrows (a trail of white
        // sparkles), bombs (the flash) and fire (the candle). The same numbers as gatherLights.
        {
            auto at = [&](int s, float& x, float& y) { x = (float)(ox + n.ram[zelda::kObjX + s] + 8); y = (float)(oy + n.ram[zelda::kObjY + s] - HUD_H + 8); };
            float x, y;
            if (n.ram[zelda::kObjState + zelda::kSlotShot] >= 0x10) { at(zelda::kSlotShot, x, y); lightSources.push_back({x, y, 52, 0.95f, 0.0f, 190, 225, 255}); }
            for (int sl : {0x0F, (int)zelda::kSlotLastWeapon})
                if (n.ram[zelda::kObjState + sl]) {
                    at(sl, x, y);
                    if (sl != 0x0F) { const int d = n.ram[zelda::kObjDir + sl]; x += d == 1 ? 4.0f : d == 2 ? -4.0f : 0.0f; y += d == 4 ? 4.0f : d == 8 ? -4.0f : 0.0f; if (d == 1 || d == 2) y += 4.0f; }
                    if (sparkle) spawnSparkles(x, y, 1);
                }
            for (int sl : {zelda::kSlotBombA, zelda::kSlotBombB}) {
                const int st = n.ram[zelda::kObjState + sl];
                if ((st & 0xF0) == 0x20) { at(sl, x, y); lightSources.push_back({x, y, 64, 1.0f * lighting::flicker(t, sl), 0.0f, 255, 150, 60}); }
                else if (st == zelda::kStateBombFlash) { at(sl, x, y); lightSources.push_back({x, y, 120, 1.4f, 0.0f, 255, 225, 150}); }
            }
        }
        for (int s = 1; s <= 12; s++) {
            const int ty = n.ram[zelda::kObjType + s], st = n.ram[zelda::kObjState + s];
            if (!ty) continue;
            const float x = (float)(ox + n.ram[zelda::kObjX + s] + 8), y = (float)(oy + n.ram[zelda::kObjY + s] - HUD_H + 8);
            if (ty == zelda::kTypeGuardFire || ty == zelda::kTypeStandingFire) lightSources.push_back({x, y, 56, 0.9f * lighting::flicker(t, s), 0.0f, 255, 150, 60});
            else if (ty == zelda::kTypePondFairy) { lightSources.push_back({x, y, 40, 0.5f, 0.0f, 255, 190, 230}); if (sparkle) spawnSparkles(x, y, 0); }
            else if (ty == zelda::kTypeArmos) {                                                          // an armos that woke glows blue (the secret one, still in its place: red)
                bool secret = false;
                for (int k = 0; k < 7; k++) secret = secret || (zelda::kSecretArmosRoom[k] == g->room() && n.ram[zelda::kObjX + s] == zelda::kSecretArmosX[k] && n.ram[zelda::kObjY + s] == zelda::kSecretArmosY);
                const float pulse = 0.85f + 0.15f * std::sin(t * 0.08f + s);
                lightSources.push_back(secret ? lighting::Source{x, y, 34, 0.9f * pulse, 0.5f, 255, 60, 40} : lighting::Source{x, y, 32, 0.8f * pulse, 0.45f, 70, 130, 255});
            }
            else if (ty == zelda::kTypeItem && st) itemGlow(st, x, y, s, sparkle);
        }
    }
}
