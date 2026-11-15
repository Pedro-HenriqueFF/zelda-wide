// Link in dungeon doorways. The game draws Link in front of the background everywhere except that, at the left/right border, each half of his sprite goes behind
// the background (all of it, wall bars of the passage included, in 8 px steps). Here Link is layered per pixel instead, as if he walked into the passage:
//  - an open doorway (a black passage with the floor of the doorway beside it): the frame beyond the black part covers Link (the lintel above the top door, the bar
//    below the bottom door, the bar and the outer wall beside the left/right door); the floor of the doorway and the passage walls are covered by Link;
//  - a blasted hole (the whole 16x16 cell is black): every non-black pixel of the wall around it covers Link, and at the left/right holes the pixels above a covered
//    pixel of the same column are covered too (nothing of his cap pokes out above the hole he walks into).
// Only while Link is lined up with the doorway; closed doors and walls are left to the game. The doors are found in the picture (black pixels of the background
// layer in the doorway cell), the rooms of a scroll (old and new) each use their own cell.
#include "widescreen_internal.h"

namespace {
enum DoorKind : uint8_t { None, Open, Hole };
enum Side { Top, Bottom, Left, Right };
constexpr int kDoorA = 120, kDoorB = 80, kCell = 16, kTopY = 16, kBottomY = 144, kLeftX = 16, kRightX = 224, kAlign = 4;
}

