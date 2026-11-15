#pragma once
#include <cstdint>
#include <string>
#include <vector>

// iNES cartridge. Implements mapper 0 (NROM) and mapper 1 (MMC1), which is what Zelda 1 uses.
class Cart {
public:
    bool load(const std::string& path, std::string& error);
    bool loadFromMemory(const std::vector<uint8_t>& image, std::string& error);

    uint8_t cpuRead(uint16_t addr);
    void    cpuWrite(uint16_t addr, uint8_t v);
    uint8_t ppuRead(uint16_t addr);
    void    ppuWrite(uint16_t addr, uint8_t v);

    // 0 = one-screen (lower), 1 = one-screen (upper), 2 = vertical, 3 = horizontal
    int mirroring() const { return mirrorMode; }

    // Battery-backed save RAM ($6000-$7FFF).
    bool hasBattery() const { return battery; }
    std::vector<uint8_t>& saveRam() { return prgRam; }
    bool saveRamDirty = false;

    int mapper = 0;

    // Handy for the widescreen/fog code later: direct access to the raw PRG image.
    const std::vector<uint8_t>& prgData() const { return prg; }
    void patchPrg(size_t offset, uint8_t v) { if (offset < prg.size()) prg[offset] = v; }   // (used for the border-bounds tweak only)

private:
    std::vector<uint8_t> prg, chr, prgRam;
    bool chrIsRam = false;
    bool battery = false;
    int  mirrorMode = 3;

    // MMC1 state
    uint8_t shift = 0x10;
    uint8_t control = 0x0C, chrBank0 = 0, chrBank1 = 0, prgBank = 0;

    void mmc1Write(uint16_t addr, uint8_t v);
    size_t prgOffset(uint16_t addr) const;
    size_t chrOffset(uint16_t addr) const;
};
