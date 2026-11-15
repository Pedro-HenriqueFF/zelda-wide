// Widescreen: day/night lighting of the overworld, the lights of fire, shots and other glowing things, torches in dungeon
// rooms, the dim light of caves, and the candle fire's true place.
#include "widescreen.h"
#include "widescreen_internal.h"
#include "perf.h"
#include "zelda_ram.h"
#include <algorithm>
#include <cmath>

using namespace zelda;

namespace {
constexpr float kDungeonLitAmbient = 0.55f, kDungeonDarkAmbient = 0.18f, kCaveAmbient = 0.50f;
constexpr int kTorchSpots[4][2] = {{16, 16}, {240, 16}, {16, 160}, {240, 160}};   // the wall corners of a dungeon room (room pixels)

struct Spot { int x, y, i, room; };                                                // a torch on the picture (room pixels); room 0 = the room at rest, 1 = the one sliding in

int wrapTo(int v, int lo, int period) { return ((v - lo) % period + period) % period + lo; }

void ease(float& value, float target, float step) { value += std::clamp(target - value, -step, step); }

// Where the torches are now. At rest: the room's corners. While the game slides to the next room (mode 7) the picture has moved by (dx, dy)
// since the room was at rest; the next room follows one room length behind (256 px sideways, 176 px up or down).
void torchSpots(int dx, int dy, bool scrolling, std::vector<Spot>& out) {
    const bool two = scrolling && (dx != 0 || dy != 0);
    for (int room = 0; room < (two ? 2 : 1); room++) {
        const int ox = room == 0 ? -dx : (dx > 0 ? 256 - dx : dx < 0 ? -256 - dx : 0);
        const int oy = room == 0 ? -dy : (dy > 0 ? 176 - dy : dy < 0 ? -176 - dy : 0);
        for (int i = 0; i < 4; i++) {
            const int px = kTorchSpots[i][0] + ox, py = kTorchSpots[i][1] + oy;
            if (px >= -12 && px <= 268 && py >= -12 && py <= 188) out.push_back({px, py, i, room});
        }
    }
}

void putPx(Canvas& c, int x, int y, uint32_t col) {
    if (x >= 0 && y >= 0 && x < c.w && y < c.h) c.pixels[(size_t)y * c.w + x] = col;
}
void putBuf(uint32_t* pix, int w, int h, int x, int y, uint32_t col) {
    if (x >= 0 && y >= 0 && x < w && y < h) pix[(size_t)y * w + x] = col;
}

bool litPatch(const Canvas& c, int x, int y) {                                   // any colour in the 3x3 patch: the room is revealed there
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        const int xx = x + dx, yy = y + dy;
        if (xx >= 0 && xx < c.w && yy >= 0 && yy < c.h && (c.pixels[(size_t)yy * c.w + xx] & 0x00FFFFFF)) return true;
    }
    return false;
}

// The torch's picture in its own upright space: (lx, ly) with ly < 0 towards the flame. 0 = nothing.
uint32_t torchPixel(int lx, int ly, float lit, unsigned tick, int salt) {
    if (ly >= 0 && ly <= 6 && std::abs(lx) <= 1) return lx < 0 ? 0xFF4A301A : 0xFF6E4A28;                     // the handle
    if (ly >= -2 && ly <= -1 && std::abs(lx) <= 2) return 0xFF8A6A48;                                       // the cup
    if (lit < 0.12f) return (ly == -3 && std::abs(lx) <= 1) ? 0xFF3A2414 : 0;                                // unlit: a dark stub
    const int tall = std::max(2, (int)std::lround((6 + (int)((tick / 3 + salt * 2) % 2)) * std::min(1.0f, lit * 1.4f)));
    const int i = -3 - ly;                                                                                     // 0 at the cup upwards
    if (i < 0 || i >= tall) return 0;
    const int sway = i >= 4 ? (int)((tick / 5 + salt) % 3) - 1 : 0;
    const int half = i < 2 ? 2 : i < 4 ? 1 : 0;
    if (std::abs(lx - sway) > half) return 0;
    return i < 2 ? 0xFFE8601C : i < 4 ? 0xFFF8A828 : 0xFFF8E060;
}

// The torch drawn along the room's corner line: its flame end points away from the floor, towards the corner of the room.
void drawTorch(Canvas& c, int cx, int cy, int corner, float lit, float shade, unsigned tick) {
    const float ang = corner == 0 ? -0.7853982f : corner == 1 ? 0.7853982f : corner == 2 ? -2.3561945f : 2.3561945f;    // the flame points out to the room's corner
    const float cs = std::cos(ang), sn = std::sin(ang);
    for (int dy = -12; dy <= 12; dy++)
        for (int dx = -12; dx <= 12; dx++) {
            const int lx = (int)std::lround(dx * cs + dy * sn), ly = (int)std::lround(-dx * sn + dy * cs);
            uint32_t col = torchPixel(lx, ly, lit, tick, corner);
            if (!col) continue;
            if (col != 0xFFE8601C && col != 0xFFF8A828 && col != 0xFFF8E060)                                      // (the flame shines; the rest is in the room's shade)
                col = 0xFF000000u | (uint32_t)((col >> 16 & 255) * shade) << 16 | (uint32_t)((col >> 8 & 255) * shade) << 8 | (uint32_t)((col & 255) * shade);
            putPx(c, cx + dx, cy + dy, col);
        }
}
}  // namespace

