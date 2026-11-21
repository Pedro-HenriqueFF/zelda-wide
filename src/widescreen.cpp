// Widescreen: setup, zoom, and the frame composition (render / renderCore). The game state (capture), the decoded-screen cache and the fog
// discovery are in widescreen_state.cpp.
#include "widescreen_internal.h"
#include "shadow.h"
#include "perf.h"

void linkWorldCenter(const ViewSnapshot& s, int& cx, int& cy) {
    // ObjX/ObjY are the top-left of Link's 16x16 sprite; the play area starts 64 pixels down the screen.
    cx = (s.room & 15) * ROOM_W + s.linkX + 8;
    cy = (s.room >> 4) * ROOM_H + (s.linkY + 8 - HUD_H);
}

void Widescreen::adjustZoom(int delta) {
    viewHeight = std::clamp(viewHeight + delta, MIN_VIEW_HEIGHT, MAX_VIEW_HEIGHT);
}

void Widescreen::cycleZoom() {
    static const int presets[] = {176, 288, 448, 528};
    for (int p : presets) if (p > viewHeight) { viewHeight = p; return; }
    viewHeight = presets[0];
}

bool Widescreen::init(Nes& nes, std::string& error) {
    mist.init();
    return map.init(nes.cart.prgData(), error);
}


void Widescreen::render(Nes& nes, double windowAspect, Canvas& out) {
    mazeFxOk = false;
    renderCore(nes, windowAspect, out);
    if (mazeFxOk) applyMazeFx(out);
    if (enabled && mistP > 0.001f) {                                          // gray clouds over everything, the HUD included (drawn by main.cpp at window size)
        overlay.mistH = 270; overlay.mistW = std::clamp((int)std::lround(270 * windowAspect), 256, 960);
        mist.layer(overlay.mist, overlay.mistW, overlay.mistH, mistP, mistTick);
    }
}

// The maze's presentation: dimmed, with a waving gray haze (the mist of the transitions is a layer over the whole window, see render()).
void Widescreen::applyMazeFx(Canvas& c) {
    if (mazePhase == 2) { mistP = 1.0f - (float)mazeT / kMazeMistOut; if (++mazeT >= kMazeMistOut) { mazePhase = 0; mazeT = 0; mistP = 0.0f; } }
    if (mazeShown) mist.haze(c.pixels.data(), c.w, c.h, topBand, mistTick, 120, 70);
}

