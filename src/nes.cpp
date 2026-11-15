#include "nes.h"
#include <cstring>

void Nes::reset() {
    std::memset(ram, 0, sizeof ram);
    ppu.reset();
    cpu.reset();
    strobe = false; padShift = 0; dmaStall = 0;
}

void Nes::cloneFrom(const Nes& src, bool lite) {
    std::memcpy(ram, src.ram, sizeof ram);
    cart = src.cart;                                  // (vectors and MMC1 registers; the PPU keeps pointing at our own `cart`)
    cpu.copyStateFrom(src.cpu);
    ppu.copyStateFrom(src.ppu);
    ppu.lite = lite; ppu.unlimitedSprites = false; ppu.smoothV = false;
    pad1 = src.pad1; strobe = src.strobe; padShift = src.padShift; dmaStall = src.dmaStall;
    lagFree = src.lagFree; lagCycles = 0;
}

void Nes::runFrame() {
    ppu.frameReady = false;
    lagCycles = 0;
    while (!ppu.frameReady) {
        if (lagFree && cpu.inNmi() && ppu.nearVblank() && lagCycles < kLagBudget) {
            int c = cpu.step();                  // the handler is still working when vblank is due: it gets the time, the PPU waits
            if (dmaStall) { c += dmaStall; dmaStall = 0; }
            lagCycles += c;
            continue;
        }
        int cycles = cpu.step();
        if (dmaStall) { cycles += dmaStall; dmaStall = 0; }
        ppu.step(cycles * 3);
        if (ppu.nmiOut) { ppu.nmiOut = false; cpu.requestNmi(); }
    }
}

uint8_t Nes::cpuRead(uint16_t addr) {
    if (addr < 0x2000) return ram[addr & 0x7FF];
    if (addr < 0x4000) return ppu.readReg(addr);
    if (addr == 0x4016) {
        if (strobe) padShift = pad1;
        uint8_t bit = padShift & 1;
        padShift = (padShift >> 1) | 0x80;      // after 8 reads the real hardware returns 1s
        return bit | 0x40;
    }
    if (addr < 0x6000) return 0;                 // APU / unmapped: silent for now
    return cart.cpuRead(addr);
}

void Nes::cpuWrite(uint16_t addr, uint8_t v) {
    if (addr < 0x2000) { ram[addr & 0x7FF] = v; return; }
    if (addr < 0x4000) { ppu.writeReg(addr, v); return; }
    if (addr == 0x4014) {                        // OAM DMA
        uint16_t base = (uint16_t)v << 8;
        for (int i = 0; i < 256; i++) ppu.writeOam(cpuRead(base + i));
        dmaStall = 513 + (int)(cpu.totalCycles & 1);
        return;
    }
    if (addr == 0x4016) {
        bool newStrobe = v & 1;
        if (strobe && !newStrobe) padShift = pad1;
        strobe = newStrobe;
        if (strobe) padShift = pad1;
        return;
    }
    if (addr < 0x6000) return;                   // APU registers: ignored until sound milestone
    cart.cpuWrite(addr, v);
}
