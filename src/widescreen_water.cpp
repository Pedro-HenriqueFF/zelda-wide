// Water: reflections and waves on the overworld's lakes and sea.
//  * The water is found per PIXEL: the water's blue (the commonest colour of the water tiles) and the dots inside the water tiles, in or right
//    beside a water tile ($89-$98), so ragged shores and outlines stay out of it.
//  * Scenery reflects about the shoreline (walls, trees, stairs: all of it; walkable ground only the bottom quarter of its tile).
//  * Link, monsters, shots and drops reflect about their own FEET: the mirror image hangs below the feet and only shows where it falls on water.
//  * Waves only on big bodies: water that is 3+ tiles deep and reaches across at least two screens (the sea, the big lakes).
// Pass A runs on the decoded terrain inside drawWorld (so the fog covers it), pass B on the game's own picture of the current screen.
#include "widescreen.h"
#include "widescreen_internal.h"
#include "parallel.h"
#include "perf.h"
#include "zelda_ram.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <unordered_set>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace zelda;

namespace wd {
bool isWaterTile(uint8_t t) { return t >= 0x89 && t < 0x99; }   // ($8D/$91, the top corners of a pond, are walkable for the game but are water)
}

namespace {
constexpr int R = OverworldMap::TILE_ROWS;
constexpr float kReflectMax = 0.42f;                  // share of the scenery's reflection right under the shore
constexpr int kReflectLen = 40;                       // px (2.5 squares) that the reflection of a wall reaches below the shore at most
float fsin(float x) {                                   // a table sine (4096 steps): the waves need three per water pixel
    static const std::array<float, 4096> table = [] { std::array<float, 4096> t{}; for (int i = 0; i < 4096; i++) t[i] = std::sin(i * (6.2831853f / 4096.0f)); return t; }();   // (built once, thread-safe)
    return table[(int)(x * (4096.0f / 6.2831853f) + 1048576.0f) & 4095];   // (+2^20 keeps the index positive for the small negative phases)
}

constexpr int kWaveScreens[] = {14, 15, 31};          // screens (row * 16 + column) whose water always waves: the top right of the map
bool isSpecial(uint8_t t) { static const uint8_t sp[] = {0x9C, 0xAC, 0xAD, 0xCC, 0xD2, 0xD5, 0xDF}; for (uint8_t v : sp) if (v == t) return true; return false; }

// Tiles with background showing in them (trees and bushes: 8-56 of 64 pixels in the pattern), not walls. The mountain wall ids ($CE-$D1, $D8-$DB)
// are never open, whatever the pattern table holds at that moment (CHR-RAM: another level's tiles can be loaded while the screen data is read).
void openTiles(Nes& nes, bool open[256]) {
    const uint16_t patBase = (nes.ppu.lineCtrl[100] & 0x10) ? 0x1000 : 0;
    for (int id = 0; id < 256; id++) {
        int zero = 0;
        for (int r = 0; r < 8; r++) { const int lo = nes.cart.ppuRead(patBase + id * 16 + r), hi = nes.cart.ppuRead(patBase + id * 16 + r + 8); for (int b = 0; b < 8; b++) zero += !(((lo | hi) >> b) & 1); }
        open[id] = zero >= 8 && zero <= 56 && !((id >= 0xCE && id <= 0xD1) || (id >= 0xD8 && id <= 0xDB));
    }
}
}  // namespace

namespace wd {
// What a tile id means for the water pass (also for the tiles beyond the map): water, open (trees), stairs/ladder/docks, reflects whole.
uint8_t tileKind(uint8_t id, bool open) {
    uint8_t k = 0;
    if (isWaterTile(id)) k |= kWater;
    if (open) k |= kOpen;
    if (isSpecial(id) || (id >= 0x70 && id <= 0x77)) k |= kStairs;          // the stairs, the ladder ($70-$75) and the docks ($76/$77): never water, whatever the colour
    if (id >= 0x89 || isSpecial(id) || (id >= 0x70 && id <= 0x75)) k |= kFull;   // ($76/$77 are the docks: planks, not wall): walls, trees, stairs, the ladder...: the whole tile reflects
    return k;
}
void openTilesOf(Nes& nes, bool open[256]) { openTiles(nes, open); }
}

// The water grid follows the game's own screen data: whenever the decode key of any screen changes (the game loads another level's attributes into
// its PRG-RAM, a secret is found, a save is loaded) the grid is built again. (It used to be built once, from whatever the PRG-RAM held at the first frame.)
void Widescreen::ensureWaterWorld() {
    if (!fxNes || wwDungeon) return;                                          // (a dungeon room's grid is in place: the overworld's is left alone)
    uint64_t sig = 1469598103934665603ULL;
    for (int room = 0; room < 128; room++) { roomTiles(*fxNes, room); sig = (sig ^ (uint64_t)(cache[room].key + 2)) * 1099511628211ULL; }
    bool open[256]; openTiles(*fxNes, open);                                  // (the tile patterns count too: the grid is rebuilt when what is "open" changes)
    for (int id = 0; id < 256; id++) sig = (sig ^ (uint64_t)(open[id] ? 3 : 2)) * 1099511628211ULL;
    if (!wwReady || sig != wwSig) { buildWaterWorld(); wwSig = sig; }
}

