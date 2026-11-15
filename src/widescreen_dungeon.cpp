// Dungeon transitions: the game hides every sprite while a room scrolls (mode 7) and on the stairs (mode $10). The last frame of play is recorded (monsters,
// items, their feet for the reflections, the room's water grid, the lights) and drawn again, frozen, so they scroll away with the room / stay until the picture is black.
#include "widescreen_internal.h"

// The brightness of the room's picture = its brightest channel value (the game's fade lowers every colour step by step: the brightest pixel follows the fade
// exactly, an average would call a faint but still visible picture black).
int Widescreen::roomLuma(const Canvas& c, int top) {
    int peak = 0;
    for (int y = top; y < std::min(c.h, top + ROOM_H); y += 2) for (int x = 0; x < std::min(c.w, ROOM_W); x += 2) {
        const uint32_t p = c.pixels[(size_t)y * c.w + x];
        peak = std::max(peak, (int)std::max({p >> 16 & 255, p >> 8 & 255, p & 255}));
    }
    return peak;
}

void Widescreen::recordDungeonFrozen(Nes& nes, const Canvas& c, int top, bool cellar) {
    frozenCellar = cellar;
    std::vector<FrozenPix> old; old.swap(frozenSpr);
    uint8_t kinds[704]; roomKindsLive(nes, kinds); frozenGrid.assign(kinds, kinds + 704);
    frozenLuma = roomLuma(c, top); frozenOn = true; frozenDone = false;
    frozenWaterPix.clear(); frozenWaterWant = !cellar;
    frozenTerrain.assign((size_t)ROOM_W * ROOM_H, 0xFF000000u);                      // (the picture without sprites: the background layer where one is)
    for (int y = 0; y < ROOM_H && top + y < c.h; y++) for (int x = 0; x < ROOM_W && x < c.w; x++) {
        const size_t id = (size_t)(HUD_H + y) * Ppu::SPR_W + x;
        frozenTerrain[(size_t)y * ROOM_W + x] = nes.ppu.sprCol[id] != 0xFF ? (nes.ppu.bgColor[(size_t)(HUD_H + y) * Ppu::W + x] | 0xFF000000u) : c.pixels[(size_t)(top + y) * c.w + x];
    }
    const Ppu& ppu = nes.ppu;
    std::vector<int16_t> feetRow((size_t)Ppu::SPR_W * Ppu::SPR_H, -1000); std::vector<uint8_t> isShot((size_t)Ppu::SPR_W * Ppu::SPR_H, 0);
    for (int s = 1; s <= 0x13; s++) {                                     // which object owns which sprite pixel (the same boxes as the water pass)
        const int st = nes.ram[kObjState + s], ty = nes.ram[kObjType + s];
        const bool active = s <= 12 ? ty != 0 : s < 0x13 ? st != 0 : (!(st & 0x80) && nes.ram[kRoomItemId] != 0x3F);
        if (!active) continue;
        const int ox = nes.ram[kObjX + s], oy = nes.ram[kObjY + s], m = s >= kSlotSword ? 12 : 2;
        const int feet = (s == kSlotSword ? nes.ram[kObjY] : oy) + 17 - HUD_H;
        const bool shot = s == kSlotSword || s == 0x0E || s == 0x0F || s == 0x12 || (s >= 1 && s <= 12 && (ty == 0x53 || ty == 0x54 || ty == 0x57 || ty == 0x58 || ty == 0x59 || ty == 0x5B || ty == 0x5C));
        for (int py = std::max(HUD_H, oy - m); py < std::min(Ppu::SPR_H, oy + 17 + m); py++)
            for (int px = std::max(0, ox - m); px < std::min(256, ox + 16 + m); px++) {
                const size_t id = (size_t)py * Ppu::SPR_W + px;
                if (feetRow[id] != -1000 || ppu.sprCol[id] == 0xFF) continue;
                feetRow[id] = (int16_t)feet; isShot[id] = shot;
            }
    }
    for (int py = HUD_H; py < std::min(Ppu::SPR_H, HUD_H + ROOM_H); py++) for (int px = 0; px < 256; px++) {
        const size_t id = (size_t)py * Ppu::SPR_W + px;
        if (ppu.sprCol[id] == 0xFF) {                                                                          // (a sprite the PPU left out because its lines were full: the fire/flames at the top and bottom walls)
            const int o = ppu.dropOam[id];
            if (ppu.dropCol[id] == 0xFF || o == kLinkOam0 || o == kLinkOam1 || hudSpriteEntry(ppu.oam, o)) continue;
            frozenSpr.push_back({(int16_t)px, (int16_t)(py - HUD_H), (int16_t)-1000, Ppu::nesColor(ppu.dropCol[id]), true});
            continue;
        }
        if (ppu.sprOam[id] == kLinkOam0 || ppu.sprOam[id] == kLinkOam1) continue;     // (Link is drawn by the game during the scroll)
        frozenSpr.push_back({(int16_t)px, (int16_t)(py - HUD_H), (int16_t)feetRow[id], Ppu::nesColor(ppu.sprCol[id]), !isShot[id]});
    }
    // A frame without any sprite right after frames with some is the game hiding them (a transition begins): keep what was seen for two frames
    // (a monster that really died is gone after those two).
    if (frozenSpr.empty() && !old.empty() && frozenAge < 2) { frozenSpr.swap(old); frozenAge++; } else if (!frozenSpr.empty() || frozenAge >= 2) frozenAge = 0;
}