void Widescreen::gatherLights(Nes& nes, const ViewSnapshot& v, int ox, int oy, int slot, bool overworld, bool objects) {
    lightSources.clear();
    sparkOx = ox; sparkOy = oy; roomAge++; if (objects) roomPlayAge++;
    const unsigned t = dayClock.tick;
    auto at = [&](int s, float& x, float& y) { x = (float)(ox + nes.ram[kObjX + s] + 8); y = (float)(oy + nes.ram[kObjY + s] - HUD_H + 8); };
    auto add = [&](float x, float y, float rad, float pw, int r, int g, int b) { lightSources.push_back({x, y, rad, pw, 0.0f, (uint8_t)r, (uint8_t)g, (uint8_t)b}); };
    float x, y;
    const bool scrollNow = v.mode == kModeScroll || (!overworld && (v.mode == kModeStairs || staleObjects));   // a room scroll / the stairs, until the next room plays: the game's objects are hidden
    if (scrollNow) objects = false;                                         // (their RAM positions are the old room's); the dungeon replays the last frame of play instead (frozenLights, sparkles)
    if (objects) sparkSpots.clear();
    candleIdx = -1;
    if (objects) {
    if (nes.ram[kObjState + kSlotShot] >= 0x10) { at(kSlotShot, x, y); add(x, y, 52, 0.95f, 190, 225, 255); }          // sword shot, its splash, magic
    for (int sl : {0x0F, (int)kSlotLastWeapon})                                                                 // the boomerang and arrows leave a trail of white sparkles
        if (nes.ram[kObjState + sl]) {
            at(sl, x, y);
            if (sl != 0x0F) { const int d = nes.ram[kObjDir + sl]; x += d == 1 ? 4.0f : d == 2 ? -4.0f : 0.0f; y += d == 4 ? 4.0f : d == 8 ? -4.0f : 0.0f; if (d == 1 || d == 2) y += 4.0f; }   // an arrow's trail starts at its head
            spawnSparkles(x, y, 1);
        }
    for (int s = 0; s < 2; s++) {
        const int sl = kSlotBombA + s, st = nes.ram[kObjState + sl];
        if ((st & 0xF0) == 0x20) {                                                                              // fire (candle, burning tree...)
            if (fireTrack[s].on) { x = (float)(ox + fireTrack[s].tx + 8); y = (float)(oy + fireTrack[s].ty - HUD_H + 8); }
            else at(sl, x, y);
            add(x, y, 64, 1.0f * lighting::flicker(t, sl), 255, 150, 60);
        } else if (st == kStateBombFlash) { at(sl, x, y); add(x, y, 120, 1.4f, 255, 225, 150); }               // the bomb's flash
    }
    for (int s = 1; s <= 12; s++) {
        const int st = nes.ram[kObjState + s], ty = nes.ram[kObjType + s];
        at(s, x, y);
        // (an object lives while its ObjType is set: ObjState is 0 for flames, armos and the fairy; for items it is the item's id)
        if (ty == kTypeGuardFire || ty == kTypeStandingFire) add(x, y, 56, 0.9f * lighting::flicker(t, s), 255, 150, 60);   // Zelda's guards, the flames of cave people
        else if (ty == kTypePondFairy) { if (roomPlayAge >= 8) { add(x, y, 40, 0.5f, 255, 190, 230); spawnSparkles(x, y, 0); } }   // (her first frames of play on a screen put her somewhere else)
        else if (ty == kTypeArmos) {                                                                            // every armos glows blue; the secret one (still in its place) red
            bool secret = false;
            const uint8_t* sec = overworld ? roomSecrets(nes, v.room) : nullptr;
            for (int k = 0; k < 7 && sec; k++)
                if (kSecretArmosRoom[k] == v.room && nes.ram[kObjX + s] == kSecretArmosX[k] && nes.ram[kObjY + s] == kSecretArmosY &&
                    sec[(kSecretArmosX[k] >> 3) * OverworldMap::TILE_ROWS + ((kSecretArmosY - HUD_H) >> 3)] == SecretArmos) secret = true;
            const float pulse = 0.85f + 0.15f * std::sin(t * 0.08f + s);
            lighting::Source src = secret ? lighting::Source{x, y, 34, 0.9f * pulse, 0.5f, 255, 60, 40} : lighting::Source{x, y, 32, 0.8f * pulse, 0.45f, 70, 130, 255};
            lightSources.push_back(src);                                                                     // (a faint halo even in daylight)
        }
        if (!st) continue;
        if (ty == kTypeFireballA || ty == kTypeFireballB) add(x, y, 44, 0.95f * lighting::flicker(t, s), 255, 140, 50);   // Zora and other fireballs
        else if (ty == kTypeMonsterSwordShot) add(x, y, 40, 0.8f, 190, 225, 255);
        else if (ty == kTypeMagicA || ty == kTypeMagicB) add(x, y, 44, 0.9f, 150, 150, 255);
        else if (ty == kTypeItem && st == kItemFairy) { add(x, y, 40, 0.5f, 255, 190, 230); spawnSparkles(x, y, 0); }     // a fairy dropped by a monster
        else if (ty == kTypeItem && st == kItemTriforce) { add(x, y, 44, 0.6f, 255, 215, 90); spawnSparkles(x, y, 2); }    // a shard of the triforce: gold glitter
        else if (ty == kTypeItem && st == kItemHeartContainer) { add(x, y, 32, 0.4f, 255, 90, 100); spawnSparkles(x, y, 3); }
        else if (ty == kTypeItem && st == kItemHeart) { add(x, y, 20, 0.28f, 255, 90, 100); spawnSparkles(x, y, 3); }          // a heart: red glitter
        else if (ty == kTypeItem && st == kItemRupee) { add(x, y, 20, 0.28f, 255, 190, 60); spawnSparkles(x, y, 4); }          // a rupee: amber
        else if (ty == kTypeItem && st == kItemRupee5) { add(x, y, 20, 0.28f, 90, 150, 255); spawnSparkles(x, y, 5); }         // five rupees: blue
        else if (ty == kTypeItem) add(x, y, 22, 0.55f * (0.8f + 0.2f * std::sin(t * 0.15f + s)), 255, 235, 170);           // a sparkle on dropped items
    }
    // A room item lies in slot $13 until it is taken (ObjState $FF); then Link lifts it for $80 frames: the game puts it 16 px above him in
    // slot $13 again (ItemTypeToLift != 0) and the room item's own flag is already set.
    const int lifted = nes.ram[kItemTypeToLift];
    const bool roomItemShown = !(nes.ram[kObjState + 0x13] & 0x80) && (!roomItemTaken(nes) || v.mode == kModeEndLevel);
    const int shownId = scrollNow ? -1 : lifted ? lifted : roomItemShown ? nes.ram[kRoomItemId] : -1;
    if (shownId == kItemHeartContainer) { at(0x13, x, y); add(x, y, 32, 0.4f, 255, 90, 100); spawnSparkles(x, y, 3); }   // a heart container
    if (shownId == kItemTriforce) {                                       // a shard of the triforce (lying in the room or lifted by Link): gold glitter
        at(0x13, x, y); add(x, y, 44, 0.6f, 255, 215, 90); spawnSparkles(x, y, 2);
    }
    if (!overworld && nes.ram[kSelectedItem] == kItemSlotCandle && nes.ram[kInvCandle]) {                                   // Link carries the candle he holds: blue = a cold light, red = warm
        at(0, x, y); candleIdx = (int)lightSources.size();
        if (nes.ram[kInvCandle] == 1) add(x, y, 44, 0.6f * lighting::flicker(t, 9), 110, 170, 255);
        else                          add(x, y, 44, 0.6f * lighting::flicker(t, 9), 255, 190, 110);
    }
    baseLightN = (int)lightSources.size();
    for (const Sparkle& sp : sparkles) {
        const int age = (int)(t - sp.born);
        if (age >= sp.life) continue;
        const float k = 1.0f - (float)age / sp.life;
        if (sp.kind == 1) continue;                                          // (the white trails do not light anything)
        if (sp.kind == 3) add(sp.x - (float)lightShiftX, sp.y + sp.vy * age - (float)lightShiftY, 9, 0.5f * k, 255, 90, 100);
        else if (sp.kind == 4) add(sp.x - (float)lightShiftX, sp.y + sp.vy * age - (float)lightShiftY, 9, 0.5f * k, 255, 190, 60);
        else if (sp.kind == 5) add(sp.x - (float)lightShiftX, sp.y + sp.vy * age - (float)lightShiftY, 9, 0.5f * k, 90, 150, 255);
        else if (sp.kind == 2) add(sp.x - (float)lightShiftX, sp.y + sp.vy * age - (float)lightShiftY, 10, 0.7f * k, 255, 215, 90);
        else add(sp.x - (float)lightShiftX, sp.y + sp.vy * age - (float)lightShiftY, 10, 0.7f * k, 255, 150, 230);
    }
    }
    if (!overworld) return;
    gatherStaticLights(nes, v.room, slot, true, ox, oy);
}

