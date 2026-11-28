#pragma once
#include "beyond.h"
#include "clouds.h"
#include "lighting.h"
#include "mist.h"
#include "nes.h"
#include "world.h"
#include "zelda_ram.h"
#include <cstdint>
#include <string>
#include <vector>

// How a frame is laid out on the canvas.
enum class Layout : uint8_t {
    Plain,   // the untouched 256x240 picture (title, file select, dungeons, ...)
    World,   // overworld gameplay: status bar on top, world around Link, camera centred on Link
    Boxed    // overworld, but inside a cave/shop/transition: status bar + the room centred, black around
};

// What kind of moment a frame is. The picture size only changes between *steady* moments, never during
// a transition animation (entering a cave, stairs, scrolling between dungeon rooms, ...).
enum class Kind : uint8_t {
    NonGameplay,        // title, file select, continue screen, ...
    Overworld,          // an overworld screen: walking around, or walking in/out of a screen edge/cave/dungeon
    OverworldIntro,     // the curtain opening on the overworld screen after leaving a dungeon
    OverworldOdd,       // overworld mode, but the screen doesn't match our decode (fallback: centred box)
    Interior,           // steady play inside a cave, shop, cellar or dungeon
    Menu,               // the inventory screen
    GameOver,           // the black "GAME OVER" screen at the end of the death sequence
    Cave,               // inside an overworld cave or shop (the game's modes $B/$C), including walking in and the fades
    Transition          // any other gameplay moment: cave/stairs animations, fades, dungeon scrolls, death...
};

// What the game was doing on a given frame (read from emulated RAM; addresses from the disassembly).
struct ViewSnapshot {
    uint8_t mode = 0, sub = 0, level = 0, room = 0, linkX = 0, linkY = 0, slot = 0, updating = 0, exitType = 0;
    bool menu = false;
    bool caveSteady = false;    // (Kind::Cave) Link is in the cave and under the player's control
    bool inOverworld = false;   // an overworld gameplay mode (room id is meaningful)
    Kind kind = Kind::NonGameplay;
};

// Link's centre in world pixels (the overworld is 16x8 screens of 256x176 pixels).
void linkWorldCenter(const ViewSnapshot& s, int& cx, int& cy);

// The finished image. Its size changes with the view height and the window shape.
struct Canvas {
    int w = Ppu::W, h = Ppu::H;
    std::vector<uint32_t> pixels;
};

// Extra pictures the front-end draws on top of the canvas at the window's own resolution, each at the largest whole
// number scale that fits (so they keep crisp pixels and a size that doesn't depend on the canvas zoom).
struct OverlayImage {
    std::vector<uint32_t> pixels;           // 0xAARRGGBB
    int w = 0, h = 0;
    float anchorX = 0.5f, anchorY = 0.5f;   // the point of the window the image is attached to (0..1)
    float pivotX = 0.5f, pivotY = 0.5f;     // the point of the image that sits on the anchor (0..1)
    float maxWidthFrac = 1.0f, maxHeightFrac = 1.0f;   // the scaled image must fit within this part of the window
    int heightPerScale = 0;                 // if > 0: scale = window height / heightPerScale, ignoring the fractions
    bool sharpScale = false;                // with heightPerScale: allow a fractional scale (blown up by a whole number, then shrunk smoothly)
};

struct Overlay {
    float dim = 0.0f;                       // 0..1: darken the whole picture underneath
    std::vector<OverlayImage> images;
    std::vector<uint32_t> mist;             // 0xAARRGGBB clouds over the whole window (HUD included), stretched to it
    int mistW = 0, mistH = 0;
    bool active() const { return dim > 0.0f || !images.empty() || mistW > 0; }
};

class Widescreen {
public:
    static constexpr int MIN_VIEW_HEIGHT = 176, MAX_VIEW_HEIGHT = 704;

    bool init(Nes& nes, std::string& error);
    bool available() const { return map.ready(); }

    // Call before every runFrame(). On the overworld with the widescreen on, Link may swing his sword next to any screen
    // border (the game masks the A button there, which the wide picture makes pointless); everywhere else the game's own
    // border limits are restored. Patches four bytes of the loaded PRG (BorderBounds, third set).
    void prepareFrame(Nes& nes);
    // Call after every runFrame(). While the sword restriction is lifted, a sword beam that would start at a screen border
    // (Link facing it within the game's old limit) is removed: it would hit the border at once and splash there.
    void afterFrame(Nes& nes);

    Overlay overlay;            // filled by every render() call, drawn by the front-end on top of the canvas

