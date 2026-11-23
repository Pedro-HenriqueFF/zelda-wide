// Optional timing of the frame's stages (`--perf`): where does the time go on this machine? Costs nothing when off.
#pragma once
#include <chrono>

namespace perf {

enum Stage { kEmu, kRender, kWorld, kWaterA, kWaterB, kClouds, kLight, kUpload, kPresent, kWait, kPrep, kRoom, kBackdrop, kRoomWater, kShadowStep, kShadowDraw, kShadowLight, kStages };
inline const char* const kNames[kStages] = {"emu", "render", "world", "waterA", "waterB", "clouds", "light", "upload", "present", "wait", "prep", "room", "backdrop", "roomWater", "shadowStep", "shadowDraw", "shadowLight"};
inline bool enabled = false;
inline double total[kStages] = {};      // ms since the last report
inline int frames = 0;
inline int shadowPasses = 0;            // shadow screens whose objects went through the water pass since the last report

using Clock = std::chrono::steady_clock;
inline Clock::time_point now() { return enabled ? Clock::now() : Clock::time_point(); }
inline void add(Stage s, Clock::time_point t0) { if (enabled) total[s] += std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

struct Scope {                           // times what is left of the block it is declared in
    Stage s; Clock::time_point t0;
    explicit Scope(Stage st) : s(st), t0(now()) {}
    ~Scope() { add(s, t0); }
};

}  // namespace perf
