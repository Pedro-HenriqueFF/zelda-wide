// The world beyond the map's edge (the map is 16 x 8 screens; the camera follows Link and shows what lies past it).
// ensureBeyond builds the band of tiles around the map; loadBeyondFile reads the hand-made rooms; dumpEdges: the Delete key writes the tile ids along the four borders so the extension rules can be written from the real screens.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "widescreen.h"
#include "widescreen_internal.h"
#include "beyond.h"
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

std::string Widescreen::loadBeyond(const std::string& text) {
    beyond::File f; std::string err;
    if (!beyond::parse(text, f, err)) return err;
    beyondFile = f; beyondEpoch++;
    return "";
}

bool Widescreen::loadBeyondFile(const std::string& path, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec)) {                                // a template to start from
        std::ofstream o(path); if (!o) return false;
        o << beyond::templateText();
    }
    const fs::file_time_type t = fs::last_write_time(path, ec);
    if (ec) return false;
    const long long m = (long long)t.time_since_epoch().count();
    if (m == beyondMtime) return false;
    beyondMtime = m;
    std::ifstream in(path); std::stringstream ss; ss << in.rdbuf();
    err = loadBeyond(ss.str());
    return true;
}

uint16_t Widescreen::ringTileAt(int gx, int gy) const {
    const int x = gx + kRingL, y = gy + kRingT;
    if (!ringReady || x < 0 || y < 0 || x >= RW || y >= RH || (gx >= 0 && gx < GW && gy >= 0 && gy < GH)) return 0xFFFF;
    return ringTile[(size_t)y * RW + x];
}

uint8_t Widescreen::gridKind(int gx, int gy, bool* valid) const {
    if (gx >= 0 && gx < GW && gy >= 0 && gy < GH) { if (valid) *valid = true; return wwKind.empty() ? 0 : wwKind[(size_t)gy * GW + gx]; }
    const uint16_t t = wwDungeon ? 0xFFFF : ringTileAt(gx, gy);
    if (valid) *valid = t != 0xFFFF;
    return t == 0xFFFF ? 0 : ringKind[(size_t)(gy + kRingT) * RW + gx + kRingL];
}

