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

// The shadow is the screen as Link left it, so its creatures replace whatever the game has just made: monsters that were killed stay dead, the items on the ground
// (with the despawn timers that went on running) are there, flying monsters keep their speed.
ShadowGame::HandOff ShadowGame::handOffTo(Nes& real) {
    if (!visible() || !game || real.ram[kRoomId] != target || real.ram[kCurLevel] != 0) return HandOff::No;
    auto scenery = [](uint8_t t) { return t >= kTypeSceneryFirst && t <= kTypeSceneryLast; };
    for (int s = 1; s <= 12; s++) {
        if (scenery(real.ram[kObjType + s]) || scenery(game->ram[kObjType + s])) continue;       // (docks, rocks, trees: the game's own)
        for (uint16_t base : kHandOffArrays) real.ram[base + s] = game->ram[base + s];
    }
    for (uint16_t a : kHandOffBytes) real.ram[a] = game->ram[a];
    for (int i = 0; i < kRoomObjTemplateLen; i++) real.ram[kRoomObjTemplateType + i] = game->ram[kRoomObjTemplateType + i];
    for (int i = 0; i < kRandomLen; i++) real.ram[kRandom + i] = game->ram[kRandom + i];     // (the same dice and the same Link position: the shadow that stands in for a few more frames and the game then go on the same way)
    followRealLink(real);
    return HandOff::Done;
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
    lx = wx = kCenterX; ly = wy = kCenterY; wanders = true;
}

// The stand-in Link strolls about the screen (the monsters of Zelda 1 steer towards the player: with a Link who never moves they all gathered around him). He keeps out of
// reach of the items on the ground (the game lets Link take an item within 8 px, Z_01.asm 4422-4438: a shadow must not pick them up before the real Link comes back).
void ShadowGame::wanderLink() {
    uint8_t* r = game->ram;
    auto rnd = [&]() { wrng = wrng * 1664525u + 1013904223u; return (int)(wrng >> 16); };
    auto safe = [&](float x, float y) {
        for (int s = 1; s <= 0x13; s++)
            if (r[kObjType + s] == kTypeItem && std::fabs(x - r[kObjX + s]) < kItemClear && std::fabs(y + 3 - r[kObjY + s]) < kItemClear) return false;
        // The room item (a heart container, a triforce piece) has no object type: the game draws it and lets Link take it all the same (the stand-in took the overworld's heart container
        // as he strolled over the lake: its room flag was set in the shadow and it stayed gone until Link walked into the screen himself).
        if (!(r[kObjState + kSlotRoomItem] & 0x80) && r[kRoomItemId] != kRoomItemNone && std::fabs(x - r[kObjX + kSlotRoomItem]) < kItemClear && std::fabs(y + 3 - r[kObjY + kSlotRoomItem]) < kItemClear) return false;
        return true;
    };
    auto newGoal = [&]() { wx = (float)(kWanderX0 + rnd() % (kWanderX1 - kWanderX0)); wy = (float)(kWanderY0 + rnd() % (kWanderY1 - kWanderY0)); };
    for (int k = 0; k < 4; k++) {                                        // (he never stands still: the game mirrors the chase target of a Link who does not move, and the monsters would dither between him and the far side of the screen)
        const float dx = wx - lx, dy = wy - ly, d = std::sqrt(dx * dx + dy * dy);
        if (d < kWanderSpeed) { newGoal(); continue; }
        const float nx = lx + dx / d * kWanderSpeed, ny = ly + dy / d * kWanderSpeed;
        if (safe(nx, ny)) { lx = nx; ly = ny; break; }
        newGoal();                                                       // (an item lies in the way: another goal)
    }
    for (int k = 0; k < 12 && !safe(lx, ly); k++) { lx = (float)(kWanderX0 + rnd() % (kWanderX1 - kWanderX0)); ly = (float)(kWanderY0 + rnd() % (kWanderY1 - kWanderY0)); }
    r[kObjX] = (uint8_t)lx; r[kObjY] = (uint8_t)ly; r[kObjGridOffset] = 0;
    if (r[kObjY] == kPondEdgeY) r[kObjY]++;                                                    // (a Link at the pond's edge makes the Great Fairy fill hearts and halt him: UpdatePondFairy, Z_04.asm 3384-3400)
    if (r[kObjX] == kDockX || r[kObjX] == kDockXRoom55) r[kObjX]++;                            // (a Link standing exactly there starts the raft ride: UpdateDock, Z_04.asm 3862-3900)
    r[kObjState] = 0;
    r[kChaseOtherTarget] = 0; r[kChaseLongTimer] = kChaseKeep;                                // (the monsters always chase him, never the mirrored point: the game flips the target when this timer runs out)
    if (aimOn) { r[kObjX] = aimX; r[kObjY] = aimY; aimOn = false; }                          // (a boomerang on its way back to the real Link: the one it steers to stands where it must go)
}