void Widescreen::drawFrozenSprites(Canvas& c, int top, int dx, int dy, int fade) {
    for (const FrozenPix& f : frozenSpr) {
        const int x = f.x - dx, y = top + f.y - dy;
        if (x < 0 || x >= std::min(c.w, ROOM_W) || y < top || y >= std::min(c.h, top + ROOM_H)) continue;
        uint32_t p = f.argb;
        if (fade < 256) p = 0xFF000000u | ((p >> 16 & 255) * fade >> 8) << 16 | ((p >> 8 & 255) * fade >> 8) << 8 | ((p & 255) * fade >> 8);
        c.pixels[(size_t)y * c.w + x] = p;
    }
}

// ---- the water of a dungeon room (moved here from widescreen_water.cpp; the pass itself is waterPass there) ----
namespace { constexpr int R = OverworldMap::TILE_ROWS; }

// A dungeon room (Layout::Plain): the moat is tile $F4 (PrimarySquaresUW index 6, whole squares) inside the 24x14 tile floor; the frame of 4 tiles all
// round (walls, doors) is scenery that reflects whole from its base. The room's tiles are put into the (swapped-in) water grid and the same pass as the
// overworld runs on the room's picture: ripples of small water, reflections of the back wall (from its base), Link, monsters, items.
// A room at rest sits at tile (32, 22) of the grid (world pixel (256, 176)). During a room scroll the room we leave (the grid recorded on the last frame of
// play) stays there and the next room is put one room away in the scroll's direction (256 px sideways, 176 px up/down); the canvas shows the world shifted by
// the slide (dx, dy). On the stairs (dx = dy = 0) the recorded room stays. The things that moved are the frozen sprites (`extra`) and Link's live pixels.
int Widescreen::roomKindsLive(Nes& nes, uint8_t out[704]) {
    int nWater = 0;
    for (int col = 0; col < 32; col++) for (int row = 0; row < R; row++) {
        const bool floor = col >= 4 && col < 28 && row >= 4 && row < 18;
        uint8_t k = floor ? 0 : kFull;
        if (floor && nes.cart.cpuRead((uint16_t)(zelda::kPlayAreaTiles + col * R + row)) == 0xF4) { k = kWater; nWater++; }
        out[col * R + row] = k;
    }
    return nWater;
}

