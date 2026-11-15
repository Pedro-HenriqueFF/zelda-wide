// Widescreen: the overlay HUD (status bar corners), minimap and its thumbnails, cave/menu/game-over overlays.
#include "widescreen_internal.h"

namespace {
// Draws `text` with the game's own font: the tiles in the PPU's background pattern table (A-Z = $0A-$23, space $24, dash $62).
void drawText(OverlayImage& img, Nes& nes, int x0, int y0, const char* text) {
    const uint16_t base = (nes.ppu.ctrl & 0x10) ? 0x1000 : 0;
    for (const char* p = text; *p; p++, x0 += 8) {
        const int tile = *p >= 'A' && *p <= 'Z' ? 0x0A + (*p - 'A') : *p == '-' ? 0x62 : 0x24;
        if (tile == 0x24) continue;
        for (int y = 0; y < 8; y++) {
            const uint8_t lo = nes.cart.ppuRead((uint16_t)(base + tile * 16 + y)), hi = nes.cart.ppuRead((uint16_t)(base + tile * 16 + y + 8));
            for (int b = 0; b < 8; b++)
                if (((lo | hi) >> (7 - b)) & 1) { const int px = x0 + b, py = y0 + y; if (px < img.w && py < img.h) img.pixels[(size_t)py * img.w + px] = 0xFFFCFCFC; }
        }
    }
}

// A picture in a white frame with a black margin: `rows` rows of the game's 256-wide picture, starting at `firstRow`.
OverlayImage framedPicture(const uint32_t* firstRow, int rows) {
    const int frame = 2, pad = 2, edge = frame + pad;
    OverlayImage img;
    img.w = Ppu::W + 2 * edge; img.h = rows + 2 * edge;
    img.pixels.assign((size_t)img.w * img.h, 0xFFFCFCFC);                       // frame
    for (int y = frame; y < img.h - frame; y++)
        for (int x = frame; x < img.w - frame; x++) img.pixels[(size_t)y * img.w + x] = 0xFF000000;
    for (int y = 0; y < rows; y++)
        std::memcpy(&img.pixels[(size_t)(y + edge) * img.w + edge], &firstRow[(size_t)y * Ppu::W], Ppu::W * sizeof(uint32_t));
    return img;
}
}

int Widescreen::thumbKey(Nes& nes, int room) {
    const int layout = nes.cart.cpuRead(kAttrsD + room) & 0x7F;
    const int secret = (nes.ram[kWorldFlags + room] & 0x80) ? 1 : 0;
    return layout | (secret << 7) | ((nes.cart.cpuRead(kAttrsA + room) & 3) << 8) | ((nes.cart.cpuRead(kAttrsB + room) & 3) << 10);
}

