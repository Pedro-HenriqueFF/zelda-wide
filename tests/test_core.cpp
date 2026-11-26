// Headless self-tests for the emulator core. No SDL, no ROM needed.
//   Build:  g++ -std=c++17 -O1 -g -fsanitize=address,undefined -I../src test_core.cpp ../src/{cpu,cart,ppu,nes}.cpp -o test_core
#include <chrono>
#include "parallel.h"
#include "shadow.h"
#include <map>
#include <set>
#include "nes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { failures++; std::printf("FAIL %s:%d  %s == %s  (got 0x%llX, want 0x%llX)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

// ---------------------------------------------------------------- tiny assembler
struct Asm {
    std::vector<uint8_t> b;
    uint16_t base = 0xC000;
    uint16_t pc() const { return base + (uint16_t)b.size(); }
    Asm& raw(std::initializer_list<uint8_t> l) { b.insert(b.end(), l.begin(), l.end()); return *this; }
    Asm& i(uint8_t op) { b.push_back(op); return *this; }
    Asm& i1(uint8_t op, uint8_t v) { b.push_back(op); b.push_back(v); return *this; }
    Asm& i2(uint8_t op, uint16_t a) { b.push_back(op); b.push_back(a & 0xFF); b.push_back(a >> 8); return *this; }
    Asm& pad(uint16_t target) { while (pc() < target) b.push_back(0xEA); return *this; }
    // shorthands
    Asm& lda(uint8_t v) { return i1(0xA9, v); }
    Asm& ldx(uint8_t v) { return i1(0xA2, v); }
    Asm& ldy(uint8_t v) { return i1(0xA0, v); }
    Asm& sta(uint16_t a) { return i2(0x8D, a); }
    Asm& stz_zp(uint8_t a) { return i1(0x85, a); }   // STA zp
};

// Builds a 2x16KB MMC1 iNES image whose last bank starts with `code` at $C000.
static std::vector<uint8_t> makeRom(const std::vector<uint8_t>& code, uint16_t nmi = 0xC000, uint16_t irq = 0xC000, int prgBanks = 2) {
    std::vector<uint8_t> img(16 + prgBanks * 16384, 0xEA);
    const uint8_t hdr[16] = {'N','E','S',0x1A,(uint8_t)prgBanks,0,0x10,0,0,0,0,0,0,0,0,0};
    std::memcpy(img.data(), hdr, 16);
    size_t last = 16 + (prgBanks - 1) * 16384;
    std::memcpy(&img[last], code.data(), code.size());
    auto vec = [&](uint16_t at, uint16_t val) { img[last + (at - 0xC000)] = val & 0xFF; img[last + (at - 0xC000) + 1] = val >> 8; };
    vec(0xFFFA, nmi); vec(0xFFFC, 0xC000); vec(0xFFFE, irq);
    return img;
}

static bool boot(Nes& nes, const std::vector<uint8_t>& img) {
    std::string err;
    if (!nes.cart.loadFromMemory(img, err)) { std::printf("ROM load failed: %s\n", err.c_str()); return false; }
    nes.reset();
    return true;
}

static Cpu& runSteps(Nes& nes, int n) { for (int k = 0; k < n; k++) nes.cpu.step(); return nes.cpu; }

// ---------------------------------------------------------------- CPU tests
static void testCpu() {
    { // loads and flags
        Asm a; a.lda(0x80).lda(0x00).lda(0x7F);
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        Cpu& c = runSteps(nes, 1); CHECK_EQ(c.a, 0x80); CHECK(c.p & Cpu::N); CHECK(!(c.p & Cpu::Z));
        runSteps(nes, 1); CHECK_EQ(c.a, 0x00); CHECK(c.p & Cpu::Z); CHECK(!(c.p & Cpu::N));
        runSteps(nes, 1); CHECK(!(c.p & Cpu::Z)); CHECK(!(c.p & Cpu::N));
    }
    { // ADC signed overflow, carry out
        Asm a; a.i(0x18).lda(0x50).i1(0x69, 0x50);              // CLC; LDA #$50; ADC #$50
        a.i(0x18).lda(0xFF).i1(0x69, 0x01);                       // CLC; LDA #$FF; ADC #1
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        Cpu& c = runSteps(nes, 3);
        CHECK_EQ(c.a, 0xA0); CHECK(c.p & Cpu::V); CHECK(c.p & Cpu::N); CHECK(!(c.p & Cpu::C));
        runSteps(nes, 3);
        CHECK_EQ(c.a, 0x00); CHECK(c.p & Cpu::C); CHECK(c.p & Cpu::Z); CHECK(!(c.p & Cpu::V));
    }
    { // SBC borrow + overflow
        Asm a; a.i(0x38).lda(0x50).i1(0xE9, 0xB0);               // SEC; LDA #$50; SBC #$B0
        a.i(0x38).lda(0x05).i1(0xE9, 0x03);                       // SEC; LDA #5; SBC #3
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        Cpu& c = runSteps(nes, 3);
        CHECK_EQ(c.a, 0xA0); CHECK(c.p & Cpu::V); CHECK(!(c.p & Cpu::C)); CHECK(c.p & Cpu::N);
        runSteps(nes, 3);
        CHECK_EQ(c.a, 0x02); CHECK(c.p & Cpu::C); CHECK(!(c.p & Cpu::V));
    }
    { // CMP / CPX / CPY
        Asm a; a.lda(0x10).i1(0xC9, 0x10).i1(0xC9, 0x20).i1(0xC9, 0x08);
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        Cpu& c = runSteps(nes, 2); CHECK(c.p & Cpu::Z); CHECK(c.p & Cpu::C);
        runSteps(nes, 1);          CHECK(!(c.p & Cpu::Z)); CHECK(!(c.p & Cpu::C)); CHECK(c.p & Cpu::N);
        runSteps(nes, 1);          CHECK(c.p & Cpu::C); CHECK(!(c.p & Cpu::Z));
    }
    { // shifts and rotates
        Asm a; a.i(0x38).lda(0x81).i(0x2A)                         // SEC; LDA #$81; ROL A  -> $03, C=1
               .i(0x6A)                                             // ROR A -> $81, C=1
               .i(0x4A)                                             // LSR A -> $40, C=1
               .i(0x0A);                                            // ASL A -> $80, C=0
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        Cpu& c = runSteps(nes, 3); CHECK_EQ(c.a, 0x03); CHECK(c.p & Cpu::C);
        runSteps(nes, 1); CHECK_EQ(c.a, 0x81); CHECK(c.p & Cpu::C);
        runSteps(nes, 1); CHECK_EQ(c.a, 0x40); CHECK(c.p & Cpu::C);
        runSteps(nes, 1); CHECK_EQ(c.a, 0x80); CHECK(!(c.p & Cpu::C)); CHECK(c.p & Cpu::N);
    }
    { // stack, JSR/RTS
        Asm a;
        a.i2(0x20, 0xC010);                                         // JSR $C010
        a.ldy(0x11);                                                // after return
        a.pad(0xC010).ldx(0x42).i(0x60);                            // sub: LDX #$42; RTS
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        Cpu& c = runSteps(nes, 4);                                  // JSR, LDX, RTS, LDY
        CHECK_EQ(c.x, 0x42); CHECK_EQ(c.y, 0x11); CHECK_EQ(c.sp, 0xFD);
    }
    { // PHA/PLA, PHP/PLP (B flag must not stick)
        Asm a; a.lda(0x5A).i(0x48).lda(0x00).i(0x68).i(0x08).i(0x28);
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        Cpu& c = runSteps(nes, 4); CHECK_EQ(c.a, 0x5A);
        runSteps(nes, 2); CHECK(!(c.p & Cpu::B)); CHECK(c.p & Cpu::U);
    }
    { // zero-page indexed wrap-around and indirect addressing
        Asm a;
        a.lda(0x77).stz_zp(0x08);                                   // [$08] = $77
        a.ldx(0x10).i1(0xB5, 0xF8);                                 // LDA $F8,X -> reads $08 (wraps in page 0)
        a.lda(0x00).i1(0x85, 0x20).lda(0x03).i1(0x85, 0x21);        // pointer at $20 = $0300
        a.lda(0x99).sta(0x0305);                                    // [$0305] = $99
        a.ldy(0x05).i1(0xB1, 0x20);                                 // LDA ($20),Y -> $99
        a.ldx(0x00).lda(0xAB).sta(0x0300);                          // [$0300] = $AB
        a.i1(0xA1, 0x20);                                           // LDA ($20,X) with X=0 -> reads $0300 -> $AB
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        Cpu& c = runSteps(nes, 4); CHECK_EQ(c.a, 0x77);
        runSteps(nes, 7); CHECK_EQ(c.a, 0x99);
        runSteps(nes, 4); CHECK_EQ(c.a, 0xAB);
    }
    { // JMP ($xxFF) page-wrap bug
        Asm a;
        a.lda(0x40).sta(0x02FF).lda(0xC0).sta(0x0200);
        a.i2(0x6C, 0x02FF);                                         // JMP ($02FF) -> hi from $0200
        a.pad(0xC040).ldx(0x99);
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        Cpu& c = runSteps(nes, 6); CHECK_EQ(c.x, 0x99);
    }
    { // cycle counts: base, page-cross read, branches
        Asm a;
        a.ldx(0xFF);                                                // 2
        a.i2(0xBD, 0x02F0);                                         // LDA $02F0,X -> $03EF  (page cross) 5
        a.i2(0xBD, 0x0200);                                         // LDA $0200,X no cross 4
        a.i2(0x9D, 0x0200);                                         // STA $0200,X always 5
        a.lda(0x00);                                                // sets Z
        a.i1(0xF0, 0x00);                                           // BEQ +0 taken, no cross: 3
        a.i1(0xD0, 0x00);                                           // BNE not taken: 2
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        CHECK_EQ(nes.cpu.step(), 2);
        CHECK_EQ(nes.cpu.step(), 5);
        CHECK_EQ(nes.cpu.step(), 4);
        CHECK_EQ(nes.cpu.step(), 5);
        nes.cpu.step();
        CHECK_EQ(nes.cpu.step(), 3);
        CHECK_EQ(nes.cpu.step(), 2);
    }
    { // INC/DEC memory, INX wrap, BIT
        Asm a;
        a.lda(0xFF).stz_zp(0x30).i1(0xE6, 0x30);                    // INC $30 -> 0
        a.ldx(0xFF).i(0xE8);                                        // INX -> 0
        a.lda(0xC0).stz_zp(0x31).lda(0x01).i1(0x24, 0x31);          // BIT $31: A&M=0 -> Z; N,V from M
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(a.b));
        Cpu& c = runSteps(nes, 3); CHECK_EQ(nes.ram[0x30], 0); CHECK(c.p & Cpu::Z);
        runSteps(nes, 2); CHECK_EQ(c.x, 0); CHECK(c.p & Cpu::Z);
        runSteps(nes, 4); CHECK(c.p & Cpu::Z); CHECK(c.p & Cpu::N); CHECK(c.p & Cpu::V);
    }
}

// ---------------------------------------------------------------- MMC1 test
static void testMmc1() {
    // 4 banks (64KB), each filled so that byte 0 of the bank == bank number
    std::vector<uint8_t> code = {0xEA};
    std::vector<uint8_t> img = makeRom(code, 0xC000, 0xC000, 4);
    for (int b = 0; b < 4; b++) img[16 + b * 16384 + 0x100] = 0xA0 + b;       // marker at $8100 / $C100
    auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, img);
    auto serial = [&](uint16_t addr, uint8_t v) { for (int k = 0; k < 5; k++) nes.cpuWrite(addr, (v >> k) & 1); };

    CHECK_EQ(nes.cpuRead(0xC100), 0xA3);          // last bank fixed at power-on
    CHECK_EQ(nes.cpuRead(0x8100), 0xA0);
    serial(0xE000, 2);                            // PRG bank 2 at $8000
    CHECK_EQ(nes.cpuRead(0x8100), 0xA2);
    CHECK_EQ(nes.cpuRead(0xC100), 0xA3);
    serial(0xE000, 1);
    CHECK_EQ(nes.cpuRead(0x8100), 0xA1);
    // control: mode 3 (fix last) + vertical mirroring (value 0b01110)
    serial(0x8000, 0x0E);
    CHECK_EQ(nes.cart.mirroring(), 2);
    serial(0x8000, 0x0F);
    CHECK_EQ(nes.cart.mirroring(), 3);
    // a write with bit 7 resets the shift register mid-sequence
    nes.cpuWrite(0xE000, 1); nes.cpuWrite(0xE000, 1);
    nes.cpuWrite(0xE000, 0x80);
    serial(0xE000, 3);
    CHECK_EQ(nes.cpuRead(0x8100), 0xA3);
    // PRG RAM
    nes.cpuWrite(0x6000, 0x5A); nes.cpuWrite(0x7FFF, 0xA5);
    CHECK_EQ(nes.cpuRead(0x6000), 0x5A); CHECK_EQ(nes.cpuRead(0x7FFF), 0xA5);
    {   // 32 KB switching on a PRG of three 16 KB banks (not a multiple of 32 KB): the read wraps inside the image
        std::vector<uint8_t> odd = makeRom(code, 0xC000, 0xC000, 3);
        odd[16 + 16128] = 0x77;                                               // image offset 16128 = (bank 2 * 16 KB + $7F00) mod 48 KB
        auto n3 = std::make_unique<Nes>(); boot(*n3, odd);
        auto ser = [&](uint16_t addr, uint8_t v) { for (int k = 0; k < 5; k++) n3->cpuWrite(addr, (v >> k) & 1); };
        ser(0x8000, 0x00); ser(0xE000, 2);                                     // control: 32 KB mode; PRG bank 2
        CHECK_EQ(n3->cpuRead(0xFF00), 0x77);
    }
}

