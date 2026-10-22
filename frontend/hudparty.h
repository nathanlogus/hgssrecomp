#pragma once
// The party menu (online-HUD design 20-ss-party) and the Pokemon summary (21-ss-summary)
// redrawn in the HUD style, drawn over the game's picture while the game's own screens run.
// The game keeps taking the input; every frame the redrawn screen mirrors what the game's
// application holds in memory: which Pokemon the cursor is on, the context menu and its
// cursor, which Pokemon and page the summary shows. Whenever anything does not check out,
// or the game is on a step these screens do not draw (using an item, the move details,
// giving an item...), nothing is drawn and the game's own picture shows.
#include <cmath>
#include <functional>
#include "hudscreens.h"
#include "party.h"

namespace HudParty
{
// Main RAM, read only.
struct Ram
{
    const uint8_t* p = nullptr;
    uint32_t size = 0;
    bool Ok(uint32_t a, uint32_t n = 4) const { return (a >> 24) == 0x02 && (a & 0xFFFFFF) + n <= size; }
    uint32_t R8(uint32_t a) const { return Ok(a, 1) ? p[a & 0xFFFFFF] : 0; }
    uint32_t R16(uint32_t a) const { return Ok(a, 2) ? RomGet16(p + (a & 0xFFFFFE)) : 0; }
    uint32_t R32(uint32_t a) const { return Ok(a, 4) ? RomGet32(p + (a & 0xFFFFFC)) : 0; }
    bool Ptr(uint32_t a) const { return Ok(a) && !(a & 3); }
};

// A game application's OverlayManager (src/overlay_manager.c): the template copied into it
// (its init, exec and exit functions, Thumb), the stage (+0x10: 2 while its exec function
// runs), the step of that function (+0x14), its arguments (+0x18) and its data (+0x1C).
// Found by the template plus `check`, and kept while it still matches.
inline uint32_t FindApp(const Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit,
    const std::function<bool(uint32_t mgr)>& check)
{
    auto looks = [&](uint32_t a) {
        return (ram.R32(a) & ~1u) == init && (ram.R32(a + 4) & ~1u) == exec && (ram.R32(a + 8) & ~1u) == exit &&
            ram.Ptr(ram.R32(a + 0x18)) && ram.Ptr(ram.R32(a + 0x1C)) && check(a);
    };
    if (cache && looks(cache)) return cache;
    cache = 0;
    if (!init || !exec || !exit) return 0;
    // The first word alone, straight from memory, before the rest is looked at.
    for (uint32_t off = 0; off + 0x20 <= ram.size; off += 4)
        if ((RomGet32(ram.p + off) & ~1u) == init && looks(0x02000000 + off)) return cache = 0x02000000 + off;
    return 0;
}

// The player's party (a Party: the most, 6; the count; then 236 bytes each), decoded; false
// unless all of it checks out.
inline bool ReadParty(const Ram& ram, uint32_t party, int& count, PartyMon* mons)
{
    if (!ram.Ptr(party) || ram.R32(party) != 6) return false;
    count = (int)ram.R32(party + 4);
    if (count < 1 || count > 6) return false;
    for (int i = 0; i < count; i++)
        if (!Party_ReadMon(party + 8 + 236 * i, mons[i])) return false;
    return true;
}

// Text of the game's String (HudScreens::GameString).
inline bool ReadString(const Ram& ram, uint32_t s, std::string& out)
{
    return HudScreens::GameString([&](uint32_t a) { return ram.R16(a); }, s, out);
}

using HudScreens::TitleCase;

// The party menu (src/party_menu.c), from the field's menu or from the bag (using or giving
// an item).
struct PartyScreen
{
    int count = 0;
    PartyMon mons[6];
    int cursor = 0;                  // 0-5 a Pokemon, 7 Cancel (6 too: the selection's own number for it)
    int step = 0;                    // PartyMenuState
    int context = 0;                 // PartyMenuContext: 0 the field, 5 using an item, 9 and 10 giving one
    std::vector<std::string> items;  // the context menu, when open
    int sel = 0;                     // its cursor
    bool columns = false;            // laid out in two columns (the first menu, with field moves)
    int from = -1;                   // the Pokemon being moved (Switch)
    std::string prompt;              // what the game asks at the top, when it is not the usual
    std::string message;             // the game's message, while one is shown
    int yesNo = -1;                  // the cursor of the yes/no under it, -1 when there is none
};

// "POKéMON" in the game's text as the HUD writes it.
inline std::string PokemonCase(std::string s)
{
    for (size_t at; (at = s.find("POK\xC3\xA9MON")) != std::string::npos;) s.replace(at, 8, "Pok\xC3\xA9mon");
    return s;
}

// PartyMenu (include/party_menu.h): args +0x654, the context menu's cursor +0x824, what is
// drawn of each Pokemon (+0x828, 0x30 each: the species +4, HP +6 of +8, which an item's
// healing counts up), the message being shown (formattedStrBuf +0x7C8), the yes/no
// (+0xC88), the Pokemon the cursor is on +0xC65, the one being switched (6 bits) and
// whether that is going on (bit 6) at +0xC63. PartyMenuArgs: the party +0, the field system
// +0x1C, the context +0x24, the item being used or given +0x28.
inline bool ReadPartyScreen(const Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, PartyScreen& s)
{
    const uint32_t fs = ram.R32(0x021D4158);
    const uint32_t mgr = FindApp(ram, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t pm = ram.R32(m + 0x1C), args = ram.R32(m + 0x18);
        return ram.Ok(pm, 0xCA8) && ram.R32(pm + 0x654) == args && fs && ram.R32(args + 0x1C) == fs;
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2) return false;
    const uint32_t pm = ram.R32(mgr + 0x1C), args = ram.R32(mgr + 0x18);
    s.context = (int)ram.R8(args + 0x24);
    s.step = (int)ram.R32(mgr + 0x14);
    const int item = (int)ram.R16(args + 0x28);
    // Drawn, by context: opening and closing (0, 32, 33), choosing (1, 4, 8), the context
    // menu and the Item one under it (2, 15), choosing where to move a Pokemon and moving it
    // (28, 29), an item used (5) or given (16), and the messages (9, 11, 17, 24, 25) and
    // yes/no (10, 27).
    // Not drawn: choosing a move to restore the PP of, learning a move, the level-up
    // window of a Rare Candy, form changes, an evolution stone (whose ABLE / NOT ABLE on each
    // Pokemon is not read), the other contexts (battle, contests...).
    static const std::vector<int> field = { 0, 1, 2, 9, 10, 11, 15, 17, 24, 25, 27, 28, 29, 32, 33 };
    static const std::vector<int> use = { 0, 4, 5, 24, 25, 32, 33 }, give = { 0, 8, 9, 10, 11, 16, 24, 25, 32, 33 };
    const std::vector<int>* shown = s.context == 0 ? &field : s.context == 5 ? &use
        : s.context == 9 || s.context == 10 ? &give : nullptr;
    if (!shown || std::find(shown->begin(), shown->end(), s.step) == shown->end() || (s.step == 5 && item == 50)) return false;
    if (!ReadParty(ram, ram.R32(args), s.count, s.mons)) return false;
    s.cursor = (int)ram.R8(pm + 0xC65);
    if (s.cursor > 7) return false;
    // HP as drawn, which counts up while an item heals.
    int hpNow = -1;
    for (int i = 0; i < s.count; i++)
    {
        const uint32_t d = pm + 0x828 + 0x30 * i;
        const int hp = (int)ram.R16(d + 6);
        if ((int)ram.R16(d + 4) != s.mons[i].species || (int)ram.R16(d + 8) != s.mons[i].stats[0] || hp > s.mons[i].stats[0]) continue;
        if (i == s.cursor) hpNow = s.mons[i].hp;
        s.mons[i].hp = hp;
    }
    if (s.context == 5) s.prompt = PokemonCase(Party_Text(300, 33)); // "Use on which POKéMON?"
    if (s.context == 9) s.prompt = PokemonCase(Party_Text(300, 32)); // "Give to which POKéMON?"
    // The message, once an item's healing has counted up.
    static const int messageSteps[] = { 9, 10, 11, 17, 24, 25, 27 };
    const bool message = std::find(std::begin(messageSteps), std::end(messageSteps), s.step) != std::end(messageSteps);
    if (message ||(s.step == 5 && (s.cursor >= s.count || s.mons[s.cursor].hp == hpNow)))
    {
        if (!ReadString(ram, ram.R32(pm + 0x7C8), s.message) || s.message.empty()) return false;
        s.message = PokemonCase(s.message);
    }
    if (s.step == 10 || s.step == 27)
    {
        // As PartyMenu_CreateYesNoPrompt places it (25, 10); the cursor at +0x75. Once
        // answered, it is freed: the message alone then.
        const uint32_t yn = ram.R32(pm + 0xC88);
        s.yesNo = (int)ram.R8(yn + 0x75);
        if (!ram.Ptr(yn) || ram.R8(yn + 0x70) != 25 || ram.R8(yn + 0x71) != 10 || s.yesNo > 1) s.yesNo = -1;
    }
    const uint32_t flags = ram.R8(pm + 0xC63);
    s.from = s.step == 28 && (flags & 0x40) ? (int)(flags & 0x3F) : -1;
    if (s.from >= s.count) return false;
    if (s.step == 2 || s.step == 15)
    {
        // PartyMenuContextMenuCursor: selection +1, the count +2, its kind +3, then the menu,
        // whose list (+4) holds a String and a value for each item. The first menu (kind 0)
        // with field moves puts them in a column of their own on the left (sButtonWindowIDs:
        // items 0-2 on the right, 3 Quit below them, 4 on the field moves), reached with
        // LEFT and RIGHT.
        const uint32_t cur = ram.R32(pm + 0x824);
        if (!ram.Ptr(cur) || s.cursor >= s.count) return false;
        const int n = (int)ram.R8(cur + 2);
        s.sel = (int)ram.R8(cur + 1);
        s.columns = ram.R8(cur + 3) == 0 && n > 4;
        const uint32_t list = ram.R32(cur + 4);
        if (n < 1 || n > 12 || s.sel >= n || !ram.Ptr(list)) return false;
        for (int i = 0; i < n; i++)
        {
            std::string t;
            if (!ReadString(ram, ram.R32(list + 8 * i), t)) return false;
            s.items.push_back(TitleCase(t));
        }
    }
    return true;
}

// The summary (asm/unk_02088288.s, PokemonSummary): its data holds the page (+0x7BC: 0 the
// trainer memo, 1 skills and moves, 2 ribbons) and its arguments (+0x22C); those hold the
// party (+0), how the Pokemon are given (+0x11, 1: a party), the mode (+0x12, 0: just
// looking, not learning a move), the count (+0x13), which one is shown (+0x14) and
// whether the National Pokedex is had (+0x1C).
struct SummaryScreen
{
    PartyMon mon;
    int page = 0, slot = 0, count = 0;
    bool national = false;           // the National Pokedex is had: its numbers are shown
};

inline bool ReadSummaryScreen(const Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, SummaryScreen& s)
{
    const uint32_t mgr = FindApp(ram, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t w = ram.R32(m + 0x1C);
        return ram.Ok(w, 0x7D8) && ram.R32(w + 0x22C) == ram.R32(m + 0x18);
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2) return false;
    // Drawn: opening, looking (2), after changing Pokemon (19), closing (21, 22). Choosing a
    // move to see its details and the rest are the game's own.
    const int step = (int)ram.R32(mgr + 0x14);
    if (step != 1 && step != 2 && step != 19 && step != 21 && step != 22) return false;
    const uint32_t w = ram.R32(mgr + 0x1C), args = ram.R32(mgr + 0x18);
    if (ram.R8(args + 0x11) != 1 || ram.R8(args + 0x12) != 0) return false;
    PartyMon mons[6];
    if (!ReadParty(ram, ram.R32(args), s.count, mons)) return false;
    s.slot = (int)ram.R8(args + 0x14);
    s.page = (int)(int8_t)ram.R8(w + 0x7BC);
    s.national = ram.R32(args + 0x1C) != 0;
    if (s.slot >= s.count || (int)ram.R8(args + 0x13) != s.count || s.page < 0 || s.page > 2) return false;
    s.mon = mons[s.slot];
    return true;
}

// Drawing helpers --------------------------------------------------------------------------

// A filled rectangle with round corners of radius `r` (design pixels), edges smoothed.
inline void FillRound(Canvas& c, const Ui& ui, float x, float y, float w, float h, float r, uint32_t argb)
{
    const float X = ui(x), Y = ui(y), W = ui(w), H = ui(h), R = std::min(ui(r), std::min(W, H) / 2);
    const int x0 = std::max(0, (int)X), y0 = std::max(0, (int)Y), x1 = std::min(c.w, (int)std::ceil(X + W)), y1 = std::min(c.h, (int)std::ceil(Y + H));
    if (x1 <= x0 || y1 <= y0) return;
    c.Mark(x0, y0, x1, y1);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
        {
            const float px = xx + 0.5f, py = yy + 0.5f;
            const float dx = std::max(std::max(X + R - px, px - (X + W - R)), 0.0f), dy = std::max(std::max(Y + R - py, py - (Y + H - R)), 0.0f);
            float cov = std::min(1.0f, std::min(px - X + 0.5f, X + W - px + 0.5f)) * std::min(1.0f, std::min(py - Y + 0.5f, Y + H - py + 0.5f));
            if (dx > 0 && dy > 0) cov = std::min(cov, std::clamp(R - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f));
            if (cov <= 0) continue;
            uint32_t& d = c.px[(size_t)yy * c.w + xx];
            d = BlendOver(d, (uint32_t)((argb >> 24) * std::min(cov, 1.0f)) << 24 | (argb & 0xFFFFFF));
        }
}

// A convex polygon (design pixels), filled, its edges smoothed by 4x4 samples.
inline void FillConvex(Canvas& c, const Ui& ui, const std::vector<std::pair<float, float>>& pts, uint32_t argb)
{
    if (pts.size() < 3) return;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    std::vector<std::pair<float, float>> p;
    for (auto& q : pts)
    {
        p.push_back({ ui(q.first), ui(q.second) });
        minx = std::min(minx, p.back().first);
        maxx = std::max(maxx, p.back().first);
        miny = std::min(miny, p.back().second);
        maxy = std::max(maxy, p.back().second);
    }
    // Inside: on the same side of every edge (either winding).
    float area = 0;
    for (size_t i = 0; i < p.size(); i++)
    {
        const auto &a = p[i], &b = p[(i + 1) % p.size()];
        area += a.first * b.second - b.first * a.second;
    }
    const float sign = area < 0 ? -1.0f : 1.0f;
    auto inside = [&](float x, float y) {
        for (size_t i = 0; i < p.size(); i++)
        {
            const auto &a = p[i], &b = p[(i + 1) % p.size()];
            if (sign * ((b.first - a.first) * (y - a.second) - (b.second - a.second) * (x - a.first)) < 0) return false;
        }
        return true;
    };
    const int x0 = std::max(0, (int)minx), y0 = std::max(0, (int)miny), x1 = std::min(c.w, (int)std::ceil(maxx)), y1 = std::min(c.h, (int)std::ceil(maxy));
    if (x1 <= x0 || y1 <= y0) return;
    c.Mark(x0, y0, x1, y1);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
        {
            int n = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) n += inside(xx + (sx + 0.5f) / 4, yy + (sy + 0.5f) / 4);
            if (!n) continue;
            uint32_t& d = c.px[(size_t)yy * c.w + xx];
            d = BlendOver(d, (uint32_t)((argb >> 24) * n / 16) << 24 | (argb & 0xFFFFFF));
        }
}

