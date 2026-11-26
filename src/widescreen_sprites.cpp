// Widescreen: per-frame sprite fixes (hidden/true sprites, sword-beam splash) and Link's sprites.
#include "widescreen_internal.h"

// True when OAM entry i is one of the four corners (tile $30) of a spreading sword-shot splash (position tested with the
// game's 8-bit wrap-around, see drawSplashes). `snap`: the object slots now and one frame ago.
static bool isSplashCorner(const uint8_t* o, const Widescreen::ObjSnap* snap) {
    auto test = [&](int cx, int cy) {
        const int dx = (int8_t)(uint8_t)(o[3] - 4 - cx), dy = (int8_t)(uint8_t)(o[0] - cy), ad = std::abs(dy);
        if (ad < 2 || ad > 24 || std::abs(dx) != ad) return false;
        const int flips = o[2] & 0xC0;
        return (flips == 0x00 && dx < 0 && dy < 0) || (flips == 0x80 && dx < 0 && dy > 0) ||
               (flips == 0xC0 && dx > 0 && dy > 0) || (flips == 0x40 && dx > 0 && dy < 0);
    };
    for (int g = 0; g < 2; g++) {
        const Widescreen::ObjSnap& s = snap[g];
        for (int slot = 1; slot <= kSlotShot; slot++) {
            const bool mine = slot == kSlotShot, mon = slot <= 12 && s.type[slot] == kTypeMonsterSwordShot;
            const int st = s.st[slot];
            if ((mine || mon) && (st & 1) && !(st & 0x80) && test(s.x[slot], s.y[slot])) return true;
        }
    }
    return false;
}

static int wrapTo256(int v) { return ((v + 128) & 255) - 128; }   // a signed distance on the 8-bit range

void Widescreen::planSpriteFixes(Nes& nes, const ViewSnapshot& view, bool hideLinkSprites) {
    std::memset(hideSprite, 0, sizeof hideSprite);
    trueSpriteCount = 0;
    objSnap[1] = objSnap[0];
    for (int slot = 0; slot < 20; slot++) {
        objSnap[0].st[slot] = nes.ram[kObjState + slot]; objSnap[0].x[slot] = nes.ram[kObjX + slot];
        objSnap[0].y[slot] = nes.ram[kObjY + slot]; objSnap[0].type[slot] = nes.ram[kObjType + slot];
    }
    // Only while one of Link's weapons is out (sword, shot, boomerang, bombs, rod beam / arrow: slots $0D-$12) can a wrapped
    // sprite be one of his; otherwise a monster standing at the opposite border would be taken for one.
    bool weaponOut = false;
    for (int slot = kSlotSword; slot <= kSlotLastWeapon; slot++) weaponOut = weaponOut || nes.ram[kObjState + slot] != 0;
    weaponLinger = weaponOut ? 2 : std::max(0, weaponLinger - 1);
    const uint8_t* oam = nes.ppu.oam;
    // The candle's fire at a border (see trackFire): its sprites are drawn at the true place (past the picture's side, or below its last row).
    bool fireOwned[64] = {};
    for (int s = 0; s < 2; s++)
        for (int g = 0; g < 2; g++) {
            const FireTrack& f = fireTrack[s];
            if (!f.act[g] || (f.overX[g] == 0 && f.overY[g] == 0 && f.drawY[g] < 0xE0)) continue;   // (the PPU hides what is drawn at Y >= $EF)
            for (int i = 0; i < kOamEntries; i++) {
                if (fireOwned[i] || i == kLinkOam0 || i == kLinkOam1) continue;
                const int sx = oam[i * 4 + 3], sy = (oam[i * 4] + 1) & 255;
                const int dx = wrapTo256(sx - f.drawX[g]), dy = wrapTo256(sy - f.drawY[g] - 1);
                if (dx < -3 || dx > 12 || dy < -4 || dy > 18) continue;
                fireOwned[i] = true; hideSprite[i] = true;
                trueSprites[trueSpriteCount++] = {i, sx + f.overX[g], sy + f.overY[g]};
            }
        }
    for (int i = 0; i < kOamEntries; i++) {
        if (fireOwned[i]) continue;
        if (oam[i * 4] >= 0xEF || hudSpriteEntry(oam, i)) continue;               // parked off screen / part of the status bar
        const int sy = oam[i * 4] + 1, sx = oam[i * 4 + 3];
        // 0. A sword-beam splash corner: drawSplashes draws all four at their real places, so the game's own copies go,
        //    in particular the ones whose X wrapped (beam at the right border: they appear at the left border, next to Link).
        if (oam[i * 4 + 1] == 0x30 && isSplashCorner(&oam[i * 4], objSnap)) { hideSprite[i] = true; continue; }
        // 1. Coming out of a cave/dungeon: Link's sprites still sit at the cave position (see render()).
        if (hideLinkSprites && sx >= view.linkX - 8 && sx <= view.linkX + 16 && sy >= view.linkY - 8 && sy <= view.linkY + 16) {
            hideSprite[i] = true;
            continue;
        }
        // 1b. A piece of one of Link's weapons near a side border whose X wrapped (a sword beam at the right border: its second
        //     half is at the left border, next to Link): hide that copy, draw it at the weapon's side.
        if (i != kLinkOam0 && i != kLinkOam1) {                                         // (never Link himself)
            int wrapX = sx;
            for (int g = 0; g < 2 && wrapX == sx; g++)                                  // now and one frame ago (the picture's frame)
                for (int slot = kSlotSword + 1; slot <= kSlotLastWeapon && wrapX == sx; slot++) {     // (not the sword: rule 2 knows where Link is)
                    if (objSnap[g].st[slot] == 0) continue;
                    const int wx = objSnap[g].x[slot], wy = objSnap[g].y[slot];
                    if (sy < wy - 4 || sy > wy + 20) continue;
                    if (wx >= 232 && sx <= 40 && sx + 256 - wx <= 24) wrapX = sx + 256;
                    else if (wx <= 24 && sx >= 216 && wx - (sx - 256) <= 24) wrapX = sx - 256;
                }
            if (wrapX != sx) { hideSprite[i] = true; trueSprites[trueSpriteCount++] = {i, wrapX, sy}; continue; }
        }
        // 2. A sprite next to Link whose X wrapped around the 8-bit OAM range (his sword when he stabs at the left or right
        //    border): the PPU shows it at the opposite border. Hide that copy, draw it where the game meant it to be.
        if (sy < view.linkY - 16 || sy > view.linkY + 16) continue;
        if (weaponLinger == 0) continue;
        // (the sword is at most 11 px left of Link, and its second half up to 19 px right of him)
        int trueX = sx;
        if (view.linkX <= 24 && sx >= 232 && view.linkX - (sx - 256) <= 16) trueX = sx - 256;
        else if (view.linkX >= 232 && sx <= 24 && sx + 256 - view.linkX <= 24) trueX = sx + 256;
        // Next to the top/bottom border the sprite sticks out of the picture's playfield rows (the status bar rows or past
        // row 239), where the PPU shows or cuts it: draw it whole at its real place instead.
        const int hgt = (nes.ppu.lineCtrl[100] & 0x20) ? 16 : 8;
        const bool cutV = sy < HUD_H || sy + hgt > 240;
        if (trueX != sx || cutV) {
            hideSprite[i] = true;
            trueSprites[trueSpriteCount++] = {i, trueX, sy};
        }
    }
}