void Widescreen::bakeThumb(Nes& nes, int room) {
    // Averages the screen's picture down to THUMB_W x THUMB_H pixels, using the overworld's palette.
    const uint8_t* tiles = roomTiles(nes, room);
    const int outer = nes.cart.cpuRead(kAttrsA + room) & 3, inner = nes.cart.cpuRead(kAttrsB + room) & 3;
    uint32_t pal[16];
    for (int i = 0; i < 16; i++) pal[i] = Ppu::nesColor(refPalette[(i & 3) ? i : 0]);
    const uint16_t patBase = (nes.ppu.lineCtrl[100] & 0x10) ? 0x1000 : 0;

    float tr[OverworldMap::TILE_ROWS][OverworldMap::TILE_COLS], tg[OverworldMap::TILE_ROWS][OverworldMap::TILE_COLS],
          tb[OverworldMap::TILE_ROWS][OverworldMap::TILE_COLS];
    for (int col = 0; col < OverworldMap::TILE_COLS; col++)
        for (int row = 0; row < OverworldMap::TILE_ROWS; row++) {
            const bool isInner = (col >> 1) >= 2 && (col >> 1) <= 13 && (row >> 1) >= 2 && (row >> 1) <= 8;
            const int sel = isInner ? inner : outer;
            const uint8_t tile = tiles[col * OverworldMap::TILE_ROWS + row];
            float sr = 0, sg = 0, sb = 0;
            for (int py = 0; py < 8; py++) {
                const uint16_t pat = patBase + tile * 16 + py;
                const uint8_t lo = nes.cart.ppuRead(pat), hi = nes.cart.ppuRead(pat + 8);
                for (int px = 0; px < 8; px++) {
                    const int bit = 7 - px;
                    const int ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                    const uint32_t p = pal[ci ? (sel << 2) | ci : 0];
                    sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255;
                }
            }
            tr[row][col] = sr / 64.0f; tg[row][col] = sg / 64.0f; tb[row][col] = sb / 64.0f;
        }

    Thumb& t = thumbs[room];
    for (int ty = 0; ty < THUMB_H; ty++) {
        const float y0 = ty * (float)OverworldMap::TILE_ROWS / THUMB_H, y1 = (ty + 1) * (float)OverworldMap::TILE_ROWS / THUMB_H;
        for (int tx = 0; tx < THUMB_W; tx++) {
            const float x0 = tx * (float)OverworldMap::TILE_COLS / THUMB_W, x1 = (tx + 1) * (float)OverworldMap::TILE_COLS / THUMB_W;
            float r = 0, g = 0, b = 0, wsum = 0;
            for (int row = (int)y0; row < OverworldMap::TILE_ROWS && row < y1; row++) {
                const float wy = std::min<float>(row + 1, y1) - std::max<float>(row, y0);
                for (int col = (int)x0; col < OverworldMap::TILE_COLS && col < x1; col++) {
                    const float wx = std::min<float>(col + 1, x1) - std::max<float>(col, x0);
                    const float wgt = wx * wy;
                    r += tr[row][col] * wgt; g += tg[row][col] * wgt; b += tb[row][col] * wgt; wsum += wgt;
                }
            }
            t.px[ty * THUMB_W + tx] = 0xFF000000 | ((int)(r / wsum + 0.5f) << 16) | ((int)(g / wsum + 0.5f) << 8) | (int)(b / wsum + 0.5f);
        }
    }
    t.key = thumbKey(nes, room);
}

void Widescreen::bakeThumbs(Nes& nes, int slot) {
    uint32_t sig = 2166136261u;
    for (int i = 0; i < 16; i++) sig = (sig ^ refPalette[i]) * 16777619u;
    if (sig != thumbSig) { thumbSig = sig; for (auto& t : thumbs) t.key = -1; }
    int budget = 16;                                       // a few per frame so that opening a big save doesn't stall
    for (int room = 0; room < 128 && budget > 0; room++) {
        if (fog && !mazeSeen(slot, room)) continue;
        if (thumbs[room].key != thumbKey(nes, room)) { bakeThumb(nes, room); budget--; }
    }
}