void Widescreen::linkAtDoors(Nes& nes, Canvas& c, int top, int dx, int dy, bool scrolling) {
    const Ppu& ppu = nes.ppu;
    // (a line with more than 8 sprites: the PPU left some of Link's pixels out, they are in the dropped layer: handled like the others, and drawn if they are not covered)
    struct P { int sx, sy; uint8_t col; };
    std::vector<P> link; int x0, y0;                                       // (Link's top-left corner: from the sprite entries below)
    for (int sy = 0; sy < ROOM_H; sy++) for (int sx = 0; sx < ROOM_W; sx++) {
        const size_t id = (size_t)(sy + HUD_H) * Ppu::SPR_W + sx;
        if (ppu.sprCol[id] != 0xFF && (ppu.sprOam[id] == kLinkOam0 || ppu.sprOam[id] == kLinkOam1)) { link.push_back({sx, sy, ppu.sprCol[id]});}
        else if (ppu.sprCol[id] == 0xFF && ppu.dropCol[id] != 0xFF && (ppu.dropOam[id] == kLinkOam0 || ppu.dropOam[id] == kLinkOam1)) { link.push_back({sx, sy, ppu.dropCol[id]});}
    }
    if (link.empty()) return;
    // Link's place from the sprite entries (his sprite pair), not from the opaque pixels
    const uint8_t* oam = ppu.oam;
    if (oam[kLinkOam0 * 4] >= 0xEF) return;
    x0 = std::min((int)oam[kLinkOam0 * 4 + 3], (int)oam[kLinkOam1 * 4 + 3]); y0 = oam[kLinkOam0 * 4] + 1 - HUD_H;
    // the sword / rod: its sprites (not Link's) inside its object's box are hidden like Link's pixels (never shown extra)
    std::vector<P> sword;
    if (nes.ram[kObjState + kSlotSword] != 0) {
        const int ox = nes.ram[kObjX + kSlotSword], oy = nes.ram[kObjY + kSlotSword] + 1 - HUD_H;
        for (int sy = std::max(0, oy - 6); sy < std::min(ROOM_H, oy + 22); sy++) for (int sx = std::max(0, ox - 6); sx < std::min(ROOM_W, ox + 22); sx++) {
            const size_t id = (size_t)(sy + HUD_H) * Ppu::SPR_W + sx;
            if (ppu.sprCol[id] == 0xFF || ppu.sprOam[id] == kLinkOam0 || ppu.sprOam[id] == kLinkOam1 || ppu.sprOam[id] >= 64 || hudSpriteEntry(oam, ppu.sprOam[id])) continue;
            sword.push_back({sx, sy, ppu.sprCol[id]});
        }
    }
    struct Room { int ox, oy; DoorKind kind[4]; bool aligned[4]; };
    Room rooms[2]; int nRooms = 1;
    rooms[0] = {0, 0, {None, None, None, None}, {false, false, false, false}};
    if (scrolling) { rooms[1] = {dx > 0 ? 256 : dx < 0 ? -256 : 0, dy > 0 ? 176 : dy < 0 ? -176 : 0, {None, None, None, None}, {false, false, false, false}}; nRooms = 2; }
    auto black = [&](int sx, int sy) { return (ppu.bgColor[(size_t)(sy + HUD_H) * Ppu::W + sx] & 0x00FFFFFF) == 0; };
    for (int r = 0; r < nRooms; r++) {
        Room& rm = rooms[r];
        // black share of a cell part (room coordinates), counting only what is on screen
        auto share = [&](int lx, int ly, int w, int h, int& seen) {
            int n = 0; seen = 0;
            for (int y = ly; y < ly + h; y++) for (int x = lx; x < lx + w; x++) {
                const int sx = x - dx + rm.ox, sy = y - dy + rm.oy;
                if (sx < 0 || sx >= ROOM_W || sy < 0 || sy >= ROOM_H) continue;
                seen++; n += black(sx, sy);
            }
            return n;
        };
        for (int s = 0; s < 4; s++) {
            const bool vertical = s == Top || s == Bottom;
            const int cx = vertical ? kDoorA : s == Left ? kLeftX : kRightX, cy = vertical ? (s == Top ? kTopY : kBottomY) : kDoorB;
            const int w = vertical ? kCell : kCell / 2, h = vertical ? kCell / 2 : kCell;                     // one half of the cell: the one beside the floor (inner) and the far one (outer)
            const int inX = vertical ? cx : (s == Left ? cx + kCell / 2 : cx), inY = vertical ? (s == Top ? cy + kCell / 2 : cy) : cy;
            const int outX = vertical ? cx : (s == Left ? cx : cx + kCell / 2), outY = vertical ? (s == Top ? cy : cy + kCell / 2) : cy;
            int seenIn = 0, seenOut = 0;
            const int bIn = share(inX, inY, w, h, seenIn), bOut = share(outX, outY, w, h, seenOut);
            if (seenIn >= 24 && bIn * 2 >= seenIn) rm.kind[s] = Hole;
            else if (seenOut >= 24 && bOut * 5 >= seenOut * 2) rm.kind[s] = Open;
            // lined up: Link's top-left corner (room coordinates) within kAlign of the doorway's
            const int lx = x0 + dx - rm.ox, ly = y0 + dy - rm.oy;
            rm.aligned[s] = vertical ? std::abs(lx - kDoorA) <= kAlign : std::abs(ly - kDoorB) <= kAlign;
        }
    }
    enum Act : uint8_t { Keep, Show, Hide };
    const size_t nLink = link.size();
    link.insert(link.end(), sword.begin(), sword.end());                          // (the sword's pixels follow Link's: hidden like his, never shown extra)
    std::vector<uint8_t> act(link.size(), Keep), colRule(link.size(), 0);
    std::vector<int> lowHidden(ROOM_W, -1);
    for (size_t i = 0; i < link.size(); i++) {
        const P& p = link[i];
        for (int r = 0; r < nRooms; r++) {
            const Room& rm = rooms[r];
            const int lx = p.sx + dx - rm.ox, ly = p.sy + dy - rm.oy;
            if (lx < 0 || lx >= ROOM_W || ly < 0 || ly >= ROOM_H) continue;
            for (int s = 0; s < 4; s++) {
                const DoorKind k = rm.kind[s];
                if (k == None || !rm.aligned[s]) continue;
                bool inBand, inZone;
                switch (s) {
                    case Top:    inBand = lx >= 96 && lx < 160 && ly < 32;   inZone = lx >= 112 && lx < 144 && ly < (k == Hole ? 32 : 24); break;
                    case Bottom: inBand = lx >= 96 && lx < 160 && ly >= 144; inZone = lx >= 112 && lx < 144 && ly >= (k == Hole ? 144 : 152); break;
                    case Left:   inBand = lx < 40 && ly >= 64 && ly < 112;   inZone = lx < (k == Hole ? 32 : 16); break;
                    default:     inBand = lx >= 216 && ly >= 64 && ly < 112; inZone = lx >= (k == Hole ? 224 : 240); break;
                }
                if (!inBand) continue;
                const bool covered = inZone && !black(p.sx, p.sy);
                if (covered) act[i] = Hide; else if (act[i] == Keep && i < nLink) act[i] = Show;
                if (k == Hole && (s == Left || s == Right) && i < nLink) { colRule[i] = 1; if (covered) lowHidden[p.sx] = std::max(lowHidden[p.sx], p.sy); }
            }
        }
    }
    for (size_t i = 0; i < link.size(); i++) {
        const P& p = link[i];
        if (colRule[i] && p.sy < lowHidden[p.sx]) act[i] = Hide;                      // above a covered pixel of its column: covered
        if (act[i] == Keep && !(ppu.sprCol[(size_t)(p.sy + HUD_H) * Ppu::SPR_W + p.sx] == 0xFF && i < nLink)) continue;   // (kept: the game's own pixel; a dropped one of Link's is drawn)
        if (act[i] == Keep) act[i] = Show;
        const int cy = top + p.sy;
        if (cy < 0 || cy >= c.h || p.sx >= c.w) continue;
        if (act[i] == Hide && (ppu.mask & 1)) continue;                              // (the bomb's gray flash: the background layer is not gray)
        c.pixels[(size_t)cy * c.w + p.sx] = act[i] == Show ? Ppu::nesColor((ppu.mask & 1) ? (p.col & 0x30) : p.col) : ppu.bgColor[(size_t)(p.sy + HUD_H) * Ppu::W + p.sx];
    }
}