bool Widescreen::roomItemTaken(Nes& nes) {                                  // the dungeon/overworld room flag of the room item ($10), via LevelInfo_WorldFlagsAddr
    const int ptr = nes.cart.cpuRead(kLevelWorldFlagsAddr) | nes.cart.cpuRead(kLevelWorldFlagsAddr + 1) << 8;
    if (ptr < 0x6000) return false;
    return (nes.cart.cpuRead((uint16_t)(ptr + nes.ram[kRoomId])) & 0x10) != 0;
}

// Armos statues are background tiles until they are touched: the screen itself (its live tiles while Link is on it, else the decoded ones)
// and the explored ones around it. (ox, oy) = where that screen's top-left is in the lights' space.
void Widescreen::gatherStaticLights(Nes& nes, int room, int slot, bool live, int ox, int oy) {
    std::vector<uint8_t> tiles(OverworldMap::TILES);
    const uint8_t* own = live ? nullptr : roomTiles(nes, room);
    if (live || !own) for (int i = 0; i < OverworldMap::TILES; i++) tiles[i] = nes.cart.cpuRead(kPlayAreaTiles + i);
    gatherArmosTiles(own ? own : tiles.data(), room, (float)ox, (float)oy);
    for (int r2 = 0; r2 < 128; r2++) {                                      // every explored screen, wherever the camera is
        if (r2 == room || !discovered(slot, r2)) continue;
        const uint8_t* nb = roomTiles(nes, r2);
        if (nb) gatherArmosTiles(nb, r2, (float)(ox + ((r2 & 15) - (room & 15)) * ROOM_W), (float)(oy + ((r2 >> 4) - (room >> 4)) * ROOM_H));
    }
}

