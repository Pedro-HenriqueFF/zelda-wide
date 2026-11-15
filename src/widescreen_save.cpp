// Widescreen: everything that touches the game's saving and the ROM patches: the sword/beam/save-slot patches (prepareFrame), the fog
// kept per save slot and written next to the ROM (<rom>.fog), SAVE from the inventory (requestSave / afterFrame).
#include <fstream>
#include "widescreen_internal.h"

void Widescreen::prepareFrame(Nes& nes) {
    nes.ppu.unlimitedSprites = enabled && !nesLimits;                       // every sprite of a line is drawn: crowded rooms do not flicker
    nes.lagFree = enabled && !nesLimits;                                    // the game's logic gets the time it needs: crowded rooms do not slow down
    nes.ppu.smoothV = enabled && smoothScroll && nes.ram[kCurLevel] != 0;    // dungeon rooms scroll up/down as smoothly as sideways
    maskCandleAtBorder(nes);
    patchSaveSlotHearts(nes);
    stairsPrepare(nes);
    // Loading the slot that was saved inside a dungeon: start in the dungeon (its first room), as if entering it from the
    // overworld room it was entered from.
    if (spawnRoom >= 0) {
        const int mode = nes.ram[kGameMode];
        if (mode == kModeLoad || mode == kModeUnfurl) {
            if (nes.ram[kCurSaveSlot] == spawnSlot && (mode == kModeLoad || nes.ram[kGameSubmode] <= 1)) {
                nes.ram[kCurLevel] = (uint8_t)spawnLevel;                    // like entering the dungeon from the overworld (LoadLevel, Z_05.asm 7390)
                nes.ram[kCaveSourceRoomId] = (uint8_t)spawnRoom;                          // and leaving it again returns to this room
            }
        } else if (mode >= kModeEnter && mode != kModeSave) spawnRoom = -1;                // done (mode 4 = entering the room)
    }
    if (beamBoundsOff == -2) {                           // MakeSwordShot's X check (Z_07.asm 4573): LDA ObjX,X; CMP #$14; BCC; CMP #$EC; BCS
        const std::vector<uint8_t>& prg = nes.cart.prgData();
        beamBoundsOff = -1;
        for (size_t i = 0; i + 9 <= prg.size(); i++)
            if (prg[i] == 0xB5 && prg[i + 1] == 0x70 && prg[i + 2] == 0xC9 && prg[i + 3] == 0x14 && prg[i + 4] == 0x90 && prg[i + 6] == 0xC9 && prg[i + 7] == 0xEC && prg[i + 8] == 0xB0) { beamBoundsOff = (long)i; break; }
    }
    if (borderBoundsOff == -2) {                         // BorderBounds (Z_05.asm): down, up, right, left for movement (x2) and the sword
        static const uint8_t sig[12] = {0xD6, 0x45, 0xE9, 0x07, 0xC6, 0x55, 0xD9, 0x17, 0xBE, 0x54, 0xD1, 0x1F};
        const std::vector<uint8_t>& prg = nes.cart.prgData();
        auto it = std::search(prg.begin(), prg.end(), sig, sig + 12);
        borderBoundsOff = it == prg.end() ? -1 : it - prg.begin() + 8;
    }
    if (borderBoundsOff < 0) return;
    const bool free = enabled && lastKind == Kind::Overworld && nes.ram[kCurLevel] == 0;
    if (free == borderFree) return;
    // Facing down/right the A button is masked from the bound on (>=), facing up/left below it (<): $FF / $00 never match.
    static const uint8_t orig[4] = {0xBE, 0x54, 0xD1, 0x1F}, open[4] = {0xFF, 0x00, 0xFF, 0x00};
    for (int i = 0; i < 4; i++) nes.cart.patchPrg((size_t)borderBoundsOff + i, free ? open[i] : orig[i]);
    // The overworld outer set (first set) clears ALL buttons when the coordinate of the OPPOSITE direction is past its bound
    // (facing up at the bottom edge, facing right at the left edge...): that is what kept the sword from being swung there.
    // Facing right/down it masks below the bound, facing left/up from the bound on: $00 / $FF never match.
    static const uint8_t orig1[4] = {0xD6, 0x45, 0xE9, 0x07}, open1[4] = {0xFF, 0x00, 0xFF, 0x00};
    for (int i = 0; i < 4; i++) nes.cart.patchPrg((size_t)borderBoundsOff - 8 + i, free ? open1[i] : orig1[i]);
    // A horizontal beam starts 16 px from Link (8-bit wrap): the game drops it (no sprite at all) when that X is < $14 or >= $EC.
    // With $00 / $F0 it is dropped when Link's centre is less than 24 px from the left / right border (x < $10, x >= $E0).
    if (beamBoundsOff >= 0) {
        nes.cart.patchPrg((size_t)beamBoundsOff + 3, free ? 0x00 : 0x14);
        nes.cart.patchPrg((size_t)beamBoundsOff + 7, free ? 0xF0 : 0xEC);
    }
    borderFree = free;
}

