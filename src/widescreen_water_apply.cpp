// Water: the wrappers around the water passes: pass B (the game's own picture of the screen), the water behind a cave's box, the pass for
// everything else, and the debug dump (`Home`). The passes themselves are in widescreen_water.cpp.
#include "widescreen.h"
#include "widescreen_internal.h"
#include "parallel.h"
#include "perf.h"
#include "zelda_ram.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace zelda;
using namespace wd;

namespace {
constexpr int R = OverworldMap::TILE_ROWS;
}

// Pass B: the game's own picture of this screen (which holds the sprites) gets the water effects, from the terrain above it and the mask of pass A.
void Widescreen::roomWater(Canvas& c, int camX, int camY, bool objects, bool scenery) {
    const int M = kWaterMargin, PW = c.w, PH = c.h;
    const int rx0 = fxOx - camX, ry0 = fxOy - camY + topBand + M;
    WaterJob j{nullptr, PW, PH + M, topBand + M, camX, camY, std::max(rx0, 0), std::max(ry0, topBand + M), std::min(rx0 + ROOM_W, PW), std::min(ry0 + ROOM_H, PH + M), &terrainMask, false, objects, nullptr, scenery, &terrainRaw, 0};
    // The picture with the plain terrain above it. The pass only reads what readRect says (the screen it works on and what its reflections and
    // waves look at), so only that is copied: the rest of the buffer is never looked at.
    std::vector<uint32_t>& ext = wExt;
    const size_t need = (size_t)PW * (PH + M);
    if (ext.size() != need) ext.assign(need, 0);
    const ReadRect rr = readRect(PW, PH + M, j.firstRow >= 0 ? j.firstRow : j.top, j.cx0, j.cy0, j.cx1, j.cy1);          // (the same first row as waterPass uses)
    const int ey0 = rr.y0, ey1 = rr.y1, ex0 = rr.x0, ex1 = rr.x1;
    for (int y = ey0; y < ey1 && ex0 < ex1; y++)
        std::memcpy(&ext[(size_t)y * PW + ex0], y < M + topBand ? &terrainRaw[(size_t)y * PW + ex0] : &c.pixels[(size_t)(y - M) * PW + ex0], sizeof(uint32_t) * (size_t)(ex1 - ex0));
    j.px = ext.data();
    waterPass(j);
    for (int y = std::max(j.cy0, topBand + M); y < j.cy1; y++) for (int x = j.cx0; x < j.cx1; x++) c.pixels[(size_t)(y - M) * PW + x] = ext[(size_t)y * PW + x];
}

// The backdrop behind a cave's box: the water passes run again on the terrain and the room's picture as they were before the water (recorded when the
// backdrop was made), with the waves of the present: waves, ripples and reflections keep moving while Link is in a cave or shop.
void Widescreen::redoBackdropWater(Canvas& c) {
    const int W = c.w, H = c.h, M = kWaterMargin;
    const size_t need = (size_t)W * (H + M);
    if (!enabled || !waterOn() || !backdropWaterOk || terrainW != W || terrainCamX != backdropCamX || terrainCamY != backdropCamY || terrainMask.size() != need ||
        backdropRawPre.size() != need || backdropVis.size() != need) return;
    waterTick++;
    std::vector<uint32_t> tmp = backdropRawPre;
    WaterJob jA{tmp.data(), W, H + M, backdropTop + M, backdropCamX, backdropCamY, 0, backdropTop + M, W, H + M, &terrainMask, false, false, nullptr, true, nullptr, 0};
    waterPass(jA);
    for (int y = backdropTop; y < H; y++) {                                  // the fog over the terrain, as drawWorld mixes it
        const int wy = backdropCamY + (y - backdropTop);
        uint32_t* row = &c.pixels[(size_t)y * W];
        for (int x = 0; x < W; x++) {
            const size_t id = (size_t)(y + M) * W + x; const int v = backdropVis[id];
            if (v == 0) continue;
            row[x] = v == 256 ? tmp[id] : mixColor(scaleColor(fogPixel(backdropCamX + x, wy), backdropFogScale), tmp[id], v);
        }
    }
    const int rx0 = backdropRoomRect[0], ry0 = backdropRoomRect[1], rx1 = backdropRoomRect[2], ry1 = backdropRoomRect[3];
    if (!backdropRoomPre.empty() && rx1 > rx0 && ry1 > ry0) {                // the game's picture of the room, then pass B on it
        for (int y = ry0; y < ry1; y++) for (int x = rx0; x < rx1; x++) c.pixels[(size_t)y * W + x] = backdropRoomPre[(size_t)(y - ry0) * (rx1 - rx0) + (x - rx0)];
        terrainRaw = std::move(tmp);
        const int tb = topBand, ox = fxOx, oy = fxOy; topBand = backdropTop; fxOx = backdropFx[0]; fxOy = backdropFx[1];
        roomWater(c, backdropCamX, backdropCamY, false);
        topBand = tb; fxOx = ox; fxOy = oy;
    }
}