void Widescreen::gatherArmosTiles(const uint8_t* tiles, int room, float ox, float oy) {
    const unsigned t = dayClock.tick;
    for (int col = 0; col + 1 < OverworldMap::TILE_COLS; col++)
        for (int row = 0; row + 1 < OverworldMap::TILE_ROWS; row++) {
            if (tiles[col * OverworldMap::TILE_ROWS + row] != 0xC0 || tiles[(col + 1) * OverworldMap::TILE_ROWS + row] != 0xC2) continue;   // the top-left of an armos' 2x2
            bool secret = false;
            for (int k = 0; k < 7; k++) secret = secret || (kSecretArmosRoom[k] == room && kSecretArmosX[k] == col * 8 && kSecretArmosY == row * 8 + HUD_H);
            const float pulse = 0.85f + 0.15f * std::sin(t * 0.08f + col);
            const float x = ox + col * 8 + 8, y = oy + row * 8 + 8;
            lightSources.push_back(secret ? lighting::Source{x, y, 34, 0.9f * pulse, 0.5f, 255, 60, 40} : lighting::Source{x, y, 32, 0.8f * pulse, 0.45f, 70, 130, 255});
        }
}

void Widescreen::applyNightLight(Canvas& c, int top, int camX, int camY) {
    dayClock.step();
    if (!lighting) return;
    const float dl = dayClock.daylight(), bolt = dayClock.lightning();
    lighting::Params p;
    p.ambient = lighting::overworldAmbient(dl);
    p.ambient += (1.0f - p.ambient) * 0.85f * bolt;                       // lightning
    p.nightTint = (1.0f - dl) * (1.0f - bolt);
    p.warm = dayClock.twilight();
    p.vision = true; p.vx = lightLinkX; p.vy = lightLinkY;
    std::vector<lighting::Source> src = lightSources;
    if (candleHeld) {                                                       // Link carries the candle he holds (his steady place, so it also lights the screen changes)
        const float fl = lighting::flicker(dayClock.tick, 9);
        if (candleHeld == 1) src.push_back({lightLinkX, lightLinkY, 44, 0.6f * fl, 0.0f, 110, 170, 255});
        else                 src.push_back({lightLinkX, lightLinkY, 44, 0.6f * fl, 0.0f, 255, 190, 110});
    }
    std::vector<uint8_t> blackMask;
    if (curtainMask.size() == (size_t)c.w * c.h) { p.mask = curtainMask.data(); p.maskValue = 1; }
    else if (fxNes && (fxNes->ram[kGameMode] == kModeLoad || fxNes->ram[kGameMode] == kModeUnfurl)) {          // loading / the curtain on a picture in its box: only what is already showing is lit
        blackMask.resize((size_t)c.w * c.h);
        for (size_t i = 0; i < blackMask.size(); i++) blackMask[i] = (c.pixels[i] & 0x00FFFFFF) != 0;
        p.mask = blackMask.data(); p.maskValue = 1;
    }   // the closed part of the curtain stays black
    { perf::Scope perfScope(perf::kLight); lighting::apply(c.pixels.data(), c.w, c.h, top, c.h, camX, camY, p, src); }
    if (lightPpu) drawSparkles(c.pixels.data(), c.w, c.h, top, camX, camY, *lightPpu);
}

