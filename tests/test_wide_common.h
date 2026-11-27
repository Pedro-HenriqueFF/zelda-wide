// Shared harness for the widescreen integration tests (test_wide*.cpp): CHECK macros, a synthetic Zelda ROM, and the Ctx
// that holds the emulator + compositor every section works on.
#pragma once
#include "widescreen.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

extern int checks, failures;
#define CHECK_EQ(a, b) do { checks++; long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { failures++; std::printf("FAIL %s:%d  %s == %s  (got 0x%llX, want 0x%llX)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// The compositor reads the PPU's colour/background layers; the tests only poke `framebuffer`, so syncFb mirrors it first.
struct LinkPixels { bool on = false; int x = 0, y = 0; };
extern LinkPixels g_link;                                       // a white 4x4 "Link" in the colour layer only
struct ExtraPixel { int x, y; uint32_t color; };
extern std::vector<ExtraPixel> g_extra;                         // more sprite pixels in the colour layer only (enemy, projectile)
void syncFb(Nes& nes);

extern int gScrollX, gScrollY;
extern std::vector<uint32_t> gBg;   // when not empty: the background-only layer syncFb gives the PPU (a picture without its sprites); else it equals the framebuffer
struct Ctx {
    std::unique_ptr<Nes> nes = std::make_unique<Nes>();
    std::string err;
    Widescreen ws;                                              // the "old" instance: HUD overlay and cave box off
    OverworldMap map;
    uint8_t decoded[OverworldMap::TILES] = {};                  // our decode of layout 0 (every test room uses it)
    Canvas c;                                                   // the last canvas rendered by frames()
    double aspect = 512.0 / 240.0;                              // -> 512 wide with viewHeight 176; status bar/room start at x = 128

    bool setup();                                               // fake ROM, palettes, rooms; false (and a message) if it fails
    void tile(int id, uint8_t lo, uint8_t hi);                  // CHR tile with the given bit planes in every row
    // Game state: Link stands at the start position (X=120, Y=141) unless a test says otherwise.
    void setState(int mode, int room, int level = 0);
    uint32_t px(int x, int y) const { return c.pixels[(size_t)y * c.w + x]; }
    void frames(Widescreen& w2, int n, double asp);             // n frames: syncFb + snapCamera + render into c
    static bool isFog(uint32_t p) { return p != 0xFF000000 && ((p >> 16) & 0xFF) < 0x40 && ((p >> 8) & 0xFF) < 0x40 && (p & 0xFF) < 0x40; }
};

// Gives a section function the names it was written with (nes, ws, c, px, frames, setState, ...).
#define SECTION_BINDINGS \
    [[maybe_unused]] auto& nes = t.nes; [[maybe_unused]] auto& err = t.err; [[maybe_unused]] auto& ws = t.ws; \
    [[maybe_unused]] auto& map = t.map; [[maybe_unused]] auto& decoded = t.decoded; [[maybe_unused]] auto& c = t.c; \
    [[maybe_unused]] const double aspect = t.aspect; \
    [[maybe_unused]] auto tile = [&](int id, uint8_t lo, uint8_t hi) { t.tile(id, lo, hi); }; \
    [[maybe_unused]] auto setState = [&](int mode, int room, int level = 0) { t.setState(mode, room, level); }; \
    [[maybe_unused]] auto px = [&](int x, int y) { return t.px(x, y); }; \
    [[maybe_unused]] auto frames = [&](Widescreen& w2, int n, double asp) { t.frames(w2, n, asp); }; \
    [[maybe_unused]] auto isFog = [](uint32_t p) { return Ctx::isFog(p); };

// The sections, in the order they run (see test_wide.cpp). Files: test_wide_world / _caves / _hud / _sprites.cpp.
void section01(Ctx&); void section02(Ctx&); void section03(Ctx&); void section04(Ctx&); void section05(Ctx&);
void section06(Ctx&); void section07(Ctx&); void section08(Ctx&); void section09(Ctx&); void section10(Ctx&);
void section11(Ctx&); void section12(Ctx&); void section13(Ctx&); void section14(Ctx&); void section15(Ctx&);
void section16(Ctx&); void section17(Ctx&); void section18(Ctx&); void section19(Ctx&); void section20(Ctx&); void section21(Ctx&); void section22(Ctx&); void section23(Ctx&); void section24(Ctx&); void section32(Ctx&); void section35(Ctx&); void section25(Ctx&); void section26(Ctx&); void section27(Ctx&); void section28(Ctx&); void section29(Ctx&); void section30(Ctx&); void section31(Ctx&);
