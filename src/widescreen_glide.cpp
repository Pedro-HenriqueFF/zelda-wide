// Widescreen: the glide from one overworld screen to the next (Link walking the last pixels).
#include "widescreen_internal.h"
#include <memory>

void Widescreen::glideAnimStep(int& counter, int& frame) {
    if (--counter <= 0) { counter = 6; frame ^= 1; }                // (AnimateObjectWalking / RollOverAnimCounter, Z_07.asm 5078-5091, 5174-5180)
}

bool Widescreen::probeLinkPoses(const Nes& nes) {
    // Link's two walking poses as the game itself draws them (shield, direction, flipping included). Right after a scroll the
    // game is still in the init submodes of mode 4, which do not redraw Link, so a clone first plays on (nothing pressed, lite
    // PPU) until normal play (mode 5), where Link is drawn from ObjAnimFrame every frame; then ObjAnimFrame is set to 0 and to 1
    // and Link's two sprites are read from the game's OAM buffer after one frame each.
    if (nes.ram[kObjState] != 0) return false;                       // (attacking, halted: the picture's own pose stays)
    auto clone = std::make_unique<Nes>();
    clone->cloneFrom(nes, true); clone->pad1 = 0;
    for (int f = 0; f < kProbeMaxFrames && clone->ram[kGameMode] != kModePlay; f++) clone->runFrame();
    if (clone->ram[kGameMode] != kModePlay || clone->ram[kObjState] != 0) return false;
    LinkPose pose[2];
    for (int f = 0; f < 2; f++) {
        clone->ram[kObjAnimFrame] = (uint8_t)f; clone->ram[kObjInputDir] = 0;
        clone->runFrame();
        const uint8_t* a = &clone->ram[kSpriteRam + kLinkOam0 * 4];
        const uint8_t* b = &clone->ram[kSpriteRam + kLinkOam1 * 4];
        if (a[0] >= 0xEF || b[0] >= 0xEF || clone->ram[kObjState] != 0) return false;   // (hurt in the clone: another pose)
        pose[f].tile[0] = a[1]; pose[f].attr[0] = a[2]; pose[f].tile[1] = b[1]; pose[f].attr[1] = b[2];
        pose[f].dx = (int8_t)(uint8_t)(b[3] - a[3]); pose[f].dy = (int8_t)(uint8_t)(b[0] - a[0]);
    }
    if (pose[0].tile[0] == pose[1].tile[0] && pose[0].tile[1] == pose[1].tile[1] && ((pose[0].attr[0] ^ pose[1].attr[0]) & ~3) == 0 && ((pose[0].attr[1] ^ pose[1].attr[1]) & ~3) == 0) return false;   // (nothing to alternate)
    const uint8_t* shown = &nes.ppu.oam[kLinkOam0 * 4];
    const uint8_t* shown1 = &nes.ppu.oam[kLinkOam1 * 4];
    for (int f = 0; f < 2; f++) {                                    // (the palette is the picture's: it flashes while Link is hurt)
        pose[f].attr[0] = (uint8_t)((pose[f].attr[0] & ~3) | (shown[2] & 3)); pose[f].attr[1] = (uint8_t)((pose[f].attr[1] & ~3) | (shown1[2] & 3));
        glidePose[f] = pose[f];
    }
    glideAnimF = nes.ram[kObjAnimFrame] & 1;                         // the pose the picture shows when it is one of the two ...
    for (int f = 0; f < 2; f++)
        if (pose[f].tile[0] == shown[1] && pose[f].tile[1] == shown1[1]) glideAnimF = f;
    glideAnimC = std::clamp((int)nes.ram[kObjAnimCounter], 1, 6);
    return true;
}

void Widescreen::startGlide(Nes& nes) {
    if (!enabled || !map.ready() || !haveHeldCam) return;
    ViewSnapshot v = capture(nes);
    if (!v.inOverworld || (v.mode != kModeEnter && v.mode != kModePlay)) return;
    if (zelda::isMazeRoom(v.room) != mazeShown) return;               // (in or out of a maze: the mist hides the change)
    int bx, by;
    linkWorldCenter(v, bx, by);
    const int ax = smoothValid ? smoothX : heldCamX, ay = smoothValid ? smoothY : heldCamY;   // where the camera is now
    const int dist = std::max(std::abs(bx - ax), std::abs(by - ay));
    if (dist < 3 || dist > 48) return;                                // nothing to smooth, or something unexpected
    glideAx = ax; glideAy = ay; glideBx = bx; glideBy = by;
    glideN = std::clamp(dist, 8, 32);                                 // about one pixel per frame: Link's walking pace
    glideT = 0;
    glideView = v;
    glideAnimate = probeLinkPoses(nes);                               // (false: the picture's own frozen pose)
    glideActive = true;
    const int slot = v.slot < 3 ? v.slot : 0;
    syncFogSlot(nes, slot);
    discoverRoom(slot, v);
}

