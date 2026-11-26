#pragma once
// Shadow games: copies of the running game that walk into the screens NEXT to Link and keep running there, so that what lives on those
// screens (monsters, great fairies, items on the ground) can be shown moving in the margins of the widescreen view. The NES game itself only
// simulates the room Link is in.
//
// A shadow is a `Nes` cloned from the real game while it is in plain overworld play (Nes::cloneFrom, lite PPU). It is spun up with the game's own
// code: the stand-in Link is put at the edge facing the target screen and the game is put into "leave the room" mode 6, exactly the change
// GoToNextModeFromPlay makes (Z_07.asm 3036); the game then runs its modes 6, 7 (scroll), 4 (load the screen, spawn the monsters) and 5 by itself.
// A diagonal neighbour takes two such hops. Then the shadow settles for a while (the monsters appear) and is stepped in lockstep with the real game.
// The stand-in Link takes no input, his hearts are refilled every frame, and the random number generator is reseeded so that the shadows do not
// move in lockstep with the real game.
#include "nes.h"
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

class ShadowGame {
public:
    enum class State { Idle, SpinUp, Settling, Ready, Failed };
    static constexpr int kHopTimeout = 900;       // frames a hop may take (a scroll is ~64 frames, loading ~60 more)
    static constexpr int kMaxAttempts = 3, kRetryDelay = 120;
    static constexpr int kSettleFrames = 120;     // frames after arriving (monsters poof in, then roam)
    static constexpr int kWanderX0 = 0x28, kWanderX1 = 0xD0, kWanderY0 = 0x6D, kWanderY1 = 0xBD;   // where the stand-in Link strolls (ObjX / ObjY)
    static constexpr float kWanderSpeed = 1.5f, kItemClear = 14.0f;
    static constexpr uint8_t kPondEdgeY = 0xAD;     // the Y at which a Link at the Great Fairy's pond starts her heart animation (X $70-$80)
    static constexpr uint8_t kDockX = 0x60, kDockXRoom55 = 0x80;   // the X at which a Link on a dock starts the raft (rooms $3F / $55)
    static constexpr uint8_t kChaseKeep = 7;       // ChaseLongTimer is kept up so that the game never switches the monsters' target
    static constexpr uint8_t kUnkillable = 0xFF;  // HeartValues / HeartPartial of the stand-in Link: full, many hearts (a hit can never kill him)

    ShadowGame() = default;                       // (the Nes is made when the shadow starts: a disabled feature costs no memory)

    // The target screen after hops: its room id, or -1 when it is off the map, or the way leads over or into a maze room (the game repeats those on purpose).
    static int targetRoom(int fromRoom, int dx, int dy);
    // Clones `real` (must be in plain overworld play) and plans the walk to the screen dx, dy (each -1..1, not both 0) away. False: nothing started.
    bool start(const Nes& real, int dx, int dy, uint32_t seed);
    void stepFrame();                             // one frame of the shadow (spin-up, keeping the stand-in Link neutral)
    State state() const { return st; }
    bool ready() const { return st == State::Ready; }
    bool visible() const { return st == State::Settling || st == State::Ready; }   // (drawn: from the moment it has arrived on its screen, so the monsters' spawn smoke is seen)
    int  room() const { return target; }          // the screen this shadow lives on
    int  frames() const { return frameCount; }
    int  attemptsMade() const { return attempts; }
    const char* lastFailure() const { return lastFail; }      // why the shadow failed the last time (for shadow_debug.txt)
    void followRealLink(const Nes& real);          // its stand-in Link stops strolling and stands where the real Link does (the hand-off)
    Nes& nes() { return *game; }
    const Nes& nes() const { return *game; }
    void aimLink(int x, int y) { aimOn = true; aimX = (uint8_t)x; aimY = (uint8_t)y; }   // the next frame the stand-in Link stands here (a returning boomerang flies to him)
    int boomWait = 0;                             // relay calls a boomerang has hovered at the edge of the screen waiting for the next one to take it
    uint8_t tile(int col, int row) const;         // the screen's tile map in this shadow's own game (an armos that woke left floor or stairs here)
    bool retry(const Nes& real, uint32_t seed);   // a failed shadow starts again (at most kMaxAttempts times for one set of shadows)
    int  failedFor() const { return failedFrames; }
    bool hasNes() const { return game != nullptr; }
    void reset() { st = State::Idle; target = -1; }
    // Hand-off: Link has walked into this shadow's screen and the real game has just loaded it with fresh monsters: the shadow's monsters (slots 1-12 and their variables)
    // replace them, so what was seen in the margin goes on instead of popping. Only when the real game's monsters are kinds the shadow has too. False: nothing copied.
    enum class HandOff { Done, No };               // Done: the shadow's creatures were copied into the game; No: there is no shadow of the screen
    HandOff handOffTo(Nes& real);
    // The screen Link is leaving (the game has just entered its leave-the-room mode 6, the monsters are as they were): a shadow that carries on from exactly that state.
    bool captureFrom(const Nes& real, int room, uint32_t seed);
    void adopt(const Nes& src, int room);         // a ready shadow that is a plain copy of `src` standing on `room` (tests of the drawing)

private:
    std::unique_ptr<Nes> game;
    State st = State::Idle;
    int target = -1, hopIdx = 0, hopCount = 0, hopFrames = 0, settle = 0, frameCount = 0;
    bool hopIssued = false;
    int attempts = 0, failedFrames = 0;
    bool aimOn = false; uint8_t aimX = 0, aimY = 0;
    uint8_t hopDir[2] = {0, 0}, hopRoom[2] = {0, 0};
    uint8_t hearts = 0, heartPart = 0;
    void issueHop();
    void reseed(uint32_t seed);                    // the dice and the stand-in Link's own random numbers: not the real game's
    void clearLeaveState();                        // what GoToNextModeFromPlay clears besides the mode
    void placeLinkNeutral();
    void containScenery();                        // undoes a raft ride / Great Fairy heart animation the stand-in Link has set off
    void wanderLink();
    float lx = 0, ly = 0, wx = 0, wy = 0;   // the stand-in Link's place and where he strolls to
    bool wanders = false; uint32_t wrng = 1;
    const char* lastFail = nullptr;
};

