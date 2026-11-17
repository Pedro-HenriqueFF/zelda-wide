#include "shadow.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "parallel.h"
#include "zelda_ram.h"
#include <cstdio>
#include <cstring>

using namespace zelda;

int ShadowGame::targetRoom(int fromRoom, int dx, int dy) {
    if (fromRoom < 0 || fromRoom > 0x7F || (!dx && !dy)) return -1;
    const int col = (fromRoom & 15) + dx, row = (fromRoom >> 4) + dy;
    if (col < 0 || col > 15 || row < 0 || row > 7) return -1;
    auto maze = [](int room) { return room == kForestMazeRoom || room == kMountainMazeRoom; };
    if (maze(fromRoom)) return -1;                                    // leaving the Lost Woods / Hills is a puzzle (CheckMazes repeats the room): not walked
    if (dx && dy && maze((fromRoom & 15) + dx + (fromRoom >> 4) * 16)) return -1;   // (nor is the way through one)
    return row * 16 + col;
}

bool ShadowGame::start(const Nes& real, int dx, int dy, uint32_t seed) {
    st = State::Idle; target = -1;
    if (real.ram[kGameMode] != kModePlay || real.ram[kCurLevel] != 0 || real.ram[kMenuState] != 0 || real.ram[kPaused] != 0 || real.cpu.inNmi()) return false;
    const int from = real.ram[kRoomId], t = targetRoom(from, dx, dy);
    if (t < 0) return false;
    if (!game) game = std::make_unique<Nes>();
    game->cloneFrom(real, true);
    game->lagFree = true;                                             // (the game's logic always finishes within its frame: RAM can be changed between frames)
    uint8_t* r = game->ram;
    uint32_t x = seed * 2654435761u + 12345u;                         // reseed the RNG: the shadows must not move like the real game
    bool any = false;
    for (int i = 0; i < kRandomLen; i++) { x = x * 1664525u + 1013904223u; r[kRandom + i] = (uint8_t)(x >> 24); any = any || r[kRandom + i]; }
    if (!any) r[kRandom] = 1;
    hearts = kUnkillable; heartPart = kUnkillable;
    hopCount = 0; hopIdx = 0;
    if (dx) { hopDir[hopCount] = dx > 0 ? 1 : 2; hopRoom[hopCount++] = (uint8_t)((from & 0xF0) + (from & 15) + dx); }
    if (dy) { hopDir[hopCount] = dy > 0 ? 4 : 8; hopRoom[hopCount++] = (uint8_t)t; }
    dxs = dx; dys = dy; attempts = 1; failedFrames = 0;
    target = t; hopIssued = false; hopFrames = 0; settle = 0; frameCount = 0;
    st = State::SpinUp;
    return true;
}

void ShadowGame::adopt(const Nes& src, int room) {
    if (!game) game = std::make_unique<Nes>();
    game->cloneFrom(src, true);
    target = room; st = State::Ready; frameCount = 0;
    game->ppu.lineCtrl[100] = game->ppu.ctrl;
}

bool ShadowGame::retry(const Nes& real, uint32_t seed) {
    if (attempts >= kMaxAttempts) return false;
    const int a = attempts + 1;
    const int oldTarget = target;
    if (!start(real, dxs, dys, seed)) { st = State::Failed; lastFail = "start refused"; target = oldTarget; return false; }
    attempts = a;
    return true;
}

