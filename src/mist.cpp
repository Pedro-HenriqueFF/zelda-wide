#include "mist.h"
#include "widescreen_internal.h"
#include <algorithm>
#include <cmath>

using namespace wd;

namespace {
uint32_t rnd(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
float rf(uint32_t& s) { return (rnd(s) & 0xFFFF) / 65535.0f; }
float smooth(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }
}  // namespace

void Mist::init() {
    static_assert((kTex & (kTex - 1)) == 0 && kTex % 32 == 0, "the mist texture wraps with a mask and its octaves divide it evenly");
    uint32_t s = 0x1F2E3D4Cu;
    // A tileable cloud texture: four octaves of value noise on a wrapping lattice.
    tex.assign((size_t)kTex * kTex, 0);
    std::vector<float> acc((size_t)kTex * kTex, 0.0f);
    float amp = 1.0f, total = 0.0f;
    for (int oct = 0, cells = 4; oct < 4; oct++, cells *= 2, amp *= 0.55f) {
        std::vector<float> lat((size_t)cells * cells);
        for (float& v : lat) v = rf(s);
        const int step = kTex / cells;
        for (int y = 0; y < kTex; y++)
            for (int x = 0; x < kTex; x++) {
                const int gx = x / step, gy = y / step;
                const float fx = smooth((x % step) / (float)step), fy = smooth((y % step) / (float)step);
                auto L = [&](int a, int b) { return lat[(size_t)(b % cells) * cells + (a % cells)]; };
                const float v = (L(gx, gy) * (1 - fx) + L(gx + 1, gy) * fx) * (1 - fy) + (L(gx, gy + 1) * (1 - fx) + L(gx + 1, gy + 1) * fx) * fy;
                acc[(size_t)y * kTex + x] += v * amp;
            }
        total += amp;
    }
    float lo = 1e9f, hi = -1e9f;
    for (float& v : acc) { v /= total; lo = std::min(lo, v); hi = std::max(hi, v); }
    for (size_t i = 0; i < acc.size(); i++) tex[i] = (uint8_t)std::clamp((acc[i] - lo) / (hi - lo) * 255.0f, 0.0f, 255.0f);
    // The patches: clusters of three round lobes (so no two look alike), sizes from small to very large, each with its own start and speed.
    patches.clear();
    for (int i = 0; i < 26; i++) {
        Patch p;
        const float cx = rf(s), cy = rf(s), size = 0.10f + 0.34f * rf(s) * rf(s) + (i % 7 == 0 ? 0.18f : 0.0f);
        for (Lobe& l : p.lobe) { l.x = cx + (rf(s) - 0.5f) * size * 1.6f; l.y = cy + (rf(s) - 0.5f) * size * 1.0f; l.r = size * (0.55f + 0.6f * rf(s)); }
        p.start = 0.6f * rf(s);
        p.dur = 0.35f + 0.4f * rf(s);
        patches.push_back(p);
    }
}

float Mist::patchAlpha(float nx, float ny, float aspect, float p) const {
    float keep = 1.0f;
    for (const Patch& pt : patches) {
        const float q = smooth((p - pt.start) / pt.dur);
        if (q <= 0.0f) continue;
        float v = 0.0f;
        for (const Lobe& l : pt.lobe) {
            const float dx = (nx - l.x) * aspect, dy = ny - l.y, d2 = (dx * dx + dy * dy) / (l.r * l.r);
            if (d2 < 1.0f) v = std::max(v, (1 - d2) * (1 - d2));
        }
        keep *= 1.0f - q * std::min(1.0f, v * 1.8f);
    }
    float a = 1.0f - keep;
    const float g = smooth((p - 0.8f) / 0.2f);                     // (whatever the patches left open fills up at the end)
    return 1.0f - (1.0f - a) * (1.0f - g);
}

void Mist::layer(std::vector<uint32_t>& px, int w, int h, float p, unsigned t) const {
    px.assign((size_t)w * h, 0);
    if (p <= 0.0f || tex.empty() || w < 2 || h < 2) return;
    constexpr int C = 8;
    const int gw = (w + C - 1) / C + 2, gh = (h + C - 1) / C + 2;
    std::vector<float> grid((size_t)gw * gh);
    const float aspect = (float)w / h;
    for (int gy = 0; gy < gh; gy++)
        for (int gx = 0; gx < gw; gx++) grid[(size_t)gy * gw + gx] = patchAlpha(((gx - 0.5f) * C) / w, ((gy - 0.5f) * C) / h, aspect, p);
    const int sc = std::max(2, h / 150);                              // (texture pixels follow the zoom)
    const int ox = (int)(t * 0.35f), oy = (int)(t * 0.12f);
    const uint32_t lightC = 0xFFBEC2CA, darkC = 0xFF767C88;
    for (int y = 0; y < h; y++) {
        const float fy = (y + C * 0.5f) / C; const int gy = (int)fy; const float ty = fy - gy;
        for (int x = 0; x < w; x++) {
            const float fx = (x + C * 0.5f) / C; const int gx = (int)fx; const float tx = fx - gx;
            const float* g0 = &grid[(size_t)gy * gw + gx]; const float* g1 = g0 + gw;
            float a = (g0[0] * (1 - tx) + g0[1] * tx) * (1 - ty) + (g1[0] * (1 - tx) + g1[1] * tx) * ty;
            if (a <= 0.002f) continue;
            const int n = tex[(size_t)(((y / sc + oy) & (kTex - 1)) * kTex + ((x / sc + ox) & (kTex - 1)))];
            a = std::clamp(a + ((n / 255.0f) - 0.5f) * 0.9f * std::min(a, 1.0f - a) * 2.0f, 0.0f, 1.0f);   // wispy edges
            px[(size_t)y * w + x] = ((uint32_t)(a * 255.0f) << 24) | (mixColor(darkC, lightC, n) & 0x00FFFFFF);   // the cloud keeps its texture when it covers everything
        }
    }
}

void Mist::haze(uint32_t* px, int w, int h, int top, unsigned t, int dim256, int amount256) const {
    if (tex.empty()) return;
    std::vector<int> wx((size_t)w), wy((size_t)std::max(0, h));
    for (int x = 0; x < w; x++) wx[x] = (int)(6.0f * std::sin(x * 0.037f - t * 0.021f));
    for (int y = 0; y < h; y++) wy[y] = (int)(6.0f * std::sin(y * 0.041f + t * 0.017f));
    const int sc = std::max(2, (h - top) / 120);
    const int ox = (int)(t * 0.20f), oy = (int)(t * 0.08f);
    const uint32_t hazeC = 0xFF8C929C;
    for (int y = top; y < h; y++) {
        uint32_t* row = px + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            const int n = tex[(size_t)((((y + wx[x]) / sc + oy) & (kTex - 1)) * kTex + (((x + wy[y]) / sc + ox) & (kTex - 1)))];
            const int a = std::clamp(amount256 * (96 + n) / 224, 0, 255);   // the haze thickens and thins as it drifts
            row[x] = mixColor(scaleColor(row[x], dim256), hazeC, a);
        }
    }
}
