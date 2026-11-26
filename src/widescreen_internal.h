#pragma once
// Shared between the widescreen_*.cpp files: constants and small helpers (no behaviour; moved from widescreen.cpp).
#include "widescreen.h"
#include "zelda_ram.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wd {

// The canvas is as wide as the window's shape asks for (an even number of pixels), within these limits.
constexpr int kMinCanvasW = 512, kMaxCanvasW = 2048;
inline int canvasWidth(int h, double windowAspect, int minW = kMinCanvasW) { return std::clamp((int)std::lround(h * windowAspect) & ~1, minW, kMaxCanvasW); }
// Which object slots own sprite pixels (the water pass and the dungeon's frozen replay use the same boxes): Link always, a monster or item while its ObjType is set,
// a weapon while its ObjState is, the room item until it is taken.
inline bool slotActive(const uint8_t* ram, int s) {
    const int st = ram[zelda::kObjState + s], ty = ram[zelda::kObjType + s];
    return s == 0 ? true : s <= zelda::kLastMonsterSlot ? ty != 0 : s < zelda::kSlotRoomItem ? st != 0 : (!(st & 0x80) && ram[zelda::kRoomItemId] != zelda::kRoomItemNone);
}
// Shots (Link's sword shot and beam, arrows, the boomerang, the monsters' shots and magic; not the fireballs) are COPIED into a reflection, not mirrored: an arrow keeps pointing the way it flies.
inline bool slotIsShot(const uint8_t* ram, int s) {
    const int ty = ram[zelda::kObjType + s];
    const bool monsterShot = ty >= zelda::kTypeFirstProjectile && ty <= zelda::kTypeLastProjectile && ty != zelda::kTypeFireballA && ty != zelda::kTypeFireballB && ty != 0x5A;   // ($5A: no object has it)
    return s == zelda::kSlotSword || s == zelda::kSlotShot || s == zelda::kSlotBoomerang || s == zelda::kSlotLastWeapon || (s >= 1 && s <= zelda::kLastMonsterSlot && monsterShot);
}
constexpr int kMazeDim = 120, kMazeHaze = 70;                    // the maze's presentation (tuned by feel): the dimming (of 256) and the waving haze's strength
constexpr int kMistLayerH = 270, kMistLayerMinW = 256, kMistLayerMaxW = 960;   // the transitions' mist: one window-shaped layer, stretched over everything by main.cpp
bool isWaterTile(uint8_t t);                           // widescreen_water.cpp: overworld water tile (not the walkable specials)
constexpr int ROOM_W = 256, ROOM_H = 176, HUD_H = 64, FRAME_H = 240;
constexpr int CAVE_MISMATCH_THRESHOLD = 250;   // of 704 tiles: more than this means "not a normal overworld screen"

// Water (widescreen_water.cpp, widescreen_dungeon.cpp): the whole world in tiles, the per-tile flags, the size of a reflection square.
constexpr int GW = 16 * 32, GH = 8 * 22;
enum : uint8_t { kWater = 1, kBig = 2, kFull = 4, kStairs = 8, kOpen = 16 };   // kOpen: a tile with plenty of background in it (trees, not wall)
constexpr int kSquare = 16;
inline int feetOf(int objY) { return objY + 17; }       // the sprite's rows are Y+1 .. Y+16: its feet end at Y+17 (screen rows, status bar included)

inline uint32_t fogPixel(int wx, int wy) {
    // Deep fog: almost black with a faint blue tint and a little grain.
    unsigned h = ((unsigned)wx * 73856093u) ^ ((unsigned)wy * 19349663u);
    h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
    const int n = (int)(h & 3) - 1;                 // -1..2
    return 0xFF000000 | ((5 + n) << 16) | ((6 + n) << 8) | (11 + n * 2);
}