void Widescreen::applyDungeonLight(Nes& nes, Canvas& c, const ViewSnapshot& v, int top, int bottom) {
    dayClock.step();
    const bool sliding = v.mode == kModeScroll;
    // (on the stairs the room id is already the next room's while the picture still shows this one: use the room at rest)
    bool picBlack = true;                                                    // (is the picture black right now: a cellar's own picture comes after a black screen)
    for (int y = std::max(top, 0); y < std::min(bottom, c.h) && picBlack; y += 6) for (int x = 0; x < c.w; x += 6) if (c.pixels[(size_t)y * c.w + x] & 0x00FFFFFF) { picBlack = false; break; }
    if (v.mode != kModeCellar) cellarBlackSeen = false; else if (picBlack) cellarBlackSeen = true;
    const bool dark = (v.mode == kModeStairs || (v.mode == kModeCellar && !cellarBlackSeen)) ? restDark : (nes.cart.cpuRead(kAttrsE + v.room) & 0x80) != 0;     // the game darkens these rooms itself
    if (v.mode == kModeCellar && cellarBlackSeen) {                                    // a cellar: no torches, Link carries a little light of his own
        ease(dungeonDim, 0.0f, 1.0f / 6); ease(dungeonLit, 0.0f, 1.0f / 6);
        wasScroll = false;
        recordDungeonFrozen(nes, c, top, true);                              // (the cellar's bats stay on screen while it fades when Link climbs out: mode $A)
        frozenLinkX = v.linkX; frozenLinkY = v.linkY;                         // (the picture's Link: the game moves him in RAM before the cellar has faded)
        caveLight(nes, c.pixels.data(), c.w, c.h, top, bottom, v.linkX, v.linkY);
        return;
    }
    // Climbing out of a cellar (whichever of these modes the game uses for it): the picture is still the cellar's until it has faded to black: no torches of the room, the
    // cellar's own light, its bats (hidden by the game) stay until the picture is black.
    if ((v.mode == kModeLeaveCave || v.mode == kModeStairs || v.mode == kModeEnter || v.mode == kModeLeave) && frozenOn && frozenCellar && !frozenDone) {
        ease(dungeonDim, 0.0f, 1.0f / 6); ease(dungeonLit, 0.0f, 1.0f / 6);
        wasScroll = false; staleObjects = true;
        const int lum = roomLuma(c, top);
        if (lum <= 2) frozenDone = true;
        else drawFrozenSprites(c, top, 0, 0, frozenLuma > 0 ? std::min(256, lum * 256 / frozenLuma) : 256);
        caveLight(nes, c.pixels.data(), c.w, c.h, top, bottom, frozenLinkX, frozenLinkY);
        return;
    }
    ease(caveDim, 0.0f, 1.0f / 12);
    const int sx = nes.ppu.lineScrollX[150], oy = wrapTo((int)nes.ppu.lineScrollY[150] - 150, 0, 480);
    if (sliding) {                                                          // how far the picture has slid, counted frame by frame (the registers jump back at the end)
        if (!wasScroll) { slideX = slideY = 0; }
        else {
            const int ddx = wrapTo(sx - prevSx, -256, 512), ddy = wrapTo(oy - prevOy, -240, 480);
            if (std::abs(ddx) <= 16) slideX += ddx;
            if (std::abs(ddy) <= 16) slideY += ddy;
        }
        prevSx = sx; prevOy = oy;
    }
    const bool arrived = sliding && (std::abs(slideX) >= 248 || std::abs(slideY) >= 168);   // the next room fills the picture: it is the room at rest now
    const bool scrolling = sliding && !arrived;
    if (!scrolling) { restDark = dark; }
    if (scrolling && !wasScroll) { litOld = dungeonLit; dungeonLit = 0; }   // the room we leave stays lit, the next one starts dark
    if (sliding && !wasScroll && arrived) dungeonLit = 0;
    wasScroll = sliding;
    const bool playing = v.mode == kModePlay || v.mode == kModeLeave || v.mode == kModeDeath || v.mode == kModeStairs || v.mode == kModeEndLevel || (v.mode == kModeCellar && !cellarBlackSeen);     // (the death scene keeps the light)
    if (!scrolling) ease(dungeonDim, dark ? 0.0f : 1.0f, 1.0f / 6);
    ease(dungeonLit, (!dark && playing) ? 1.0f : 0.0f, 1.0f / 24);
    const int dx = scrolling ? std::clamp(slideX, -256, 256) : 0, dy = scrolling ? std::clamp(slideY, -176, 176) : 0;
    // The game hides all sprites while the room scrolls and on the stairs: the last frame of play is replayed, frozen (scrolled with the room; on the stairs
    // until the picture is black), with its reflections.
    // The last pixels of a scroll (the next room is the room at rest already, but the picture still has a few px to go): the water follows the picture.
    const int wdx = scrolling ? dx : (sliding && arrived && std::abs(slideX) >= 248 ? slideX - (slideX > 0 ? 256 : -256) : 0);
    const int wdy = scrolling ? dy : (sliding && arrived && std::abs(slideY) >= 168 ? slideY - (slideY > 0 ? 176 : -176) : 0);
    const bool recordNow = !sliding && v.mode == kModePlay;
    if (recordNow) { recordDungeonFrozen(nes, c, top); frozenLinkX = v.linkX; frozenLinkY = v.linkY; dunRefLuma = paletteLuma(nes.ppu.palette); }
    // modes that fade the picture (the old room / the cellar fades out, sprites hidden by the game, the replay fades with it): stairs, entering a cellar, leaving it
    const bool fading = v.mode == kModeStairs || (v.mode == kModeCellar && !cellarBlackSeen) || v.mode == kModeLeaveCave;
    int fade = 256;
    if (fading && frozenOn && !frozenDone) {
        fade = frozenLuma > 0 ? std::min(256, roomLuma(c, top) * 256 / frozenLuma) : 0;
        if (roomLuma(c, top) <= 2) frozenDone = true;                       // black: the next room comes, the frozen things are gone
    }
    const bool trans = frozenOn && (scrolling || v.mode == kModeLeave || (fading && !frozenDone));     // (mode 6: the game hides the sprites a frame before the scroll starts)
    // torches: the flames follow the game's own fade of the picture (they stay lit on a dark picture otherwise and leave bare torches when they switch off)
    if (recordNow) staleObjects = false; else if (trans || v.mode == kModeScroll || fading) staleObjects = true;
    if (!trans && (v.mode == kModeScroll || (fading && frozenDone))) sparkles.clear();   // (arrived / black: the old room's glitter must not flash at its old place)
    const float torchFade = dunRefLuma > 0 ? std::clamp((float)paletteLuma(nes.ppu.palette) / (float)dunRefLuma, 0.0f, 1.0f) : 1.0f;
    if (v.mode >= kModeEnter && v.mode <= kModeScroll) linkAtDoors(nes, c, top, dx, dy, scrolling);   // (before the water: his reflection and the light use the finished picture)
    if (trans || (v.mode == kModeEnter || v.mode == kModePlay || v.mode == kModeLeave || v.mode == kModeDeath || v.mode == kModeEndLevel || (v.mode == kModeScroll && !scrolling) || v.mode == kModeStairs || v.mode == kModeLeaveCave))
        dungeonWater(nes, c, top, 0, wdx, wdy, trans, fade);                       // the moat: reflections of the wall, Link, monsters and items
    if (!lighting) return;
    std::vector<Spot> spots;
    torchSpots(dx, dy, scrolling, spots);
    auto roomDark = [&](int room) { return scrolling ? (room == 0 ? restDark : dark) : dark; };
    auto roomLit = [&](int room) { return scrolling && room == 0 ? litOld : dungeonLit; };
    auto roomDim = [&](int room) { return scrolling ? (roomDark(room) ? 0.0f : 1.0f) : dungeonDim; };
    std::vector<char> shown;                                                // (a torch is only drawn where the room itself is revealed: not on the curtain's black)
    for (const Spot& s : spots) {
        const int y = top + s.y;
        shown.push_back(!roomDark(s.room) && litPatch(c, s.x - 9, y - 9) && litPatch(c, s.x + 9, y - 9) && litPatch(c, s.x - 9, y + 9) && litPatch(c, s.x + 9, y + 9));
    }
    lightShiftX = trans ? dx : 0; lightShiftY = trans ? dy : 0;
    gatherLights(nes, v, 0, 0, 0, false);
    lightShiftX = lightShiftY = 0;
    if (candleIdx < 0 && nes.ram[kSelectedItem] == kItemSlotCandle && nes.ram[kInvCandle]) {
        // The game's objects are hidden in a scroll (and Link too), so the candle's light was not gathered: it is Link's own light, and he walks on through the door:
        // from where he left the old room (his place stays in RAM during the scroll) to where the new room's door will put him, as far as the slide has got.
        float x = (float)nes.ram[kObjX] + 8.0f, y = (float)nes.ram[kObjY] - HUD_H + 8.0f;
        if (scrolling) {
            const float px = dx != 0 ? std::min(1.0f, std::fabs((float)dx) / 256.0f) : 0.0f, py = dy != 0 ? std::min(1.0f, std::fabs((float)dy) / 176.0f) : 0.0f;
            if (dx != 0) x += ((dx > 0 ? 24.0f : 232.0f) - x) * px;               // (into the next room's left door when the scroll goes right, ...)
            if (dy != 0) y += ((dy > 0 ? 24.0f : 152.0f) - y) * py;
        }
        const float pw = 0.6f * lighting::flicker(dayClock.tick, 9) * (trans ? (float)fade / 256.0f : 1.0f);
        if (nes.ram[kInvCandle] == 1) lightSources.push_back({x, y, 44, pw, 0.0f, 110, 170, 255});
        else                          lightSources.push_back({x, y, 44, pw, 0.0f, 255, 190, 110});
    }
    if (recordNow) frozenSparkSpots = sparkSpots;
    // (the white trail of slot $0F is the boomerang's, but the food that Link drops lies in the same slot: in a transition the trail is not replayed and what is left
    // of it is dropped, or it would hang as white dust around the frozen meat)
    if (trans) sparkles.erase(std::remove_if(sparkles.begin(), sparkles.end(), [](const Sparkle& s) { return s.kind == 1; }), sparkles.end());
    if (trans && fade >= 128) { sparkReplay = true; for (const SparkSpot& f : frozenSparkSpots) if (f.kind != 1) spawnSparkles(f.x, f.y, f.kind); sparkReplay = false; }   // the glitter goes on around the frozen things
    if (recordNow) {                                                        // (what glows now, without Link's candle and the sparkles' lights)
        frozenLights.clear();
        for (int i = 0; i < baseLightN && i < (int)lightSources.size(); i++) if (i != candleIdx) frozenLights.push_back(lightSources[i]);
    }
    std::vector<lighting::Source> frozenShifted;
    if (trans) for (lighting::Source l : frozenLights) { l.x -= (float)dx; l.y -= (float)dy; if (fade < 256) l.power *= (float)fade / 256.0f; frozenShifted.push_back(l); }
    const std::vector<lighting::Source> objects = lightSources;
    const unsigned t = dayClock.tick;
    std::vector<uint8_t> mask;
    if (scrolling && !spots.empty()) {                                      // which pixels belong to the room that is leaving (0) and to the one coming in (1)
        mask.assign((size_t)c.w * c.h, 1);
        for (int y = std::max(top, 0); y < std::min(bottom, c.h); y++)
            for (int x = 0; x < c.w; x++) { const int lx = x + dx, ly = y - top + dy; if (lx >= 0 && lx < 256 && ly >= 0 && ly < 176) mask[(size_t)y * c.w + x] = 0; }
    }
    const int regions = mask.empty() ? 1 : 2;
    for (int r = 0; r < regions; r++) {
        lightSources = objects;
        if (r == 0) lightSources.insert(lightSources.end(), frozenShifted.begin(), frozenShifted.end());     // (the room that is leaving; r == 0 also when there is no mask)
        for (const Spot& s : spots)
            if (s.room == r && roomLit(r) > 0 && !roomDark(r))
                lightSources.push_back({(float)s.x, (float)s.y, 200.0f, 1.0f * roomLit(r) * torchFade * lighting::flicker(t, s.i), 0.0f, 255, 170, 80});
        lighting::Params p;
        const float room = kDungeonDarkAmbient + (kDungeonLitAmbient - kDungeonDarkAmbient) * roomLit(r);
        p.ambient = 1.0f + (room - 1.0f) * roomDim(r);
        if (!mask.empty()) { p.mask = mask.data(); p.maskValue = (uint8_t)r; }
        { perf::Scope perfScope(perf::kLight); lighting::apply(c.pixels.data(), c.w, c.h, top, bottom, 0, 0, p, lightSources); }
    }
    lightSources = objects;
    drawSparkles(c.pixels.data(), c.w, c.h, top, trans ? dx : 0, trans ? dy : 0, nes.ppu);
    for (size_t i = 0; i < spots.size(); i++) {                             // (a torch's flame does not dim; the rest of it is in its room's shade)
        if (!shown[i]) continue;
        const int r = spots[i].room;
        const float room = kDungeonDarkAmbient + (kDungeonLitAmbient - kDungeonDarkAmbient) * roomLit(r), amb = 1.0f + (room - 1.0f) * roomDim(r);
        drawTorch(c, spots[i].x, top + spots[i].y, spots[i].i, roomLit(r) * torchFade, amb + (1.0f - amb) * roomLit(r) * torchFade, t);
    }
}