void Widescreen::patchSaveSlotHearts(Nes& nes) {
    // The file-select screen shows each slot's hearts built from the file's Items block: containers as both the number of
    // hearts and the number filled (AND #$F0; STA $0C; LSR x4; ORA $0C, Z_02.asm ~290). With the AND kept whole and the shifts
    // removed it shows the saved HeartValues as they are, so the real number of full hearts.
    if (heartsPatchOff == -2) {
        static const uint8_t sig[11] = {0x29, 0xF0, 0x85, 0x0C, 0x4A, 0x4A, 0x4A, 0x4A, 0x05, 0x0C, 0x48};
        const std::vector<uint8_t>& prg = nes.cart.prgData();
        auto it = std::search(prg.begin(), prg.end(), sig, sig + 11);
        heartsPatchOff = it == prg.end() ? -1 : it - prg.begin();
    }
    if (heartsPatchOff < 0 || enabled == heartsPatched) return;
    nes.cart.patchPrg((size_t)heartsPatchOff + 1, enabled ? 0xFF : 0xF0);
    for (int i = 4; i < 8; i++) nes.cart.patchPrg((size_t)heartsPatchOff + i, enabled ? 0xEA : 0x4A);
    heartsPatched = enabled;
}

void Widescreen::syncFogSlot(Nes& nes, int slot) {
    // A slot's fog belongs to the player name stored in that slot; a different name means a new game.
    const uint8_t* name = &nes.ram[kNames + slot * 8];
    if (!fogNameValid[slot] || std::memcmp(fogName[slot], name, 8) != 0) {
        std::memset(fogBits[slot], 0, 16);
        std::memcpy(fogName[slot], name, 8);
        fogNameValid[slot] = true;
    }
}

void Widescreen::commitFog(Nes& nes, int slot) {
    syncFogSlot(nes, slot);
    std::memcpy(fogSaved[slot], fogBits[slot], 16);
    std::memcpy(fogSavedName[slot], fogName[slot], 8);
    fogSavedValid[slot] = true;
    fogDirty = true;
}

void Widescreen::revertFog() {
    // Back to the last saved fog (a save file was loaded): slots never saved start empty (their name is taken from RAM later).
    for (int s = 0; s < 3; s++) {
        if (fogSavedValid[s]) { std::memcpy(fogBits[s], fogSaved[s], 16); std::memcpy(fogName[s], fogSavedName[s], 8); fogNameValid[s] = true; }
        else { std::memset(fogBits[s], 0, 16); fogNameValid[s] = false; }
    }
}

void Widescreen::requestSave(Nes& nes) {
    if (nes.ram[kGameMode] != kModePlay) return;                 // only while playing
    // The inventory may be open: a MenuState left behind stops the game's timers, so the curtain of the next loaded game
    // (it waits for ObjTimer) would never finish.
    nes.ram[kMenuState] = 0; nes.ram[kPaused] = 0;
    // Do what the game's own SAVE does (mode 8 -> $0D): ResetPlayerState, reset UndergroundExitType (InitMode8), clear the room
    // history, then start mode $0D from its init (IsUpdatingMode 0), so InitModeD and the save run as usual.
    // The hearts are kept: the file gets the current ones from the Items block; the slot's display is restored in afterFrame.
    savedHearts[0] = nes.ram[kHeartValues]; savedHearts[1] = nes.ram[kHeartPartial]; heartsPending = true;
    // Saved inside a dungeon: remember where it was entered from (CaveSourceRoomId), see prepareFrame.
    const int entered = nes.ram[kCaveSourceRoomId];
    spawnRoom = nes.ram[kCurLevel] != 0 && entered != 0xFF ? entered : -1;
    spawnSlot = nes.ram[kCurSaveSlot]; spawnLevel = nes.ram[kCurLevel];
    nes.ram[kObjState] = 0; nes.ram[kInvClock] = 0;
    nes.ram[kUndergroundExitType] = 0;
    for (int i = 0; i < 6; i++) nes.ram[kRoomHistory + i] = 0;
    nes.ram[kForceSwordShot] = 0;
    nes.ram[kIsUpdatingMode] = 0;
    nes.ram[kGameMode] = kModeSave; nes.ram[kGameSubmode] = 0;
}

bool Widescreen::menuOpenForSave(const Nes& nes) {
    return nes.ram[kGameMode] == kModePlay && nes.ram[kMenuState] == (nes.ram[kCurLevel] == 0 ? 8 : 7);
}

