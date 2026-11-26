// Widescreen: stairs on the overworld. The game puts Link at the screen's exit spot (AttrsA high nibble, AttrsF row; Z_05.asm 1575)
// when he comes out of a cave or dungeon, which for stairs ($70-$73) is often another square than the one he walked onto. Here he comes
// out on the stairs he entered by, and the game's own guard against going straight back in (UndergroundExitType, which CheckWarps
// honours, Z_05.asm 7250) is kept until he has left that square completely.
#include "widescreen_internal.h"

namespace {
constexpr int kExitRowBase = 0x4D;      // ObjY = row * 16 + $4D (Z_05.asm 1590)
bool isStairTile(int t) { return t >= 0x70 && t <= 0x73; }
}

bool Widescreen::stairsExitSpot(const Nes& nes, int room, int& x, int& y) const {
    if (!enabled || !stairValid || room != stairRoom || nes.ram[kCurLevel] != 0 || !isStairTile(nes.ram[kUndergroundEntranceTile])) return false;
    if ((stairX & 0x0F) != 0 || stairY < kExitRowBase || stairY > kExitRowBase + 7 * 16 || ((stairY - kExitRowBase) & 0x0F) != 0) return false;   // (the attributes only hold 16 px columns, rows 0-7)
    x = stairX; y = stairY;
    return true;
}

void Widescreen::stairsPrepare(Nes& nes) {
    const int mode = nes.ram[kGameMode], room = nes.ram[kRoomId] & 0x7F;
    int x, y;
    if (mode == kModeEnter && nes.ram[kUndergroundExitType] != 0 && !stairAttrsOn && stairsExitSpot(nes, room, x, y)) {
        // The game reads its exit spot from the screen's attributes while it lays Link out: lend it ours for this frame.
        stairAttrRoom = room; stairSavedA = nes.cart.cpuRead(kAttrsA + room); stairSavedF = nes.cart.cpuRead(kAttrsF + room);
        nes.cart.cpuWrite(kAttrsA + room, (uint8_t)((stairSavedA & 0x0F) | x));
        nes.cart.cpuWrite(kAttrsF + room, (uint8_t)((stairSavedF & 0xF8) | ((y - kExitRowBase) >> 4)));
        stairAttrsOn = true; stairLock = true;
    }
    if (!stairLock) return;
    const bool here = mode == kModePlay && nes.ram[kCurLevel] == 0 && room == stairRoom &&
                      std::abs((int)nes.ram[kObjX] - stairX) < 16 && std::abs((int)nes.ram[kObjY] - stairY) < 16;
    if (here) nes.ram[kUndergroundExitType] = 1;                     // the game clears it after a step of 8 px: still on the square, so keep it
    else if (mode != kModeEnter) { stairLock = false; if (mode == kModePlay) nes.ram[kUndergroundExitType] = 0; }
}

void Widescreen::stairsAfter(Nes& nes) {
    const int mode = nes.ram[kGameMode], level = nes.ram[kCurLevel];
    if (stairAttrsOn) {                                              // (the attributes go back as they were)
        nes.cart.cpuWrite(kAttrsA + stairAttrRoom, stairSavedA); nes.cart.cpuWrite(kAttrsF + stairAttrRoom, stairSavedF);
        stairAttrsOn = false;
    }
    if (mode <= kModeFileSelect) { stairValid = false; stairLock = false; }
    if (stairPrevMode == kModePlay && stairPrevLevel == 0 && mode == kModeStairs) {      // Link went down some stairs or into a cave: remember the square
        const int x = nes.ram[kObjX], y = nes.ram[kObjY];
        stairValid = (x & 7) == 0 && (y & 0x0F) == 0x0D;             // (CheckWarps only fires on such a position, Z_05.asm 7250)
        stairRoom = nes.ram[kRoomId] & 0x7F; stairX = x; stairY = y;
    }
    stairPrevMode = mode; stairPrevLevel = level;
}