void Widescreen::gatherWareLights(Nes& nes, int ox, int oy, bool cave) {
    if (!cave || !(nes.ram[kCaveFlags] & 4)) return;                      // the wares of a cave (or the gift of an old man / the money of a secret)
    static const int kWareX[3] = {0x58, 0x78, 0x98};
    for (int i = 0; i < 3; i++) {
        const int id = nes.ram[kCaveItemIds + i] & 0x3F;                  // (the same glow and glitter as the item lying in the open; the price rupee beside the wares is no ware)
        const float x = (float)(ox + kWareX[i] + 8), y = (float)(oy + 0x98 - HUD_H + 8);
        if (id == kItemHeartContainer) { lightSources.push_back({x, y, 32, 0.4f, 0.0f, 255, 90, 100}); spawnSparkles(x, y, 3); }
        else if (id == kItemHeart)     { lightSources.push_back({x, y, 20, 0.28f, 0.0f, 255, 90, 100}); spawnSparkles(x, y, 3); }
        else if (id == kItemRupee)     { lightSources.push_back({x, y, 20, 0.28f, 0.0f, 255, 190, 60}); spawnSparkles(x, y, 4); }
        else if (id == kItemRupee5)    { lightSources.push_back({x, y, 20, 0.28f, 0.0f, 90, 150, 255}); spawnSparkles(x, y, 5); }
    }
}

