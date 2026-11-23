#include "lighting.h"
#include "parallel.h"
#include <algorithm>
#include <cmath>

namespace lighting {

static float smooth(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }

float daylightAt(double p) {
    if (p < 0.50) return 1.0f;
    if (p < 0.62) return 1.0f - smooth((float)((p - 0.50) / 0.12));
    if (p < 0.88) return 0.0f;
    return smooth((float)((p - 0.88) / 0.12));
}

float twilightAt(double p) {
    const double a = 16.0 / 24.0 - 0.25;                    // 16:00 (phase 0 = 6:00)
    if (p < a) return 0.0f;
    if (p < 0.50) return smooth((float)((p - a) / (0.50 - a)));
    if (p < 0.62) return daylightAt(p);
    return 0.0f;
}

void Clock::step() {
    ++tick;
    if (!frozen) { phase += 1.0 / kDayFrames; if (phase >= 1.0) phase -= 1.0; }
    rng = rng * 1664525u + 1013904223u;
    if (bolt >= 0) { if (++bolt > 40) bolt = -1; }
    else if (--nextBolt <= 0) {
        nextBolt = 1500 + (int)((rng >> 8) % 3600);        // 25 to 85 seconds apart
        if (daylight() < 0.3f) bolt = 0;                  // (only after dark)
    }
}

float Clock::lightning() const {
    if (bolt < 0) return 0.0f;
    if (bolt < 4) return 0.9f;
    if (bolt < 9) return 0.0f;
    if (bolt < 14) return 1.0f - (bolt - 9) * 0.04f;
    return std::max(0.0f, 0.8f - (bolt - 14) * 0.03f);
}

float flicker(unsigned tick, int salt) {
    const float t = (float)tick;
    return 1.0f + 0.05f * std::sin(t * 0.37f + salt * 1.7f) + 0.05f * std::sin(t * 0.91f + salt * 4.1f);
}

void apply(uint32_t* px, int w, int h, int top, int bottom, int camX, int camY, const Params& p, const std::vector<Source>& srcAll) {
    bool anyAdd = false;
    for (const Source& s : srcAll) anyAdd = anyAdd || s.add > 0;
    if (p.ambient >= 0.999f && p.warm <= 0.001f && !anyAdd) return;    // (full ambient light: a light can only brighten up to the picture's own colours)
    bottom = std::min(bottom, h);
    const int rowA = std::max(top, 0);
    if (bottom <= rowA || w <= 0) return;
    // A light that reaches no pixel of the canvas changes nothing: only the others are looked at (the same sources in the same order, so the same sums).
    const float wyMin = (float)(camY + rowA - top), wyMax = (float)(camY + bottom - 1 - top);
    std::vector<Source> src; src.reserve(srcAll.size());
    for (const Source& s : srcAll) {
        if (s.power <= 0 || s.y - s.radius >= wyMax || s.y + s.radius <= wyMin) continue;
        if (std::max(0, (int)std::floor(s.x - s.radius - camX)) > std::min(w - 1, (int)std::ceil(s.x + s.radius - camX))) continue;
        src.push_back(s);
    }
    // Full daylight (every factor exactly 1): the picture is untouched except where a glow with `add` (the armos halos) is screened over it, so only those pixels are visited.
    if (!p.slow && p.ambient >= 1.0f && p.warm <= 0.0f && p.nightTint <= 0.0f) {
        std::vector<const Source*> adds;
        for (const Source& s : src) if (s.add > 0) adds.push_back(&s);
        if (adds.empty()) return;
        float yLo = 1e30f, yHi = -1e30f;
        for (const Source* s : adds) { yLo = std::min(yLo, s->y - s->radius); yHi = std::max(yHi, s->y + s->radius); }
        const int ya0 = std::max(rowA, (int)std::floor(yLo - camY + top) - 1), ya1 = std::min(bottom, (int)std::ceil(yHi - camY + top) + 2);
        par::forRange(ya0, ya1, 16, [&](int ya, int yb) {
            static thread_local std::vector<float> ar, ag, ab;
            if ((int)ar.size() < w) { ar.assign(w, 0); ag.assign(w, 0); ab.assign(w, 0); }
            for (int y = ya; y < yb; y++) {
                const float wy = (float)(camY + y - top);
                int xlo = w, xhi = -1;
                for (const Source* sp : adds) {
                    const Source& s = *sp;
                    if (std::fabs(wy - s.y) >= s.radius) continue;
                    const float ddy = wy - s.y, r2 = s.radius * s.radius;
                    const int x0 = std::max(0, (int)std::floor(s.x - s.radius - camX)), x1 = std::min(w - 1, (int)std::ceil(s.x + s.radius - camX));
                    const float cr = s.r / 255.0f, cg = s.g / 255.0f, cb = s.b / 255.0f;
                    for (int x = x0; x <= x1; x++) {
                        const float ddx = (float)(camX + x) - s.x, d2 = ddx * ddx + ddy * ddy;
                        if (d2 >= r2) continue;
                        const float f = 1.0f - std::sqrt(d2) / s.radius, ka = s.add * f * f;
                        ar[x] += ka * cr; ag[x] += ka * cg; ab[x] += ka * cb;
                    }
                    xlo = std::min(xlo, x0); xhi = std::max(xhi, x1);
                }
                if (xhi < xlo) continue;
                uint32_t* row = px + (size_t)y * w;
                for (int x = xlo; x <= xhi; x++) {
                    const bool lit = ar[x] > 0 || ag[x] > 0 || ab[x] > 0;
                    if (lit && !(p.mask && p.mask[(size_t)y * w + x] != p.maskValue)) {
                        const uint32_t c = row[x];
                        int r = (int)(c >> 16 & 255), g = (int)(c >> 8 & 255), b = (int)(c & 255);
                        r = 255 - (int)((255 - r) * (1.0f - std::min(ar[x], 1.0f))); g = 255 - (int)((255 - g) * (1.0f - std::min(ag[x], 1.0f))); b = 255 - (int)((255 - b) * (1.0f - std::min(ab[x], 1.0f)));
                        row[x] = (c & 0xFF000000u) | (uint32_t)(r << 16) | (uint32_t)(g << 8) | (uint32_t)b;
                    }
                    ar[x] = ag[x] = ab[x] = 0;
                }
            }
        });
        return;
    }
    const float tr = (1.0f - 0.30f * p.nightTint) * (1.0f - 0.04f * p.warm), tg = (1.0f - 0.14f * p.nightTint) * (1.0f - 0.14f * p.warm), tb = 1.0f - 0.34f * p.warm;
    const float vo = p.visionOuter, vi = p.visionInner;
    par::forRange(rowA, bottom, 16, [&](int ya, int yb) {                                            // (rows do not depend on each other; the scratch rows belong to the thread)
    static thread_local std::vector<float> gr, gg, gb, ar, ag, ab;
    if ((int)gr.size() < w) { gr.resize(w); gg.resize(w); gb.resize(w); ar.resize(w); ag.resize(w); ab.resize(w); }
    for (int y = ya; y < yb; y++) {
        const float wy = (float)(camY + y - top);
        const float dy = wy - p.vy;
        for (int x = 0; x < w; x++) {
            float vis = 0;
            if (p.vision && std::fabs(dy) < vo) {
                const float dx = (float)(camX + x) - p.vx;
                const float d2 = dx * dx + dy * dy;
                if (d2 < vo * vo) { const float d = std::sqrt(d2); vis = d <= vi ? 1.0f : smooth(1.0f - (d - vi) / (vo - vi)); }
            }
            const float L = p.ambient + (1.0f - p.ambient) * vis;
            gr[x] = L * tr; gg[x] = L * tg; gb[x] = L * tb; ar[x] = ag[x] = ab[x] = 0;
        }
        for (const Source& s : src) {
            if (std::fabs(wy - s.y) >= s.radius) continue;
            const float ddy = wy - s.y, r2 = s.radius * s.radius;
            const int x0 = std::max(0, (int)std::floor(s.x - s.radius - camX)), x1 = std::min(w - 1, (int)std::ceil(s.x + s.radius - camX));
            const float cr = s.r / 255.0f, cg = s.g / 255.0f, cb = s.b / 255.0f;
            for (int x = x0; x <= x1; x++) {
                const float ddx = (float)(camX + x) - s.x, d2 = ddx * ddx + ddy * ddy;
                if (d2 >= r2) continue;
                const float f = 1.0f - std::sqrt(d2) / s.radius, k = s.power * f * f;
                gr[x] += k * cr; gg[x] += k * cg; gb[x] += k * cb;
                if (s.add > 0) { const float ka = s.add * f * f; ar[x] += ka * cr; ag[x] += ka * cg; ab[x] += ka * cb; }
            }
        }
        uint32_t* row = px + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            if (p.mask && p.mask[(size_t)y * w + x] != p.maskValue) continue;
            const uint32_t c = row[x];
            int r = (int)((c >> 16 & 255) * std::min(gr[x], 1.0f)), g = (int)((c >> 8 & 255) * std::min(gg[x], 1.0f)), b = (int)((c & 255) * std::min(gb[x], 1.0f));
            if (ar[x] > 0 || ag[x] > 0 || ab[x] > 0) {                                        // screen blend: 1 - (1 - c)(1 - a)
                r = 255 - (int)((255 - r) * (1.0f - std::min(ar[x], 1.0f))); g = 255 - (int)((255 - g) * (1.0f - std::min(ag[x], 1.0f))); b = 255 - (int)((255 - b) * (1.0f - std::min(ab[x], 1.0f)));
            }
            row[x] = (c & 0xFF000000u) | (uint32_t)(r << 16) | (uint32_t)(g << 8) | (uint32_t)b;
        }
    }
    });
}

}  // namespace lighting
