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
    if (room < 0 || room > 0x7F || wwKind.size() < (size_t)GW * GH) return false;           // (no water grid yet)
    const int gx0 = (room & 15) * 32, gy0 = (room >> 4) * 22;
    for (int i = 0; i < kOamEntries; i++) {
        if (i == zelda::kLinkOam0 || i == zelda::kLinkOam1) continue;
        const uint8_t* o = &n.ppu.oam[i * 4];
        const int sy = o[0] + 1;
        if (o[0] >= 0xEF || sy + 16 <= HUD_H || hudSpriteEntry(n.ppu.oam, i)) continue;
        const int ry = sy - HUD_H, x0 = o[3] >> 3, x1 = std::min(31, (o[3] + 15) >> 3), y0 = std::max(0, (ry + 8) >> 3), y1 = std::min(21, (ry + 16 + 16 + 7) >> 3);
        for (int gy = y0; gy <= y1; gy++) for (int gx = x0; gx <= x1; gx++) if (wwKind[(size_t)(gy0 + gy) * GW + gx0 + gx] & 3) return true;
    }
    return false;
}

void Widescreen::drawShadowSprites(Canvas& out, int camX, int camY, int band, int curRoom, int slot, bool onlyCurrent) {
    perf::Scope perfScope(perf::kShadowDraw);
    if (!shadows || !shadows->enabled || !enabled) return;
    const FlashEffect noFlash;
    for (ShadowGame* g : shadows->games()) {
        if (!g->visible()) continue;
        if (onlyCurrent ? g != shadows->bridgeGame() : g->room() == curRoom) continue;
        if (fog && !discovered(slot, g->room())) continue;                       // (a screen under the fog shows nothing living)
        const int rx = (g->room() & 15) * ROOM_W - camX, ry = band + (g->room() >> 4) * ROOM_H - camY;
        if (rx + ROOM_W + 16 <= 0 || rx - 16 >= out.w || ry + ROOM_H + 16 <= band || ry - 16 >= out.h) continue;     // (not on the canvas)
        Nes& n = g->nes();
        for (int i = 63; i >= 0; i--) {                                          // (the lower the OAM index, the further in front)
            if (i == zelda::kLinkOam0 || i == zelda::kLinkOam1) continue;       // the stand-in Link is not drawn
            const uint8_t* o = &n.ppu.oam[i * 4];
            const int sy = o[0] + 1;
            if (o[0] >= 0xEF || sy + 16 <= HUD_H || hudSpriteEntry(n.ppu.oam, i)) continue;   // (parked, or the status bar's icons and the map marker, which pokes out below the bar)
            drawSpriteAt(n, out, o[1], o[2], o[3], sy, rx, ry, band, noFlash, 0, 0);
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
        if (!(n.ram[zelda::kObjState + zelda::kSlotRoomItem] & 0x80) && !roomItemTaken(n)) {
            const int id = n.ram[zelda::kRoomItemId];
            const float x = (float)(ox + n.ram[zelda::kObjX + zelda::kSlotRoomItem] + 8), y = (float)(oy + n.ram[zelda::kObjY + zelda::kSlotRoomItem] - HUD_H + 8);
            if (id == zelda::kItemHeartContainer || id == zelda::kItemTriforce) lootGlow(id, x, y, sparkle);
        }
        // What Link had out when he left the screen goes on in its shadow, with the light and the trail it had: the sword's shot, the boomerang and arrows (a trail of white
        // sparkles), bombs (the flash) and fire (the candle). The same numbers as gatherLights.
        gatherWeaponLights(n.ram, ox, oy, sparkle, false);
        for (int s = 1; s <= 12; s++) {
            const int ty = n.ram[zelda::kObjType + s], st = n.ram[zelda::kObjState + s];
            if (!ty) continue;
            const float x = (float)(ox + n.ram[zelda::kObjX + s] + 8), y = (float)(oy + n.ram[zelda::kObjY + s] - HUD_H + 8);
            if (ty == zelda::kTypeGuardFire || ty == zelda::kTypeStandingFire) lightSources.push_back({x, y, 56, 0.9f * lighting::flicker(t, s), 0.0f, 255, 150, 60});
            else if (ty == zelda::kTypePondFairy) { lightSources.push_back({x, y, 40, 0.5f, 0.0f, 255, 190, 230}); if (sparkle) spawnSparkles(x, y, 0); }
            else if (ty == zelda::kTypeArmos) {                                                          // an armos that woke glows blue (the secret one, still in its place: red)
                bool secret = false;
                for (int k = 0; k < 7; k++) secret = secret || (zelda::kSecretArmosRoom[k] == g->room() && n.ram[zelda::kObjX + s] == zelda::kSecretArmosX[k] && n.ram[zelda::kObjY + s] == zelda::kSecretArmosY);
                addArmosLight(x, y, secret, (float)s);
            }
            else if (ty == zelda::kTypeItem && st) itemGlow(st, x, y, s, sparkle);
        }
    }
}

// N: which drawing of the neighbour screens goes with this picture (the comments say why).
Widescreen::ShadowPlan Widescreen::planShadows(const Nes& nes, const ViewSnapshot& view, Layout layout, Kind kind) {
    ShadowPlan p;
    p.scene = layout == Layout::World && ((kind == Kind::Overworld && view.level == 0) || (kind == Kind::Transition && (view.mode == kModeStairs || (view.mode == kModeCellar && !cellarBlackSeen && view.level == 0) || (view.mode == kModeLeaveCave && view.level == 0))));
    if (kind == Kind::Overworld && view.mode == kModePlay && view.level == 0) { const int pl = paletteLuma(nes.ppu.palette); if (pl > 0) owRefLuma = pl; }
    // (stairs, a cave, a cellar: the game fades its picture by palette; the shadows' monsters on it fade the same way)
    p.fade = kind == Kind::Transition && owRefLuma > 0 ? std::clamp(paletteLuma(nes.ppu.palette) * 256 / owRefLuma, 0, 256) : 256;
    // (the stairs' RoomId is already the cellar's: while Link walks down the game's own monsters are on the picture, once it fades into the cellar the screen's shadow is)
    // The game's own picture of the walk into a cave, stairs or a dungeon has no monsters (or they vanish with the fade): the shadow of the screen stands in from the first picture of the walk.
    p.takeover = p.scene && shadows && view.mode == kModeStairs && shadows->hasVisible(shadows->baseRoom());
    p.cur = p.takeover ? -1 : (view.mode == kModeStairs && shadows ? shadows->baseRoom() : (view.mode == kModeCellar ? -1 : view.room));
    // The screen's own shadow is drawn over the picture while the game's picture has no monsters: leaving a screen (mode 6), walking into a screen or out of a cave, the first pictures of play.
    // (and in play whenever the game's own picture has no monster sprites at all: the frame in which the game changes mode, or the first ones of a screen, are drawn without them)
    bool gameDrawsMonsters = false;
    for (int i = kFirstObjectOam; i < kOamEntries && !gameDrawsMonsters; i++) { const uint8_t* o = &nes.ppu.oam[i * 4]; gameDrawsMonsters = o[0] < 0xEF && o[0] + 1 + 16 > HUD_H && !hudSpriteEntry(nes.ppu.oam, i); }   // (objects use OAM entries 24-63: SpriteOffsets, Z_01.asm 5007)
    p.bridge = shadows && shadows->bridgeGame() && p.scene && (view.mode == kModeEnter || view.mode == kModeLeaveCave || view.mode == kModeLeave || (view.mode == kModePlay && (shadows->bridgeShowsInPlay() || !gameDrawsMonsters)));
    return p;
}

// --trace: what the neighbour screens are doing (printed when it changes).
void Widescreen::traceShadows(const ViewSnapshot& view, bool shadowScene) const {
    if (!trace || !shadows || !shadows->enabled) return;
    static int lastScene = -1, lastBridge = -1, lastVisible = -1, traceMode = -1;
    int vis = 0; for (ShadowGame* g : shadows->games()) vis += g->visible();
    const int br = shadows->bridgeGame() ? 1 + (shadows->bridgeShowsInPlay() ? 1 : 0) : 0, sc = shadowScene ? 1 : 0;
    if (sc == lastScene && br == lastBridge && vis == lastVisible && (int)view.mode == traceMode) return;
    std::printf("[trace] shadows: view mode=$%02X scene=%d bridge=%d visible=%d handoff=%d\n", view.mode, sc, br, vis, shadows->handOffPending() ? 1 : 0);
    lastScene = sc; lastBridge = br; lastVisible = vis; traceMode = view.mode;
}

// The screens on the canvas get a shadow (N): more of them the more zoomed out.
void Widescreen::publishWantedScreens(const ViewSnapshot& view, int camX, int camY, int W, int canvasH) {
    if (!shadows) return;
    uint8_t want[128] = {};
    const int c0 = std::max(0, camX / ROOM_W - (camX < 0 ? 1 : 0)), c1 = std::min(15, (camX + W - 1) / ROOM_W), r0 = std::max(0, camY / ROOM_H - (camY < 0 ? 1 : 0)), r1 = std::min(7, (camY + (canvasH - topBand) - 1) / ROOM_H);
    for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) if (!fog || discovered(view.slot, r * 16 + c)) want[r * 16 + c] = 1;   // (a screen under the fog shows nothing living: not made)
    shadows->setWanted(want);
}

// A bomb that goes off on a screen next to Link's flashes there too: the shadow's bomb is in its flash state and the game's grayscale bit (cleared at timer $12 and $0D, set at
// $16 and $11, UpdateBombFlashEffect, Z_01.asm 4086) is on while the timer is in $13-$16 and $0E-$11.
void Widescreen::shadowBombFlash(FlashEffect& flash, const ViewSnapshot& view, int slot) const {
    if (flash.on || !shadows || !shadows->enabled) return;
    for (const ShadowGame* g : shadows->games()) {
        if (!g->visible() || g->room() == view.room || (fog && !discovered(slot, g->room()))) continue;
        const uint8_t* r = g->nes().ram;
        for (int bomb : {kSlotBombA, kSlotBombB}) {
            const int tm = r[kObjTimer + bomb];
            if (r[kObjState + bomb] != kStateBombFlash || !((tm >= 0x13 && tm <= 0x16) || (tm >= 0x0E && tm <= 0x11))) continue;
            flash.on = true; flash.cx = (g->room() & 15) * ROOM_W + r[kObjX + bomb] + 8; flash.cy = (g->room() >> 4) * ROOM_H + r[kObjY + bomb] - HUD_H + 8;
        }
        if (flash.on) break;
    }
}