void Widescreen::drawSpriteAt(Nes& nes, Canvas& out, uint8_t tile, uint8_t attr, int sx, int sy, int roomX, int roomY, int minY,
                              const FlashEffect& fl, int wx0, int wy0, int oamIdx) {
    // One OAM-style sprite at any position of the screen picture (sx, sy = its top-left in the game's 256x240 picture,
    // negative or beyond 255 is fine), not clipped to the picture: the parts outside land in the neighbouring screens.
    const uint8_t ctrl = nes.ppu.lineCtrl[100];
    const bool tall = ctrl & 0x20;
    const int hgt = tall ? 16 : 8;
    for (int row = 0; row < hgt; row++) {
        const int r = (attr & 0x80) ? hgt - 1 - row : row;
        uint16_t pat;
        if (tall) pat = ((tile & 1) ? 0x1000 : 0) + (tile & 0xFE) * 16 + (r & 7) + (r >= 8 ? 16 : 0);
        else      pat = ((ctrl & 0x08) ? 0x1000 : 0) + tile * 16 + r;
        const uint8_t lo = nes.cart.ppuRead(pat), hi = nes.cart.ppuRead(pat + 8);
        const int y = roomY + (sy - HUD_H + row);
        if (y < minY || y >= out.h) continue;
        for (int px = 0; px < 8; px++) {
            const int bit = (attr & 0x40) ? px : 7 - px;
            const int c = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            if (!c) continue;
            const int x = roomX + sx + px;
            if (x < 0 || x >= out.w) continue;
            if (oamIdx >= 0) {                                   // a sprite the PPU draws in front of this one (lower OAM index) stays in front
                const int qx = sx + px, qy = sy + row;
                if (qx >= 0 && qx < Ppu::SPR_W && qy >= 0 && qy < FRAME_H) {
                    const int k = qy * Ppu::SPR_W + qx;
                    if (nes.ppu.sprCol[k] != 0xFF && nes.ppu.sprOam[k] < oamIdx && !hideSprite[nes.ppu.sprOam[k]]) continue;
                }
            }
            const uint8_t ci = nes.ppu.palette[0x10 + ((attr & 3) << 2) + c] & 0x3F;
            uint32_t col = Ppu::nesColor(ci);
            if (fl.on) {
                const int t = flashAmount(wx0 + sx + px, wy0 + (sy - HUD_H + row), fl.cx, fl.cy);
                if (t) col = mixColor(col, Ppu::nesColor(ci & 0x30), t);
            }
            if (spriteFade < 256) col = mixColor(0xFF000000u, col, spriteFade);
            out.pixels[(size_t)y * out.w + x] = col;
        }
    }
}