// A line `t` design pixels thick.
inline void Line(Canvas& c, const Ui& ui, float ax, float ay, float bx, float by, float t, uint32_t argb)
{
    const float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0) return;
    const float nx = -dy / len * t / 2, ny = dx / len * t / 2;
    FillConvex(c, ui, { { ax + nx, ay + ny }, { bx + nx, by + ny }, { bx - nx, by - ny }, { ax - nx, ay - ny } }, argb);
}

// The types' colours (the game's order: Normal, Fighting, Flying, Poison, Ground, Rock,
// Bug, Ghost, Steel, ???, Fire, Water, Grass, Electric, Psychic, Ice, Dragon, Dark).
inline uint32_t TypeColour(int t)
{
    static const uint32_t c[18] = { 0xFFA8A878, 0xFFC03028, 0xFFA890F0, 0xFFA040A0, 0xFFD8B860, 0xFFB8A038, 0xFFA8B820, 0xFF705898,
        0xFFA0A0C0, 0xFF68A090, 0xFFF08030, 0xFF6890F0, 0xFF6CC04C, 0xFFE8C020, 0xFFF85888, 0xFF80C8C8, 0xFF7038F8, 0xFF705848 };
    return t >= 0 && t < 18 ? c[t] : 0xFF8A93A3;
}
inline std::string TypeName(int t) { return TitleCase(Party_Text(735, t)); }

