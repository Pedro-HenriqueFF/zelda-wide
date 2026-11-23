// Widescreen: drawing the decoded overworld (fog, seams, secret marks), the game's own room and its overhang.
#include <functional>
#include "widescreen_internal.h"
#include "parallel.h"
#include "perf.h"

void Widescreen::blitRoom(Nes& nes, Canvas& out, int roomX, int roomY, int minY, const FlashEffect& fl, int wx0, int wy0) {
    // The game's picture of the current screen, in colour even when the game turns its grayscale bit on for the bomb
    // flash. The flash is applied here instead, as a circle around the bomb that also reaches the surroundings.
    const bool unfoggingHere = fog && unfogRoom >= 0 && blitRoomId == unfogRoom;
    const int markFade = refLuma > 0 ? std::clamp(paletteLuma(nes.ppu.palette) * 256 / refLuma, 0, 256) : 256;
    for (int r = 0; r < ROOM_H; r++) {
        const int y = roomY + r;
        if (y < minY || y >= out.h) continue;
        for (int x = 0; x < Ppu::W; x++) {
            const int cx = roomX + x;
            if (cx < 0 || cx >= out.w) continue;
            const int i = (HUD_H + r) * Ppu::W + x;
            const uint8_t owner = nes.ppu.sprOam[(HUD_H + r) * Ppu::SPR_W + x];
            const bool hidden = owner < 64 && hideSprite[owner];            // a sprite we draw elsewhere (or not at all)
            uint32_t col = hidden ? nes.ppu.bgColor[i] : nes.ppu.fbColor[i];
            if (blitSecrets) {
                const int kind = blitSecrets[(x >> 3) * OverworldMap::TILE_ROWS + (r >> 3)];
                // Armos statues are sprites (not Link's); everything else is scenery (not under Link or a monster).
                if (kind && (owner >= 64 || (kind == SecretArmos && owner != 18 && owner != 19 && !hidden))) {
                    bool ground = false;                                         // the ground is what the square's corners show (a tree or rock never fills them)
                    if (kind == SecretBurn || kind == SecretPush) {
                        const uint32_t* b = &nes.ppu.bgColor[(HUD_H + (r & ~15)) * Ppu::W + (x & ~15)];
                        const uint32_t c0 = b[0], c1 = b[15], c2 = b[15 * Ppu::W], c3 = b[15 * Ppu::W + 15];
                        const uint32_t ref = squareGround(c0, c1, c2, c3);
                        ground = nes.ppu.bgColor[i] == ref;
                    }
                    col = secretMark(col, kind, x & 15, r & 15, ground, markFade);
                }
            }
            if (fl.on) {
                const int t = flashAmount(wx0 + x, wy0 + r, fl.cx, fl.cy);
                if (t) {
                    if (hidden) {                                              // (no colour index of the background: use its brightness)
                        const int g = (int)(((col >> 16) & 255) + ((col >> 8) & 255) + (col & 255)) / 3;
                        col = mixColor(col, 0xFF000000 | (g << 16) | (g << 8) | g, t);
                    } else {
                        col = mixColor(col, Ppu::nesColor(nes.ppu.fbIdx[i] & 0x30), t);
                    }
                }
            }
            if (unfoggingHere) {                                         // (the unfogging room: the game's picture only inside the clear patch)
                const int t = revealAmount(x, r);
                if (t < 256) col = mixColor(out.pixels[(size_t)y * out.w + cx], col, t);
            }
            out.pixels[(size_t)y * out.w + cx] = col;
        }
    }
}

void Widescreen::blitRoomBackground(Nes& nes, Canvas& out, int roomX, int roomY, int minY) {
    // The game's picture of the current screen with the background layer only: no Link, enemies, projectiles or effects.
    for (int r = 0; r < ROOM_H; r++) {
        const int y = roomY + r;
        if (y < minY || y >= out.h) continue;
        for (int x = 0; x < Ppu::W; x++) {
            const int cx = roomX + x;
            if (cx < 0 || cx >= out.w) continue;
            out.pixels[(size_t)y * out.w + cx] = nes.ppu.bgColor[(HUD_H + r) * Ppu::W + x];
        }
    }
}