ShadowGame::HandOff ShadowGame::handOffTo(Nes& real) {
    if (!visible() || !game || real.ram[kRoomId] != target || real.ram[kCurLevel] != 0) return HandOff::No;
    int have[256] = {}, want[256] = {};                               // the real game's monsters must be kinds the shadow has too (else the screen is not the same: leave it)
    for (int s = 1; s <= 12; s++) {
        const uint8_t a = game->ram[kObjType + s], b = real.ram[kObjType + s];
        if (a && !(a >= kTypeFirstProjectile && a <= kTypeLastProjectile)) have[a]++;
        if (b) want[b]++;
    }
    int shadowMonsters = 0, realMonsters = 0;                         // (the game has not made the screen's monsters yet: nothing to take over, try again later)
    for (int s = 1; s <= 12; s++) { const uint8_t a = game->ram[kObjType + s], b = real.ram[kObjType + s]; shadowMonsters += a && a < kTypeFirstProjectile; realMonsters += b && b < kTypeFirstProjectile; }
    if (shadowMonsters > 0 && realMonsters == 0) return HandOff::Wait;
    for (int t = 0; t < 256; t++) if (want[t] > have[t]) return carryItems(real) ? HandOff::Done : HandOff::No;     // (other monsters: the game's own stay, but the items on the ground come along)
    for (uint16_t base : kHandOffArrays) for (int s = 1; s <= 12; s++) real.ram[base + s] = game->ram[base + s];
    for (uint16_t a : kHandOffBytes) real.ram[a] = game->ram[a];
    for (int i = 0; i < kRoomObjTemplateLen; i++) real.ram[kRoomObjTemplateType + i] = game->ram[kRoomObjTemplateType + i];
    for (int i = 0; i < kRandomLen; i++) real.ram[kRandom + i] = game->ram[kRandom + i];     // (the same dice and the same Link position: the shadow that stands in for a few more frames and the game then go on the same way)
    followRealLink(real);
    return HandOff::Done;
}

// The items lying on the ground of the shadow's screen (type $60, slots 1-12) are moved into free monster slots of the real game, with all their variables (the despawn
// timer included): an item dropped before Link left can still be picked up when he comes back. False: no item, or no free slot.
bool ShadowGame::carryItems(Nes& real) const {
    bool any = false;
    for (int s = 1; s <= 12; s++) {
        if (game->ram[kObjType + s] != kTypeItem) continue;
        for (int t = 1; t <= 12; t++) {
            if (real.ram[kObjType + t] != 0) continue;
            for (uint16_t base : kHandOffArrays) real.ram[base + t] = game->ram[base + s];
            any = true; break;
        }
    }
    return any;
}

// `room`: the screen being left (for a warp the real game's RoomId may already be the cellar's). The game has only just switched to mode 6 (scroll) or, after the
// walk of mode $10, to the cave / cellar / level loading: the monsters are as they were (their RAM is cleared only when the next room is laid out).
bool ShadowGame::captureFrom(const Nes& real, int room, uint32_t seed) {
    st = State::Idle; target = -1;
    if (real.cpu.inNmi() || room < 0 || room > 0x7F || room == kForestMazeRoom || room == kMountainMazeRoom) return false;
    if (!game) game = std::make_unique<Nes>();
    game->cloneFrom(real, true);
    game->lagFree = true;
    uint8_t* r = game->ram;
    wrng = seed * 747796405u + 2891336453u;
    uint32_t x = seed * 2654435761u + 12345u;
    bool any = false;
    for (int i = 0; i < kRandomLen; i++) { x = x * 1664525u + 1013904223u; r[kRandom + i] = (uint8_t)(x >> 24); any = any || r[kRandom + i]; }
    if (!any) r[kRandom] = 1;
    hearts = kUnkillable; heartPart = kUnkillable;
    r[kGameMode] = kModePlay; r[kGameSubmode] = 0; r[kIsUpdatingMode] = 1;      // back into play (mode 6 had only just begun): the monsters carry on where they were
    r[kCurLevel] = 0; r[kRoomId] = (uint8_t)room;                                // (a warp had already set the level / the cellar's room)
    r[kObjState] = 0; r[kObjShoveDir] = 0; r[kObjShoveDistance] = 0; r[kObjInvincibilityTimer] = 0; r[0x0F] = 0;
    placeLinkNeutral();
    target = room; dxs = dys = 0; attempts = kMaxAttempts; failedFrames = 0;   // (no retry: there is nothing to walk to)
    hopCount = hopIdx = 0; hopIssued = false; settle = 0; frameCount = 0;
    st = State::Ready;
    game->ppu.lineCtrl[100] = game->ppu.ctrl;
    stepFrame();                                                      // (the sprites the game hid in mode 6 / $10 are drawn again: no blink on the first frame)
    return true;
}