// A pill with text in it; returns its width.
inline float Pill(Canvas& c, const Ui& ui, const RomFont& f, float x, float y, float h, const std::string& s, uint32_t bg,
    uint32_t ink = HudColour::White, float px = 32)
{
    const float w = HudTextWidth(ui, f, px, s) + h * 0.8f;
    FillRound(c, ui, x, y, w, h, h / 2, bg);
    HudText(c, ui, f, x + w / 2, y + (h - px) / 2, px, s, ink, 1);
    return w;
}

// A bar in a frame, filled to `k` (0-1).
inline void Bar(Canvas& c, const Ui& ui, Sprites& sp, float x, float y, float w, float k, const char* fill)
{
    HudSprite(c, ui, sp, "bars/bar_frame", x, y, w, 22);
    if (k > 0) HudSprite(c, ui, sp, fill, x + 6, y + 4, std::max(16.0f, (w - 12) * std::min(k, 1.0f)), 14);
}

// The HP bar's colour as the game picks it: green above half, yellow above a fifth, red.
inline const char* HpFill(int hp, int most) { return hp * 2 > most ? "bars/fill_green" : hp * 5 > most ? "bars/fill_yellow" : "bars/fill_red"; }

// The status shown on a Pokemon, as the game's abbreviation and colour; false for none.
inline bool StatusOf(const PartyMon& m, std::string& s, uint32_t& col)
{
    if (m.hp == 0) { s = "FNT"; col = 0xFF8A93A3; }
    else if (m.status & 7) { s = "SLP"; col = 0xFF8E6BE8; }
    else if (m.status & 0x88) { s = "PSN"; col = 0xFFA040A0; }
    else if (m.status & 0x10) { s = "BRN"; col = 0xFFF0505A; }
    else if (m.status & 0x20) { s = "FRZ"; col = 0xFF3E8EF0; }
    else if (m.status & 0x40) { s = "PAR"; col = 0xFFE8B400; }
    else return false;
    return true;
}

