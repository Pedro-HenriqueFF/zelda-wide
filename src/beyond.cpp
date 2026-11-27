#include "beyond.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace beyond {

Zone zoneOf(int tx, int ty) {
    const bool inX = tx >= 0 && tx < kMapTilesW, inY = ty >= 0 && ty < kMapTilesH;
    if (inX && inY) return Zone::Inside;
    if (tx >= kMapTilesW) return Zone::Sea;                    // the whole east side, corners included
    const int col = inX ? tx / 32 : (tx < 0 ? -1 : 16);        // screen column (-1 = west of the map)
    if (ty < 0) return col >= 14 ? Zone::Sea : Zone::Extrude;  // above the two top right screens the sea goes on
    if (ty >= kMapTilesH) {
        if (col >= 12) return Zone::Sea;
        if (col >= kPatchFirstCol && col < kPatchFirstCol + kPatchCount) return Zone::Custom;
    }
    return Zone::Extrude;                                      // west side and the rest of the north / south sides
}

Fade fadeFor(int mode) {
    static const Fade modes[kFadeModes] = {{0.5f, 3.5f}, {1.0f, 3.5f}, {0.f, 0.f}};
    return modes[mode < 0 || mode >= kFadeModes ? 1 : mode];
}
const char* fadeName(int mode) {
    static const char* names[kFadeModes] = {"land 0.5 + sea 3.5 screens", "land 1 + sea 3.5 screens", "no fade (the extension goes on to the end of its band)"};
    return names[mode < 0 || mode >= kFadeModes ? 1 : mode];
}

namespace {
// The land's fade length at a pixel: longer where the sea begins beside it (north: from screen column 14; south: from column 4, the hand-made rooms).
float landReach(const Fade& f, int wx, int wy) {
    float a = 1e9f;                                            // distance to the seam in pixels
    if (wy < 0 && wx < 14 * 256) a = (float)(14 * 256 - wx);
    else if (wy >= kMapTilesH * 8 && wx < kPatchFirstCol * 256) a = (float)(kPatchFirstCol * 256 - wx);
    if (a >= kSeamWidth) return f.land;
    const float u = a / kSeamWidth, reach = std::max(f.land, kSeamReach);
    return f.land + (reach - f.land) * std::sqrt(1.f - u * u);
}
}

int fadeVis(int wx, int wy, const Fade& f) {
    const Zone z = zoneOf(wx >> 3, wy >> 3);
    if (z == Zone::Inside || f.land <= 0.f || f.sea <= 0.f) return 256;
    const float dx = wx < 0 ? (float)-wx : wx >= kMapTilesW * 8 ? (float)(wx - kMapTilesW * 8 + 1) : 0.f;
    const float dy = wy < 0 ? (float)-wy : wy >= kMapTilesH * 8 ? (float)(wy - kMapTilesH * 8 + 1) : 0.f;
    const float d = std::sqrt((dx / 256.f) * (dx / 256.f) + (dy / 176.f) * (dy / 176.f));    // distance in screens
    const float t = std::min(1.f, d / (z == Zone::Extrude ? landReach(f, wx, wy) : f.sea));
    return (int)(256.f * (1.f - t * t * (3.f - 2.f * t)) + 0.5f);
}

File defaults() {
    File f;
    auto put = [&](char c, int a, int b, int d, int e, int pal) { Cell x; x.id[0] = (uint8_t)a; x.id[1] = (uint8_t)b; x.id[2] = (uint8_t)d; x.id[3] = (uint8_t)e; x.pal = (uint8_t)pal; f.legend[c] = x; };
    put('~', 0x90, 0x90, 0x95, 0x95, 3);       // sea (the lake interior: the even row $90, the odd row $95)
    put('.', 0x26, 0x26, 0x26, 0x26, 0);       // ground
    put('M', 0xD8, 0xDA, 0xD9, 0xDB, 3);       // mountain wall
    put('T', 0xC4, 0xC6, 0xC5, 0xC7, 3);       // trees
    return f;
}

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
bool hexByte(const std::string& t, int& v) {
    if (t.empty() || t.size() > 2) return false;
    char* end = nullptr; const long x = std::strtol(t.c_str(), &end, 16);
    if (*end) return false;
    v = (int)x; return true;
}
}

