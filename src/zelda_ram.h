#pragma once
// Zelda 1 RAM / PRG-RAM addresses and object-slot numbers used by the front-end and the compositor.
// Source: the disassembly's Variables.inc and Z_07.asm (see verified-facts.md).
#include <cstdint>

namespace zelda {
// Save / load / SAVE AND QUIT (Variables.inc): UndergroundExitType, CaveSourceRoomId, ForceSwordShot, RoomHistory (6 bytes), InvClock,
// HeartValues (high nibble containers - 1, low nibble full hearts - 1), HeartPartial, SaveSlotHearts (2 bytes per slot).
constexpr uint16_t kUndergroundEntranceTile = 0x65, kUndergroundExitType = 0x5A, kCaveSourceRoomId = 0x526, kForceSwordShot = 0x529, kRoomHistory = 0x621, kInvClock = 0x66C,
                   kHeartValues = 0x66F, kHeartPartial = 0x670, kSaveSlotHearts = 0x650;
constexpr uint16_t kCurLevel = 0x10, kIsUpdatingMode = 0x11, kGameMode = 0x12, kGameSubmode = 0x13, kCurSaveSlot = 0x16;
constexpr uint16_t kObjX = 0x70, kObjY = 0x84, kObjState = 0xAC, kMenuState = 0xE1, kPaused = 0xE0, kRoomId = 0xEB;   // objects: slot 0 = Link
constexpr uint16_t kObjType = 0x34F;
constexpr uint8_t kTypeFirstProjectile = 0x53, kTypeLastProjectile = 0x5C;   // monster shots, fireballs, wizzrobe magic, arrows, boomerang (UpdateObject_JumpTable, Z_07.asm 5405-5414); $5D+ are dummies, items, rocks, trees
// What a hand-off copies from a shadow into the real game (ShadowGame::handOffTo): per-slot arrays (slots 1-12 = the monsters), then single room-wide bytes.
// Bases from Variables.inc / ObjVars.inc (arrays of 13 entries: ObjTimer 28, ObjStunTimer 3D, ObjX 70, ObjY 84, ObjDir 98, ObjState AC, ObjShoveDir C0, ObjShoveDistance D3,
// ObjType 34F, ObjMovingLimit 380, ObjGridOffset 394, ObjPosFrac 3A8, ObjQSpeedFrac 3BC, ObjAnimCounter 3D0, ObjAnimFrame 3E4, ObjInputDir 3F8, ObjMetastate 405,
// the per-monster variables 444/451/45E/46B/478, ObjHP 485, ObjUninitialized 492, ObjCollidedTile 49E, ObjInvincibilityMask 4B2, ObjAttr 4BF, ObjInvincibilityTimer 4F0).
constexpr uint16_t kHandOffArrays[] = {0x28, 0x3D, 0x70, 0x84, 0x98, 0xAC, 0xC0, 0xD3, 0x34F, 0x380, 0x394, 0x3A8, 0x3BC, 0x3D0, 0x3E4, 0x3F8, 0x405, 0x412, 0x41F, 0x42C, 0x437, 0x444, 0x451, 0x45E, 0x46B, 0x478, 0x485, 0x492, 0x49E, 0x4B2, 0x4BF, 0x4F0};
// RoomAllDead 34D, RoomObjCount 34E, FlyingMaxSpeedFrac 4D1, ActiveRedLeeverCount 510, ZoraActive 514, ActiveBoulders 515-519, ChaseLongTimer 4A, MonstersFromEdgesLongTimer 4B,
// RedLeeverLongTimer 4D, EmptyMonsterSlot 59, SpawnCycle 524, CurEdgeSpawnCell 525
constexpr uint16_t kHandOffBytes[] = {0x34D, 0x34E, 0x4D1, 0x510, 0x514, 0x515, 0x516, 0x517, 0x518, 0x519, 0x4A, 0x4B, 0x4D, 0x59, 0x524, 0x525};
constexpr uint16_t kRoomObjTemplateType = 0x35F, kRoomObjTemplateLen = 0x21;   // (RoomObjTemplateType 35F-37F, the room's monster list)
// $412 ObjWantsToShoot / Flyer_ObjSpeedFrac, $41F ObjTurnRate / Flyer_ObjSpeed / Jumper_ObjSpeedFracY, $42C Flyer_ObjTurns, $437 Flyer_ObjDistTraveled (ObjVars.inc 1-30): a flying peahat
// or a jumping tektite loses its speed and turns without them (the rest of ObjVars.inc's arrays are among the others).
// Scenery objects the game makes from the room (UpdateObject_JumpTable, Z_07.asm 5415-5425): $61 dock (the raft: its ObjState carries Link up or down), $62/$65/$66 rock or
// gravestone, $63/$67 rock wall, $64 tree, $68 push block, $69 nothing. The shadow games never hand these over: a raft that a shadow's Link had started would drag the real Link.
constexpr uint8_t kTypeSceneryFirst = 0x61, kTypeSceneryLast = 0x69;
constexpr uint16_t kChaseOtherTarget = 0x60, kChaseLongTimer = 0x4A;   // (Variables.inc 14, 34: who the monsters chase; Z_07.asm 1895-1950)
constexpr uint16_t kActiveMonsterShots = 0x34C;      // ActiveMonsterShots (Variables.inc 97): counted monster shots, at most 4; DestroyCountedMonsterShot decrements it (Z_04.asm 919); at 4 no monster can shoot (Z_04.asm 11347)
constexpr uint16_t kWorldIsFillingHearts = 0x63;     // World_IsFillingHearts (Variables.inc 37): the Great Fairy's heart animation, Link halted
constexpr uint16_t kRandom = 0x18;                   // Random: the 13-byte shift register the NMI handler scrambles every frame (Z_07.asm 511-531, Variables.inc 8)
constexpr int kRandomLen = 13;
constexpr uint16_t kObjShoveDir = 0xC0, kObjShoveDistance = 0xD3, kObjGridOffset = 0x394, kObjInvincibilityTimer = 0x4F0;   // (Variables.inc 54, 55, 103, 147)
// Link's coordinates at the screen edge in each direction (PlayerScreenEdgeBounds, Z_07.asm 2831: up $3D, down $DD, left $00, right $F0).
constexpr uint8_t kEdgeUp = 0x3D, kEdgeDown = 0xDD, kEdgeLeft = 0x00, kEdgeRight = 0xF0, kCenterX = 0x78, kCenterY = 0x8D;
constexpr uint16_t kObjDir = 0x98;       // facing: 1 right, 2 left, 4 down, 8 up
constexpr uint8_t kModeSave = 0x0D;     // game mode: save (sub 0..2; sub 2 = done, back to the title)
// The mazes (verified-facts.md, "Lost Woods / Lost Hills"): the room, and the room Link reaches when he leaves it the right way.
constexpr uint8_t kForestMazeRoom = 0x61, kForestMazeExit = 0x60, kMountainMazeRoom = 0x1B, kMountainMazeExit = 0x0B;
inline bool isMazeRoom(int room) { return room == kForestMazeRoom || room == kMountainMazeRoom; }
// Game modes (GameMode; verified-facts.md, "Game modes"): 0 title, 1 file select, 2 load level, 3 unfurl (curtain), 4 enter room, 5 play, 6 leave room, 7 scroll to the next screen,
// 8 continue/save/retry, 9 cellar, $A leave cave/cellar, $B cave, $C shortcut, $D save, $10 stairs, $11 death, $12 end level.
constexpr uint8_t kModeTitle = 0, kModeFileSelect = 1, kModeLoad = 2, kModeUnfurl = 3, kModeEnter = 4, kModePlay = 5, kModeLeave = 6, kModeScroll = 7, kModeContinue = 8, kModeCellar = 9,
                  kModeLeaveCave = 0x0A, kModeCave = 0x0B, kModeShortcut = 0x0C, kModeStairs = 0x10, kModeDeath = 0x11, kModeEndLevel = 0x12;
constexpr int kLinkOam0 = 18, kLinkOam1 = 19;          // Link is drawn by two 8x16 sprites: OAM entries 18 and 19
// The armos that hides a secret (Z_04.asm SecretArmosRoomIds / SecretArmosXs): the one in that room at this X and Y = $80.
constexpr uint8_t kSecretArmosRoom[7] = {0x24, 0x0B, 0x1C, 0x22, 0x34, 0x3D, 0x4E}, kSecretArmosX[7] = {0xE0, 0xB0, 0xB0, 0x30, 0x40, 0x90, 0xA0}, kSecretArmosY = 0x80;
constexpr uint16_t kSelectedItem = 0x656;           // B-button item slot (WieldItem_JumpTable: 0 boomerang, 1 bomb, 2 arrow, 4 candle ...)
constexpr int kItemSlotCandle = 4;
constexpr int kItemTriforce = 0x1B;                 // item id of a triforce shard (ItemIdToSlot, Z_01.asm 4318: id $1B -> slot $1A); the Triforce of Power is id $0E
constexpr int kItemHeart = 0x22, kItemRupee = 0x18, kItemRupee5 = 0x0F;   // dropped items (DropItemTable, Z_04.asm ~11063)
constexpr uint16_t kItemTypeToLift = 0x505;         // item id Link is lifting (Variables.inc; 0 = none; set by TakeItem, cleared by EndLinkLiftingItem after $80 frames)
constexpr uint16_t kRoomItemId = 0xAB;              // the room item's id (ObjDir of slot $13); the item lives in slot $13 (active when ObjState bit 7 is clear)
constexpr int kItemHeartContainer = 0x1A;           // (ItemIdToSlot: id $1A -> slot $18 = HeartValues)
constexpr uint16_t kCaveFlags = 0x413, kCaveItemIds = 0x422;   // cave wares: shown when CaveFlags bit 2 is set; ids & $3F ($3F = none) at x $58/$78/$98, y $98 (DrawCaveItems, Z_01.asm 395)
constexpr int kItemFairy = 0x23;                    // Item_ObjItemId (= ObjState of an item object) of a fairy
constexpr uint16_t kInvCandle = 0x65B;   // 0 none, 1 blue candle, 2 red candle
constexpr uint16_t kQuestNumbers = 0x62D;   // QuestNumbers: one byte per save slot (0 first quest, 1 second)
constexpr uint16_t kNames = 0x638, kWorldFlags = 0x67F;
constexpr uint16_t kPlayAreaTiles = 0x6530;
constexpr uint16_t kLevelWorldFlagsAddr = 0x6BAF;     // LevelInfo_WorldFlagsAddr (Variables.inc): pointer to the level's room-flag table (2 bytes)
constexpr uint16_t kAttrsA = 0x687E, kAttrsB = 0x68FE, kAttrsE = 0x6A7E, kAttrsD = 0x69FE, kAttrsF = 0x6AFE;

// Object slots (the order they are updated in, Z_07.asm "@UpdateWeapons"): 0 Link, 1-12 monsters, then Link's weapons.
constexpr int kSlotSword = 0x0D;          // sword / rod
constexpr int kSlotShot = 0x0E;           // sword shot / magic shot
constexpr int kSlotBombA = 0x10, kSlotBombB = 0x11;   // bomb / fire (two of them: two bombs can be out)
constexpr int kSlotLastWeapon = 0x12;     // rod beam / arrow
constexpr uint8_t kStateBombFlash = 0x13;             // a bomb in this state toggles the PPU's grayscale bit
constexpr uint8_t kTypeMonsterSwordShot = 0x57;       // ObjType of a monster's sword shot
// ObjTypes that glow (UpdateObject_JumpTable, Z_07.asm ~5321: type = index): $3F guard fire, $40 standing fire, $55/$56 fireballs (Zora,
// Aquamentus, Gleeok, statues...), $57 monster sword shot, $58/$59 wizzrobe magic. A dungeon room is dark when AttrsE bit 7 is set.
constexpr uint8_t kTypeArmos = 0x1E, kTypePondFairy = 0x2F, kTypeItem = 0x60, kTypeDock = 0x61;   // (same table: UpdateArmos, UpdatePondFairy, UpdateItem)
constexpr uint8_t kTypeGuardFire = 0x3F, kTypeStandingFire = 0x40, kTypeFireballA = 0x55, kTypeFireballB = 0x56, kTypeMagicA = 0x58, kTypeMagicB = 0x59;
}
