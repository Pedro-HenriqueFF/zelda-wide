#include "ppu.h"
#include <algorithm>
#include <cstdlib>
#include "cart.h"
#include <cstring>

namespace {
const uint32_t kPalette[64] = {
    0x666666,0x002A88,0x1412A7,0x3B00A4,0x5C007E,0x6E0040,0x6C0600,0x561D00,
    0x333500,0x0B4800,0x005200,0x004F08,0x00404D,0x000000,0x000000,0x000000,
    0xADADAD,0x155FD9,0x4240FF,0x7527FE,0xA01ACC,0xB71E7B,0xB53120,0x994E00,
    0x6B6D00,0x388700,0x0C9300,0x008F32,0x007C8D,0x000000,0x000000,0x000000,
    0xFFFEFF,0x64B0FF,0x9290FF,0xC676FF,0xF36AFF,0xFE6ECC,0xFE8170,0xEA9E22,
    0xBCBE00,0x88D800,0x5CE430,0x45E082,0x48CDDE,0x4F4F4F,0x000000,0x000000,
    0xFFFEFF,0xC0DFFF,0xD3D2FF,0xE8C8FF,0xFBC2FF,0xFEC4EA,0xFECCC5,0xF7D8A5,
    0xE4E594,0xCFEF96,0xBDF4AB,0xB3F3CC,0xB5EBF2,0xB8B8B8,0x000000,0x000000,
};
inline uint8_t palIndex(uint8_t a) { a &= 0x1F; if ((a & 0x13) == 0x10) a &= 0x0F; return a; }
}

uint32_t Ppu::nesColor(uint8_t index) { return 0xFF000000 | kPalette[index & 0x3F]; }

void Ppu::buildSpriteLayerLine(int sl) {
    // Re-evaluates the sprites on one line into the sprite layer (same 8-per-line limit and priority as the PPU, but
    // not clipped at the screen edges and not hidden behind the background).
    uint8_t* col = &sprCol[sl * SPR_W];
    uint8_t* own = &sprOam[sl * SPR_W];
    std::memset(col, 0xFF, SPR_W);
    std::memset(own, 0xFF, SPR_W);
    uint8_t* dcol = &dropCol[sl * SPR_W];
    uint8_t* down = &dropOam[sl * SPR_W];
    std::memset(dcol, 0xFF, SPR_W);
    std::memset(down, 0xFF, SPR_W);
    if (!(mask & 0x10)) return;
    const int h = (ctrl & 0x20) ? 16 : 8;
    int found = 0;
    for (int i = 0; i < 64; i++) {
        const int y = oam[i * 4];
        if (y >= 0xEF) continue;                                     // parked off screen
        int row = sl - (y + 1);
        if (row < 0 || row >= h) continue;
        const bool dropped = !unlimitedSprites && found >= 8;        // (the 9th sprite of a line and later: not drawn by the PPU)
        found++;
        if (dropped && (oam[i * 4 + 2] & 0x20)) continue;            // (behind the background: stays hidden)
        const uint8_t tile = oam[i * 4 + 1], attr = oam[i * 4 + 2];
        const int sx0 = oam[i * 4 + 3];
        if (attr & 0x80) row = h - 1 - row;
        uint16_t pat;
        if (h == 8) pat = ((ctrl & 0x08) ? 0x1000 : 0) + tile * 16 + row;
        else        pat = ((tile & 1) ? 0x1000 : 0) + (tile & 0xFE) * 16 + (row & 7) + (row >= 8 ? 16 : 0);
        const uint8_t lo = vramRead(pat), hi = vramRead(pat + 8);
        for (int px = 0; px < 8; px++) {
            const int x = sx0 + px;
            if (x >= SPR_W) break;
            const int bit = (attr & 0x40) ? px : 7 - px;
            const uint8_t c = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            if (!c) continue;
            if (dropped) {
                if (dcol[x] == 0xFF) { dcol[x] = palette[0x10 + ((attr & 3) << 2) + c] & 0x3F; down[x] = (uint8_t)i; }
                continue;
            }
            if (col[x] != 0xFF) continue;
            col[x] = palette[0x10 + ((attr & 3) << 2) + c] & 0x3F;
            own[x] = (uint8_t)i;
        }
    }
}