void ShadowGame::issueHop() {
    uint8_t* r = game->ram;
    const uint8_t dir = hopDir[hopIdx];
    r[kObjDir] = dir;
    r[kObjX] = dir == 1 ? kEdgeRight : dir == 2 ? kEdgeLeft : kCenterX;
    r[kObjY] = dir == 4 ? kEdgeDown : dir == 8 ? kEdgeUp : kCenterY;
    r[kObjGridOffset] = 0; r[kObjState] = 0; r[kObjShoveDir] = 0; r[kObjShoveDistance] = 0; r[kObjInvincibilityTimer] = 0;
    r[0x0F] = 0;
    r[kGameSubmode] = 0; r[kIsUpdatingMode] = 0; r[kGameMode] = kModeLeave;   // what GoToNextModeFromPlay does (Z_07.asm 3036)
}

void ShadowGame::placeLinkNeutral() {
    game->ram[kObjX] = kCenterX; game->ram[kObjY] = kCenterY; game->ram[kObjGridOffset] = 0;
    lx = wx = kCenterX; ly = wy = kCenterY; wpause = 0; wanders = true;
}

// The stand-in Link strolls about the screen (the monsters of Zelda 1 steer towards the player: with a Link who never moves they all gathered around him). He keeps out of
// reach of the items on the ground (the game lets Link take an item within 8 px, Z_01.asm 4422-4438: a shadow must not pick them up before the real Link comes back).
void ShadowGame::wanderLink() {
    uint8_t* r = game->ram;
    auto rnd = [&]() { wrng = wrng * 1664525u + 1013904223u; return (int)(wrng >> 16); };
    auto safe = [&](float x, float y) {
        for (int s = 1; s <= 0x13; s++)
            if (r[kObjType + s] == kTypeItem && std::fabs(x - r[kObjX + s]) < kItemClear && std::fabs(y + 3 - r[kObjY + s]) < kItemClear) return false;
        return true;
    };
    auto newGoal = [&]() { wx = (float)(kWanderX0 + rnd() % (kWanderX1 - kWanderX0)); wy = (float)(kWanderY0 + rnd() % (kWanderY1 - kWanderY0)); };
    if (wpause > 0) wpause--;
    else {
        const float dx = wx - lx, dy = wy - ly, d = std::sqrt(dx * dx + dy * dy);
        if (d < 1.5f) { wpause = rnd() % kWanderPause; newGoal(); }
        else if (safe(lx + dx / d * kWanderSpeed, ly + dy / d * kWanderSpeed)) { lx += dx / d * kWanderSpeed; ly += dy / d * kWanderSpeed; }
        else newGoal();                                                                        // (an item lies in the way)
    }
    for (int k = 0; k < 12 && !safe(lx, ly); k++) { lx = (float)(kWanderX0 + rnd() % (kWanderX1 - kWanderX0)); ly = (float)(kWanderY0 + rnd() % (kWanderY1 - kWanderY0)); }
    r[kObjX] = (uint8_t)lx; r[kObjY] = (uint8_t)ly; r[kObjGridOffset] = 0;
}

// The shadow stands in for the real game's monsters for a few more frames (the hand-off): its Link stands where the real one does.
void ShadowGame::followRealLink(const Nes& real) {
    wanders = false; game->ram[kObjX] = real.ram[kObjX]; game->ram[kObjY] = real.ram[kObjY];
}

