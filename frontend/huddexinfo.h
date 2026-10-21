#pragma once
// The Pokedex's details redrawn in the HUD style, beside the list (huddex.h): the pages the
// game turns between with the d-pad (Area, Size, Forms and the one back to the list), with
// the Pokemon's card on the left. The game keeps the input; all is read from its memory
// each frame (PokedexApp_MainSeq_66 to _91 in src/application/pokedex/ov18_021E8BF4.c).
// Comparing two forms side by side is the game's own picture.
#include "huddex.h"

namespace HudDexInfo
{
enum { Area = 1, Size = 2, Forms = 3, Back = 4, Compare = 5 };

struct View
{
    int page = 0;
    int species = 0, number = 0;
    bool caught = false;
    // Area: the time of day (0 morning, 1 day, 2 night), Johto (0) or Kanto (1), the place
    // chosen (0 all), the places as the game lists them (map headers, after "Show all"),
    // and the map's cells: every place of the region, and each listed place's own.
    int time = 0, region = 0, cursor = 0;
    std::vector<int> places;
    struct Cell
    {
        int x, y, place; // place: its index in `places` (1 on), or 0 for one not listed
    };
    std::vector<Cell> cells;
    // Size: heights in decimetres, weights in hectograms (the Pokemon's; the player's as
    // the game shows them).
    int height = 0, weight = 0;
    // Forms: as the game lists them (1 male, 2 female, 3 neither, 0x80 + a form), and the one
    // chosen.
    std::vector<int> forms;
    int form = 0;
    // Comparing two forms: the right one (the left is `form`), and the side being changed.
    int other = 0, side = 0;
};

// The maps of the area page's places (sOverworldMapIDs and sDungeonMapIDs, overlay 18).
constexpr uint16_t kOverworldMaps[70] = { 65535, 60, 33, 67, 34, 35, 73, 36, 37, 38, 39, 40, 41, 78, 42, 43, 77, 94, 95, 75, 44, 45,
    88, 46, 89, 47, 48, 151, 90, 20, 91, 92, 49, 50, 52, 54, 55, 56, 57, 152, 30, 31, 32, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21,
    22, 23, 422, 25, 26, 93, 27, 28, 29, 414, 51, 74, 174, 24, 65535 };
constexpr uint16_t kDungeonMaps[26] = { 65535, 155, 113, 99, 177, 117, 96, 7, 332, 121, 119, 120, 253, 176, 449, 146, 462, 279, 342,
    240, 108, 124, 126, 106, 147, 145 };

// The page shown at a step of PokedexApp_RunMainSeq, 0 for none; -1 for steps that only fade
// or flash a button (the page shown the frame before stays). A page's fade in (68, 76, 79,
// 89) shows it once its data is loaded (its +0x868 is 1). The step that waits for a fade (3)
// counts as the one it goes on to (+0x85C).
inline int PageOf(int seq, int sub)
{
    switch (seq)
    {
    case 69: case 71: case 72: case 73: case 74: return Area;
    case 75: return sub >= 3 ? Area : -1; // the time of day changing: its places are loaded in step 2
    case 77: return Size;
    case 80: case 82: case 83: case 84: case 85: return Forms;
    case 90: return Back;
    case 87: return Compare;
    case 86: case 88: return -1;
    case 68: return sub == 1 ? Area : -1;
    case 76: return sub == 1 ? Size : -1;
    case 79: return sub == 1 ? Forms : -1;
    case 89: return sub == 1 ? Back : -1;
    case 4: case 67: case 70: case 78: case 81: case 91: return -1;
    default: return 0;
    }
}

// Reads the details: 1 when `v` is read, -1 when the page shown before should stay, 0 for the
// game's own picture. PokedexAppData (pokedex_internal.h): the species +0x18A2, the area's
// time +0x18C9, region +0x18C8 and place +0x18CA, its lists +0x18DC (the places, -2 first for
// "Show all", +0x18FC with their count +0x1900 and flags +0x1904: bit 31 for a dungeon), the
// places' cells (gOverworlds +0x190C, 36 bytes each: x, y, w, h and w*h cells; gDungeons
// +0x1908, x and y), heights +0x1848 and weights +0x184C by species, the forms seen +0x18A4
// (count +0x18C4) and the one chosen +0x18C5.
inline int Read(const HudParty::Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, const std::vector<uint16_t>& johto,
    View& v)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t d = ram.R32(m + 0x1C), args = ram.R32(m + 0x18);
        return ram.Ok(d, 0x1910) && ram.R32(d) == args && ram.R32(ram.R32(args)) == 0xBEEFCAFE;
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2 || johto.size() != 494) return 0;
    const uint32_t d = ram.R32(mgr + 0x1C), dex = ram.R32(ram.R32(mgr + 0x18));
    int seq = (int)ram.R32(mgr + 0x14);
    if (seq == 3) seq = (int)ram.R32(d + 0x85C);
    v = View();
    v.page = PageOf(seq, (int)ram.R16(d + 0x868));
    if (v.page <= 0) return v.page;
    v.species = (int)ram.R16(d + 0x18A2);
    if (v.species < 1 || v.species > 493 || !ram.Ok(dex, 0x340) || ram.R8(d + 0x1858) > 1) return 0;
    auto flag = [&](uint32_t at, int s) { return (ram.R8(dex + at + ((s - 1) >> 3)) >> ((s - 1) & 7) & 1) != 0; };
    if (!flag(0x44, v.species)) return 0;
    v.caught = flag(4, v.species);
    v.number = ram.R8(d + 0x1858) ? v.species : johto[v.species];
    if (v.page == Size && !v.caught) return 0; // the game only shows it for those caught

    const uint32_t hs = ram.R32(d + 0x1848), ws = ram.R32(d + 0x184C);
    if (!ram.Ok(hs + 4 * v.species) || !ram.Ok(ws + 4 * v.species)) return 0;
    v.height = (int)ram.R32(hs + 4 * v.species);
    v.weight = (int)ram.R32(ws + 4 * v.species);

    const int nForms = (int8_t)ram.R8(d + 0x18C4);
    if (nForms < 1 || nForms > 32) return 0;
    for (int i = 0; i < nForms; i++)
    {
        const int f = (int)ram.R8(d + 0x18A4 + i);
        if (f & 0x80 ? (f & 0x7F) >= 28 : f < 1 || f > 3) return 0;
        v.forms.push_back(f);
    }
    v.form = (int)ram.R8(d + 0x18C5);
    if ((v.page == Forms || v.page == Compare) && v.form >= nForms) return 0;
    v.other = (int)ram.R8(d + 0x18C6);
    v.side = (int)ram.R8(d + 0x18C7) & 0x1F;
    if (v.page == Compare && (v.other >= nForms || v.side > 1)) return 0;

    if (v.page == Area)
    {
        v.time = (int)ram.R8(d + 0x18C9);
        v.region = (int)ram.R8(d + 0x18C8);
        v.cursor = (int)ram.R8(d + 0x18CA);
        const uint32_t list = ram.R32(d + 0x18FC), flags = ram.R32(d + 0x1904), ow = ram.R32(d + 0x190C), dg = ram.R32(d + 0x1908);
        const int n = (int)ram.R32(d + 0x1900);
        if (v.time > 2 || v.region > 1 || n < 1 || n > 200 || v.cursor >= n || !ram.Ok(list, 4 * n) || !ram.Ok(flags, 4 * n) ||
            !ram.Ok(ow, 36 * 70) || !ram.Ok(dg, 4 * 26) || ram.R32(list) != 0xFFFFFFFEu)
            return 0;
        // A place's cells: an overworld's laid out by its mask, a dungeon's one.
        auto cellsOf = [&](bool dungeon, int idx, int place) {
            if (dungeon)
            {
                v.cells.push_back({ (int)ram.R8(dg + 4 * idx), (int)ram.R8(dg + 4 * idx + 1), place });
                return;
            }
            const uint32_t e = ow + 36 * idx;
            const int x = (int)ram.R8(e), y = (int)ram.R8(e + 1), w = (int)ram.R8(e + 2), h = (int)ram.R8(e + 3);
            for (int k = 0; k < w * h && k < 32; k++)
                if (ram.R8(e + 4 + k)) v.cells.push_back({ x + k % w, y + k / w, place });
        };
        // The region's places, as the game picks them (x < 22: Johto).
        auto inRegion = [&](int x) { return (x < 22) == (v.region == 0); };
        for (int i = 1; i < 70; i++)
            if (inRegion((int)ram.R8(ow + 36 * i))) cellsOf(false, i, 0);
        for (int i = 1; i < 26; i++)
            if (inRegion((int)ram.R8(dg + 4 * i))) cellsOf(true, i, 0);
        for (int i = 1; i < n; i++)
        {
            const int idx = (int)ram.R32(list + 4 * i);
            const bool dungeon = (ram.R32(flags + 4 * i) & 0x80000000u) != 0;
            if (idx < 1 || idx >= (dungeon ? 26 : 70)) return 0;
            const int map = dungeon ? kDungeonMaps[idx] : kOverworldMaps[idx];
            if (map >= 540) return 0;
            v.places.push_back(map);
            cellsOf(dungeon, idx, i);
        }
    }
    return 1;
}

