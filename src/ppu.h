#pragma once
#include <cstdint>

class Cart;

// Dot-accurate-enough NES PPU: registers, loopy scrolling, sprite 0 hit, NMI.
// Each visible scanline is rendered in one go on dot 1, using the scroll state at that moment.
class Ppu {
public:
    static constexpr int W = 256, H = 240;

    void attach(Cart* c) { cart = c; }
    void reset();
    void step(int dots);

    uint8_t  readReg(uint16_t addr);
    void     writeReg(uint16_t addr, uint8_t v);
    void     buildSpriteLayer() { for (int sl = 0; sl < 240; sl++) buildSpriteLayerLine(sl); }   // the sprite layer of the whole picture from the OAM (a shadow game's, which is lite)
    void     copyStateFrom(const Ppu& o);   // registers, OAM, palette and VRAM only (not the picture buffers, not the cart pointer)
    void     writeOam(uint8_t value) { oam[oamAddr++] = value; }   // used by $4014 DMA

    uint32_t framebuffer[W * H] = {};   // 0xAARRGGBB, as the real PPU shows it (grayscale bit applied)

    // The same picture split up for the widescreen compositor, which needs more than the finished image:
    uint32_t fbColor[W * H] = {};       // the finished picture WITHOUT the grayscale bit (the bomb "flash" turns it on)
    uint8_t  fbIdx[W * H] = {};         // NES colour index of each finished pixel (to apply grayscale later)
    uint32_t bgColor[W * H] = {};       // background only (no sprites), no grayscale
    // Sprite layer, one pixel wider than the screen on the right (8 columns) and taller (256 rows): sprites hanging
    // over the edges of the picture are kept here instead of being cut off.
    static constexpr int SPR_W = W + 8, SPR_H = 256;
    uint8_t  sprCol[SPR_W * SPR_H];     // NES colour index of the topmost sprite pixel, 0xFF = none
    uint8_t  sprOam[SPR_W * SPR_H];     // OAM index of the sprite that owns that pixel
    uint8_t  dropCol[SPR_W * SPR_H];    // like sprCol, for the sprites the PPU leaves out because a line already has 8 (not the ones drawn behind the background); 0xFF = none
    uint8_t  dropOam[SPR_W * SPR_H];    // OAM index of the sprite that owns that pixel
    bool     frameReady = false;
    bool     lite = false;              // shadow game: no picture is drawn, only the sprite-0 hit the game waits for (see liteLine)
    bool     unlimitedSprites = false;  // draw every sprite of a line, not just the first 8 (no flicker); off = the real PPU
    bool     nearVblank() const { return scanline == 240 && cycle >= 300; }   // the end of the visible area: the next ticks lead into vblank
    bool     nmiOut = false;            // set when the PPU wants the CPU to take an NMI

    // Exposed so the custom renderer can use them later.
    uint8_t  ctrl = 0, mask = 0, status = 0;
    uint8_t  oam[256] = {};
    uint8_t  palette[32] = {};
    uint8_t  vram[2048] = {};
    int      scanline = 0, cycle = 0;

    // Per-scanline snapshots of the state the line was drawn with (used by the widescreen renderer).
    uint8_t  lineCtrl[H] = {}, lineMask[H] = {};
    uint16_t lineScrollX[H] = {}, lineScrollY[H] = {};   // where the line starts in nametable space (x 0..511, y 0..479), for scrolling rooms

    // Smooth vertical scroll (dungeons): the game moves the scrolled play area 8 px every 4 frames; with smoothV on the picture eases over the steps at 2 px a frame
    // (the same speed as the sideways scroll). Only the lines from kSmoothFirstLine on move, lineScrollY holds the shown position. Set by the host before a frame.
    bool     smoothV = false;
    static constexpr int kSmoothFirstLine = 64;

    static uint32_t nesColor(uint8_t index);   // 0xFFRRGGBB for a NES palette index

    uint8_t  vramRead(uint16_t addr);
    void     vramWrite(uint16_t addr, uint8_t v);

private:
    Cart* cart = nullptr;

    uint16_t v = 0, t = 0;
    uint8_t  fineX = 0;
    bool     w = false;
    uint8_t  oamAddr = 0, readBuffer = 0;
    bool     oddFrame = false;
    int      spr0HitDot = -1;           // dot at which sprite-0-hit will become visible on this line

    bool renderingOn() const { return mask & 0x18; }
    uint16_t ntAddr(uint16_t a) const;
    void tick();
    void renderLine(int sl);
    void buildSpriteLayerLine(int sl);
    void incrementY();
    void fetchBg(uint8_t* bg, int lineY, bool moved);
    void liteLine(int sl);
    void smoothStep(int rawY);          // once per frame at kSmoothFirstLine: moves smDisp towards the game's scroll position
    int  smRaw = -1, smDisp = 0, smAdj = 0;   // the game's last scroll Y (-1 = none), the shown one, shown minus game's
};
