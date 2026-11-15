#pragma once
#include <cstdint>

class Nes;

// MOS 6502 as found in the NES (2A03): no decimal mode.
// Executes one whole instruction per step() and reports how many cycles it took.
class Cpu {
public:
    explicit Cpu(Nes& bus) : nes(bus) {}

    void reset();
    int  step();                       // run one instruction (or service a pending NMI)
    void requestNmi() { nmiPending = true; }
    void copyStateFrom(const Cpu& o) {         // everything but the bus (a clone of a Nes keeps its own)
        a = o.a; x = o.x; y = o.y; sp = o.sp; p = o.p; pc = o.pc; totalCycles = o.totalCycles; badOpcode = o.badOpcode;
        nmiPending = o.nmiPending; nmiActive = o.nmiActive; nmiReturnSp = o.nmiReturnSp;
    }
    bool inNmi() const { return nmiActive; }   // an NMI handler is running (from its entry until the RTI that returns to where the NMI came in)

    uint8_t  a = 0, x = 0, y = 0, sp = 0xFD, p = 0x24;
    uint16_t pc = 0;
    uint64_t totalCycles = 0;
    int      badOpcode = -1;           // set to the opcode if we ever hit an unofficial one

    enum Flag : uint8_t { C = 0x01, Z = 0x02, I = 0x04, D = 0x08, B = 0x10, U = 0x20, V = 0x40, N = 0x80 };

private:
    Nes& nes;
    bool nmiPending = false, nmiActive = false;
    uint8_t nmiReturnSp = 0;

    uint8_t  rd(uint16_t addr);
    void     wr(uint16_t addr, uint8_t v);
    uint16_t rd16(uint16_t addr);
    void     push(uint8_t v)  { wr(0x100 + sp--, v); }
    uint8_t  pull()           { return rd(0x100 + ++sp); }
    void     setZN(uint8_t v) { p = (p & ~(Z | N)) | (v == 0 ? Z : 0) | (v & N); }
    void     setFlag(uint8_t f, bool on) { if (on) p |= f; else p &= ~f; }
    void     adc(uint8_t m);
    void     compare(uint8_t reg, uint8_t m);
};