OverlayImage Widescreen::buildMinimap(Nes&, const ViewSnapshot& view, int slot) {
    // The overworld map for the status bar: one cell per screen. Style 1: grey cells, blue for the ones you have
    // seen, green for the one you are in. Style 2: a small picture of each screen you have seen, fog for the rest,
    // and a white frame around the current one. (In dungeons the game's own map is used.)
    const int M = 2;                                        // room around the cells for the outline and the frame
    const int cw = minimapStyle == 1 ? 4 : THUMB_W, ch = minimapStyle == 1 ? 4 : THUMB_H;
    OverlayImage img;
    img.w = 16 * cw + 2 * M; img.h = 8 * ch + 2 * M;
    img.pixels.assign((size_t)img.w * img.h, 0x00000000);
    for (int y = M - 1; y < M + 8 * ch + 1; y++)            // dark outline around the whole map
        for (int x = M - 1; x < M + 16 * cw + 1; x++) img.pixels[(size_t)y * img.w + x] = 0xC0000000;
    const int cur = view.inOverworld ? view.room : -1;
    for (int room = 0; room < 128; room++) {
        const int cx = M + (room & 15) * cw, cy = M + (room >> 4) * ch;
        const bool seen = !fog || mazeSeen(slot, room);
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                uint32_t color;
                if (minimapStyle == 1) {
                    color = room == cur ? Ppu::nesColor(0x2A) : seen ? Ppu::nesColor(0x12) : Ppu::nesColor(0x00);
                } else if (seen) {
                    color = thumbs[room].key >= 0 ? thumbs[room].px[y * THUMB_W + x] : Ppu::nesColor(0x00);
                } else {
                    color = 0xFF10131F;                     // fog
                }
                img.pixels[(size_t)(cy + y) * img.w + cx + x] = color;
            }
    }
    if (minimapStyle == 2 && cur >= 0) {                    // white frame just outside the current cell
        const int cx = M + (cur & 15) * cw - 1, cy = M + (cur >> 4) * ch - 1;
        for (int x = 0; x < cw + 2; x++) { img.pixels[(size_t)cy * img.w + cx + x] = 0xFFFCFCFC; img.pixels[(size_t)(cy + ch + 1) * img.w + cx + x] = 0xFFFCFCFC; }
        for (int y = 0; y < ch + 2; y++) { img.pixels[(size_t)(cy + y) * img.w + cx] = 0xFFFCFCFC; img.pixels[(size_t)(cy + y) * img.w + cx + cw + 1] = 0xFFFCFCFC; }
    }
    img.heightPerScale = 360;
    return img;
}