void ShadowGame::stepFrame() {
    if (st == State::Failed) failedFrames++;
    if (st == State::Idle || st == State::Failed || !game) return;
    Nes& n = *game; uint8_t* r = n.ram;
    n.pad1 = 0;
    r[kHeartValues] = hearts; r[kHeartPartial] = heartPart;           // (the stand-in Link cannot die)
    frameCount++;
    if (!n.cpu.inNmi() && st == State::SpinUp) {                      // between frames the game's logic is not running: its RAM may be changed
        const uint8_t mode = r[kGameMode];
        if (!hopIssued) {
            if (mode == kModePlay) { issueHop(); hopIssued = true; hopFrames = 0; }
            else if (++hopFrames > kHopTimeout) { st = State::Failed; lastFail = "timeout before the hop"; }
        } else if (mode == kModePlay) {                               // the game has scrolled and loaded the screen
            if (r[kRoomId] != hopRoom[hopIdx]) { st = State::Failed; lastFail = "wrong screen after a hop"; }
            else {
                hopIdx++; hopIssued = false; hopFrames = 0;
                if (hopIdx >= hopCount) { st = State::Settling; settle = 0; placeLinkNeutral(); }
            }
        } else if (++hopFrames > kHopTimeout) { st = State::Failed; lastFail = "timeout during a hop"; }
    }
    if (st == State::Failed) return;
    if (st == State::Settling || st == State::Ready) r[kUndergroundExitType] = 1;     // (the stand-in Link must not take a cave or stairs under his feet: CheckWarps returns at once, Z_05.asm 7250)
    if (wanders && (st == State::Settling || st == State::Ready) && !n.cpu.inNmi()) wanderLink();
    n.runFrame();
    // Nothing flies in a shadow: the monsters' shots are deleted (a shot spawned this frame is drawn once at most) and the picture's 8x16 flag is kept for the drawing.
    for (int slot = 1; slot <= 12; slot++) if (r[kObjType + slot] >= kTypeFirstProjectile && r[kObjType + slot] <= kTypeLastProjectile) r[kObjType + slot] = 0;
    n.ppu.lineCtrl[100] = n.ppu.ctrl;
    if ((st == State::Settling || st == State::Ready) && (r[kGameMode] != kModePlay || r[kRoomId] != target)) { st = State::Failed; lastFail = r[kGameMode] != kModePlay ? "left play mode" : "left its screen"; }     // (it went somewhere it should not: not drawn any more)
    if (st == State::Ready || st == State::Settling) {                                              // (the picture of the monsters: the stand-in Link is left out)
        n.ppu.oam[zelda::kLinkOam0 * 4] = n.ppu.oam[zelda::kLinkOam1 * 4] = 0xFF;
        n.ppu.unlimitedSprites = true; n.ppu.buildSpriteLayer();
    }
    if (st == State::Settling && ++settle >= kSettleFrames) st = State::Ready;
}

// ---------------------------------------------------------------- the set

void ShadowSet::clear() {
    for (ShadowGame& g : pool) g.reset();
    active.clear(); base = -1; bridge = nullptr; bridgeHold = 0; departed = false; walking = false; wasOverworldPlay = false;   // (an armed hand-off stays armed: it is for the screen the real game is loading)
}

ShadowGame* ShadowSet::alloc() {
    for (ShadowGame& g : pool) if (g.state() == ShadowGame::State::Idle && &g != bridge) return &g;
    return nullptr;
}

void ShadowSet::armHandOff(const Nes& real) {
    handOffArmed = true; entryFrames = 0; bridge = nullptr; bridgeHold = 0;
    if (!enabled) return;
    for (ShadowGame* g : active) if (g->visible() && g->room() == (int)real.ram[kRoomId]) bridge = g;
}

// The real game is in plain play in a room the shadows were not planned for: shadows that are still neighbours go on as they are (nothing is respawned), the
// others are dropped, the missing neighbours are made.
void ShadowSet::reconcile(Nes& real) {
    static const int kOrder[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};      // (the orthogonal neighbours first: one hop)
    const int room = real.ram[kRoomId];
    std::vector<ShadowGame*> keep;
    for (ShadowGame* g : active) {
        const int dx = (g->room() & 15) - (room & 15), dy = (g->room() >> 4) - (room >> 4);
        if (g == bridge) continue;                                    // (the screen Link has entered: its monsters are the real game's now; the shadow only bridges the picture)
        if (g->room() != room && std::abs(dx) <= 1 && std::abs(dy) <= 1 && g->state() != ShadowGame::State::Failed) keep.push_back(g);
        else g->reset();
    }
    active = keep; base = room;
    for (int i = 0; i < 8; i++) {
        const int t = ShadowGame::targetRoom(room, kOrder[i][0], kOrder[i][1]);
        if (t < 0) continue;
        bool have = false; for (ShadowGame* g : active) have = have || g->room() == t;
        if (have) continue;
        ShadowGame* g = alloc();
        if (!g) break;
        if (g->start(real, kOrder[i][0], kOrder[i][1], (uint32_t)(real.cpu.totalCycles >> 4) + (uint32_t)i * 7919u + (uint32_t)room)) active.push_back(g);
    }
}