void Widescreen::drawOverhang(Nes& nes, Canvas& out, int roomX, int roomY, int minY, const FlashEffect& fl, int wx0, int wy0) {
    // Sprites hang over the edges of the game's 256x176 picture: over the top (where the status bar was) and past the
    // right edge. The original cuts them there; here the missing parts are drawn into the neighbouring screens. (Not
    // below the bottom: nothing real is ever there, but the game parks hidden sprites in that area.)
    bool hud[64];
    for (int i = 0; i < 64; i++) hud[i] = hudSpriteEntry(nes.ppu.oam, i) || hideSprite[i];
    for (int y = 0; y < FRAME_H; y++) {
        const int cy = roomY + (y - HUD_H);
        if (cy < minY || cy >= out.h) continue;
        for (int x = 0; x < Ppu::SPR_W; x++) {
            if (y >= HUD_H && x < Ppu::W) continue;                            // inside the game's own picture: already drawn
            const int k = y * Ppu::SPR_W + x;
            const uint8_t ci = nes.ppu.sprCol[k], oamIdx = nes.ppu.sprOam[k];
            if (ci == 0xFF || oamIdx >= 64 || hud[oamIdx]) continue;
            const int cx = roomX + x;
            if (cx < 0 || cx >= out.w) continue;
            uint32_t col = Ppu::nesColor(ci);
            if (fl.on) {
                const int t = flashAmount(wx0 + x, wy0 + (y - HUD_H), fl.cx, fl.cy);
                if (t) col = mixColor(col, Ppu::nesColor(ci & 0x30), t);
            }
            out.pixels[(size_t)cy * out.w + cx] = col;
        }
    }
}