// The room that scrolls in from above: the game lays it out only when it has arrived, so its moat is read from the picture. A cell is water when (almost) all
// of its visible pixels have the moat's colour; cells above the screen take what the cell below them is (the water goes on up to the wall).
void Widescreen::pictureWaterGrid(const Canvas& c, int top, int left, int dy, uint8_t out[704]) const {
    const int camY = dy + 176;
    signed char state[32][22];                                                        // -1 unknown, 0 floor, 1 water
    for (int col = 0; col < 32; col++) for (int row = 0; row < R; row++) {
        const bool floor = col >= 4 && col < 28 && row >= 4 && row < 18;
        state[col][row] = floor ? -1 : 0;
        if (!floor) continue;
        int n = 0, moat = 0;
        for (int y = row * 8; y < row * 8 + 8; y++) {
            const int yc = y - camY; if (yc < 0 || yc >= ROOM_H || top + yc >= c.h) continue;
            n++; for (int x = col * 8; x < col * 8 + 8; x++) { const int px = left + x; if (px < c.w) moat += ((c.pixels[(size_t)(top + yc) * c.w + px] ^ moatColor) & 0xFFFFFFu) == 0; }
        }
        if (n >= 4) state[col][row] = moat * 10 >= n * 8 * 9 ? 1 : 0;
    }
    for (int col = 4; col < 28; col++) { signed char next = 0; for (int row = 17; row >= 4; row--) { if (state[col][row] < 0) state[col][row] = next; else next = state[col][row]; } }
    for (int col = 0; col < 32; col++) for (int row = 0; row < R; row++) {
        const bool floor = col >= 4 && col < 28 && row >= 4 && row < 18;
        out[col * R + row] = floor ? (state[col][row] > 0 ? kWater : 0) : kFull;
    }
}