    // Moves the clouds one frame and shades canvas rows [top, h) (canvas (x, y) = world (camX + x, camY + y - top)); (cx, cy) is
    // the world point the camera looks at (where the first cloud starts).
    // The death fade (mode $11) only rewrites the bottom half of the background palette (AnimateWorldFading), so everything that
    // uses a colour of the top half (white gravestones, for example) would stay bright: those colours fade by the same amount as
    // the bottom half. Link's own pixels (OAM 18/19) keep the game's colours. (roomX, roomY) = the game's room on the canvas.
    void fadeDeathTopHalf(Nes& nes, Canvas& out, int roomX, int roomY);
    void applyClouds(Canvas& c, int top, int camX, int camY, int cx, int cy);
    static constexpr int kWaterMargin = 96;                                     // rows of terrain above the picture that the reflections can read
    void applyWater(Canvas& c, int top, int camX, int camY);                    // a picture that did not come through drawWorld
    struct WaterObj { int x, y, feet; bool flip; };                            // a sprite pixel (canvas) and its object's feet row; flip = mirrored (false for arrows, boomerangs, beams)
    struct WaterJob { uint32_t* px; int w, h, top, camX, camY, cx0, cy0, cx1, cy1; std::vector<uint8_t>* mask; bool computeMask, objects; const std::vector<WaterObj>* extra = nullptr; bool scenery = true; const std::vector<uint32_t>* rawSrc = nullptr; int firstRow = -1; int rowShift = 0; };   // firstRow: first row that is scanned (default: top); rows above top are margin above the picture; rowShift: the margin rows that px does NOT hold (an objects-only job on the canvas itself: px[0] is row rowShift)
    void waterPass(const WaterJob& j);
    void buildWaterWorld();
    void ensureWaterWorld();                                                    // (builds it again when the game's screen data changed)
    uint64_t wwSig = 0; uint32_t dbgBlue = 0;
    int dbgLink[4] = {0, 0, 0, 0};                                              // (Link's position, scenery-or-object, distance to the water: for water_debug.txt)
    void dumpWaterDebug(const char* path);                                       // Home key: tile ids and water flags of the current screen
    std::vector<uint8_t> wwKind;                                                // per world tile (512 x 176): 1 water, 2 big body, 4 reflects whole
    bool wwReady = false, waterDone = false;                                    // waterDone: this picture's water was drawn by drawWorld (and the game's room by pass B)
    // The world beyond the map (widescreen_beyond.cpp): mountains/trees repeat outwards, sea goes on, hand-made rooms from <rom>.beyond.txt.
    int fadeMode = 0;                                                           // B key: how far the extension reaches (beyond::fadeFor)
    bool extendWorld = true;                                                    // (tests of the older sections switch it off)
    beyond::File beyondFile = beyond::builtin(); unsigned beyondEpoch = 0;
    std::vector<uint16_t> ringTile; std::vector<uint8_t> ringKind;              // the band around the map (RW x RH tiles): id | palette << 8, water-pass flags
    uint64_t ringSig = 0; bool ringReady = false; long long beyondMtime = -1;
    std::string loadBeyond(const std::string& text);                            // parses the file's text; "" or the error message
    bool loadBeyondFile(const std::string& path, std::string& err);             // (re)reads the optional file when it changed (modification time); without a file the built-in layout is used
    void ensureBeyond(Nes& nes);                                                // builds the band again when the border screens, palettes or the file changed
    uint16_t ringTileAt(int gx, int gy) const;                                  // world tile coordinates; 0xFFFF = no data (inside the map or too far out)
    uint8_t gridKind(int gx, int gy, bool* valid = nullptr) const;              // the water pass's flags of a world tile: the map's grid, else the band
    bool dumpEdges(const char* path);
    bool dumpSquares(const char* txtPath, const char* bmpPath);                 // (Delete key) the game's squares as a labelled picture and as text
    const OverworldMap& overworldMap() const { return map; }                    // (tests)
    void setDiscoveredForTest(int slot, int room, bool on) { if (on) fogBits[slot][room >> 3] |= (uint8_t)(1 << (room & 7)); else fogBits[slot][room >> 3] &= (uint8_t)~(1 << (room & 7)); }
    std::vector<uint32_t> wSrcBuf;                                              // waterPass: the picture as it was
    std::vector<int16_t> wAxis;                                                 // waterPass scratch (mirror axis per pixel)
    std::vector<uint8_t> wObjSkip, wLinkMark;                                   // (more waterPass scratch: kept between frames so their memory is reused, not given back and asked for again)
    std::vector<int> wIA, wIB;                                                  //  (the big-water tables)
    std::vector<uint32_t> wExt;                                                 //  (the copy pass B works on)
    std::vector<uint32_t> wRawSpare; std::vector<uint16_t> wVis;                 // (drawWorld) the buffers of pass A, kept between frames (terrainRaw and wRawSpare trade places each frame)
    std::vector<uint32_t> terrainRaw;                                           // pass A's plain terrain (no fog, no effects on land): the reflections' source outside the current screen
    std::vector<uint8_t> terrainMask; int terrainW = 0, terrainH = 0, terrainCamX = 0, terrainCamY = 0;   // (mask and raw have kWaterMargin extra rows on top)   // pass A's water mask of the picture
    bool fxIsShadow = false;                                                    // (the pass runs on a shadow game's screen: no Link, no weapons)
    Nes* fxNes = nullptr; int fxSlot = 0, fxRoom = -1, fxOx = 0, fxOy = 0; unsigned waterTick = 0;   // (set by render for the effects that look up the world)
    int backdropCamX = 0, backdropCamY = 0, backdropTop = 0;   // the camera the cave backdrop was drawn with
    CloudField clouds;          // the cloud shadows (world coordinates), stepped once per overworld frame
    bool cloudsReady = false;