void Widescreen::ensureBeyond(Nes& nes) {
    if (!extendWorld) { if (ringReady) { ringReady = false; ringTile.clear(); ringKind.clear(); } return; }
    bool open[256]; wd::openTilesOf(nes, open);
    auto outer = [&](int room) { return (int)(nes.cart.cpuRead((uint16_t)(kAttrsA + room)) & 3); };
    auto border = [](int room) { const int rx = room & 15, ry = room >> 4; return rx == 0 || rx == 15 || ry == 0 || ry == 7; };
    uint64_t sig = 1469598103934665603ULL ^ beyondEpoch;
    for (int room = 0; room < 128; room++) if (border(room)) { roomTiles(nes, room); sig = (sig ^ (uint64_t)(cache[room].key + 2)) * 1099511628211ULL; sig = (sig ^ (uint64_t)outer(room)) * 1099511628211ULL; }
    for (int id = 0; id < 256; id++) sig = (sig ^ (uint64_t)(open[id] ? 3 : 2)) * 1099511628211ULL;
    if (ringReady && sig == ringSig) return;
    ringSig = sig;
    ringTile.assign((size_t)RW * RH, 0xFFFF); ringKind.assign((size_t)RW * RH, 0);
    std::vector<uint8_t> water((size_t)RW * RH, 0);             // water tiles of the band and of the border screens (for the big-water count)
    const uint8_t* tilesOfRoom[128] = {};
    for (int room = 0; room < 128; room++) if (border(room)) {
        tilesOfRoom[room] = roomTiles(nes, room);
        for (int col = 0; col < 32; col++) for (int row = 0; row < R; row++) {
            const uint8_t id = tilesOfRoom[room][col * R + row];
            water[(size_t)((room >> 4) * R + row + kRingT) * RW + (room & 15) * 32 + col + kRingL] = wd::isWaterTile(id);
        }
    }
    auto sea = [&](int ty) { return (uint16_t)((((ty & 1) ? 0x95 : 0x90)) | (3 << 8)); };
    for (int ty = -kRingT; ty < GH + kRingB; ty++) for (int tx = -kRingL; tx < GW + kRingR; tx++) {
        const beyond::Zone z = beyond::zoneOf(tx, ty);
        if (z == beyond::Zone::Inside) continue;
        uint16_t t = sea(ty);
        if (z == beyond::Zone::Extrude) {                       // the border's own tiles, repeated outwards in step with their 2 x 2 squares
            const int cx = tx < 0 ? (tx & 1) : tx >= GW ? GW - 2 + (tx & 1) : tx, cy = ty < 0 ? (ty & 1) : ty >= GH ? GH - 2 + (ty & 1) : ty;
            const int room = (cy / R) * 16 + cx / 32;
            t = (uint16_t)(tilesOfRoom[room][(cx % 32) * R + cy % R] | (outer(room) << 8));
        } else if (z == beyond::Zone::Custom) {                 // a hand-made patch under rooms $74..$7B; where it ends: sea
            const int pi = tx / 32 - beyond::kPatchFirstCol, sq = (ty - GH) >> 1;
            if (sq < beyondFile.rows[pi]) {
                const auto it = beyondFile.legend.find(beyondFile.grid[pi][(size_t)sq * beyond::kPatchCols + (tx % 32) / 2]);
                if (it != beyondFile.legend.end()) t = (uint16_t)(it->second.id[(ty & 1) * 2 + (tx & 1)] | (it->second.pal << 8));
            }
        }
        const size_t i = (size_t)(ty + kRingT) * RW + tx + kRingL;
        ringTile[i] = t; water[i] = wd::isWaterTile((uint8_t)t);
    }
    // Big water (waves): the core is water 3+ tiles from anything else, and every water tile within 3 steps of it waves (as in buildWaterWorld).
    std::vector<uint8_t> dist((size_t)RW * RH, 255), big((size_t)RW * RH, 0);
    std::vector<int> q; q.reserve((size_t)RW * RH);
    for (int i = 0; i < RW * RH; i++) if (!water[i]) { dist[i] = 0; q.push_back(i); }
    auto nbrs = [&](int i, int* o) { int n = 0; const int x = i % RW, y = i / RW; if (x > 0) o[n++] = i - 1; if (x < RW - 1) o[n++] = i + 1; if (y > 0) o[n++] = i - RW; if (y < RH - 1) o[n++] = i + RW; return n; };
    for (size_t h = 0; h < q.size(); h++) { int nb[4]; const int n = nbrs(q[h], nb); for (int j = 0; j < n; j++) if (dist[nb[j]] == 255) { dist[nb[j]] = (uint8_t)std::min(250, dist[q[h]] + 1); q.push_back(nb[j]); } }
    std::vector<int> grow; std::vector<uint8_t> step((size_t)RW * RH, 255);
    for (int i = 0; i < RW * RH; i++) if (water[i] && dist[i] >= 3) { big[i] = 1; step[i] = 0; grow.push_back(i); }
    for (size_t h = 0; h < grow.size(); h++) {
        const int i = grow[h]; if (step[i] >= 3) continue;
        int nb[4]; const int n = nbrs(i, nb);
        for (int j = 0; j < n; j++) if (water[nb[j]] && step[nb[j]] == 255) { step[nb[j]] = (uint8_t)(step[i] + 1); big[nb[j]] = 1; grow.push_back(nb[j]); }
    }
    for (int i = 0; i < RW * RH; i++) if (ringTile[i] != 0xFFFF) ringKind[i] = (uint8_t)(wd::tileKind((uint8_t)ringTile[i], open[(uint8_t)ringTile[i]]) | (big[i] ? kBig : 0));
    ringReady = true;
}