void Widescreen::dungeonWater(Nes& nes, Canvas& c, int top, int left, int dx, int dy, bool trans, int fade) {
    auto drawFrozen = [&] { if (trans) drawFrozenSprites(c, top, dx, dy, fade); };
    if (!waterOn() || !enabled) { frozenWaterWant = false; drawFrozen(); return; }
    constexpr int OX = 256, OY = 176;
    uint8_t live[704]; const int liveWater = roomKindsLive(nes, live);
    int frozenWater = 0; if (trans && frozenOn && frozenGrid.size() == 704) for (uint8_t k : frozenGrid) frozenWater += (k & kWater) != 0;
    const int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    const bool vert = trans && frozenOn && dy != 0 && frozenTerrain.size() == (size_t)ROOM_W * ROOM_H;      // up/down scroll: the reflections' source rows above the screen are made up (below)
    // Going up the game has not laid the next room out yet (PlayAreaTiles is still the old room): its moat is read from the picture.
    uint8_t seen[704]; int seenWater = 0;
    if (vert && sy < 0 && dx == 0 && moatColor) { pictureWaterGrid(c, top, left, dy, seen); for (uint8_t k : seen) seenWater += (k & kWater) != 0; }
    if (!liveWater && !frozenWater && !seenWater) { frozenWaterWant = false; drawFrozen(); return; }       // (no moat: nothing to do)
    if (dungeonKind.size() != (size_t)GW * GH) dungeonKind.assign((size_t)GW * GH, 0); else std::fill(dungeonKind.begin(), dungeonKind.end(), 0);
    int nWater = 0;
    auto place = [&](const uint8_t* kinds, int tx, int ty) {
        for (int col = 0; col < 32; col++) for (int row = 0; row < R; row++) {
            const uint8_t k = kinds[col * R + row]; nWater += (k & kWater) != 0;
            if (tx + col >= 0 && tx + col < GW && ty + row >= 0 && ty + row < GH) dungeonKind[(size_t)(ty + row) * GW + tx + col] = k;
        }
    };
    if (trans && frozenOn && frozenGrid.size() == 704) {
        place(frozenGrid.data(), 32, 22);
        if (sy < 0) { if (seenWater) place(seen, 32, 22 + 22 * sy); }
        else if (sx || sy) place(live, 32 + 32 * sx, 22 + 22 * sy);
    } else place(live, 32, 22);
    if (!nWater) { frozenWaterWant = false; drawFrozen(); return; }
    waterTick++;
    // Source rows above the picture (the old room's wall that has scrolled off the top, the wall of the room that is still above the screen): the old room's own
    // picture, recorded at the last frame of play (the next room's wall is made up from it: the walls of a dungeon look alike).
    const int M = vert ? 64 : 0, W = c.w, H = c.h + M, topE = top + M;
    auto madeUpRow = [&](int yc, uint32_t* out) {                                   // room row yc (negative: above the picture), as the old room's row it stands for
        int vy = yc + dy; if (vy < 0) vy += ROOM_H; if (vy < 0 || vy >= ROOM_H) vy = std::clamp(vy, 0, ROOM_H - 1);
        for (int x = 0; x < W; x++) out[x] = (x >= left && x < left + ROOM_W) ? frozenTerrain[(size_t)vy * ROOM_W + (x - left)] : 0xFF000000u;
    };
    auto withMargin = [&](const std::vector<uint32_t>& base) {                      // rows 0..topE-1 made up, then the canvas from `top` on
        std::vector<uint32_t> e((size_t)W * H);
        for (int y = 0; y < topE; y++) madeUpRow(y - topE, &e[(size_t)y * W]);
        std::copy(base.begin() + (size_t)top * W, base.end(), e.begin() + (size_t)topE * W);
        return e;
    };
    std::vector<WaterObj> ex;
    if (trans) {
        auto inside = [&](int x, int y) { return x >= 0 && x < c.w && y >= 0 && y < c.h; };           // (a frozen sprite can be scrolled out of the canvas: its source pixel must exist)
        for (const FrozenPix& f : frozenSpr) if (f.feet > -500 && inside(left + f.x - dx, top + f.y - dy)) ex.push_back({left + f.x - dx, top + f.y - dy + M, top + f.feet - dy + M, f.flip});
        const int feet = top + feetOf(nes.ram[kObjY]) - HUD_H;
        for (int py = HUD_H; py < HUD_H + ROOM_H; py++) for (int px = 0; px < 256; px++) {
            const size_t id = (size_t)py * Ppu::SPR_W + px;
            if (nes.ppu.sprCol[id] != 0xFF && (nes.ppu.sprOam[id] == kLinkOam0 || nes.ppu.sprOam[id] == kLinkOam1) && inside(left + px, top + py - HUD_H)) ex.push_back({left + px, top + py - HUD_H + M, feet + M, true});
        }
    }
    const bool record = !trans && frozenWaterWant && dx == 0 && dy == 0;                // (the frame of play whose water the room keeps while it scrolls away)
    const bool replay = trans && fade >= 256 && frozenWaterPix.size() == (size_t)ROOM_W * ROOM_H;
    std::vector<uint32_t> before, beforeT;                                              // (the room's pixels before the pass: at rest / with the frozen sprites drawn)
    if (record) { before.assign((size_t)ROOM_W * ROOM_H, 0); for (int y = 0; y < ROOM_H && top + y < c.h; y++) for (int x = 0; x < ROOM_W && left + x < c.w; x++) before[(size_t)y * ROOM_W + x] = c.pixels[(size_t)(top + y) * c.w + left + x]; }
    std::swap(wwKind, dungeonKind); wwDungeon = true;
    const int ox = fxOx, oy = fxOy; fxOx = OX; fxOy = OY;
    // Pass A on the room without any sprite (the game's background layer where a sprite is): what is water is decided from the terrain. A white sword over the
    // water would make its tile look like a ladder (no water) and the reflection would vanish. Pass B puts the reflections on the real picture.
    std::vector<uint32_t> clean(c.pixels), work;                                  // (clean stays the plain terrain: pass A works on a copy)
    for (int py = HUD_H; py < std::min(Ppu::SPR_H, HUD_H + ROOM_H); py++) for (int px = 0; px < 256; px++)
        if (nes.ppu.sprCol[(size_t)py * Ppu::SPR_W + px] != 0xFF) {
            const int cx = left + px, cy = top + py - HUD_H;
            if (cx >= 0 && cx < c.w && cy >= 0 && cy < c.h) clean[(size_t)cy * c.w + cx] = nes.ppu.bgColor[(size_t)py * Ppu::W + px] | 0xFF000000u;
        }
    std::vector<uint8_t> mask;
    const int camX = -left + dx + OX, camY = dy + OY, x1 = std::min(c.w, left + ROOM_W), y1 = std::min(c.h, top + ROOM_H);
    if (!vert) {
        work = clean;
        WaterJob jA{work.data(), c.w, c.h, top, camX, camY, left, top, x1, y1, &mask, true, false, nullptr};
        waterPass(jA);
        drawFrozen(); if (replay) beforeT = c.pixels;
        WaterJob jB{c.pixels.data(), c.w, c.h, top, camX, camY, left, top, x1, y1, &mask, false, !trans, trans ? &ex : nullptr, true, &clean, 0};
        waterPass(jB);
    } else {                                                                         // the same on buffers with M rows of source above the picture
        std::vector<uint32_t> cleanE = withMargin(clean); work = cleanE;
        WaterJob jA{work.data(), W, H, topE, camX, camY, left, topE, x1, y1 + M, &mask, true, false, nullptr, true, nullptr, 0};   // (firstRow 0: the mask covers the rows above the picture, where the water's top edge may lie)
        waterPass(jA);
        drawFrozen(); if (replay) beforeT = c.pixels;
        std::vector<uint32_t> pic = withMargin(c.pixels);
        WaterJob jB{pic.data(), W, H, topE, camX, camY, left, topE, x1, y1 + M, &mask, false, false, &ex, true, &cleanE, 0};
        waterPass(jB);
        for (int y = top; y < y1; y++) for (int x = left; x < x1; x++) c.pixels[(size_t)y * W + x] = pic[(size_t)(y + M) * W + x];
    }
    if (record) {
        frozenWaterPix.assign((size_t)ROOM_W * ROOM_H, 0); frozenWaterWant = false;
        for (int y = 0; y < ROOM_H && top + y < c.h; y++) for (int x = 0; x < ROOM_W && left + x < c.w; x++) {
            const uint32_t p = c.pixels[(size_t)(top + y) * c.w + left + x];
            if (p != before[(size_t)y * ROOM_W + x]) frozenWaterPix[(size_t)y * ROOM_W + x] = p | 0xFF000000u;
        }
    } else if (replay) {   // the room that scrolls away keeps the look of its water at rest (waves stand still): the pass only decides for the room that scrolls in
        const int linkFeet = frozenLinkY + 17 - HUD_H;
        for (int y = std::max(top, 0); y < std::min(c.h, top + ROOM_H); y++) {                  // (only inside the playable screen: not on the bars above and below it)
            const int ry = y - top + dy; if (ry < 0 || ry >= ROOM_H) continue;
            for (int x = left; x < std::min(c.w, left + ROOM_W + 0); x++) {
                const int rx = x - left + dx; if (rx < 0 || rx >= ROOM_W) continue;
                if (rx >= frozenLinkX - 6 && rx < frozenLinkX + 22 && ry >= linkFeet - 2 && ry < linkFeet + 34) continue;   // (Link's own reflection: the game hides him in a scroll, it must not go on alone)
                const uint32_t v = frozenWaterPix[(size_t)ry * ROOM_W + rx];
                c.pixels[(size_t)y * c.w + x] = v ? v : beforeT[(size_t)y * c.w + x];
            }
        }
    }
    if (dbgBlue) moatColor = dbgBlue;
    fxOx = ox; fxOy = oy;
    std::swap(wwKind, dungeonKind); wwDungeon = false;
}