    // Day/night cycle and dynamic lights (widescreen_light.cpp). The overworld darkens at night around Link's sight; fire, shots,
    // explosions and the secret armos glow; dungeon rooms that are not dark get a torch in each corner.
    lighting::Clock dayClock;
    std::vector<lighting::Source> lightSources;   // glowing things of the picture, in the space of the call (world pixels on the overworld, room pixels in dungeons)
    float lightLinkX = 0, lightLinkY = 0;         // Link's centre in the same space (his sight)
    float backdropLinkX = 0, backdropLinkY = 0;   // ... as it was when the cave backdrop was kept
    float steadyLinkX = 0, steadyLinkY = 0; bool steadyValid = false; int steadyJump = 0;   // Link's sight follows the camera (his steady overworld place), not the raw coordinates
    // Dungeon lighting: dungeonDim = how much the room is dimmed (0 untouched .. 1), dungeonLit = how far the torches are lit
    // (0: dark room with unlit torches .. 1); caveDim = the same for a cave or cellar.
    float dungeonDim = 0, dungeonLit = 0, caveDim = 0;
    bool restDark = false, wasScroll = false; float litOld = 0; int candleHeld = 0;   // the room at rest while sliding into the next one
    // The candle's fire: its true place (the game's 8-bit coordinates wrap at the borders), see trackFire.
    struct FireTrack { bool on = false; int tx = 0, ty = 0, lastX = 0, lastY = 0; int drawX[2] = {}, drawY[2] = {}, overX[2] = {}, overY[2] = {}; bool act[2] = {}; };
    FireTrack fireTrack[2];
    void trackFire(Nes& nes);
    void maskCandleAtBorder(Nes& nes);
    struct Sparkle { float x, y, vy; unsigned born; int life; int kind; };   // kind 0 pink (fairies), 1 white (boomerang / arrow trails), 2 gold (triforce), 3 red (heart), 4 amber (rupee), 5 blue (5 rupees)
    std::vector<Sparkle> sparkles;                 // the fairies' pink sparkles
    uint32_t sparkRng = 1;
    void spawnSparkles(float x, float y, int kind);
    bool spritesNearWater(const Nes& n, int room) const;   // does a sprite of a shadow's OAM have water within reach of its reflection (the shadows' water pass runs only then)
    const uint8_t* roomTilesFor(Nes& nes, int room) { return roomTiles(nes, room); }                    // (tests) a screen's tiles as drawn: the decode with the armos that woke in its shadow gone
    void testSetStatue(int room, int col, int row) { Cached& c = cache[room]; c.nStatue = 1; c.statue[0] = (uint16_t)(col << 8 | row); for (int dc = 0; dc < 2; dc++) for (int dr = 0; dr < 2; dr++) c.tiles[(col + dc) * OverworldMap::TILE_ROWS + row + dr] = (uint8_t)(0xC0 + dc * 2 + dr); }   // (tests) pretend the decode has a statue there
    int lightRoom = -1;                            // the screen the sparkles belong to
    void drawSparkles(uint32_t* pix, int w, int h, int top, int camX, int camY, const Ppu& ppu);
    std::vector<uint8_t> curtainMask;                 // OverworldIntro: 1 where the curtain is open (the lighting skips the rest)
    void gatherLights(Nes& nes, const ViewSnapshot& v, int ox, int oy, int slot, bool overworld, bool objects = true);
    void gatherStaticLights(Nes& nes, int room, int slot, bool live, int ox, int oy);   // armos statues of a screen and the explored ones around it (nothing that moves)
    const Ppu* lightPpu = nullptr;                                     // (to keep sparkle trails under the sprites)
    bool roomItemTaken(Nes& nes);   // the room's item flag is set (a triforce shard or heart container that was taken stays dark)
    int lastOverRoom = -1; bool cellarBlackSeen = false;               // the last overworld screen (for the statues' glow while a dungeon is entered); a cellar's own picture has appeared
    void gatherWareLights(Nes& nes, int ox, int oy, bool cave);
    void itemGlow(int st, float x, float y, int s, bool sparkle);                      // glow (and glitter) of an item on the ground
    bool lootGlow(int id, float x, float y, bool sparkle);                             // the glow and glitter of a fairy, triforce shard, heart container, heart or rupees by item id
    void addArmosLight(float x, float y, bool secret, float phase);                    // an armos' halo: blue, the secret one red
    void gatherWeaponLights(const uint8_t* ram, int ox, int oy, bool sparkle, bool tracked);   // what Link has out (real screen or a shadow): shot, trails, bombs, fire
    void gatherShadowLights(int curRoom, int slot, bool sparkle, bool withBridge = false);                      // N8: glow and glitter of the items, fairies and flames on the shadow games' screens
    int roomAge = 0, roomPlayAge = 0, lightSpace = -1, sparkOx = 0, sparkOy = 0;        // frames on this screen; 0 overworld / 1 cave / 2 dungeon; where the sparkles' space starts
    int slideX = 0, slideY = 0, prevSx = 0, prevOy = 0;                 // how far the room has slid (mode 7), counted frame by frame
    void caveLight(Nes& nes, uint32_t* pix, int w, int h, int top, int bottom, int linkX = -1, int linkY = -1);   // (linkX/Y: Link's place when the game has already moved him: the cellar exit)
    void applyNightLight(Canvas& c, int top, int camX, int camY);
    void applyDungeonLight(Nes& nes, Canvas& c, const ViewSnapshot& v, int top, int bottom);

