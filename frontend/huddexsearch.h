#pragma once
// The Pokedex's search redrawn in the HUD style: the menu of what to search by (order, first
// letter, types, height, weight, area, body style) with the values chosen, and each of the
// pages that choose one, laid out as the game's buttons are so the d-pad moves the same way.
// The game keeps the input; all is read from its memory each frame (PokedexApp_MainSeq_21
// to _56 in src/application/pokedex/ov18_021E8BF4.c). The body styles' pictures are taken
// from the game's own touch screen while their page is up, as nothing else has them.
#include "huddexinfo.h"

namespace HudDexSearch
{
enum Page { Menu, Order, Name, Type, Height, Weight, Area, Body };

struct View
{
    Page page = Menu;
    int cursor = -1;            // the game's button under the cursor (-1: none, touching)
    uint32_t enabled = ~0u;     // the buttons that can be chosen (the game's flags)
    uint32_t crit[10] = {};     // the criteria (pokedex_internal_constants.h)
    // The page's buttons: their centres on the touch screen (DpadMenuBox), and how many.
    int n = 0, bx[32] = {}, by[32] = {};
    // The heights (decimetres) and weights (hectograms) of the ranges' ends, as the
    // criteria's indexes stand for them.
    int h[2] = {}, w[2] = {};
    bool steady = false;        // the page itself, not fading or flashing (its pictures can be taken)
    int message = -1;           // text bank 802's line shown over the menu, or -1
    bool national = false;
};

// The page a step of PokedexApp_RunMainSeq shows: 0 none, -1 a step that only fades or
// flashes a button (the picture before stays), else the page + 1. The step that waits for a
// fade (3) counts as the one it goes on to (+0x85C).
inline int PageOf(int seq, int sub)
{
    switch (seq)
    {
    case 21: return sub == 1 ? Menu + 1 : -1;
    case 22: case 23: case 24: return Menu + 1;
    case 29: return Order + 1;
    case 32: return Name + 1;
    case 35: return Type + 1;
    case 38: case 40: case 41: case 42: case 43: return Height + 1;
    case 45: case 47: case 48: case 49: case 50: return Weight + 1;
    case 52: return Area + 1;
    case 55: return Body + 1;
    case 4: case 25: case 26: case 27: case 28: case 30: case 31: case 33: case 34: case 36: case 37: case 39: case 44: case 46:
    case 51: case 53: case 54: case 56: return -1;
    default: return 0;
    }
}

// How many buttons each page has (its GridInputHandler's DpadMenuBox table).
constexpr int kButtons[8] = { 10, 8, 29, 20, 8, 8, 6, 17 };

// Reads the search (1, -1 or 0 as HudDexInfo::Read). PokedexAppData: the criteria +0x1868,
// the page's buttons (GridInputHandler +0x1864: the d-pad's boxes +4, whether the buttons
// are used +8, the one under the cursor +0x0D, the enabled flags +0x10), the slider's values
// +0x1850 (by index: height, weight), whether the National Pokedex is had +0x1860; the step
// of seq 23 +0x868 (15-17: nothing found).
inline int Read(const HudParty::Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, View& v)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t d = ram.R32(m + 0x1C), args = ram.R32(m + 0x18);
        return ram.Ok(d, 0x1910) && ram.R32(d) == args && ram.R32(ram.R32(args)) == 0xBEEFCAFE;
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2) return 0;
    const uint32_t d = ram.R32(mgr + 0x1C);
    int seq = (int)ram.R32(mgr + 0x14);
    if (seq == 3) seq = (int)ram.R32(d + 0x85C);
    const int sub = (int)ram.R16(d + 0x868);
    const int page = PageOf(seq, sub);
    if (page <= 0) return page;
    v = View();
    v.page = (Page)(page - 1);
    for (int i = 0; i < 10; i++) v.crit[i] = ram.R32(d + 0x1868 + 4 * i);
    const uint32_t* c = v.crit;
    if (c[0] > 5 || c[1] > 26 || c[2] > 17 || c[3] > 17 || c[4] > c[5] || c[5] > 152 || c[6] > c[7] || c[7] > 152 || c[8] > 3 || c[9] > 14)
        return 0;
    v.national = ram.R32(d + 0x1860) == 1;
    const uint32_t g = ram.R32(d + 0x1864), table = ram.R32(d + 0x1850);
    if (!ram.Ok(g, 0x20) || !ram.Ok(table, 4 * 153)) return 0;
    for (int k = 0; k < 2; k++)
    {
        v.h[k] = (int)ram.R16(table + 4 * c[4 + k]);
        v.w[k] = (int)ram.R16(table + 4 * c[6 + k] + 2);
    }
    v.steady = (int)ram.R32(mgr + 0x14) == seq;
    v.n = kButtons[v.page];
    const uint32_t boxes = ram.R32(g + 4);
    if (!ram.Ok(boxes, 8 * v.n)) return 0;
    for (int i = 0; i < v.n; i++) v.bx[i] = (int)ram.R8(boxes + 8 * i), v.by[i] = (int)ram.R8(boxes + 8 * i + 1);
    v.cursor = ram.R32(g + 8) ? (int)ram.R8(g + 0x0D) : -1;
    if (v.cursor >= v.n) return 0;
    v.enabled = ram.R32(g + 0x10);
    if (seq == 23) v.message = sub >= 15 ? 23 : 25; // "No matching Pokemon were found." / "Searching..."
    return 1;
}