void ShadowSet::stepAll() {
    const int n = (int)active.size();
    par::forRange(0, n, 1, [&](int a, int b) {
        for (int i = a; i < b; i++) {
            ShadowGame& g = *active[i];
            for (int s = 0; s < kSpinStepsPerFrame; s++) {               // (only the walk to the screen, which nobody sees, is hurried: the arrival and everything after runs at the game's speed)
                const bool spinning = g.state() == ShadowGame::State::SpinUp;
                g.stepFrame();
                if (!spinning || g.state() != ShadowGame::State::SpinUp) break;
            }
        }
    });
}

void ShadowSet::animate() { if (enabled) stepAll(); }

// Modes in which the overworld is gone for good (title, file select, game over / continue, saving, the end): the shadows are dropped.
static bool worldGone(uint8_t mode) { return mode <= kModeFileSelect || mode == kModeContinue || mode == kModeSave || mode >= kModeDeath; }

void ShadowSet::update(Nes& real) {
    if (!enabled) { handOffArmed = false; lastMode = -1; if (!active.empty() || base >= 0) clear(); return; }
    const uint8_t mode = real.ram[kGameMode];
    const int level = real.ram[kCurLevel];
    if (worldGone(mode)) { handOffArmed = false; lastMode = -1; wasOverworldPlay = false; if (!active.empty() || base >= 0) clear(); return; }
    // Inside a dungeon nothing of the overworld shows and the game makes its monsters afresh when Link comes out: the shadows only live through the walk in.
    if (level != 0 && mode != kModeStairs) { handOffArmed = false; lastMode = -1; wasOverworldPlay = false; if (!active.empty() || base >= 0) clear(); return; }
    // The overworld goes on in every other mode: scrolls, caves, stairs, the walk in and out. The shadows are stepped once per frame like the real game would.
    if (mode == kModeLeave && level == 0) {                           // Link is leaving the screen: it carries on as a shadow from exactly this state
        if (!departed) { departed = true; capture(real, real.ram[kRoomId]); }
    } else if (mode != kModeLeave) departed = false;
    if (mode == kModeStairs) { if (wasOverworldPlay && base >= 0) walking = true; }       // a cave, stairs or a dungeon entrance: Link walks in (the real game's monsters still move)
    else if (walking) {                                               // the walk is over: the screen Link stood on carries on from this very state (RoomId may already be the cellar's)
        walking = false; if (level == 0 && base >= 0) capture(real, base);
    }
    if (mode == kModeEnter && lastMode != kModeEnter && !handOffArmed) armHandOff(real);   // (a way back from a cave, stairs or a dungeon: the scroll's own entry is armed by main.cpp)
    lastMode = mode;
    if (bridgeHold > 0 && --bridgeHold == 0 && bridge) { dropBridge(); }
    if (handOffArmed) {                                               // Link has walked into a screen: its shadow's monsters replace the fresh ones once the game has made them
        if (mode == kModeEnter && real.ram[kGameSubmode] == 0) entryFrames++;
        // (the first frame seen in mode 4 submode 0 comes BEFORE InitMode_EnterRoom, which clears $0300-$051F and makes the monsters; the second comes after it)
        if ((mode == kModeEnter && entryFrames >= 2) || mode == kModePlay) {
            const ShadowGame::HandOff r = tryHandOff(real);
            if (r != ShadowGame::HandOff::Wait || mode == kModePlay) { handOffArmed = false; if (r != ShadowGame::HandOff::Done) retire(real.ram[kRoomId]); }
        }
    }
    const bool plain = mode == kModePlay && level == 0 && real.ram[kMenuState] == 0 && real.ram[kPaused] == 0;
    wasOverworldPlay = mode == kModePlay && level == 0;
    if (plain && (int)real.ram[kRoomId] != base) reconcile(real);
    if (mode == kModeLeaveCave && level == 0 && !bridge) for (ShadowGame* g : active) if (g->visible() && g->room() == (int)real.ram[kRoomId]) bridge = g;   // (the overworld comes back without its monsters: the screen's shadow stands in for them)
    if (plain && bridge && bridgeHold == 0) bridgeHold = kBridgeHold;
    if (real.ram[kMenuState] != 0 || real.ram[kPaused] != 0) return;  // the inventory / pause: the game stands still, so do the shadows
    stepAll();
    if (plain)
        for (ShadowGame* g : active)                                   // (a failed shadow gets another go after a pause, a few times)
            if (g->state() == ShadowGame::State::Failed && g->failedFor() >= ShadowGame::kRetryDelay) g->retry(real, (uint32_t)(real.cpu.totalCycles >> 3) + (uint32_t)g->room() * 31u);
}