    bool enabled = true;        // F3
    bool fog = true;            // F2 (off = reveal the whole map)
    bool unfogAnim = true;      // a newly entered screen is revealed by a growing egg-shaped patch from where Link entered (off = instantly)
    bool verify = false;        // --verify: log decoder-vs-game comparisons
    bool trace = false;         // --trace: log every change of game mode / layout (for debugging flicker)
    bool caveBox = true;        // F9: caves and shops appear in a box over the overworld instead of replacing the picture
    void markExplored(int slot, int room) { markDiscovered(slot, room); }   // (fog: the screen counts as explored)
    class ShadowSet* shadows = nullptr;   // N: the shadow games of the neighbouring screens, drawn into the margins (set by main.cpp)
    bool lighting = true;       // F1: day/night cycle (T = skip a quarter of the day) and dynamic lights
    int currentScreen() const { return fxRoom; }   // the overworld screen (row * 16 + column) of the last frame, -1 before the first
    bool waterDirectObjects = true;               // (tests) a shadow's objects-only water pass works on the canvas itself; false = the old way through a copy (the results are the same)
    bool waterReflections = true;   // END: reflections of the shore, walls, Link, monsters and shots in the water
    bool waterWaves = true;         // PAGE DOWN: waves on big lakes and the sea
    bool waterOn() const { return waterReflections || waterWaves; }
    void setWater(bool on) { waterReflections = waterWaves = on; }
    bool nesLimits = false;     // INSERT: the NES's own limits (sprites flicker when more than 8 share a line; the game slows down when its logic takes longer than a frame); off = neither
    bool smoothScroll = true;   // dungeon scrolls up/down ease between the game's 8 px steps (2 px a frame, like sideways)
    bool cloudShadow = true;    // F12: clouds drift over the overworld and shade the picture (Link and monsters included)
    bool hudOverlay = true;     // F7: the status bar as a transparent overlay in the corners (off = the original bar on top)
    int  minimapStyle = 2;      // F8: 0 = the original grey map, 1 = discovered screens in blue, 2 = a scaled-down picture of the world
    bool interiorsBig = true;   // F6: caves, shops and dungeons use the big full-size picture (after the transition)
    int  viewHeight = 448;      // world pixels visible below the status bar (176 = one screen tall)

    void adjustZoom(int delta);     // +/- keys: change viewHeight by `delta` pixels (clamped)
    void cycleZoom();               // F4: step through a few presets

    // Tell the compositor that emulation was fast-forwarded, so the previous frame's state is stale.
    void resetLag() { lagReset = true; }
    int cameraX() const { return smoothX; }  int cameraY() const { return smoothY; }   // (the eased camera centre in world pixels)
    void drawFadedSprite(Nes& nes, Canvas& out, uint8_t tile, int sx, int sy, int fade256) {          // (tests: one sprite at brightness fade256/256)
        const FlashEffect none; spriteFade = fade256; drawSpriteAt(nes, out, tile, 0, sx, sy, 0, 0, 0, none, 0, 0); spriteFade = 256;
    }
    bool backdropKept() const { return haveBackdrop; }  int backdropCameraX() const { return backdropCamX; }   // (the picture kept behind a cave's box, and the camera it was drawn with)
    void snapCamera() { smoothValid = false; }     // next world frame uses the exact camera position (no easing)

    // Builds the final image for a window with the given width/height ratio. Call once per emulated frame.
    void render(Nes& nes, double windowAspect, Canvas& out);