bool Widescreen::saveConfirmInput(const Nes& nes, uint8_t pressed) {
    if (!menuOpenForSave(nes)) { saveConfirm = false; return false; }
    if (!saveConfirm) {
        if (pressed & BTN_SELECT) { saveConfirm = true; saveConfirmYes = false; }
        return false;
    }
    if (pressed & BTN_LEFT) saveConfirmYes = true;                             // (YES on the left, NO on the right)
    if (pressed & BTN_RIGHT) saveConfirmYes = false;
    if (pressed & BTN_B) { saveConfirm = false; return false; }
    if (pressed & BTN_A) { const bool yes = saveConfirmYes; saveConfirm = false; saveConfirmYes = false; return yes; }
    return false;
}

void Widescreen::afterFrame(Nes& nes) {
    trackFire(nes);
    stairsAfter(nes);
    if (heartsPending && nes.ram[kGameMode] == kModeSave && nes.ram[kGameSubmode] >= 1) {
        // The game stored full hearts for the slot (and in the profile) at submode 0; the file itself holds the current ones.
        const int slot = nes.ram[kCurSaveSlot] % 3;
        nes.ram[kSaveSlotHearts + slot * 2] = nes.ram[kHeartValues] = savedHearts[0];
        nes.ram[kSaveSlotHearts + 1 + slot * 2] = nes.ram[kHeartPartial] = savedHearts[1];
        heartsPending = false;
    }
    const uint8_t shot = nes.ram[kObjState + kSlotShot];
    const bool fresh = shot != 0 && shotPrev == 0;               // a beam that was just fired
    shotPrev = shot;
    if (!borderFree || !fresh) return;
    // Facing a border within two tiles of the overworld limits ($D6/$45/$E9/$07): the beam would splash at once, so it is not fired.
    const int dir = nes.ram[kObjDir], x = nes.ram[kObjX], y = nes.ram[kObjY];
    // (less than 1.5 tiles from the border to the centre of Link's sprite: the picture is 64..239 high and 256 wide)
    const bool atBorder = (dir == 4 && y >= 0xD0) || (dir == 8 && y < 0x50) || (dir == 1 && x >= 0xE0) || (dir == 2 && x < 0x10);
    if (atBorder) { nes.ram[kObjState + kSlotShot] = 0; shotPrev = 0; }

}

bool Widescreen::loadFog(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4];
    f.read(magic, 4);
    if (!f || std::memcmp(magic, "ZWF1", 4) != 0) return false;
    for (int s = 0; s < 3; s++) {
        f.read(reinterpret_cast<char*>(fogSavedName[s]), 8);
        f.read(reinterpret_cast<char*>(fogSaved[s]), 16);
        fogSavedValid[s] = true;
    }
    if (!f) { std::memset(fogSaved, 0, sizeof fogSaved); std::memset(fogSavedValid, 0, sizeof fogSavedValid); return false; }
    revertFog();
    return true;
}

void Widescreen::saveFogIfDirty(const std::string& path) {
    if (!fogDirty) return;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write("ZWF1", 4);
    for (int s = 0; s < 3; s++) {
        f.write(reinterpret_cast<const char*>(fogSavedName[s]), 8);
        f.write(reinterpret_cast<const char*>(fogSaved[s]), 16);
    }
    f.flush();
    if (f) fogDirty = false;                       // (if the write failed, try again next time)
}

void Widescreen::fadeDeathTopHalf(Nes& nes, Canvas& out, int roomX, int roomY) {
    const int ref = paletteLumaRange(refPalette, 8, 16);
    const int scale = ref > 0 ? std::clamp(paletteLumaRange(nes.ppu.palette, 8, 16) * 256 / ref, 0, 256) : 256;
    if (scale >= 256) return;
    uint32_t top[8]; int n = 0;
    for (int i = 0; i < 8; i++) { const uint32_t c = Ppu::nesColor(nes.ppu.palette[(i & 3) ? i : 0]); if (c & 0xFFFFFF) top[n++] = c | 0xFF000000u; }
    for (int y = topBand; y < out.h; y++) {
        uint32_t* row = &out.pixels[(size_t)y * out.w];
        const int fr = y - roomY;
        for (int x = 0; x < out.w; x++) {
            const int fx = x - roomX;
            if (fx >= 0 && fx < Ppu::W && fr >= 0 && fr < ROOM_H) {
                const uint8_t o = nes.ppu.sprOam[(HUD_H + fr) * Ppu::SPR_W + fx];
                if (o == kLinkOam0 || o == kLinkOam1) continue;
            }
            for (int k = 0; k < n; k++) if (row[x] == top[k]) { row[x] = scaleColor(row[x], scale); break; }
        }
    }
}
