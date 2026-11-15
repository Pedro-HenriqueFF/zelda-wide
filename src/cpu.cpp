#include "cpu.h"
#include "nes.h"

namespace {

enum Mode : uint8_t { IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL };

enum Ins : uint8_t {
    ILL, ADC, AND, ASL, BCC, BCS, BEQ, BIT, BMI, BNE, BPL, BRK, BVC, BVS, CLC, CLD, CLI, CLV,
    CMP, CPX, CPY, DEC, DEX, DEY, EOR, INC, INX, INY, JMP, JSR, LDA, LDX, LDY, LSR, NOP, ORA,
    PHA, PHP, PLA, PLP, ROL, ROR, RTI, RTS, SBC, SEC, SED, SEI, STA, STX, STY, TAX, TAY, TSX,
    TXA, TXS, TYA
};

struct OpInfo { Ins ins; Mode mode; uint8_t cycles; bool pageCrossPenalty; };

OpInfo table[256];

void def(uint8_t opc, Ins i, Mode m, uint8_t cyc, bool pen = false) { table[opc] = {i, m, cyc, pen}; }

// Eight "standard" read instructions share the same addressing layout.
void defAlu(Ins i, uint8_t base) {
    def(base + 0x09, i, IMM, 2);
    def(base + 0x05, i, ZP, 3);
    def(base + 0x15, i, ZPX, 4);
    def(base + 0x0D, i, ABS, 4);
    def(base + 0x1D, i, ABX, 4, true);
    def(base + 0x19, i, ABY, 4, true);
    def(base + 0x01, i, IZX, 6);
    def(base + 0x11, i, IZY, 5, true);
}

void defShift(Ins i, uint8_t base) {
    def(base + 0x0A, i, ACC, 2);
    def(base + 0x06, i, ZP, 5);
    def(base + 0x16, i, ZPX, 6);
    def(base + 0x0E, i, ABS, 6);
    def(base + 0x1E, i, ABX, 7);
}

struct TableInit {
    TableInit() {
        for (auto& e : table) e = {ILL, IMP, 2, false};

        defAlu(ORA, 0x00);   // base 0x00 -> opcodes 09,05,15,0D,1D,19,01,11
        defAlu(AND, 0x20);
        defAlu(EOR, 0x40);
        defAlu(ADC, 0x60);
        defAlu(LDA, 0xA0);
        defAlu(CMP, 0xC0);
        defAlu(SBC, 0xE0);

        // STA: no immediate, and abs,X / abs,Y / (zp),Y always take the extra cycle.
        def(0x85, STA, ZP, 3);  def(0x95, STA, ZPX, 4); def(0x8D, STA, ABS, 4);
        def(0x9D, STA, ABX, 5); def(0x99, STA, ABY, 5); def(0x81, STA, IZX, 6); def(0x91, STA, IZY, 6);

        defShift(ASL, 0x00); defShift(ROL, 0x20); defShift(LSR, 0x40); defShift(ROR, 0x60);

        def(0xA2, LDX, IMM, 2); def(0xA6, LDX, ZP, 3); def(0xB6, LDX, ZPY, 4);
        def(0xAE, LDX, ABS, 4); def(0xBE, LDX, ABY, 4, true);
        def(0xA0, LDY, IMM, 2); def(0xA4, LDY, ZP, 3); def(0xB4, LDY, ZPX, 4);
        def(0xAC, LDY, ABS, 4); def(0xBC, LDY, ABX, 4, true);
        def(0x86, STX, ZP, 3); def(0x96, STX, ZPY, 4); def(0x8E, STX, ABS, 4);
        def(0x84, STY, ZP, 3); def(0x94, STY, ZPX, 4); def(0x8C, STY, ABS, 4);

        def(0xE0, CPX, IMM, 2); def(0xE4, CPX, ZP, 3); def(0xEC, CPX, ABS, 4);
        def(0xC0, CPY, IMM, 2); def(0xC4, CPY, ZP, 3); def(0xCC, CPY, ABS, 4);

        def(0xC6, DEC, ZP, 5); def(0xD6, DEC, ZPX, 6); def(0xCE, DEC, ABS, 6); def(0xDE, DEC, ABX, 7);
        def(0xE6, INC, ZP, 5); def(0xF6, INC, ZPX, 6); def(0xEE, INC, ABS, 6); def(0xFE, INC, ABX, 7);

        def(0x24, BIT, ZP, 3); def(0x2C, BIT, ABS, 4);

        def(0x10, BPL, REL, 2); def(0x30, BMI, REL, 2); def(0x50, BVC, REL, 2); def(0x70, BVS, REL, 2);
        def(0x90, BCC, REL, 2); def(0xB0, BCS, REL, 2); def(0xD0, BNE, REL, 2); def(0xF0, BEQ, REL, 2);

        def(0x4C, JMP, ABS, 3); def(0x6C, JMP, IND, 5); def(0x20, JSR, ABS, 6);
        def(0x60, RTS, IMP, 6); def(0x40, RTI, IMP, 6); def(0x00, BRK, IMP, 7);

        def(0x48, PHA, IMP, 3); def(0x08, PHP, IMP, 3); def(0x68, PLA, IMP, 4); def(0x28, PLP, IMP, 4);

        def(0x18, CLC, IMP, 2); def(0xD8, CLD, IMP, 2); def(0x58, CLI, IMP, 2); def(0xB8, CLV, IMP, 2);
        def(0x38, SEC, IMP, 2); def(0xF8, SED, IMP, 2); def(0x78, SEI, IMP, 2);

        def(0xAA, TAX, IMP, 2); def(0xA8, TAY, IMP, 2); def(0xBA, TSX, IMP, 2);
        def(0x8A, TXA, IMP, 2); def(0x9A, TXS, IMP, 2); def(0x98, TYA, IMP, 2);
        def(0xCA, DEX, IMP, 2); def(0x88, DEY, IMP, 2); def(0xE8, INX, IMP, 2); def(0xC8, INY, IMP, 2);
        def(0xEA, NOP, IMP, 2);
    }
} tableInit;

} // namespace