// A height (decimetres) and a weight (hectograms) as the search writes them
// (ov18_021EFD00, ov18_021EFDB4): feet and inches, pounds to a tenth; the top of the range
// reads 99'00" and 9999.0 lbs.
inline std::string HeightText(int dm)
{
    const int in = dm == 999 ? 1188 : (dm * 10000 / 254 + 5) / 10;
    char b[24];
    snprintf(b, sizeof b, "%d\xE2\x80\x99%02d\xE2\x80\x9D", in / 12, in % 12);
    return b;
}
inline std::string WeightText(int hg)
{
    const int t = hg == 9999 ? 99990 : (int)(((long long)hg * 220462 + 50000) / 100000);
    return std::to_string(t / 10) + "." + std::to_string(t % 10) + " lbs.";
}

// The type a search type stands for (DEX_SEARCH_TYPE_*: the game's types without the
// Mystery type, 9).
inline int GameType(int t) { return t < 9 ? t : t + 1; }

// The body styles' pictures, taken from the game's touch screen (256 by 192, 0xRRGGBB) at the
// centres of their buttons while the page is shown: the silhouette's grey, whether the
// button is chosen (teal) or not (white). Kept once taken.
struct Shapes
{
    Image img[14];
    void Take(const View& v, const uint32_t* screen)
    {
        if (!screen || v.page != Body || !v.steady) return;
        for (int s = 0; s < 14; s++)
        {
            if (!img[s].px.empty()) continue;
            // Only from a button drawn as it should be: its corner white, or teal when chosen.
            const int cx = v.bx[s] - 12, cy = v.by[s] - 12;
            if (cx < 0 || cy < 0 || cx > 255 || cy > 191) continue;
            const uint32_t q = screen[cy * 256 + cx];
            const int qr = (q >> 16) & 0xFF, qg = (q >> 8) & 0xFF, qb = q & 0xFF;
            if (!(std::min({ qr, qg, qb }) > 200 || (qb > 150 && qr < 100))) continue;
            Image im;
            im.w = im.h = 26;
            int dark = 0;
            for (int y = 0; y < 26; y++)
                for (int x = 0; x < 26; x++)
                {
                    const int sx = v.bx[s] - 13 + x, sy = v.by[s] - 13 + y;
                    uint32_t px = 0;
                    if (sx >= 0 && sx < 256 && sy >= 0 && sy < 192)
                    {
                        const uint32_t p = screen[sy * 256 + sx];
                        const int r = (p >> 16) & 0xFF, gg = (p >> 8) & 0xFF, b = p & 0xFF;
                        if (std::max({ r, gg, b }) < 160 && std::max({ r, gg, b }) - std::min({ r, gg, b }) < 24) px = 0xFF3B3F4A, dark++;
                    }
                    im.px.push_back(px);
                }
            // A picture with nothing in it (the screen faded or not drawn yet) is not kept.
            if (dark > 20) img[s] = im;
        }
    }
    const Image* Get(int s) const { return s >= 0 && s < 14 && !img[s].px.empty() ? &img[s] : nullptr; }
};