    // Screen-to-screen glide: after the emulator fast-forwarded a scroll to the next overworld screen (the game puts
    // Link ~16 pixels forward at the end), call startGlide(); the following render() calls then show Link walking
    // those pixels with the camera following him, without running the emulator. Poll gliding() to know when to resume.
    void startGlide(Nes& nes);
    bool gliding() const { return glideActive; }
    // Link's walk cycle during the glide: the game's own two poses (ObjAnimFrame 0/1, probed on a clone of the game by
    // `probeLinkPoses`) shown in turn, the frame flipping every 6 glide frames like the game's ObjAnimCounter.
    struct LinkPose { uint8_t tile[2] = {0, 0}, attr[2] = {0, 0}; int dx = 8, dy = 0; };   // dx/dy: the right sprite relative to the left
    bool     glideAnimate = false;
    LinkPose glidePose[2];
    int      glideAnimC = 6, glideAnimF = 0;                  // frames until the next flip, the pose shown
    static constexpr int kProbeMaxFrames = 150;               // the probe's clone plays at most this long to reach mode 5
    bool probeLinkPoses(const Nes& nes);
    static void glideAnimStep(int& counter, int& frame);      // one game frame of Link's animation counter (DEC; at 0 flip and reload 6)
    bool mazeFading() const { return mazePhase != 0; }                          // the mist closes, covers or lifts: the game waits (main.cpp)
    bool keepScroll(const Nes& nes) const;                                      // the scroll to the next screen stays the game's own (from a maze room into itself)
    bool mazeSeen(int slot, int room) const { return fogBits[slot][room >> 3] & (1 << (room & 7)); }   // on the map (a maze room: solved at least once)
    bool unfogging() const { return unfogRoom >= 0; }     // an unfogging animation is running
    int  unfogFrame() const { return unfogT; }

    // Fog-of-war persistence (three save slots, matched by player name).
    bool loadFog(const std::string& path);
    void saveFogIfDirty(const std::string& path);   // writes the committed fog only (see commitFog)
    // The fog is only written to the file when the game itself saves (mode $0D, detected by render()); discoveries made
    // since stay in memory for the session and are dropped when a file is loaded from the file-select screen.
    static bool menuOpenForSave(const Nes& nes);    // the inventory is fully open (SELECT there = SAVE AND QUIT)
    // SAVE AND QUIT asks first: SELECT in the open inventory shows a box (YES / NO) in the middle of the window. `pressed` = the buttons newly pressed
    // this frame. Left/right pick, A confirms (NO closes the box), B closes it too. Returns true when YES was confirmed (the front-end then fades and saves).
    bool saveConfirmInput(const Nes& nes, uint8_t pressed);
    bool saveConfirm = false, saveConfirmYes = false;   // the box is open; YES is the highlighted answer (NO is the default)
    void requestSave(Nes& nes);              // starts the game's own save (mode $0D) from gameplay; false-safe in other modes

    struct ObjSnap { uint8_t st[20], x[20], y[20], type[20]; };   // object slots 0-19 (state, X, Y, type)
    // Stairs on the ground (widescreen_stairs.cpp): Link comes out of them on the square he went in by.
    bool stairValid = false, stairLock = false, stairAttrsOn = false;
    int stairRoom = 0, stairX = 0, stairY = 0, stairPrevMode = 0, stairPrevLevel = 0, stairAttrRoom = 0;
    uint8_t stairSavedA = 0, stairSavedF = 0;
    void stairsPrepare(Nes& nes);                   // before a frame (prepareFrame)
    void stairsAfter(Nes& nes);                     // after a frame (afterFrame)
    bool stairsExitSpot(const Nes& nes, int room, int& x, int& y) const;   // where Link will come out of the stairs of this room (false = the game decides)
private:
    friend void section26(struct Ctx&);   // (tests of the sprite fixes look at the plan)
    friend void section27(struct Ctx&);
    friend void section28(struct Ctx&);
    OverworldMap map;
    ViewSnapshot prev;          // state that produced the frame currently in the PPU framebuffer
    bool lagReset = false;
    bool heldInterior = true;   // picture size currently in use: true = big interior picture, false = overworld view
    int  videoOffFrames = 0;
    int  heldCamX = 0, heldCamY = 0;     // camera centre of the last normal overworld frame
    bool haveHeldCam = false;
    int  smoothX = 0, smoothY = 0;       // eased camera centre (hides jumps when the game repositions Link)
    bool smoothValid = false;
    int  lastTraceMode = -1, lastTraceKind = -1, lastTraceLayout = -1;
    int lastVerifiedRoom = -1;

    struct Cached { int key = -1; bool anySecret = false; int nStatue = 0; uint16_t statue[16] = {}; uint8_t tiles[OverworldMap::TILES]; uint8_t secret[OverworldMap::TILES]; };   // statue: the armos squares (col << 8 | row) of the plain decode; key bits 9+: which of them woke in the screen's shadow
    void gatherArmosTiles(const uint8_t* tiles, int room, float ox, float oy, const Cached* known = nullptr);   // (known: the screen's statue squares are already listed, no need to look at all its tiles)   // blue glow on the armos statues of a screen (tiles until they wake), red on the secret one
    Cached cache[128];