// The gender symbols, drawn rather than written, as not every font of the player's has
// them: a ring with an arrow (male, blue) or a cross (female, red), `s` design pixels
// square with its top left at (x, y).
inline void Gender(Canvas& c, const Ui& ui, float x, float y, int g, float s = 30)
{
    if (g > 1) return;
    const uint32_t col = g == 0 ? 0xFF3E8EF0 : 0xFFF0505A;
    const float r = s * 0.27f, t = s * 0.12f;
    const float cx = g == 0 ? x + r + t : x + s / 2, cy = g == 0 ? y + s - r - t : y + r + t;
    for (int k = 0; k < 24; k++)
    {
        const float a0 = k * 0.2618f, a1 = (k + 1) * 0.2618f;
        Line(c, ui, cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, t, col);
    }
    if (g == 0)
    {
        const float ex = x + s - t / 2, ey = y + t / 2;
        Line(c, ui, cx + r * 0.7f, cy - r * 0.7f, ex, ey, t, col);
        Line(c, ui, ex - s * 0.36f, ey, ex + t / 2, ey, t, col);
        Line(c, ui, ex, ey - t / 2, ex, ey + s * 0.36f, t, col);
    }
    else
    {
        Line(c, ui, cx, cy + r, cx, y + s + t, t, col);
        Line(c, ui, cx - s * 0.22f, y + s - s * 0.1f, cx + s * 0.22f, y + s - s * 0.1f, t, col);
    }
}

// Text with an arrow after it (the stat a nature raises or lowers): the arrow is drawn,
// as not every font has one. align as HudText.
inline void TextArrow(Canvas& c, const Ui& ui, const RomFont& f, float x, float y, const std::string& s, uint32_t col, int align, int dir)
{
    const float w = HudTextWidth(ui, f, 32, s) + (dir ? 28 : 0);
    const float left = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
    HudText(c, ui, f, left, y, 32, s, col);
    if (!dir) return;
    const float ax = left + w - 10, top = y + 6, bottom = y + 28;
    Line(c, ui, ax, dir > 0 ? top + 8 : top, ax, dir > 0 ? bottom : bottom - 8, 4, col);
    if (dir > 0) FillConvex(c, ui, { { ax - 8, top + 10 }, { ax, top }, { ax + 8, top + 10 } }, col);
    else FillConvex(c, ui, { { ax - 8, bottom - 10 }, { ax + 8, bottom - 10 }, { ax, bottom } }, col);
}

// The screens ----------------------------------------------------------------------------

// A Pokemon's party icon: its form's, or an egg's.
using MonIconFn = std::function<const Image*(const PartyMon&)>;

