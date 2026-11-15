#include "test_wide_common.h"

int checks = 0, failures = 0;
LinkPixels g_link;
std::vector<ExtraPixel> g_extra;

int gScrollY = 0;
int gScrollX = 0;   // the nametable scroll the fake frames report (a room sliding sideways)
std::vector<uint32_t> gBg;
void syncFb(Nes& nes) {
    for (int i = 0; i < Ppu::W * Ppu::H; i++) {
        nes.ppu.fbColor[i] = nes.ppu.framebuffer[i];
        nes.ppu.bgColor[i] = gBg.size() == (size_t)Ppu::W * Ppu::H ? gBg[i] : nes.ppu.framebuffer[i];
        nes.ppu.fbIdx[i] = 0x16;
    }
    for (int y = 0; y < Ppu::H; y++) { nes.ppu.lineScrollX[y] = (uint16_t)(((gScrollX % 512) + 512) % 512); nes.ppu.lineScrollY[y] = (uint16_t)((((y + gScrollY) % 480) + 480) % 480); }   // (a room at rest: the nametable starts at the picture's corner)
    if (g_link.on)
        for (int y = g_link.y; y < g_link.y + 4; y++)
            for (int x = g_link.x; x < g_link.x + 4; x++) nes.ppu.fbColor[y * Ppu::W + x] = 0xFFFFFEFF;
    for (const ExtraPixel& e : g_extra) nes.ppu.fbColor[e.y * Ppu::W + e.x] = e.color;
}

static std::vector<uint8_t> makeFakeZeldaRom() {
    std::vector<uint8_t> img(16 + 8 * 16384, 0);
    const uint8_t hdr[16] = {'N','E','S',0x1A, 8, 0, 0x12, 0};
    std::memcpy(img.data(), hdr, 16);
    uint8_t* prg = &img[16];
    // Column directory (heap 0 must be $9BD8 to line up with the layout offset), signature + remaining entries.
    const uint16_t dir[16] = {0x9BD8, 0x9C0D, 0x9C3E, 0x9C80, 0x9CC4, 0x9CF6, 0x9D32, 0x9D6D,
                              0x9DA8, 0x9DE6, 0x9E27, 0x9E6C, 0x9EA9, 0x9EDF, 0x9F21, 0x9F55};
    for (int i = 0; i < 16; i++) { prg[0x100 + i * 2] = dir[i] & 0xFF; prg[0x100 + i * 2 + 1] = dir[i] >> 8; }
    const uint8_t pri[24] = {0x24,0x6F,0xF3,0xFA,0x98,0x90,0x8F,0x95,0x8E,0x90,0x74,0x76,0xF3,0x24,0x26,0x89,0x03,0x04,0x70,0xC8,0xBC,0x8D,0x8F,0x93};
    std::memcpy(prg + 0x200, pri, 24);
    const uint8_t sec[16] = {0x24,0x24,0x24,0x24,0x6F,0x6F,0x6F,0x6F,0xF3,0xF3,0xF3,0xF3,0xFA,0xFA,0xFA,0xFA};
    std::memcpy(prg + 0x300, sec, 16);
    // Heap 0, column 0: 0x80|0x10 then ten more squares of index 0x10 -> primary $03 -> tiles 3,4,5,6
    size_t heap0 = 5 * 16384 + (0x9BD8 - 0x8000);
    prg[heap0] = 0x80 | 0x10;
    for (int i = 1; i < 11; i++) prg[heap0 + i] = 0x10;
    // BorderBounds (the third set is the one masking the sword)
    const uint8_t bb[12] = {0xD6,0x45,0xE9,0x07,0xC6,0x55,0xD9,0x17,0xBE,0x54,0xD1,0x1F};
    std::memcpy(prg + 0x400, bb, 12);
    const uint8_t beam[10] = {0xB5,0x70,0xC9,0x14,0x90,0x05,0xC9,0xEC,0xB0,0x03};   // MakeSwordShot's X check
    std::memcpy(prg + 0x440, beam, 10);
    const uint8_t hearts[11] = {0x29,0xF0,0x85,0x0C,0x4A,0x4A,0x4A,0x4A,0x05,0x0C,0x48};   // the file-select hearts (Z_02.asm)
    std::memcpy(prg + 0x460, hearts, 11);
    // Reset vector (not used here)
    prg[7 * 16384 + 0x3FFC] = 0x00; prg[7 * 16384 + 0x3FFD] = 0xC0;
    return img;
}