// The raft (UpdateDock, Z_04.asm 3863-3955) and the Great Fairy's hearts (UpdatePondFairy, Z_04.asm 3374-3400) are set off by where the PLAYER stands (X exactly the dock's and Y $3D / $7D;
// Y $AD and X $70-$80) and then halt Link: nothing a screen Link is not in may play them. They are undone as soon as the game has started them: the first frame of either draws nothing
// yet (the raft and the hearts are drawn from the second frame on), so nothing of it is ever seen.
void ShadowGame::containScenery() {
    uint8_t* r = game->ram; bool undone = false;
    for (int slot = 1; slot <= 12; slot++) {
        const uint8_t t = r[kObjType + slot], s = r[kObjState + slot];
        if ((t == kTypeDock && s != 0) || (t == kTypePondFairy && (s == 1 || s == 2))) { r[kObjState + slot] = 0; undone = true; }
    }
    if (undone) { r[kObjState] = 0; r[kWorldIsFillingHearts] = 0; }          // (Link was halted for it)
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
    if (!n.cpu.inNmi() && st != State::Failed) containScenery();
    if (st == State::Failed) return;
    if (st == State::Settling || st == State::Ready) r[kUndergroundExitType] = 1;     // (the stand-in Link must not take a cave or stairs under his feet: CheckWarps returns at once, Z_05.asm 7250)
    if (wanders && (st == State::Settling || st == State::Ready) && !n.cpu.inNmi()) wanderLink();
    n.runFrame();
    if (!n.cpu.inNmi()) containScenery();
    // Nothing flies in a shadow: the monsters' shots are deleted (a shot spawned this frame is drawn once at most) and the picture's 8x16 flag is kept for the drawing.
    for (int slot = 1; slot <= 12; slot++) if (r[kObjType + slot] >= kTypeFirstProjectile && r[kObjType + slot] <= kTypeLastProjectile) r[kObjType + slot] = 0;
    r[kActiveMonsterShots] = 0;                                       // (the game counts live monster shots and stops all shooting at 4: a monster that wants to shoot but cannot stands still for 46 of every 48 frames. The deleted shots must not stay counted)
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
    active.clear(); base = -1; bridge = nullptr; bridgeHold = 0; departed = false; wasOverworldPlay = false; dropDelay = -1;   // (an armed hand-off stays armed: it is for the screen the real game is loading)
}

ShadowGame* ShadowSet::alloc() {
    for (ShadowGame& g : pool) if (g.state() == ShadowGame::State::Idle && &g != bridge) return &g;
    return nullptr;
}