// ---------------------------------------------------------------- PPU render test
static void ppuAddr(Asm& a, uint16_t addr) { a.lda(addr >> 8).sta(0x2006).lda(addr & 0xFF).sta(0x2006); }

static std::vector<uint8_t> makeRenderRom(uint8_t scrollX) {
    Asm a;
    a.i(0x78).i(0xD8).ldx(0xFF).i(0x9A);                            // SEI CLD LDX #$FF TXS
    a.lda(0).sta(0x2000).sta(0x2001);
    ppuAddr(a, 0x3F00);
    for (uint8_t col : {0x0F, 0x16, 0x2A, 0x12}) a.lda(col).sta(0x2007);   // bg palette 0
    ppuAddr(a, 0x3F10);
    for (uint8_t col : {0x0F, 0x30, 0x28, 0x14}) a.lda(col).sta(0x2007);   // sprite palette 0
    ppuAddr(a, 0x0010);                                              // CHR tile 1: solid color 1
    for (int k = 0; k < 8; k++) a.lda(0xFF).sta(0x2007);
    for (int k = 0; k < 8; k++) a.lda(0x00).sta(0x2007);
    for (int k = 0; k < 8; k++) a.lda(0x00).sta(0x2007);            // tile 2: solid color 2
    for (int k = 0; k < 8; k++) a.lda(0xFF).sta(0x2007);
    ppuAddr(a, 0x2060);                                              // nametable row 3, col 0
    a.lda(1).sta(0x2007).lda(2).sta(0x2007);
    // sprite 0 via OAM DMA from page 2: y=30 (drawn on line 31), tile 1, attr 0, x=0
    a.lda(0x1E).sta(0x0200).lda(1).sta(0x0201).lda(0).sta(0x0202).sta(0x0203);
    a.lda(0x02).sta(0x4014);
    a.lda(scrollX).sta(0x2005).lda(0).sta(0x2005);
    a.lda(0x1E).sta(0x2001);                                         // bg+sprites on, left 8px shown
    a.lda(0x80).sta(0x2000);                                         // NMI on
    uint16_t wait = a.pc();
    a.i2(0x2C, 0x2002);                                              // BIT $2002
    a.i1(0x50, (uint8_t)(wait - (a.pc() + 2)));                      // BVC wait
    a.lda(0x42).stz_zp(0x11);
    uint16_t spin = a.pc();
    a.i2(0x4C, spin);
    a.pad(0xC400);
    uint16_t nmi = a.pc();
    a.i1(0xE6, 0x10).i(0x40);                                        // INC $10 ; RTI
    return makeRom(a.b, nmi);
}


static void testPpu() {
    const uint32_t RED = 0xFFB53120, GREEN = 0xFF5CE430, BLACK = 0xFF000000, WHITE = 0xFFFFFEFF;
    {
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRenderRom(0));
        for (int f = 0; f < 4; f++) nes.runFrame();
        CHECK_EQ(nes.ram[0x11], 0x42);                              // sprite-0 hit was seen by the CPU
        CHECK(nes.ram[0x10] >= 3);                                  // NMI fired about once per frame and RTI worked
        auto px = [&](int x, int y) { return nes.ppu.framebuffer[y * 256 + x]; };
        CHECK_EQ(px(0, 24), RED);       CHECK_EQ(px(7, 30), RED);
        CHECK_EQ(px(8, 24), GREEN);     CHECK_EQ(px(15, 24), GREEN);
        CHECK_EQ(px(16, 24), BLACK);    CHECK_EQ(px(100, 100), BLACK);
        CHECK_EQ(px(0, 31), WHITE);     CHECK_EQ(px(7, 38), WHITE);     // sprite drawn in front of bg
        CHECK_EQ(px(8, 38), BLACK);     CHECK_EQ(px(0, 39), BLACK);
        CHECK_EQ(px(0, 30), RED);                                       // line above the sprite is bg only
    }
    {
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRenderRom(3));                       // fine scroll X = 3
        for (int f = 0; f < 4; f++) nes.runFrame();
        auto px = [&](int x, int y) { return nes.ppu.framebuffer[y * 256 + x]; };
        CHECK_EQ(px(0, 24), RED);   CHECK_EQ(px(4, 24), RED);
        CHECK_EQ(px(5, 24), GREEN); CHECK_EQ(px(12, 24), GREEN);
        CHECK_EQ(px(13, 24), BLACK);
    }
    { // timing: a frame should be ~29780 CPU cycles
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRenderRom(0));
        for (int f = 0; f < 3; f++) nes.runFrame();
        uint64_t t0 = nes.cpu.totalCycles;
        for (int f = 0; f < 10; f++) nes.runFrame();
        double perFrame = (double)(nes.cpu.totalCycles - t0) / 10.0;
        std::printf("cycles/frame = %.1f (expect ~29780.5)\n", perFrame);
        CHECK(perFrame > 29700 && perFrame < 29860);
    }
}


// ---------------------------------------------------------------- smooth vertical scroll (dungeons)
static void testPpuSmoothScroll() {
    const uint32_t RED = 0xFFB53120, GREEN = 0xFF5CE430;
    auto setScroll = [](Nes& nes, int y) {                          // scroll Y of the whole frame (a room at rest scrolled by y); y < 240
        nes.ppu.readReg(0x2002); nes.ppu.writeReg(0x2005, 0); nes.ppu.writeReg(0x2005, (uint8_t)y);
    };
    auto prepare = [&](Nes& nes, bool smooth) {
        for (int f = 0; f < 4; f++) nes.runFrame();
        for (int r = 0; r < 30; r++) { nes.ppu.vramWrite((uint16_t)(0x2000 + r * 32), (uint8_t)(r % 2 ? 2 : 1)); nes.ppu.vramWrite((uint16_t)(0x2800 + r * 32), (uint8_t)(r % 2 ? 2 : 1)); }
        nes.ppu.smoothV = smooth;
    };
    auto shown = [](Nes& nes) { return ((int)nes.ppu.lineScrollY[100] - 100 + 480) % 480; };   // where the picture's top is in nametable space
    for (int smooth = 0; smooth < 2; smooth++) {
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRenderRom(0));
        prepare(nes, smooth != 0);
        const int base = 152;                                       // (line 64 of the picture reaches nametable row 28..29 and the other nametable: the wrap is crossed)
        int last = -1; bool ok = true, colours = true;
        for (int f = 0; f < 48; f++) {
            const int raw = base + std::min(f / 4, 10) * 8;          // the game: 8 px every 4 frames, 10 steps, then it stops
            setScroll(nes, raw % 240);
            nes.runFrame();
            const int d = shown(nes);
            if (smooth && f >= 4) { if (f < 40 && d != (base + 2 * (f - 3)) % 480) ok = false; if (last >= 0 && f < 43 && (d - last + 480) % 480 != 2 && d != (base + 80) % 480) ok = false; }
            if (!smooth && d != raw % 480 && d != (raw + 0) % 480) ok = false;
            const int ey = (d + 100) % 480 % 240;
            if (nes.ppu.framebuffer[100 * 256 + 0] != ((ey / 8) % 2 ? GREEN : RED)) colours = false;     // the picture really shows the rows lineScrollY says
            last = d;
        }
        CHECK(ok); CHECK(colours);
        CHECK_EQ(shown(nes), (base + 80) % 480);                     // ends exactly where the game stands
        if (smooth) {                                                // a jump that is not one scroll step shows at once
            setScroll(nes, 60); nes.runFrame(); CHECK_EQ(shown(nes), 60 - 64 + 64 > 0 ? 60 : 60);
            setScroll(nes, 60); nes.runFrame(); setScroll(nes, 68); nes.runFrame(); CHECK_EQ(shown(nes), 62);
        }
    }
}

// ---------------------------------------------------------------- PPU layers used by the widescreen compositor
static void testPpuLayers() {
    const uint32_t RED = 0xFFB53120, WHITE = 0xFFFFFEFF;
    auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRenderRom(0));
    for (int f = 0; f < 4; f++) nes.runFrame();
    const Ppu& p = nes.ppu;
    // (0,31): the white sprite is in front of the red background tile
    CHECK_EQ(p.framebuffer[31 * 256], WHITE);
    CHECK_EQ(p.fbColor[31 * 256], WHITE);
    CHECK_EQ(p.bgColor[31 * 256], RED);                           // background only
    CHECK_EQ(p.fbIdx[31 * 256], 0x30);
    CHECK_EQ(p.sprCol[31 * Ppu::SPR_W], 0x30);                    // the sprite layer: colour index and OAM index
    CHECK_EQ(p.sprOam[31 * Ppu::SPR_W], 0);
    CHECK_EQ(p.sprCol[24 * Ppu::SPR_W], 0xFF);                    // no sprite on that line
    // Grayscale bit (the bomb flash): the real picture turns gray, the colour layer doesn't, the index tells how to gray it.
    nes.ppu.writeReg(0x2001, 0x1F);
    for (int f = 0; f < 2; f++) nes.runFrame();
    CHECK_EQ(nes.ppu.framebuffer[24 * 256 + 0], Ppu::nesColor(0x16 & 0x30));
    CHECK_EQ(nes.ppu.fbColor[24 * 256 + 0], RED);
    CHECK_EQ(nes.ppu.fbIdx[24 * 256 + 0], 0x16);
    CHECK_EQ(nes.ppu.bgColor[24 * 256 + 0], RED);
    CHECK_EQ(nes.ppu.lineMask[100] & 1, 1);
    nes.ppu.writeReg(0x2001, 0x1E);
    // A sprite hanging over the right edge keeps its overflow in the sprite layer; one over the top edge, too.
    nes.ppu.oam[0] = 100; nes.ppu.oam[1] = 1; nes.ppu.oam[2] = 0; nes.ppu.oam[3] = 252;       // x=252..259, y=101..108
    nes.ppu.oam[4] = 4;   nes.ppu.oam[5] = 1; nes.ppu.oam[6] = 0; nes.ppu.oam[7] = 40;        // y=5..12 (above the play area)
    nes.ppu.oam[8] = 0xF8; nes.ppu.oam[9] = 1; nes.ppu.oam[10] = 0; nes.ppu.oam[11] = 60;     // parked: never drawn
    for (int f = 0; f < 2; f++) nes.runFrame();
    CHECK_EQ(nes.ppu.sprCol[105 * Ppu::SPR_W + 255], 0x30);       // inside the picture
    CHECK_EQ(nes.ppu.sprCol[105 * Ppu::SPR_W + 256], 0x30);       // hanging over the right edge: kept
    CHECK_EQ(nes.ppu.sprCol[105 * Ppu::SPR_W + 259], 0x30);
    CHECK_EQ(nes.ppu.sprCol[105 * Ppu::SPR_W + 260], 0xFF);
    CHECK(nes.ppu.framebuffer[105 * 256 + 255] != 0);             // (the real picture is cut at x=255, of course)
    CHECK_EQ(nes.ppu.sprOam[8 * Ppu::SPR_W + 40], 1);
    CHECK_EQ(nes.ppu.sprCol[(0xF9) * Ppu::SPR_W + 60], 0xFF);
    // More than 8 sprites on a line: the PPU draws the first 8, the sprite layer too, and the rest goes to the dropped layer (not the ones behind the background).
    for (int i = 0; i < 64; i++) nes.ppu.oam[i * 4] = 0xF8;
    for (int i = 0; i < 11; i++) { nes.ppu.oam[i * 4] = 145; nes.ppu.oam[i * 4 + 1] = 1; nes.ppu.oam[i * 4 + 2] = i == 9 ? 0x20 : 0; nes.ppu.oam[i * 4 + 3] = (uint8_t)(20 * i); }
    for (int f = 0; f < 2; f++) nes.runFrame();
    CHECK_EQ(nes.ppu.sprOam[149 * Ppu::SPR_W + 140], 7);          // the 8th sprite is drawn
    CHECK_EQ(nes.ppu.sprCol[149 * Ppu::SPR_W + 160], 0xFF);       // the 9th is not,
    CHECK_EQ(nes.ppu.dropCol[149 * Ppu::SPR_W + 160], 0x30);      // but it is in the dropped layer
    CHECK_EQ(nes.ppu.dropOam[149 * Ppu::SPR_W + 160], 8);
    CHECK_EQ(nes.ppu.dropCol[149 * Ppu::SPR_W + 180], 0xFF);      // the 10th is behind the background: not kept
    CHECK_EQ(nes.ppu.dropOam[149 * Ppu::SPR_W + 200], 10);
    CHECK_EQ(nes.ppu.dropCol[149 * Ppu::SPR_W + 140], 0xFF);      // (the drawn ones are not in it)
    // With unlimitedSprites the PPU draws every sprite of a line (no flicker): the 9th and later are in the sprite layer and in the picture, nothing is left out.
    nes.ppu.unlimitedSprites = true;
    for (int f = 0; f < 2; f++) nes.runFrame();
    CHECK_EQ(nes.ppu.sprCol[149 * Ppu::SPR_W + 160], 0x30);
    CHECK_EQ(nes.ppu.sprOam[149 * Ppu::SPR_W + 160], 8);
    CHECK_EQ(nes.ppu.sprOam[149 * Ppu::SPR_W + 200], 10);         // (the 11th sprite, too)
    CHECK_EQ(nes.ppu.dropCol[149 * Ppu::SPR_W + 160], 0xFF);
    CHECK(nes.ppu.fbColor[149 * 256 + 160] != nes.ppu.bgColor[149 * 256 + 160]);   // (and the real picture shows it)
    nes.ppu.unlimitedSprites = false;
    for (int f = 0; f < 2; f++) nes.runFrame();
    CHECK_EQ(nes.ppu.sprCol[149 * Ppu::SPR_W + 160], 0xFF);       // (off again: the PPU's own limit)
    // And below the picture: rows 240..255 exist too.
    nes.ppu.oam[0] = 236; nes.ppu.oam[3] = 10;                    // y=237..244: five rows below row 239
    for (int f = 0; f < 2; f++) nes.runFrame();
    CHECK_EQ(nes.ppu.sprCol[243 * Ppu::SPR_W + 12], 0x30);
}

