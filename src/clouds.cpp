#include "clouds.h"
#include <algorithm>
#include <cmath>

uint32_t CloudField::rnd() { s_ ^= s_ << 13; s_ ^= s_ >> 17; s_ ^= s_ << 5; return s_; }

void CloudField::newCourse(Cloud& c) {
    const float pi = 3.14159265f;
    c.target = c.heading + (rnd01() * 2 - 1) * pi * 0.9f;       // a clearly different direction (up to ~160 degrees off)
    c.timer = 360 + (int)(rnd01() * 840);                        // keeps going for 6 to 20 seconds
}

void CloudField::build(Cloud& c) {
    const float tau = 6.2831853f;
    const float scale = 0.8f + rnd01() * 0.45f;
    c.nShapes = 2 + (int)(rnd() % (kMaxShapes - 1));             // 2..7 shapes
    for (int i = 0; i < c.nShapes; i++) {
        Shape& s = c.shape[i];
        s.radius = (38 + rnd01() * 38) * scale;
        s.sx = 0.7f + rnd01() * 0.6f; s.sy = 0.7f + rnd01() * 0.6f;
        float total = 0;
        for (int k = 0; k < 4; k++) { s.amp[k] = rnd01() * 0.34f / (float)(k + 1); total += s.amp[k]; s.phase[k] = rnd01() * tau; }
        if (total > 0.5f) for (float& a : s.amp) a *= 0.5f / total;   // stays a smooth, star-shaped loop (never pinches)
        if (i == 0) { s.cx = 0; s.cy = 0; continue; }
        // The new shape grows out of a random earlier one (a tree, not a chain).
        const Shape& p = c.shape[rnd() % i];
        const float a = rnd01() * tau, d = (p.radius + s.radius) * (0.5f + rnd01() * 0.3f);
        s.cx = p.cx + std::cos(a) * d; s.cy = p.cy + std::sin(a) * d;
    }
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < c.nShapes; i++) {
        const Shape& s = c.shape[i];
        const float ex = s.radius * 1.5f * s.sx + 2, ey = s.radius * 1.5f * s.sy + 2;
        x0 = std::min(x0, s.cx - ex); x1 = std::max(x1, s.cx + ex); y0 = std::min(y0, s.cy - ey); y1 = std::max(y1, s.cy + ey);
    }
    c.bx = (int)std::floor(x0); c.by = (int)std::floor(y0); c.bw = (int)std::ceil(x1) - c.bx; c.bh = (int)std::ceil(y1) - c.by;
    c.mask.clear();
}

void CloudField::raster(Cloud& c) {
    c.mask.assign((size_t)c.bw * c.bh, 0);
    constexpr int N = 256;
    for (int i = 0; i < c.nShapes; i++) {
        const Shape& s = c.shape[i];
        float rad[N];                                            // outline radius by angle
        for (int j = 0; j < N; j++) {
            const float a = (j + 0.5f) * 6.2831853f / N; float f = 1;
            for (int k = 0; k < 4; k++) f += s.amp[k] * std::cos((k + 2) * a + s.phase[k]);
            rad[j] = s.radius * f;
        }
        const float ex = s.radius * 1.5f * s.sx + 2, ey = s.radius * 1.5f * s.sy + 2, sc = std::min(s.sx, s.sy);
        const int x0 = std::max(c.bx, (int)std::floor(s.cx - ex)), x1 = std::min(c.bx + c.bw - 1, (int)std::ceil(s.cx + ex));
        const int y0 = std::max(c.by, (int)std::floor(s.cy - ey)), y1 = std::min(c.by + c.bh - 1, (int)std::ceil(s.cy + ey));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                const float dx = (x + 0.5f - s.cx) / s.sx, dy = (y + 0.5f - s.cy) / s.sy;
                const float rho = std::sqrt(dx * dx + dy * dy);
                if (rho > s.radius * 1.5f) continue;
                float a = std::atan2(dy, dx); if (a < 0) a += 6.2831853f;
                const float depth = (rad[std::min(N - 1, (int)(a * (N / 6.2831853f)))] - rho) * sc;   // pixels inside the outline
                const int v = (int)std::clamp(depth / kEdge * 255.0f, 0.0f, 255.0f);
                uint8_t& m = c.mask[(size_t)(y - c.by) * c.bw + (x - c.bx)];
                if (v > m) m = (uint8_t)v;
            }
    }
}