    uint8_t fogBits[3][16] = {};
    uint8_t fogName[3][8] = {};
    bool fogNameValid[3] = {false, false, false};
    uint8_t fogSaved[3][16] = {};       // fog as of the last game save (what the file holds)
    uint8_t fogSavedName[3][8] = {};
    bool fogSavedValid[3] = {false, false, false};
    int  lastMode = -1, lastSub = -1;                 // game mode of the previous rendered frame
    bool fogDirty = false;              // the committed fog changed: the file needs writing
    void commitFog(Nes& nes, int slot);
    void revertFog();

    // glide state
    bool glideActive = false;
    int  glideT = 0, glideN = 0, glideAx = 0, glideAy = 0, glideBx = 0, glideBy = 0;
    ViewSnapshot glideView;
    Layout lastLayout = Layout::Plain;
    int  topBand = 64;                  // canvas rows above the world: 64 for the original status bar, 0 with the HUD overlay
    // Scaled-down pictures of the overworld screens for the minimap, drawn with the overworld's own palette.
    static constexpr int THUMB_W = 8, THUMB_H = 6;
    struct Thumb { int key = -1; uint32_t px[THUMB_W * THUMB_H]; };
    Thumb thumbs[128];
    uint8_t refPalette[16] = {};        // background palette of the last normal overworld frame
    uint32_t thumbSig = 0;
    int  refLuma = 0;                   // brightness of that palette (to fade the fog along with the picture)
    std::vector<OverlayImage> lastHud;  // the overlay HUD of the last frame (kept while the game blanks the picture)