// The screen being left (`room`) goes on as a shadow; there is never more than one shadow of a screen.
void ShadowSet::capture(const Nes& real, int room) {
    ShadowGame* g = alloc();
    if (!g || !g->captureFrom(real, room, (uint32_t)(real.cpu.totalCycles >> 5) + (uint32_t)room * 131u)) return;
    for (ShadowGame* o : active) if (o != g && o->room() == g->room()) o->reset();
    active.erase(std::remove_if(active.begin(), active.end(), [](ShadowGame* o) { return o->state() == ShadowGame::State::Idle; }), active.end());
    active.push_back(g);
}

// Nothing could be taken over from the shadow of the screen Link is in: it is dropped (two simulations of one screen would run side by side).
void ShadowSet::retire(int room) {
    for (ShadowGame* g : active) if (g->room() == room) { g->reset(); if (g == bridge) bridge = nullptr; }
    active.erase(std::remove_if(active.begin(), active.end(), [](ShadowGame* o) { return o->state() == ShadowGame::State::Idle; }), active.end());
}

// The entered screen's shadow has done its bridging: the real game's monsters are the only ones now.
void ShadowSet::dropBridge() {
    bridge->reset();
    active.erase(std::remove(active.begin(), active.end(), bridge), active.end());
    bridge = nullptr;
}

ShadowGame::HandOff ShadowSet::tryHandOff(Nes& real) {
    if (!enabled || real.ram[kCurLevel] != 0) return ShadowGame::HandOff::No;
    for (ShadowGame* g : active) if (g->visible() && g->room() == (int)real.ram[kRoomId]) return g->handOffTo(real);
    return ShadowGame::HandOff::No;
}

std::string ShadowSet::summary() const {
    static const char* names[] = {"idle", "spin-up", "settling", "ready", "FAILED"};
    char buf[64]; std::string s = "shadows of room " + std::to_string(base) + ":";
    for (const ShadowGame* g : active) { std::snprintf(buf, sizeof buf, " %02X=%s(%d)", g->room(), names[(int)g->state()], g->frames()); s += buf; }
    return s;
}

bool ShadowSet::dump(const char* path) const {
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    std::fprintf(f, "%s\n", summary().c_str());
    for (const ShadowGame* g : active) {
        const Nes& n = g->nes();
        std::fprintf(f, "\nshadow of screen %02X: state %d, %d frames, game mode %02X sub %02X, room %02X\n", g->room(), (int)g->state(), g->frames(), n.ram[kGameMode], n.ram[kGameSubmode], n.ram[kRoomId]);
        if (g->attemptsMade() || g->lastFailure()) std::fprintf(f, "  attempts %d, last failure: %s\n", g->attemptsMade(), g->lastFailure() ? g->lastFailure() : "none");
        for (int slot = 1; slot <= 0x13; slot++)
            if (n.ram[kObjType + slot]) std::fprintf(f, "  slot %02X type %02X at (%d,%d) dir %d state %02X\n", slot, n.ram[kObjType + slot], n.ram[kObjX + slot], n.ram[kObjY + slot], n.ram[kObjDir + slot], n.ram[kObjState + slot]);
    }
    std::fclose(f);
    return true;
}