void Widescreen::buildWaterWorld() {
    wwReady = true;
    wwKind.assign((size_t)GW * GH, 0);
    std::vector<uint8_t> ids((size_t)GW * GH, 0);
    bool open[256]; openTiles(*fxNes, open);
    for (int room = 0; room < 128; room++) {
        const uint8_t* tiles = roomTiles(*fxNes, room);
        for (int col = 0; col < 32; col++)
            for (int row = 0; row < R; row++) {
                const uint8_t id = tiles[col * R + row];
                ids[(size_t)((room >> 4) * R + row) * GW + (room & 15) * 32 + col] = id;
                const uint8_t k = wd::tileKind(id, open[id]);
                wwKind[(size_t)((room >> 4) * R + row) * GW + (room & 15) * 32 + col] = k;
            }
    }
    // Docks: a walkable special tile whose line (up-down or left-right, through other such tiles) has water at both ends is a plank, not wall.
    for (int gy = 0; gy < GH; gy++) for (int gx = 0; gx < GW; gx++) {
        const size_t i = (size_t)gy * GW + gx;
        if (!(wwKind[i] & kFull) || (wwKind[i] & kWater) || !ids[i] || !isSpecial(ids[i])) continue;
        auto endWater = [&](int dx, int dy) { for (int st = 1; st <= 4; st++) { const int x = gx + dx * st, y = gy + dy * st; if (x < 0 || y < 0 || x >= GW || y >= GH) return false; const size_t k = (size_t)y * GW + x; if (wwKind[k] & kWater) return true; if (!isSpecial(ids[k])) return false; } return false; };
        if ((endWater(0, -1) && endWater(0, 1)) || (endWater(-1, 0) && endWater(1, 0))) wwKind[i] &= (uint8_t)~(kFull | kStairs | kOpen);
    }
    // A square with 2+ wall tiles (not water) is wall all over: the curved and capped squares at the ends of walls have ground-id tiles in them.
    for (int gy = 0; gy + 1 < GH; gy += 2) for (int gx = 0; gx + 1 < GW; gx += 2) {
        uint8_t* q[4] = {&wwKind[(size_t)gy * GW + gx], &wwKind[(size_t)gy * GW + gx + 1], &wwKind[(size_t)(gy + 1) * GW + gx], &wwKind[(size_t)(gy + 1) * GW + gx + 1]};
        int nf = 0; for (uint8_t* t : q) nf += (*t & kFull) && !(*t & kWater);
        if (nf >= 2) for (uint8_t* t : q) if (!(*t & kWater)) *t |= kFull;
    }
    // Big bodies: the "core" is water 3+ tiles away from any other tile (the world's edge counts as sea); a connected core that reaches
    // into two or more screens is a lake or the sea; every water tile within 3 steps of such a core gets the waves.
    std::vector<uint8_t> dist((size_t)GW * GH, 255);
    std::vector<int> q; q.reserve((size_t)GW * GH);
    for (int i = 0; i < GW * GH; i++) if (!(wwKind[i] & kWater)) { dist[i] = 0; q.push_back(i); }
    auto nbrs = [&](int i, int* o) { int n = 0; const int x = i % GW, y = i / GW; if (x > 0) o[n++] = i - 1; if (x < GW - 1) o[n++] = i + 1; if (y > 0) o[n++] = i - GW; if (y < GH - 1) o[n++] = i + GW; return n; };
    for (size_t h = 0; h < q.size(); h++) {
        int nb[4]; const int n = nbrs(q[h], nb);
        for (int j = 0; j < n; j++) if (dist[nb[j]] == 255) { dist[nb[j]] = (uint8_t)std::min(250, dist[q[h]] + 1); q.push_back(nb[j]); }
    }
    auto roomOf = [&](int i) { return ((i / GW) / R) * 16 + (i % GW) / 32; };
    std::vector<uint8_t> seen((size_t)GW * GH, 0);
    std::vector<int> grow, stack, comp;
    for (int s = 0; s < GW * GH; s++) {
        if (!(wwKind[s] & kWater) || dist[s] < 3 || seen[s]) continue;
        comp.clear(); stack.assign(1, s); seen[s] = 1;
        bool rooms[128] = {}; int nRooms = 0;
        while (!stack.empty()) {
            const int i = stack.back(); stack.pop_back(); comp.push_back(i);
            const int r = roomOf(i); if (!rooms[r]) { rooms[r] = true; nRooms++; }
            int nb[4]; const int n = nbrs(i, nb);
            for (int j = 0; j < n; j++) if ((wwKind[nb[j]] & kWater) && dist[nb[j]] >= 3 && !seen[nb[j]]) { seen[nb[j]] = 1; stack.push_back(nb[j]); }
        }
        if (nRooms >= 2) grow.insert(grow.end(), comp.begin(), comp.end());
    }
    std::vector<uint8_t> step((size_t)GW * GH, 255);
    for (int i : grow) { step[i] = 0; wwKind[i] |= kBig; }
    for (int room : kWaveScreens)                                                                       // the screens listed by the user always wave
        for (int gy = (room >> 4) * R; gy < (room >> 4) * R + R; gy++) for (int gx = (room & 15) * 32; gx < (room & 15) * 32 + 32; gx++) if (wwKind[(size_t)gy * GW + gx] & kWater) wwKind[(size_t)gy * GW + gx] |= kBig;
    for (size_t h = 0; h < grow.size(); h++) {
        const int i = grow[h]; if (step[i] >= 3) continue;
        int nb[4]; const int n = nbrs(i, nb);
        for (int j = 0; j < n; j++) if ((wwKind[nb[j]] & kWater) && step[nb[j]] == 255) { step[nb[j]] = (uint8_t)(step[i] + 1); wwKind[nb[j]] |= kBig; grow.push_back(nb[j]); }
    }
}

