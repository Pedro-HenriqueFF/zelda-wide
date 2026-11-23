// Effects added on top of the world view: cloud shadows (shapes, transitions, caves, glide), the death-scene fade, walkable sand for the
// fog, and a randomized stress run over everything. Runs after section 24 on the same shared Ctx (see test_wide.cpp).
#include <memory>
#include <cstdio>
#include <algorithm>
#include "test_wide_common.h"
#include "../src/parallel.h"
#include "../src/widescreen_internal.h"

void section25(Ctx& t) {
    SECTION_BINDINGS
    // 25a0. The cloud shading works in bands of rows on several threads: the same picture as with one thread, and the clouds do darken (a wide canvas, clouds spread over it)
    {
        const int W = 1500, H = 700, band = 40;
        CloudField c1, c8; c1.init(11); c8.init(11);
        for (int i = 0; i < 300; i++) { c1.step(); c8.step(); }
        auto make = [&]() { std::vector<uint32_t> p((size_t)W * H); for (size_t i = 0; i < p.size(); i++) p[i] = 0xFF000000u | (uint32_t)((i * 2654435761u) >> 8 & 0x00FFFFFFu) | 0x00404040u; return p; };
        std::vector<uint32_t> before = make(), a = before, b = before; const int saved = par::maxThreads();
        par::maxThreads() = 1; c1.shade(a, W, H, band, 700, 300);
        par::maxThreads() = 8; c8.shade(b, W, H, band, 700, 300); par::maxThreads() = saved;
        CHECK(a == b);                                                                        // (the same with 1 and with 8 threads)
        size_t darker = 0, brighter = 0; for (size_t i = 0; i < a.size(); i++) { const int pa = (a[i] >> 8 & 255), pb = (before[i] >> 8 & 255); darker += pa < pb; brighter += pa > pb; }
        CHECK(darker > 1000); CHECK_EQ(brighter, (size_t)0);
        CHECK(std::equal(a.begin(), a.begin() + (size_t)W * band, before.begin()));           // (not the status bar)
    }
    // 25a. Cloud shadows: they drift, keep a heading for a while then turn, wrap around the world, and darken the finished picture
    //      (sprites included) but never brighten it and never touch the status bar.
    {
        CloudField a, b; a.init(7); b.init(7);
        CHECK_EQ(a.cloud(2).x, b.cloud(2).x);                                                 // deterministic
        const float x0 = a.cloud(0).x, y0 = a.cloud(0).y, h0 = a.cloud(0).heading;
        for (int i = 0; i < 100; i++) a.step();
        CHECK(a.cloud(0).x != x0 || a.cloud(0).y != y0);                                      // it moves
        CHECK_EQ(a.cloud(0).heading, h0);                                                     // ... along the same heading for a while
        int turned = 0; float prev = a.cloud(0).heading;
        for (int i = 0; i < 4000; i++) { a.step(); if (a.cloud(0).heading != prev) turned++; prev = a.cloud(0).heading; }
        CHECK(turned > 20);                                                                   // ... then it changes direction (smoothly)
        bool inside = true;
        for (int i = 0; i < CloudField::kClouds; i++) inside = inside && a.cloud(i).x >= 0 && a.cloud(i).x < CloudField::kWorldW && a.cloud(i).y >= 0 && a.cloud(i).y < CloudField::kWorldH;
        CHECK(inside);
        const auto& c0 = b.cloud(0);
        CHECK_EQ(b.strengthAt(c0.x + c0.shape[0].cx, c0.y + c0.shape[0].cy), 255);           // solid in the heart
        CHECK_EQ(b.strengthAt(c0.x + c0.shape[0].cx + CloudField::kWorldW, c0.y + c0.shape[0].cy), 255);   // wraps

        auto sh_h = std::make_unique<Widescreen>(); Widescreen& sh = *sh_h; sh.init(*nes, err); sh.unfogAnim = false; sh.viewHeight = 176; sh.hudOverlay = false; sh.caveBox = false; sh.fog = false;
        auto pl_h = std::make_unique<Widescreen>(); Widescreen& pl = *pl_h; pl.init(*nes, err); pl.unfogAnim = false; pl.cloudShadow = false; pl.lighting = false; pl.viewHeight = 176; pl.hudOverlay = false; pl.caveBox = false; pl.fog = false;
        setState(5, 0x23); frames(sh, 2, aspect);                                             // (creates the clouds)
        for (int i = 0; i < CloudField::kClouds; i++) { sh.clouds.cloud(i).x = 10; sh.clouds.cloud(i).y = 10; }   // all far from the view ...
        auto& cl = sh.clouds.cloud(0);
        cl.x = (0x23 & 15) * 256 + 128; cl.y = (0x23 >> 4) * 176 + 88; cl.speed = 0;
        cl.fillAll = true;                                                                   // ... except one cloud over the screen
        frames(sh, 2, aspect);
        const std::vector<uint32_t> shaded = c.pixels; const int cw = c.w, chh = c.h;
        frames(pl, 2, aspect);
        CHECK_EQ(cw, c.w); CHECK_EQ(chh, c.h);
        bool never = true, hud = true; int darker = 0;
        for (int y = 0; y < chh; y++)
            for (int x = 0; x < cw; x++) {
                const uint32_t p = shaded[(size_t)y * cw + x], q = c.pixels[(size_t)y * cw + x];
                for (int sft : {16, 8, 0}) never = never && ((p >> sft & 255) <= (q >> sft & 255) + 0);
                if (y < HUD_H && p != q) hud = false;
                if (p != q) darker++;
            }
        CHECK(never); CHECK(hud); CHECK(darker > 1000);
        // Link is in the shadow too: the pixel under his sprite is darker than without clouds.
        const int lx = cw / 2, ly = chh / 2;
        CHECK((shaded[(size_t)ly * cw + lx] & 0xFFFFFF) == 0 || shaded[(size_t)ly * cw + lx] != c.pixels[(size_t)ly * cw + lx] || (c.pixels[(size_t)ly * cw + lx] & 0xFFFFFF) == 0);
    }

    // 25b. The death fade only touches the bottom half of the background palette: colours of the top half (a white gravestone) fade
    //      along with it, anything else stays as the game draws it.
    {
        auto d_h = std::make_unique<Widescreen>(); Widescreen& d = *d_h; d.init(*nes, err); d.unfogAnim = false; d.cloudShadow = false; d.lighting = false; d.viewHeight = 176; d.hudOverlay = false; d.caveBox = false; d.fog = false;
        const uint32_t white = Ppu::nesColor(0x30);
        auto count = [&](uint32_t col) { int n = 0; for (int y = HUD_H; y < c.h; y++) for (int x = 0; x < c.w; x++) n += c.pixels[(size_t)y * c.w + x] == col; return n; };
        for (int i = 0; i < 8; i++) nes->ppu.palette[i] = (i & 3) ? 0x30 : 0x0F;               // top half: white
        for (int i = 8; i < 16; i++) nes->ppu.palette[i] = (i & 3) ? 0x16 : 0x0F;
        setState(5, 0x23); frames(d, 3, aspect);
        const int before = count(white);
        setState(0x11, 0x23); nes->ram[0x13] = 8; frames(d, 1, aspect);                          // (the frame before still counts as normal play)
        for (int i = 8; i < 16; i++) nes->ppu.palette[i] = 0x0F;                                 // the game has faded the bottom half to black
        frames(d, 2, aspect);
        CHECK(before > 0);
        CHECK_EQ(count(white), 0);
        nes->ram[0x13] = 0xB; frames(d, 2, aspect);                                              // still faded until the GAME OVER text takes over
        CHECK_EQ(count(white), 0);
        {   // Link's own pixels (OAM 18/19) keep the game's colours: no square around him
            g_link.on = true; g_link.x = 120; g_link.y = 120;
            for (int y = 120; y < 124; y++) for (int x = 120; x < 124; x++) nes->ppu.sprOam[y * Ppu::SPR_W + x] = 18;
            for (int i = 8; i < 16; i++) nes->ppu.palette[i] = (i & 3) ? 0x16 : 0x0F;
            setState(5, 0x23); frames(d, 2, aspect);
            setState(0x11, 0x23); nes->ram[0x13] = 8; frames(d, 1, aspect);
            for (int i = 8; i < 16; i++) nes->ppu.palette[i] = 0x0F;
            frames(d, 2, aspect);
            const int withLink = count(white);
            CHECK(withLink > 0 && withLink <= 16);
            g_link.on = false; for (int y = 120; y < 124; y++) for (int x = 120; x < 124; x++) nes->ppu.sprOam[y * Ppu::SPR_W + x] = 0xFF;
        }
        setState(5, 0x23); frames(d, 2, aspect);
        CHECK(count(white) > 0);                                                              // normal play is unchanged
    }

    // 25c. Cloud shapes: flat shade (20% darker) with a short soft edge, 40 clouds covering ~1/5 of the map, trees of 2..7 smooth random loops.
    {
        CloudField f; f.init(3);
        {   // one cloud on a uniform canvas: a flat interior, a short ramp, never brighter than the input
            std::vector<uint32_t> px(700 * 700, 0xFF808080u); CloudField g; g.init(3);
            for (int i = 1; i < CloudField::kClouds; i++) { g.cloud(i).x = 5000; g.cloud(i).y = 5; }
            g.cloud(0).x = 350 - g.cloud(0).bx - g.cloud(0).bw / 2.0f; g.cloud(0).y = 350 - g.cloud(0).by - g.cloud(0).bh / 2.0f;
            g.shade(px, 700, 700, 0, 0, 0);
            int none = 0, full = 0, mid = 0; bool never = true;
            for (uint32_t v : px) { const int b = v >> 8 & 255; never = never && b <= 0x80; if (b == 0x80) none++; else if (b == 0x80 * (256 - g.maxDark) / 256) full++; else mid++; }
            CHECK(never); CHECK(full > 2000); CHECK(mid > 0 && mid < full);                         // a gradient about 11 px wide, smaller than the flat part
        }
        CHECK_EQ(CloudField::kClouds, 40);
        float lo = 1, hi = 0;
        for (uint32_t seed : {1u, 7u, 99u, 12345u}) { CloudField g; g.init(seed); const float cv = g.coverage(32); lo = std::min(lo, cv); hi = std::max(hi, cv); }
        CHECK(lo > 0.15f && hi < 0.27f);
        int wide = 0, tall = 0, minN = 99, maxN = 0; bool smooth = true, slow = true;
        for (int i = 0; i < CloudField::kClouds; i++) {
            const auto& c = f.cloud(i);
            const float asp = (float)c.bw / c.bh; wide += asp > 1.3f; tall += asp < 0.85f;
            minN = std::min(minN, c.nShapes); maxN = std::max(maxN, c.nShapes);
            for (int k = 0; k < c.nShapes; k++) { float t = 0; for (float a : c.shape[k].amp) t += a; smooth = smooth && t <= 0.5001f && t > 0.01f; }
            slow = slow && c.speed >= 0.12f && c.speed <= 0.31f;
        }
        CHECK(wide >= 4 && tall >= 2);                                                        // different proportions
        CHECK(minN >= 2 && maxN <= 7 && maxN > minN + 2);                                     // a tree of 2..7 shapes
        CHECK(smooth); CHECK(slow);                                                           // outlines are smooth loops, drifting slowly
    }

    // 25d. The clouds stay in every overworld transition (scroll, entering a room, curtain, death, caves, stairs), never brighten.
    {
        struct St { int mode, sub, upd, level; const char* name; };
        const St states[] = {{7, 3, 1, 0, "scroll"}, {4, 0, 1, 0, "enter room"}, {3, 4, 1, 0, "curtain"}, {0x11, 8, 1, 0, "death fade"}, {0x10, 2, 1, 0, "stairs"}, {6, 0, 1, 0, "leave room"}};
        auto sh_h = std::make_unique<Widescreen>(); Widescreen& sh = *sh_h; sh.init(*nes, err); sh.unfogAnim = false; sh.viewHeight = 176; sh.hudOverlay = false; sh.caveBox = false; sh.fog = false;
        auto pl_h = std::make_unique<Widescreen>(); Widescreen& pl = *pl_h; pl.init(*nes, err); pl.unfogAnim = false; pl.cloudShadow = false; pl.lighting = false; pl.viewHeight = 176; pl.hudOverlay = false; pl.caveBox = false; pl.fog = false;
        for (const St& st : states) {
            auto go = [&](Widescreen& w) { setState(5, 0x23); frames(w, 3, aspect); setState(st.mode, 0x23, st.level); nes->ram[0x13] = st.sub; nes->ram[0x11] = st.upd; frames(w, 3, aspect); };
            go(sh);
            for (int i = 0; i < CloudField::kClouds; i++) { sh.clouds.cloud(i).x = 10; sh.clouds.cloud(i).y = 10; sh.clouds.cloud(i).speed = 0; }
            sh.clouds.cloud(0).fillAll = true;
            sh.clouds.cloud(0).x = 2048; sh.clouds.cloud(0).y = 704;
            frames(sh, 1, aspect);
            const std::vector<uint32_t> shaded = c.pixels; const int cw = c.w;
            go(pl); frames(pl, 1, aspect);
            int dark = 0; bool never = c.w == cw;
            for (size_t i = 0; never && i < shaded.size(); i++) {
                for (int sft : {16, 8, 0}) never = never && ((shaded[i] >> sft & 255) <= (c.pixels[i] >> sft & 255));
                dark += shaded[i] != c.pixels[i];
            }
            if (dark <= 1000 || !never) std::printf("cloud state failed: %s (dark %d)\n", st.name, dark);
            CHECK(never); CHECK(dark > 1000);
        }
    }

    // 25e. In a cave the backdrop behind the box is shaded too, and the clouds keep moving over it.
    {
        auto sh_h = std::make_unique<Widescreen>(); Widescreen& sh = *sh_h; sh.init(*nes, err); sh.unfogAnim = false; sh.viewHeight = 176; sh.hudOverlay = false; sh.fog = false;
        auto pl_h = std::make_unique<Widescreen>(); Widescreen& pl = *pl_h; pl.init(*nes, err); pl.unfogAnim = false; pl.cloudShadow = false; pl.lighting = false; pl.viewHeight = 176; pl.hudOverlay = false; pl.fog = false;
        auto go = [&](Widescreen& w) { setState(5, 0x23); frames(w, 4, aspect); setState(0xB, 0x23); nes->ram[0x13] = 7; frames(w, 3, aspect); };
        go(sh);
        for (int i = 0; i < CloudField::kClouds; i++) { sh.clouds.cloud(i).x = 10; sh.clouds.cloud(i).y = 10; sh.clouds.cloud(i).speed = 0; }
        sh.clouds.cloud(0).fillAll = true;
        sh.clouds.cloud(0).x = 2048; sh.clouds.cloud(0).y = 704;
        frames(sh, 1, aspect);
        const std::vector<uint32_t> shaded = c.pixels; const int cw = c.w;
        go(pl); frames(pl, 1, aspect);
        int dark = 0; for (size_t i = 0; i < shaded.size() && cw == c.w; i++) dark += shaded[i] != c.pixels[i];
        CHECK(dark > 1000);
        const float before = sh.clouds.cloud(1).x; sh.clouds.cloud(1).speed = 0.5f; sh.clouds.cloud(1).heading = 0; sh.clouds.cloud(1).target = 0; sh.clouds.cloud(1).timer = 9999;
        frames(sh, 5, aspect);
        CHECK(sh.clouds.cloud(1).x > before + 1.0f);                                          // still drifting while the box is up
    }

    // 25f. Sand ($84-$88: the game blocks tiles from $89 on) is walkable for the fog, like plain ground; a lone water tile and a tree are not.
    {
        uint8_t t[32 * 22]; bool m[32 * 22];
        for (auto& v : t) v = 0x24;
        t[5 * 22 + 3] = 0x84; t[6 * 22 + 3] = 0x86; t[7 * 22 + 3] = 0x88; t[9 * 22 + 3] = 0xC4;
        for (int c = 20; c < 24; c++) for (int r = 5; r < 8; r++) t[c * 22 + r] = 0x89;   // water next to ground: a shore
        owWalkMask(t, m);
        CHECK(m[5 * 22 + 3] && m[6 * 22 + 3] && m[7 * 22 + 3] && m[10 * 22 + 10]);
        CHECK(!m[9 * 22 + 3]);                                                                 // a tree blocks
        uint8_t u[32 * 22]; bool n[32 * 22];
        for (auto& v : u) v = 0xB0;
        for (int c = 4; c < 8; c++) for (int r = 4; r < 7; r++) u[c * 22 + r] = 0x89;
        owWalkMask(u, n);
        CHECK(!n[5 * 22 + 5]);                                                                 // an enclosed lake ($89) stays blocked
        for (auto& v : u) v = 0xB0;
        u[5 * 22 + 5] = 0x88;
        owWalkMask(u, n);
        CHECK(n[5 * 22 + 5]);                                                                  // ... but $88 is sand, never blocked
    }

    // 25g. The glide into the next screen (moving between overworld areas) keeps the clouds, too.
    {
        auto sh_h = std::make_unique<Widescreen>(); Widescreen& sh = *sh_h; sh.init(*nes, err); sh.unfogAnim = false; sh.viewHeight = 176; sh.hudOverlay = false; sh.caveBox = false; sh.fog = false;
        auto pl_h = std::make_unique<Widescreen>(); Widescreen& pl = *pl_h; pl.init(*nes, err); pl.unfogAnim = false; pl.cloudShadow = false; pl.lighting = false; pl.viewHeight = 176; pl.hudOverlay = false; pl.caveBox = false; pl.fog = false;
        auto go = [&](Widescreen& w) { setState(5, 0x23); frames(w, 4, aspect); nes->ram[0x70] = 150; w.startGlide(*nes); };
        go(sh);
        CHECK(sh.gliding());
        for (int i = 0; i < CloudField::kClouds; i++) { sh.clouds.cloud(i).x = 10; sh.clouds.cloud(i).y = 10; sh.clouds.cloud(i).speed = 0; }
        sh.clouds.cloud(0).fillAll = true;
        frames(sh, 1, aspect);
        const std::vector<uint32_t> shaded = c.pixels; const int cw = c.w;
        go(pl); CHECK(pl.gliding()); frames(pl, 1, aspect);
        int dark = 0; bool never = cw == c.w;
        for (size_t i = 0; never && i < shaded.size(); i++) { never = never && (shaded[i] >> 8 & 255) <= (c.pixels[i] >> 8 & 255); dark += shaded[i] != c.pixels[i]; }
        CHECK(never); CHECK(dark > 1000);
    }


    // 25h. Stress: random game states, rooms, palettes, window shapes and zoom levels with every feature on (the sanitizers
    //      catch out-of-range reads and writes); the canvas must always be consistent.
    {
        auto w_h = std::make_unique<Widescreen>(); Widescreen& w = *w_h; w.init(*nes, err);
        uint32_t rs = std::getenv("ZW_SEED") ? (uint32_t)std::atoi(std::getenv("ZW_SEED")) : 12345;          // (ZW_SEED / ZW_ITER: other seeds and longer runs in the review)
        auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; };
        const double aspects[] = {0.4, 1.0, 1.333, 1.78, 2.4, 3.6, 6.0};
        bool sane = true; int frames_ = 0;
        const int iters = std::getenv("ZW_ITER") ? std::atoi(std::getenv("ZW_ITER")) : 350;
        for (int it = 0; it < iters; it++) {
            const int mode = rnd() % 0x14, room = rnd() % 256, level = (rnd() % 4 == 0) ? rnd() % 10 : 0;
            setState(mode, room, level);
            nes->ram[0x13] = rnd() % 16; nes->ram[0x11] = rnd() & 1; nes->ram[0xE1] = rnd() % 11; nes->ram[0x5A] = rnd() % 3;
            for (int i = 0; i < 13; i++) { nes->ram[0x70 + i] = rnd(); nes->ram[0x84 + i] = rnd(); nes->ram[0x98 + i] = 1 << (rnd() % 4); nes->ram[0xAC + i] = rnd() % 0x20; }
            for (int i = 0; i < 32; i++) nes->ppu.palette[i] = rnd() % 4 ? rnd() % 0x40 : rnd();
            for (int i = 0; i < 64; i++) nes->ppu.sprCol[(rnd() % Ppu::SPR_W) * 1 + (rnd() % Ppu::H) * Ppu::SPR_W] = rnd() % 4 ? 0xFF : rnd() % 4, nes->ppu.sprOam[rnd() % (Ppu::SPR_W * Ppu::H)] = rnd() % 70;
            if (rnd() % 5 == 0) nes->cpuWrite(0x6AFE + rnd() % 128, rnd());                 // attribute / flag junk
            if (rnd() % 5 == 0) nes->cpuWrite(0x69FE + rnd() % 128, rnd());
            if (rnd() % 7 == 0) nes->cpuWrite(0x6530 + rnd() % 704, rnd());
            w.viewHeight = 60 + rnd() % 900; w.hudOverlay = rnd() & 1; w.caveBox = rnd() & 1; w.fog = rnd() % 4 != 0; w.cloudShadow = rnd() % 4 != 0;
            if (rnd() % 6 == 0) w.startGlide(*nes);
            const int n = 1 + rnd() % 3;
            for (int k = 0; k < n; k++) {
                frames(w, 1, aspects[rnd() % 7]); frames_++;
                sane = sane && c.w >= 256 && c.w <= 2048 && c.h >= 1 && c.pixels.size() == (size_t)c.w * c.h;
            }
        }
        CHECK(sane); CHECK(frames_ > 350);
    }

    // 25i. Stress: dungeon transitions with frozen sprites, lights and water: play frames in a dungeon room (random moat tiles, random sprites incl. Link's, random objects),
    //      then a random transition (6, 7 with a random slide, stairs, cellar in / out) with the sprites gone and random picture brightness (the sanitizers watch).
    {
        auto w_h = std::make_unique<Widescreen>(); Widescreen& w = *w_h; w.init(*nes, err); w.unfogAnim = false; w.cloudShadow = false;
        uint32_t rs = std::getenv("ZW_SEED") ? (uint32_t)std::atoi(std::getenv("ZW_SEED")) + 7 : 777;
        auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; };
        const double aspects[] = {0.6, 1.0, 1.78, 3.0};
        const int iters = std::getenv("ZW_ITER") ? std::atoi(std::getenv("ZW_ITER")) / 7 : 50;
        const int trans[] = {6, 7, 0x10, 9, 0x0A, 4, 0x11};
        bool sane = true;
        std::vector<uint8_t> ramSave(nes->ram, nes->ram + 0x800), tilesSave, fbSave(sizeof(nes->ppu.palette));   // (the later sections need the fixture back)
        for (int i = 0; i < 704; i++) tilesSave.push_back(nes->cart.cpuRead((uint16_t)(0x6530 + i)));
        std::memcpy(fbSave.data(), nes->ppu.palette, sizeof(nes->ppu.palette));
        std::vector<uint32_t> fbPix(nes->ppu.framebuffer, nes->ppu.framebuffer + (size_t)Ppu::W * Ppu::H);
        std::vector<uint8_t> sprColSave(nes->ppu.sprCol, nes->ppu.sprCol + (size_t)Ppu::SPR_W * Ppu::SPR_H), sprOamSave(nes->ppu.sprOam, nes->ppu.sprOam + (size_t)Ppu::SPR_W * Ppu::SPR_H);
        std::vector<uint8_t> dropColSave(nes->ppu.dropCol, nes->ppu.dropCol + (size_t)Ppu::SPR_W * Ppu::SPR_H), dropOamSave(nes->ppu.dropOam, nes->ppu.dropOam + (size_t)Ppu::SPR_W * Ppu::SPR_H), oamSave(nes->ppu.oam, nes->ppu.oam + 256);
        for (int it = 0; it < iters; it++) {
            w.setWater(rnd() % 5 != 0); w.lighting = rnd() % 5 != 0; w.smoothScroll = rnd() & 1;
            std::memset(nes->ppu.dropCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.dropOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);   // the sprites the PPU left out (junk OAM numbers up to $FF too)
            for (int i = 0; i < 120; i++) { const int p = (int)(rnd() % (Ppu::SPR_W * Ppu::H)); nes->ppu.dropCol[p] = rnd() % 64; nes->ppu.dropOam[p] = rnd() % 3 ? rnd() % 64 : rnd(); }
            for (int i = 0; i < 256; i++) nes->ppu.oam[i] = (uint8_t)rnd();
            const int room = rnd() % 128, level = 1 + rnd() % 9;
            for (int i = 0; i < 704; i++) nes->cpuWrite((uint16_t)(0x6530 + i), rnd() % 3 ? 0x24 : 0xF4);
            for (int i = 0; i < 13; i++) { nes->ram[0x70 + i] = rnd(); nes->ram[0x84 + i] = rnd(); nes->ram[0x34F + i] = rnd() % 4 ? 0 : rnd() % 0x70; nes->ram[0xAC + i] = rnd() % 0x30; }
            nes->ram[0x34F + 0x13] = rnd() % 2 ? 0x60 : 0;
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H); std::memset(nes->ppu.sprOam, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            for (int i = 0; i < 300; i++) { const int p = (int)(rnd() % (Ppu::SPR_W * Ppu::H)); nes->ppu.sprCol[p] = rnd() % 64; nes->ppu.sprOam[p] = rnd() % 4 ? rnd() % 70 : 18 + rnd() % 2; }
            gScrollX = gScrollY = 0;
            setState(5, room, level); frames(w, 3, aspects[rnd() % 4]);
            std::memset(nes->ppu.sprCol, 0xFF, Ppu::SPR_W * Ppu::SPR_H);
            const int mode = trans[rnd() % 7];
            setState(mode, (room + 1) & 127, level); nes->ram[0x11] = rnd() & 1;
            const int steps = 2 + rnd() % 30, dir = rnd() % 4;
            for (int k = 0; k < steps; k++) {
                if (mode == 7) { (dir < 2 ? gScrollX : gScrollY) += (dir & 1 ? -1 : 1) * (int)(rnd() % 12); }
                const uint32_t shade = rnd() % 3 == 0 ? 0xFF000000u : 0xFF000000u | (rnd() % 256) * 0x010101u;
                if (rnd() % 2) for (int i = 0; i < Ppu::W * Ppu::H; i++) nes->ppu.framebuffer[i] = (rnd() % 5 == 0) ? 0xFF000000u : shade;
                frames(w, 1, aspects[rnd() % 4]);
                sane = sane && c.w >= 256 && c.pixels.size() == (size_t)c.w * c.h;
            }
        }
        std::memcpy(nes->ram, ramSave.data(), 0x800);
        for (int i = 0; i < 704; i++) nes->cpuWrite((uint16_t)(0x6530 + i), tilesSave[(size_t)i]);
        std::memcpy(nes->ppu.palette, fbSave.data(), sizeof(nes->ppu.palette));
        std::copy(fbPix.begin(), fbPix.end(), nes->ppu.framebuffer);
        std::copy(sprColSave.begin(), sprColSave.end(), nes->ppu.sprCol); std::copy(sprOamSave.begin(), sprOamSave.end(), nes->ppu.sprOam);
        std::copy(dropColSave.begin(), dropColSave.end(), nes->ppu.dropCol); std::copy(dropOamSave.begin(), dropOamSave.end(), nes->ppu.dropOam); std::copy(oamSave.begin(), oamSave.end(), nes->ppu.oam);
        gScrollX = gScrollY = 0;
        CHECK(sane);
    }

    // 25i. The mask buffer is reused between frames and canvas sizes: a field that shaded other canvases first gives the same result
    //      as a fresh one.
    {
        CloudField a, b; a.init(11); b.init(11);
        auto canvas = [](int w, int h) { return std::vector<uint32_t>((size_t)w * h, 0xFF9080A0u); };
        for (auto sz : {std::pair<int, int>{900, 500}, {500, 300}, {700, 640}}) { auto big = canvas(sz.first, sz.second); a.shade(big, sz.first, sz.second, 64, 1500, 400); }
        a.cloud(0).x = 3000; a.cloud(0).y = 600; b.cloud(0).x = 3000; b.cloud(0).y = 600;
        auto p1 = canvas(600, 400), p2 = canvas(600, 400);
        for (int i = 0; i < CloudField::kClouds; i++) { a.cloud(i).x = b.cloud(i).x = 3000.f + i * 20; a.cloud(i).y = b.cloud(i).y = 600.f + i * 7; }
        a.shade(p1, 600, 400, 64, 3000, 600); b.shade(p2, 600, 400, 64, 3000, 600);
        CHECK(p1 == p2);
        CHECK(std::count(p1.begin(), p1.end(), 0xFF9080A0u) < (long)p1.size());           // (and it did shade something)
    }
}