void ShadowSet::armHandOff(const Nes& real) {
    handOffArmed = true; bridge = nullptr; bridgeHold = 0;
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
        if (g == bridge) { keep.push_back(g); continue; }             // (the screen Link has entered: its monsters are the real game's now; the shadow only bridges the picture, and goes on until it is dropped)
        if (g->room() != room && ((std::abs(dx) <= kKeepRadius && std::abs(dy) <= kKeepRadius) || wanted[g->room()]) && g->state() != ShadowGame::State::Failed) keep.push_back(g);   // (two screens away: only kept, never started: walking in and out quickly must not make them start afresh)
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

// Every screen on the canvas (the compositor says which: more of them the further out the camera is) gets a shadow. The eight around Link are made from the real game (reconcile);
// a screen further out is made from a neighbouring shadow that is already up, one hop away (two for a diagonal), a few per frame, the nearest first.
void ShadowSet::topUp(Nes& real) {
    const int room = real.ram[kRoomId];
    if (room < 0 || room > 0x7F) return;
    std::vector<std::pair<int, int>> todo;
    for (int r = 0; r < 128; r++) {
        if (!wanted[r] || r == room) continue;
        bool have = false; for (const ShadowGame* g : active) have = have || g->room() == r;
        if (have) continue;
        const int dc = std::abs((r & 15) - (room & 15)), dr = std::abs((r >> 4) - (room >> 4));
        todo.push_back({std::max(dc, dr) * 8 + dc + dr, r});
    }
    std::sort(todo.begin(), todo.end());
    int started = 0;
    for (const auto& t : todo) {
        if (started >= kStartsPerUpdate) break;
        const int r = t.second;
        ShadowGame* src = nullptr; int best = 1 << 20;
        for (ShadowGame* g : active) {                                // (an up shadow beside it: orthogonal = one hop is best, then the one nearest Link)
            const int dx = (r & 15) - (g->room() & 15), dy = (r >> 4) - (g->room() >> 4);
            if (!g->visible() || g->room() == r || std::abs(dx) > 1 || std::abs(dy) > 1) continue;
            const int cost = (dx && dy ? 100 : 0) + std::abs((g->room() & 15) - (room & 15)) + std::abs((g->room() >> 4) - (room >> 4));
            if (cost < best) { best = cost; src = g; }
        }
        ShadowGame* g = alloc();
        if (!g) break;
        const int sdx = src ? (r & 15) - (src->room() & 15) : (r & 15) - (room & 15), sdy = src ? (r >> 4) - (src->room() >> 4) : (r >> 4) - (room >> 4);
        if (!src && (std::abs(sdx) > 1 || std::abs(sdy) > 1)) continue;   // (nothing up beside it yet: a later frame)
        const Nes& from = src ? src->nes() : real;
        if (g->start(from, sdx, sdy, (uint32_t)(real.cpu.totalCycles >> 4) + (uint32_t)r * 104729u)) { active.push_back(g); started++; }
    }
}

// A boomerang Link threw and left behind keeps flying in its screen's shadow; when it turns back it flies to the real Link, however many screens away: it steers to the edge of its
// screen that faces his, and there it is handed to the next screen (the shadow beside it, or the real game itself), until the real game's own boomerang code brings it to Link.
void ShadowSet::relayBoomerangs(Nes& real) {
    if (real.ram[kCurLevel] != 0) return;
    const bool realPlays = real.ram[kGameMode] == kModePlay;          // (the real game can take a boomerang only in play)
    static const uint16_t kCopy[] = {kObjX, kObjY, kObjDir, kObjState, kObjTimer, kObjGridOffset, kObjMovingLimit, kObjPosFrac, kObjQSpeedFrac, kObjAnimCounter, kObjAnimFrame, kObjType};
    const int realRoom = real.ram[kRoomId];
    for (ShadowGame* g : active) {
        if (!g->visible()) continue;
        uint8_t* r = g->nes().ram;
        const int st = r[kObjState + kSlotBoomerang] & 0xF0;
        if (r[kObjType + kSlotBoomerang] != kTypeBoomerang || (st != 0x40 && st != 0x50)) { g->boomWait = 0; continue; }       // (flying back to the thrower: states $40, $50)
        const int dc = (realRoom & 15) - (g->room() & 15), dr = (realRoom >> 4) - (g->room() >> 4);
        if (!dc && !dr) continue;
        const bool horiz = std::abs(dc) >= std::abs(dr);
        const int sx = horiz ? (dc > 0 ? 1 : -1) : 0, sy = horiz ? 0 : (dr > 0 ? 1 : -1);
        const int bx = r[kObjX + kSlotBoomerang], by = r[kObjY + kSlotBoomerang];
        g->aimLink(horiz ? (sx > 0 ? kEdgeRight : kEdgeLeft) : bx, horiz ? by : (sy > 0 ? kEdgeDown : kEdgeUp));
        const bool atEdge = horiz ? (sx > 0 ? bx >= kEdgeRight - kRelayBand : bx <= kEdgeLeft + kRelayBand) : (sy > 0 ? by >= kEdgeDown - kRelayBand : by <= kEdgeUp + kRelayBand);
        if (!atEdge) continue;
        const int next = ShadowGame::targetRoom(g->room(), sx, sy);
        uint8_t* d = nullptr;
        if (next == realRoom) d = realPlays ? real.ram : nullptr;
        else for (ShadowGame* o : active) if (o->room() == next && o->visible()) { d = o->nes().ram; break; }
        if (!d || d[kObjState + kSlotBoomerang] != 0) {                                                 // (nobody can take it yet: it hovers at the edge, out of reach of the stand-in Link, until somebody can)
            if (++g->boomWait <= kRelayPatience) {                                                      // (not for ever: when nobody comes it is caught at the edge)
                if (horiz) r[kObjX + kSlotBoomerang] = (uint8_t)(sx > 0 ? kEdgeRight - kRelayBand : kEdgeLeft + kRelayBand); else r[kObjY + kSlotBoomerang] = (uint8_t)(sy > 0 ? kEdgeDown - kRelayBand : kEdgeUp + kRelayBand);
            }
            continue;
        }
        g->boomWait = 0;
        for (uint16_t a : kCopy) d[a + kSlotBoomerang] = r[a + kSlotBoomerang];
        if (horiz) d[kObjX + kSlotBoomerang] = (uint8_t)(sx > 0 ? kEdgeLeft + kRelayBand : kEdgeRight - kRelayBand);
        else d[kObjY + kSlotBoomerang] = (uint8_t)(sy > 0 ? kEdgeUp + kRelayBand : kEdgeDown - kRelayBand);
        r[kObjState + kSlotBoomerang] = 0; r[kObjType + kSlotBoomerang] = 0;
    }
}

void ShadowSet::stepAll() {
    const int n = (int)active.size();
    int spinners = 0; for (const ShadowGame* g : active) spinners += g->state() == ShadowGame::State::SpinUp;
    const int steps = forceSpinSteps > 0 ? forceSpinSteps : spinSteps(spinners, par::maxThreads());           // (the more cores, the faster the unseen walk to the screens goes)
    par::forRange(0, n, 1, [&](int a, int b) {
        for (int i = a; i < b; i++) {
            ShadowGame& g = *active[i];
            for (int s = 0; s < steps; s++) {               // (only the walk to the screen, which nobody sees, is hurried: the arrival and everything after runs at the game's speed)
                const bool spinning = g.state() == ShadowGame::State::SpinUp;
                g.stepFrame();
                if (!spinning || g.state() != ShadowGame::State::SpinUp) break;
            }
        }
    });
}

void ShadowSet::animate(Nes* real) { if (!enabled) return; if (real && real->ram[kCurLevel] == 0) relayBoomerangs(*real); stepAll(); }

// Modes in which the overworld is gone for good (title, file select, game over / continue, saving, the end): the shadows are dropped.
static bool worldGone(uint8_t mode) { return mode <= kModeFileSelect || mode == kModeContinue || mode == kModeSave || mode >= kModeDeath; }

void ShadowSet::update(Nes& real) {
    if (!enabled) { handOffArmed = false; lastMode = -1; if (!active.empty() || base >= 0) clear(); return; }
    const uint8_t mode = real.ram[kGameMode];
    const int level = real.ram[kCurLevel];
    if (worldGone(mode)) { handOffArmed = false; lastMode = -1; wasOverworldPlay = false; if (!active.empty() || base >= 0) clear(); return; }
    // Inside a dungeon nothing of the overworld shows and the game makes its monsters afresh when Link comes out: the shadows only live through the walk in.
    if (level != 0 && mode != kModeStairs) {                          // (the picture is a frame behind: the shadows stay for the last pictures of the walk)
        if (dropDelay < 0) dropDelay = kDropDelay;
        if (dropDelay-- == 0) { handOffArmed = false; lastMode = -1; wasOverworldPlay = false; if (!active.empty() || base >= 0) clear(); dropDelay = -1; return; }
        if (!active.empty()) stepAll();
        return;
    }
    dropDelay = -1;
    // The overworld goes on in every other mode: scrolls, caves, stairs, the walk in and out. The shadows are stepped once per frame like the real game would.
    if (mode == kModeLeave && level == 0) {                           // Link is leaving the screen: it carries on as a shadow from exactly this state
        if (!departed) { departed = true; if (ShadowGame* g = capture(real, real.ram[kRoomId])) { bridge = g; bridgeHold = 0; } }   // (the picture of mode 6 has no monsters either: the shadow stands in for them)
    } else if (mode != kModeStairs) departed = false;
    // A cave, stairs or a dungeon entrance: the screen Link stood on carries on as a shadow from the start of the walk in (RoomId may already be the cellar's). The game hides its
    // monsters from the picture of the walk on, so the shadow's are drawn instead (Widescreen::render).
    if (mode == kModeStairs && wasOverworldPlay && base >= 0 && !departed) { departed = true; capture(real, base); }
    if (mode == kModeEnter && lastMode != kModeEnter && !handOffArmed) armHandOff(real);   // (a way back from a cave, stairs or a dungeon: the scroll's own entry is armed by main.cpp)
    lastMode = mode;
    if (bridgeHold > 0 && --bridgeHold == 0 && bridge) { dropBridge(); }
    const bool plain = mode == kModePlay && level == 0 && real.ram[kMenuState] == 0 && real.ram[kPaused] == 0;
    wasOverworldPlay = mode == kModePlay && level == 0;
    if (plain && (int)real.ram[kRoomId] != base) reconcile(real);
    if (mode == kModeLeaveCave && level == 0 && !bridge) for (ShadowGame* g : active) if (g->visible() && g->room() == (int)real.ram[kRoomId]) { bridge = g; bridgeHold = 0; }   // (the overworld comes back without its monsters: the screen's shadow stands in for them)
    if (plain) topUp(real);
    if (level == 0) relayBoomerangs(real);
    if (plain && bridge && bridgeHold == 0) bridgeHold = kBridgeHold;
    if (real.ram[kMenuState] != 0 || real.ram[kPaused] != 0) return;  // the inventory / pause: the game stands still, so do the shadows
    stepAll();
    if (level == 0 && (mode == kModeEnter || mode == kModePlay || mode == kModeLeaveCave)) carryTiles(real);   // (mode $A: the overworld is laid out again behind the fade)
    if (handOffArmed && mode == kModePlay) {                          // Link has walked into the screen and the game plays again: the shadow's creatures take over where the shadow is NOW
        const ShadowGame::HandOff r = tryHandOff(real);               // (not earlier: the game does not move its monsters while Link walks in, the shadow does)
        handOffArmed = false; if (r != ShadowGame::HandOff::Done) retire(real.ram[kRoomId]);
    }
    if (plain)
        for (ShadowGame* g : active)                                   // (a failed shadow gets another go after a pause, a few times)
            if (g->state() == ShadowGame::State::Failed && g->failedFor() >= ShadowGame::kRetryDelay) {
                const int dc = std::abs((g->room() & 15) - (real.ram[kRoomId] & 15)), dr = std::abs((g->room() >> 4) - (real.ram[kRoomId] >> 4));
                if (dc > 1 || dr > 1) g->reset();                       // (two screens out: it was made from another shadow; topUp makes it afresh)
                else g->retry(real, (uint32_t)(real.cpu.totalCycles >> 3) + (uint32_t)g->room() * 31u);
            }
    active.erase(std::remove_if(active.begin(), active.end(), [](ShadowGame* o) { return o->state() == ShadowGame::State::Idle; }), active.end());
}

// The screen being left (`room`) goes on as a shadow; there is never more than one shadow of a screen.
ShadowGame* ShadowSet::capture(const Nes& real, int room) {
    ShadowGame* g = alloc();
    if (!g) { captureFailures++; lastCaptureFailure = "no free shadow"; return nullptr; }
    if (!g->captureFrom(real, room, (uint32_t)(real.cpu.totalCycles >> 5) + (uint32_t)room * 131u)) { captureFailures++; lastCaptureFailure = real.cpu.inNmi() ? "the game was inside its NMI handler" : "maze screen or bad room"; return nullptr; }
    captures++;
    for (ShadowGame* o : active) if (o != g && o->room() == g->room()) o->reset();
    active.erase(std::remove_if(active.begin(), active.end(), [](ShadowGame* o) { return o->state() == ShadowGame::State::Idle; }), active.end());
    if (bridge && bridge->state() == ShadowGame::State::Idle) { bridge = nullptr; bridgeHold = 0; }   // (the old bridging shadow of this screen was just replaced)
    active.push_back(g);
    return g;
}

// Nothing could be taken over from the shadow of the screen Link is in: it is dropped (two simulations of one screen would run side by side).
void ShadowSet::retire(int room) {
    for (ShadowGame* g : active) if (g->room() == room) { g->reset(); if (g == bridge) { bridge = nullptr; bridgeHold = 0; } }
    active.erase(std::remove_if(active.begin(), active.end(), [](ShadowGame* o) { return o->state() == ShadowGame::State::Idle; }), active.end());
}

// The entered screen's shadow has done its bridging: the real game's monsters are the only ones now.
void ShadowSet::dropBridge() {
    bridge->reset();
    active.erase(std::remove(active.begin(), active.end(), bridge), active.end());
    bridge = nullptr;
}

uint8_t ShadowGame::tile(int col, int row) const { return game->cart.cpuRead((uint16_t)(kPlayAreaTiles + col * kPlayRows + row)); }

// An armos that woke on a screen left the floor (or, the secret one, the stairs) where its statue stood, and the shadow of that screen kept it so; the game lays the screen
// out afresh when Link comes back: its statues are put back to what the shadow has, the way the game itself changes them (ChangeTileObjTiles, Z_07.asm 1145: the
// four tiles of PlayAreaTiles and a transfer of the same four tiles to the nametable through the PPU buffer). A statue whose tiles still are the statue's is left alone.
int ShadowSet::carryTiles(Nes& real) {
    const ShadowGame* g = visibleShadow(real.ram[kRoomId]);
    if (!g || real.ram[kCurLevel] != 0) return 0;
    int n = 0;
    for (int col = 0; col + 1 < kPlayCols; col += 2)
        for (int row = 0; row + 1 < kPlayRows; row += 2) {
            const uint16_t a = (uint16_t)(kPlayAreaTiles + col * kPlayRows + row), b = (uint16_t)(a + kPlayRows);
            if (real.cart.cpuRead(a) != kArmosTile || real.cart.cpuRead(a + 1) != kArmosTile + 1 || real.cart.cpuRead(b) != kArmosTile + 2 || real.cart.cpuRead(b + 1) != kArmosTile + 3) continue;
            const uint8_t t0 = g->tile(col, row), t1 = g->tile(col, row + 1), t2 = g->tile(col + 1, row), t3 = g->tile(col + 1, row + 1);
            if (t0 == kArmosTile && t1 == kArmosTile + 1 && t2 == kArmosTile + 2 && t3 == kArmosTile + 3) continue;       // (still a statue there)
            const int len = real.ram[kDynTileBufLen];
            if (len + 11 > 0xFE) return n;                                                                               // (the buffer is full: the next frame)
            const int y = (kHudRows + row) * 8, ppu = 0x2000 + y * 4 + col;                                              // (MapScreenPosToPpuAddr, Z_01.asm 4901)
            const uint8_t rec[11] = {(uint8_t)(ppu >> 8), (uint8_t)ppu, 0x82, t0, t1, (uint8_t)(ppu >> 8), (uint8_t)(ppu + 1), 0x82, t2, t3, 0xFF};
            std::memcpy(&real.ram[kDynTileBuf + len], rec, sizeof rec);
            real.ram[kDynTileBufLen] = (uint8_t)(len + 10);
            real.cart.cpuWrite(a, t0); real.cart.cpuWrite(a + 1, t1); real.cart.cpuWrite(b, t2); real.cart.cpuWrite(b + 1, t3);
            n++;
        }
    return n;
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
    std::fprintf(f, "\ncaptures of the screen Link leaves: %d made, %d failed (last failure: %s); pool in use: %d of %d\n", captures, captureFailures, lastCaptureFailure ? lastCaptureFailure : "none", (int)active.size(), kPool);
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