void CloudField::init(uint32_t seed) {
    s_ = seed ? seed : 1;
    for (int i = 0; i < 4; i++) rnd();
    for (Cloud& c : c_) {
        c = Cloud();
        c.x = rnd01() * kWorldW; c.y = rnd01() * kWorldH;
        c.heading = rnd01() * 6.2831853f; c.target = c.heading;
        c.speed = 0.125f + rnd01() * 0.175f;                     // 7.5..18 pixels per second
        newCourse(c);
        build(c);
    }
}

void CloudField::step() {
    for (Cloud& c : c_) {
        if (--c.timer <= 0) newCourse(c);
        const float d = c.target - c.heading;
        c.heading += std::clamp(d, -0.012f, 0.012f);             // turns slowly
        c.x += std::cos(c.heading) * c.speed; c.y += std::sin(c.heading) * c.speed;
        c.x = std::fmod(c.x + kWorldW, (float)kWorldW); c.y = std::fmod(c.y + kWorldH, (float)kWorldH);
    }
}

int CloudField::strengthAt(float wx, float wy) {
    int best = 0;
    for (Cloud& c : c_) {
        if (c.fillAll) return 255;
        for (int ox = -1; ox <= 1; ox++)
            for (int oy = -1; oy <= 1; oy++) {
                const int lx = (int)std::floor(wx - (c.x + ox * kWorldW)) - c.bx, ly = (int)std::floor(wy - (c.y + oy * kWorldH)) - c.by;
                if (lx < 0 || ly < 0 || lx >= c.bw || ly >= c.bh) continue;
                if (c.mask.empty()) raster(c);
                best = std::max(best, (int)c.mask[(size_t)ly * c.bw + lx]);
            }
    }
    return best;
}

float CloudField::coverage(int step) {
    int in = 0, all = 0;
    for (int y = step / 2; y < kWorldH; y += step)
        for (int x = step / 2; x < kWorldW; x += step) { all++; in += strengthAt((float)x, (float)y) >= 128; }
    return all ? (float)in / all : 0.f;
}

void CloudField::shade(std::vector<uint32_t>& pixels, int w, int h, int topBand, int camX, int camY) {
    const int rows = h - topBand;
    if (rows <= 0 || w <= 0) return;
    if (mask_.size() != (size_t)w * rows) mask_.assign((size_t)w * rows, 0);   // (kept all zero between frames: only the rows touched are cleared again)
    int r0 = rows, r1 = -1;                                      // rows that have any cloud in them
    for (Cloud& c : c_) {
        if (c.fillAll) { std::fill(mask_.begin(), mask_.end(), 255); r0 = 0; r1 = rows - 1; break; }
        for (int ox = -1; ox <= 1; ox++)
            for (int oy = -1; oy <= 1; oy++) {
                const int px = (int)std::lround(c.x) + ox * kWorldW + c.bx - camX, py = (int)std::lround(c.y) + oy * kWorldH + c.by - camY;   // mask origin on the canvas
                const int x0 = std::max(0, px), x1 = std::min(w, px + c.bw), y0 = std::max(0, py), y1 = std::min(rows, py + c.bh);
                if (x0 >= x1 || y0 >= y1) continue;
                if (c.mask.empty()) raster(c);
                for (int y = y0; y < y1; y++) {
                    const uint8_t* src = &c.mask[(size_t)(y - py) * c.bw + (x0 - px)];
                    uint8_t* dst = &mask_[(size_t)y * w + x0];
                    for (int x = x0; x < x1; x++, src++, dst++) if (*src > *dst) *dst = *src;
                }
                r0 = std::min(r0, y0); r1 = std::max(r1, y1 - 1);
            }
    }
    for (int y = r0; y <= r1; y++) {
        uint8_t* m = &mask_[(size_t)y * w];
        uint32_t* row = &pixels[(size_t)(topBand + y) * w];
        for (int x = 0; x < w; x++) {
            if (!m[x]) continue;
            const int f = 256 - maxDark * m[x] / 255;           // the flat shade inside, a short ramp at the edge (a little less darkening for blue: cool)
            const uint32_t p = row[x];
            const int r = (p >> 16 & 255) * f >> 8, g = (p >> 8 & 255) * f >> 8, b = (int)(p & 255) * std::min(256, f + 10) >> 8;
            row[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            m[x] = 0;
        }
    }
}