inline void DrawParty(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const PartyScreen& s, const MonIconFn& icon,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_lime", 0xFFFFFFFF);
    const float wd = ui.W / ui.s;
    HudSprite(c, ui, sp, "people/chip_ring_green", 64, 44, 88, 88);
    HudSprite(c, ui, sp, "icons/icon_party", 84, RingCentreY(44, 88) - 24, 48, 48);
    HudText(c, ui, f, 172, 64, 48, "POK\xC3\xA9MON", Ink, 0, true);

    // What the game asks, top right.
    const bool menuOpen = !s.items.empty();
    std::string prompt = s.step == 28 ? "Move to where?" : menuOpen && s.step == 15 ? "Do what with an item?"
        : menuOpen ? "Do what with " + s.mons[s.cursor].name + "?" : !s.prompt.empty() ? s.prompt : "Choose a Pok\xC3\xA9mon.";
    const float pw = HudTextWidth(ui, f, 32, prompt) + 80;
    HudPanel(c, ui, sp, "panels/button_lavender", wd - 60 - pw, 52, pw, 70);
    HudText(c, ui, f, wd - 60 - pw / 2, 71, 32, prompt, Ink, 1);

    // The cards, two columns of three.
    const float cw = 610, ch = 220;
    for (int i = 0; i < 6; i++)
    {
        const float x = 60 + (i % 2) * (cw + 20), y = 170 + (i / 2) * (ch + 20);
        if (i >= s.count)
        {
            HudSprite(c, ui, sp, "panels/slot_grid_empty", x, y, cw, ch);
            HudText(c, ui, f, x + cw / 2, y + ch / 2 - 16, 32, "Empty", Muted, 1);
            continue;
        }
        const PartyMon& m = s.mons[i];
        const bool sel = i == s.cursor && !(s.step == 28 && i == s.from);
        HudPanel(c, ui, sp, sel ? "panels/party_card_selected" : "panels/card", x, y, cw, ch);
        if (i == s.from) FillRound(c, ui, x + 4, y + 4, cw - 8, ch - 8, 28, 0x403E8EF0); // the one being moved
        HudSprite(c, ui, sp, sel ? "people/chip_ring_orange" : "people/chip_ring_green", x + 26, y + 35, 150, 150);
        HudPixelArtCentred(c, ui, icon(m), x + 26 + 75, RingCentreY(y + 35, 150), 99, 128); // inside the ring's disc
        float nx = x + 196;
        if (i == 0)
        {
            HudSprite(c, ui, sp, "people/crown_gold", nx, y + 40, 48, 44);
            nx += 56;
        }
        // (An egg has only its name: in the middle of the card's height.)
        HudText(c, ui, f, nx, m.egg ? y + ch / 2 - 24 : y + 36, 48, m.egg ? "Egg" : m.name, Ink);
        if (m.egg) continue;
        Gender(c, ui, x + cw - 60, y + 48, m.gender);
        // The level and held item; the status at the right.
        HudText(c, ui, f, x + 196, y + 108, 32, "Lv " + std::to_string(m.level), Ink);
        std::string st;
        uint32_t stc;
        float right = x + cw - 30;
        if (StatusOf(m, st, stc))
        {
            const float w = HudTextWidth(ui, f, 32, st) + 28;
            Pill(c, ui, f, right - w, y + 102, 44, st, stc);
            right -= w + 14;
        }
        if (m.item)
        {
            // Shortened to end before the status.
            std::string it = Party_Text(222, m.item);
            const float room = right - 12 - (x + 344);
            if (HudTextWidth(ui, f, 32, it) > room)
            {
                while (!it.empty() && HudTextWidth(ui, f, 32, it + "\xE2\x80\xA6") > room)
                {
                    while ((it.back() & 0xC0) == 0x80 && it.size() > 1) it.pop_back(); // a whole character
                    it.pop_back();
                }
                it += "\xE2\x80\xA6";
            }
            HudSprite(c, ui, sp, "icons/icon_bag", x + 300, y + 104, 36, 36);
            HudText(c, ui, f, x + 344, y + 108, 32, it, Muted);
        }
        // HP.
        HudText(c, ui, f, x + 196, y + 160, 32, "HP", Muted);
        Bar(c, ui, sp, x + 250, y + 165, cw - 250 - 170, (float)m.hp / std::max(1, m.stats[0]), HpFill(m.hp, m.stats[0]));
        HudText(c, ui, f, x + cw - 30, y + 160, 32, std::to_string(m.hp) + "/" + std::to_string(m.stats[0]),
            m.hp * 5 <= m.stats[0] ? Danger : Ink, 2);
    }

    // The context menu, on the right.
    const float mx = wd - 60 - 510, mw = 510;
    if (menuOpen)
    {
        // In two columns, as the game lays them out: the field moves on the left, the rest
        // on the right with Quit at the bottom.
        const float mh = 90 + 62 * (s.columns ? 4 : s.items.size());
        HudPanel(c, ui, sp, "panels/card", mx, 170, mw, mh);
        FillRound(c, ui, mx + 22, 190, mw - 44, 48, 16, 0xFF2EC4C6);
        HudText(c, ui, f, mx + 42, 198, 32, s.mons[s.cursor].name, White);
        const float colW = (mw - 44 - 12) / 2;
        for (size_t k = 0; k < s.items.size(); k++)
        {
            float bx = mx + 22, bw = mw - 44, ry = 254 + 62 * k;
            if (s.columns)
            {
                const bool left = k >= 4;
                bw = colW;
                bx = left ? mx + 22 : mx + 22 + colW + 12;
                ry = 254 + 62 * (left ? k - 4 : k);
            }
            const bool on = (int)k == s.sel;
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", bx, ry, bw, 50);
            if (on) HudSprite(c, ui, sp, "cursor/cursor", bx + 12, ry + 5, 36, 40);
            HudText(c, ui, f, bx + 62, ry + 9, 32, s.items[k], Ink);
        }
    }
    // The game's message, in the menu's place, with its yes/no.
    else if (!s.message.empty())
    {
        std::vector<std::string> lines = HudWrap(ui, f, 32, s.message, mw - 44 - 60);
        lines.resize(std::min(lines.size(), (size_t)6));
        const float th = 40 + 50.0f * lines.size(), mh = 44 + th + (s.yesNo >= 0 ? 2 * 62 + 10 : 0);
        HudPanel(c, ui, sp, "panels/card", mx, 170, mw, mh);
        HudPanel(c, ui, sp, "panels/dialogue_box", mx + 22, 192, mw - 44, th);
        for (size_t i = 0; i < lines.size(); i++) HudText(c, ui, f, mx + 52, 214 + 50.0f * i, 32, lines[i], Ink);
        for (int k = 0; s.yesNo >= 0 && k < 2; k++)
        {
            const float ry = 192 + th + 16 + 62 * k;
            const bool on = k == s.yesNo;
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", mx + 22, ry, mw - 44, 50);
            if (on) HudSprite(c, ui, sp, "cursor/cursor", mx + 34, ry + 5, 36, 40);
            HudText(c, ui, f, mx + 84, ry + 9, 32, k ? "No" : "Yes", Ink);
        }
    }
    // Cancel, under the cards at the right, where the game has it.
    if (!menuOpen)
    {
        const bool on = s.cursor >= 6;
        const float bw = 300, bx = 60 + 2 * cw + 20 - bw, by = 170 + 3 * (ch + 20) + 4;
        HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", bx, by, bw, 56);
        if (on) HudSprite(c, ui, sp, "cursor/cursor", bx + 14, by + 8, 36, 40);
        HudText(c, ui, f, bx + bw / 2, by + 12, 32, "Cancel", Ink, 1);
    }
    DrawHintBar(c, ui, sp, f, hints);
}