void Widescreen::drawWorld(Nes& nes, Canvas& out, int camX, int camY, int slot, const FlashEffect& fl) {
    perf::Scope perfScope(perf::kWorld);                      // (includes pass A of the water)
    const int W = out.w, H = out.h;
    uint32_t bg[16];                                     // background palettes; colour 0 of each is the shared backdrop
    for (int i = 0; i < 16; i++) bg[i] = Ppu::nesColor(nes.ppu.palette[(i & 3) ? i : 0]);
    uint32_t bgGray[16];                                 // the same colours with the grayscale bit (for the bomb flash)
    for (int i = 0; i < 16; i++) bgGray[i] = Ppu::nesColor(nes.ppu.palette[(i & 3) ? i : 0] & 0x30);
    const uint16_t patBase = (nes.ppu.lineCtrl[100] & 0x10) ? 0x1000 : 0;
    // The fog fades to black together with the rest of the picture (death, fades between scenes).
    const int fogScale = refLuma > 0 ? std::clamp(paletteLuma(nes.ppu.palette) * 256 / refLuma, 0, 256) : 256;

    struct RoomView { bool ready = false, disc = false, near = false; const uint8_t* tiles = nullptr; const uint8_t* sec = nullptr; uint8_t outer = 0, inner = 0; uint8_t nb = 0; int weakBit = -1; bool reveal = false, endsDone = false; uint8_t cornerLit = 0; FogDepths fd{}; };   // cornerLit: bit c = both rooms beside corner patch c are lit   // nb: bit k set = neighbour kFogNb[k] is explored (fogged rooms only)
    RoomView views[128];
    // Unfogging animation: a clear patch (an ellipse with the room's proportions) grows from where Link entered; the glow
    // the new room casts on its fogged neighbours fades in over the second half.
    // The glow an unfogging room casts on a fogged neighbour follows the reveal front: at each pixel it is as strong as
    // the clear patch is at the nearest point of the new room.
    auto glowW = [&](const RoomView& rv, int wx, int wy) {
        if (rv.weakBit < 0 || unfogRoom < 0) return 1.0;
        const int ux = std::clamp(wx - (unfogRoom & 15) * ROOM_W, 0, ROOM_W - 1), uy = std::clamp(wy - (unfogRoom >> 4) * ROOM_H, 0, ROOM_H - 1);
        return revealAmount(ux, uy) / 256.0;
    };
    std::function<RoomView&(int)> roomView = [&](int room) -> RoomView& {
        RoomView& v = views[room];
        if (!v.ready) {
            v.ready = true;
            v.reveal = fog && room == unfogRoom;                 // being unfogged: drawn as fogged plus the growing clear patch
            v.disc = !fog || (discovered(slot, room) && !v.reveal);
            if (!v.disc) {
                for (int k = 0; k < 8; k++) {
                    const int nx = (room & 15) + kFogNb[k][0], ny = (room >> 4) + kFogNb[k][1];
                    if (nx >= 0 && nx < 16 && ny >= 0 && ny < 8 && discovered(slot, ny * 16 + nx)) {
                        v.nb |= 1 << k;
                        if (ny * 16 + nx == unfogRoom) v.weakBit = k;     // its glow only appears as the reveal finishes
                    }
                }
            }
            if (!v.disc && fog) {                                // a fogged room beside a fogged room that is lit by an explored one: the seam is blended
                for (int k : {1, 3, 4, 6}) {
                    const int qx = (room & 15) + kFogNb[k][0], qy = (room >> 4) + kFogNb[k][1];
                    if (qx < 0 || qx >= 16 || qy < 0 || qy >= 8 || discovered(slot, qy * 16 + qx)) continue;
                    for (int j = 0; j < 8 && !v.near; j++) {
                        const int sx = qx + kFogNb[j][0], sy = qy + kFogNb[j][1];
                        if (sx >= 0 && sx < 16 && sy >= 0 && sy < 8 && discovered(slot, sy * 16 + sx)) v.near = true;
                    }
                }
            }
            if (v.disc || v.nb || v.reveal || v.near) {
                v.tiles = roomTiles(nes, room);
                if (v.disc || v.reveal) v.sec = roomSecrets(nes, room);   // (hidden secrets are only marked in explored screens, and already under the fog while one is being unfogged)
                v.outer = nes.cart.cpuRead(kAttrsA + room) & 3;
                v.inner = nes.cart.cpuRead(kAttrsB + room) & 3;
                // Fog depth along each border: short where either side of it is blocked (water only counts as walkable where it
                // touches real ground, see owWalkMask).
                if (!v.disc && v.nb) {
                bool mine[704];
                owWalkMask(v.tiles, mine);
                auto theirs = [&](int k, bool* m) { const int nx = (room & 15) + kFogNb[k][0], ny = (room >> 4) + kFogNb[k][1];
                                                    if (!(v.nb >> k & 1) || nx < 0 || nx >= 16 || ny < 0 || ny >= 8) return false;
                                                    owWalkMask(roomTiles(nes, ny * 16 + nx), m); return true; };
                bool nm[704];
                for (int side = 0; side < 4; side++) {
                    static const int kSideK[4] = {1, 3, 4, 6};
                    const bool has = !v.disc && theirs(kSideK[side], nm);
                    for (int i = 0; i < (side == 0 || side == 3 ? 32 : 22); i++) {
                        bool ok = true;
                        for (int j = 0; j < 2 && has; j++) {          // the two rows / columns on each side of the border
                            const int a = side == 0 ? i * 22 + j : side == 3 ? i * 22 + 21 - j : side == 1 ? j * 22 + i : (31 - j) * 22 + i;
                            const int b = side == 0 ? i * 22 + 21 - j : side == 3 ? i * 22 + j : side == 1 ? (31 - j) * 22 + i : j * 22 + i;
                            ok = ok && mine[a] && nm[b];
                        }
                        v.fd.d[side][i] = (uint8_t)(ok ? kFogDepth : kFogDepthWall);
                    }
                }
                for (int c = 0; c < 4; c++) {                         // corners NW, NE, SW, SE
                    // The patch is an ellipse: horizontally as deep as the border to the north / south room (the light that spreads
                    // along the neighbour's edge continues here: the glow of that straight border has the same width), vertically
                    // as the border to the west / east room. Each is short when the 2x2 tiles on both sides of that border, or of
                    // the corner itself, are blocked.
                    static const int kCornerK[4] = {0, 2, 5, 7}, kVK[4] = {1, 1, 6, 6}, kHK[4] = {3, 4, 3, 4};
                    bool dm[704], vm[704], hm[704];
                    auto load = [&](int k, bool* m) { const int nx = (room & 15) + kFogNb[k][0], ny = (room >> 4) + kFogNb[k][1];
                                                      if (nx < 0 || nx >= 16 || ny < 0 || ny >= 8) return false;
                                                      owWalkMask(roomTiles(nes, ny * 16 + nx), m); return true; };
                    const bool hasD = load(kCornerK[c], dm), hasV = load(kVK[c], vm), hasH = load(kHK[c], hm);
                    const int cx = c & 1 ? 30 : 0, cy = c & 2 ? 20 : 0, ox = c & 1 ? 0 : 30, oy = c & 2 ? 0 : 20;
                    bool okD = true, okV = true, okH = true;
                    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) {
                        okD = okD && (!hasD || (mine[(cx + i) * 22 + cy + j] && dm[(ox + i) * 22 + oy + j]));
                        okV = okV && (!hasV || (mine[(cx + i) * 22 + cy + j] && vm[(cx + i) * 22 + oy + j]));   // border to the N/S room
                        okH = okH && (!hasH || (mine[(cx + i) * 22 + cy + j] && hm[(ox + i) * 22 + cy + j]));   // border to the W/E room
                    }
                    v.fd.diagX[c] = (uint8_t)(okV && okD ? kFogDepth : kFogDepthWall);
                    v.fd.diagY[c] = (uint8_t)(okH && okD ? kFogDepth : kFogDepthWall);
                }
                }
            }
        }
        if (!v.endsDone && !v.disc && v.nb) {
            v.endsDone = true;
            const int rx = room & 15, ry = room >> 4;
            // Corner patches (this room is reached diagonally from an explored one): are the two rooms beside the patch lit?
            for (int kk : {0, 2, 5, 7}) {
                if (!fogDiagActive(v.nb, kk)) continue;
                const int kx = kFogNb[kk][0], ky = kFogNb[kk][1];
                // V = the room on the explored room's side along y (its border to the explored room is vertical), H = along x
                auto depthAt = [&](int qx, int qy, int toE, int side, int tile) {
                    if (qx < 0 || qx >= 16 || qy < 0 || qy >= 8) return 0;
                    if (discovered(slot, qy * 16 + qx)) return 255;                      // explored: fully lit
                    RoomView& q = roomView(qy * 16 + qx);
                    return (q.nb & (1u << toE)) ? (int)q.fd.d[side][tile] : 0;
                };
                const int dV = depthAt(rx, ry + ky, kx > 0 ? 4 : 3, kx > 0 ? 2 : 1, ky > 0 ? 0 : 21);
                const int dH = depthAt(rx + kx, ry, ky > 0 ? 6 : 1, ky > 0 ? 3 : 0, kx > 0 ? 0 : 31);
                if (fogCornerLit(dV, dH)) v.cornerLit |= 1u << (kk == 0 ? 0 : kk == 2 ? 1 : kk == 5 ? 2 : 3);
            }
            // Beyond the ends of a border: a dark corner patch there (with both rooms beside it lit) makes the glow curve inwards
            for (int k : {1, 3, 4, 6}) {
                if (!(v.nb & (1u << k))) continue;
                const int side = k == 1 ? 0 : k == 3 ? 1 : k == 4 ? 2 : 3;
                const int er = (ry + kFogNb[k][1]) * 16 + rx + kFogNb[k][0];                 // the explored room
                for (int end = 0; end < 2; end++) {
                    const bool horiz = k == 1 || k == 6;
                    const int qx = rx + (horiz ? (end ? 1 : -1) : 0), qy = ry + (horiz ? 0 : (end ? 1 : -1));
                    if (qx < 0 || qx >= 16 || qy < 0 || qy >= 8 || discovered(slot, qy * 16 + qx)) continue;
                    const int dx = (er & 15) - qx, dy = (er >> 4) - qy;
                    int kk = -1; for (int j = 0; j < 8; j++) if (kFogNb[j][0] == dx && kFogNb[j][1] == dy) kk = j;
                    if (kk < 0) continue;
                    RoomView& q = roomView(qy * 16 + qx);
                    if (q.disc || !fogDiagActive(q.nb, kk)) continue;
                    const int c = kk == 0 ? 0 : kk == 2 ? 1 : kk == 5 ? 2 : 3, low = std::min<int>(q.fd.diagX[c], q.fd.diagY[c]);
                    if (low < kFogDepth && (q.cornerLit >> c & 1)) v.fd.endLow[side][end] = (uint8_t)low;
                }
            }
        }
        return v;
    };

    // Fog glow of a fogged room, blended over the seams to fogged neighbours (within kSeam pixels of the seam, half way to the value the
    // neighbour has there), so a lit room next to a dark one never ends in a hard step.
    auto ownVis = [&](RoomView& r, int lx, int ly, int wx, int wy) { return r.nb ? fogVisibility(r.nb, lx, ly, r.weakBit, glowW(r, wx, wy), &r.fd) : 0; };
    // Where the glow along a border already changes depth near the end of the room (its edge is a quarter ellipse there), the seam is
    // left alone: the blend would flatten that curve. axis 0 = a seam at the left/right (borders to the north/south matter), 1 = top/bottom.
    auto endFlat = [&](const RoomView& r, int axis, bool low) {
        for (int j = 0; j < 2; j++) {
            const int k = axis == 0 ? (j ? 6 : 1) : (j ? 4 : 3), side = axis == 0 ? (j ? 3 : 0) : (j ? 2 : 1), n = axis == 0 ? 32 : 22;
            if (!(r.nb & (1u << k))) continue;
            const int from = low ? 0 : n - 5, to = low ? 4 : n - 1;
            for (int t = from + 1; t <= to; t++) if (r.fd.d[side][t] != r.fd.d[side][from]) return false;
        }
        return true;
    };
    auto cornerBlocks = [&](const RoomView& r, int axis, bool low) {   // a corner patch on this seam whose two neighbours are not both lit
        for (int k : {0, 2, 5, 7})
            if (fogDiagActive(r.nb, k) && fogDiagOnSeam(k, axis, low) && !(r.cornerLit >> (k == 0 ? 0 : k == 2 ? 1 : k == 5 ? 2 : 3) & 1)) return true;
        return false;
    };
    auto seamVis = [&](RoomView& rv, int room, int lx, int ly, int wx, int wy) {
        double v = ownVis(rv, lx, ly, wx, wy);
        auto blend = [&](int nroom, int nlx, int nly, double t, int axis, bool low) {
            RoomView& nv = roomView(nroom);
            if (nv.disc || nv.reveal) return;
            if (!endFlat(rv, axis, low) || !endFlat(nv, axis, !low)) return;
            if (cornerBlocks(rv, axis, low) || cornerBlocks(nv, axis, !low)) return;   // a corner with a dark neighbour: its patch matches already
            v = seamBlend(v, ownVis(nv, nlx, nly, wx, wy), t);
        };
        const int rx = room & 15, ry = room >> 4;
        if (lx < kSeam && rx > 0) blend(room - 1, ROOM_W - 1, ly, lx + 0.5, 0, true);
        else if (lx >= ROOM_W - kSeam && rx < 15) blend(room + 1, 0, ly, ROOM_W - lx - 0.5, 0, false);
        if (ly < kSeam && ry > 0) blend(room - 16, lx, ROOM_H - 1, ly + 0.5, 1, true);
        else if (ly >= ROOM_H - kSeam && ry < 7) blend(room + 16, lx, 0, ROOM_H - ly - 0.5, 1, false);
        return (int)(v + 0.5);
    };

    const bool water = waterOn() && fxNes && enabled;                      // the water is drawn on the terrain BEFORE the fog is mixed in
    std::vector<uint32_t>& raw = wRawSpare; std::vector<uint16_t>& vis = wVis;   // (kept between frames: no 4-8 MB allocation, and its page faults, every frame)
    const int M = water ? kWaterMargin : 0;                              // rows of terrain above the picture: the reflections read them whatever the camera shows
    if (water) { raw.assign((size_t)W * (H + M), 0); vis.assign((size_t)W * (H + M), 0); }
    uint8_t chr[4096]; for (int i = 0; i < 4096; i++) chr[i] = nes.cart.ppuRead((uint16_t)(patBase + i));   // (the tile patterns, read once per frame instead of once per pixel)
    auto plain = [&](const uint8_t* tiles, int outer, int inner, int lx, int ly) -> uint32_t {      // the terrain's colour without fog, secrets or flash
        const uint8_t tile = tiles[(lx >> 3) * OverworldMap::TILE_ROWS + (ly >> 3)];
        const bool in = (lx >> 4) >= 2 && (lx >> 4) <= 13 && (ly >> 4) >= 2 && (ly >> 4) <= 8;
        const uint16_t pat = patBase + tile * 16 + (ly & 7);
        const int bit = 7 - (lx & 7), c = ((chr[pat - patBase] >> bit) & 1) | (((chr[pat - patBase + 8] >> bit) & 1) << 1);
        return bg[c ? ((in ? inner : outer) << 2) | c : 0];
    };
    const bool useVis = fogCache && fog && unfogRoom < 0;                // (no animation: the glow of every room is the same as in the last frame unless the explored rooms or the screen data changed)
    if (useVis) {
        uint64_t e = 1469598103934665603ULL ^ (uint64_t)(slot + 1);
        for (int b = 0; b < 16; b++) e = (e ^ fogBits[slot][b]) * 1099511628211ULL;
        for (int r = 0; r < 128; r++) { roomTiles(nes, r); e = (e ^ (uint64_t)(cache[r].key + 2)) * 1099511628211ULL; }
        if (e != fogEpoch) { for (auto& m : fogVisMap) { m.clear(); m.shrink_to_fit(); } fogEpoch = e; }
    }
    // The rows are worked out on several threads, so everything they would fill in lazily is made ready first (serially): the views of every
    // screen the canvas touches and of the ring around it (the fog seams look at their neighbours), the tiles and palettes, the fog glow caches.
    const uint8_t* tilesOf[128] = {}; int outerOf[128] = {}, innerOf[128] = {};
    {
        const int rx0 = std::clamp(floorDiv(camX, ROOM_W), 0, 15), rx1 = std::clamp(floorDiv(camX + W - 1, ROOM_W), 0, 15);
        const int ry0 = std::clamp(floorDiv(camY - M, ROOM_H), 0, 7), ry1 = std::clamp(floorDiv(camY + (H - topBand) - 1, ROOM_H), 0, 7);
        for (int ry = std::max(0, ry0 - 1); ry <= std::min(7, ry1 + 1); ry++)
            for (int rx = std::max(0, rx0 - 1); rx <= std::min(15, rx1 + 1); rx++) {
                const int room = ry * 16 + rx;
                RoomView& rv = roomView(room);
                if (rx < rx0 || rx > rx1 || ry < ry0 || ry > ry1) continue;
                tilesOf[room] = roomTiles(nes, room);
                outerOf[room] = nes.cart.cpuRead(kAttrsA + room) & 3; innerOf[room] = nes.cart.cpuRead(kAttrsB + room) & 3;
                if (useVis && !rv.disc) { std::vector<uint16_t>& vm = fogVisMap[room]; if (vm.empty()) vm.assign((size_t)ROOM_W * ROOM_H, 0xFFFF); }
            }
    }
    par::forRange(topBand - M, H, 8, [&](int ya, int yb) {
    static thread_local std::vector<uint32_t> scratchTL;                 // (rows above the picture are only needed as terrain for the water: this thread's own)
    if (water && (int)scratchTL.size() < W) scratchTL.resize(W);
    std::vector<uint32_t>& scratch = scratchTL;
    for (int y = ya; y < yb; y++) {
        const int wy = camY + (y - topBand);
        const int ry = floorDiv(wy, ROOM_H);
        const int ly = wy - ry * ROOM_H;
        uint32_t* row = y >= topBand ? &out.pixels[(size_t)y * W] : scratch.data();

        for (int x = 0; x < W;) {
            const int wx = camX + x;
            const int n = std::min(8 - (wx & 7), W - x);             // pixels left in this 8-pixel tile column
            const int rx = floorDiv(wx, ROOM_W);

            if (rx >= 0 && rx < 16 && ry >= 0 && ry < 8) {           // outside the world stays black
                const int room = ry * 16 + rx;
                RoomView& rv = roomView(room);
                const int lx = wx - rx * ROOM_W;
                if (!rv.disc && !rv.nb && !rv.reveal && !rv.near) {
                    for (int i = 0; i < n; i++) row[x + i] = scaleColor(fogPixel(wx + i, wy), fogScale);
                    if (water) { const uint8_t* ft = tilesOf[room]; const int fo = outerOf[room], fi = innerOf[room]; for (int i = 0; i < n; i++) raw[(size_t)(y + M) * W + x + i] = plain(ft, fo, fi, lx + i, ly); }   // (reflections see the terrain behind the fog)
                } else {
                    const uint8_t tile = rv.tiles[(lx >> 3) * OverworldMap::TILE_ROWS + (ly >> 3)];
                    const bool inner = (lx >> 4) >= 2 && (lx >> 4) <= 13 && (ly >> 4) >= 2 && (ly >> 4) <= 8;
                    const int sel = inner ? rv.inner : rv.outer;
                    const uint16_t pat = patBase + tile * 16 + (ly & 7);
                    const uint8_t lo = chr[pat - patBase], hi = chr[pat - patBase + 8];
                    for (int i = 0; i < n; i++) {
                        int v = 256;                                         // how much terrain shows through
                        if (!rv.disc) {
                            if (useVis) {                                    // (the glow of a room only changes with the explored rooms or the screen data: worked out once per pixel)
                                std::vector<uint16_t>& vm = fogVisMap[room];
                                if (vm.empty()) vm.assign((size_t)ROOM_W * ROOM_H, 0xFFFF);
                                uint16_t& cv = vm[(size_t)ly * ROOM_W + lx + i];
                                if (cv == 0xFFFF) cv = (uint16_t)seamVis(rv, room, lx + i, ly, wx + i, wy);
                                v = cv;
                            } else v = seamVis(rv, room, lx + i, ly, wx + i, wy);
                        }
                        if (rv.reveal) v = std::max(v, revealAmount(lx + i, ly));
                        if (v == 0) { row[x + i] = scaleColor(fogPixel(wx + i, wy), fogScale); if (water) raw[(size_t)(y + M) * W + x + i] = plain(rv.tiles, rv.outer, rv.inner, lx + i, ly); continue; }
                        const int bit = 7 - ((lx & 7) + i);
                        const int c = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                        const int pi = c ? (sel << 2) | c : 0;
                        uint32_t col = bg[pi];
                        if (rv.sec) {
                            const int kind = rv.sec[(lx >> 3) * OverworldMap::TILE_ROWS + (ly >> 3)];
                            if (kind) {
                                bool ground = pi == 0;
                                if (kind == SecretBurn || kind == SecretPush) {      // the ground is what the square's corners show
                                    auto pidx = [&](int px, int py) {
                                        const uint8_t t = rv.tiles[(px >> 3) * OverworldMap::TILE_ROWS + (py >> 3)];
                                        const bool in = (px >> 4) >= 2 && (px >> 4) <= 13 && (py >> 4) >= 2 && (py >> 4) <= 8;
                                        const uint16_t pt = patBase + t * 16 + (py & 7);
                                        const int b = 7 - (px & 7), cc = ((chr[pt - patBase] >> b) & 1) | (((chr[pt - patBase + 8] >> b) & 1) << 1);
                                        return cc ? ((in ? rv.inner : rv.outer) << 2) | cc : 0;
                                    };
                                    const int x0 = (lx + i) & ~15, y0 = ly & ~15;
                                    ground = pi == squareGround(pidx(x0, y0), pidx(x0 + 15, y0), pidx(x0, y0 + 15), pidx(x0 + 15, y0 + 15));
                                }
                                col = secretMark(col, kind, (lx + i) & 15, ly & 15, ground, fogScale);
                            }
                        }
                        if (fl.on) {
                            const int t = flashAmount(wx + i, wy, fl.cx, fl.cy);
                            if (t) col = mixColor(col, bgGray[pi], t);
                        }
                        if (water) { raw[(size_t)(y + M) * W + x + i] = col; vis[(size_t)(y + M) * W + x + i] = (uint16_t)v; }
                        row[x + i] = v == 256 ? col : mixColor(scaleColor(fogPixel(wx + i, wy), fogScale), col, v);
                    }
                }
            }
            x += n;
        }
    }
    });
    if (water) {                                                         // reflections and waves on the terrain, then the fog over them
        waterTick++;
        terrainMask.clear();
        if (recordShare) { backdropRawPre = raw; backdropVis = vis; backdropFogScale = fogScale; }     // (the cave box's backdrop redoes the water from these)
        WaterJob j{raw.data(), W, H + M, topBand + M, camX, camY, 0, topBand + M, W, H + M, &terrainMask, true, false, nullptr, true, nullptr, 0};
        waterPass(j);
        terrainW = W; terrainH = H; terrainCamX = camX; terrainCamY = camY;
        par::forRange(topBand, H, 16, [&](int ya, int yb) {
            for (int y = ya; y < yb; y++) {
                const int wy = camY + (y - topBand);
                uint32_t* row = &out.pixels[(size_t)y * W];
                for (int x = 0; x < W; x++) {
                    const size_t id = (size_t)(y + M) * W + x; const int v = vis[id];
                    if (v == 0) continue;
                    row[x] = v == 256 ? raw[id] : mixColor(scaleColor(fogPixel(camX + x, wy), fogScale), raw[id], v);
                }
            }
        });
        terrainRaw.swap(raw);                                            // (the buffer of the frame before becomes the next frame's scratch)
        waterDone = true;
    }
}