static void testBadRoms() {
    std::string err; Cart c;
    std::vector<uint8_t> img(16 + 16384, 0); img[0] = 'N'; img[1] = 'E'; img[2] = 'S'; img[3] = 0x1A; img[4] = 1;
    CHECK(c.loadFromMemory(img, err));                                       // a plain 16 KB NROM image loads
    img[4] = 0; CHECK(!c.loadFromMemory(img, err));                          // no PRG banks at all: refused (it used to divide by zero)
    img[4] = 2; CHECK(!c.loadFromMemory(img, err));                          // header says 32 KB, file has 16: truncated
    img[0] = 'X'; CHECK(!c.loadFromMemory(img, err));                        // not an iNES file
    CHECK(!c.loadFromMemory(std::vector<uint8_t>(5, 0), err));               // shorter than a header
}

// ---------------------------------------------------------------- no lag: a handler that takes longer than a frame
static void testLagFree() {
    // The NMI handler switches NMI off, counts itself, works for ~38,000 cycles (more than a frame: 29,780) and switches NMI on again, the way the game's does.
    std::vector<uint8_t> code(0x40, 0xEA);
    const uint8_t reset[] = {0x78, 0xA9, 0x80, 0x8D, 0x00, 0x20, 0x4C, 0x06, 0xC0};                     // SEI; LDA #$80; STA $2000; JMP *
    const uint8_t nmi[] = {0xA9, 0x00, 0x8D, 0x00, 0x20, 0xE6, 0x00, 0xA2, 0x1E, 0xA0, 0x00, 0x88, 0xD0, 0xFD, 0xCA, 0xD0, 0xF8,
                           0xA9, 0x80, 0x8D, 0x00, 0x20, 0x40};                                          // ... INC $00; LDX #30; (LDY #0; DEY; BNE) x256; DEX; BNE; LDA #$80; STA $2000; RTI
    std::memcpy(&code[0], reset, sizeof reset); std::memcpy(&code[0x20], nmi, sizeof nmi);
    auto count = [&](bool lagFree, int frames, int& lagged) {
        auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(code, 0xC020));
        nes.lagFree = lagFree; lagged = 0;
        for (int f = 0; f < frames; f++) { nes.runFrame(); if (nes.lagCycles > 0) lagged++; }
        return (int)nes.ram[0];
    };
    int lagA = 0, lagB = 0;
    const int slow = count(false, 20, lagA), fast = count(true, 20, lagB);
    CHECK(slow <= 12);                                       // the handler outlasts the frame: every other NMI is lost
    CHECK(fast >= 19);                                       // with lagFree one runs for every frame
    CHECK_EQ(lagA, 0);                                       // (and nothing was given extra time without it)
    CHECK(lagB >= 10);
    // A handler that is quick gets nothing extra.
    std::vector<uint8_t> quick = code; quick[0x28] = 1;       // LDX #1: ~1,300 cycles
    auto nesPtr = std::make_unique<Nes>(); Nes& nes = *nesPtr; boot(nes, makeRom(quick, 0xC020)); nes.lagFree = true;
    int extra = 0; for (int f = 0; f < 10; f++) { nes.runFrame(); extra += nes.lagCycles; }
    CHECK_EQ(extra, 0);
    CHECK(nes.ram[0] >= 9);
}

// ---------------------------------------------------------------- shadow games: Nes::cloneFrom and the lite PPU
static void testClone() {
    auto src = std::make_unique<Nes>(); boot(*src, makeRenderRom(0));
    for (int f = 0; f < 4; f++) src->runFrame();
    const uint8_t srcCounter = src->ram[0x10];
    {   // a clone is independent: running it, writing its RAM, cartridge RAM and CHR-RAM leaves the source alone
        auto c = std::make_unique<Nes>(); c->cloneFrom(*src);
        for (int f = 0; f < 10; f++) c->runFrame();
        CHECK(c->ram[0x10] >= srcCounter + 9); CHECK_EQ(src->ram[0x10], srcCounter);
        c->cpuWrite(0x6000, 0x5A); CHECK_EQ(c->cpuRead(0x6000), 0x5A); CHECK(src->cpuRead(0x6000) != 0x5A);
        c->ppu.vramWrite(0x0010, 0x77); CHECK_EQ(c->ppu.vramRead(0x0010), 0x77); CHECK(src->ppu.vramRead(0x0010) != 0x77);   // (its PPU reads its own cartridge)
        c->ppu.vramWrite(0x2000, 0x66); CHECK(src->ppu.vramRead(0x2000) != 0x66);
        CHECK_EQ(c->ppu.framebuffer[24 * 256], 0u);                                      // lite: no picture was drawn
    }
    {   // lite and full clones of the same state run the game the same way, and two clones are deterministic
        auto a = std::make_unique<Nes>(), b = std::make_unique<Nes>(), c = std::make_unique<Nes>();
        a->cloneFrom(*src, true); b->cloneFrom(*src, false); c->cloneFrom(*src, true);
        for (int f = 0; f < 12; f++) { a->runFrame(); b->runFrame(); c->runFrame(); }
        CHECK(std::memcmp(a->ram, b->ram, sizeof a->ram) == 0); CHECK(std::memcmp(a->ram, c->ram, sizeof a->ram) == 0);
        CHECK_EQ(a->cpu.pc, b->cpu.pc); CHECK_EQ(a->cpu.totalCycles, b->cpu.totalCycles); CHECK_EQ(a->cpu.totalCycles, c->cpu.totalCycles);
        CHECK(b->ppu.framebuffer[24 * 256] != 0u);                                        // (the full one drew)
    }
    {   // frames skipped without drawing (the scroll between two screens, main.cpp) leave the game and the next drawn picture exactly as drawing them would
        auto a = std::make_unique<Nes>(), b = std::make_unique<Nes>();
        a->cloneFrom(*src, false); b->cloneFrom(*src, false);
        for (int f = 0; f < 40; f++) a->runFrame();
        b->ppu.lite = true; for (int f = 0; f < 39; f++) b->runFrame(); b->ppu.lite = false; b->runFrame();
        CHECK(std::memcmp(a->ram, b->ram, sizeof a->ram) == 0); CHECK_EQ(a->cpu.pc, b->cpu.pc); CHECK_EQ(a->cpu.totalCycles, b->cpu.totalCycles);
        CHECK(std::memcmp(a->ppu.framebuffer, b->ppu.framebuffer, 256 * 240 * sizeof(uint32_t)) == 0);
        CHECK(b->ppu.framebuffer[24 * 256] != 0u);                                           // (and the last one was drawn)
    }
    {   // a lite shadow booted from scratch gets through the game's wait for the sprite-0 hit
        auto c = std::make_unique<Nes>(); boot(*c, makeRenderRom(0));
        auto l = std::make_unique<Nes>(); l->cloneFrom(*c, true);
        for (int f = 0; f < 4; f++) l->runFrame();
        CHECK_EQ(l->ram[0x11], 0x42);
    }
    {   // the sprite-0 hit comes on the same dot in lite and full mode: random scroll, OAM, mask and control bits, stepped dot by dot
        uint32_t seed = 12345; auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (int)(seed >> 8); };
        int hitsSeen = 0, mismatches = 0;
        for (int trial = 0; trial < 40; trial++) {
            auto full = std::make_unique<Nes>(), lite = std::make_unique<Nes>();
            full->cloneFrom(*src, false); lite->cloneFrom(*src, true);
            const int y = rnd() % 200, tile = rnd() % 3, attr = rnd() & 0xE3, x = trial % 4 == 0 ? rnd() % 6 : rnd() % 256, sx = rnd() % 256, sy = rnd() % 8;
            const uint8_t ctrl = (uint8_t)(0x80 | (rnd() & 0x38)), mask = (uint8_t)(0x18 | (rnd() & 6));
            for (Nes* n : {full.get(), lite.get()}) {
                n->ppu.oam[0] = (uint8_t)y; n->ppu.oam[1] = (uint8_t)tile; n->ppu.oam[2] = (uint8_t)attr; n->ppu.oam[3] = (uint8_t)x;
                n->ppu.writeReg(0x2000, ctrl); n->ppu.writeReg(0x2001, mask); n->ppu.writeReg(0x2005, (uint8_t)sx); n->ppu.writeReg(0x2005, (uint8_t)sy);
                for (int i = 0; i < 960; i++) n->ppu.vram[i] = (uint8_t)((i * 7 + trial) % 5 < 3 ? 1 + (i & 1) : 0);   // (a patchy background, the same in both)
                n->ppu.status = 0; n->ppu.scanline = 0; n->ppu.cycle = 0;
            }
            const bool chunks = trial & 1;                                               // odd trials step in chunks of 1..300 dots (the lite PPU skips the quiet dots), even ones dot by dot
            for (int dot = 0; dot < 341 * 262 * 2;) {
                const int k = chunks ? 1 + rnd() % 300 : 1;
                full->ppu.step(k); lite->ppu.step(k); dot += k;
                const bool fh = full->ppu.status & 0x40, lh = lite->ppu.status & 0x40;
                if (fh) hitsSeen++;
                if (fh != lh || (full->ppu.status & 0xE0 & ~0x20) != (lite->ppu.status & 0xE0 & ~0x20) || full->ppu.scanline != lite->ppu.scanline || full->ppu.cycle != lite->ppu.cycle
                    || full->ppu.nmiOut != lite->ppu.nmiOut) { mismatches++; break; }
            }
        }
        CHECK_EQ(mismatches, 0); CHECK(hitsSeen > 500);                                 // (and the trials did hit: not a vacuous comparison)
    }
    {   // several shadows stepped on the thread pool end in the same state as stepped one after another (independent Nes objects)
        constexpr int N = 6;
        std::unique_ptr<Nes> seq[N], par4[N];
        for (int i = 0; i < N; i++) { seq[i] = std::make_unique<Nes>(); par4[i] = std::make_unique<Nes>(); seq[i]->cloneFrom(*src); par4[i]->cloneFrom(*src); seq[i]->ram[0x40] = par4[i]->ram[0x40] = (uint8_t)i; }
        const int saved = par::maxThreads(); par::maxThreads() = 4;
        for (int f = 0; f < 20; f++) {
            for (int i = 0; i < N; i++) seq[i]->runFrame();
            par::forRange(0, N, 1, [&](int a, int b) { for (int i = a; i < b; i++) par4[i]->runFrame(); });
        }
        par::maxThreads() = saved;
        bool same = true;
        for (int i = 0; i < N; i++) same = same && std::memcmp(seq[i]->ram, par4[i]->ram, sizeof seq[i]->ram) == 0 && seq[i]->cpu.totalCycles == par4[i]->cpu.totalCycles && seq[i]->cpu.pc == par4[i]->cpu.pc;
        CHECK(same); CHECK_EQ(par4[3]->ram[0x40], 3);
    }
    {   // cost: a lite frame is much cheaper than a full one
        auto a = std::make_unique<Nes>(), b = std::make_unique<Nes>(); a->cloneFrom(*src, true); b->cloneFrom(*src, false);
        auto time = [&](Nes& n) { auto t0 = std::chrono::steady_clock::now(); for (int f = 0; f < 60; f++) n.runFrame(); return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 60; };
        const double tl = time(*a), tf = time(*b);
        std::printf("frame: lite %.3f ms, full %.3f ms\n", tl, tf);
        CHECK(tl * 2 < tf);
    }
}