void Ppu::reset() {
    std::memset(sprCol, 0xFF, sizeof sprCol);
    std::memset(sprOam, 0xFF, sizeof sprOam);
    std::memset(dropCol, 0xFF, sizeof dropCol);
    std::memset(dropOam, 0xFF, sizeof dropOam);
    ctrl = mask = status = 0;
    v = t = 0; fineX = 0; w = false;
    oamAddr = 0; readBuffer = 0;
    scanline = 0; cycle = 0; oddFrame = false;
    frameReady = false; nmiOut = false;
    spr0HitDot = -1;
}

uint16_t Ppu::ntAddr(uint16_t a) const {
    a &= 0x0FFF;
    int nt = a >> 10;
    switch (cart ? cart->mirroring() : 3) {
        case 0: nt = 0; break;
        case 1: nt = 1; break;
        case 2: nt &= 1; break;     // vertical
        default: nt >>= 1; break;   // horizontal
    }
    return nt * 0x400 + (a & 0x3FF);
}

uint8_t Ppu::vramRead(uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x2000) return cart->ppuRead(addr);
    if (addr < 0x3F00) return vram[ntAddr(addr)];
    return palette[palIndex(addr)];
}

void Ppu::vramWrite(uint16_t addr, uint8_t val) {
    addr &= 0x3FFF;
    if (addr < 0x2000) cart->ppuWrite(addr, val);
    else if (addr < 0x3F00) vram[ntAddr(addr)] = val;
    else palette[palIndex(addr)] = val & 0x3F;
}

uint8_t Ppu::readReg(uint16_t addr) {
    switch (addr & 7) {
        case 2: {
            uint8_t r = status;
            status &= ~0x80;
            w = false;
            return r;
        }
        case 4: return oam[oamAddr];
        case 7: {
            uint8_t r;
            uint16_t a = v & 0x3FFF;
            if (a >= 0x3F00) {
                r = vramRead(a);
                readBuffer = vram[ntAddr(a)];            // buffer holds the nametable "underneath"
            } else {
                r = readBuffer;
                readBuffer = vramRead(a);
            }
            v += (ctrl & 0x04) ? 32 : 1;
            return r;
        }
        default: return 0;
    }
}

void Ppu::writeReg(uint16_t addr, uint8_t val) {
    switch (addr & 7) {
        case 0: {
            bool wasNmi = ctrl & 0x80;
            ctrl = val;
            t = (t & 0xF3FF) | ((val & 3) << 10);
            if (!wasNmi && (ctrl & 0x80) && (status & 0x80)) nmiOut = true;
            break;
        }
        case 1: mask = val; break;
        case 3: oamAddr = val; break;
        case 4: oam[oamAddr++] = val; break;
        case 5:
            if (!w) { t = (t & 0xFFE0) | (val >> 3); fineX = val & 7; }
            else    { t = (t & 0x8C1F) | ((val & 7) << 12) | ((val & 0xF8) << 2); }
            w = !w;
            break;
        case 6:
            if (!w) { t = (t & 0x00FF) | ((val & 0x3F) << 8); }
            else    { t = (t & 0xFF00) | val; v = t; }
            w = !w;
            break;
        case 7:
            vramWrite(v, val);
            v += (ctrl & 0x04) ? 32 : 1;
            break;
    }
}