uint8_t  Cpu::rd(uint16_t addr)            { return nes.cpuRead(addr); }
void     Cpu::wr(uint16_t addr, uint8_t v) { nes.cpuWrite(addr, v); }
uint16_t Cpu::rd16(uint16_t addr) {
    uint8_t lo = rd(addr);
    uint8_t hi = rd(addr + 1);
    return lo | (hi << 8);
}

void Cpu::reset() {
    a = x = y = 0;
    sp = 0xFD;
    p = 0x24;
    pc = rd16(0xFFFC);
    nmiPending = false; nmiActive = false;
    totalCycles = 7;
}

void Cpu::adc(uint8_t m) {
    unsigned sum = a + m + (p & C);
    setFlag(V, (~(a ^ m) & (a ^ sum) & 0x80) != 0);
    setFlag(C, sum > 0xFF);
    a = (uint8_t)sum;
    setZN(a);
}

void Cpu::compare(uint8_t reg, uint8_t m) {
    setFlag(C, reg >= m);
    setZN((uint8_t)(reg - m));
}

int Cpu::step() {
    if (nmiPending) {
        nmiPending = false;
        nmiActive = true; nmiReturnSp = sp;
        push(pc >> 8);
        push(pc & 0xFF);
        push((p & ~B) | U);
        p |= I;
        pc = rd16(0xFFFA);
        totalCycles += 7;
        return 7;
    }

    const uint8_t opc = rd(pc++);
    const OpInfo& op = table[opc];
    int cycles = op.cycles;

    uint16_t addr = 0;
    bool crossed = false;

    switch (op.mode) {
        case IMP: case ACC: break;
        case IMM: addr = pc++; break;
        case ZP:  addr = rd(pc++); break;
        case ZPX: addr = (uint8_t)(rd(pc++) + x); break;
        case ZPY: addr = (uint8_t)(rd(pc++) + y); break;
        case ABS: addr = rd16(pc); pc += 2; break;
        case ABX: { uint16_t b = rd16(pc); pc += 2; addr = b + x; crossed = (b ^ addr) & 0xFF00; break; }
        case ABY: { uint16_t b = rd16(pc); pc += 2; addr = b + y; crossed = (b ^ addr) & 0xFF00; break; }
        case IND: {
            uint16_t ptr = rd16(pc); pc += 2;
            uint8_t lo = rd(ptr);
            uint8_t hi = rd((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));   // the famous page-wrap bug
            addr = lo | (hi << 8);
            break;
        }
        case IZX: {
            uint8_t z = (uint8_t)(rd(pc++) + x);
            uint8_t lo = rd(z);
            uint8_t hi = rd((uint8_t)(z + 1));
            addr = lo | (hi << 8);
            break;
        }
        case IZY: {
            uint8_t z = rd(pc++);
            uint8_t lo = rd(z);
            uint8_t hi = rd((uint8_t)(z + 1));
            uint16_t b = lo | (hi << 8);
            addr = b + y;
            crossed = (b ^ addr) & 0xFF00;
            break;
        }
        case REL: {
            int8_t off = (int8_t)rd(pc++);
            addr = pc + off;
            break;
        }
    }
    if (crossed && op.pageCrossPenalty) cycles++;

    auto branch = [&](bool cond) {
        if (cond) {
            cycles++;
            if ((pc ^ addr) & 0xFF00) cycles++;
            pc = addr;
        }
    };

    // Read-modify-write helper for the shift/rotate/inc/dec family.
    auto rmw = [&](auto fn) {
        if (op.mode == ACC) { a = fn(a); }
        else { uint8_t m = rd(addr); m = fn(m); wr(addr, m); }
    };

    switch (op.ins) {
        case ADC: adc(rd(addr)); break;
        case SBC: adc(~rd(addr)); break;
        case AND: a &= rd(addr); setZN(a); break;
        case ORA: a |= rd(addr); setZN(a); break;
        case EOR: a ^= rd(addr); setZN(a); break;
        case LDA: a = rd(addr); setZN(a); break;
        case LDX: x = rd(addr); setZN(x); break;
        case LDY: y = rd(addr); setZN(y); break;
        case STA: wr(addr, a); break;
        case STX: wr(addr, x); break;
        case STY: wr(addr, y); break;
        case CMP: compare(a, rd(addr)); break;
        case CPX: compare(x, rd(addr)); break;
        case CPY: compare(y, rd(addr)); break;
        case BIT: {
            uint8_t m = rd(addr);
            setFlag(Z, (a & m) == 0);
            setFlag(N, m & 0x80);
            setFlag(V, m & 0x40);
            break;
        }
        case ASL: rmw([&](uint8_t m) { setFlag(C, m & 0x80); m <<= 1; setZN(m); return m; }); break;
        case LSR: rmw([&](uint8_t m) { setFlag(C, m & 0x01); m >>= 1; setZN(m); return m; }); break;
        case ROL: rmw([&](uint8_t m) { uint8_t c = p & C; setFlag(C, m & 0x80); m = (m << 1) | c; setZN(m); return m; }); break;
        case ROR: rmw([&](uint8_t m) { uint8_t c = p & C; setFlag(C, m & 0x01); m = (m >> 1) | (c << 7); setZN(m); return m; }); break;
        case INC: rmw([&](uint8_t m) { m++; setZN(m); return m; }); break;
        case DEC: rmw([&](uint8_t m) { m--; setZN(m); return m; }); break;
        case INX: x++; setZN(x); break;
        case INY: y++; setZN(y); break;
        case DEX: x--; setZN(x); break;
        case DEY: y--; setZN(y); break;

        case BPL: branch(!(p & N)); break;
        case BMI: branch(p & N); break;
        case BVC: branch(!(p & V)); break;
        case BVS: branch(p & V); break;
        case BCC: branch(!(p & C)); break;
        case BCS: branch(p & C); break;
        case BNE: branch(!(p & Z)); break;
        case BEQ: branch(p & Z); break;

        case JMP: pc = addr; break;
        case JSR: { uint16_t ret = pc - 1; push(ret >> 8); push(ret & 0xFF); pc = addr; break; }
        case RTS: { uint8_t lo = pull(); uint8_t hi = pull(); pc = (lo | (hi << 8)) + 1; break; }
        case RTI: {
            p = (pull() & ~B) | U;
            uint8_t lo = pull(); uint8_t hi = pull();
            pc = lo | (hi << 8);
            if (nmiActive && sp == nmiReturnSp) nmiActive = false;
            break;
        }
        case BRK: {
            pc++;                                   // BRK has a padding byte
            push(pc >> 8); push(pc & 0xFF);
            push(p | B | U);
            p |= I;
            pc = rd16(0xFFFE);
            break;
        }

        case PHA: push(a); break;
        case PHP: push(p | B | U); break;
        case PLA: a = pull(); setZN(a); break;
        case PLP: p = (pull() & ~B) | U; break;

        case CLC: p &= ~C; break;
        case CLD: p &= ~D; break;
        case CLI: p &= ~I; break;
        case CLV: p &= ~V; break;
        case SEC: p |= C; break;
        case SED: p |= D; break;          // stored, but ignored: the 2A03 has no decimal mode
        case SEI: p |= I; break;

        case TAX: x = a; setZN(x); break;
        case TAY: y = a; setZN(y); break;
        case TSX: x = sp; setZN(x); break;
        case TXA: a = x; setZN(a); break;
        case TXS: sp = x; break;
        case TYA: a = y; setZN(a); break;

        case NOP: break;
        case ILL: default:
            badOpcode = opc;                // unofficial opcode: treated as a 2-cycle NOP
            break;
    }

    totalCycles += cycles;
    return cycles;
}