void Widescreen::addHudOverlay(Nes& nes, const ViewSnapshot& view, int slot) {
    // Cuts the game's own status bar (rows 0-63 of its picture) into four pieces and places them in the corners of
    // the window, as in the later Zelda games: life top left, items top right, map bottom left, counters bottom
    // right. Black becomes see-through (a dark panel behind the map and the item boxes), and everything gets a thin
    // dark outline so it stays readable over any scenery. Positions are the status bar's tile positions (see
    // StatusBarStaticsTransferBuf in the disassembly): life at tiles 22-29, items 15-20, counters 11-14, map 2-9.
    struct Piece { int x, y, w, h; float ax, ay, px, py; bool panel; int panelFrom; int heightPerScale; bool isMap; };
    static const Piece pieces[] = {
        {176, 24, 64, 32, 0.02f, 0.03f, 0.0f, 0.0f, false, 0, 288, false},      // -LIFE- and the hearts
        {120, 24, 48, 32, 0.98f, 0.03f, 1.0f, 0.0f, true, 24, 288, false},      // B and A item boxes
        { 16, 16, 64, 40, 0.02f, 0.97f, 0.0f, 1.0f, true, 24, 360, true},      // map ("LEVEL-n" above it in dungeons: no panel there)
        { 88, 24, 32, 32, 0.98f, 0.97f, 1.0f, 1.0f, false, 0, 288, false},      // rupees, keys, bombs
    };
    // The status bar is rebuilt from the background layer and the bar's own sprites (the item icons, the map marker), in
    // colour: the bomb flash (grayscale) and sprites of the scene passing over the top border don't reach it.
    static uint32_t fb[Ppu::W * HUD_H];
    std::memcpy(fb, nes.ppu.bgColor, sizeof fb);
    for (int y = 0; y < HUD_H; y++)
        for (int x = 0; x < Ppu::W; x++) {
            const int k = y * Ppu::SPR_W + x;
            const uint8_t o = nes.ppu.sprOam[k];
            if (o != 0xFF && o != 0 && o < 64 && hudSpriteEntry(nes.ppu.oam, o)) fb[y * Ppu::W + x] = Ppu::nesColor(nes.ppu.sprCol[k]);
        }
    lastHud.clear();
    // The overworld map is drawn by us. While Link walks down the stairs into a dungeon the game already says "dungeon"
    // although the overworld is still on screen, so look at what the PPU shows.
    const bool stairsFromOverworld = view.level != 0 && view.mode == kModeStairs && nametableShowsRoom(nes, view.room);
    for (const Piece& p : pieces) {
        if (p.isMap && (view.level == 0 || stairsFromOverworld) && minimapStyle != 0) {
            ViewSnapshot mv = view;
            if (stairsFromOverworld) mv.inOverworld = true;                     // the room id is still the overworld screen
            OverlayImage mapImg = buildMinimap(nes, mv, slot);
            mapImg.anchorX = p.ax; mapImg.anchorY = p.ay; mapImg.pivotX = p.px; mapImg.pivotY = p.py;
            lastHud.push_back(mapImg);
            overlay.images.push_back(std::move(mapImg));
            continue;
        }
        OverlayImage img;
        img.w = p.w + 2; img.h = p.h + 2;                       // one pixel of room for the outline
        img.pixels.assign((size_t)img.w * img.h, 0x00000000);
        std::vector<uint8_t> solid((size_t)img.w * img.h, 0);
        for (int y = 0; y < p.h; y++)
            for (int x = 0; x < p.w; x++) {
                const uint32_t src = fb[(p.y + y) * Ppu::W + p.x + x];
                const size_t i = (size_t)(y + 1) * img.w + (x + 1);
                if (src & 0x00FFFFFF) { img.pixels[i] = src | 0xFF000000; solid[i] = 1; }
                else if (p.panel && p.y + y >= p.panelFrom) { img.pixels[i] = 0xB0000000; }
            }
        for (int y = 0; y < img.h; y++)
            for (int x = 0; x < img.w; x++) {
                const size_t i = (size_t)y * img.w + x;
                if (img.pixels[i] != 0) continue;
                bool next = false;
                for (int dy = -1; dy <= 1 && !next; dy++)
                    for (int dx = -1; dx <= 1 && !next; dx++) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < img.w && ny < img.h && solid[(size_t)ny * img.w + nx]) next = true;
                    }
                if (next) img.pixels[i] = 0xC0000000;
            }
        img.anchorX = p.ax; img.anchorY = p.ay; img.pivotX = p.px; img.pivotY = p.py;
        img.heightPerScale = p.heightPerScale;                  // 3.75x at 1080p (the map: 3x)
        img.sharpScale = p.heightPerScale != 360;
        lastHud.push_back(img);
        overlay.images.push_back(std::move(img));
    }
}

void Widescreen::buildCaveOverlay(Nes& nes) {
    // The inside of a cave or shop: the game's 256x176 play area in a framed box over the dimmed overworld.
    const uint32_t* room = &nes.ppu.framebuffer[HUD_H * Ppu::W];
    std::vector<uint32_t> lit;
    if (enabled && lighting) {                             // dim, lit by Link's sight and the flames inside
        lit.assign(room, room + Ppu::W * ROOM_H);
        caveLight(nes, lit.data(), Ppu::W, ROOM_H, 0, ROOM_H);
        room = lit.data();
    }
    OverlayImage img = framedPicture(room, ROOM_H);
    img.maxWidthFrac = 0.70f; img.maxHeightFrac = 0.78f;
    overlay.dim = 0.60f;
    lastCaveImage = img; haveCaveImage = true;
    overlay.images.push_back(std::move(img));
}