// The shadows around the real game's room (up to 8 neighbours; the screen Link has just left is one of them).
class ShadowSet {
public:
    static constexpr int kSpinStepsMax = 16, kSpinBudget = 24, kStartsPerUpdate = 3;   // shadows still spinning up run up to kSpinStepsMax frames per real frame (kSpinBudget frames per thread shared by all of them); at most kStartsPerUpdate new shadows are made per real frame
    static int spinSteps(int spinners, int threads) { return threads <= 1 || spinners <= 0 ? kSpinStepsPerFrame : std::clamp(kSpinBudget * threads / spinners, (int)kSpinStepsPerFrame, kSpinStepsMax); }
    static constexpr int kSpinStepsPerFrame = 3;  // a shadow that is still spinning up runs this many frames per real frame (it appears sooner)
    static constexpr int kRelayPatience = 240;     // relay calls a boomerang waits at an edge for the next screen to take it
    static constexpr int kRelayBand = 14;          // px from a screen's edge where a returning boomerang is handed to the next screen
    static constexpr int kKeepRadius = 2;          // shadows up to this many screens from Link go on (the 8 around him are started, the others only kept)
    static constexpr int kDropDelay = 2;           // updates the shadows stay after Link is inside a dungeon (the last pictures of the walk in)
    static constexpr int kBridgeHold = 4;         // updates the entered screen's shadow stays after the hand-off; it is drawn for the first pictures of play too (they are a frame or more behind the game and have no monsters yet)
    bool enabled = false;
    // Called once per real frame after it ran. Keeps the shadows going: new neighbours are made when the real game is in plain overworld play in a new room
    // (shadows that are still neighbours go on as they are, the others are dropped), the screen Link is leaving is captured as a shadow, the hand-off after a
    // scroll is carried out; all shadows are stepped (in parallel). Dropped in dungeons.
    void update(Nes& real);
    void animate(Nes* real = nullptr);                               // one frame for every shadow while the real game is paused (the glide between screens): the monsters keep moving
    void clear();
    const std::vector<ShadowGame*>& games() const { return active; }
    int baseRoom() const { return base; }
    // The scroll is over and the game is about to load the screen (mode 4 clears its object RAM and makes the screen's monsters a few frames later):
    // update() hands the monsters of the shadow of that screen over as soon as the game has made its own (or at the latest in mode 5). Until the picture shows
    // the real monsters again the shadow (the `bridge`) is drawn over it: the game does not draw its monsters while Link walks in (mode 4).
    void armHandOff(const Nes& real);
    bool handOffPending() const { return handOffArmed; }
    const ShadowGame* bridgeGame() const { return bridge; }
    const ShadowGame* visibleShadow(int room) const { for (const ShadowGame* g : active) if (g->room() == room && g->visible()) return g; return nullptr; }
    int carryTiles(Nes& real);                    // armos that woke in the shadow's game: the real game's statues on those squares become what the shadow has (floor, stairs)
    bool hasVisible(int room) const { for (const ShadowGame* g : active) if (g->room() == room && g->visible()) return true; return false; }   // a shadow of this screen is there to be drawn
    // The first picture of plain play after a walk in has no monsters yet either (the game draws them one frame later): the bridge is still drawn for it.
    bool bridgeShowsInPlay() const { return bridge && bridgeHold > 0 && bridgeHold < kBridgeHold; }
    ShadowGame::HandOff tryHandOff(Nes& real);
    bool handOff(Nes& real) { return tryHandOff(real) == ShadowGame::HandOff::Done; }
    bool dump(const char* path) const;            // shadow_debug.txt: per shadow its state and the monsters/items in its object slots
    std::string summary() const;                  // one line for --trace
    static constexpr int kPool = 48;              // the screens on a fully zoomed-out canvas (7 x 5 = 35) and the ones kept two screens from Link, with a spare for the capture of the screen he leaves
    ShadowGame pool[kPool];
    int captures = 0, captureFailures = 0; const char* lastCaptureFailure = nullptr;   // (debugging: shadow_debug.txt)
    int forceSpinSteps = 0;                       // (tests) a fixed number of frames per real frame for spinning shadows, whatever the thread count
    uint8_t wanted[128] = {};                     // the screens on the canvas (set by the compositor every picture): they get a shadow, whatever the zoom
    void setWanted(const uint8_t* mask) { std::memcpy(wanted, mask, sizeof wanted); }
    std::vector<ShadowGame*> active;
    int base = -1;
    bool handOffArmed = false, departed = false, wasOverworldPlay = false;
    int dropDelay = -1;
    int lastMode = -1;
    int bridgeHold = 0;
    ShadowGame* bridge = nullptr;
private:
    void reconcile(Nes& real);
    void topUp(Nes& real);
    void relayBoomerangs(Nes& real);   // (also in the glide: the shadows' boomerangs must not be caught by a stand-in Link while the real game is not yet playing)
    ShadowGame* capture(const Nes& real, int room);
    void dropBridge();
    void retire(int room);
    void stepAll();
    ShadowGame* alloc();
    void pruneIdle();                              // drops the shadows that went Idle from `active`
};
