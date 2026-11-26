// Widescreen: the glide from one overworld screen to the next (Link walking the last pixels).
#include "widescreen_internal.h"

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
    drawLinkSprites(nes, out, glideView, camX, camY, ox, oy);
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
        lagReset = true;                                              // the framebuffer matches the state we stopped at
        smoothValid = false;
        heldCamX = glideBx; heldCamY = glideBy;
    }
}
