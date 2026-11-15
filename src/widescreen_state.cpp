// Widescreen: what the game is doing (capture of the emulated RAM into a ViewSnapshot), the cache of decoded overworld screens and their
// secrets, and the fog's discovery / unfogging state. (Moved out of widescreen.cpp byte for byte.)
#include "widescreen_internal.h"

const uint8_t* Widescreen::roomTiles(Nes& nes, int room) {
    int layout = nes.cart.cpuRead(kAttrsD + room) & 0x7F;
    bool secret = nes.ram[kWorldFlags + room] & 0x80;
    int key = layout | (secret ? 0x100 : 0);
    Cached& c = cache[room];
    if (c.key != key) {
        map.decodeRoom(layout, secret, c.tiles, c.secret);
        if (!secret)                                                     // the armos that hides stairs (or the Power Bracelet): 2x2 tiles
            for (int k = 0; k < 7; k++)
                if (kSecretArmosRoom[k] == room)
                    for (int dc = 0; dc < 2; dc++) for (int dr = 0; dr < 2; dr++)
                        c.secret[((kSecretArmosX[k] >> 3) + dc) * OverworldMap::TILE_ROWS + ((kSecretArmosY - HUD_H) >> 3) + dr] = SecretArmos;
        c.anySecret = std::any_of(c.secret, c.secret + OverworldMap::TILES, [](uint8_t k) { return k != 0; });
        c.key = key;
    }
    return c.tiles;
}

const uint8_t* Widescreen::roomSecrets(Nes& nes, int room) {
    roomTiles(nes, room);
    const Cached& c = cache[room];
    if (!c.anySecret) return nullptr;
    // Game's rule: AttrsF bits 6-7 = 0 (both quests), 1 (first quest only) or 2 (second quest only).
    const int q = nes.cart.cpuRead(kAttrsF + room) >> 6;
    if (q && (q > 2 || q - 1 != nes.ram[kQuestNumbers + (nes.ram[kCurSaveSlot] % 3)])) return nullptr;
    return c.secret;
}

bool Widescreen::nametableShowsRoom(Nes& nes, int room) {
    // Does the picture the PPU is showing still hold this overworld screen? (Compares the visible play-area
    // rows of the active nametable with our decode.)
    const uint8_t* t = roomTiles(nes, room);
    const uint16_t base = 0x2000 + (nes.ppu.lineCtrl[100] & 3) * 0x400;
    int mismatches = 0;
    for (int col = 0; col < OverworldMap::TILE_COLS; col++)
        for (int row = 0; row < OverworldMap::TILE_ROWS; row++)
            if (nes.ppu.vramRead(base + (8 + row) * 32 + col) != t[col * OverworldMap::TILE_ROWS + row] && ++mismatches > CAVE_MISMATCH_THRESHOLD) return false;
    return true;
}

