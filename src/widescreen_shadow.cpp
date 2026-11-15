// The neighbouring screens running (shadow games, shadow.h): what lives on them (monsters, great fairies, items on the ground) is drawn into the
// margins of the widescreen view from the shadow's OAM, with the shadow's own graphics and palette.
#include "widescreen_internal.h"
#include "widescreen.h"
#include "shadow.h"
#include "zelda_ram.h"

// curRoom: the screen Link is in (its shadow is not drawn: the game's own picture shows it), -1 in the glide. onlyCurrent: draw only that screen's shadow (over the game's picture,
// for the few frames between the end of the glide and the hand-off).
void Widescreen::drawShadowSprites(Canvas& out, int camX, int camY, int topBand, int curRoom, int slot, bool onlyCurrent) {
    if (!shadows || !shadows->enabled || !enabled) return;
    const FlashEffect noFlash;
    for (ShadowGame* g : shadows->games()) {
        if (!g->ready() || (g->room() == curRoom) == !onlyCurrent) continue;
        if (fog && !discovered(slot, g->room())) continue;                       // (a screen under the fog shows nothing living)
        const int rx = (g->room() & 15) * ROOM_W - camX, ry = topBand + (g->room() >> 4) * ROOM_H - camY;
        if (rx + ROOM_W + 16 <= 0 || rx - 16 >= out.w || ry + ROOM_H + 16 <= topBand || ry - 16 >= out.h) continue;     // (not on the canvas)
        Nes& n = g->nes();
        for (int i = 63; i >= 0; i--) {                                          // (the lower the OAM index, the further in front)
            if (i == zelda::kLinkOam0 || i == zelda::kLinkOam1) continue;       // the stand-in Link is not drawn
            const uint8_t* o = &n.ppu.oam[i * 4];
            const int sy = o[0] + 1;
            if (o[0] >= 0xEF || sy + 16 <= HUD_H) continue;                      // (parked, or the status bar's icons)
            drawSpriteAt(n, out, o[1], o[2], o[3], sy, rx, ry, topBand, noFlash, 0, 0);
        }
        if (onlyCurrent) continue;
        // The monsters reflect in the water of their screen (pass B on that screen, objects only: the scenery's water was done with the terrain).
        if (!waterDone || !waterReflections || terrainW != out.w || terrainH != out.h || terrainCamX != camX || terrainCamY != camY) continue;
        bool monsters = false, water = false;
        for (int s = 1; s <= 12 && !monsters; s++) monsters = n.ram[zelda::kObjType + s] != 0;
        const int gx0 = (g->room() & 15) * 32, gy0 = (g->room() >> 4) * 22;
        for (int gy = gy0; gy < gy0 + 22 && monsters && !water; gy++) for (int gx = gx0; gx < gx0 + 32; gx++) if (wwKind[(size_t)gy * GW + gx] & 3) { water = true; break; }
        if (!monsters || !water) continue;
        Nes* sN = fxNes; const Ppu* sP = lightPpu; const int sOx = fxOx, sOy = fxOy, sRoom = fxRoom;
        fxNes = &n; lightPpu = &n.ppu; fxOx = (g->room() & 15) * ROOM_W; fxOy = (g->room() >> 4) * ROOM_H; fxRoom = g->room(); fxIsShadow = true;
        roomWater(out, camX, camY, true, false);
        fxNes = sN; lightPpu = sP; fxOx = sOx; fxOy = sOy; fxRoom = sRoom; fxIsShadow = false;
    }
}