// ---------------------------------------------------------------- shadow games (src/shadow.*): the neighbour screens, on a mock of the game's mode walk
// The mock's NMI handler does what the real game does around a screen change: mode 6 picks the next room from ObjDir ($98), mode 7 scrolls (40 frames),
// mode 4 loads (30 frames) and spawns a monster in slot 1 ($350), mode 5 moves it ($71) and drains the hearts ($66F) by 1 a frame.
static std::vector<uint8_t> makeMockGame(bool dead) {
    std::vector<uint8_t> code(0x20, 0xEA);
    const uint8_t reset[] = {0x78, 0xA9, 0x80, 0x8D, 0x00, 0x20, 0x4C, 0x06, 0xC0};
    std::memcpy(code.data(), reset, sizeof reset);
    struct P { std::vector<uint8_t> b; std::map<std::string, int> lab; std::vector<std::pair<int, std::string>> rel, abs;
        void op(std::initializer_list<uint8_t> l) { b.insert(b.end(), l.begin(), l.end()); }
        void label(const char* n) { lab[n] = (int)b.size(); }
        void br(uint8_t o, const char* n) { b.push_back(o); rel.push_back({(int)b.size(), n}); b.push_back(0); }
        void jmp(const char* n) { b.push_back(0x4C); abs.push_back({(int)b.size(), n}); b.push_back(0); b.push_back(0); }
    } p;
    if (!dead) {
        p.op({0xA5, 0x12, 0xC9, 6}); p.br(0xD0, "n7");
        p.op({0xA5, 0x98, 0xC9, 1}); p.br(0xD0, "d2"); p.op({0xE6, 0xEB}); p.jmp("m6");
        p.label("d2"); p.op({0xC9, 2}); p.br(0xD0, "d4"); p.op({0xC6, 0xEB}); p.jmp("m6");
        p.label("d4"); p.op({0xC9, 4}); p.br(0xD0, "d8"); p.op({0xA5, 0xEB, 0x18, 0x69, 0x10, 0x85, 0xEB}); p.jmp("m6");
        p.label("d8"); p.op({0xA5, 0xEB, 0x38, 0xE9, 0x10, 0x85, 0xEB});
        p.label("m6"); p.op({0xA9, 7, 0x85, 0x12, 0xA9, 40, 0x85, 0x60}); p.jmp("end");
        p.label("n7"); p.op({0xC9, 7}); p.br(0xD0, "n4"); p.op({0xC6, 0x60}); p.br(0xD0, "end"); p.op({0xA9, 4, 0x85, 0x12, 0xA9, 30, 0x85, 0x60}); p.jmp("end");
        p.label("n4"); p.op({0xC9, 4}); p.br(0xD0, "n5"); p.op({0xC6, 0x60}); p.br(0xD0, "end"); p.op({0xA9, 5, 0x85, 0x12, 0xA9, 0x10, 0x8D, 0x50, 0x03}); p.jmp("end");
        p.label("n5"); p.op({0xC9, 5}); p.br(0xD0, "end"); p.op({0xCE, 0x6F, 0x06, 0xE6, 0x71});
    }
    p.label("end"); p.op({0xE6, 0x15, 0x40});
    for (auto& r : p.rel) p.b[r.first] = (uint8_t)(p.lab[r.second] - (r.first + 1));
    for (auto& a : p.abs) { const int at = 0xC020 + p.lab[a.second]; p.b[a.first] = at & 0xFF; p.b[a.first + 1] = (uint8_t)(at >> 8); }
    code.insert(code.end(), p.b.begin(), p.b.end());
    return makeRom(code, 0xC020);
}