void Widescreen::drawTrueSprites(Nes& nes, Canvas& out, int roomX, int roomY, int minY, const FlashEffect& fl, int wx0, int wy0) {
    for (int k = 0; k < trueSpriteCount; k++) {
        const uint8_t* o = &nes.ppu.oam[trueSprites[k].oam * 4];
        drawSpriteAt(nes, out, o[1], o[2], trueSprites[k].sx, trueSprites[k].sy, roomX, roomY, minY, fl, wx0, wy0, trueSprites[k].oam);
    }
}

void Widescreen::drawSplashes(Nes& nes, Canvas& out, int roomX, int roomY, int minY, const FlashEffect& fl, int wx0, int wy0) {
    // The sword beam's splash: when a shot hits something it spreads into four corner sprites around the shot's position
    // (see SpreadShot in the disassembly: corners at +-d, d growing from 2 to 24, each a narrow 8x16 sprite, tile $30,
    // flipped H/V per corner). The game leaves out the corners whose X would wrap or reach $FC, and the ones below the
    // bottom wrap in Y, so near the borders the splash is cut off. Here all four corners are drawn at their real places:
    // the centre is the shot object's position; the distance d and the palette come from any corner the game did draw.
    if (!(nes.ppu.lineCtrl[100] & 0x20)) return;                                  // (Zelda uses 8x16 sprites)
    int centres[2 * kSlotShot][2], n = 0;
    for (int g = 0; g < 2; g++)                                                     // now and one frame ago (the picture's frame)
        for (int slot = 1; slot <= kSlotShot; slot++) {
            const ObjSnap& s = objSnap[g];
            const int st = s.st[slot];
            if ((slot == kSlotShot || (slot <= 12 && s.type[slot] == kTypeMonsterSwordShot)) && (st & 1) && !(st & 0x80)) { centres[n][0] = s.x[slot]; centres[n][1] = s.y[slot]; n++; }
        }
    const uint8_t* oam = nes.ppu.oam;
    for (int c = 0; c < n; c++) {
        const int cx = centres[c][0], cy = centres[c][1];
        int d = 0, pal = 0;
        for (int i = 0; i < kOamEntries && !d; i++) {
            if (oam[i * 4] >= 0xEF || oam[i * 4 + 1] != 0x30) continue;
            const int dx = (int8_t)(uint8_t)(oam[i * 4 + 3] - 4 - cx), dy = (int8_t)(uint8_t)(oam[i * 4] - cy);
            const int ad = std::abs(dy);
            if (ad < 2 || ad > 24 || std::abs(dx) != ad) continue;
            const int flips = oam[i * 4 + 2] & 0xC0;
            const bool ok = (flips == 0x00 && dx < 0 && dy < 0) || (flips == 0x80 && dx < 0 && dy > 0) ||
                            (flips == 0xC0 && dx > 0 && dy > 0) || (flips == 0x40 && dx > 0 && dy < 0);
            if (!ok) continue;
            d = ad; pal = oam[i * 4 + 2] & 3;
        }
        if (!d) continue;                                                           // none of its corners is on screen
        static const int cornerX[4] = {-1, -1, 1, 1}, cornerY[4] = {-1, 1, 1, -1};
        static const uint8_t cornerAttr[4] = {0x00, 0x80, 0xC0, 0x40};              // TL, BL, BR, TR
        for (int k = 0; k < 4; k++)
            drawSpriteAt(nes, out, 0x30, cornerAttr[k] | pal, cx + cornerX[k] * d + 4, cy + cornerY[k] * d + 1,
                         roomX, roomY, minY, fl, wx0, wy0);
    }
}

void Widescreen::drawLinkSprites(Nes& nes, Canvas& out, const ViewSnapshot& v, int camX, int camY, int originX, int originY) {
    // Draws the sprites around Link (Link himself and anything attached to him) at an offset position.
    const FlashEffect noFlash;
    for (int i = 0; i < kOamEntries; i++) {
        const uint8_t* o = &nes.ppu.oam[i * 4];
        if (o[0] >= 0xEF) continue;                                  // parked off screen
        const int sy = o[0] + 1, sx = o[3];
        if (sx < v.linkX - 8 || sx > v.linkX + 16 || sy < v.linkY - 8 || sy > v.linkY + 16) continue;
        drawSpriteAt(nes, out, o[1], o[2], sx, sy, originX - camX, topBand + originY - camY, topBand, noFlash, 0, 0);
    }
}