void Ppu::step(int dots) {
    if (!lite) { while (dots-- > 0) tick(); return; }
    // Lite: tick() only does something on a few dots of a line (the line's start, the sprite-0 hit, the scroll copies, vblank, the line's end), so the dots in
    // between are skipped in one go; the PPU state is the same as after the same number of single ticks (tests compare them).
    while (dots > 0) {
        int ev = 340;
        auto cand = [&](int c) { if (c >= cycle && c < ev) ev = c; };
        cand(1);
        if (renderingOn() && (scanline < 240 || scanline == 261)) {
            cand(256); cand(257);
            if (scanline == 261 && cycle <= 304) cand(std::max(cycle, 280));
        }
        if (scanline == 261) cand(339);                                          // (the odd frame's short line: tick() steps over dot 340 from here)
        if (scanline < 240 && spr0HitDot >= 0) cand(std::max(cycle, spr0HitDot));
        if (ev > cycle) { const int k = std::min(dots, ev - cycle); cycle += k; dots -= k; continue; }
        tick(); dots--;
    }
}

void Ppu::incrementY() {
    if ((v & 0x7000) != 0x7000) { v += 0x1000; return; }
    v &= ~0x7000;
    int y = (v & 0x03E0) >> 5;
    if (y == 29)      { y = 0; v ^= 0x0800; }
    else if (y == 31) { y = 0; }
    else              { y++; }
    v = (v & ~0x03E0) | (y << 5);
}

void Ppu::tick() {
    const bool rendering = renderingOn();

    if (scanline < 240) {
        if (cycle == 1) { if (lite) liteLine(scanline); else renderLine(scanline); }
        if (spr0HitDot >= 0 && cycle >= spr0HitDot) { status |= 0x40; spr0HitDot = -1; }
    }
    if (!lite && scanline == 240 && cycle == 0)                              // the rows below the picture, for sprites hanging over the bottom
        for (int sl = 240; sl < SPR_H; sl++) buildSpriteLayerLine(sl);
    if (scanline == 241 && cycle == 1) {
        status |= 0x80;
        if (ctrl & 0x80) nmiOut = true;
        frameReady = true;
    }
    if (scanline == 261 && cycle == 1) {
        status &= ~0xE0;
        spr0HitDot = -1;
    }

    if (rendering && (scanline < 240 || scanline == 261)) {
        if (cycle == 256) incrementY();
        if (cycle == 257) v = (v & ~0x041F) | (t & 0x041F);
        if (scanline == 261 && cycle >= 280 && cycle <= 304) v = (v & ~0x7BE0) | (t & 0x7BE0);
    }

    cycle++;
    bool skip = (cycle == 340 && scanline == 261 && oddFrame && rendering);
    if (cycle >= 341 || skip) {
        cycle = 0;
        if (++scanline > 261) { scanline = 0; oddFrame = !oddFrame; }
    }
}

void Ppu::smoothStep(int rawY) {
    if (!smoothV || rawY < 0) { smRaw = -1; smAdj = 0; return; }
    auto wrap = [](int d) { d %= 480; if (d >= 240) d -= 480; if (d < -240) d += 480; return d; };
    if (smRaw < 0) smDisp = rawY;
    else {
        const int step = wrap(rawY - smRaw);                // 0 = no new step, +-8 = one tile row of the scroll: ease; anything else: the picture jumps
        const int gap = wrap(rawY - smDisp);
        if ((step == 0 || std::abs(step) == 8) && std::abs(gap) <= 8) smDisp = (smDisp + std::clamp(gap, -2, 2) + 480) % 480;
        else smDisp = rawY;
    }
    smRaw = rawY;
    smAdj = wrap(smDisp - rawY);
}