// Sprites that stick out above or below the room (the candle's fire and a bomb's smoke at the top/bottom wall, a monster in the wall): the game's picture ends
// at the room, so they were cut off. Drawn into the canvas margin above/below the room (when the status bar is an overlay). Not Link: he goes into the wall.
void Widescreen::spritesBeyondRoom(Nes& nes, Canvas& c, int top) {
    const Ppu& ppu = nes.ppu;
    for (int py = 0; py < Ppu::SPR_H; py++) {
        if (py >= HUD_H && py < HUD_H + ROOM_H) continue;
        const int cy = top + py - HUD_H;
        if (cy < 0 || cy >= c.h) continue;
        for (int px = 0; px < ROOM_W && px < c.w; px++) {
            const size_t id = (size_t)py * Ppu::SPR_W + px;
            const uint8_t o = ppu.sprOam[id];
            if (ppu.sprCol[id] == 0xFF || o >= 64 || o == kLinkOam0 || o == kLinkOam1 || hudSpriteEntry(ppu.oam, o)) continue;
            c.pixels[(size_t)cy * c.w + px] = Ppu::nesColor(ppu.sprCol[id]);
        }
    }
}

// The PPU draws at most 8 sprites per line and leaves out the rest; in a dungeon the torches in the corners, Link, the candle's fire and a sword beam's
// splash together go over that at the top and bottom walls (the rows of the corner torches), so the effect vanished there while it was fine at the side walls.
// The sprites the PPU left out (Ppu::dropCol, not the ones meant to be behind the background) are drawn here, anywhere on the canvas.
void Widescreen::drawDroppedSprites(Nes& nes, Canvas& c, int top, bool skipLink) {   // skipLink: linkAtDoors draws Link's own dropped pixels
    const Ppu& ppu = nes.ppu;
    for (int py = 0; py < Ppu::SPR_H; py++) {
        const int cy = top + py - HUD_H;
        if (cy < 0 || cy >= c.h) continue;
        for (int px = 0; px < ROOM_W && px < c.w; px++) {
            const size_t id = (size_t)py * Ppu::SPR_W + px;
            if (ppu.dropCol[id] == 0xFF || ppu.sprCol[id] != 0xFF || hudSpriteEntry(ppu.oam, ppu.dropOam[id])) continue;
            if (skipLink && (ppu.dropOam[id] == kLinkOam0 || ppu.dropOam[id] == kLinkOam1)) continue;
            c.pixels[(size_t)cy * c.w + px] = Ppu::nesColor(ppu.dropCol[id]);
        }
    }
}