// Text the game pads with spaces, without them.
inline std::string Trim(std::string s)
{
    for (;;)
        if (!s.empty() && s[0] == ' ') s.erase(0, 1);
        else if (s.compare(0, 3, "\xE2\x80\x89") == 0) s.erase(0, 3);
        else return s;
}

// A form's name as the forms page writes it (ov18_021F09D8): Male or Female, the form's own
// name for the species that have forms, else the species' name.
inline std::string FormName(int species, int code)
{
    const int form = code & 0x7F;
    int at = -1;
    switch (species)
    {
    case 172: at = form == 0 ? 114 : form == 1 ? 115 : 166; break; // Pichu
    case 201: at = 121; break; // Unown
    case 351: at = 160 + form; break; // Castform
    case 386: at = 145 + form; break; // Deoxys
    case 412: case 413: at = 118 + form; break; // Burmy, Wormadam
    case 421: at = 164 + form; break; // Cherrim
    case 422: case 423: at = 116 + form; break; // Shellos, Gastrodon
    case 479: at = 153 + form; break; // Rotom
    case 487: at = 151 + form; break; // Giratina
    case 492: at = 149 + form; break; // Shaymin
    default: at = code == 1 ? 114 : code == 2 ? 115 : -1;
    }
    return at < 0 ? Party_SpeciesName(species) : HudScreens::TitleCase(Party_Text(802, at));
}