    void addHudOverlay(Nes& nes, const ViewSnapshot& view, int slot);
    void buildCaveOverlay(Nes& nes);
    bool lastWasCaveBox = false;
    // Caves: the overworld picture as it was just before Link went in, kept as the backdrop behind the cave's box.
    Canvas backdrop;
    bool haveBackdrop = false, outIsBackdrop = false, caveSession = false;
    OverlayImage lastCaveImage;
    bool haveCaveImage = false;
    struct FlashEffect { bool on = false; int cx = 0, cy = 0; };       // the bomb "flash": world position of the bomb
    void blitRoom(Nes& nes, Canvas& out, int roomX, int roomY, int minY, const FlashEffect& fl, int worldX0, int worldY0);
    void blitRoomBackground(Nes& nes, Canvas& out, int roomX, int roomY, int minY);
    // Per-frame fixes for sprites the PPU draws wrongly for a wide picture (planned once per frame by planSpriteFixes):
    //  - hideSprite[i]: OAM entry i is not drawn from the game's picture (Link's stale sprites after a cave; a sprite whose X
    //    wrapped around the 8-bit OAM range and so shows up at the opposite border);
    //  - trueSprites: those wrapped entries, to be drawn at the position the game meant (outside the picture's border).
    int spriteFade = 256;               // 0..256: brightness of the sprites drawn by drawSpriteAt (the shadows' monsters fade with the game's picture)
    int owRefLuma = 0;                  // palette brightness of the last frame of overworld play (the fade of stairs and caves is measured against it)
    bool hideSprite[64] = {};
    struct TrueSprite { int oam, sx, sy; };
    TrueSprite trueSprites[64];
    int trueSpriteCount = 0;
    long borderBoundsOff = -2;          // PRG offset of the third BorderBounds set (-2 = not searched yet, -1 = not found)
    Kind lastKind = Kind::NonGameplay;  // kind of the last rendered frame (for prepareFrame)
    uint8_t savedHearts[2] = {0, 0};    // HeartValues / HeartPartial at requestSave (the game overwrites them with full hearts)
    bool heartsPending = false;
    // Object slots 0-19 as the last two planSpriteFixes calls saw them: [0] now, [1] one frame earlier = the frame the game's picture
    // belongs to (see 'How a frame works'): a splash or beam that ended this frame is still in the picture.
    ObjSnap objSnap[2] = {};
    long heartsPatchOff = -2; bool heartsPatched = false;   // PRG offset of the file-select hearts code (see patchSaveSlotHearts)
    void patchSaveSlotHearts(Nes& nes);
    int spawnRoom = -1, spawnSlot = 0, spawnLevel = 0;  // overworld room a dungeon was entered from, when the game was saved inside it (-1 = none)
    long beamBoundsOff = -2;            // PRG offset of the sword beam's X check (-2 = not searched yet, -1 = not found)
    uint8_t shotPrev = 0;               // ObjState of the sword-shot slot after the last frame (a fresh beam = 0 -> non-zero)
    bool borderFree = false;            // the sword restriction is currently removed
    int weaponLinger = 0;               // frames left in which one of Link's weapons counts as out (the picture lags the game's RAM by a frame)
    void planSpriteFixes(Nes& nes, const ViewSnapshot& view, bool hideLinkSprites);
    void drawShadowSprites(Canvas& out, int camX, int camY, int topBand, int curRoom, int slot, bool onlyCurrent = false);   // monsters, fairies and items of the shadow games' screens
    void drawSpriteAt(Nes& nes, Canvas& out, uint8_t tile, uint8_t attr, int sx, int sy, int roomX, int roomY, int minY, const FlashEffect& fl, int wx0, int wy0, int oamIdx = -1);
    void drawTrueSprites(Nes& nes, Canvas& out, int roomX, int roomY, int minY, const FlashEffect& fl, int wx0, int wy0);
    void drawSplashes(Nes& nes, Canvas& out, int roomX, int roomY, int minY, const FlashEffect& fl, int wx0, int wy0);
    void drawOverhang(Nes& nes, Canvas& out, int roomX, int roomY, int minY, const FlashEffect& fl, int worldX0, int worldY0);
    OverlayImage buildMinimap(Nes& nes, const ViewSnapshot& view, int slot);
    void bakeThumbs(Nes& nes, int slot);
    void bakeThumb(Nes& nes, int room);
    int  thumbKey(Nes& nes, int room);
    void buildMenuOverlay(Nes& nes);
    void buildGameOverOverlay(Nes& nes);
    void drawWorld(Nes& nes, Canvas& out, int camX, int camY, int slot, const FlashEffect& fl);
    void drawLinkSprites(Nes& nes, Canvas& out, const ViewSnapshot& v, int camX, int camY, int originX, int originY, const LinkPose* pose = nullptr);
    void renderGlide(Nes& nes, double windowAspect, Canvas& out);
    ViewSnapshot capture(Nes& nes);
    bool nametableShowsRoom(Nes& nes, int room);
    const uint8_t* roomTiles(Nes& nes, int room);
    // Hidden secrets of an overworld screen (OverworldMap's Secret kinds per tile), or nullptr when it has none that apply:
    // none left hidden, or the secret belongs to the other quest (AttrsF bits 6-7, see IsQuestSecretMismatch).
    const uint8_t* roomSecrets(Nes& nes, int room);
    const uint8_t* blitSecrets = nullptr;   // roomSecrets of the room the game's picture shows (set by render before blitRoom)
    // The fog bits of a maze room mean "solved" (the map shows it); its fog in the overworld never lifts.
    bool discovered(int slot, int room) const { return !zelda::isMazeRoom(room) && (fogBits[slot][room >> 3] & (1 << (room & 7))); }
    void markDiscovered(int slot, int room);
    void discoverRoom(int slot, const ViewSnapshot& v);   // markDiscovered + start of the unfogging animation for a new room
    void tickUnfog();                                      // once per rendered frame
    bool fogCache = true;               // keep the per-pixel fog glow of each room between frames (off in the tests that compare)
    uint64_t fogEpoch = 0;              // what the cached glow was made for (explored rooms + screen data)
    std::vector<uint16_t> fogVisMap[128];
    // Lost Woods / Lost Hills: the game's single screen at its original size behind a waving gray haze; entered and left through cloud-like mist.
    // When the screen changes the mist grows over the picture (phase 1: closing, then 2 seconds covered, the game waits) and lifts again over
    // the other presentation (phase 2, the game still waits).
    bool mazeShown = false;             // the maze presentation is on
    int  mazePhase = 0, mazeT = 0;      // 1: frozen picture under the mist (the game waits); 2: the new picture, the mist lifting
    bool mazeNext = false, mazeFxOk = false;
    float mistP = 0.0f;                 // how much of the window the mist covers (0..1)
    unsigned mistTick = 0;
    int  mazeLastRoom = -1;             // the screen before this one (a correct way out solves the maze)
    Canvas mazeFrozen;
    Mist mist;
    static constexpr int kMazeRamp = 70, kMazeHold = 120, kMazeMistOut = 80;     // frames: closing (from nothing), covered (2 s), lifting
    void dungeonWater(Nes& nes, Canvas& c, int top, int left, int dx, int dy, bool trans, int fade);   // the moat ($F4 tiles) of a dungeon room; also draws the frozen sprites of a transition (widescreen_dungeon.cpp)
    static int roomKindsLive(Nes& nes, uint8_t out[704]);                      // the room's water/wall flags from PlayAreaTiles (returns the number of moat tiles)
    // What the game hides while a dungeon room scrolls or the stairs fade (HideAllSprites): the monsters, items and lights of the last frame of play are kept
    // and drawn again, frozen, on the room that is leaving (scrolled with it; on the stairs they stay until the picture is black). widescreen_dungeon.cpp.
    struct FrozenPix { int16_t x, y, feet; uint32_t argb; bool flip; };        // a sprite pixel (room px, status bar not counted) and its object's feet row (< -500: no reflection)
    std::vector<FrozenPix> frozenSpr; std::vector<lighting::Source> frozenLights; std::vector<uint8_t> frozenGrid;
    std::vector<uint32_t> frozenTerrain;    // the room's picture at the last frame of play without sprites (256 x 176): the reflections' source above the screen while the room scrolls up/down
    int frozenLinkX = 0, frozenLinkY = 0;   // Link's place in the picture of the last frame of play (the cellar exit's light; his reflection is left out of the replayed water)
    std::vector<uint32_t> frozenWaterPix;   // the water effect of the last frame of play (256 x 176; 0 = the pixel was not changed): the room that scrolls away keeps exactly this look
    bool frozenWaterWant = false;           // (set by recordDungeonFrozen: the next dungeonWater at rest records it)
    uint32_t moatColor = 0;                 // the moat's colour (last found): the next room's water is found by it while it scrolls in
    void pictureWaterGrid(const Canvas& c, int top, int left, int dy, uint8_t out[704]) const;   // the room that scrolls in from above: its moat tiles from the picture (the game has not laid it out)
    int frozenLuma = 0, dunRefLuma = 0, frozenAge = 0; bool frozenOn = false, frozenDone = false;      // (dunRefLuma: the room's palette brightness on the last frame of play)
    int candleIdx = -1, baseLightN = 0, lightShiftX = 0, lightShiftY = 0;     // (gatherLights: where the candle's light is, how many sources come before the sparkles', sparkles' shift)
    void recordDungeonFrozen(Nes& nes, const Canvas& c, int top, bool cellar = false);
    void drawDroppedSprites(Nes& nes, Canvas& c, int top, bool skipLink);   // the sprites the PPU left out (more than 8 on a line), widescreen_door.cpp
    void spritesBeyondRoom(Nes& nes, Canvas& c, int top);   // sprites past the room's top/bottom edge, drawn into the margin (widescreen_door.cpp)
    void linkAtDoors(Nes& nes, Canvas& c, int top, int dx, int dy, bool scrolling);   // Link goes into the doorways (in front of the passage, behind the frame / the wall around a hole), see widescreen_door.cpp
    struct SparkSpot { float x, y; int kind; };                                // where the glitter is made (the things that glitter), recorded on the last frame of play and replayed during a transition
    std::vector<SparkSpot> sparkSpots, frozenSparkSpots; bool sparkReplay = false;
    bool frozenCellar = false;                                                 // the recorded frame is a cellar's
    bool staleObjects = false;                                                 // a transition began and the new room has not started to play: the objects in RAM are the old room's
    void drawFrozenSprites(Canvas& c, int top, int dx, int dy, int fade);        // fade 0..256 (the stairs' fade to black)
    static int roomLuma(const Canvas& c, int top);
    std::vector<uint8_t> dungeonKind; bool wwDungeon = false;     // (the room's grid, swapped with wwKind while the pass runs)
    void roomWater(Canvas& c, int camX, int camY, bool objects, bool scenery = true);   // pass B: the water effects on the game's own picture of this screen
    // What the cave box's backdrop needs to keep its water alive: the terrain before the water, the fog share of every pixel, the room's own picture before
    // pass B (recorded while a backdrop is made); `redoBackdropWater` runs the water passes on them again every frame.
    bool recordShare = false, backdropWaterOk = false;
    std::vector<uint32_t> backdropRawPre, backdropRoomPre; std::vector<uint16_t> backdropVis;
    int backdropFogScale = 256, backdropRoomRect[4] = {0, 0, 0, 0}, backdropFx[2] = {0, 0};
    void applyMazeFx(Canvas& c);
    void redoBackdropWater(Canvas& c);               // the water of the cave box's backdrop (waves, reflections) keeps moving
    void renderCore(Nes& nes, double windowAspect, Canvas& out);
    struct ShadowPlan { bool scene = false, takeover = false, bridge = false; int cur = -1, fade = 256; };   // N: what is drawn of the neighbour screens with this picture
    ShadowPlan planShadows(const Nes& nes, const ViewSnapshot& view, Layout layout, Kind kind);
    void traceShadows(const ViewSnapshot& view, bool shadowScene) const;
    void publishWantedScreens(const ViewSnapshot& view, int camX, int camY, int W, int canvasH);
    void shadowBombFlash(FlashEffect& flash, const ViewSnapshot& view, int slot) const;
    int  unfogRoom = -1;                // room being revealed (-1 = none); it counts as undiscovered for drawing until the animation ends
    int  unfogT = 0;                    // frames elapsed
    int  unfogX = 0, unfogY = 0;        // where Link entered, inside that room (world pixels)
    double unfogRadius = 0;             // current size of the clear patch (see updateUnfogRadius)
    int  blitRoomId = -1;               // room the game's picture shows (set by render before blitRoom)
    void updateUnfogRadius();
    int  revealAmount(int lx, int ly) const;   // 0..256: how clear the unfogging room is at (lx, ly) inside it
    void syncFogSlot(Nes& nes, int slot);
};
