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

class ShadowGame {
public:
    enum class State { Idle, SpinUp, Settling, Ready, Failed };
    static constexpr int kHopTimeout = 900;       // frames a hop may take (a scroll is ~64 frames, loading ~60 more)
    static constexpr int kMaxAttempts = 3, kRetryDelay = 120;
    static constexpr int kSettleFrames = 120;     // frames after arriving (monsters poof in, then roam)

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
    Nes& nes() { return *game; }
    const Nes& nes() const { return *game; }
    bool retry(const Nes& real, uint32_t seed);   // a failed shadow starts again (at most kMaxAttempts times for one set of shadows)
    int  failedFor() const { return failedFrames; }
    bool hasNes() const { return game != nullptr; }
    void reset() { st = State::Idle; target = -1; }
    // Hand-off: Link has walked into this shadow's screen and the real game has just loaded it with fresh monsters: the shadow's monsters (slots 1-12 and their variables)
    // replace them, so what was seen in the margin goes on instead of popping. Only when the real game's monsters are kinds the shadow has too. False: nothing copied.
    enum class HandOff { Done, Wait, No };        // Done: copied; Wait: the real game has not made its monsters yet; No: nothing to take over (other kinds, not on this screen)
    HandOff handOffTo(Nes& real) const;
    // The screen Link is leaving (the game has just entered its leave-the-room mode 6, the monsters are as they were): a shadow that carries on from exactly that state.
    bool captureFrom(const Nes& real, uint32_t seed);
    void adopt(const Nes& src, int room);         // a ready shadow that is a plain copy of `src` standing on `room` (tests of the drawing)

private:
    std::unique_ptr<Nes> game;
    State st = State::Idle;
    int target = -1, hopIdx = 0, hopCount = 0, hopFrames = 0, settle = 0, frameCount = 0;
    bool hopIssued = false;
    int dxs = 0, dys = 0, attempts = 0, failedFrames = 0;
    uint8_t hopDir[2] = {0, 0}, hopRoom[2] = {0, 0};
    uint8_t hearts = 0, heartPart = 0;
    void issueHop();
    void placeLinkNeutral();
};

// The shadows around the real game's room (up to 8 neighbours; the screen Link has just left is one of them).
class ShadowSet {
public:
    static constexpr int kSpinStepsPerFrame = 3;  // a shadow that is still spinning up runs this many frames per real frame (it appears sooner)
    static constexpr int kBridgeHold = 3;         // updates the entered screen's shadow stays after the hand-off (the picture is one frame behind the game)
    bool enabled = false;
    // Called once per real frame after it ran. Keeps the shadows going: new neighbours are made when the real game is in plain overworld play in a new room
    // (shadows that are still neighbours go on as they are, the others are dropped), the screen Link is leaving is captured as a shadow, the hand-off after a
    // scroll is carried out; all shadows are stepped (in parallel). Dropped in dungeons.
    void update(Nes& real);
    void animate();                               // one frame for every shadow while the real game is paused (the glide between screens): the monsters keep moving
    void clear();
    const std::vector<ShadowGame*>& games() const { return active; }
    int baseRoom() const { return base; }
    // The scroll is over and the game is about to load the screen (mode 4 clears its object RAM and makes the screen's monsters a few frames later):
    // update() hands the monsters of the shadow of that screen over as soon as the game has made its own (or at the latest in mode 5). Until the picture shows
    // the real monsters again the shadow (the `bridge`) is drawn over it: the game does not draw its monsters while Link walks in (mode 4).
    void armHandOff(const Nes& real);
    bool handOffPending() const { return handOffArmed; }
    const ShadowGame* bridgeGame() const { return bridge; }
    ShadowGame::HandOff tryHandOff(Nes& real);
    bool handOff(Nes& real) { return tryHandOff(real) == ShadowGame::HandOff::Done; }
    bool dump(const char* path) const;            // shadow_debug.txt: per shadow its state and the monsters/items in its object slots
    std::string summary() const;                  // one line for --trace
    ShadowGame pool[12];
    std::vector<ShadowGame*> active;
    int base = -1;
    bool handOffArmed = false, departed = false;
    int entryFrames = 0, bridgeHold = 0;
    ShadowGame* bridge = nullptr;
private:
    void reconcile(Nes& real);
    void stepAll();
    ShadowGame* alloc();
};