// What each of the menu's rows is called and what it shows.
inline std::string ValueOf(const View& v, int row)
{
    const uint32_t* c = v.crit;
    switch (row)
    {
    case 0: return Party_Text(802, 41 + c[0]);
    case 1: return c[1] == 26 ? "Any" : std::string(1, (char)('A' + c[1]));
    case 2:
    {
        if (c[2] == 17 && c[3] == 17) return "Any";
        std::string s = c[2] == 17 ? "" : HudParty::TypeName(GameType(c[2]));
        if (c[3] != 17) s += (s.empty() ? "" : " / ") + HudParty::TypeName(GameType(c[3]));
        return s;
    }
    case 3: return c[4] == 0 && c[5] == 152 ? "Any" : HeightText(v.h[0]) + " ~ " + HeightText(v.h[1]);
    case 4: return c[6] == 0 && c[7] == 152 ? "Any" : WeightText(v.w[0]) + " ~ " + WeightText(v.w[1]);
    case 5: return c[8] == 3 ? "Any" : HudScreens::TitleCase(Party_Text(802, 65 + c[8]));
    default: return c[9] == 14 ? "Any" : "";
    }
}

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const View& v, const Shapes& shapes, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float x0 = ui.W / ui.s / 2 - 960;
    HudSprite(c, ui, sp, "people/chip_ring_red", x0 + 60, 40, 96, 96);
    HudSprite(c, ui, sp, "icons/icon_search", x0 + 84, 64, 48, 48);
    HudText(c, ui, f, x0 + 176, 64, 48, "SEARCH", Ink, 0, true);

    // The menu: what to search by, with the values chosen; the one being chosen marked.
    static const char* rows[7] = { "Order", "Name", "Type", "Height", "Weight", "Area", "Body style" };
    const float lx = x0 + 60, ly = 160, lw = 760, lh = 820;
    HudOnline::TitledCard(c, ui, sp, f, lx, ly, lw, lh, "SEARCH BY");
    const int at = v.page == Menu ? v.cursor : (int)v.page - 1;
    for (int r = 0; r < 7; r++)
    {
        const float ry = ly + 90 + r * 84;
        const bool here = r == at;
        HudPanel(c, ui, sp, here ? "panels/row_selected" : "panels/row", lx + 24, ry, lw - 48, 68);
        if (here && v.page == Menu) HudSprite(c, ui, sp, "cursor/cursor_small", lx + 30, ry + 18, 30, 32);
        HudText(c, ui, f, lx + 70, ry + 18, 32, rows[r], Muted);
        const std::string val = ValueOf(v, r);
        if (r == 6 && val.empty())
        {
            if (const Image* s = shapes.Get((int)v.crit[9])) HudPixelArt(c, ui, s, lx + lw - 100, ry + 4, 60, 60);
        }
        else
            HudText(c, ui, f, lx + lw - 50, ry + 18, 32, val, Ink, 2);
    }
    const char* buttons[3] = { "Reset", "Start", "Cancel" };
    for (int k = 0; k < 3; k++)
    {
        const float bw = 210, bx = lx + 24 + k * (bw + 20), by = ly + lh - 110;
        HudPanel(c, ui, sp, v.page == Menu && v.cursor == 7 + k ? "panels/button_yellow" : "panels/button_white", bx, by, bw, 64);
        HudText(c, ui, f, bx + bw / 2, by + 16, 32, buttons[k], k == 2 ? Danger : Ink, 1);
    }

    // The page choosing a value, or what the row under the cursor does.
    const float rx = x0 + 860, ry = 160, rw = 1000, rh = 820;
    static const int about[7] = { 16, 17, 18, 20, 19, 22, 21 };
    const int topic = v.page == Menu ? (v.cursor >= 0 && v.cursor < 7 ? about[v.cursor] : 13) : about[v.page - 1];
    HudOnline::TitledCard(c, ui, sp, f, rx, ry, rw, rh, v.page == Menu ? "SEARCH" : HudScreens::TitleCase(rows[v.page - 1]));
    HudText(c, ui, f, rx + 40, ry + 100, 32, Party_Text(802, topic), Muted);
    if (v.page == Menu)
    {
        if (v.message >= 0)
        {
            HudPanel(c, ui, sp, "panels/dialogue_box", rx + 60, ry + 340, rw - 120, 120);
            HudText(c, ui, f, rx + rw / 2, ry + 380, 48, Party_Text(802, v.message), Ink, 1, true);
        }
        else
        {
            HudSprite(c, ui, sp, "people/chip_ring_red", rx + rw / 2 - 120, ry + 260, 240, 240);
            HudSprite(c, ui, sp, "icons/icon_search", rx + rw / 2 - 60, ry + 320, 120, 120);
        }
    }
    else if (v.page == Height || v.page == Weight)
    {
        // The two ends of the range, each with its arrows, as the game's sliders.
        const bool h = v.page == Height;
        for (int k = 0; k < 2; k++)
        {
            const bool here = v.cursor == k || v.cursor == 4 + 2 * k || v.cursor == 5 + 2 * k;
            const float by = ry + 200 + k * 170, bw = rw - 200, bx = rx + 100;
            HudPanel(c, ui, sp, here ? "panels/row_selected" : "panels/row", bx, by, bw, 110);
            HudText(c, ui, f, bx + 40, by + 36, 32, k ? "Min" : "Max", Muted);
            HudText(c, ui, f, bx + bw / 2, by + 26, 48, h ? HeightText(v.h[1 - k]) : WeightText(v.w[1 - k]), Ink, 1, true);
            HudSprite(c, ui, sp, "cursor/value_arrow_left", bx + bw - 150, by + 35, 40, 40);
            HudSprite(c, ui, sp, "cursor/value_arrow_right", bx + bw - 80, by + 35, 40, 40);
        }
        for (int k = 0; k < 2; k++)
        {
            const float bw = 260, bx = rx + rw / 2 - bw - 20 + k * (bw + 40), by = ry + rh - 130;
            HudPanel(c, ui, sp, v.cursor == 2 + k ? "panels/button_yellow" : "panels/button_white", bx, by, bw, 64);
            HudText(c, ui, f, bx + bw / 2, by + 16, 32, k ? "Cancel" : "OK", k ? Danger : Ink, 1);
        }
    }
    else
    {
        // The game's buttons where the game has them (their centres, on its 256 by 192
        // screen), each with what it stands for.
        const float ax = rx + 40, ay = ry + 150, aw = rw - 80, ah = rh - 190;
        const float bw = v.page == Name ? 96 : v.page == Body ? 130 : v.page == Type ? 200 : 260, bh = v.page == Body ? 130 : 64;
        // The area of the screen the buttons use, so they fill the card.
        int x1 = 255, x2 = 0, y1 = 191, y2 = 0;
        for (int i = 0; i < v.n; i++) x1 = std::min(x1, v.bx[i]), x2 = std::max(x2, v.bx[i]), y1 = std::min(y1, v.by[i]), y2 = std::max(y2, v.by[i]);
        const float kx = x2 > x1 ? (aw - bw) / (x2 - x1) : 0, ky = y2 > y1 ? (ah - bh) / (y2 - y1) : 0;
        const int n = v.n, ok = n - 2;
        for (int i = 0; i < n; i++)
        {
            const float cx = ax + bw / 2 + (v.bx[i] - x1) * kx, cy = ay + bh / 2 + (v.by[i] - y1) * ky;
            const bool on = (v.enabled >> i & 1) != 0;
            std::string label;
            uint32_t ink = on ? Ink : Muted;
            const Image* pic = nullptr;
            bool chosen = false;
            if (i == ok) label = "OK";
            else if (i == ok + 1) label = "Cancel", ink = Danger;
            else if (i == ok - 1 && v.page != Order) label = "Any"; // the game's "----"
            else switch (v.page)
            {
            case Order: label = Party_Text(802, 41 + i); chosen = (int)v.crit[0] == i; break;
            case Name: label = std::string(1, (char)('A' + i)); chosen = (int)v.crit[1] == i; break;
            case Type: label = HudParty::TypeName(GameType(i)); chosen = (int)v.crit[2] == i || (int)v.crit[3] == i; break;
            case Area: label = HudScreens::TitleCase(Party_Text(802, 65 + i)); chosen = (int)v.crit[8] == i; break;
            default: pic = shapes.Get(i); chosen = (int)v.crit[9] == i; break;
            }
            if (i == ok - 1 && v.page != Order)
                chosen = v.page == Name ? v.crit[1] == 26 : v.page == Type ? v.crit[2] == 17 && v.crit[3] == 17 : v.page == Area ? v.crit[8] == 3 : v.crit[9] == 14;
            const bool here = i == v.cursor;
            HudPanel(c, ui, sp, here ? "panels/button_yellow" : chosen ? "panels/button_lavender" : "panels/button_white", cx - bw / 2, cy - bh / 2, bw, bh,
                on ? 0xFFFFFFFF : 0x80FFFFFF);
            if (v.page == Type && i < 17) HudParty::FillRound(c, ui, cx - bw / 2 + 14, cy - 8, 16, 16, 8, HudParty::TypeColour(GameType(i)));
            if (pic) HudPixelArt(c, ui, pic, cx - 52, cy - 52, 104, 104);
            else HudText(c, ui, f, cx + (v.page == Type && i < 17 ? 10 : 0), cy - 16, 32, label, ink, 1);
        }
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