// The background of the current line: 4 bits per pixel = (palette << 2) | color, 0 = transparent. `moved` = the smooth scroll shows another row than the game's.
void Ppu::fetchBg(uint8_t* bg, int lineY, bool moved) {
    const int coarseX = v & 31;
    const int coarseY = moved ? (lineY % 240) >> 3 : (v >> 5) & 31;
    const int fineY   = moved ? lineY & 7 : (v >> 12) & 7;
    const int ntSel   = moved ? (((v >> 10) & 1) | ((lineY / 240) << 1)) : (v >> 10) & 3;
    const uint16_t patBase = (ctrl & 0x10) ? 0x1000 : 0;
    for (int tile = 0; tile < 33; tile++) {
        int col = coarseX + tile;
        int nt = ntSel ^ ((col >> 5) & 1);
        col &= 31;
        uint16_t base = 0x2000 | (nt << 10);
        uint8_t idx  = vramRead(base | (coarseY << 5) | col);
        uint8_t attr = vramRead(base | 0x3C0 | ((coarseY >> 2) << 3) | (col >> 2));
        int shiftAmt = ((coarseY & 2) << 1) | (col & 2);
        uint8_t pal = (attr >> shiftAmt) & 3;
        uint16_t pat = patBase + idx * 16 + fineY;
        uint8_t lo = vramRead(pat), hi = vramRead(pat + 8);
        for (int px = 0; px < 8; px++) {
            int sx = tile * 8 + px - fineX;
            if (sx < 0 || sx >= W) continue;
            int bit = 7 - px;
            uint8_t c = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            bg[sx] = c ? ((pal << 2) | c) : 0;
        }
    }
    if (!(mask & 0x02)) for (int x = 0; x < 8; x++) bg[x] = 0;
}

// Lite mode (a shadow game that nobody looks at): no picture, only what the game waits for: the sprite-0 hit, found with the same background and sprite rules as renderLine.
void Ppu::liteLine(int sl) {
    if (!(mask & 0x08) || !(mask & 0x10) || spr0HitDot >= 0 || (status & 0x40)) return;
    const int h = (ctrl & 0x20) ? 16 : 8;
    int row = sl - (oam[0] + 1);
    if (row < 0 || row >= h) return;
    uint8_t bg[W] = {};
    fetchBg(bg, 0, false);
    const uint8_t tile = oam[1], attr = oam[2]; const int sx0 = oam[3];
    if (attr & 0x80) row = h - 1 - row;
    uint16_t pat;
    if (h == 8) pat = ((ctrl & 0x08) ? 0x1000 : 0) + tile * 16 + row;
    else        pat = ((tile & 1) ? 0x1000 : 0) + (tile & 0xFE) * 16 + (row & 7) + (row >= 8 ? 16 : 0);
    const uint8_t lo = vramRead(pat), hi = vramRead(pat + 8);
    for (int px = 0; px < 8; px++) {
        const int x = sx0 + px;
        if (x >= W) break;
        const int bit = (attr & 0x40) ? px : 7 - px;
        if (!(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1))) continue;
        if (x < 8 && !(mask & 0x04)) continue;
        if (bg[x] && x != 255) { spr0HitDot = x + 1; return; }
    }
}

void Ppu::copyStateFrom(const Ppu& o) {
    ctrl = o.ctrl; mask = o.mask; status = o.status;
    std::memcpy(oam, o.oam, sizeof oam); std::memcpy(palette, o.palette, sizeof palette); std::memcpy(vram, o.vram, sizeof vram);
    scanline = o.scanline; cycle = o.cycle;
    v = o.v; t = o.t; fineX = o.fineX; w = o.w; oamAddr = o.oamAddr; readBuffer = o.readBuffer; oddFrame = o.oddFrame; spr0HitDot = o.spr0HitDot;
    nmiOut = o.nmiOut; frameReady = o.frameReady;
    smRaw = -1; smDisp = 0; smAdj = 0;
}