static void testShadow() {
    // which screen a shadow lives on
    CHECK_EQ(ShadowGame::targetRoom(0x35, 1, 0), 0x36); CHECK_EQ(ShadowGame::targetRoom(0x35, -1, 0), 0x34); CHECK_EQ(ShadowGame::targetRoom(0x35, 0, 1), 0x45);
    CHECK_EQ(ShadowGame::targetRoom(0x35, 0, -1), 0x25); CHECK_EQ(ShadowGame::targetRoom(0x35, 1, 1), 0x46); CHECK_EQ(ShadowGame::targetRoom(0x35, -1, -1), 0x24);
    CHECK_EQ(ShadowGame::targetRoom(0x00, -1, 0), -1); CHECK_EQ(ShadowGame::targetRoom(0x0F, 1, 0), -1); CHECK_EQ(ShadowGame::targetRoom(0x03, 0, -1), -1); CHECK_EQ(ShadowGame::targetRoom(0x73, 0, 1), -1);
    CHECK_EQ(ShadowGame::targetRoom(0x35, 0, 0), -1);
    CHECK_EQ(ShadowGame::targetRoom(0x61, 1, 0), -1); CHECK_EQ(ShadowGame::targetRoom(0x1B, 0, -1), -1);     // leaving a maze room is a puzzle
    CHECK_EQ(ShadowGame::targetRoom(0x51, 0, 1), 0x61);                                                          // (arriving in one is fine)
    CHECK_EQ(ShadowGame::targetRoom(0x60, 1, 1), -1);                                                            // the way through one is not
    auto realGame = [&](int room) {
        auto r = std::make_unique<Nes>(); boot(*r, makeMockGame(false));
        for (int f = 0; f < 3; f++) r->runFrame();
        r->ram[0x12] = 5; r->ram[0x10] = 0; r->ram[0xE1] = 0; r->ram[0xE0] = 0; r->ram[0xEB] = (uint8_t)room; r->ram[0x66F] = 0x77; r->ram[0x670] = 0xFF;
        return r;
    };
    {   // a shadow is only made from plain overworld play
        auto r = realGame(0x35); ShadowGame g;
        r->ram[0x12] = 7; CHECK(!g.start(*r, 1, 0, 1)); r->ram[0x12] = 5; r->ram[0x10] = 1; CHECK(!g.start(*r, 1, 0, 1)); r->ram[0x10] = 0;
        r->ram[0xE1] = 1; CHECK(!g.start(*r, 1, 0, 1)); r->ram[0xE1] = 0; r->ram[0xE0] = 1; CHECK(!g.start(*r, 1, 0, 1)); r->ram[0xE0] = 0;
        CHECK(!g.start(*r, 0, 0, 1)); CHECK(g.start(*r, 1, 0, 1)); CHECK(g.state() == ShadowGame::State::SpinUp);
    }
    {   // all eight neighbours walk to their screens with the game's own mode changes and appear; the real game is untouched
        auto r = realGame(0x35); ShadowSet set; set.enabled = true; set.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        for (int i = 0; i < 330; i++) set.update(*r);
        CHECK_EQ(set.active.size(), (size_t)8);
        std::set<int> rooms; bool allReady = true, monsters = true, linkStandsIn = true;
        for (ShadowGame* g : set.active) {
            allReady = allReady && g->ready(); rooms.insert(g->room());
            monsters = monsters && g->nes().ram[0x350] == 0x10 && g->nes().ram[0x12] == 5 && g->nes().ram[0xEB] == g->room();
            linkStandsIn = linkStandsIn && g->nes().ram[0x70] >= 0x28 && g->nes().ram[0x70] <= 0xD0 && g->nes().ram[0x84] >= 0x6D && g->nes().ram[0x84] <= 0xBD;   // (he strolls about)
        }
        CHECK(allReady); CHECK(monsters); CHECK(linkStandsIn);
        CHECK(rooms == (std::set<int>{0x36, 0x34, 0x45, 0x25, 0x46, 0x44, 0x26, 0x24}));
        CHECK_EQ(r->ram[0x12], 5); CHECK_EQ(r->ram[0xEB], 0x35); CHECK_EQ(r->ram[0x350], 0);              // (nothing leaked into the real game)
        std::set<std::vector<uint8_t>> rngs; for (ShadowGame* g : set.active) rngs.insert(std::vector<uint8_t>(g->nes().ram + 0x18, g->nes().ram + 0x18 + 13));
        CHECK_EQ(rngs.size(), (size_t)8);                                                                  // every shadow has its own random numbers
        const int lived = set.active[0]->frames(); const uint8_t hearts = set.active[0]->nes().ram[0x66F];
        for (int i = 0; i < 10; i++) set.update(*r);
        CHECK_EQ(set.active[0]->frames(), lived + 10); CHECK_EQ(hearts, 0xFE); CHECK_EQ(set.active[0]->nes().ram[0x66F], 0xFE);   // (hearts are put back every frame, always the most there are: 0xFF minus this frame's drain)
        CHECK(set.active[0]->nes().ram[0x71] != 0);                                                        // the monster moves in lockstep
        CHECK_EQ(set.active[0]->nes().ram[0x5A], 1);                                                        // (the stand-in Link cannot take a warp: the game's guard is held)
        {   // a shadow that ends up somewhere it should not (a cave, a menu) is dropped from the picture and tried again after a pause, a few times only
            ShadowGame* g1 = set.active[0]; const ShadowGame::State good = ShadowGame::State::Ready;
            CHECK(g1->state() == good);
            for (int attempt = 1; attempt <= ShadowGame::kMaxAttempts; attempt++) {
                g1->nes().ram[0x12] = 0x0B; set.update(*r); CHECK(g1->state() == ShadowGame::State::Failed);
                for (int i = 0; i < ShadowGame::kRetryDelay + 2; i++) set.update(*r);
                if (attempt < ShadowGame::kMaxAttempts) { CHECK(g1->state() != ShadowGame::State::Failed); for (int i = 0; i < 200; i++) set.update(*r); CHECK(g1->state() == good); }
                else CHECK(g1->state() == ShadowGame::State::Failed);                                       // (the third go failed too: given up)
            }
        }
        {   // hand-off: the shadow's creatures replace the ones the real game has just made (the killed ones stay dead), Link is not touched
            ShadowGame* e = nullptr; for (ShadowGame* g : set.active) if (g->room() == 0x34) e = g;
            CHECK(e != nullptr && e->ready());
            Nes& sh = e->nes(); sh.ram[0x350] = sh.ram[0x351] = sh.ram[0x352] = 0x10; sh.ram[0x71] = 100; sh.ram[0x72] = 110; sh.ram[0x73] = 120; sh.ram[0x486] = 7; sh.ram[0x34E] = 3;
            sh.ram[0x413] = 0x55; sh.ram[0x420] = 0x66; sh.ram[0x42D] = 0x77; sh.ram[0x438] = 0x88;                   // (flyer speed, turn rate, turns and distance of slot 1)
            r->ram[0xEB] = 0x34; r->ram[0x350] = r->ram[0x351] = 0x10; r->ram[0x352] = 0; r->ram[0x71] = 11; r->ram[0x72] = 22; r->ram[0x70] = 0x40; r->ram[0x486] = 1; r->ram[0x34E] = 2;
            CHECK(set.handOff(*r));
            CHECK_EQ(r->ram[0x71], 100); CHECK_EQ(r->ram[0x72], 110); CHECK_EQ(r->ram[0x73], 120); CHECK_EQ(r->ram[0x352], 0x10); CHECK_EQ(r->ram[0x486], 7); CHECK_EQ(r->ram[0x34E], 3);
            CHECK_EQ(r->ram[0x413], 0x55); CHECK_EQ(r->ram[0x420], 0x66); CHECK_EQ(r->ram[0x42D], 0x77); CHECK_EQ(r->ram[0x438], 0x88);      // (a peahat keeps its flight, a tektite its jump)
            CHECK_EQ(r->ram[0x70], 0x40);                                                                   // (Link's slot 0 is untouched)
            r->ram[0x71] = 5; r->ram[0x353] = 0x11; r->ram[0x354] = 0x10; CHECK(set.handOff(*r)); CHECK_EQ(r->ram[0x71], 100); CHECK_EQ(r->ram[0x353], 0); CHECK_EQ(r->ram[0x354], 0);   // monsters the shadow does not have (killed there) are gone
            r->ram[0x71] = 5; r->ram[0xEB] = 0x40; CHECK(!set.handOff(*r)); CHECK_EQ(r->ram[0x71], 5);          // not a screen with a shadow
            r->ram[0xEB] = 0x35;
        }
        {   // the armed hand-off (after a scroll, a cave, stairs) waits until the game plays again: the game does not move its monsters while Link walks in, the shadow does
            ShadowGame* e = nullptr; for (ShadowGame* g : set.active) if (g->room() == 0x45) e = g;
            CHECK(e != nullptr && e->ready());
            Nes& sh = e->nes(); sh.ram[0x350] = sh.ram[0x351] = 0x10; sh.ram[0x71] = 90; sh.ram[0x72] = 95;
            r->ram[0xEB] = 0x45; for (int s = 1; s <= 12; s++) r->ram[0x34F + s] = 0;
            r->ram[0x12] = 4; r->ram[0x13] = 1; r->ram[0x71] = 1;
            set.armHandOff(*r); set.update(*r);                                                               // mode 4, submode 1
            CHECK(set.handOffPending()); CHECK_EQ(r->ram[0x71], 1);
            r->ram[0x13] = 0; r->ram[0x350] = r->ram[0x351] = 0x10; for (int i = 0; i < 5; i++) set.update(*r);   // (the game made its monsters, the walk goes on: nothing is copied yet)
            CHECK(set.handOffPending()); CHECK_EQ(r->ram[0x71], 1);
            r->ram[0x12] = 5; set.update(*r);                                                                 // in play: the shadow's monsters, where the shadow is now
            CHECK(!set.handOffPending()); CHECK_EQ(r->ram[0x71], sh.ram[0x71]); CHECK(sh.ram[0x71] >= 95);   // (it went on moving while the game walked Link in)
            for (int s = 1; s <= 12; s++) { r->ram[0x34F + s] = 0; }
            r->ram[0x71] = 1; r->ram[0x12] = 4; set.armHandOff(*r); set.update(*r); set.update(*r);          // (no monsters at all in the real game: they come from the shadow)
            CHECK(set.handOffPending()); CHECK_EQ(r->ram[0x71], 1);
            r->ram[0x12] = 5; set.update(*r); CHECK(!set.handOffPending()); CHECK_EQ(r->ram[0x71], sh.ram[0x71]); CHECK_EQ(r->ram[0x350], 0x10);
            r->ram[0xEB] = 0x35; r->ram[0x12] = 5; r->ram[0x71] = 1; set.armHandOff(*r); set.update(*r);     // a screen without a ready shadow: tried once in mode 5, then given up
            CHECK(!set.handOffPending()); CHECK_EQ(r->ram[0x71], 1);
        }
        {   // the screen Link leaves becomes a shadow, shadows that are still neighbours go on (nothing respawns), the others are dropped, new ones are made
            auto find = [&](ShadowSet& st, int room) -> ShadowGame* { for (ShadowGame* g : st.active) if (g->room() == room) return g; return nullptr; };
            auto q = realGame(0x35); ShadowSet ps; ps.enabled = true; ps.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
            for (int i = 0; i < 330; i++) ps.update(*q);
            CHECK_EQ(ps.active.size(), (size_t)8);
            ShadowGame *g25 = find(ps, 0x25), *g45 = find(ps, 0x45), *g24 = find(ps, 0x24), *g36 = find(ps, 0x36);
            CHECK(g25 && g45 && g24 && g36); const int f25 = g25->frames();
            {   // a shadow is drawn from the moment it has arrived on its screen (settling): the monsters' spawn smoke is seen, not only the finished monsters
                auto early = realGame(0x35); ShadowSet es; es.enabled = true; es.forceSpinSteps = ShadowSet::kSpinStepsPerFrame; bool settlingSeen = false, hiddenWhileSpinning = true;
                for (int i = 0; i < 45; i++) { es.update(*early); for (ShadowGame* g : es.active) { settlingSeen = settlingSeen || (g->state() == ShadowGame::State::Settling && g->visible()); hiddenWhileSpinning = hiddenWhileSpinning && (g->state() != ShadowGame::State::SpinUp || !g->visible()); } }
                CHECK(settlingSeen); CHECK(hiddenWhileSpinning);
            }
            q->ram[0x350] = 0x10; q->ram[0x71] = 33; q->ram[0x12] = 6;                                      // the game leaves screen 35 (mode 6) with a monster in it
            ps.update(*q); ps.update(*q);
            ShadowGame* dep = find(ps, 0x35);
            CHECK(dep != nullptr && dep->ready());
            if (dep) { CHECK_EQ(dep->nes().ram[0x12], 5); CHECK_EQ(dep->nes().ram[0x350], 0x10); CHECK(dep->nes().ram[0x71] >= 33 && dep->nes().ram[0x71] <= 40); }   // (no blink: it ran a frame at once, the monster moved on)
            int n35 = 0; for (ShadowGame* g : ps.active) n35 += g->room() == 0x35; CHECK_EQ(n35, 1);       // (one capture, not one per frame)
            q->ram[0xEB] = 0x36; q->ram[0x12] = 4; q->ram[0x13] = 1; ps.armHandOff(*q);                      // the scroll is skipped: screen 36, mode 4
            CHECK(ps.bridgeGame() == g36);
            q->ram[0x12] = 5; q->ram[0x13] = 0; ps.update(*q);                                              // in play: handed off, the shadows are planned for screen 36
            CHECK_EQ(ps.baseRoom(), 0x36); CHECK(find(ps, 0x36) == g36); CHECK(ps.bridgeGame() == g36);      // (the entered screen's shadow only bridges the picture now: it stays in the list (and moves) until it is dropped)
            CHECK(find(ps, 0x25) == g25 && find(ps, 0x45) == g45 && find(ps, 0x35) == dep);                 // still neighbours: the same shadows, not respawned
            CHECK(g25->frames() > f25); CHECK(find(ps, 0x24) != nullptr);                                   // 24 is two screens from 36: kept (walking in and out quickly must not restart the screens), never started that far out
            CHECK(find(ps, 0x37) && find(ps, 0x27) && find(ps, 0x47)); CHECK_EQ(ps.active.size(), (size_t)12);  // (8 neighbours, the bridging shadow of 36 and the 3 kept two screens away)
            for (int i = 0; i < ShadowSet::kBridgeHold + 1; i++) ps.update(*q);
            CHECK(ps.bridgeGame() == nullptr);
            const int fa = g45->frames(); ps.animate(); CHECK(g45->frames() > fa);                           // (the glide: the real game is paused, the shadows run)
            const int fb = g45->frames(); ps.enabled = false; ps.animate(); CHECK_EQ(g45->frames(), fb);                                  // (switched off: nothing runs)
        }
        {   // nothing flies in a shadow: shots (types $53-$5C) are deleted, monsters and the dummy ($5D) are left alone
            Nes& g0 = set.active[1]->nes(); g0.ram[0x351] = 0x53; g0.ram[0x352] = 0x11; g0.ram[0x353] = 0x5C; g0.ram[0x354] = 0x5D; set.update(*r);
            CHECK_EQ(g0.ram[0x351], 0); CHECK_EQ(g0.ram[0x352], 0x11); CHECK_EQ(g0.ram[0x353], 0); CHECK_EQ(g0.ram[0x354], 0x5D);
        }
        {   // the game counts the monster shots in the air and lets nobody shoot at 4: shots deleted here must not stay counted (monsters stood still for 46 of every 48 frames)
            Nes& g0 = set.active[2]->nes(); g0.ram[0x34C] = 4; g0.ram[0x351] = 0x57; set.update(*r);
            CHECK_EQ(g0.ram[0x351], 0); CHECK_EQ(g0.ram[0x34C], 0);
        }
        {   // the raft and the Great Fairy's hearts are set off by where the player stands: a screen's stand-in Link must never start them (and is not left halted by them)
            Nes& g0 = set.active[3]->nes();
            g0.ram[0x34F + 11] = 0x61; g0.ram[0xAC + 11] = 2;                                               // a raft going up
            g0.ram[0x34F + 12] = 0x2F; g0.ram[0xAC + 12] = 1;                                               // the fairy's animation begun
            g0.ram[0x34F + 10] = 0x2F; g0.ram[0xAC + 10] = 3;                                               // (a fairy that has finished stays finished)
            g0.ram[0x34F + 9] = 0x61; g0.ram[0xAC + 9] = 0;                                                 // (a raft at rest)
            g0.ram[0xAC] = 0x40; g0.ram[0x63] = 0x40;                                                       // Link halted, hearts filling
            set.update(*r);
            CHECK_EQ(g0.ram[0xAC + 11], 0); CHECK_EQ(g0.ram[0xAC + 12], 0); CHECK_EQ(g0.ram[0xAC + 10], 3); CHECK_EQ(g0.ram[0xAC + 9], 0);
            CHECK_EQ(g0.ram[0xAC], 0); CHECK_EQ(g0.ram[0x63], 0);
            for (int sl = 9; sl <= 12; sl++) g0.ram[0x34F + sl] = 0;
        }
        // the real game changes room: new shadows; not in plain play: they wait; in a dungeon: they are gone
        r->ram[0xEB] = 0x36; set.update(*r); CHECK_EQ(set.baseRoom(), 0x36); CHECK_EQ(set.active.size(), (size_t)14);   // (8 around 36: 3 new, 3 kept from 35's ring and 3 two screens away)
        {   // (still neighbours: kept with their age; the new ones just began)
            ShadowGame *n37 = nullptr, *k45 = nullptr; for (ShadowGame* g : set.active) { if (g->room() == 0x37) n37 = g; if (g->room() == 0x45) k45 = g; }
            CHECK(n37 != nullptr && n37->frames() <= ShadowSet::kSpinStepsPerFrame); CHECK(k45 != nullptr && k45->frames() > ShadowSet::kSpinStepsPerFrame * 5);
        }
        r->ram[0x12] = 7; const int f0 = set.active[0]->frames(); set.update(*r); CHECK(set.active[0]->frames() > f0);       // (a scroll, a cave, the walk in: the overworld goes on)
        r->ram[0xE1] = 8; const int f1 = set.active[0]->frames(); set.update(*r); CHECK_EQ(set.active[0]->frames(), f1);     // (the inventory: the game stands still, so do the shadows)
        r->ram[0xE1] = 0; r->ram[0xE0] = 1; set.update(*r); CHECK_EQ(set.active[0]->frames(), f1); r->ram[0xE0] = 0;
        r->ram[0x12] = 5; r->ram[0x10] = 1; for (int i = 0; i <= ShadowSet::kDropDelay; i++) { CHECK(!set.active.empty()); set.update(*r); } CHECK(set.active.empty()); CHECK_EQ(set.baseRoom(), -1);                   // (inside a dungeon the shadows are gone: nothing of the overworld shows, the game makes its monsters afresh when Link comes out)
        r->ram[0x10] = 0; r->ram[0x12] = 0x11; set.update(*r); CHECK(set.active.empty()); CHECK_EQ(set.baseRoom(), -1);               // (the overworld is gone: game over, title)
        r->ram[0x12] = 5; set.enabled = false; set.update(*r); CHECK(set.active.empty());
    }
    {   // the overworld goes on while Link is in a cave, down stairs or in a dungeon, and the screen he left carries on as a shadow (no monsters popping up when he comes back)
        auto find = [&](ShadowSet& st, int room) -> ShadowGame* { for (ShadowGame* g : st.active) if (g->room() == room) return g; return nullptr; };
        auto r = realGame(0x35); ShadowSet set; set.enabled = true; set.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        for (int i = 0; i < 330; i++) set.update(*r);
        CHECK_EQ(set.active.size(), (size_t)8);
        r->ram[0x350] = 0x10; r->ram[0x71] = 40;
        r->ram[0x12] = 0x10; set.update(*r);                                                                // Link steps onto a cave entrance: the walk of mode $10 (RoomId is still this screen's)
        ShadowGame* dep = find(set, 0x35);                                                                  // the screen carries on from exactly this state (the game's picture of the walk has no monsters)
        for (int i = 0; i < 7; i++) set.update(*r);                                                         // (the walk goes on: nothing is captured again)
        r->ram[0x12] = 0x0B; set.update(*r); CHECK_EQ(find(set, 0x35), dep);
        CHECK(dep != nullptr && dep->ready()); CHECK_EQ(set.active.size(), (size_t)9);
        if (dep) { CHECK_EQ(dep->nes().ram[0x12], 5); CHECK_EQ(dep->nes().ram[0xEB], 0x35); CHECK_EQ(dep->nes().ram[0x350], 0x10); CHECK(dep->nes().ram[0x71] >= 40 && dep->nes().ram[0x71] <= 55); }
        ShadowGame* n36 = find(set, 0x36); CHECK(n36 != nullptr);
        r->ram[0x12] = 0x0B; r->ram[0x350] = 0;                                                             // inside the cave (the real game's object RAM is something else now)
        const int fd = dep ? dep->frames() : 0, fn = n36 ? n36->frames() : 0; const uint8_t x0 = dep ? dep->nes().ram[0x71] : 0;
        for (int i = 0; i < 20; i++) set.update(*r);
        if (dep && n36) { CHECK_EQ(dep->frames(), fd + 20); CHECK_EQ(n36->frames(), fn + 20); CHECK(dep->nes().ram[0x71] != x0); CHECK_EQ(dep->nes().ram[0x350], 0x10); }
        CHECK_EQ(set.active.size(), (size_t)9); CHECK_EQ(find(set, 0x35), dep);                             // (nothing respawned, no second shadow of the screen)
        r->ram[0x12] = 0x0A; set.update(*r); CHECK(set.bridgeGame() == dep);                                // coming out: the picture shows the screen without monsters, its shadow stands in
        r->ram[0x12] = 4; r->ram[0x13] = 0; for (int sl = 1; sl <= 12; sl++) r->ram[0x34F + sl] = 0; r->ram[0x71] = 1;
        set.update(*r); CHECK(set.handOffPending()); CHECK(set.bridgeGame() == dep);                        // (armed by the way back, not only by a scroll)
        r->ram[0x350] = 0x10; for (int i = 0; i < 20; i++) set.update(*r);                                     // the game has made its monsters and Link walks out of the cave (20 frames): nothing is copied yet
        CHECK(set.handOffPending()); CHECK_EQ(r->ram[0x71], 1);
        r->ram[0x12] = 5; set.update(*r);                                                                    // in play: the shadow's monsters take over where the shadow is NOW (no jump back)
        CHECK(!set.handOffPending()); CHECK_EQ(r->ram[0x71], dep->nes().ram[0x71]); CHECK(dep->nes().ram[0x71] > x0);
        CHECK(set.bridgeGame() == dep && !set.bridgeShowsInPlay());                                          // (the first plain frame starts the hold)
        for (int i = 0; i < ShadowSet::kBridgeHold - 1; i++) { set.update(*r); CHECK(set.bridgeShowsInPlay()); }   // the first PICTURES of plain play have no monsters yet: the bridge is drawn for them
        set.update(*r); CHECK(!set.bridgeShowsInPlay());
        for (int i = 0; i < ShadowSet::kBridgeHold + 2; i++) set.update(*r);
        CHECK(set.bridgeGame() == nullptr); CHECK(find(set, 0x35) == nullptr); CHECK_EQ(set.active.size(), (size_t)8);   // (the screen is the real game's again: one simulation of it only)
        // the killed ones stay dead: the shadow has one monster, the game makes two
        r->ram[0x350] = 0x10; r->ram[0x12] = 0x10; set.update(*r); r->ram[0x12] = 0x0B; set.update(*r); dep = find(set, 0x35); CHECK(dep != nullptr);
        r->ram[0x12] = 4; r->ram[0x13] = 0; for (int sl = 1; sl <= 12; sl++) r->ram[0x34F + sl] = 0; set.update(*r);
        r->ram[0x350] = 0x10; r->ram[0x351] = 0x10; r->ram[0x12] = 5; set.update(*r); CHECK(!set.handOffPending()); CHECK_EQ(r->ram[0x350], 0x10); CHECK_EQ(r->ram[0x351], 0);      // (nothing is dropped and respawned: the screen is as it was)
        // stairs: RoomId is already the cellar's during the walk and when it is over; a dungeon: CurLevel is already the level's, and the shadows live only through the walk
        auto q = realGame(0x35); ShadowSet ds; ds.enabled = true; ds.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        for (int i = 0; i < 330; i++) ds.update(*q);
        q->ram[0xEB] = 0x3F; q->ram[0x12] = 0x10; q->ram[0x350] = 0x10; ds.update(*q);
        ShadowGame* sd = find(ds, 0x35); CHECK(sd != nullptr && sd->ready()); CHECK(find(ds, 0x3F) == nullptr); CHECK_EQ(ds.active.size(), (size_t)9);
        for (int i = 0; i < 5; i++) ds.update(*q);
        q->ram[0x12] = 9; ds.update(*q); CHECK_EQ(find(ds, 0x35), sd);                                      // (the cellar's fade begins: the same shadow)
        if (sd) { CHECK_EQ(sd->nes().ram[0xEB], 0x35); CHECK_EQ(sd->nes().ram[0x10], 0); CHECK_EQ(sd->nes().ram[0x12], 5); }
        const int fs = sd ? sd->frames() : 0; for (int i = 0; i < 30; i++) ds.update(*q); CHECK(sd && sd->frames() == fs + 30);
        auto dq = realGame(0x35); ShadowSet dd; dd.enabled = true; dd.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        for (int i = 0; i < 330; i++) dd.update(*dq);
        dq->ram[0x10] = 3; dq->ram[0x12] = 0x10; for (int i = 0; i < 4; i++) dd.update(*dq);                // a dungeon entrance: the shadows go on through the walk (the screen's own included)
        CHECK_EQ(dd.active.size(), (size_t)9); CHECK(find(dd, 0x35) != nullptr);
        dq->ram[0x12] = 2; for (int i = 0; i < ShadowSet::kDropDelay; i++) { dd.update(*dq); CHECK(!dd.active.empty()); }   // the dungeon is loading: the last pictures of the walk still show them
        dd.update(*dq); CHECK(dd.active.empty()); CHECK_EQ(dd.baseRoom(), -1);                              // ... then all gone
        dq->ram[0x12] = 5; dd.update(*dq); CHECK(dd.active.empty());
    }
    {   // the room item (the overworld's heart container of room $5F: slot $13, no object type) is kept clear of too: the stand-in must not take it as he strolls over the lake
        auto r = realGame(0x35); ShadowSet set; set.enabled = true; set.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        for (int i = 0; i < 330; i++) set.update(*r);
        Nes& sh = set.active[0]->nes();
        sh.ram[0xAC + 0x13] = 0; sh.ram[0xAB] = 0x1A; sh.ram[0x70 + 0x13] = 0x78; sh.ram[0x84 + 0x13] = 0x8D - 3;   // the heart container right where he starts (type stays 0)
        bool clear = true;
        for (int i = 0; i < 900; i++) { set.update(*r); clear = clear && (std::abs(sh.ram[0x70] - sh.ram[0x70 + 0x13]) >= 9 || std::abs(sh.ram[0x84] + 3 - sh.ram[0x84 + 0x13]) >= 9); }
        CHECK(clear);
        sh.ram[0xAB] = 0x3F; sh.ram[0xAC + 0x13] = 0xFF;                                                          // (no item, or one that is not active: nothing to keep clear of)
    }
    {   // the stand-in Link strolls about (monsters steer towards the player: a Link who never moves gathers them all around him) but never takes an item
        auto r = realGame(0x35); ShadowSet set; set.enabled = true; set.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        for (int i = 0; i < 330; i++) set.update(*r);
        ShadowGame* g = set.active[0]; Nes& sh = g->nes();
        sh.ram[0x34F + 6] = 0x60; sh.ram[0xAC + 6] = 0x22; sh.ram[0x70 + 6] = 0x78; sh.ram[0x84 + 6] = 0x8D - 3;      // an item right where he starts
        int minX = 255, maxX = 0, minY = 255, maxY = 0; bool clear = true, inside = true, moved = false; uint8_t px = sh.ram[0x70], py = sh.ram[0x84];
        for (int i = 0; i < 900; i++) {
            set.update(*r);
            const int x = sh.ram[0x70], y = sh.ram[0x84];
            minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y);
            clear = clear && (std::abs(x - sh.ram[0x70 + 6]) >= 9 || std::abs(y + 3 - sh.ram[0x84 + 6]) >= 9);          // (the game lets Link take an item within 8 px)
            inside = inside && x >= 0x28 && x <= 0xD0 && y >= 0x6D && y <= 0xBD;
            moved = moved || x != px || y != py; px = (uint8_t)x; py = (uint8_t)y;
        }
        CHECK(moved); CHECK(clear); CHECK(inside); CHECK(maxX - minX > 40 && maxY - minY > 20);
        CHECK_EQ(sh.ram[0x60], 0); CHECK_EQ(sh.ram[0x4A], 7);                                                  // (the monsters always chase him: the game flips to a mirrored target when this timer runs out)
        int still = 0; px = sh.ram[0x70]; py = sh.ram[0x84];
        for (int i = 0; i < 600; i++) { set.update(*r); sh.ram[0x4A] = 0; still += sh.ram[0x70] == px && sh.ram[0x84] == py; px = sh.ram[0x70]; py = sh.ram[0x84]; }
        CHECK(still < 20);                                                                                     // (he keeps walking: a Link who stands still makes the game mirror the monsters' target)
        set.update(*r); CHECK_EQ(sh.ram[0x4A], 7);                                                             // (the timer is put back every frame)
        // the hand-off: the dice and the Link position are the shadow's / the real game's from then on
        auto q = realGame(0x35); ShadowGame h; h.adopt(*q, 0x35); Nes& hs = h.nes();
        q->ram[0x350] = 0x10; hs.ram[0x34F + 1] = 0x10; q->ram[0x70] = 9; q->ram[0x84] = 99; hs.ram[0x18] = 0xAB; q->ram[0x18] = 0xCD;
        CHECK(h.handOffTo(*q) == ShadowGame::HandOff::Done);
        CHECK_EQ(q->ram[0x18], 0xAB); CHECK_EQ(hs.ram[0x70], 9); CHECK_EQ(hs.ram[0x84], 99);
        for (int i = 0; i < 5; i++) h.stepFrame();
        CHECK_EQ(hs.ram[0x70], 9);                                  // (and he stays there: no more strolling)
    }
    {   // enter a screen and leave it again within a moment (main.cpp: mode 6 seen by update, the scroll skipped, armHandOff, the glide = animate, then mode 4 and play): both screens keep their monsters
        auto find = [&](ShadowSet& st, int room) -> ShadowGame* { for (ShadowGame* g : st.active) if (g->room() == room) return g; return nullptr; };
        for (int quick : {1, 2, 3, 4, 5, 6, 10, 14}) {
            auto q = realGame(0x35); ShadowSet ps; ps.enabled = true; ps.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
            for (int i = 0; i < 330; i++) ps.update(*q);
            auto cross = [&](int to, int glide) {                                                                  // what one screen change looks like to the shadow set
                q->ram[0x12] = 6; ps.update(*q);
                q->ram[0x12] = 7; ps.update(*q);                                                                   // (the next frame: the game is in mode 7; main.cpp skips the rest of the scroll)
                q->ram[0xEB] = (uint8_t)to; q->ram[0x12] = 4; q->ram[0x13] = 1; for (int sl = 1; sl <= 12; sl++) q->ram[0x34F + sl] = 0; q->ram[0x71] = 1;
                ps.armHandOff(*q);
                for (int i = 0; i < glide; i++) ps.animate();
                for (int i = 0; i < 3; i++) ps.update(*q);                                                         // (mode 4: Link walks in)
                q->ram[0x12] = 5; q->ram[0x13] = 0; q->ram[0x350] = 0x10;                                          // the game's own fresh monster
            };
            q->ram[0x350] = 0x10; q->ram[0x71] = 33;
            cross(0x36, 12); ps.update(*q);                                                                         // (hand-off of 36 happens in this first play update)
            for (int i = 1; i < quick; i++) ps.update(*q);
            ShadowGame* a = find(ps, 0x35); CHECK(a != nullptr && a->ready());
            const int fa = a ? a->frames() : 0; const uint8_t xa = a ? a->nes().ram[0x71] : 0;
            q->ram[0x71] = 70;                                                                                      // 36's monster at the moment Link leaves
            cross(0x35, 12);
            ShadowGame* b = find(ps, 0x36); CHECK(b != nullptr && b->ready());                                      // 36 became a shadow, its monster where Link left it
            if (b) CHECK(b->nes().ram[0x71] >= 70 && b->nes().ram[0x71] <= 90);
            ps.update(*q);
            CHECK(!ps.handOffPending());
            CHECK(q->ram[0x71] != 1 && q->ram[0x71] != 0);                                                          // 35's monsters are the shadow's, not the fresh ones
            CHECK(a && a->frames() >= fa);
            (void)xa;
        }
    }
    {   // two screens away is still kept (a quick walk there and back must not start them afresh), three screens away is dropped
        auto find = [&](ShadowSet& st, int room) -> ShadowGame* { for (ShadowGame* g : st.active) if (g->room() == room) return g; return nullptr; };
        auto q = realGame(0x35); ShadowSet ps; ps.enabled = true; ps.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        for (int i = 0; i < 330; i++) ps.update(*q);
        ShadowGame* k26 = find(ps, 0x26); CHECK(k26 != nullptr);
        q->ram[0xEB] = 0x38; ps.update(*q);                                                                  // (columns 4-6 around 35; the new base is column 8)
        CHECK(find(ps, 0x26) == k26); CHECK(find(ps, 0x25) == nullptr); CHECK(find(ps, 0x45) == nullptr);   // 26 is two columns away: kept; 25 and 45 are three: gone
    }
    {   // an armos that woke in a screen's shadow left floor (stairs for the secret one) where its statue stood: the real game's statues on those squares follow when Link comes back
        auto find = [&](ShadowSet& st, int room) -> ShadowGame* { for (ShadowGame* g : st.active) if (g->room() == room) return g; return nullptr; };
        auto q = realGame(0x35); ShadowSet ps; ps.enabled = true; ps.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        for (int i = 0; i < 330; i++) ps.update(*q);
        ShadowGame* sh = find(ps, 0x36); CHECK(sh && sh->ready());
        auto put = [&](Nes& n, int col, int row, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {                    // a 2x2 square, column-major
            n.cart.cpuWrite((uint16_t)(0x6530 + col * 22 + row), a); n.cart.cpuWrite((uint16_t)(0x6530 + col * 22 + row + 1), b);
            n.cart.cpuWrite((uint16_t)(0x6530 + (col + 1) * 22 + row), c); n.cart.cpuWrite((uint16_t)(0x6530 + (col + 1) * 22 + row + 1), d); };
        auto get = [&](Nes& n, int col, int row) { return (int)n.cart.cpuRead((uint16_t)(0x6530 + col * 22 + row)); };
        put(sh->nes(), 4, 6, 0x70, 0x71, 0x72, 0x73);                                                             // the secret armos woke: stairs
        put(sh->nes(), 10, 2, 0x26, 0x26, 0x26, 0x26);                                                            // another one: floor
        put(sh->nes(), 20, 8, 0xC0, 0xC1, 0xC2, 0xC3);                                                            // one that did not wake
        put(*q, 4, 6, 0xC0, 0xC1, 0xC2, 0xC3); put(*q, 10, 2, 0xC0, 0xC1, 0xC2, 0xC3); put(*q, 20, 8, 0xC0, 0xC1, 0xC2, 0xC3);   // the game's fresh layout: all statues
        q->ram[0x301] = 0;
        q->ram[0xEB] = 0x36; q->ram[0x12] = 4; ps.armHandOff(*q); ps.update(*q);
        CHECK_EQ(get(*q, 4, 6), 0x70); CHECK_EQ(get(*q, 5, 7), 0x73); CHECK_EQ(get(*q, 10, 2), 0x26); CHECK_EQ(get(*q, 20, 8), 0xC0);
        CHECK_EQ((int)q->ram[0x301], 20);                                                                         // two squares queued for the PPU: 2 x (2 records of 5 bytes)
        // the first square (scan order: column 4): records {hi, lo, $82, t, t'} {hi, lo+1, $82, t, t'}, nametable row (8 + 6) * 8 = 112 -> $2000 + 112 * 4 + 4 = $21C4
        CHECK(q->ram[0x302] == 0x21 && q->ram[0x303] == 0xC4 && q->ram[0x304] == 0x82 && q->ram[0x305] == 0x70 && q->ram[0x306] == 0x71);
        CHECK(q->ram[0x307] == 0x21 && q->ram[0x308] == 0xC5 && q->ram[0x309] == 0x82 && q->ram[0x30A] == 0x72 && q->ram[0x30B] == 0x73);
        CHECK_EQ((int)q->ram[0x302 + 20], 0xFF);                                                                  // (the end marker follows the last record; the next square overwrites it, as in the game)
        ps.update(*q); CHECK_EQ((int)q->ram[0x301], 20);                                                          // done once: no statue left there, nothing queued again
        // coming out of a cave or stairs (mode $A, the fade): the statues are already gone behind the fade, before the first picture
        auto q4 = realGame(0x35); ShadowSet p4; p4.enabled = true; p4.forceSpinSteps = ShadowSet::kSpinStepsPerFrame; for (int i = 0; i < 330; i++) p4.update(*q4);
        ShadowGame* s4 = find(p4, 0x36); CHECK(s4 != nullptr);
        if (s4) { put(s4->nes(), 6, 4, 0x70, 0x71, 0x72, 0x73); put(*q4, 6, 4, 0xC0, 0xC1, 0xC2, 0xC3); q4->ram[0x301] = 0; q4->ram[0xEB] = 0x36; q4->ram[0x12] = 0x0A; p4.update(*q4);
                 CHECK_EQ(get(*q4, 6, 4), 0x70); CHECK_EQ((int)q4->ram[0x301], 10); }
        // a screen with no shadow of its own is left as the game laid it out
        auto q2 = realGame(0x35); ShadowSet p2; p2.enabled = true; p2.forceSpinSteps = ShadowSet::kSpinStepsPerFrame; put(*q2, 4, 6, 0xC0, 0xC1, 0xC2, 0xC3); q2->ram[0x301] = 0; q2->ram[0x12] = 5; p2.update(*q2);
        CHECK_EQ(get(*q2, 4, 6), 0xC0); CHECK_EQ((int)q2->ram[0x301], 0);
    }
    {   // the PPU transfer buffer is only $302-$33E long (OpenDoorwayMask follows): a busy buffer is left alone, a free one is used up to its end
        auto find = [&](ShadowSet& st, int room) -> ShadowGame* { for (ShadowGame* g : st.active) if (g->room() == room) return g; return nullptr; };
        auto put = [&](Nes& n, int col, int row, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
            n.cart.cpuWrite((uint16_t)(0x6530 + col * 22 + row), a); n.cart.cpuWrite((uint16_t)(0x6530 + col * 22 + row + 1), b);
            n.cart.cpuWrite((uint16_t)(0x6530 + (col + 1) * 22 + row), c); n.cart.cpuWrite((uint16_t)(0x6530 + (col + 1) * 22 + row + 1), d); };
        auto get = [&](Nes& n, int col, int row) { return (int)n.cart.cpuRead((uint16_t)(0x6530 + col * 22 + row)); };
        for (int len : {0x38, 0x31, 0xF0}) {
            auto q = realGame(0x35); ShadowSet ps; ps.enabled = true; ps.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
            for (int i = 0; i < 330; i++) ps.update(*q);
            ShadowGame* sh = find(ps, 0x36); CHECK(sh && sh->ready());
            if (!sh) continue;
            put(sh->nes(), 4, 6, 0x70, 0x71, 0x72, 0x73); put(*q, 4, 6, 0xC0, 0xC1, 0xC2, 0xC3);
            q->ram[0x301] = (uint8_t)len; q->ram[0x33F] = 0xAB; q->ram[0x340] = 0xCD;                              // (the bytes after the buffer: the game's own variables)
            q->ram[0xEB] = 0x36; q->ram[0x12] = 4; ps.armHandOff(*q); ps.update(*q);
            CHECK_EQ((int)q->ram[0x33F], 0xAB); CHECK_EQ((int)q->ram[0x340], 0xCD);                                  // never written past the buffer
            if (len == 0x31) { CHECK_EQ((int)q->ram[0x301], 0x31 + 10); CHECK_EQ(get(*q, 4, 6), 0x70); }             // 49 + 11 = 60 <= 61: fits
            else             { CHECK_EQ((int)q->ram[0x301], len); CHECK_EQ(get(*q, 4, 6), 0xC0); }                    // no room: the statue is left for the next frame
        }
    }
    {   // a retry starts the same screen again from the REAL game, whichever shadow the first start came from (the offset is worked out from the rooms)
        auto src = realGame(0x44); auto real = realGame(0x35);                                                      // (a shadow of 0x44 stands in for the source)
        ShadowGame g; CHECK(g.start(*src, 0, -1, 1)); CHECK_EQ(g.room(), 0x34);                                    // made from 0x44, one screen up
        CHECK(g.retry(*real, 5)); CHECK_EQ(g.room(), 0x34);                                                        // the real game is at 0x35: the target is still 0x34 (it used to become 0x25)
        CHECK(g.state() == ShadowGame::State::SpinUp);
        ShadowGame far; CHECK(far.start(*src, 0, -1, 1));
        auto away = realGame(0x37);                                                                                // three screens from the target: no walk to it from there
        CHECK(!far.retry(*away, 5)); CHECK_EQ(far.room(), 0x34); CHECK(far.state() == ShadowGame::State::Failed);
    }
    {   // scenery (the dock of the raft, rocks, trees) is never handed over: a raft a shadow's Link had started would drag the real Link up the screen
        auto q = realGame(0x35); ShadowGame g; g.adopt(*q, 0x35); Nes& hs = g.nes();
        for (int sl = 1; sl <= 12; sl++) { hs.ram[0x34F + sl] = 0; q->ram[0x34F + sl] = 0; }
        hs.ram[0x34F + 5] = 0x61; hs.ram[0xAC + 5] = 2; hs.ram[0x84 + 5] = 0x7D;            // the shadow's dock: the raft is on its way up
        q->ram[0x34F + 5] = 0x61; q->ram[0xAC + 5] = 0; q->ram[0x84 + 5] = 0x40;            // the game's dock: at rest
        hs.ram[0x34F + 6] = 0x10; hs.ram[0x70 + 6] = 77; q->ram[0x34F + 6] = 0x64; q->ram[0xAC + 6] = 0;   // a monster where the game has a tree
        q->ram[0x34F + 7] = 0x11; q->ram[0x70 + 7] = 5; hs.ram[0x34F + 7] = 0x62; hs.ram[0xAC + 7] = 9;    // a monster where the shadow has a rock
        hs.ram[0x34F + 8] = 0x10; hs.ram[0x70 + 8] = 88; q->ram[0x34F + 8] = 0;
        CHECK(g.handOffTo(*q) == ShadowGame::HandOff::Done);
        CHECK_EQ(q->ram[0xAC + 5], 0); CHECK_EQ(q->ram[0x84 + 5], 0x40); CHECK_EQ(q->ram[0x34F + 5], 0x61);   // (the dock is the game's own)
        CHECK_EQ(q->ram[0x34F + 6], 0x64); CHECK_EQ(q->ram[0x70 + 6], 0);                                       // (so is the tree)
        CHECK_EQ(q->ram[0x34F + 7], 0x11); CHECK_EQ(q->ram[0x70 + 7], 5);                                       // (and the monster the shadow has a rock for)
        CHECK_EQ(q->ram[0x34F + 8], 0x10); CHECK_EQ(q->ram[0x70 + 8], 88);                                      // (monsters are copied)
        // the stand-in Link never stands where a Link starts the raft (X $60, or $80 in room $55), and never halted
        auto r = realGame(0x35); ShadowSet set; set.enabled = true; set.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        for (int i = 0; i < 330; i++) set.update(*r);
        Nes& sh = set.active[0]->nes(); bool onDock = false, below = false, above = false, halted = false;
        for (int i = 0; i < 4000; i++) {
            set.update(*r); const int x = sh.ram[0x70];
            onDock = onDock || x == 0x60 || x == 0x80; below = below || x < 0x60; above = above || x > 0x80; halted = halted || sh.ram[0xAC] != 0;
        }
        CHECK(!onDock); CHECK(below && above); CHECK(!halted);
        bool atPond = false; for (int i = 0; i < 4000; i++) { set.update(*r); atPond = atPond || sh.ram[0x84] == 0xAD; }          // (and never at the Great Fairy's pond edge: Y $AD makes her fill hearts and halt him)
        CHECK(!atPond);
    }
    {   // the picture of leaving a screen (mode 6) has no monsters: the shadow made from that very state stands in for them
        auto r = realGame(0x35); ShadowSet set; set.enabled = true; set.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        for (int i = 0; i < 330; i++) set.update(*r);
        r->ram[0x12] = 6; set.update(*r);
        ShadowGame* g = nullptr; for (ShadowGame* o : set.active) if (o->room() == 0x35) g = o;
        CHECK(g != nullptr); CHECK(set.bridgeGame() == g);
        r->ram[0x12] = 7; set.update(*r); CHECK(set.bridgeGame() == g);                                         // (until the next screen is armed)
        r->ram[0xEB] = 0x36; r->ram[0x12] = 4; r->ram[0x13] = 1; set.armHandOff(*r); CHECK(set.bridgeGame() != g);
    }
    {   // a captured shadow has run one frame already (the game hides its sprites in mode 6 / $10; the first picture of the shadow must not be that blank one)
        auto r = realGame(0x35); r->ram[0x12] = 6; ShadowGame g;
        CHECK(g.captureFrom(*r, 0x35, 5)); CHECK_EQ(g.frames(), 1); CHECK(g.ready());
        r->ram[0x12] = 0x10; CHECK(g.captureFrom(*r, 0x34, 5)); CHECK_EQ(g.room(), 0x34); CHECK_EQ(g.nes().ram[0xEB], 0x34);        // (a warp: the room is given, RoomId may be the cellar's)
        r->ram[0x12] = 6; CHECK(!g.captureFrom(*r, 0x61, 5));                                                                      // (not a maze room)
    }
    {   // an item that dropped on the screen Link left lies there when he comes back (its despawn timer ran on), whatever monsters the game made
        auto r = realGame(0x35); ShadowGame g; g.adopt(*r, 0x35); Nes& sh = g.nes();
        for (int sl = 1; sl <= 12; sl++) sh.ram[0x34F + sl] = 0;
        sh.ram[0x34F + 4] = 0x60; sh.ram[0xAC + 4] = 0x22; sh.ram[0x28 + 4] = 0x77; sh.ram[0x70 + 4] = 120; sh.ram[0x84 + 4] = 150;
        for (int sl = 1; sl <= 12; sl++) r->ram[0x34F + sl] = 0;
        r->ram[0x350] = 0x11; r->ram[0x351] = 0x11; r->ram[0x71] = 9;                                           // two monsters the shadow does not have (killed there)
        CHECK(g.handOffTo(*r) == ShadowGame::HandOff::Done);
        CHECK_EQ(r->ram[0x34F + 4], 0x60); CHECK_EQ(r->ram[0xAC + 4], 0x22); CHECK_EQ(r->ram[0x28 + 4], 0x77); CHECK_EQ(r->ram[0x70 + 4], 120); CHECK_EQ(r->ram[0x84 + 4], 150);   // (the item, its timer included)
        CHECK_EQ(r->ram[0x350], 0); CHECK_EQ(r->ram[0x351], 0);                                                 // (the monsters that died there do not come back)
        for (int sl = 1; sl <= 12; sl++) sh.ram[0x34F + sl] = 0;                                                 // a screen with nothing left in it
        r->ram[0x350] = 0x11; CHECK(g.handOffTo(*r) == ShadowGame::HandOff::Done); CHECK_EQ(r->ram[0x350], 0);
    }
    {   // the spawn is seen at the game's own speed: only the unseen walk to the screen is hurried
        auto r = realGame(0x35); ShadowSet set; set.enabled = true; set.forceSpinSteps = ShadowSet::kSpinStepsPerFrame; int settleSteps = 0, spinFast = 0; bool slow = true;
        for (int i = 0; i < 400; i++) {
            std::vector<std::pair<ShadowGame*, std::pair<int, int>>> before;
            for (ShadowGame* g : set.active) before.push_back({g, {g->frames(), (int)g->state()}});
            set.update(*r);
            for (auto& b : before) {
                const int d = b.first->frames() - b.second.first;
                if (b.second.second == (int)ShadowGame::State::Settling) { settleSteps++; slow = slow && d == 1; }
                if (b.second.second == (int)ShadowGame::State::SpinUp && d == ShadowSet::kSpinStepsPerFrame) spinFast++;
            }
        }
        CHECK(settleSteps > 100); CHECK(slow); CHECK(spinFast > 50);
    }
    {   // spin-up speed: more cores and fewer spinning shadows run more frames per real frame (never fewer than 3, never more than 16); one thread keeps 3
        CHECK_EQ(ShadowSet::spinSteps(8, 1), 3); CHECK_EQ(ShadowSet::spinSteps(0, 8), 3); CHECK_EQ(ShadowSet::spinSteps(8, 8), 16); CHECK_EQ(ShadowSet::spinSteps(8, 4), 12);
        CHECK_EQ(ShadowSet::spinSteps(35, 8), 5); CHECK_EQ(ShadowSet::spinSteps(60, 2), 3); CHECK(ShadowSet::spinSteps(1, 8) == ShadowSet::kSpinStepsMax);
    }
    {   // every screen the canvas shows gets a shadow (7 x 5 at the widest zoom), the far ones made from a shadow beside them; screens that are no longer wanted go once they are 3 away
        auto q = realGame(0x35); ShadowSet ps; ps.enabled = true; ps.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        uint8_t want[128] = {}; int wantedN = 0;
        for (int row = 1; row <= 5; row++) for (int col = 2; col <= 8; col++) { want[row * 16 + col] = 1; wantedN++; }
        ps.setWanted(want);
        for (int i = 0; i < 1500; i++) ps.update(*q);
        int up = 0; for (int row = 1; row <= 5; row++) for (int col = 2; col <= 8; col++) { const int r = row * 16 + col; if (r == 0x35) continue; bool v = false; for (ShadowGame* g : ps.active) v = v || (g->room() == r && g->visible()); up += v; }
        CHECK_EQ(up, wantedN - 1);                                                                          // (all of them but Link's own screen)
        CHECK_EQ(ps.active.size(), (size_t)(wantedN - 1));
        uint8_t none[128] = {}; ps.setWanted(none); q->ram[0xEB] = 0x36; ps.update(*q);                       // one screen east, nothing wanted any more: only the ones within two stay
        for (ShadowGame* g : ps.active) CHECK(std::abs((g->room() & 15) - 6) <= 2 && std::abs((g->room() >> 4) - 3) <= 2);
    }
    {   // a boomerang Link left behind that turns back flies to the REAL Link, screen by screen: it steers to the edge facing him and is handed over there
        auto find = [&](ShadowSet& st, int room) -> ShadowGame* { for (ShadowGame* g : st.active) if (g->room() == room) return g; return nullptr; };
        auto q = realGame(0x35); ShadowSet ps; ps.enabled = true; ps.forceSpinSteps = ShadowSet::kSpinStepsPerFrame;
        uint8_t w[128] = {}; w[0x37] = 1; ps.setWanted(w);                                                  // (0x37, two screens east, is on the canvas)
        for (int i = 0; i < 900; i++) ps.update(*q);
        ShadowGame* e = find(ps, 0x36); ShadowGame* ee = e; CHECK(e && e->ready());
        uint8_t* er = e->nes().ram; const int B = 0x0F;
        auto boom = [&](uint8_t* r, int x, int y, int state) { r[0x70 + B] = (uint8_t)x; r[0x84 + B] = (uint8_t)y; r[0xAC + B] = (uint8_t)state; r[0x98 + B] = 2; r[0x28 + B] = 7; };
        boom(er, 100, 120, 0x50); ps.update(*q);                                                          // returning, still mid-screen: the stand-in Link is where it must go (the west edge)
        CHECK_EQ((int)er[0x70], 0); CHECK_EQ((int)er[0x84], 120); CHECK_EQ((int)q->ram[0xAC + B], 0);
        er[0x70 + B] = 0x0A; q->ram[0x12] = 5; ps.update(*q);                                              // at the west edge: into the real game, entering at its east edge
        CHECK_EQ((int)q->ram[0x70 + B], 0xE2); CHECK_EQ((int)q->ram[0x84 + B], 120); CHECK_EQ((int)q->ram[0xAC + B], 0x50); CHECK_EQ((int)q->ram[0x28 + B], 7);
        CHECK_EQ((int)er[0xAC + B], 0);                                                                   // (gone from the shadow)
        q->ram[0x34F + B] = 0; q->ram[0xAC + B] = 0;
        boom(er, 8, 90, 0x40); q->ram[0xAC + B] = 0x10; ps.update(*q);           // the real boomerang slot is busy: it waits at the edge
        CHECK_EQ((int)er[0xAC + B], 0x40); CHECK_EQ((int)q->ram[0xAC + B], 0x10);                          // (the real game's own boomerang is untouched)
        q->ram[0xAC + B] = 0; q->ram[0x34F + B] = 0; er[0x34F + B] = 0; er[0xAC + B] = 0;
        {   // the real game is not playing yet (the glide, the walk in): the boomerang is aimed all the same, hovers at the edge out of the stand-in's reach, and is handed over once it plays
            boom(er, 40, 100, 0x50); q->ram[0x12] = 4; ps.animate(q.get());
            CHECK_EQ((int)er[0x70], 0);                                                                    // (aimed at the west edge during the glide)
            er[0x70 + B] = 0x0A; q->ram[0x34F + B] = 0; ps.animate(q.get()); ps.update(*q);
            CHECK_EQ((int)er[0xAC + B], 0x50); CHECK_EQ((int)er[0x70 + B], 14); CHECK_EQ((int)q->ram[0xAC + B], 0);   // (held at the edge band, not handed over)
            q->ram[0x12] = 5; ps.update(*q); CHECK_EQ((int)q->ram[0xAC + B], 0x50); CHECK_EQ((int)er[0xAC + B], 0);  // (played: handed over)
            q->ram[0x34F + B] = 0; q->ram[0xAC + B] = 0;
            boom(er, 10, 100, 0x50); q->ram[0x12] = 4;                                                       // nobody comes: after a while it is let go
            for (int i = 0; i < ShadowSet::kRelayPatience + 5; i++) { er[0x70 + B] = 3; ps.animate(q.get()); }
            CHECK_EQ((int)er[0x70 + B], 3);                                                                // (no longer pinned)
            er[0x34F + B] = 0; er[0xAC + B] = 0; q->ram[0x12] = 5;
        }
        ShadowGame* e2 = find(ps, 0x37); CHECK(e2 && e2->ready()); if (!e2) return;                                         // two screens east: shadow to shadow
        boom(e2->nes().ram, 5, 100, 0x40); ps.update(*q);
        CHECK_EQ((int)er[0xAC + B], 0x40); CHECK_EQ((int)er[0x70 + B], 0xE2); CHECK_EQ((int)e2->nes().ram[0xAC + B], 0);
        er[0x34F + B] = 0; er[0xAC + B] = 0;
        boom(er, 100, 120, 0x20); ps.update(*q); CHECK_EQ((int)er[0x70 + B], 100); CHECK_EQ((int)er[0xAC + B], 0x20);   // (still flying away: left alone)
        er[0xAC + B] = 0; q->ram[0xAC + B] = 0; boom(er, 5, 100, 0x85); ps.update(*q); CHECK_EQ((int)q->ram[0xAC + B], 0); CHECK_EQ((int)er[0xAC + B], 0x85);   // (bait in the slot is not a boomerang: never handed over)
        er[0xAC + B] = 0;
        (void)ee;
    }
    {   // a corner screen has only the neighbours it has
        auto r = realGame(0x00); ShadowSet set; set.enabled = true; set.forceSpinSteps = ShadowSet::kSpinStepsPerFrame; set.update(*r);
        CHECK_EQ(set.active.size(), (size_t)3);
    }
    {   // the pool gives the same result as one thread
        auto r = realGame(0x35); ShadowSet a, b; a.enabled = b.enabled = true; a.forceSpinSteps = b.forceSpinSteps = 3;
        const int saved = par::maxThreads();
        for (int i = 0; i < 60; i++) { par::maxThreads() = 1; a.update(*r); par::maxThreads() = 4; b.update(*r); }
        par::maxThreads() = saved;
        bool same = a.active.size() == b.active.size();
        for (size_t i = 0; same && i < a.active.size(); i++) same = std::memcmp(a.active[i]->nes().ram, b.active[i]->nes().ram, 0x800) == 0 && a.active[i]->state() == b.active[i]->state();
        CHECK(same);
    }
    {   // a game that never gets back to play (the walk is stuck) fails after the timeout instead of running forever
        auto r = std::make_unique<Nes>(); boot(*r, makeMockGame(true)); for (int f = 0; f < 3; f++) r->runFrame();
        r->ram[0x12] = 5; r->ram[0xEB] = 0x35; r->ram[0x10] = 0; ShadowGame g;
        CHECK(g.start(*r, 1, 0, 3));
        for (int f = 0; f < ShadowGame::kHopTimeout + 50 && g.state() == ShadowGame::State::SpinUp; f++) g.stepFrame();
        CHECK(g.state() == ShadowGame::State::Failed);
        ShadowSet set; set.enabled = true; set.forceSpinSteps = ShadowSet::kSpinStepsPerFrame; set.update(*r); for (int i = 0; i < 400; i++) set.update(*r);        // (a failed shadow is not stepped again and does no harm)
        CHECK_EQ(r->ram[0x12], 5);
    }
    {   // shadow_debug.txt lists the shadows and the monsters in their slots
        auto r = realGame(0x35); ShadowSet set; set.enabled = true; set.forceSpinSteps = ShadowSet::kSpinStepsPerFrame; for (int i = 0; i < 160; i++) set.update(*r);
        CHECK(set.dump("shadow_test_dump.txt"));
        std::FILE* f = std::fopen("shadow_test_dump.txt", "r"); std::string txt; char buf[256]; while (f && std::fgets(buf, sizeof buf, f)) txt += buf; if (f) std::fclose(f); std::remove("shadow_test_dump.txt");
        CHECK(txt.find("shadow of screen 36") != std::string::npos); CHECK(txt.find("slot 01 type 10") != std::string::npos);
    }
}

int main() {
    testBadRoms();
    testCpu();
    testPpuLayers();
    testPpuSmoothScroll();
    testMmc1();
    testPpu();
    testLagFree();
    testClone();
    testShadow();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