void Widescreen::renderCore(Nes& nes, double windowAspect, Canvas& out) {
    waterDone = false;
    overlay = Overlay();
    tickUnfog();
    if (glideActive) { renderGlide(nes, windowAspect, out); return; }
    // The frame in the framebuffer was drawn from the game state we saw one frame ago (the NMI
    // handler applies scroll/OAM that the previous update produced), so use the previous snapshot,
    // unless emulation was just fast-forwarded.
    ViewSnapshot cur = capture(nes);
    ViewSnapshot view = lagReset ? cur : prev;
    lagReset = false;
    prev = cur;
    if (!enabled || view.mode == kModeLoad || view.mode == kModeUnfurl || view.mode == kModeContinue || view.kind == Kind::GameOver || view.kind == Kind::NonGameplay) {
        mazeShown = false; mazePhase = 0; mazeLastRoom = -1;                      // (a new start: no mist)
    }
    if (view.inOverworld && view.level == 0) {                                    // the way out of a maze that only opens at the end of the sequence: solved
        if (mazeLastRoom >= 0 && view.room != mazeLastRoom &&
            ((mazeLastRoom == kForestMazeRoom && view.room == kForestMazeExit) || (mazeLastRoom == kMountainMazeRoom && view.room == kMountainMazeExit)))
            markDiscovered(view.slot < 3 ? view.slot : 0, mazeLastRoom);
        mazeLastRoom = view.room;
    }

    if (lastMode == kModeFileSelect && cur.mode != kModeFileSelect) revertFog();                // a file was chosen: its fog is the saved one
    const bool saveDone = cur.mode == kModeSave && cur.sub == 2 && !(lastMode == kModeSave && lastSub == 2);
    lastMode = cur.mode; lastSub = cur.sub;
    if (saveDone) commitFog(nes, cur.slot < 3 ? cur.slot : 0);
    const int slot = view.slot < 3 ? view.slot : 0;
    if (view.inOverworld) {                              // discovery is tracked even with widescreen switched off
        syncFogSlot(nes, slot);
        discoverRoom(slot, view);
    }

    // The inventory: leave the canvas exactly as the last gameplay frame left it (it is frozen anyway while the menu
    // is open) and put the menu in a box on top of it.
    if (enabled && view.kind == Kind::Menu && !out.pixels.empty()) {
        if (caveSession && haveCaveImage) {                    // inside a cave: the cave's box stays, dimmed, under the inventory
            OverlayImage under = lastCaveImage;
            for (uint32_t& p : under.pixels) p = scaleColor(p, 140);
            overlay.images.push_back(std::move(under));
        }
        buildMenuOverlay(nes);
        return;
    }

    // Caves and shops: the cave's own picture appears in a box over the overworld as it was when Link went in, like the
    // inventory, with the status bar around it. Nothing is shown of the fade to black: the overworld stays on screen
    // until the cave's picture is there, and then the box and the cave appear together. On the way out the box goes
    // when the cave fades away, and the overworld is simply back (the picture the game redraws takes over as soon as it
    // is on screen).
    Kind kind = view.kind;
    lastKind = kind;
    // Lost Woods / Lost Hills. The moment Link touches the border and the screen changes into or out of a maze the picture freezes under growing mist; after
    // 2 seconds fully covered the other presentation appears and the mist lifts, Link already there. The game waits for all of it. The maze rooms are the
    // game's single screen at its own size, without gliding; repeating the room is the game's own scroll.
    if (enabled) {
        mistTick++;
        const bool decides = kind == Kind::Overworld || kind == Kind::OverworldOdd || (kind == Kind::Transition && view.mode == kModeScroll);
        const bool wantMaze = decides && view.level == 0 && isMazeRoom(view.room);
        if (mazePhase == 0 && decides && wantMaze != mazeShown) {
            if (out.pixels.empty()) mazeShown = wantMaze;
            else { mazeFrozen = out; mazeNext = wantMaze; mazePhase = 1; mazeT = 0; }
        }
        if (mazePhase == 1) {
            out = mazeFrozen;
            const float r = std::min(1.0f, (float)(mazeT + 1) / kMazeRamp);
            mistP = r * r * (3 - 2 * r);
            if (hudOverlay) for (const OverlayImage& img : lastHud) overlay.images.push_back(img);
            if (++mazeT >= kMazeRamp + kMazeHold) { mazeShown = mazeNext; mazePhase = 2; mazeT = 0; mistP = 1.0f; }
            return;
        }
    }
    if (kind == Kind::Cave && !caveBox) kind = view.caveSteady ? Kind::Interior : Kind::Transition;   // the older behaviour
    const bool leavingCave = kind == Kind::Transition && view.mode == kModeLeaveCave && view.level == 0;
    if (enabled && caveBox) {
        if (kind == Kind::Cave && haveBackdrop && (caveSession || lastLayout == Layout::World)) caveSession = true;
        else if (caveSession && !leavingCave && kind != Kind::Menu) caveSession = false;
    } else {
        caveSession = false;
    }
    if (caveSession) {
        const bool visible = playAreaVisible(nes.ppu.framebuffer);
        const bool overworldOnScreen = nametableShowsRoom(nes, view.room);
        if (leavingCave && visible && overworldOnScreen) {
            caveSession = false;                               // the overworld is back: normal frames from here
        } else {
            const bool shadowsLive = enabled && shadows && shadows->enabled && !shadows->games().empty();      // (the overworld goes on behind the cave: the monsters of every screen keep moving)
            if (!outIsBackdrop || cloudShadow || waterOn() || shadowsLive) { out = backdrop; outIsBackdrop = true; redoBackdropWater(out); }
            if (shadowsLive) drawShadowSprites(out, backdropCamX, backdropCamY, backdropTop, -1, slot);
            lightSources.clear(); lightLinkX = backdropLinkX; lightLinkY = backdropLinkY;
            if (lighting && shadowsLive) gatherShadowLights(-1, slot, false);
            if (lighting && lightRoom >= 0) gatherStaticLights(nes, lightRoom, slot, false, (lightRoom & 15) * ROOM_W, (lightRoom >> 4) * ROOM_H);   // (the statues keep glowing behind the cave)
            waterDone = true;                                  // (the water is baked into the backdrop)
            applyClouds(out, backdropTop, backdropCamX, backdropCamY, backdropCamX + out.w / 2, backdropCamY + (out.h - backdropTop) / 2);
            heldInterior = false; smoothValid = false;
            lastLayout = Layout::World;
            const bool boxNow = visible && !overworldOnScreen;
            if (trace && boxNow != lastWasCaveBox)
                std::printf("[trace] mode=$%02X sub=%d updating=%d room=$%02X -> cave box %s\n", view.mode, view.sub, view.updating, view.room, boxNow ? "on" : "off");
            lastWasCaveBox = boxNow;
            if (boxNow) buildCaveOverlay(nes);
            if (hudOverlay) {
                if (nes.ppu.lineMask[100] & 0x18) addHudOverlay(nes, view, slot);
                else for (const OverlayImage& img : lastHud) overlay.images.push_back(img);
            }
            return;                                            // the canvas is the backdrop
        }
    }
    lastWasCaveBox = false;
    outIsBackdrop = false;

    // ---- Layout ----
    // The picture size only changes at steady moments. During transitions we keep whatever size the
    // previous steady moment used, so animations never change scale halfway through.
    Layout layout = Layout::Plain;
    bool followLink = false;
    bool gameOverScreen = false;
    const bool textUp = kind == Kind::GameOver && playAreaVisible(nes.ppu.framebuffer);
    if (enabled) {
        switch (kind) {
            case Kind::NonGameplay:                  // after the title screen etc. start from the big picture
                heldInterior = true; break;
            case Kind::Overworld:
                heldInterior = mazeShown; layout = mazeShown ? Layout::Plain : Layout::World; followLink = !mazeShown; break;
            case Kind::OverworldIntro:
                // The curtain on an overworld screen. Until the game has set up the screen (the picture is black)
                // there is nothing to draw around it.
                heldInterior = false; layout = view.inOverworld ? Layout::World : Layout::Boxed; break;
            case Kind::OverworldOdd:
                heldInterior = false; layout = Layout::Boxed; break;
            case Kind::Cave:                         // (the overworld screen is still on the PPU: carry on with the world view)
                heldInterior = false; layout = Layout::World; break;
            case Kind::Interior:
                heldInterior = interiorsBig; layout = heldInterior ? Layout::Plain : Layout::Boxed; break;
            case Kind::Menu:
                heldInterior = true; layout = Layout::Plain; break;     // the inventory covers the whole window
            case Kind::GameOver: {
                // The screen is black until the text shows up; switching the size earlier would only make the
                // status bar flash.
                if (textUp) { heldInterior = true; layout = Layout::Plain; gameOverScreen = true; }
                else layout = heldInterior ? Layout::Plain : (view.level == 0 ? Layout::World : Layout::Boxed);
                break;
            }
            case Kind::Transition:
                // Still on the overworld screen (cave/stairs animations): keep showing the world around it. The
                // game already counts as "inside the dungeon" while Link walks down the entrance stairs, so
                // there we check what the PPU is really showing.
                if (heldInterior) {
                    // Leaving a cave (mode $A): the overworld screen is redrawn while the picture is dark. Switch to
                    // the overworld view as soon as the PPU holds it, instead of waiting for the walk-out.
                    // (switching while the picture is still black would only flash the HUD)
                    if (view.mode == kModeLeaveCave && view.level == 0 && playAreaVisible(nes.ppu.framebuffer) && nametableShowsRoom(nes, view.room)) {
                        heldInterior = false; layout = Layout::World;
                    } else {
                        layout = Layout::Plain;
                    }
                }
                else if (view.level == 0) layout = mazeShown ? Layout::Plain : Layout::World;
                else if (view.mode == kModeStairs && nametableShowsRoom(nes, view.room)) layout = Layout::World;
                else layout = Layout::Boxed;
                break;
        }
    }
    if (trace && (view.mode != lastTraceMode || (int)kind != lastTraceKind || (int)layout != lastTraceLayout)) {
        static const char* kindNames[] = {"NonGameplay", "Overworld", "OverworldIntro", "OverworldOdd", "Interior", "Menu", "GameOver", "Cave", "Transition"};
        static const char* layoutNames[] = {"Plain", "World", "Boxed", "Menu"};
        std::printf("[trace] mode=$%02X sub=%d updating=%d level=%d exit=%d room=$%02X menu=%d -> %s, %s%s\n", view.mode, view.sub,
                    view.updating, view.level, view.exitType, view.room, view.menu, kindNames[(int)kind], layoutNames[(int)layout],
                    heldInterior ? " (big)" : "");
        lastTraceMode = view.mode; lastTraceKind = (int)kind; lastTraceLayout = (int)layout;
    }

    if (layout != Layout::World) smoothValid = false;

    // The status bar as an overlay: on every gameplay picture (not the menu, the title or the GAME OVER screen).
    // It stays on during the GAME OVER sequence until the text itself appears. When a game is starting (the load and the
    // curtain), the game builds its status bar piece by piece behind the black screen; show nothing until it is done.
    const bool gameplayHud = kind != Kind::NonGameplay && kind != Kind::Menu && !(kind == Kind::GameOver && textUp);
    const bool hiddenHud = enabled && hudOverlay && gameplayHud && view.level == 0 && (view.mode == kModeLoad || view.mode == kModeUnfurl) && view.exitType == 0;
    const bool hudOn = enabled && hudOverlay && gameplayHud && !hiddenHud;

    // Frames where the game has switched the picture off (fades while loading a screen) would show up as
    // black flashes; keep showing the last picture for a few frames instead.
    const bool videoOff = !(nes.ppu.lineMask[100] & 0x18);
    if (layout != Layout::Plain && lastLayout != Layout::Plain && videoOff && videoOffFrames < 6 && !out.pixels.empty()) {
        videoOffFrames++;
        if (hudOn) for (const OverlayImage& img : lastHud) overlay.images.push_back(img);    // the bar stays too
        return;
    }
    if (!videoOff) videoOffFrames = 0;
    if (layout == Layout::World && videoOff) layout = Layout::Boxed;
    lastLayout = layout;
    const uint32_t* fb = nes.ppu.framebuffer;
    topBand = (hudOn || hiddenHud) ? 0 : HUD_H;
    if (kind == Kind::Overworld && view.mode == kModePlay && paletteLuma(nes.ppu.palette) > 0) {   // a normal overworld frame
        std::memcpy(refPalette, nes.ppu.palette, 16);                                     // (remember what it looks like)
        refLuma = paletteLuma(refPalette);
        if (minimapStyle == 2) bakeThumbs(nes, slot);
    }
    if (hudOn && !videoOff) addHudOverlay(nes, view, slot);

    if (gameOverScreen) {
        out.w = Ppu::W; out.h = Ppu::H;
        out.pixels.assign((size_t)out.w * out.h, 0xFF000000);       // black, with the text drawn large on top
        buildGameOverOverlay(nes);
        return;
    }
    const int vh = std::clamp(viewHeight, MIN_VIEW_HEIGHT, MAX_VIEW_HEIGHT);
    const int H = layout == Layout::Plain ? FRAME_H : topBand + vh;
    int W = Ppu::W;
    if (layout != Layout::Plain) W = std::clamp((int)std::lround(H * windowAspect) & ~1, 512, 2048);
    else if (mazeShown && view.level == 0) W = std::clamp((int)std::lround(H * windowAspect) & ~1, Ppu::W, 2048);      // (a maze: its own size, the haze reaches the bars beside it)
    out.w = W; out.h = H;
    { perf::Scope perfScope(perf::kPrep); out.pixels.assign((size_t)W * H, 0xFF000000); }
    const int left = (W - Ppu::W) / 2;
    // Link's sight: his steady overworld place, eased like the camera; coordinates that jump (cave/dungeon positions while coming out,
    // a scroll that changes the room) are only followed once they hold for a few frames.
    if (view.mode == kModeLoad || view.mode == kModeUnfurl || view.mode == kModeContinue || kind == Kind::GameOver) steadyValid = false;   // a new start (loading, dying and continuing): no easing from the old place
    if ((kind == Kind::Overworld && view.level == 0) || (kind == Kind::OverworldIntro && !steadyValid)) {
        int lcx, lcy; linkWorldCenter(view, lcx, lcy);
        const float dx = lcx - steadyLinkX, dy = lcy - steadyLinkY;
        if (!steadyValid) { steadyLinkX = (float)lcx; steadyLinkY = (float)lcy; steadyValid = true; steadyJump = 0; }
        else if (std::fabs(dx) > 40 || std::fabs(dy) > 40) { if (++steadyJump >= 6) { steadyLinkX = (float)lcx; steadyLinkY = (float)lcy; steadyJump = 0; } }
        else { steadyJump = 0; steadyLinkX += dx / 3; steadyLinkY += dy / 3; }
    }
    if (mazeShown && kind == Kind::Transition && view.mode == kModeScroll && view.level == 0) {      // a maze's own scroll: Link's sight goes with the Link sprite on the scrolling picture
        const uint8_t* lo = &nes.ppu.oam[kLinkOam0 * 4];
        if (lo[0] < 0xEF) { steadyLinkX = (float)((view.room & 15) * ROOM_W + lo[3] + 8); steadyLinkY = (float)((view.room >> 4) * ROOM_H + lo[0] + 9 - HUD_H); steadyValid = true; steadyJump = 0; }
    }
    lightLinkX = steadyLinkX; lightLinkY = steadyLinkY;
    lightPpu = &nes.ppu; fxNes = &nes; fxSlot = slot; fxRoom = view.room; fxOx = (view.room & 15) * ROOM_W; fxOy = (view.room >> 4) * ROOM_H;
    const bool dungeonTrans = view.level != 0 && (view.mode == kModeScroll || view.mode == kModeStairs);       // (the room we leave scrolls away with its sparkles: they are cleared once the next room is at rest)
    if (view.room != lightRoom && !dungeonTrans) {                                                                                   // (in a dungeon or cave the sparkles stay with their screen; on the overworld they sit in world space and
        if (view.level != 0 || kind == Kind::Cave || kind == Kind::Interior) sparkles.clear();                                          //  go on across a change of screen: the items of the neighbours were glittering already)
        else sparkles.erase(std::remove_if(sparkles.begin(), sparkles.end(), [](const Sparkle& sp) { return sp.kind == 1; }), sparkles.end());      // (only the boomerang's trail is dropped)
        lightRoom = view.room; roomAge = roomPlayAge = 0;
    }
    const int space = view.level != 0 ? 2 : (kind == Kind::Cave || kind == Kind::Interior) ? 1 : 0;      // overworld / inside a cave / dungeon
    if (space != lightSpace) { sparkles.clear(); lightSpace = space; }
    candleHeld = nes.ram[kSelectedItem] == kItemSlotCandle ? nes.ram[kInvCandle] : 0;
    if (kind == Kind::Overworld && view.level == 0) lastOverRoom = view.room;
    // The overworld picture is on screen (play, the walk into stairs / a cave / a dungeon, the way back out of a cave): the neighbouring screens' shadows are drawn around it.
    const bool shadowScene = layout == Layout::World && ((kind == Kind::Overworld && view.level == 0) || (kind == Kind::Transition && (view.mode == kModeStairs || (view.mode == kModeCellar && !cellarBlackSeen && view.level == 0) || (view.mode == kModeLeaveCave && view.level == 0))));
    if (kind == Kind::Overworld && view.mode == kModePlay && view.level == 0) { const int pl = paletteLuma(nes.ppu.palette); if (pl > 0) owRefLuma = pl; }
    // (stairs, a cave, a cellar: the game fades its picture by palette; the shadows' monsters on it fade the same way)
    const int shadowFade = kind == Kind::Transition && owRefLuma > 0 ? std::clamp(paletteLuma(nes.ppu.palette) * 256 / owRefLuma, 0, 256) : 256;
    // (the stairs' RoomId is already the cellar's: while Link walks down the game's own monsters are on the picture, once it fades into the cellar the screen's shadow is)
    // The game's own picture of the walk into a cave, stairs or a dungeon has no monsters (or they vanish with the fade): the shadow of the screen stands in from the first picture of the walk.
    const bool takeover = shadowScene && shadows && view.mode == kModeStairs && shadows->hasVisible(shadows->baseRoom());
    const int shadowCur = takeover ? -1 : (view.mode == kModeStairs && shadows ? shadows->baseRoom() : (view.mode == kModeCellar ? -1 : view.room));
    // The screen's own shadow is drawn over the picture while the game's picture has no monsters: leaving a screen (mode 6), walking into a screen or out of a cave, the first pictures of play.
    const bool bridgeNow = shadows && shadows->bridgeGame() && shadowScene && (view.mode == kModeEnter || view.mode == kModeLeaveCave || view.mode == kModeLeave || (view.mode == kModePlay && shadows->bridgeShowsInPlay()));
    if (lighting && (layout != Layout::Plain || mazeShown) && (kind == Kind::Overworld || kind == Kind::OverworldOdd))   // what glows (moving things only while the game plays; statues always)
        gatherLights(nes, view, (view.room & 15) * ROOM_W, (view.room >> 4) * ROOM_H, slot, true, view.mode == kModePlay);
    else if (lighting && layout != Layout::Plain && (kind == Kind::Transition || kind == Kind::OverworldIntro || kind == Kind::Cave)) {   // (also while a dungeon is entered: the overworld is still on screen)
        const int sr = view.level != 0 && lastOverRoom >= 0 ? lastOverRoom : view.room;                  // the screen that is shown
        lightSources.clear();
        gatherStaticLights(nes, sr, slot, false, (sr & 15) * ROOM_W, (sr >> 4) * ROOM_H);
    }
    else lightSources.clear();
    if (trace && shadows && shadows->enabled) {                          // --trace: what the neighbour screens are doing (printed when it changes)
        static int lastScene = -1, lastBridge = -1, lastVisible = -1, lastMode = -1;
        int vis = 0; for (ShadowGame* g : shadows->games()) vis += g->visible();
        const int br = shadows->bridgeGame() ? 1 + (shadows->bridgeShowsInPlay() ? 1 : 0) : 0, sc = shadowScene ? 1 : 0;
        if (sc != lastScene || br != lastBridge || vis != lastVisible || (int)view.mode != lastMode) {
            std::printf("[trace] shadows: view mode=$%02X scene=%d bridge=%d visible=%d handoff=%d\n", view.mode, sc, br, vis, shadows->handOffPending() ? 1 : 0);
            lastScene = sc; lastBridge = br; lastVisible = vis; lastMode = view.mode;
        }
    }
    if (lighting && shadowScene) gatherShadowLights(shadowCur, slot, true, bridgeNow);
    const bool dungeonView = view.level != 0 && layout == Layout::Plain && (kind == Kind::Interior || kind == Kind::Transition);   // (rooms sliding, stairs, cellars, the death scene)
    if (!dungeonView) { dungeonDim = dungeonLit = 0; wasScroll = false; frozenOn = false; staleObjects = false; }
    if (!(kind == Kind::Cave || (kind == Kind::Interior && view.level == 0) || dungeonView) && !caveSession) caveDim = 0;

    mazeFxOk = enabled && (mazeShown || mazePhase == 2);
    switch (layout) {
        case Layout::Plain:
            if (hudOn) blit(out, &fb[HUD_H * Ppu::W], Ppu::W, ROOM_H, left, (FRAME_H - ROOM_H) / 2, 0);   // room centred; the bar is an overlay
            else       blit(out, fb, Ppu::W, FRAME_H, left, 0, 0);
            if (enabled && dungeonView) {
                const int rt = hudOn ? (FRAME_H - ROOM_H) / 2 : HUD_H;
                drawDroppedSprites(nes, out, rt, view.mode >= kModeEnter && view.mode <= kModeScroll);
                if (hudOn) spritesBeyondRoom(nes, out, rt);
                applyDungeonLight(nes, out, view, rt, hudOn ? rt + ROOM_H : FRAME_H);       // the torches in the corners
            } else if (enabled && mazeShown && view.level == 0 && (kind == Kind::Overworld || kind == Kind::Transition)) {   // a maze: clouds and the night light at its true place
                const int bx = (view.room & 15) * ROOM_W, by = (view.room >> 4) * ROOM_H, off = hudOn ? (FRAME_H - ROOM_H) / 2 : 0;
                applyClouds(out, topBand, bx - left, by - off, bx + ROOM_W / 2, by + ROOM_H / 2);
            } else if (enabled && kind == Kind::Interior && view.level == 0) {              // a cave drawn at full size
                const int rt = hudOn ? (FRAME_H - ROOM_H) / 2 : HUD_H;
                dayClock.step();
                caveLight(nes, out.pixels.data(), out.w, out.h, rt, hudOn ? rt + ROOM_H : FRAME_H);
            }
            return;
        case Layout::Boxed:
            if (!hudOn && !hiddenHud) blit(out, fb, Ppu::W, HUD_H, left, 0, 0);
            blit(out, &fb[HUD_H * Ppu::W], Ppu::W, ROOM_H, left, topBand + (vh - ROOM_H) / 2, topBand);
            if ((view.level == 0 || view.mode == kModeStairs) && kind != Kind::Interior && kind != Kind::Cave && kind != Kind::Menu) {
                // The overworld screen in its box (curtains, odd screens): the room sits at its true world position.
                const int bx = (view.room & 15) * ROOM_W, by = (view.room >> 4) * ROOM_H;
                applyClouds(out, topBand, bx - left, by - (vh - ROOM_H) / 2, bx + ROOM_W / 2, by + ROOM_H / 2);
            }
            return;
        case Layout::World:
            break;
    }

    // ---- World view: camera centred on Link ----
    int cx, cy;
    linkWorldCenter(view, cx, cy);
    const int linkCx = cx, linkCy = cy;
    bool hideLinkSprites = false;
    const int ox = (view.room & 15) * ROOM_W, oy = (view.room >> 4) * ROOM_H;
    const bool steppingOut = view.level == 0 && view.exitType != 0 &&
                             (view.mode == kModeLoad || view.mode == kModeUnfurl || view.mode == kModeEnter || view.mode == kModeLeaveCave);
    if (steppingOut) {
        // Coming out of a cave or dungeon: Link will appear at a spot the game derives from the screen's
        // attributes. Put the camera there from the start so it doesn't have to move when he shows up.
        cx = ox + (nes.cart.cpuRead(kAttrsA + view.room) & 0xF0) + 8;
        cy = oy + (((nes.cart.cpuRead(kAttrsF + view.room) & 7) << 4) + 0x4D) + 8 - HUD_H;
        int sx, sy;
        if (stairsExitSpot(nes, view.room, sx, sy)) { cx = ox + sx + 8; cy = oy + sy + 8 - HUD_H; }   // (stairs: the square he went in by)
        // Link's object still holds the cave/dungeon position for a few frames before the game moves him to the entrance;
        // his sprites at that stale spot don't belong on this screen.
        hideLinkSprites = std::max(std::abs(linkCx - cx), std::abs(linkCy - cy)) > 24;
    } else if (followLink) {
        heldCamX = cx; heldCamY = cy; haveHeldCam = true;
    } else if (kind == Kind::OverworldIntro) {
        // Starting or continuing a game: Link is already standing at the start position (cx, cy).
    } else if (haveHeldCam && heldCamX >= ox - 128 && heldCamX < ox + ROOM_W + 128 &&
               heldCamY >= oy - 128 && heldCamY < oy + ROOM_H + 128) {
        cx = heldCamX; cy = heldCamY;                          // transitions: the camera stays where it was
    }
    // Ease out jumps (the game puts Link a little forward when a screen scroll ends, for example).
    if (!smoothValid) {
        smoothX = cx; smoothY = cy;
    } else {
        auto ease = [](int from, int target) {
            const int d = target - from;
            if (d > -3 && d < 3) return target;
            int step = d / 4;
            if (step == 0) step = d > 0 ? 1 : -1;
            return from + step;
        };
        smoothX = ease(smoothX, cx);
        smoothY = ease(smoothY, cy);
    }
    smoothValid = true;
    cx = smoothX; cy = smoothY;
    const int camX = cx - W / 2;                         // world x at canvas column 0
    const int camY = cy - vh / 2;                        // world y at canvas row topBand
    const int roomX = (view.room & 15) * ROOM_W - camX, roomY = topBand + (view.room >> 4) * ROOM_H - camY;   // the game's own room on the canvas
    if (shadows && layout == Layout::World) {                            // the screens on the canvas get a shadow (N): more of them the more zoomed out
        uint8_t want[128] = {};
        const int c0 = std::max(0, camX / ROOM_W - (camX < 0 ? 1 : 0)), c1 = std::min(15, (camX + W - 1) / ROOM_W), r0 = std::max(0, camY / ROOM_H - (camY < 0 ? 1 : 0)), r1 = std::min(7, (camY + (out.h - topBand) - 1) / ROOM_H);
        for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) if (!fog || discovered(view.slot, r * 16 + c)) want[r * 16 + c] = 1;   // (a screen under the fog shows nothing living: not made)
        shadows->setWanted(want);
    }

    // The bomb "flash": the game turns its grayscale bit on and off for a few frames. Instead of tinting the whole picture
    // (status bar included) it becomes a circle of gray around the bomb, which also reaches the neighbouring screens.
    FlashEffect flash;
    if (nes.ppu.lineMask[100] & 0x01) {
        flash.on = true;
        flash.cx = ox + view.linkX + 8; flash.cy = oy + view.linkY - HUD_H + 8;           // (Link, if no bomb is found)
        for (int bomb : {kSlotBombA, kSlotBombB})
            if (nes.ram[kObjState + bomb] == kStateBombFlash) { flash.cx = ox + nes.ram[kObjX + bomb] + 8; flash.cy = oy + nes.ram[kObjY + bomb] - HUD_H + 8; break; }
    }
    // A bomb that goes off on a screen next to Link's flashes there too: the shadow's bomb is in its flash state and the game's grayscale bit (cleared at timer $12 and $0D, set at
    // $16 and $11, UpdateBombFlashEffect, Z_01.asm 4086) is on while the timer is in $13-$16 and $0E-$11.
    if (!flash.on && shadows && shadows->enabled && layout == Layout::World)
        for (const ShadowGame* g : shadows->games()) {
            if (!g->visible() || g->room() == view.room || (fog && !discovered(slot, g->room()))) continue;
            const uint8_t* r = g->nes().ram;
            for (int bomb : {kSlotBombA, kSlotBombB}) {
                const int tm = r[kObjTimer + bomb];
                if (r[kObjState + bomb] != kStateBombFlash || !((tm >= 0x13 && tm <= 0x16) || (tm >= 0x0E && tm <= 0x11))) continue;
                flash.on = true; flash.cx = (g->room() & 15) * ROOM_W + r[kObjX + bomb] + 8; flash.cy = (g->room() >> 4) * ROOM_H + r[kObjY + bomb] - HUD_H + 8;
            }
            if (flash.on) break;
        }
    // The picture that stays behind a cave's box: the last bright, steady overworld frame, *without* anything that moves
    // (Link, enemies, their projectiles, effects). So it is the scenery as drawn by drawWorld plus the game's room with only
    // its background layer; no flash frames (they would leave a gray circle in it).
    // It is only kept when something is about to cover the overworld (a cave, stairs, the menu, a scroll): while the picture's game state
    // and the one a frame ahead are both plain play on the same screen, nothing is copied (about 10 MB of copies and a water pass per frame).
    auto plainPlay = [](const ViewSnapshot& s) { return s.mode == kModePlay && s.level == 0 && !s.menu && s.exitType == 0; };
    const bool steadyPlay = haveBackdrop && plainPlay(view) && plainPlay(cur) && view.room == cur.room;
    const bool keepBackdrop = caveBox && layout == Layout::World && (kind == Kind::Overworld || kind == Kind::Cave) && refLuma > 0 &&
                              !flash.on && paletteLuma(nes.ppu.palette) * 10 >= refLuma * 8 && !steadyPlay;
    curtainMask.clear();
    bool roomWaterOk = false;
    recordShare = keepBackdrop;
    drawWorld(nes, out, camX, camY, slot, flash);
    if (keepBackdrop) { perf::Scope perfScope(perf::kBackdrop); backdrop = out; backdropCamX = camX; backdropCamY = camY; backdropTop = topBand; backdropLinkX = lightLinkX; backdropLinkY = lightLinkY; }

    if (kind == Kind::OverworldIntro) {
        // The curtain opening after leaving a dungeon. In the original it opens the 256-pixel screen from its
        // middle; here it opens the whole wide picture the same way. The screen itself comes from our decode
        // (nothing but the scenery is visible yet), and the width of the opening is measured from the game's
        // own picture.
        int minX = Ppu::W, maxX = -1;
        for (int y = HUD_H; y < FRAME_H; y++)
            for (int x = 0; x < Ppu::W; x++)
                if (fb[y * Ppu::W + x] & 0x00FFFFFF) { minX = std::min(minX, x); maxX = std::max(maxX, x); }
        const double open = maxX >= minX ? (double)(maxX - minX + 1) / Ppu::W : 0.0;
        const int half = (int)(open * W / 2.0 + 0.5);
        curtainMask.assign((size_t)W * H, 1);                          // (the lighting leaves the curtain's black alone: it only shows where the screen is open)
        for (int y = topBand; y < H; y++) {
            uint32_t* row = &out.pixels[(size_t)y * W];
            for (int x = 0; x < W; x++)
                if (x < W / 2 - half || x >= W / 2 + half) { row[x] = 0xFF000000; curtainMask[(size_t)y * W + x] = 0; }
        }
    } else {
        // The game's own picture of the current screen (with Link, enemies, effects) goes on top, untouched.
        perf::Scope perfRoom(perf::kRoom);                       // (the game's own picture: sprites, effects, the water of Link and the monsters)
        planSpriteFixes(nes, view, hideLinkSprites);
        if (takeover) for (int i = 0; i < 64; i++) if (i != kLinkOam0 && i != kLinkOam1 && !hudSpriteEntry(nes.ppu.oam, i)) hideSprite[i] = true;   // (the shadow's monsters are drawn: not these twice)
        blitRoomId = view.room;
        blitSecrets = kind == Kind::Overworld && view.level == 0 ? roomSecrets(nes, view.room) : nullptr;
        blitRoom(nes, out, roomX, roomY, topBand, flash, ox, oy);
        if (topBand == 0) drawOverhang(nes, out, roomX, roomY, topBand, flash, ox, oy);
        drawTrueSprites(nes, out, roomX, roomY, topBand, flash, ox, oy);
        drawSplashes(nes, out, roomX, roomY, topBand, flash, ox, oy);
        // The monsters of the shadows go over everything (a jumping monster that leaves its screen is not cut off by the next one), the game's room included.
        spriteFade = shadowFade;
        if (shadowScene) drawShadowSprites(out, camX, camY, topBand, shadowCur, view.slot);
        if (bridgeNow)
            drawShadowSprites(out, camX, camY, topBand, view.room, view.slot, true);
        spriteFade = 256;
        roomWaterOk = waterDone && waterOn() && layout == Layout::World && terrainW == out.w && terrainH == out.h && terrainCamX == camX && terrainCamY == camY
                      && terrainMask.size() == (size_t)out.w * (out.h + kWaterMargin);
        if (roomWaterOk) { perf::Scope perfWater(perf::kRoomWater); roomWater(out, camX, camY, true); }      // the game's picture of this screen gets reflections of Link, monsters and shots, the same water
    }
    if (layout == Layout::World && view.mode == kModeDeath && refLuma > 0) fadeDeathTopHalf(nes, out, roomX, roomY);
    // Cloud shadows over the whole scene (scenery, Link, monsters) in every overworld picture, transitions included.
    applyClouds(out, topBand, camX, camY, cx, cy);
    if (!hudOn && !hiddenHud) blit(out, fb, Ppu::W, HUD_H, left, 0, 0);   // the original status bar, centred at the top

    // Finish the backdrop: the room from the background layer only (no sprites at all), and the original bar if it is used.
    if (keepBackdrop) {
        perf::Scope perfScope(perf::kBackdrop);
        blitRoomBackground(nes, backdrop, roomX, roomY, topBand);
        backdropWaterOk = roomWaterOk && backdropRawPre.size() == backdrop.pixels.size() + (size_t)backdrop.w * kWaterMargin;
        if (backdropWaterOk) {                                               // the room's picture before pass B, for the water of the backdrop to redo
            const int rx0 = std::max(roomX, 0), ry0 = std::max(roomY, topBand), rx1 = std::min(roomX + ROOM_W, backdrop.w), ry1 = std::min(roomY + ROOM_H, backdrop.h);
            backdropRoomRect[0] = rx0; backdropRoomRect[1] = ry0; backdropRoomRect[2] = std::max(rx0, rx1); backdropRoomRect[3] = std::max(ry0, ry1);
            backdropFx[0] = fxOx; backdropFx[1] = fxOy;
            backdropRoomPre.clear();
            for (int y = ry0; y < backdropRoomRect[3]; y++) for (int x = rx0; x < backdropRoomRect[2]; x++) backdropRoomPre.push_back(backdrop.pixels[(size_t)y * backdrop.w + x]);
        }
        if (roomWaterOk) roomWater(backdrop, camX, camY, false);      // (the room behind the box has its reflections too: scenery only)
        if (!hudOn && !hiddenHud) blit(backdrop, fb, Ppu::W, HUD_H, left, 0, 0);
        haveBackdrop = true;
    }
}

void Widescreen::applyClouds(Canvas& c, int top, int camX, int camY, int cx, int cy) {
    if (!enabled) return;
    if (!waterDone) applyWater(c, top, camX, camY);
    waterDone = false;
    if (cloudShadow) {
        if (!cloudsReady) {                                // (one cloud starts near the camera so the effect shows up at once)
            clouds.init(); cloudsReady = true;
            clouds.cloud(0).x = (float)(((cx + 180) % CloudField::kWorldW + CloudField::kWorldW) % CloudField::kWorldW);
            clouds.cloud(0).y = (float)((cy % CloudField::kWorldH + CloudField::kWorldH) % CloudField::kWorldH);
        }
        clouds.step();
        const int baseDark = clouds.maxDark;
        if (mazeShown) clouds.maxDark = std::min(256, baseDark * 22 / 10);          // (the maze: the shadows are much darker)
        { perf::Scope perfScope(perf::kClouds); clouds.shade(c.pixels, c.w, c.h, top, camX, camY); }
        clouds.maxDark = baseDark;
    }
    applyNightLight(c, top, camX, camY);                   // night, Link's sight and the lights of fire, shots and explosions
}