void Widescreen::renderGlide(Nes& nes, double windowAspect, Canvas& out) {
    const int vh = std::clamp(viewHeight, MIN_VIEW_HEIGHT, MAX_VIEW_HEIGHT);
    topBand = hudOverlay ? 0 : HUD_H;
    outIsBackdrop = false;
    const int H = topBand + vh;
    const int W = wd::canvasWidth(H, windowAspect);
    out.w = W; out.h = H;
    out.pixels.assign((size_t)W * H, 0xFF000000);
    heldInterior = false;
    lastLayout = Layout::World;

    glideT++;
    const double f = (double)glideT / glideN;
    const int cx = glideAx + (int)std::lround((glideBx - glideAx) * f);
    const int cy = glideAy + (int)std::lround((glideBy - glideAy) * f);
    const int camX = cx - W / 2, camY = cy - vh / 2;
    const int slot = glideView.slot < 3 ? glideView.slot : 0;

    drawWorld(nes, out, camX, camY, slot, FlashEffect());
    drawShadowSprites(out, camX, camY, topBand, -1, slot);            // the monsters of the screen Link walks into (they keep moving while the glide lasts)
    // Link walks the last stretch: at the end of the glide he is exactly where the game put him.
    const int ox = (glideView.room & 15) * ROOM_W + (int)std::lround((glideAx - glideBx) * (1.0 - f));
    const int oy = (glideView.room >> 4) * ROOM_H + (int)std::lround((glideAy - glideBy) * (1.0 - f));
    std::vector<uint32_t> beforeLink; const bool water = waterDone && waterReflections && terrainW == out.w && terrainH == out.h && terrainMask.size() == (size_t)out.w * (out.h + kWaterMargin);
    if (water) beforeLink = out.pixels;
    if (glideAnimate) glideAnimStep(glideAnimC, glideAnimF);          // Link carries on walking: his legs keep their rhythm
    drawLinkSprites(nes, out, glideView, camX, camY, ox, oy, glideAnimate ? &glidePose[glideAnimF] : nullptr);
    if (water) {                                                      // Link keeps his reflection while he walks into the next screen
        std::vector<WaterObj> link;
        const int M = kWaterMargin;
        const int feet = topBand + M + (oy + glideView.linkY - HUD_H + 17) - camY;
        for (int y = topBand; y < H; y++) for (int x = 0; x < W; x++) if (out.pixels[(size_t)y * W + x] != beforeLink[(size_t)y * W + x]) link.push_back({x, y + M, feet, true});
        if (!link.empty()) {
            std::vector<uint32_t> ext(terrainRaw.begin(), terrainRaw.begin() + (size_t)(M + topBand) * W);
            ext.insert(ext.end(), out.pixels.begin() + (size_t)topBand * W, out.pixels.end());
            WaterJob j{ext.data(), W, H + M, topBand + M, camX, camY, 0, topBand + M, W, H + M, &terrainMask, false, false, &link, false, nullptr, 0};
            waterPass(j);
            std::copy(ext.begin() + (size_t)(M + topBand) * W, ext.end(), out.pixels.begin() + (size_t)topBand * W);
        }
    }
    lightLinkX = (float)(ox + glideView.linkX + 8); lightLinkY = (float)(oy + glideView.linkY - HUD_H + 8); lightSources.clear(); sparkles.erase(std::remove_if(sparkles.begin(), sparkles.end(), [](const Sparkle& sp) { return sp.kind == 1; }), sparkles.end()); lightPpu = &nes.ppu; candleHeld = nes.ram[kSelectedItem] == kItemSlotCandle ? nes.ram[kInvCandle] : 0;
    if (lighting) gatherStaticLights(nes, glideView.room, slot, false, (glideView.room & 15) * ROOM_W, (glideView.room >> 4) * ROOM_H);   // (the statues' glow does not go out)
    if (lighting) gatherShadowLights(-1, slot, true);                  // (the items and fairies of the neighbouring screens glow and glitter)
    steadyLinkX = lightLinkX; steadyLinkY = lightLinkY; steadyValid = true;   // (Link's sight walks with him)
    applyClouds(out, topBand, camX, camY, cx, cy);                    // the clouds carry on drifting while Link walks into the next screen
    if (hudOverlay) addHudOverlay(nes, glideView, slot);
    else blit(out, nes.ppu.framebuffer, Ppu::W, HUD_H, (W - Ppu::W) / 2, 0, 0);

    if (glideT >= glideN) {
        glideActive = false;
        if (glideAnimate) { nes.ram[kObjAnimFrame] = (uint8_t)glideAnimF; nes.ram[kObjAnimCounter] = (uint8_t)glideAnimC; }   // the game goes on with the same step
        glideAnimate = false;
        lagReset = true;                                              // the framebuffer matches the state we stopped at
        smoothValid = false;
        heldCamX = glideBx; heldCamY = glideBy;
    }
}
