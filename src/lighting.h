#pragma once
// Day/night clock and the lighting pass: the finished picture is multiplied per pixel by a gain made of the ambient light,
// Link's own sight (a bright disc that fades out) and any number of coloured point lights; a light's `add` also screen-blends a glow
// on top (visible even in daylight).
#include <cstdint>
#include <vector>

namespace lighting {

struct Source {
    float x = 0, y = 0;          // position in the world space of the canvas mapping (see apply)
    float radius = 40;           // reach in pixels
    float power = 1;             // 0..1.5: strength at the centre
    float add = 0;               // 0..1: extra glow added on top of the picture even in full daylight (screen blend)
    uint8_t r = 255, g = 255, b = 255;
};
// (aggregate order: x, y, radius, power, add, r, g, b)

struct Params {
    float ambient = 1;           // brightness away from every light (1 = untouched)
    float nightTint = 0;         // 0..1: colours lean blue/dark
    float warm = 0;              // 0..1: the orange of the twilight
    bool  vision = false;        // Link sees around him
    float vx = 0, vy = 0;        // where (same space as Source)
    const uint8_t* mask = nullptr; uint8_t maskValue = 0;   // only pixels where mask[y * w + x] == maskValue (one of two rooms side by side)
    float visionInner = 56, visionOuter = 150;   // fully lit inside the inner radius, ambient beyond the outer one
    bool slow = false;                           // (tests) always the general path, never the daylight shortcut
};

constexpr int kDayFrames = 36000;                       // one whole day = 10 minutes at 60 fps
float daylightAt(double phase);                         // 0 (night) .. 1 (day); phase 0 = 6:00, 0.5 = 18:00: full day until 18:00, dusk, night, dawn
float twilightAt(double phase);                         // 0..1: the orange of 16:00-18:00 (full at 18:00), which then fades as the dusk darkens
float flicker(unsigned tick, int salt);                 // 0.9 .. 1.1, never the same twice in a row

class Clock {
public:
    double phase = 0.25;         // position in the day: 0 = morning, 0.25 = noon, 0.5 = dusk starts, 0.75 = deep night
    bool frozen = false;
    unsigned tick = 0;           // frames lived (drives the flicker)
    int bolt = -1, nextBolt = 1800;   // lightning: frames since the flash began (-1 = none), frames until the next one
    unsigned rng = 12345;
    void step();
    double hour() const { double h = 6.0 + phase * 24.0; return h >= 24.0 ? h - 24.0 : h; }   // 0..24, phase 0 = 6:00
    float twilight() const { return twilightAt(phase); }
    float lightning() const;     // 0..1 brightness of the current flash (two quick pulses)
    void skip(double d) { phase += d; phase -= (int)phase; if (phase < 0) phase += 1.0; }
    float daylight() const { return daylightAt(phase); }
};

// Ambient light of the overworld at this daylight (0.17 at the darkest night).
inline float overworldAmbient(float daylight) { return 0.17f + 0.83f * daylight; }

// Multiplies rows [top, bottom) of the canvas. Canvas pixel (x, y) is at (camX + x, camY + y - top) in source space.
// Nothing is done when the ambient light is full and no source is given.
void apply(uint32_t* px, int w, int h, int top, int bottom, int camX, int camY, const Params& p, const std::vector<Source>& src);

}  // namespace lighting