bool parse(const std::string& text, File& out, std::string& err) {
    File f = defaults();
    std::vector<std::pair<int, std::string>> lines;            // (line number, text without comment), blank lines dropped
    { std::istringstream in(text); std::string l; int n = 0;
      while (std::getline(in, l)) { n++; const size_t h = l.find('#'); if (h != std::string::npos) l.erase(h); l = trim(l); if (!l.empty()) lines.push_back({n, l}); } }
    auto fail = [&](int n, const std::string& m) { err = "line " + std::to_string(n) + ": " + m; return false; };
    for (size_t i = 0; i < lines.size(); i++) {
        const int n = lines[i].first; std::istringstream ls(lines[i].second); std::string cmd; ls >> cmd;
        if (cmd == "legend") {
            std::string ch, eq, t; ls >> ch >> eq;
            if (ch.size() != 1 || eq != "=") return fail(n, "expected: legend X = (id | id id id id | sq NN) pal P");
            std::vector<int> ids; int pal = -1, sq = -1; bool gotPal = false;
            while (ls >> t) {
                if (t == "sq" && ids.empty() && sq < 0) { std::string q; int v; if (!(ls >> q) || !hexByte(q, v) || v >= kSquareCount) return fail(n, "sq needs a square number 00-37 (hex)"); sq = v; continue; }
                if (t == "pal") { std::string p; if (!(ls >> p) || p.size() != 1 || p[0] < '0' || p[0] > '3') return fail(n, "pal needs 0-3"); pal = p[0] - '0'; gotPal = true; break; }
                int v; if (!hexByte(t, v)) return fail(n, "bad tile id '" + t + "' (hex, 00-FF)"); ids.push_back(v);
            }
            if (!gotPal || (sq >= 0 ? !ids.empty() : (ids.size() != 1 && ids.size() != 4))) return fail(n, "expected: legend X = (1 or 4 tile ids | sq NN) pal 0-3");
            Cell c; c.pal = (uint8_t)pal; c.sq = sq; if (sq < 0) for (int k = 0; k < 4; k++) c.id[k] = (uint8_t)ids[ids.size() == 1 ? 0 : k];
            f.legend[ch[0]] = c;
        } else if (cmd == "below") {
            std::string room, kw, cnt; ls >> room >> kw >> cnt;
            int r, rows = std::atoi(cnt.c_str());
            if (!hexByte(room, r) || r < 0x70 + kPatchFirstCol || r >= 0x70 + kPatchFirstCol + kPatchCount) return fail(n, "below needs a room from 74 to 7B");
            if (kw != "rows" || rows < 1 || rows > kPatchMaxRows) return fail(n, "expected: below HH rows N (N = 1-" + std::to_string(kPatchMaxRows) + ")");
            const int pi = r - 0x70 - kPatchFirstCol;
            if (i + (size_t)rows >= lines.size()) return fail(n, "the patch of room " + room + " has fewer than " + cnt + " lines");
            std::vector<char> g;
            for (int k = 1; k <= rows; k++) {
                const std::string& row = lines[i + k].second;
                if ((int)row.size() != kPatchCols) return fail(lines[i + k].first, "a row needs exactly 16 characters, this one has " + std::to_string(row.size()));
                for (char c : row) if (!f.legend.count(c)) return fail(lines[i + k].first, std::string("'") + c + "' is not in the legend");
                g.insert(g.end(), row.begin(), row.end());
            }
            f.rows[pi] = rows; f.grid[pi] = g; i += rows;
        } else return fail(n, "unknown command '" + cmd + "' (legend, below)");
    }
    out = f; return true;
}

std::string templateText() {
    return
"# Hand-made rooms below the bottom edge of the map (rooms 74 .. 7B). Everything else beyond the map is made by the program:\n"
"# mountains and trees repeat outwards, the sea goes on east, above the two top right screens and below the right-hand screens.\n"
"# Where a patch ends, the sea goes on. Save this file and the game picks it up within a second.\n"
"#\n"
"# A square is 16 x 16 pixels (a room is 16 squares wide and 11 tall). The legend gives a character its four tiles, reading order\n"
"# top-left top-right bottom-left bottom-right (or one tile id for all four), and the palette 0-3 of the area (see the palettes of the\n"
"# border rooms in edge_dump.txt: rooms 74-78 use palettes 2 and 3). Tile ids are hex. Do not use '#' as a character.\n"
"# Built in (you can redefine them):\n"
"#   legend ~ = 90 90 95 95 pal 3      sea\n"
"#   legend . = 26 pal 0               ground\n"
"#   legend M = D8 DA D9 DB pal 3      mountain wall\n"
"#   legend T = C4 C6 C5 C7 pal 3      trees\n"
"# Tile ids seen on the map: water interior 90 (even row) / 95 (odd row), left shore 8E, right shore 92, top edge 8F, top corners 8D / 91,\n"
"# bottom edge 94 / 96 / 98; ground 26; mountain wall D8 DA D9 DB; scalloped mountain top CE D0 CF D1; trees C4-C7.\n"
"#\n"
"# The game's own squares: instead of four tile ids, \"sq NN\" (hex, 00-37) takes the square the game's room layouts use, e.g.\n"
"#   legend W = sq 05 pal 3\n"
"# The Delete key writes squares_sheet.bmp (every square drawn with the current palettes; four blocks = palettes 0, 1, 2, 3 from top to bottom;\n"
"# each square has its number under it) and squares.txt (the tile ids). Press it on the overworld. By their tiles (check them in the sheet):\n"
"#   05 water, 06 / 07 / 08 / 09 water top / bottom / left / right edge, 15 / 16 / 17 / 18 water corners (top-left, top-right, bottom-left,\n"
"#   bottom-right), 04 an inner water corner, 0E ground, 19 trees, 1A scalloped mountain top, 1B mountain wall, 0A ladder, 0B dock.\n"
"#\n"
"# A patch: the room it hangs under, the number of rows (squares, 1-20), then that many lines of exactly 16 characters.\n"
"# Example (half a screen of ground under room 75 with the river going on into the sea; delete the # signs to try it):\n"
"#\n"
"# below 75 rows 5\n"
"# MMMMMM~~~~MMMMMM\n"
"# ......~~~~......\n"
"# ......~~~~......\n"
"# ~~~~~~~~~~~~~~~~\n"
"# ~~~~~~~~~~~~~~~~\n";
}

}  // namespace beyond