// The tabs: the four pages, the one shown marked.
inline void Tabs(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, int page)
{
    const char* names[4] = { "Area", "Size", "Forms", "Back" };
    for (int k = 0; k < 4; k++)
    {
        const float w = 200, bx = x + k * (w + 16);
        HudPanel(c, ui, sp, k + 1 == page ? "panels/button_yellow" : "panels/button_white", bx, y, w, 56);
        HudText(c, ui, f, bx + w / 2, y + 12, 32, names[k], HudColour::Ink, 1);
    }
}

// The details: the Pokemon's card on the left, the page on the right.
inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, const uint8_t* rom, size_t romSize, const View& v,
    const Image* trainer, int gender, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float x0 = ui.W / ui.s / 2 - 960;
    HudSprite(c, ui, sp, "people/chip_ring_red", x0 + 60, 40, 96, 96);
    HudSprite(c, ui, sp, "icons/icon_dex", x0 + 84, 64, 48, 48);
    HudText(c, ui, f, x0 + 176, 64, 48, "POK\xC3\xA9" "DEX", Ink, 0, true);
    Tabs(c, ui, sp, f, x0 + 1860 - 4 * 216 + 16, 62, v.page == Compare ? Forms : v.page);

    // The Pokemon: as much as the game tells of it.
    const float lx = x0 + 60, ly = 160, lw = 480, lh = 820;
    HudPanel(c, ui, sp, "panels/card", lx, ly, lw, lh);
    HudSprite(c, ui, sp, "people/chip_ring_green", lx + lw / 2 - 150, ly + 40, 300, 300);
    const int iconForm = (v.page == Forms || v.page == Compare) && (v.forms[v.form] & 0x80) ? v.forms[v.form] & 0x7F : 0;
    if (const Image* ic = art.PokemonIcon(v.species, iconForm))
        Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(lx + lw / 2 - 116), ui(ly + 74), ui(232), ui(232), false,
            v.caught ? 0xFFFFFFFF : 0xB0FFFFFF);
    HudText(c, ui, f, lx + lw / 2, ly + 370, 32, "No. " + HudDex::Number(v.number), Muted, 1);
    HudText(c, ui, f, lx + lw / 2, ly + 414, 48, Party_SpeciesName(v.species), Ink, 1, true);
    HudText(c, ui, f, lx + lw / 2, ly + 480, 32, v.caught ? Party_Text(HudDex::kCategory, v.species) : "????? Pok\xC3\xA9mon", Ink, 1);
    if (v.caught)
    {
        int types[2] = { -1, -1 };
        static std::vector<std::pair<uint32_t, uint32_t>> personal;
        if (personal.empty()) personal = NdsNarcFiles(rom, romSize, "a/0/0/2");
        if (v.species < (int)personal.size() && personal[v.species].second >= 8)
            types[0] = rom[personal[v.species].first + 6], types[1] = rom[personal[v.species].first + 7];
        float tw = 0;
        const bool two = types[1] != types[0];
        for (int t = 0; t < (two ? 2 : 1); t++)
            if (types[t] >= 0 && types[t] < 18) tw += HudTextWidth(ui, f, 32, HudParty::TypeName(types[t])) + 46 * 0.8f + (t ? 12 : 0);
        float tx = lx + (lw - tw) / 2;
        for (int t = 0; t < (two ? 2 : 1); t++)
            if (types[t] >= 0 && types[t] < 18)
                tx += HudParty::Pill(c, ui, f, tx, ly + 540, 46, HudParty::TypeName(types[t]), HudParty::TypeColour(types[t])) + 12;
        HudScreens::StatRow(c, ui, sp, f, lx + 30, ly + 620, lw - 60, "Height", Trim(Party_Text(HudDex::kHeight, v.species)));
        HudScreens::StatRow(c, ui, sp, f, lx + 30, ly + 690, lw - 60, "Weight", Trim(Party_Text(HudDex::kWeight, v.species)));
    }

    const float px = x0 + 580, py = 160, pw = 1280, ph = 820;
    if (v.page == Area)
    {
        // The map: the region's places, those the Pokemon lives in at this time of day marked
        // (only the chosen one, when one is), beside the list of them.
        const float mw = 820;
        HudOnline::TitledCard(c, ui, sp, f, px, py, mw, ph, v.region ? "KANTO" : "JOHTO",
            v.cursor ? Party_PlaceName(v.places[v.cursor - 1]) : v.places.empty() ? "" : "All places");
        const char* times[3] = { "Morning", "Day", "Night" };
        for (int k = 0; k < 3; k++)
        {
            const float bw = 220, bx = px + mw / 2 - 1.5f * bw - 16 + k * (bw + 16);
            HudPanel(c, ui, sp, k == v.time ? "panels/button_yellow" : "panels/button_white", bx, py + 90, bw, 56);
            HudText(c, ui, f, bx + bw / 2, py + 102, 32, times[k], Ink, 1);
        }
        // The region's picture: the Pokégear's map, all of it known, whose cells these are
        // without its 2 rows on top (as the fly points'). It fills the card below the times
        // of day to the card's inner edges (2 in, its corners 22 round).
        const Image* map = art.RegionMap(63, {});
        const float ax = map ? px + 2 : px + 30, ay = py + 170, aw = map ? mw - 4 : mw - 60, ah = map ? ph - 172 : ph - 200;
        if (!map) FillBlend(c, ui(ax), ui(ay), ui(aw), ui(ah), 0xFFE6F5DA);
        int x1 = 99, y1 = 99, x2 = 0, y2 = 0;
        for (const View::Cell& k : v.cells)
            x1 = std::min(x1, k.x), y1 = std::min(y1, k.y), x2 = std::max(x2, k.x + 1), y2 = std::max(y2, k.y + 1);
        if (map || (x2 > x1 && y2 > y1))
        {
            float cell = std::min((aw - 60) / (x2 - x1), (ah - 60) / (y2 - y1));
            float ox = ax + (aw - cell * (x2 - x1)) / 2 - x1 * cell, oy = ay + (ah - cell * (y2 - y1)) / 2 - y1 * cell;
            if (map)
            {
                // Zoomed to fill the panel, centred on the places marked (as far as the map
                // goes), gliding to the next ones.
                float fx1 = 99, fy1 = 99, fx2 = -1, fy2 = -1;
                for (const View::Cell& k : v.cells)
                    if (k.place && (v.cursor == 0 || v.cursor == k.place))
                        fx1 = std::min(fx1, (float)k.x), fy1 = std::min(fy1, (float)k.y), fx2 = std::max(fx2, (float)k.x), fy2 = std::max(fy2, (float)k.y);
                if (fx2 < 0) fx1 = fx2 = v.region ? 34 : 12, fy1 = fy2 = 8;
                static float pan[2] = { -1, -1 };
                HudRegionMap(c, ui, *map, v.region ? 22 : 0, v.region ? 47 : 26, ax, ay, aw, ah, (fx1 + fx2) / 2, (fy1 + fy2) / 2 + 2, 99, 20, pan,
                    cell, ox, oy);
                oy += 2 * cell;
            }
            for (int pass = map ? 1 : 0; pass < 2; pass++)
                for (const View::Cell& k : v.cells)
                {
                    const bool on = k.place && (v.cursor == 0 || v.cursor == k.place);
                    if (on != (pass == 1)) continue;
                    // Only those in the panel.
                    if (map && (ox + k.x * cell < ax || ox + (k.x + 1) * cell > ax + aw || oy + k.y * cell < ay || oy + (k.y + 1) * cell > ay + ah))
                        continue;
                    const float g = cell * 0.08f;
                    HudParty::FillRound(c, ui, ox + k.x * cell + g, oy + k.y * cell + g, cell - 2 * g, cell - 2 * g, cell * 0.2f,
                        on ? (map ? 0xC8F08030 : 0xFFF08030) : 0xFFF2DCA0);
                }
        }
        if (v.places.empty())
        {
            HudPanel(c, ui, sp, "panels/button_white", ax + aw / 2 - 200, ay + ah / 2 - 40, 400, 80);
            HudText(c, ui, f, ax + aw / 2, ay + ah / 2 - 24, 48, "Area unknown", Ink, 1, true);
        }

        // The list, as the game's, scrolled to keep the chosen place in view.
        const float rx = px + mw + 30, rw = pw - mw - 30;
        HudOnline::TitledCard(c, ui, sp, f, rx, py, rw, ph, "PLACES", std::to_string(v.places.size()));
        const int n = (int)v.places.size() + 1, rows = 10;
        const int first = std::max(0, std::min(v.cursor - rows / 2, n - rows));
        for (int k = 0; k < rows && first + k < n; k++)
        {
            const int i = first + k;
            const float ry = py + 90 + k * 70;
            HudPanel(c, ui, sp, i == v.cursor ? "panels/row_selected" : "panels/row", rx + 24, ry, rw - 48, 56);
            if (i == v.cursor) HudSprite(c, ui, sp, "cursor/cursor_small", rx + 30, ry + 12, 30, 32);
            HudText(c, ui, f, rx + 70, ry + 12, 32, i ? Party_PlaceName(v.places[i - 1]) : "Show all", Ink);
        }
        if (first > 0) HudSprite(c, ui, sp, "icons/icon_arrow_up", rx + rw - 64, py + 76, 32, 32);
        if (first + rows < n) HudSprite(c, ui, sp, "icons/icon_arrow_down", rx + rw - 64, py + ph - 44, 32, 32);
    }
    else if (v.page == Size)
    {
        // Height: the player and the Pokemon side by side, to scale (the player as tall as the
        // game says: 4'11", 15 decimetres); weight: the two as bars.
        const float cw = (pw - 30) / 2;
        HudOnline::TitledCard(c, ui, sp, f, px, py, cw, ph, "HEIGHT");
        const float base = py + ph - 170, room = ph - 300;
        const float most = std::max(15, v.height), k = room / most;
        const float you = 15 * k, mon = std::max(24.0f, v.height * k);
        if (trainer) HudPixelArt(c, ui, trainer, px + cw * 0.3f - you / 2, base - you, you, you);
        if (const Image* ic = art.PokemonIcon(v.species))
            Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(px + cw * 0.7f - mon / 2), ui(base - mon * 1.1f), ui(mon * 1.2f), ui(mon * 1.2f));
        FillBlend(c, ui(px + 40), ui(base), ui(cw - 80), ui(4), 0xFFC9D3E0);
        HudText(c, ui, f, px + cw * 0.3f, base + 24, 32, "You", Muted, 1);
        HudText(c, ui, f, px + cw * 0.3f, base + 66, 32, Trim(Party_Text(802, 138 + (gender ? 1 : 0))), Ink, 1);
        HudText(c, ui, f, px + cw * 0.7f, base + 24, 32, Party_SpeciesName(v.species), Muted, 1);
        HudText(c, ui, f, px + cw * 0.7f, base + 66, 32, Trim(Party_Text(HudDex::kHeight, v.species)), Ink, 1);

        const float wx = px + cw + 30;
        HudOnline::TitledCard(c, ui, sp, f, wx, py, cw, ph, "WEIGHT");
        const int yours = gender ? 410 : 400; // 90.4 and 88.2 lbs., in hectograms
        const float heaviest = (float)std::max(yours, v.weight);
        const struct { std::string name, value; int w; const char* fill; } rows[2] = {
            { "You", Trim(Party_Text(802, 140 + (gender ? 1 : 0))), yours, "bars/fill_blue" },
            { Party_SpeciesName(v.species), Trim(Party_Text(HudDex::kWeight, v.species)), v.weight, "bars/fill_yellow" } };
        for (int r = 0; r < 2; r++)
        {
            const float ry = py + 140 + r * 200;
            HudText(c, ui, f, wx + 40, ry, 32, rows[r].name, Muted);
            HudText(c, ui, f, wx + cw - 40, ry, 32, rows[r].value, Ink, 2);
            HudParty::Bar(c, ui, sp, wx + 40, ry + 60, cw - 80, rows[r].w / heaviest, rows[r].fill);
        }
        const std::string than = v.weight > yours ? "heavier than you" : v.weight < yours ? "lighter than you" : "as heavy as you";
        HudText(c, ui, f, wx + cw / 2, py + 560, 32, Party_SpeciesName(v.species) + " is " + than + ".", Ink, 1);
    }
    else if (v.page == Forms)
    {
        // The forms seen, the chosen one large.
        const float cw = 620;
        HudOnline::TitledCard(c, ui, sp, f, px, py, cw, ph, "FORM", FormName(v.species, v.forms[v.form]));
        HudSprite(c, ui, sp, "people/chip_ring_green", px + cw / 2 - 220, py + 160, 440, 440);
        if (const Image* ic = art.PokemonIcon(v.species, iconForm))
            Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(px + cw / 2 - 170), ui(py + 210), ui(340), ui(340));
        HudText(c, ui, f, px + cw / 2, py + 660, 48, FormName(v.species, v.forms[v.form]), Ink, 1, true);
        const float rx = px + cw + 30, rw = pw - cw - 30;
        const int n = (int)v.forms.size(), rows = 8;
        HudOnline::TitledCard(c, ui, sp, f, rx, py, rw, ph, "FORMS SEEN", std::to_string(n));
        const int first = std::max(0, std::min(v.form - rows / 2, n - rows));
        for (int k = 0; k < rows && first + k < n; k++)
        {
            const int i = first + k;
            const float ry = py + 90 + k * 86;
            HudPanel(c, ui, sp, i == v.form ? "panels/row_selected" : "panels/row", rx + 24, ry, rw - 48, 72);
            if (i == v.form) HudSprite(c, ui, sp, "cursor/cursor_small", rx + 30, ry + 20, 30, 32);
            if (const Image* ic = art.PokemonIcon(v.species, v.forms[i] & 0x80 ? v.forms[i] & 0x7F : 0))
                Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(rx + 66), ui(ry + 4), ui(64), ui(64));
            HudText(c, ui, f, rx + 146, ry + 20, 32, FormName(v.species, v.forms[i]), Ink);
        }
        if (first > 0) HudSprite(c, ui, sp, "icons/icon_arrow_up", rx + rw - 64, py + 76, 32, 32);
        if (first + rows < n) HudSprite(c, ui, sp, "icons/icon_arrow_down", rx + rw - 64, py + ph - 44, 32, 32);
    }
    else if (v.page == Compare)
    {
        // Two forms side by side; with three or more, the side being changed is marked.
        const float cw = (pw - 30) / 2;
        for (int k = 0; k < 2; k++)
        {
            const int i = k ? v.other : v.form;
            const float cx = px + k * (cw + 30);
            HudOnline::TitledCard(c, ui, sp, f, cx, py, cw, ph, k ? "RIGHT" : "LEFT", FormName(v.species, v.forms[i]));
            if (v.forms.size() >= 3 && k == v.side) HudPanel(c, ui, sp, "panels/frame_yellow", cx + 20, py + 90, cw - 40, ph - 110);
            HudSprite(c, ui, sp, "people/chip_ring_green", cx + cw / 2 - 220, py + 160, 440, 440);
            if (const Image* ic = art.PokemonIcon(v.species, v.forms[i] & 0x80 ? v.forms[i] & 0x7F : 0))
                Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(cx + cw / 2 - 170), ui(py + 210), ui(340), ui(340));
            HudText(c, ui, f, cx + cw / 2, py + 660, 48, FormName(v.species, v.forms[i]), Ink, 1, true);
        }
    }
    else
    {
        HudPanel(c, ui, sp, "panels/card", px, py, pw, ph);
        HudSprite(c, ui, sp, "icons/icon_exit", px + pw / 2 - 60, py + 220, 120, 120);
        const float bw = 480, bx = px + (pw - bw) / 2;
        HudPanel(c, ui, sp, "panels/button_yellow", bx, py + 400, bw, 72);
        HudText(c, ui, f, bx + bw / 2, py + 418, 32, "Back to the list", Ink, 1);
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
