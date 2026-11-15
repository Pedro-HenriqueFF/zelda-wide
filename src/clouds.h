// Cloud shadows drifting over the overworld. The clouds live in world coordinates (the 16x8 screens wrap around like a
// torus). Each cloud is a random tree of up to seven shapes, each shape a smooth random closed loop (a circle whose radius is
// modulated by a few harmonics, stretched a little); the union is rasterised once into an 8-bit mask with a flat interior and a
// short soft edge. A cloud keeps its heading for a while, then turns to a new one.
#pragma once
#include <cstdint>
#include <vector>

class CloudField {
public:
    static constexpr int kClouds = 40, kMaxShapes = 7;
    static constexpr int kWorldW = 16 * 256, kWorldH = 8 * 176;
    static constexpr float kEdge = 11.0f;                        // width of the soft edge, in world pixels
    struct Shape { float cx, cy, radius, sx, sy; float amp[4], phase[4]; };   // the outline: r(a) = radius * (1 + sum amp[k] cos((k+2) a + phase[k]))
    struct Cloud {
        float x = 0, y = 0, heading = 0, target = 0, speed = 0;  // position of the mask's origin, heading in radians
        int timer = 0;                                           // frames until the next change of direction
        int nShapes = 0; Shape shape[kMaxShapes];
        int bx = 0, by = 0, bw = 0, bh = 0;                      // the mask covers [bx, bx + bw) x [by, by + bh) around (x, y)
        std::vector<uint8_t> mask;                               // rasterised on first use
        bool fillAll = false;                                    // (test hook) the whole world is under this cloud
    };
    int maxDark = 51;                                            // how much darker a shaded pixel gets, of 256 (51 = 20%)

    void init(uint32_t seed = 0x2545F491u);
    void step();                                                 // one game frame
    const Cloud& cloud(int i) const { return c_[i]; }
    Cloud& cloud(int i) { return c_[i]; }
    int strengthAt(float wx, float wy);                          // 0..255 (255 = inside, a short ramp at the edge)
    float coverage(int step = 16);                               // share of the map under a cloud (0..1)
    // Darkens the canvas rows [topBand, h): canvas (x, y) shows world (camX + x, camY + y - topBand).
    void shade(std::vector<uint32_t>& pixels, int w, int h, int topBand, int camX, int camY);
private:
    uint32_t rnd();
    float rnd01() { return (rnd() >> 8) / 16777216.0f; }
    void newCourse(Cloud& c);
    void build(Cloud& c);                                        // random shapes and the mask's extent
    void raster(Cloud& c);
    Cloud c_[kClouds];
    uint32_t s_ = 1;
    std::vector<uint8_t> mask_;
};
