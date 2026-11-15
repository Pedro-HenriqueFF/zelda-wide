#pragma once
#include "cart.h"
#include "cpu.h"
#include "ppu.h"
#include <cstdint>
#include <string>

// Joypad bit layout (matches the order the game reads the shift register).
enum Button : uint8_t {
    BTN_A = 0x01, BTN_B = 0x02, BTN_SELECT = 0x04, BTN_START = 0x08,
    BTN_UP = 0x10, BTN_DOWN = 0x20, BTN_LEFT = 0x40, BTN_RIGHT = 0x80
};

class Nes {
public:
    Nes() : cpu(*this) { ppu.attach(&cart); }
    Nes(const Nes&) = delete;               // (the CPU holds a reference to its Nes and the PPU a pointer to its Cart: a plain copy would point at the original)
    Nes& operator=(const Nes&) = delete;

    // Shadow game: makes this Nes a copy of `src` (RAM, cartridge with its PRG patches, PRG-RAM, CHR-RAM, CPU, PPU registers/OAM/palette/VRAM,
    // controller state); the PPU picture buffers are not copied. With `lite` the PPU draws nothing, only the sprite-0 hit the game waits for.
    void cloneFrom(const Nes& src, bool lite = true);

    void reset();
    void runFrame();                       // run until the PPU finishes a frame

    uint8_t cpuRead(uint16_t addr);
    void    cpuWrite(uint16_t addr, uint8_t v);

    uint8_t pad1 = 0;                      // live state of controller 1, set by the front-end

    uint8_t ram[0x800] = {};
    Cart cart;
    Cpu  cpu;
    Ppu  ppu;

    // No lag: the game's logic runs inside its NMI handler with NMI switched off, so when a frame's logic takes longer than a frame the
    // next vblank's NMI is lost and the game runs at half speed (Z_07.asm 388-526). With lagFree the picture waits at the end of the visible
    // area (the PPU stands still, the CPU goes on) until the handler is done, for at most kLagBudget CPU cycles.
    bool    lagFree = false;
    int     lagCycles = 0;                       // the CPU cycles given to the handler that way in the last frame
    static constexpr int kLagBudget = 45000;
private:
    bool    strobe = false;
    uint8_t padShift = 0;
    int     dmaStall = 0;
};