// The eight neighbours of a room (dx, dy); bit k of a fogged room's mask = that neighbour is explored.
constexpr int kFogNb[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
constexpr int kFogDepth = 37;      // the fog thins out this many world pixels from an explored room
constexpr int kUnfogFrames = 32;   // length of the unfogging animation of a newly entered room (the glide between explored rooms is ~16 frames, the game's own scroll 64)
constexpr int kUnfogFeather = 40;  // softness of the reveal front, in world pixels
constexpr int kFogEdge = 256;      // terrain visible (of 256) right at the border of an explored room: fully clear

constexpr int kFogDepthWall = 12;   // where the border to an explored room is blocked (walls, trees, rocks...) the fog starts this close

// Fog depth (px) along each straight border, per 8x8 tile: [0] north, [1] west, [2] east, [3] south. kFogDepth where Link could
// walk over, kFogDepthWall where the tiles are blocked. Between the tile centres the depth is interpolated.
struct FogDepths { uint8_t d[4][32]; uint8_t diagX[4], diagY[4]; uint8_t endLow[4][2]; };   // endLow: depth of the glow beyond the low / high end of a border (0 = nothing known: the border simply ends)   // corners NW, NE, SW, SE: the fog depth horizontally / vertically (an ellipse)

// Which tiles of an overworld room can Link walk over (tiles[col * 22 + row], out likewise)? The game blocks tiles >= $89 (ObjectRoomBoundsOW,
// Z_05.asm 6438; tiles $84-$88 are the sand of the desert screens; the pond animation raises the limit to $99 to make water walkable;
// a few special tiles, stairs and doors among them, are walkable too).
// Water ($89-$98) counts as walkable only where its body touches ground Link can really stand on (bridges, shores); a lake with
// only walls around it stays blocked.
inline void owWalkMask(const uint8_t* tiles, bool* out) {
    static const uint8_t special[] = {0x8D, 0x91, 0x9C, 0xAC, 0xAD, 0xCC, 0xD2, 0xD5, 0xDF};
    constexpr int R = 22, N = 32 * 22;
    uint8_t kind[N];                                      // 0 blocked, 1 ground, 2 water
    for (int i = 0; i < N; i++) {
        const uint8_t t = tiles[i];
        bool sp = false;
        for (uint8_t v : special) sp = sp || v == t;
        kind[i] = sp || t < 0x89 ? 1 : t < 0x99 ? 2 : 0;
    }
    int queue[N], head = 0, tail = 0;
    bool seen[N] = {};
    auto nbrs = [&](int i, int* o) { int n = 0; const int c = i / R, r = i % R; if (c > 0) o[n++] = i - R; if (c < 31) o[n++] = i + R; if (r > 0) o[n++] = i - 1; if (r < R - 1) o[n++] = i + 1; return n; };
    for (int i = 0; i < N; i++) {
        out[i] = kind[i] == 1;
        if (kind[i] != 2) continue;
        int nb[4], n = nbrs(i, nb);
        for (int j = 0; j < n; j++) if (kind[nb[j]] == 1) { seen[i] = true; queue[tail++] = i; break; }
    }
    while (head < tail) {
        const int i = queue[head++];
        out[i] = true;
        int nb[4], n = nbrs(i, nb);
        for (int j = 0; j < n; j++) if (kind[nb[j]] == 2 && !seen[nb[j]]) { seen[nb[j]] = true; queue[tail++] = nb[j]; }
    }
}

// How far the light of one straight border reaches a point: 1 at the border, falling to 0 at the border's depth. Every 8-pixel
// stretch of the border has its own depth (deep where it is open, shallow where it is blocked). Where a deep stretch meets a
// shallower one the glow's edge does not step: it follows a quarter ellipse (a quarter circle of radius = the depth difference),
// so the dark corner of the shallow part curves inwards over the lit part. s = position along the border, p = distance from it.
inline double fogBorderStrength(const FogDepths* fd, int side, int n, double s, double p) {
    if (!fd) return p < kFogDepth ? 1.0 - p / kFogDepth : 0.0;
    const int i = std::clamp((int)(s / 8.0), 0, n - 1);
    const double D = fd->d[side][i];
    double depth = D;
    for (int dir = -1; dir <= 1; dir += 2) {
        for (int k = 1; k <= 5; k++) {
            const int j = i + dir * k;
            double low;
            if (j < 0 || j >= n) {                                       // past the room's end: a dark corner there continues the profile
                const int el = fd->endLow[side][j < 0 ? 0 : 1];
                if (!el || el >= D) break;
                low = el;
            } else {
                low = fd->d[side][j];
                if (low >= D) continue;
            }
            const double u = dir < 0 ? s - (j + 1) * 8.0 : j * 8.0 - s;   // distance from s to the end of the deep stretch
            const double r = D - low, t = std::min(1.0, std::max(0.0, u / r));
            depth = std::min(depth, low + r * std::sqrt(1.0 - (1.0 - t) * (1.0 - t)));
            break;
        }
    }
    return p < depth ? 1.0 - p / depth : 0.0;
}

// 0..256: how much terrain shows through the fog at (lx, ly) inside a fogged room (0 = full fog).
// Each explored neighbour contributes by the distance to its rectangle (so a diagonal neighbour makes a
// round corner) and the contributions are combined like independent layers (so corners blend smoothly).
// A diagonal neighbour is left out when exactly one of the two rooms beside it is explored (then the straight border
// of that room already shapes the fog); alone, or with both of them, it rounds the corner.
// Corner neighbour k (0 NW, 2 NE, 5 SW, 7 SE) casts a corner patch only when both of its orthogonal rooms are explored or neither is.
inline bool fogDiagActive(unsigned nb, int k) {
    if (!(nb & (1u << k))) return false;
    const unsigned v = 1u << (k < 5 ? 1 : 6), h = 1u << (k == 0 || k == 5 ? 3 : 4);
    return ((nb & v) != 0) == ((nb & h) != 0);
}
// Does the corner patch of neighbour k sit on the seam at the left (axis 0, low) / right (0, !low) / top (1, low) / bottom (1, !low)?
inline bool fogDiagOnSeam(int k, int axis, bool low) {
    const bool west = k == 0 || k == 5, north = k == 0 || k == 2;
    return axis == 0 ? west == low : north == low;
}
// A dark corner (the corner room beside an explored one, reached only by a corner patch) is blended into the rooms beside it only when
// both of those are lit there (glow deeper than a wall's); with one dark neighbour the corner patch already matches them.
inline bool fogCornerLit(int depthV, int depthH) { return depthV > kFogDepthWall && depthH > kFogDepthWall; }
// The ground colour of a 16x16 square, from its four corner pixels (a tree, rock or gravestone never fills the corners).
template <class T> inline T squareGround(T c0, T c1, T c2, T c3) { return (c1 == c2 || c1 == c3) ? c1 : c2 == c3 ? c2 : c0; }
inline int fogVisibility(unsigned nb, int lx, int ly, int weakBit = -1, double weakW = 1.0, const FogDepths* fd = nullptr) {   // (weakBit: neighbour whose contribution is scaled by weakW)
    if (!nb) return 0;
    double hidden = 1.0;
    for (int k = 0; k < 8; k++) {
        if (!(nb & (1u << k))) continue;
        if (k == 0 || k == 2 || k == 5 || k == 7) {
            if (!fogDiagActive(nb, k)) continue;
        }
        const int ox = kFogNb[k][0], oy = kFogNb[k][1];
        const double dx = ox < 0 ? lx + 0.5 : ox > 0 ? ROOM_W - lx - 0.5 : 0;
        const double dy = oy < 0 ? ly + 0.5 : oy > 0 ? ROOM_H - ly - 0.5 : 0;
        const double d = std::sqrt(dx * dx + dy * dy);
        double depth = kFogDepth;
        double dist = d;
        double f = -1;                                       // (set by the straight borders: strength 0..1)
        if (k == 0 || k == 2 || k == 5 || k == 7) {
            // a corner patch is an ellipse: horizontally it is as deep as the vertical border beside it, vertically as the horizontal one
            if (fd) {
                const int c = k == 0 ? 0 : k == 2 ? 1 : k == 5 ? 2 : 3;
                dist = std::sqrt((dx / fd->diagX[c]) * (dx / fd->diagX[c]) + (dy / fd->diagY[c]) * (dy / fd->diagY[c])) * kFogDepth;
            }
        }
        else if (k == 1 || k == 6) f = fogBorderStrength(fd, k == 1 ? 0 : 3, 32, lx + 0.5, dy);
        else if (k == 3 || k == 4) f = fogBorderStrength(fd, k == 3 ? 1 : 2, 22, ly + 0.5, dx);
        if (f < 0) { if (dist >= depth) continue; f = 1.0 - dist / depth; }
        else if (f <= 0) continue;
        hidden *= 1.0 - (kFogEdge / 256.0) * f * f * (k == weakBit ? weakW : 1.0);
    }
    return (int)((1.0 - hidden) * 256.0 + 0.5);
}
// The glow of two fogged rooms is blended over their seam: t = distance from the seam (pixels) on the own side, `own` this room's value
// and `other` what the neighbour has at the seam. At the seam both sides get the average, kSeam pixels away nothing changes.
constexpr int kSeam = 16;
inline double seamBlend(double own, double other, double t) {
    const double w = 0.5 * (1.0 - t / kSeam);
    return w <= 0 ? own : own * (1.0 - w) + other * w;
}
constexpr int kFlashRadius = 200, kFlashFeather = 48;   // the bomb flash covers a circle this big around the bomb (world pixels), fading out over the last few

// 0..256: how much of the flash reaches a world position
inline int flashAmount(int wx, int wy, int cx, int cy) {
    const long dx = wx - cx, dy = wy - cy, d2 = dx * dx + dy * dy;
    const long inner = (long)(kFlashRadius - kFlashFeather) * (kFlashRadius - kFlashFeather), outer = (long)kFlashRadius * kFlashRadius;
    if (d2 <= inner) return 256;
    if (d2 >= outer) return 0;
    return (int)((kFlashRadius - std::sqrt((double)d2)) * 256.0 / kFlashFeather);
}

inline uint32_t mixColor(uint32_t a, uint32_t b, int t256) {
    auto ch = [&](int shift) { return ((int)((a >> shift) & 255) * (256 - t256) + (int)((b >> shift) & 255) * t256) >> 8; };
    return 0xFF000000 | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Markers for hidden secrets, applied to one pixel of the scenery (sx, sy = position inside the 16x16 square, bg = the pixel is the
// plain backdrop, fade 0..256 = how far the picture has faded to black, so the marks fade with it in the death scene and between
// scenes). Bomb: a dark crack through the rock wall; burn: a yellowish tint growing from the top of the tree to its bottom; armos: the
// blue of the eyes becomes red; push (rock / gravestone): a gray tint growing from top to bottom. Never on the ground.
inline uint32_t secretMark(uint32_t col, int kind, int sx, int sy, bool bg, int fade) {   // (bg: ground, not painted)
    auto scaled = [&](uint32_t c) { return 0xFF000000u | ((((c >> 16 & 255) * fade / 256) << 16) | (((c >> 8 & 255) * fade / 256) << 8) | ((c & 255) * fade / 256)); };
    // A tint never makes the pixel brighter than the scenery it sits on, so it fades out with the picture however the game
    // steps its palette (the fade ratio alone is only an estimate).
    auto capped = [&](uint32_t res) {
        const int m0 = std::max({(int)(col >> 16 & 255), (int)(col >> 8 & 255), (int)(col & 255)});
        const int m1 = std::max({(int)(res >> 16 & 255), (int)(res >> 8 & 255), (int)(res & 255)});
        if (m1 <= m0) return res;
        return 0xFF000000u | ((((res >> 16 & 255) * m0 / m1) << 16) | (((res >> 8 & 255) * m0 / m1) << 8) | ((res & 255) * m0 / m1));
    };
    switch (kind) {
    case SecretBomb: {
        static const int8_t kCrackX[16] = {-1, -1, 9, 10, 9, 8, 8, 7, 8, 7, 6, 6, 5, 5, -1, -1};   // one pixel per row, a jagged line
        return sx == kCrackX[sy & 15] ? mixColor(col, 0xFF000000, 210) : col;
    }
    case SecretBurn:
        return bg ? col : capped(mixColor(col, scaled(0xFFE8C840), 8 + (sy & 15) * 8));
    case SecretArmos: {
        const int r = col >> 16 & 255, g = col >> 8 & 255, b = col & 255;
        const int th = 40 * fade / 256;                  // (the blue's margin shrinks as the picture fades)
        return b > 0 && b > r + th && b > g + th ? capped(scaled(0xFFC83020)) : col;
    }
    case SecretPush: {
        // (a green rock is already close to the gray in hue, so its tint is stronger: up to 90% at the bottom of the square)
        const int r = col >> 16 & 255, g = col >> 8 & 255, b = col & 255;
        const int greenBoost = std::min(std::max(0, g - std::max(r, b)) * 2, 140);
        return bg ? col : capped(mixColor(col, scaled(0xFF909090), std::min(255, 6 + (sy & 15) * 7 + greenBoost * (sy & 15) / 15)));
    }
    default:
        return col;
    }
}

// Is OAM entry i part of the status bar rather than the game's scene? Sprite 0 (the split), the map markers (entries 21
// and 22: "Sprites+84"/"+88" in the game), and the B/A item icons, which the game draws as sprites at ($7C,$1F) and
// ($94,$1F). Everything else, enemies and sword beams passing over the top-left of the screen included, is the scene.
inline bool hudSpriteEntry(const uint8_t* oam, int i) {
    if (i == 0 || i == 21 || i == 22) return true;
    if (i < 0 || i >= 64) return false;                                 // (not an OAM entry: 0xFF = no sprite)
    const int sx = oam[i * 4 + 3], sy = oam[i * 4] + 1;
    return sy >= 0x1D && sy <= 0x25 && ((sx >= 0x7B && sx <= 0x86) || (sx >= 0x93 && sx <= 0x9E));
}

inline bool playAreaVisible(const uint32_t* fb) {
    for (int i = HUD_H * Ppu::W; i < Ppu::W * FRAME_H; i++) if (fb[i] & 0x00FFFFFF) return true;
    return false;
}

inline uint32_t scaleColor(uint32_t c, int scale256) {
    const int r = ((c >> 16) & 255) * scale256 >> 8, g = ((c >> 8) & 255) * scale256 >> 8, b = (c & 255) * scale256 >> 8;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

inline int paletteLuma(const uint8_t* palette) {
    int luma = 0;
    for (int i = 0; i < 16; i++) {
        const uint32_t c = Ppu::nesColor(palette[(i & 3) ? i : 0]);
        luma += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
    }
    return luma;
}
// The same for the palette entries [from, to) only (the death fade rewrites just the bottom half, entries 8..15).
inline int paletteLumaRange(const uint8_t* palette, int from, int to) {
    int luma = 0;
    for (int i = from; i < to; i++) {
        const uint32_t c = Ppu::nesColor(palette[(i & 3) ? i : 0]);
        luma += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
    }
    return luma;
}
// Game modes that show the status bar and a play area (see UpdateMode_JumpTable).
inline bool isGameplayMode(int m) {
    using namespace zelda;
    return m == kModeEnter || m == kModePlay || m == kModeLeave || m == kModeScroll || m == kModeCellar || m == kModeLeaveCave || m == kModeCave || m == kModeShortcut ||
           m == kModeStairs || m == kModeDeath || m == kModeEndLevel;
}

inline int floorDiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

inline void blit(Canvas& out, const uint32_t* src, int srcW, int srcRows, int dstX, int dstY, int minY) {
    for (int r = 0; r < srcRows; r++) {
        const int y = dstY + r;
        if (y < minY || y >= out.h) continue;
        const int x0 = std::max(0, dstX), x1 = std::min(out.w, dstX + srcW);
        if (x1 <= x0) continue;
        std::memcpy(&out.pixels[(size_t)y * out.w + x0], &src[(size_t)r * srcW + (x0 - dstX)], (size_t)(x1 - x0) * sizeof(uint32_t));
    }
}
// The part of the picture a water pass reads (apart from the mask it works out itself): what it works on plus what its reflections and waves look at.
constexpr int kShare = 20;                            // px either side over which neighbouring columns of a body share the highest mirror axis (rounded corners)
struct ReadRect { int x0, x1, y0, y1; };
inline ReadRect readRect(int w, int h, int firstRow, int cx0, int cy0, int cx1, int cy1) {
    return {std::max(0, cx0 - kShare - 16), std::min(w, cx1 + kShare + 16), std::max(firstRow, cy0 - 128), std::min(h, cy1 + 16)};
}

} // namespace wd

using namespace wd;
using namespace zelda;