void Widescreen::waterPass(const WaterJob& j) {
    perf::Scope perfScope(j.objects ? perf::kWaterB : perf::kWaterA);
    if (!wwReady || !fxIsShadow || j.scenery || j.computeMask) ensureWaterWorld();       // (a shadow's objects-only pass uses the grid the frame's other passes have just checked)
    const int w = j.w, h = j.h, top = j.top, y0 = j.firstRow >= 0 ? j.firstRow : j.top;
    const float t = (float)waterTick;
    std::vector<uint8_t>& mask = *j.mask;                    // 0 land, 1 water, 2 water of a big body, 3 unknown (fog / outside the world)
    auto kindAt = [&](int wx, int wy) -> uint8_t { return gridKind(wx >> 3, wy >> 3); };      // (the map's grid, beyond it the band of the extended world)
    auto near3 = [&](int gx, int gy, uint8_t bit) { for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (gridKind(gx + dx, gy + dy) & bit) return true; return false; };
    if (j.computeMask) {
        // The canvas rows cut into segments that lie in one world tile (what that tile is is looked up once per segment, not per pixel).
        struct Seg { int x0, x1, gx; uint8_t kind; bool valid, nearWater, nearBig, never, ladder; };
        const int gyMin = (y0 - top + j.camY) >> 3, gyMax = (h - 1 - top + j.camY) >> 3;
        std::vector<std::vector<Seg>> rows((size_t)std::max(0, gyMax - gyMin + 1));
        for (int gy = gyMin; gy <= gyMax; gy++) {
            auto& sg = rows[gy - gyMin];
            for (int x = 0; x < w;) {
                const int wx = x + j.camX, gx = wx >> 3, x1 = std::min(w, x + 8 - (wx & 7));
                Seg g{x, x1, gx, 0, false, false, false, false, false};
                bool known = false; const uint8_t gk = gridKind(gx, gy, &known);
                if (known) {
                    g.valid = true; g.kind = gk;
                    g.nearWater = (g.kind & kWater) || near3(gx, gy, kWater);
                    g.nearBig = near3(gx, gy, kBig);
                }
                sg.push_back(g); x = x1;
            }
        }
        auto rowOf = [&](int y) -> std::vector<Seg>& { return rows[((y - top + j.camY) >> 3) - gyMin]; };
        std::vector<std::pair<uint32_t, int>> hist;         // the water's blue = the commonest colour in the water tiles
        for (int y = y0; y < h; y += 3) for (const Seg& g : rowOf(y)) {
            if (!g.valid || !(g.kind & kWater)) continue;
            for (int x = g.x0; x < g.x1; x += 3) {
                const uint32_t c = j.px[(size_t)y * w + x]; if (!c) continue; bool f = false;
                for (auto& e : hist) if (e.first == c) { e.second++; f = true; break; }
                if (!f && hist.size() < 64) hist.push_back({c, 1});
            }
        }
        uint32_t blue = 0; int best = 0; for (auto& e : hist) if (e.second > best) { best = e.second; blue = e.first; }
        if (((blue >> 16 & 255) | (blue >> 8 & 255) | (blue & 255)) < 48) blue = 0;
        dbgBlue = blue;                // (a black/fading-in picture has no water yet)
        std::unordered_set<int> ladder;                                                              // a water-id tile with white in it is the striped ladder/stairs: scenery, not water
        for (int y = std::max(top, y0); y < h; y++) for (Seg& g : rowOf(y)) {
            if (!g.valid || !(g.kind & kWater) || g.ladder) continue;
            const int gy = (y - top + j.camY) >> 3;
            if (g.gx < 0 || g.gx >= GW || gy < 0 || gy >= GH) continue;                              // (no ladders beyond the map)
            for (int x = g.x0; x < g.x1 && !g.ladder; x++) {
                const uint32_t c = j.px[(size_t)y * w + x]; const int r = c >> 16 & 255, gg = c >> 8 & 255, b = c & 255;
                if (std::min({r, gg, b}) < 200 || std::max({r, gg, b}) - std::min({r, gg, b}) > 48) continue;
                g.ladder = true; ladder.insert(gy * GW + g.gx);
            }
        }
        for (int pass = 0; pass < 4 && !ladder.empty(); pass++) {                                    // the ladder's bare end tiles: water-id tiles between two walls, in line with it
            std::vector<int> add;
            for (int id : ladder) for (int dy : {-1, 1}) {
                const int gx = id % GW, gy = id / GW + dy;
                if (gy < 0 || gy >= GH || !(wwKind[(size_t)gy * GW + gx] & kWater) || ladder.count(gy * GW + gx)) continue;
                if (gx > 0 && gx < GW - 1 && !(wwKind[(size_t)gy * GW + gx - 1] & kWater) && !(wwKind[(size_t)gy * GW + gx + 1] & kWater)) add.push_back(gy * GW + gx);
            }
            if (add.empty()) break;
            ladder.insert(add.begin(), add.end());
        }
        for (int gy = gyMin; gy <= gyMax; gy++) for (Seg& g : rows[gy - gyMin]) g.never = g.valid && ((g.kind & kStairs) || (!ladder.empty() && g.gx >= 0 && g.gx < GW && gy >= 0 && gy < GH && ladder.count(gy * GW + g.gx)));
        mask.assign((size_t)w * h, 3);
        par::forRange(y0, h, 16, [&](int ya, int yb) { for (int y = ya; y < yb; y++) {
            uint8_t* mrow = &mask[(size_t)y * w]; const uint32_t* prow = &j.px[(size_t)y * w];
            for (const Seg& g : rowOf(y)) {
                if (!g.valid) continue;                                                              // (outside the world: unknown)
                if (g.never || (!g.nearWater && !(g.kind & kWater))) { for (int x = g.x0; x < g.x1; x++) if (prow[x]) mrow[x] = 0; continue; }
                const uint8_t land = 0, wat = g.nearBig ? 2 : 1;
                const bool inWater = (g.kind & kWater) != 0;
                for (int x = g.x0; x < g.x1; x++) {
                    const uint32_t c = prow[x];
                    if (!c) continue;
                    bool water = false;
                    if (blue && c == blue) water = g.nearWater;
                    else if (blue && inWater && x > 0 && x < w - 1 && y > y0 && y < h - 1)                  // a dot inside the water: blue on at least 3 sides
                        water = (prow[x - 1] == blue) + (prow[x + 1] == blue) + (j.px[(size_t)(y - 1) * w + x] == blue) + (j.px[(size_t)(y + 1) * w + x] == blue) >= 3;
                    mrow[x] = water ? wat : land;
                }
            }
        } });
    }
    // The picture as it was: the reflections read it while the water is written. A job that only mirrors objects writes nowhere a later reflection reads (they never land on a
    // sprite pixel, and sprite pixels are all they read), so the picture itself is the source: no copy.
    uint32_t* src = j.px;
    if (j.scenery) {
        if (wSrcBuf.size() < (size_t)w * h) wSrcBuf.resize((size_t)w * h);                       // (kept between calls: only the window the pass reads is copied into it)
        src = wSrcBuf.data();
        const ReadRect rr = readRect(w, h, y0, j.cx0, j.cy0, j.cx1, j.cy1);
        const int sx0 = rr.x0, sx1 = rr.x1, sy0 = rr.y0, sy1 = rr.y1;
        const bool haveRaw = j.rawSrc && j.rawSrc->size() == (size_t)w * h;        // what lies outside the screen the game drew: the plain terrain, as if there were no fog
        par::forRange(sy0, sy1, 16, [&](int ya, int yb) {
            for (int y = ya; y < yb; y++) {
                std::memcpy(&src[(size_t)y * w + sx0], &j.px[(size_t)y * w + sx0], sizeof(uint32_t) * (size_t)(sx1 - sx0));
                if (haveRaw) for (int x = sx0; x < sx1; x++) if ((x < j.cx0 || x >= j.cx1 || y < j.cy0 || y >= j.cy1) && (*j.rawSrc)[(size_t)y * w + x]) src[(size_t)y * w + x] = (*j.rawSrc)[(size_t)y * w + x];
            }
        });
    }
    const int rx0 = fxOx - j.camX, ry0 = fxOy - j.camY + top;                                   // the current screen on the canvas
    const bool refl = waterReflections, waves = waterWaves;                                           // (the two switches: End and Page Down)
    const Ppu* ppu = j.objects && refl ? lightPpu : nullptr;
    auto spriteAt = [&](int x, int y) {                                                          // a sprite pixel of the current screen (never the status bar's)
        if (!ppu) return false;
        const int lx = x - rx0, ly = y - ry0;
        return lx >= 0 && lx < 256 && ly >= 0 && ly < ROOM_H && ppu->sprCol[(ly + HUD_H) * Ppu::SPR_W + lx] != 0xFF;
    };

    // Things that move reflect about their feet (Link on stairs is part of the scenery: his reflection follows the stairs).
    std::vector<WaterObj> opix;
    std::vector<uint8_t>& linkMark = wLinkMark; linkMark.clear();                                                                 // Link's pixels when he counts as part of the stairs
    std::vector<uint8_t>& objSkip = wObjSkip; objSkip.clear();
    if (ppu) {
        if (j.scenery) objSkip.assign((size_t)w * h, 0);                                            // (only the scenery pass looks at it)
        std::vector<uint8_t> claimed((size_t)Ppu::SPR_W * Ppu::SPR_H, 0);
        const int linkFeet = feetOf(fxNes->ram[kObjY]);
        bool linkStairs = !fxIsShadow && (kindAt(fxOx + fxNes->ram[kObjX] + 8, fxOy + linkFeet - HUD_H - 2) & kFull) != 0;
        if (linkStairs) {                                                                          // ... but at the shore he reflects about his feet like anything else
            const int cxL = rx0 + fxNes->ram[kObjX] + 8, feetC = ry0 + (linkFeet - HUD_H);
            int dist = 99;
            if (cxL >= 0 && cxL < w) for (int yy = std::max(feetC, y0); yy < h && yy < feetC + 64; yy++) if (mask[(size_t)yy * w + cxL] == 1 || mask[(size_t)yy * w + cxL] == 2) { dist = yy - feetC; break; }
            linkStairs = dist >= kSquare;
            dbgLink[3] = dist;
        }
        dbgLink[0] = fxNes->ram[kObjX]; dbgLink[1] = fxNes->ram[kObjY]; dbgLink[2] = linkStairs;
        if (linkStairs) {
            linkMark.assign((size_t)w * h, 0);
            const int ox = fxNes->ram[kObjX], oy = fxNes->ram[kObjY];
            for (int py = std::max(HUD_H, oy - 2); py < std::min(Ppu::SPR_H, oy + 19); py++) for (int px = std::max(0, ox - 2); px < std::min(256, ox + 18); px++) {
                if (ppu->sprCol[(size_t)py * Ppu::SPR_W + px] == 0xFF) continue;
                const int cx = rx0 + px, cy = ry0 + (py - HUD_H);
                if (cx >= 0 && cx < w && cy >= y0 && cy < h) linkMark[(size_t)cy * w + cx] = 1;
            }
        }
        for (int s = 0; s <= kSlotRoomItem; s++) {
            const int ty = fxNes->ram[kObjType + s];
            if (!slotActive(fxNes->ram, s) || (linkStairs && (s == 0 || s == kSlotSword))) continue;
            if (fxIsShadow && (s == 0 || (s >= kSlotSword && s < kSlotRoomItem))) continue;                  // (a shadow's Link and weapons are not on its picture)
            const int ox = fxNes->ram[kObjX + s], oy = fxNes->ram[kObjY + s];
            const int feetNes = feetOf(s == kSlotSword ? fxNes->ram[kObjY] : oy);
            const int m = s >= kSlotSword ? 12 : 2;
            std::vector<WaterObj> mine; int maxPy = -1;                                              // (this object's pixels, and the lowest row it draws)
            for (int py = std::max(0, oy - m); py < std::min(Ppu::SPR_H, oy + 17 + m); py++)
                for (int px = std::max(0, ox - m); px < std::min(256, ox + 16 + m); px++) {
                    const size_t id = (size_t)py * Ppu::SPR_W + px;
                    if (py < HUD_H || claimed[id] || ppu->sprCol[id] == 0xFF) continue;
                    if (s == 0 && ppu->sprOam[id] < 64 && ppu->sprOam[id] != kLinkOam0 && ppu->sprOam[id] != kLinkOam1) continue;   // (the sword in front of him is not part of his mirror image: its slot's turn)
                    claimed[id] = 1; maxPy = std::max(maxPy, py);
                    const int cx = rx0 + px, cy = ry0 + (py - HUD_H);
                    if (cx < 0 || cx >= w || cy < y0 || cy >= h) continue;
                    if (j.scenery) objSkip[(size_t)cy * w + cx] = 1;
                    const bool shot = slotIsShot(fxNes->ram, s);
                    mine.push_back({cx, cy, ry0 + (feetNes - HUD_H), !shot});     // (shots are copied, not mirrored: an arrow keeps pointing the way it flies)
                }
            if (maxPy >= 0 && ((s >= 1 && s <= 12 && ty == kTypeItem) || s == kSlotRoomItem)) {                   // an item (a dropped heart fills only the top half of its 16 rows) stands on its lowest row
                const int f = ry0 + (maxPy + 1 - HUD_H);
                for (WaterObj& o : mine) o.feet = f;
            }
            opix.insert(opix.end(), mine.begin(), mine.end());
        }
    }

    if (j.extra && refl) opix.insert(opix.end(), j.extra->begin(), j.extra->end());
    // The part of the picture that is worked on: the clip plus what its reflections and waves read (a pass for one screen of a big canvas is cheap).
    const int wx0 = std::max(0, j.cx0 - kShare - 16), wx1 = std::min(w, j.cx1 + kShare + 16);
    const int wy0 = std::max(y0, j.cy0 - 128), wy1 = std::min(h, j.cy1 + 16);

    // Waves fade out over 12 px where a big body meets a river or pond: the share of big water among the water around the pixel, on a grid of 4 px cells.
    constexpr int kCell = 4, kSm = 3;
    const int gw = (w + kCell - 1) / kCell, gh = (h + kCell - 1) / kCell;
    std::vector<int>& iA = wIA; std::vector<int>& iB = wIB; bool anyBig = false;
    if (j.scenery && waves) {
        for (int y = wy0; y < wy1 && !anyBig; y++) for (int x = wx0; x < wx1; x++) if (mask[(size_t)y * w + x] == 2) { anyBig = true; break; }
        if (anyBig) {
            iA.assign((size_t)(gw + 1) * (gh + 1), 0); iB.assign(iA.begin(), iA.end());
            par::forRange(wy0 / kCell, (wy1 + kCell - 1) / kCell, 8, [&](int ca, int cb) {                  // (a thread's rows of cells are its own)
                for (int y = std::max(wy0, ca * kCell); y < std::min(wy1, cb * kCell); y++) for (int x = wx0; x < wx1; x++) {
                    const uint8_t m = mask[(size_t)y * w + x];
                    if (m == 2) { iA[(size_t)(y / kCell + 1) * (gw + 1) + x / kCell + 1]++; iB[(size_t)(y / kCell + 1) * (gw + 1) + x / kCell + 1]++; }
                    else if (m == 1) iB[(size_t)(y / kCell + 1) * (gw + 1) + x / kCell + 1]++;
                }
            });
            for (int cy = 1; cy <= gh; cy++) for (int cx = 1; cx <= gw; cx++) {                      // (summed table)
                const size_t i = (size_t)cy * (gw + 1) + cx;
                iA[i] += iA[i - 1] + iA[i - (gw + 1)] - iA[i - (gw + 1) - 1]; iB[i] += iB[i - 1] + iB[i - (gw + 1)] - iB[i - (gw + 1) - 1];
            }
        }
    }
    auto bigShare = [&](int x, int y) {
        const int cx = x / kCell, cy = y / kCell;
        const int x0 = std::max(0, cx - kSm), x1 = std::min(gw, cx + kSm + 1), yy0 = std::max(0, cy - kSm), yy1 = std::min(gh, cy + kSm + 1);
        auto box = [&](const std::vector<int>& I) { return I[(size_t)yy1 * (gw + 1) + x1] - I[(size_t)yy0 * (gw + 1) + x1] - I[(size_t)yy1 * (gw + 1) + x0] + I[(size_t)yy0 * (gw + 1) + x0]; };
        const int b = box(iB); return b > 0 ? (float)box(iA) / b : 0.0f;
    };
    // Scenery. Every water pixel gets a mirror axis: the top edge of the water tile where its column's water starts (straight, also at rounded
    // corners), shared with the neighbouring columns of the same body (the highest within 20 px) so a rim column that starts lower continues the
    // reflection of its neighbours. A shore needs 3+ land pixels above it.
    std::vector<int16_t>& axisOwn = wAxis;                                                           // (kept between frames: only the worked-on part is reset)
    if (j.scenery && refl) {                                                                         // (without reflections nothing reads the axes)
        if (axisOwn.size() < (size_t)w * h) axisOwn.assign((size_t)w * h, INT16_MIN);
        for (int y = wy0; y < wy1; y++) { std::fill(axisOwn.begin() + (size_t)y * w + wx0, axisOwn.begin() + (size_t)y * w + wx1, INT16_MIN); }
        std::vector<int> landRun(w, 0), axisC(w, 0); std::vector<uint8_t> have(w, 0);
        par::forRange(wx0, wx1, 128, [&](int xa, int xb) {                                           // (every column has its own state: columns go to different threads)
        for (int y = wy0; y < wy1; y++) {
            const uint8_t* mr = &mask[(size_t)y * w];
            for (int x = xa; x < xb; x++) {
                const uint8_t m = mr[x];
                if (m == 0) { landRun[x]++; continue; }
                if (m == 3) { landRun[x] = 0; have[x] = 0; continue; }
                if (landRun[x] >= 3) { axisC[x] = y - ((y - top + j.camY) & 7); have[x] = 1; }
                if (have[x]) axisOwn[(size_t)y * w + x] = (int16_t)axisC[x];
                landRun[x] = 0;
            }
        }
        });
    }
    // The reflection of a wall (or stairs, or a tree) always starts at its BASE. Walking up from the axis: the first tall thing (full land) found
    // is the base. Whole squares (16 px) between the base and the shore: 0 = the base is in the shore square, 1, 2, 3+ = too far, nothing.
    // The water shows the wall from the base up, 40 px at most counted from the base: with the base in the shore square it shows 40 px, one square
    // away it skips the base square and shows the next 24 px, two squares away the next 8 px. A tree (one square, whatever stands behind it) only
    // reflects when its base is in the shore square, and only itself.
    struct Base { int axis = INT_MIN, yb = -1, gsq = 0; bool tree = false, ok = false; };
    auto baseFor = [&](std::vector<Base>& baseCache, int x, int axis) -> const Base& {      // (baseCache: per column the base for the axis last asked for, one per thread)
        Base& b = baseCache[x];
        if (b.axis == axis) return b;
        b = Base(); b.axis = axis;
        for (int yy = axis - 1; yy >= y0 && yy >= axis - 3 * kSquare; yy--) {
            if (mask[(size_t)yy * w + x] != 0) break;
            if (kindAt(x + j.camX, yy - top + j.camY) & kFull) { b.yb = yy; break; }                // (a sprite in front of the wall does not change where its base is)
        }
        if (b.yb >= 0) {
            b.gsq = (axis - 1 - b.yb) / kSquare;
            const int tx = (x + j.camX) >> 3, ty = (b.yb - top + j.camY) >> 3, by = ty | 1;                      // the square's bottom tiles
            auto openAt = [&](int gx, int gy) { return (gridKind(gx, gy) & kOpen) != 0; };
            auto stairsAt = [&](int gx, int gy) { return (gridKind(gx, gy) & kStairs) != 0; };
            b.tree = openAt(tx & ~1, by) && openAt(tx | 1, by) && !stairsAt(tx & ~1, by) && !stairsAt(tx | 1, by);   // a tree is round: open at both bottom corners
            b.ok = b.tree ? b.gsq == 0 : b.gsq < 3;
        }
        return b;
    };
    struct Run { int x0, x1; int16_t v; };
    std::vector<float> crestIn(w, 0.0f);                                                             // (the part of the crest wave that depends on x only)
    for (int x = j.cx0; x < j.cx1; x++) crestIn[x] = std::sin((x + j.camX) * 0.03f - t * 0.015f) * 2.0f;
    fsin(0.0f);                                                                                      // (builds the sine table before the threads read it)
    auto doRows = [&](int ya, int yb) {
    std::vector<Base> baseCache(w); std::vector<Run> runs; std::vector<int16_t> shared(w);             // (everything a row changes belongs to the thread)
    int shareCx = -1, shareCy = -1; float shareV = 0.0f;                                              // (the wave share only changes from cell to cell)
    for (int y = ya; y < yb; y++) {
        const uint8_t* mr = &mask[(size_t)y * w];
        const float wyf = (float)(y - top + j.camY), waveY1 = std::sin(wyf * 0.11f + t * 0.02f) * 1.6f, waveY2 = wyf * 0.19f - t * 0.045f, ripY = std::sin(wyf * 0.55f + t * 0.09f);   // (per row)
        // the shared axis of this row: runs of equal axes, each pixel takes the highest axis of the runs within 20 px (that is at most 16 px higher)
        int waveX = -9; float waveK = 0.0f;
        runs.clear();
        for (int x = wx0; x < wx1;) {
            if (!refl) break;
            const int16_t v = axisOwn[(size_t)y * w + x]; int x1 = x + 1;
            while (x1 < wx1 && axisOwn[(size_t)y * w + x1] == v) x1++;
            if (v != INT16_MIN) runs.push_back({x, x1, v});
            x = x1;
        }
        for (size_t r = 0; r < runs.size(); r++) for (int x = std::max(runs[r].x0, j.cx0); x < std::min(runs[r].x1, j.cx1); x++) {
            int a = runs[r].v;
            for (size_t q = r; q-- > 0 && runs[q].x1 > x - kShare;) if (runs[q].v < a && runs[r].v - runs[q].v <= 16) a = runs[q].v;
            for (size_t q = r + 1; q < runs.size() && runs[q].x0 <= x + kShare; q++) if (runs[q].v < a && runs[r].v - runs[q].v <= 16) a = runs[q].v;
            shared[x] = (int16_t)a;
        }
        for (int x = j.cx0; x < j.cx1; x++) {
            const uint8_t m = mr[x];
            if (m != 1 && m != 2) continue;
            if (spriteAt(x, y)) continue;
            const int wx = x + j.camX, wy = y - top + j.camY;
            const uint32_t px0 = src[(size_t)y * w + x];
            int r = px0 >> 16 & 255, g = px0 >> 8 & 255, b = px0 & 255;
            if (anyBig && (x / kCell != shareCx || y / kCell != shareCy)) { shareCx = x / kCell; shareCy = y / kCell; shareV = bigShare(x, y); }
            const float share = anyBig ? shareV : 0.0f;
            if (share > 0.01f) {                                                                   // waves: slow bands of light and dark and thin bright crests
                float k;
                if ((x & 1) && waveX == x - 1) k = waveK;                                           // (the waves are smooth: every second pixel of a row shares the value of its left neighbour)
                else {
                    const float sw = fsin(wx * 0.05f + t * 0.035f + waveY1);
                    const float sw2 = fsin(waveY2 + wx * 0.03f);
                    k = 0.05f * sw + 0.03f * sw2;
                    const float crest = fsin(wx * 0.09f + wy * 0.21f - t * 0.06f + crestIn[x]);
                    if (crest > 0.93f) k += (crest - 0.93f) * 3.2f;
                    waveK = k; waveX = x;
                }
                k = 1.0f + k * share;
                r = std::min(255, (int)(r * k)); g = std::min(255, (int)(g * k)); b = std::min(255, (int)(b * k + (k > 1.0f ? (k - 1.0f) * 80 * (b / 255.0f) : 0)));
            }
            const bool hasAxis = refl && axisOwn[(size_t)y * w + x] != INT16_MIN;
            if (share <= 0.01f && !hasAxis) continue;                   // (nothing to do for this pixel)
            if (hasAxis) {
                const int axis = shared[x], d = y - axis;
                auto blend = [&](int sy, int sx, float k) {
                    uint32_t sp = src[(size_t)sy * w + sx];
                    if (!objSkip.empty() && objSkip[(size_t)sy * w + sx] && j.rawSrc && (*j.rawSrc)[(size_t)sy * w + sx]) sp = (*j.rawSrc)[(size_t)sy * w + sx];   // (behind a sprite: the terrain)
                    const int sr = (int)((sp >> 16 & 255) * 0.80f), sg = (int)((sp >> 8 & 255) * 0.85f), sb = (int)((sp & 255) * 0.95f);   // (a little darker and bluer)
                    r = (int)(r + (sr - r) * k); g = (int)(g + (sg - g) * k); b = (int)(b + (sb - b) * k);
                };
                const float rip = ripY * (m == 2 ? 1.1f : 0.6f) * (0.3f + std::min(d, 24) / 24.0f);
                const int rx = x + (int)std::lround(rip);
                auto wallAt = [&](int sy, int sx) {                                                   // a reflecting source pixel: wall land (tile based)
                    if (sy < y0 || sx < 0 || sx >= w || mask[(size_t)sy * w + sx] != 0) return false;
                    return (kindAt(sx + j.camX, sy - top + j.camY) & kFull) != 0;
                };
                const Base& bs = baseFor(baseCache, x, axis);
                const int win = bs.tree ? kSquare : kReflectLen - bs.gsq * kSquare;
                if (bs.ok && d >= 0 && d < win) {                                                       // walls, stairs and trees, from their base up
                    const int sy = bs.yb - bs.gsq * kSquare - d;
                    int sx = x; bool okp = true;
                    if (wallAt(sy, rx)) sx = rx; else if (!wallAt(sy, x)) okp = false;
                    if (okp) {
                        float kk = kReflectMax * (1.0f - (float)d / win);
                        if (!linkMark.empty() && linkMark[(size_t)sy * w + sx]) kk = std::max(kk, 0.5f * (1.0f - (float)d / 48.0f));   // Link on the stairs: clearly visible
                        blend(sy, sx, kk);
                    }
                }
            }
            j.px[(size_t)y * w + x] = (px0 & 0xFF000000u) | (uint32_t)std::clamp(r, 0, 255) << 16 | (uint32_t)std::clamp(g, 0, 255) << 8 | (uint32_t)std::clamp(b, 0, 255);
        }
    }
    };
    if (j.scenery) par::forRange(std::max(y0, j.cy0), std::min(h, j.cy1), 8, doRows);
    // Objects: each sprite pixel is mirrored about the feet and drawn where that lands on water.
    for (const WaterObj& o : opix) {
        const int mm = o.flip ? o.feet - 1 - o.y : o.y - (o.feet - 16);                       // flipped: 0 at the feet .. 15 at the head; copied: 0 at the top
        const int ty = o.feet + mm, m = ty - o.feet;
        const float rip = std::sin(ty * 0.55f + t * 0.09f) * 0.5f * (m / 16.0f);
        const int tx = o.x + (int)std::lround(rip);
        if (tx < j.cx0 || tx >= j.cx1 || ty < j.cy0 || ty >= j.cy1 || ty >= h) continue;
        if (mask[(size_t)ty * w + tx] < 1 || mask[(size_t)ty * w + tx] > 2 || spriteAt(tx, ty)) continue;
        const float k = 0.5f * (1.0f - 0.5f * (float)m / 16.0f);
        const int srow = o.y - j.rowShift, drow = ty - j.rowShift;                                // (rows of the picture the buffer holds; a pixel in the margin above the canvas has none)
        if (srow < 0 || drow < 0) continue;
        const uint32_t s = src[(size_t)srow * w + o.x], dpx = j.px[(size_t)drow * w + tx];
        const int sr = (int)((s >> 16 & 255) * 0.85f), sg = (int)((s >> 8 & 255) * 0.9f), sb = (int)((s & 255) * 0.97f);
        const int r = (int)((dpx >> 16 & 255) + (sr - (int)(dpx >> 16 & 255)) * k), g = (int)((dpx >> 8 & 255) + (sg - (int)(dpx >> 8 & 255)) * k), b = (int)((dpx & 255) + (sb - (int)(dpx & 255)) * k);
        j.px[(size_t)drow * w + tx] = (dpx & 0xFF000000u) | (uint32_t)std::clamp(r, 0, 255) << 16 | (uint32_t)std::clamp(g, 0, 255) << 8 | (uint32_t)std::clamp(b, 0, 255);
    }
}