void Ctx::tile(int id, uint8_t lo, uint8_t hi) {
    for (int r = 0; r < 8; r++) { nes->cart.ppuWrite(id * 16 + r, lo); nes->cart.ppuWrite(id * 16 + 8 + r, hi); }
}

void Ctx::setState(int mode, int room, int level) {
    nes->ram[0x12] = mode; nes->ram[0x10] = level; nes->ram[0xEB] = room; nes->ram[0x16] = 0;
    nes->ram[0x70] = 120; nes->ram[0x84] = 141; nes->ram[0xE1] = 0; nes->ram[0x11] = 1;
    nes->ram[0x13] = 0; nes->ram[0x5A] = 0;
}

void Ctx::frames(Widescreen& w2, int n, double asp) {
    for (int k = 0; k < n; k++) { syncFb(*nes); w2.snapCamera(); w2.render(*nes, asp, c); }
}

bool Ctx::setup() {
    if (!nes->cart.loadFromMemory(makeFakeZeldaRom(), err)) { std::printf("rom: %s\n", err.c_str()); return false; }
    nes->reset();

    ws.hudOverlay = false;                                   // the older tests use the original status bar on top
    ws.caveBox = false;                                      // ...and caves replacing the picture
    ws.unfogAnim = false; ws.cloudShadow = false; ws.lighting = false;                                    // ...and new screens appearing at once
    if (!ws.init(*nes, err)) { std::printf("init failed: %s\n", err.c_str()); return false; }
    CHECK(ws.available());

    // CHR: tile 3 = colour 1, tile 4 = colour 2, tile 5 = colour 3, tile 6 = empty.
    tile(3, 0xFF, 0x00); tile(4, 0x00, 0xFF); tile(5, 0xFF, 0xFF); tile(6, 0, 0);
    nes->ppu.palette[0] = 0x0F;
    nes->ppu.palette[11] = 0x16;                           // palette 2, colour 3
    for (int y = 0; y < Ppu::H; y++) { nes->ppu.lineMask[y] = 0x1E; nes->ppu.lineCtrl[y] = 0; }
    for (int i = 0; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = 0xFF123456;

    // Every room uses layout 0, outer palette 1, inner palette 2.
    for (int r = 0; r < 128; r++) {
        nes->cart.cpuWrite(0x69FE + r, 0);
        nes->cart.cpuWrite(0x687E + r, 1);
        nes->cart.cpuWrite(0x68FE + r, 2);
    }
    // Make the game's live screen identical to our decode so the screen counts as a normal overworld screen.
    map.init(nes->cart.prgData(), err);
    map.decodeRoom(0, false, decoded);
    CHECK_EQ(decoded[0], 3); CHECK_EQ(decoded[1], 4); CHECK_EQ(decoded[22], 5); CHECK_EQ(decoded[23], 6);
    for (int i = 0; i < OverworldMap::TILES; i++) nes->cart.cpuWrite(0x6530 + i, decoded[i]);

    nes->ppu.palette[7] = 0x21;                              // palette 1, colour 3 (outer area of a screen)
    nes->ppu.palette[5] = 0x2A;                              // palette 1, colour 1
    for (int i = 0; i < Ppu::W * 64; i++) nes->ppu.framebuffer[i] = 0xFF654321;   // status bar rows

    ws.viewHeight = 176;                                     // one screen tall: canvas is 240 high
    return true;
}
