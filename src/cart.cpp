#include "cart.h"
#include <cstdio>
#include <fstream>
#include <iterator>

bool Cart::load(const std::string& path, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "Could not open ROM file: " + path; return false; }
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return loadFromMemory(image, error);
}

bool Cart::loadFromMemory(const std::vector<uint8_t>& img, std::string& error) {
    if (img.size() < 16 || img[0] != 'N' || img[1] != 'E' || img[2] != 'S' || img[3] != 0x1A) {
        error = "Not an iNES (.nes) file.";
        return false;
    }
    size_t prgSize = (size_t)img[4] * 16384;
    size_t chrSize = (size_t)img[5] * 8192;
    uint8_t flags6 = img[6], flags7 = img[7];
    mapper = (flags6 >> 4) | (flags7 & 0xF0);
    battery = flags6 & 0x02;
    bool trainer = flags6 & 0x04;

    if (mapper != 0 && mapper != 1) {
        error = "Unsupported mapper " + std::to_string(mapper) + " (only 0 and 1 are implemented).";
        return false;
    }
    if (prgSize == 0) { error = "ROM has no PRG data."; return false; }
    size_t offset = 16 + (trainer ? 512 : 0);
    if (img.size() < offset + prgSize + chrSize) { error = "ROM file is truncated."; return false; }

    prg.assign(img.begin() + offset, img.begin() + offset + prgSize);
    if (chrSize) {
        chr.assign(img.begin() + offset + prgSize, img.begin() + offset + prgSize + chrSize);
        chrIsRam = false;
    } else {
        chr.assign(8192, 0);
        chrIsRam = true;
    }
    prgRam.assign(8192, 0);

    mirrorMode = (flags6 & 1) ? 2 : 3;
    shift = 0x10; control = 0x0C; chrBank0 = chrBank1 = prgBank = 0;
    return true;
}

size_t Cart::prgOffset(uint16_t addr) const {
    size_t numBanks = prg.size() / 16384;
    if (mapper == 0) return (addr - 0x8000) % prg.size();

    int mode = (control >> 2) & 3;
    size_t bank;
    if (mode <= 1) {                                    // 32KB switching
        size_t b32 = (prgBank & 0x0E) % numBanks;
        return b32 * 16384 + (addr - 0x8000);
    } else if (mode == 2) {                             // first bank fixed at $8000
        bank = (addr < 0xC000) ? 0 : (prgBank & 0x0F) % numBanks;
    } else {                                            // last bank fixed at $C000
        bank = (addr < 0xC000) ? (prgBank & 0x0F) % numBanks : numBanks - 1;
    }
    return bank * 16384 + (addr & 0x3FFF);
}

size_t Cart::chrOffset(uint16_t addr) const {
    if (chrIsRam || mapper == 0) return addr & 0x1FFF;
    size_t numBanks4k = chr.size() / 4096;
    if (control & 0x10) {                               // two 4KB banks
        size_t b = (addr < 0x1000 ? chrBank0 : chrBank1) % numBanks4k;
        return b * 4096 + (addr & 0x0FFF);
    }
    size_t b = (chrBank0 & 0x1E) % numBanks4k;          // one 8KB bank
    return b * 4096 + (addr & 0x1FFF);
}

uint8_t Cart::cpuRead(uint16_t addr) {
    if (addr >= 0x8000) return prg[prgOffset(addr)];
    if (addr >= 0x6000) return prgRam[addr - 0x6000];
    return 0;
}

void Cart::cpuWrite(uint16_t addr, uint8_t v) {
    if (addr >= 0x8000) {
        if (mapper == 1) mmc1Write(addr, v);
    } else if (addr >= 0x6000) {
        if (prgRam[addr - 0x6000] != v) saveRamDirty = true;
        prgRam[addr - 0x6000] = v;
    }
}

void Cart::mmc1Write(uint16_t addr, uint8_t v) {
    if (v & 0x80) {                                     // reset
        shift = 0x10;
        control |= 0x0C;
        return;
    }
    bool complete = shift & 1;                          // marker bit has reached the bottom: this is write #5
    shift = (shift >> 1) | ((v & 1) << 4);
    if (!complete) return;

    uint8_t value = shift;
    shift = 0x10;
    switch ((addr >> 13) & 3) {
        case 0: control = value; mirrorMode = value & 3; break;
        case 1: chrBank0 = value; break;
        case 2: chrBank1 = value; break;
        case 3: prgBank = value & 0x0F; break;
    }
}

uint8_t Cart::ppuRead(uint16_t addr) { return chr[chrOffset(addr)]; }

void Cart::ppuWrite(uint16_t addr, uint8_t v) {
    if (chrIsRam) chr[chrOffset(addr)] = v;
}
