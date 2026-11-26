// The world beyond the map's edge (the map is 16 x 8 screens; the camera follows Link and shows what lies past it).
// dumpEdges: the Delete key writes the tile ids along the four borders so the extension rules can be written from the real screens.
#include <cstdio>
#include "widescreen.h"
#include "widescreen_internal.h"
#include "zelda_ram.h"

using namespace zelda;
using namespace wd;

namespace {
constexpr int R = OverworldMap::TILE_ROWS;      // 22 tile rows in a screen, 32 columns

char classOf(uint8_t t) {                       // W water, D dock/ladder, M mountain wall, T tree, . ground, ? anything else
    if (isWaterTile(t)) return 'W';
    if (t >= 0x70 && t <= 0x77) return 'D';
    if ((t >= 0xD8 && t <= 0xDB) || (t >= 0xCE && t <= 0xD1)) return 'M';
    if (t >= 0xC4 && t <= 0xC7) return 'T';
    if (t == 0x26) return '.';
    return '?';
}
}

// For every screen on a border: its palettes, then the three outermost tile lines (outermost first) as classes and as ids.
bool Widescreen::dumpEdges(const char* path) {
    if (!fxNes) return false;
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    std::fprintf(f, "edge dump of the 16 x 8 map: per border screen, the 3 outermost tile lines (outermost first): classes (W water, D dock/ladder, M mountain, T tree, . ground, ? other), then tile ids\n"
                    "north/south lines run west to east (32 tiles), west/east lines run north to south (22 tiles)\n");
    static const char* names[4] = {"NORTH", "SOUTH", "WEST", "EAST"};
    for (int side = 0; side < 4; side++) {
        const bool horizontal = side < 2;                       // north / south border: 16 screens along it
        for (int k = 0; k < (horizontal ? 16 : 8); k++) {
            const int rx = horizontal ? k : (side == 2 ? 0 : 15), ry = horizontal ? (side == 0 ? 0 : 7) : k;
            const int room = ry * 16 + rx;
            const uint8_t* tiles = roomTiles(*fxNes, room);
            std::fprintf(f, "\n%s screen column %d row %d (room %02X) palettes outer %d inner %d\n", names[side], rx, ry, room,
                         fxNes->cart.cpuRead(kAttrsA + room) & 3, fxNes->cart.cpuRead(kAttrsB + room) & 3);
            for (int depth = 0; depth < 3; depth++) {
                const int n = horizontal ? 32 : R;
                char cls[40]; int j = 0;
                std::fprintf(f, "  line %d ids:", depth);
                for (int i = 0; i < n; i++) {
                    const int c = horizontal ? i : (side == 2 ? depth : 31 - depth), r = horizontal ? (side == 0 ? depth : R - 1 - depth) : i;
                    const uint8_t t = tiles[c * R + r];
                    cls[j++] = classOf(t);
                    std::fprintf(f, " %02X", t);
                }
                cls[j] = 0;
                std::fprintf(f, "\n  line %d class: %s\n", depth, cls);
            }
        }
    }
    std::fclose(f);
    return true;
}