void Widescreen::caveLight(Nes& nes, uint32_t* pix, int w, int h, int top, int bottom, int linkX, int linkY) {
    // The inside of a cave or shop: dim, with a small circle of sight around Link, lit by the flames of the people in it.
    ease(caveDim, 1.0f, 1.0f / 16);
    if (!lighting) return;
    ViewSnapshot v; v.room = nes.ram[kRoomId];
    gatherLights(nes, v, 0, 0, 0, false);
    gatherWareLights(nes, 0, 0, true);
    lighting::Params p;
    p.ambient = 1.0f - (1.0f - kCaveAmbient) * caveDim;
    p.vision = true; p.vx = (float)((linkX >= 0 ? linkX : nes.ram[kObjX]) + 8); p.vy = (float)((linkY >= 0 ? linkY : nes.ram[kObjY]) - HUD_H + 8); p.visionInner = 34; p.visionOuter = 100;
    { perf::Scope perfScope(perf::kLight); lighting::apply(pix, w, h, top, bottom, 0, 0, p, lightSources); }
    drawSparkles(pix, w, h, top, 0, 0, nes.ppu);
}

// The candle's fire is placed 16 px in front of Link and walks 16 px further, in the game's 8-bit coordinates: at a border it wraps to
// the other side (left/right) or goes beyond the rows the PPU shows (bottom). Its true place is tracked here; the game's own object is
// kept inside the screen, and the picture draws the flame at the true place (planSpriteFixes).
void Widescreen::trackFire(Nes& nes) {
    for (int s = 0; s < 2; s++) {
        FireTrack& f = fireTrack[s];
        const int slot = kSlotBombA + s;
        for (int g = 1; g > 0; g--) { f.drawX[g] = f.drawX[g - 1]; f.drawY[g] = f.drawY[g - 1]; f.overX[g] = f.overX[g - 1]; f.overY[g] = f.overY[g - 1]; f.act[g] = f.act[g - 1]; }
        const int st = nes.ram[kObjState + slot];
        if ((st & 0xF0) != 0x20) { f.on = false; f.act[0] = false; continue; }
        const int gx = nes.ram[kObjX + slot], gy = nes.ram[kObjY + slot];
        if (!f.on) {                                                          // just placed: where PlaceWeapon meant it
            const int dir = nes.ram[kObjDir], lx = nes.ram[kObjX], ly = nes.ram[kObjY];
            f.tx = lx + (dir == 1 ? 16 : dir == 2 ? -16 : 0);
            f.ty = ly + (dir == 4 ? 16 : dir == 8 ? -16 : 0);
            f.on = true;
        } else {
            f.tx += (int8_t)(gx - f.lastX); f.ty += (int8_t)(gy - f.lastY);
        }
        f.drawX[0] = gx; f.drawY[0] = gy; f.overX[0] = f.tx - gx; f.overY[0] = f.ty - gy; f.act[0] = true;
        const int cx = std::clamp(f.tx, 0, 255), cy = std::clamp(f.ty, 0, 0xE6);
        nes.ram[kObjX + slot] = (uint8_t)cx; nes.ram[kObjY + slot] = (uint8_t)cy;
        f.lastX = cx; f.lastY = cy;
    }
}