inline void DrawSummary(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const SummaryScreen& s, const MonIconFn& icon,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    const PartyMon& m = s.mon;
    DrawBackdrop(c, sp, "backgrounds/bg_lavender", 0xFFFFFFFF);

    // Left: the Pokemon.
    const float lx = 60, lw = 560;
    HudPanel(c, ui, sp, "panels/card", lx, 40, lw, m.egg ? 400 : 750);
    HudSprite(c, ui, sp, "people/chip_ring_green", lx + lw / 2 - 115, 66, 230, 230);
    if (m.egg)
    {
        // An egg has only its memo: what "The Egg Watch" says (text bank 302) by the cycles
        // of steps left, as the game's FormatEggWatch picks it.
        HudPixelArtCentred(c, ui, icon(m), lx + lw / 2, RingCentreY(66, 230), 152, 192);
        HudText(c, ui, f, lx + lw / 2, 316, 48, "Egg", Ink, 1, true);
        std::string watch = Party_Text(302, m.friendship <= 5 ? 105 : m.friendship <= 10 ? 106 : m.friendship <= 40 ? 107 : 108);
        std::string head = "The Egg Watch";
        const size_t end = watch.size() > 1 && watch[0] == '"' ? watch.find('"', 1) : std::string::npos;
        if (end != std::string::npos)
        {
            head = watch.substr(1, end - 1);
            watch = watch.substr(end + 1);
        }
        const float rx = 660, rw = (ui.W / ui.s) - 60 - rx;
        HudPanel(c, ui, sp, "panels/card", rx, 40, rw, 400);
        FillRound(c, ui, rx + 22, 62, rw - 44, 48, 16, 0xFF2EC4C6);
        HudText(c, ui, f, rx + 42, 70, 32, head, White);
        const std::vector<std::string> lines = HudWrap(ui, f, 48, watch, rw - 92);
        for (size_t i = 0; i < lines.size() && i < 4; i++) HudText(c, ui, f, rx + 46, 140 + 60 * i, 48, lines[i], Ink);
        if (s.count > 1)
            HudText(c, ui, f, rx + rw, 460, 32, std::to_string(s.slot + 1) + " / " + std::to_string(s.count), Muted, 2);
        DrawHintBar(c, ui, sp, f, hints);
        return;
    }
    HudPixelArtCentred(c, ui, icon(m), lx + lw / 2, RingCentreY(66, 230), 152, 192);
    const float nw = HudTextWidth(ui, f, 48, m.name, true);
    HudText(c, ui, f, lx + lw / 2 - 16, 316, 48, m.name, Ink, 1, true);
    Gender(c, ui, lx + lw / 2 - 16 + nw / 2 + 14, 326, m.gender);
    {
        // The level and types, centred.
        std::vector<std::pair<std::string, uint32_t>> pills = { { "Lv " + std::to_string(m.level), 0xFF2F3542 } };
        for (int t = 0; t < 2; t++)
            if (m.types[t] >= 0 && (t == 0 || m.types[1] != m.types[0])) pills.push_back({ TypeName(m.types[t]), TypeColour(m.types[t]) });
        float total = 0;
        for (auto& p : pills) total += HudTextWidth(ui, f, 32, p.first) + 36 + 12;
        float px = lx + lw / 2 - (total - 12) / 2;
        for (auto& p : pills) px += Pill(c, ui, f, px, 384, 46, p.first, p.second) + 12;
    }
    char num[32];
    std::vector<std::pair<std::string, std::string>> rows;
    snprintf(num, sizeof num, "%03d", s.national || !m.johto ? m.species : m.johto);
    rows.push_back({ "Dex No.", num });
    snprintf(num, sizeof num, "%05u", m.otId & 0xFFFF);
    const std::string id = num;
    rows.push_back({ "OT / ID", m.otName + " \xC2\xB7 " + id });
    rows.push_back({ "Nature", Party_Text(34, m.nature) });
    rows.push_back({ "Ability", Party_Text(720, m.ability) });
    rows.push_back({ "Item", m.item ? Party_Text(222, m.item) : "None" });
    const std::string place = Party_Text(279, m.metPlace);
    rows.push_back({ "Met", place.empty() ? "-" : m.metLevel ? place + ", Lv " + std::to_string(m.metLevel) : place + ", hatched" });
    for (size_t i = 0; i < rows.size(); i++)
    {
        const float ry = 448 + 56 * i;
        HudPanel(c, ui, sp, "panels/row", lx + 28, ry, lw - 56, 44);
        HudText(c, ui, f, lx + 46, ry + 6, 32, rows[i].first, Muted);
        HudText(c, ui, f, lx + lw - 46, ry + 6, 32, rows[i].second, Ink, 2);
    }

    // The pages' tabs.
    const float rx = 660, rw = (ui.W / ui.s) - 60 - rx;
    static const char* tabs[3] = { "Info", "Skills", "Ribbons" };
    float tx = rx;
    HudSprite(c, ui, sp, "buttons/button_dpad_left", tx, 46, 44, 44);
    tx += 60;
    for (int t = 0; t < 3; t++)
    {
        const float w = HudTextWidth(ui, f, 32, tabs[t]) + 64;
        HudPanel(c, ui, sp, t == s.page ? "panels/button_yellow" : "panels/button_white", tx, 42, w, 52);
        HudText(c, ui, f, tx + w / 2, 52, 32, tabs[t], Ink, 1);
        tx += w + 18;
    }
    HudSprite(c, ui, sp, "buttons/button_dpad_right", tx, 46, 44, 44);
    if (s.count > 1)
        HudText(c, ui, f, rx + rw, 52, 32, std::to_string(s.slot + 1) + " / " + std::to_string(s.count), Muted, 2);

    const float expK = m.expNext > m.expLevel ? (float)(m.exp - m.expLevel) / (m.expNext - m.expLevel) : 1.0f;
    const std::string toNext = m.expNext ? std::to_string(m.expNext - m.exp) + " to Lv " + std::to_string(m.level + 1) : "Top level";
    if (s.page == 0)
    {
        // The trainer memo and the experience, on a card ending where the left one does
        // (its content ends a little above), not at the screen's bottom with an empty half.
        HudPanel(c, ui, sp, "panels/card", rx, 130, rw, 660);
        FillRound(c, ui, rx + 22, 152, rw - 44, 48, 16, 0xFF2EC4C6);
        HudText(c, ui, f, rx + 42, 160, 32, "TRAINER MEMO", White);
        const std::string nature = Party_Text(34, m.nature);
        std::vector<std::string> memo = { nature + " nature." };
        if (!place.empty()) memo.push_back(m.metLevel ? "Met in " + place + " at Lv " + std::to_string(m.metLevel) + "." : "Hatched in " + place + ".");
        memo.push_back("Trainer: " + m.otName + ", ID No. " + id);
        for (size_t i = 0; i < memo.size(); i++)
            HudText(c, ui, f, rx + 46, 230 + 60 * i, 48, memo[i], Ink);
        const float ey = 230 + 60 * memo.size() + 40;
        FillRound(c, ui, rx + 22, ey, rw - 44, 48, 16, 0xFF2EC4C6);
        HudText(c, ui, f, rx + 42, ey + 8, 32, "EXPERIENCE", White);
        HudPanel(c, ui, sp, "panels/row", rx + 30, ey + 76, rw - 60, 44);
        HudText(c, ui, f, rx + 50, ey + 82, 32, "Exp. Points", Muted);
        HudText(c, ui, f, rx + rw - 50, ey + 82, 32, HudScreens::Thousands(m.exp), Ink, 2);
        HudPanel(c, ui, sp, "panels/row", rx + 30, ey + 136, rw - 60, 44);
        HudText(c, ui, f, rx + 50, ey + 142, 32, "To next level", Muted);
        HudText(c, ui, f, rx + rw - 50, ey + 142, 32, toNext, Ink, 2);
        Bar(c, ui, sp, rx + 30, ey + 204, rw - 60, expK, "bars/fill_blue");
    }
    else if (s.page == 1)
    {
        // The stats: a hexagon (HP at the top, then clockwise Attack, Defense, Speed, Sp. Def,
        // Sp. Atk), each stat against the highest, and the numbers beside it.
        HudPanel(c, ui, sp, "panels/card", rx, 130, rw, 500);
        const float hx = rx + 330, hy = 380, R = 170;
        HudSprite(c, ui, sp, "stats/hex_frame", hx - R, hy - R, 2 * R, 2 * R);
        static const int axis[6] = { 0, 1, 2, 3, 5, 4 }; // stats[] index of each corner
        static const char* names[6] = { "HP", "Attack", "Defense", "Speed", "Sp. Def", "Sp. Atk" };
        int top = 1;
        for (int i = 0; i < 6; i++) top = std::max(top, m.stats[i]);
        // The nature raises one stat (red) and lowers another (blue): index into stats[].
        const int up = 1 + m.nature / 5, down = 1 + m.nature % 5;
        const int upI = up == down ? -1 : up, downI = up == down ? -1 : down;
        std::vector<std::pair<float, float>> poly;
        for (int i = 0; i < 6; i++)
        {
            const float a = -1.5707963f + i * 1.0471976f;
            const float k = 0.12f + 0.88f * m.stats[axis[i]] / top;
            poly.push_back({ hx + std::cos(a) * R * 0.94f * k, hy + std::sin(a) * R * 0.94f * k });
            const float lxp = hx + std::cos(a) * (R + 26), lyp = hy + std::sin(a) * (R + 26) - 16;
            const int al = std::fabs(std::cos(a)) < 0.2f ? 1 : std::cos(a) > 0 ? 0 : 2;
            const uint32_t col = axis[i] == upI ? 0xFFE04848 : axis[i] == downI ? 0xFF3E7EE0 : Ink;
            TextArrow(c, ui, f, lxp, lyp + (i == 0 ? -12 : i == 3 ? 12 : 0), names[i], col, al,
                axis[i] == upI ? 1 : axis[i] == downI ? -1 : 0);
        }
        FillConvex(c, ui, poly, 0xB05BC85B);
        for (int i = 0; i < 6; i++) Line(c, ui, poly[i].first, poly[i].second, poly[(i + 1) % 6].first, poly[(i + 1) % 6].second, 4, 0xFF3CA03C);
        // The numbers, in the game's order.
        const float sx = rx + 680, sw = rw - 680 - 30;
        static const int order[6] = { 0, 1, 2, 4, 5, 3 };
        static const char* rowNames[6] = { "HP", "Attack", "Defense", "Sp. Atk", "Sp. Def", "Speed" };
        for (int r = 0; r < 6; r++)
        {
            const int i = order[r];
            const float ry = 160 + 54 * r;
            const bool u = i == upI, d = i == downI;
            HudPanel(c, ui, sp, "panels/row", sx, ry, sw, 44);
            if (u || d) FillRound(c, ui, sx + 2, ry + 2, sw - 4, 40, 18, u ? 0xFFFCE6E6 : 0xFFE6EEFC);
            TextArrow(c, ui, f, sx + 20, ry + 6, rowNames[r], u ? 0xFFE04848 : d ? 0xFF3E7EE0 : Ink, 0, u ? 1 : d ? -1 : 0);
            if (i == 0)
            {
                Bar(c, ui, sp, sx + 150, ry + 11, sw - 150 - 140, (float)m.hp / std::max(1, m.stats[0]), HpFill(m.hp, m.stats[0]));
                HudText(c, ui, f, sx + sw - 20, ry + 6, 32, std::to_string(m.hp) + "/" + std::to_string(m.stats[0]), Ink, 2);
            }
            else
                HudText(c, ui, f, sx + sw - 20, ry + 6, 32, std::to_string(m.stats[i]), Ink, 2);
        }
        HudText(c, ui, f, sx + 20, 160 + 54 * 6 + 8, 32, "Exp.", Muted);
        Bar(c, ui, sp, sx + 110, 160 + 54 * 6 + 13, sw - 110 - 230, expK, "bars/fill_blue");
        HudText(c, ui, f, sx + sw - 20, 160 + 54 * 6 + 8, 32, toNext, Muted, 2);

        // The moves: pills in their type's colour with their PP.
        HudPanel(c, ui, sp, "panels/card", rx, 660, rw, 300);
        FillRound(c, ui, rx + 22, 680, rw - 44, 48, 16, 0xFF2EC4C6);
        HudText(c, ui, f, rx + 42, 688, 32, "MOVES", White);
        const float mw = (rw - 44 - 20) / 2;
        for (int k = 0; k < 4; k++)
        {
            const float mx = rx + 22 + (k % 2) * (mw + 20), my = 748 + (k / 2) * 92;
            if (!m.moves[k])
            {
                FillRound(c, ui, mx, my, mw, 72, 36, 0xFFE9EDF2);
                HudText(c, ui, f, mx + 40, my + 20, 32, "-", Muted);
                continue;
            }
            FillRound(c, ui, mx, my, mw, 72, 36, TypeColour(m.moveType[k]));
            FillRound(c, ui, mx + 12, my + 12, 48, 48, 24, 0xFFFFFFFF);
            HudText(c, ui, f, mx + 36, my + 20, 32, TypeName(m.moveType[k]).substr(0, 1), TypeColour(m.moveType[k]), 1, true);
            HudText(c, ui, f, mx + 76, my + 20, 32, Party_Text(750, m.moves[k]), White);
            HudText(c, ui, f, mx + mw - 30, my + 20, 32, std::to_string(m.pp[k]) + "/" + std::to_string(m.ppMax[k]),
                m.pp[k] == 0 ? 0xFFFFD0D0 : White, 2);
        }
    }
    else
    {
        // Ribbons: how many, and a chip for each (up to a tray's worth), on a card as tall as
        // they need, at least as tall as the left one.
        const int perRow = (int)((rw - 92) / 104), shown = std::min(m.ribbons, perRow * 5);
        HudPanel(c, ui, sp, "panels/card", rx, 130, rw, std::max(660.0f, 320 + 104.0f * ((shown + perRow - 1) / perRow) + 30 - 130));
        FillRound(c, ui, rx + 22, 152, rw - 44, 48, 16, 0xFF2EC4C6);
        HudText(c, ui, f, rx + 42, 160, 32, "RIBBONS", White);
        HudText(c, ui, f, rx + 46, 236, 48,
            m.ribbons == 0 ? "No ribbons yet." : m.ribbons == 1 ? "1 ribbon" : std::to_string(m.ribbons) + " ribbons", Ink);
        for (int i = 0; i < std::min(m.ribbons, perRow * 5); i++)
        {
            const float bx = rx + 46 + (i % perRow) * 104, by = 320 + (i / perRow) * 104;
            HudSprite(c, ui, sp, "people/chip_ring_orange", bx, by, 88, 88);
            HudSprite(c, ui, sp, "icons/icon_star", bx + 20, RingCentreY(by, 88) - 24, 48, 48);
        }
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