void Ppu::renderLine(int sl) {
    uint32_t* out = &framebuffer[sl * W];
    lineCtrl[sl] = ctrl;
    lineMask[sl] = mask;
    lineScrollX[sl] = (uint16_t)(((v >> 10 & 1) << 8) | ((v & 31) << 3) | fineX);
    const bool bgOn = mask & 0x08, spOn = mask & 0x10;
    int lineY = (v >> 11 & 1) * 240 + (((v >> 5) & 31) << 3) + ((v >> 12) & 7);   // the game's scroll Y of this line (0..479)
    if (sl == kSmoothFirstLine) smoothStep(bgOn ? lineY : -1);
    if (sl >= kSmoothFirstLine && smAdj != 0) lineY = ((lineY + smAdj) % 480 + 480) % 480;
    lineScrollY[sl] = (uint16_t)lineY;
    const bool gray = mask & 0x01;
    const uint32_t backdrop = kPalette[palette[0] & (gray ? 0x30 : 0x3F)];

    buildSpriteLayerLine(sl);

    if (!bgOn && !spOn) {
        for (int x = 0; x < W; x++) {
            out[x] = 0xFF000000 | backdrop;
            fbColor[sl * W + x] = bgColor[sl * W + x] = 0xFF000000 | kPalette[palette[0] & 0x3F];
            fbIdx[sl * W + x] = palette[0] & 0x3F;
        }
        return;
    }

    // ---- Background: 4 bits per pixel = (palette << 2) | color, 0 = transparent ----
    uint8_t bg[W] = {};
    if (bgOn) fetchBg(bg, lineY, sl >= kSmoothFirstLine && smAdj != 0);

    // ---- Sprites ----
    // sp: bit7 = present, bit6 = behind background, bits 0-3 = (palette<<2)|color
    uint8_t sp[W] = {};
    if (spOn) {
        const int h = (ctrl & 0x20) ? 16 : 8;
        int found = 0;
        int list[64];
        for (int i = 0; i < 64; i++) {
            int row = sl - (oam[i * 4] + 1);
            if (row < 0 || row >= h) continue;
            if (found == 8) { status |= 0x20; if (!unlimitedSprites) break; }
            list[found++] = i;
        }
        for (int n = 0; n < found; n++) {
            int i = list[n];
            int row = sl - (oam[i * 4] + 1);
            uint8_t tile = oam[i * 4 + 1], attr = oam[i * 4 + 2];
            int sx0 = oam[i * 4 + 3];
            if (attr & 0x80) row = h - 1 - row;
            uint16_t pat;
            if (h == 8) {
                pat = ((ctrl & 0x08) ? 0x1000 : 0) + tile * 16 + row;
            } else {
                pat = ((tile & 1) ? 0x1000 : 0) + (tile & 0xFE) * 16 + (row & 7) + (row >= 8 ? 16 : 0);
            }
            uint8_t lo = vramRead(pat), hi = vramRead(pat + 8);
            for (int px = 0; px < 8; px++) {
                int x = sx0 + px;
                if (x >= W) break;
                int bit = (attr & 0x40) ? px : 7 - px;
                uint8_t c = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                if (!c) continue;
                if (x < 8 && !(mask & 0x04)) continue;
                if (i == 0 && bg[x] && x != 255 && bgOn && spr0HitDot < 0 && !(status & 0x40))
                    spr0HitDot = x + 1;
                if (!(sp[x] & 0x80))
                    sp[x] = 0x80 | ((attr & 0x20) ? 0x40 : 0) | (((attr & 3) << 2) | c);
            }
        }
    }

    // ---- Compose ----
    const uint8_t grayMask = gray ? 0x30 : 0x3F;
    for (int x = 0; x < W; x++) {
        uint8_t color;
        bool haveSp = sp[x] & 0x80, haveBg = bg[x] != 0;
        if (haveSp && (!haveBg || !(sp[x] & 0x40))) color = palette[0x10 + (sp[x] & 0x0F)];
        else if (haveBg)                             color = palette[bg[x] & 0x0F];
        else                                         color = palette[0];
        out[x] = 0xFF000000 | kPalette[color & grayMask];
        fbColor[sl * W + x] = 0xFF000000 | kPalette[color & 0x3F];
        fbIdx[sl * W + x] = color & 0x3F;
        bgColor[sl * W + x] = 0xFF000000 | kPalette[(haveBg ? palette[bg[x] & 0x0F] : palette[0]) & 0x3F];
    }
}