void Widescreen::spawnSparkles(float x, float y, int kind) {
    if (!sparkReplay) sparkSpots.push_back({x, y, kind});
    auto rnd = [&]() { sparkRng = sparkRng * 1664525u + 1013904223u; return sparkRng >> 8; };
    const int chance = kind == 0 ? 45 : kind == 2 ? 60 : kind >= 3 ? 25 : 100;
    if ((int)(rnd() % 100) >= chance) return;                              // a few every frame, at random places around the thing
    const float ang = (float)(rnd() % 628) / 100.0f;
    const float rad = kind == 1 ? (float)(rnd() % 4) : kind == 2 ? 2.0f + (float)(rnd() % 9) : 3.0f + (float)(rnd() % 14);
    const int life = kind == 1 ? 10 + (int)(rnd() % 7) : kind == 2 ? 18 + (int)(rnd() % 13) : 16 + (int)(rnd() % 14);
    sparkles.push_back({x + std::cos(ang) * rad, y + std::sin(ang) * rad * 0.8f, kind == 1 ? 0.0f : kind == 2 ? -0.2f : -0.25f, dayClock.tick, life, kind});
    if (sparkles.size() > 120) sparkles.erase(sparkles.begin());
}

void Widescreen::drawSparkles(uint32_t* pix, int w, int h, int top, int camX, int camY, const Ppu& ppu) {
    const unsigned t = dayClock.tick;
    sparkles.erase(std::remove_if(sparkles.begin(), sparkles.end(), [&](const Sparkle& s) { return (int)(t - s.born) >= s.life; }), sparkles.end());
    static const uint32_t kBright[6] = {0xFFFFE8F8, 0xFFFFFFFF, 0xFFFFF0A0, 0xFFFFC8C8, 0xFFFFE8A0, 0xFFD0E4FF};
    static const uint32_t kDim[6]    = {0xFFFF9AD8, 0xFFB8D4FF, 0xFFF8C830, 0xFFE83C48, 0xFFF0A020, 0xFF4878F0};
    static const uint32_t kArm[6]    = {0xFFFF7CC8, 0xFFD8E8FF, 0xFFE8A010, 0xFFC82838, 0xFFD88810, 0xFF3860D0};
    for (const Sparkle& s : sparkles) {
        const int age = (int)(t - s.born);
        const float f = 1.0f - (float)age / s.life;
        const int wx = (int)std::lround(s.x), wy = (int)std::lround(s.y + s.vy * age);
        const int cx = wx - camX, cy = wy - camY + top;
        if (cx < 1 || cy < top + 1 || cx >= w - 1 || cy >= h - 1) continue;
        if (curtainMask.size() == (size_t)w * h && !curtainMask[(size_t)cy * w + cx]) continue;   // (not on the curtain's black)
        if (s.kind == 1) {                                                   // a trail runs under the sprites (boomerang, arrow, Link...)
            const int px = wx - sparkOx, py = wy - sparkOy + HUD_H;
            if (px >= 0 && px < Ppu::SPR_W && py >= 0 && py < Ppu::SPR_H && ppu.sprCol[py * Ppu::SPR_W + px] != 0xFF) continue;
        }
        const int k = std::clamp(s.kind, 0, 5);
        putBuf(pix, w, h, cx, cy, f > 0.5f ? kBright[k] : kDim[k]);
        if (f > 0.25f && f < 0.8f && s.kind != 1) for (const auto& d : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) putBuf(pix, w, h, cx + d.first, cy + d.second, kArm[k]);   // a little star
    }
}

// The candle's fire is placed 16 px ahead of Link and walks 16 px on: it stops 32 px from him. B is released when the flame would stop (its
// 16x16 square) outside the play area, i.e. when Link is closer than that to the border he faces.
void Widescreen::maskCandleAtBorder(Nes& nes) {
    if (nes.ram[kGameMode] != kModePlay || nes.ram[kCurLevel] != 0 || nes.ram[kSelectedItem] != kItemSlotCandle || !nes.ram[kInvCandle]) return;
    const int x = nes.ram[kObjX], y = nes.ram[kObjY] - HUD_H, dir = nes.ram[kObjDir];
    const bool tooClose = (dir == 2 && x < 32) || (dir == 1 && x > 256 - 32 - 16) || (dir == 8 && y < 32) || (dir == 4 && y > ROOM_H - 32 - 16);
    if (tooClose) nes.pad1 &= (uint8_t)~BTN_B;
}
