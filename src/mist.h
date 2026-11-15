// The mist of the Lost Woods / Lost Hills: gray cloud patches of different sizes and shapes that grow over the picture one after the other
// (each with its own start and speed) until it is covered, and the waving haze lying over the maze.
#pragma once
#include <cstdint>
#include <vector>

class Mist {
public:
    void init();
    // A layer (0xAARRGGBB, straight alpha) of w x h pixels for the whole window: patches grow over it with the progress p (0 clear .. 1 covered);
    // t = a running frame count (the clouds drift).
    void layer(std::vector<uint32_t>& px, int w, int h, float p, unsigned t) const;
    // The maze's haze: dims the picture (dim256 = 256 keeps it) and lays a gray, slowly waving haze over it (amount256 = its strength).
    void haze(uint32_t* px, int w, int h, int top, unsigned t, int dim256, int amount256) const;
    // Share of coverage (0..1) of the patches at one normalised place (nx across, ny down, aspect = w / h) when the progress is p.
    float patchAlpha(float nx, float ny, float aspect, float p) const;
private:
    static constexpr int kTex = 128;
    struct Lobe { float x, y, r; };
    struct Patch { Lobe lobe[3]; float start, dur; };
    std::vector<uint8_t> tex;
    std::vector<Patch> patches;
};