// Pass for pictures that did not come through drawWorld (cave backdrop is baked, the rest): the whole canvas, colours from the picture itself.
void Widescreen::applyWater(Canvas& c, int top, int camX, int camY) {
    if (!waterOn() || !fxNes || !enabled) return;
    waterTick++;
    std::vector<uint8_t> mask;
    WaterJob j{c.pixels.data(), c.w, c.h, top, camX, camY, 0, top, c.w, c.h, &mask, true, false};
    waterPass(j);
}

// Writes the tile ids and the water flags of the current screen (and the wall/ladder/dock flags) to a text file: Home key.
void Widescreen::dumpWaterDebug(const char* path) {
    if (!fxNes || fxRoom < 0) return;
    if (fxNes->ram[kCurLevel] != 0) {                                          // in a dungeon: the room's tile ids (the moat is $F4)
        FILE* d = std::fopen(path, "w"); if (!d) return;
        std::fprintf(d, "dungeon level %d room %02X, mode %d\ntile ids of PlayAreaTiles (rows top to bottom, 32 columns); moat = F4\n", fxNes->ram[kCurLevel], fxRoom, fxNes->ram[kGameMode]);
        for (int r = 0; r < R; r++) { for (int c = 0; c < 32; c++) std::fprintf(d, "%02X ", fxNes->cart.cpuRead((uint16_t)(kPlayAreaTiles + c * R + r))); std::fprintf(d, "\n"); }
        std::fprintf(d, "last water colour %08X (0 = none found); reflections %d, waves %d\n", dbgBlue, (int)waterReflections, (int)waterWaves);
        std::fclose(d); return;
    }
    ensureWaterWorld();
    FILE* f = std::fopen(path, "w"); if (!f) return;
    const uint8_t* tiles = roomTiles(*fxNes, fxRoom);
    std::fprintf(f, "screen column %d row %d\ntile ids (rows top to bottom, 32 columns)\n", fxRoom & 15, fxRoom >> 4);
    for (int r = 0; r < R; r++) { for (int c = 0; c < 32; c++) std::fprintf(f, "%02X ", tiles[c * R + r]); std::fprintf(f, "\n"); }
    std::fprintf(f, "Link: ObjX %d ObjY %d, scenery-on-stairs %d, distance of his feet to the water %d\n", dbgLink[0], dbgLink[1], dbgLink[2], dbgLink[3]);
    int nWater = 0, nOpen = 0; for (uint8_t k : wwKind) { nWater += k & kWater; nOpen += (k & kOpen) != 0; }
    std::fprintf(f, "grid: %d water tiles, %d open tiles, signature %016llx; last water colour %08X (0 = none found); reflections %d, waves %d, enabled %d, fog %d, pattern table %04X\n", nWater, nOpen, (unsigned long long)wwSig, dbgBlue, (int)waterReflections, (int)waterWaves, (int)enabled, (int)fog, (fxNes->ppu.lineCtrl[100] & 0x10) ? 0x1000 : 0);
    std::fprintf(f, "flags (1 water, 2 big, 4 wall, 8 stairs/dock, 10 open)\n");
    for (int r = 0; r < R; r++) { for (int c = 0; c < 32; c++) std::fprintf(f, "%02X ", wwKind[(size_t)((fxRoom >> 4) * R + r) * GW + (fxRoom & 15) * 32 + c]); std::fprintf(f, "\n"); }
    std::fclose(f);
}