void Widescreen::buildMenuOverlay(Nes& nes) {
    // The inventory screen: the game's own 256x240 picture (inventory on top, status bar at the bottom, exactly
    // as the original shows it) inside a framed box over the dimmed game picture. The canvas underneath stays as
    // it was, so nothing changes size when the menu opens.
    // The last two rows of the game's picture hold a stray line of the scrolled-out play area; leave them out.
    // The picture also starts with about 30 unused black rows above the "INVENTORY" title: keep only a margin like the
    // one under the status bar at the bottom (8 rows).
    int firstRow = 0;
    for (; firstRow < Ppu::H - 2; firstRow++) {
        bool any = false;
        for (int x = 0; x < Ppu::W && !any; x++) any = nes.ppu.framebuffer[firstRow * Ppu::W + x] & 0x00FFFFFF;
        if (any) break;
    }
    const bool saveHint = nes.ram[kGameMode] == kModePlay && firstRow >= 22;
    const int top = std::max(0, firstRow - (saveHint ? 20 : 8));
    OverlayImage img = framedPicture(&nes.ppu.framebuffer[top * Ppu::W], Ppu::H - 2 - top);
    if (saveHint) drawText(img, nes, (img.w - 22 * 8) / 2, 2 + 2 + 5, "SELECT - SAVE AND QUIT");   // in the unused rows above the title
    img.maxWidthFrac = 0.70f; img.maxHeightFrac = 0.82f;
    overlay.dim = 0.65f;
    overlay.images.push_back(std::move(img));
    if (saveConfirm && menuOpenForSave(nes)) {                                  // the question in the middle, same pixel size as the inventory picture
        constexpr int kW = 25 * 8 + 16, kH = 6 * 8 + 16;
        OverlayImage box; box.w = kW; box.h = kH;
        box.pixels.assign((size_t)kW * kH, 0xFFFCFCFC);
        for (int y = 2; y < kH - 2; y++) for (int x = 2; x < kW - 2; x++) box.pixels[(size_t)y * kW + x] = 0xFF000000;
        const int x1 = (kW - 21 * 8) / 2, x2 = (kW - 17 * 8) / 2, xy = kW / 2 - 8 * 8 + 8, xn = kW / 2 + 8;   // (YES left, NO right, side by side)
        drawText(box, nes, x1, 10, "ARE YOU SURE YOU WANT");
        drawText(box, nes, x2, 22, "TO SAVE AND QUIT");
        drawText(box, nes, xy, 42, "YES");
        drawText(box, nes, xn + 8, 42, "NO");
        auto bits = [&](const uint8_t* rows, int x0, int y0) { for (int y = 0; y < 8; y++) for (int b = 0; b < 8; b++) if (rows[y] >> (7 - b) & 1) box.pixels[(size_t)(y0 + y) * kW + x0 + b] = 0xFFFCFCFC; };
        static const uint8_t qmark[8] = {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00};       // ('?' is not in the game's letters: drawn by hand)
        static const uint8_t arrow[8] = {0x00, 0x20, 0x30, 0x38, 0x30, 0x20, 0x00, 0x00};       // the cursor before the chosen answer
        bits(qmark, x2 + 16 * 8, 22);
        bits(arrow, saveConfirmYes ? xy - 12 : xn - 4, 42);
        box.heightPerScale = 244;
        overlay.images.push_back(std::move(box));
    }
}

void Widescreen::buildGameOverOverlay(Nes& nes) {
    // The "GAME OVER" screen: just the text, bigger and in the middle of the window, without the status bar.
    const uint32_t* fb = nes.ppu.framebuffer;
    int x0 = Ppu::W, x1 = -1, y0 = Ppu::H, y1 = -1;
    for (int y = HUD_H; y < FRAME_H; y++)
        for (int x = 0; x < Ppu::W; x++)
            if (fb[y * Ppu::W + x] & 0x00FFFFFF) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 < x0) return;
    OverlayImage img;
    img.w = x1 - x0 + 1; img.h = y1 - y0 + 1;
    img.pixels.resize((size_t)img.w * img.h);
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++) img.pixels[(size_t)y * img.w + x] = fb[(y0 + y) * Ppu::W + x0 + x];
    img.maxWidthFrac = 0.26f; img.maxHeightFrac = 0.30f;
    overlay.images.push_back(std::move(img));
}