ViewSnapshot Widescreen::capture(Nes& nes) {
    ViewSnapshot s;
    s.mode = nes.ram[kGameMode];  s.sub = nes.ram[kGameSubmode];
    s.level = nes.ram[kCurLevel]; s.room = nes.ram[kRoomId] & 0x7F;
    s.linkX = nes.ram[kObjX];     s.linkY = nes.ram[kObjY];     s.slot = nes.ram[kCurSaveSlot];
    s.updating = nes.ram[kIsUpdatingMode];
    s.exitType = nes.ram[kUndergroundExitType];
    s.menu = nes.ram[kMenuState] != 0;
    if (!map.ready()) return s;                                         // NonGameplay

    // Modes 2 ("load level") and 3 ("unfurl", the curtain) are part of starting a game, continuing after death,
    // and walking into or out of a dungeon. On level 0 the curtain opens on an overworld screen (the start screen,
    // or the entrance of the dungeon we just left); on any other level it opens on a dungeon room. The room id
    // only becomes valid once mode 3 has set it up, so it isn't trusted before that.
    if (s.mode == kModeLoad || s.mode == kModeUnfurl) {
        if (s.level == 0) {
            s.kind = Kind::OverworldIntro;
            s.inOverworld = s.mode == kModeUnfurl && (s.updating || s.sub >= 2);
        } else {
            s.kind = Kind::Interior;                                       // dungeon: big picture before the curtain
        }
        return s;
    }
    if (!isGameplayMode(s.mode)) return s;                              // NonGameplay

    s.inOverworld = s.level == 0 && s.mode != kModeEndLevel;

    // The death sequence (mode $11): Link spins, the screen fades, then from submode $B the black "GAME OVER"
    // screen is shown. That last part is a full-screen picture of its own.
    if (s.mode == kModeDeath && s.sub >= 0xB) { s.kind = Kind::GameOver; return s; }

    // Caves and shops ($B) and secret-passage rooms ($C) on the overworld, whether still setting up or already playing.
    if (s.level == 0 && (s.mode == kModeCave || s.mode == kModeShortcut)) {
        s.caveSteady = s.updating || s.sub >= 7;
        s.kind = (s.menu && s.caveSteady) ? Kind::Menu : Kind::Cave;
        return s;
    }

    // Modes 5, 9, $B and $C all run the normal "play" update. Mode 5 is a regular room; 9 is a cellar, $B a
    // cave and $C a shortcut room. Their init phase (fades, the room being drawn) is a transition. For caves
    // the scene is on screen with Link in it from submode 7 on (the rest is Link taking a step in).
    const bool caveScene = (s.mode == kModeCave || s.mode == kModeShortcut) && (s.updating || s.sub >= 7);
    const bool playsLikeMode5 = s.mode == kModePlay || caveScene || (s.mode == kModeCellar && s.updating);
    if (!playsLikeMode5) {
        if (s.mode == kModeEnter || s.mode == kModeLeave) {
            // Walking into/out of a screen. On the overworld that's the overworld screen itself (also when
            // climbing out of a cave or dungeon); inside a dungeon it's an ordinary dungeon room.
            s.kind = s.level == 0 ? Kind::Overworld : Kind::Interior;
        } else {
            s.kind = Kind::Transition;
        }
        return s;
    }
    if (s.menu) { s.kind = Kind::Menu; return s; }
    if (s.mode != kModePlay || s.level != 0) { s.kind = Kind::Interior; return s; }

    // Overworld, normal play. Compare the game's live screen with our decode of the same screen. Small
    // differences are fine (burnt bushes, pushed blocks). A big difference means we can't trust our
    // decode for this screen: fall back to the centred box instead of drawing wrong neighbours.
    const uint8_t* t = roomTiles(nes, s.room);
    int mismatches = 0;
    for (int i = 0; i < OverworldMap::TILES; i++)
        if (nes.cart.cpuRead(kPlayAreaTiles + i) != t[i]) mismatches++;
    s.kind = mismatches <= CAVE_MISMATCH_THRESHOLD ? Kind::Overworld : Kind::OverworldOdd;
    if (verify && s.room != lastVerifiedRoom) {
        std::printf("[verify] screen $%02X: %d/%d tiles differ from the game's live screen -> %s\n",
                    s.room, mismatches, OverworldMap::TILES,
                    s.kind == Kind::Overworld ? "world view" : "unexpected screen, using centred box");
        lastVerifiedRoom = s.room;
    }
    return s;
}


void Widescreen::discoverRoom(int slot, const ViewSnapshot& v) {
    if (zelda::isMazeRoom(v.room) || discovered(slot, v.room)) return;     // (a maze room's fog never lifts: only the map learns of it, when it is solved)
    markDiscovered(slot, v.room);
    if (!unfogAnim || !fog) return;
    int cx, cy;
    linkWorldCenter(v, cx, cy);
    unfogRoom = v.room; unfogT = 0;
    unfogX = std::clamp(cx - (v.room & 15) * ROOM_W, 0, ROOM_W - 1);
    unfogY = std::clamp(cy - (v.room >> 4) * ROOM_H, 0, ROOM_H - 1);
    updateUnfogRadius();
}

void Widescreen::updateUnfogRadius() {
    // An ellipse with the room's proportions: it starts around Link (so he is never hidden) and ends past the farthest corner.
    if (unfogRoom < 0) { unfogRadius = 0; return; }
    double far = 0;
    for (int cx = 0; cx <= ROOM_W; cx += ROOM_W)
        for (int cy = 0; cy <= ROOM_H; cy += ROOM_H)
            far = std::max(far, std::hypot(cx - unfogX, (cy - unfogY) * (double)ROOM_W / ROOM_H));
    const double u = std::min(1.0, (double)unfogT / kUnfogFrames), e = u * (2.0 - u);          // ease out
    const double start = 48, end = far + kUnfogFeather;
    unfogRadius = start + (end - start) * e;
}

int Widescreen::revealAmount(int lx, int ly) const {
    const double d = std::hypot(lx + 0.5 - unfogX, (ly + 0.5 - unfogY) * (double)ROOM_W / ROOM_H);
    return (int)(std::clamp((unfogRadius - d) / kUnfogFeather, 0.0, 1.0) * 256.0 + 0.5);
}

void Widescreen::tickUnfog() {
    if (unfogRoom < 0) return;
    if (++unfogT >= kUnfogFrames || !unfogAnim) unfogRoom = -1;
    updateUnfogRadius();
}

void Widescreen::markDiscovered(int slot, int room) {
    fogBits[slot][room >> 3] |= (1 << (room & 7));
}

bool Widescreen::keepScroll(const Nes& nes) const {
    return enabled && nes.ram[kCurLevel] == 0 && isMazeRoom(nes.ram[kRoomId] & 0x7F) && isMazeRoom(prev.room);
}
