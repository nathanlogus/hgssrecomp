#pragma once
// The Pokedex redrawn in the HUD style (online-HUD design 22-ss-pokedex): its front page
// (which Pokedex, Seen and Caught) and its list (the game's page of 15, five by three, with
// the cursor) beside a card for the Pokemon under the cursor. The game keeps taking the
// input; everything is read from its memory each frame (src/application/pokedex and
// asm/overlay_18.s in the decompilation). The details (area, size, forms), the search and
// anything that does not check out leave the game's own picture on screen.
#include "hudparty.h"
#include "romart.h"

namespace HudDex
{
// What the Pokedex application shows.
struct View
{
    bool list = false;          // the list, or else the front page
    bool results = false;       // the list of a search's results (its first place: the search)
    int found = 0;              // how many it found
    bool national = false;      // the National Pokedex is shown (else Johto's)
    bool canSwitch = false;     // the player has the National Pokedex: the front page switches
    int page = 0, pages = 1;    // the list's page (15 places) and how many there are
    int pos = 0;                // the cursor's place on the page (0-14, rows of five)
    int seen = 0, caught = 0;   // in the Pokedex shown
    struct Place
    {
        int number = 0;         // its number in the Pokedex shown
        int species = 0;        // 0: not seen
        bool caught = false;
    } places[15];
};

// The Johto Pokedex's numbers by species (a/1/3/8, member 0; 0: not in it).
inline const std::vector<uint16_t>& JohtoNumbers(const uint8_t* rom, size_t size)
{
    static std::vector<uint16_t> t;
    if (t.empty())
    {
        const auto files = NdsNarcFiles(rom, size, "a/1/3/8");
        if (!files.empty() && files[0].second >= 2 * 494)
            for (int s = 0; s < 494; s++) t.push_back((uint16_t)RomGet16(rom + files[0].first + 2 * s));
    }
    return t;
}

// The Pokedex's steps (PokedexApp_RunMainSeq): the front page (opening, waiting, the dial,
// back from the list), the list (sliding in, choosing, turning a page, touching it), and
// steps that only wait for a fade or a button's flash, which keep what was drawn the frame
// before (0 none, 1 the front page, 2 the list, 4 a search's results): returns the same.
inline int ViewOf(int seq, int before)
{
    switch (seq)
    {
    case 57: case 58: case 59: case 60: case 61: case 64: case 65:
        return 4;
    case 62: // back to the search: the list is rebuilt at once, so the results drawn last stay (main.cpp)
        return before == 4 ? 4 : 0;
    case 63: // back to the whole list, rebuilt at once
        return 2;
    case 5: case 6: case 7: case 8: case 12:
        return 1;
    case 9: case 11: case 13: case 14: case 15: case 16: case 17: case 19:
        return 2;
    case 1: case 2: case 3: case 4: case 10: case 18: case 20: case 92:
        return before;
    default:
        return 0;
    }
}

// PokedexAppData (include/application/pokedex/pokedex_internal.h, 0x1910 bytes): args +0,
// which Pokedex +0x1858 (0 Johto, 1 National), page +0x1859, place +0x185A, whether the
// National Pokedex is had +0x1860; the seen species in order (species, 1 seen or 2 caught)
// +0x878 with their count at +0x102C, and the same laid out by number (+0x1030, four bytes
// each, zero where not seen). PokedexArgs: the save's Pokedex +0 (magic 0xBEEFCAFE, the
// caught flags +4, the seen ones +0x44).
inline bool Read(const HudParty::Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, const std::vector<uint16_t>& johto,
    int before, View& v)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t d = ram.R32(m + 0x1C), args = ram.R32(m + 0x18);
        return ram.Ok(d, 0x1910) && ram.R32(d) == args && ram.R32(ram.R32(args)) == 0xBEEFCAFE;
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2 || johto.size() != 494) return false;
    // Coming back from a Pokemon's details to a search's results (65) fades in through step 3.
    const int seq = (int)ram.R32(mgr + 0x14);
    const int show = ViewOf(seq == 3 && ram.R32(ram.R32(mgr + 0x1C) + 0x85C) == 65 ? 65 : seq, before);
    if (!show) return false;
    const uint32_t d = ram.R32(mgr + 0x1C), dex = ram.R32(ram.R32(mgr + 0x18));
    const int mode = (int)ram.R8(d + 0x1858), count = (int)ram.R16(d + 0x102C);
    const uint32_t nat = ram.R32(d + 0x1860);
    v.list = show == 2 || show == 4;
    v.results = show == 4;
    v.found = count;
    v.national = mode == 1;
    v.canSwitch = nat == 1;
    v.page = (int)ram.R8(d + 0x1859);
    v.pos = (int)ram.R8(d + 0x185A);
    if (mode > 1 || nat > 1 || (v.national && !v.canSwitch) || count > 493 || v.pos > 14 || !ram.Ok(dex, 0x340)) return false;
    auto flag = [&](uint32_t at, int species) { return (ram.R8(dex + at + ((species - 1) >> 3)) >> ((species - 1) & 7) & 1) != 0; };
    auto number = [&](int species) { return v.national ? species : (int)johto[species]; };

    // Seen and Caught as the front page counts them: the Pokedex shown, from the save.
    v.seen = v.caught = 0;
    for (int s = 1; s <= 493; s++)
        if (number(s))
        {
            v.seen += flag(0x44, s);
            v.caught += flag(4, s);
        }

    // The list as the game laid it out, cross-checked with the save: every species in its
    // place by number, caught exactly when the save says so.
    int last = 0;
    for (int i = 0; i < count; i++)
    {
        const int s = (int)ram.R16(d + 0x878 + 4 * i), st = (int)ram.R16(d + 0x87A + 4 * i);
        if (s < 1 || s > 493 || !number(s) || st < 1 || st > 2 || !flag(0x44, s) || (st == 2) != flag(4, s)) return false;
        last = number(s);
    }
    // A search's results are laid out in order after the search's own place (ov18_021F8884).
    v.pages = v.results ? count / 15 + 1 : last ? (last - 1) / 15 + 1 : 1;
    if (v.page >= v.pages) return false;
    for (int k = 0; k < 15; k++)
    {
        const int idx = v.page * 15 + k;
        View::Place& p = v.places[k];
        p = View::Place();
        p.number = idx + 1;
        if (idx >= 518) return false;
        const int s = (int)ram.R16(d + 0x1030 + 4 * idx), st = (int)ram.R16(d + 0x1032 + 4 * idx);
        if (v.results)
        {
            p.number = 0;
            if (idx == 0 || idx > count)
            {
                if (s || st) return false;
                continue;
            }
            if (s != (int)ram.R16(d + 0x878 + 4 * (idx - 1))) return false;
            p.number = number(s);
        }
        if (!s && !st) continue;
        if (s < 1 || s > 493 || number(s) != (v.results ? p.number : idx + 1) || st < 1 || st > 2 || (st == 2) != flag(4, s)) return false;
        p.species = s;
        p.caught = st == 2;
    }
    return true;
}

// Text banks: categories ("Leaf Pokemon"), heights and weights as the game writes them, and the
// Pokedex entries (HeartGold's bank, SoulSilver's the next).
constexpr int kCategory = 816, kHeight = 814, kWeight = 812, kEntryHeartGold = 803;

inline std::string Number(int n)
{
    char b[8];
    snprintf(b, sizeof b, "%03d", n);
    return b;
}

// The header: the Pokedex's disc and name, Seen and Caught, which Pokedex.
inline void Header(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, const View& v, float x0)
{
    using namespace HudColour;
    HudSprite(c, ui, sp, "people/chip_ring_red", x0 + 60, 40, 96, 96);
    HudSprite(c, ui, sp, "icons/icon_dex", x0 + 84, 64, 48, 48);
    HudText(c, ui, f, x0 + 176, 64, 48, "POK\xC3\xA9" "DEX", Ink, 0, true);
    float px = x0 + 176 + HudTextWidth(ui, f, 48, "POK\xC3\xA9" "DEX", true) + 36;
    for (int k = 0; k < 2; k++)
    {
        const std::string s = (k ? "Caught " : "Seen ") + std::to_string(k ? v.caught : v.seen);
        const float w = HudTextWidth(ui, f, 32, s) + 100;
        HudPanel(c, ui, sp, "panels/button_white", px, 62, w, 56);
        if (k) HudPixelArt(c, ui, art.ItemIcon(4), px + 14, 66, 48, 48);
        else HudSprite(c, ui, sp, "icons/icon_eye", px + 20, 72, 36, 36);
        HudText(c, ui, f, px + 72, 74, 32, s, Ink);
        px += w + 20;
    }
    const std::string mode = v.national ? "National" : "Johto";
    const float mw = HudTextWidth(ui, f, 32, mode) + 56;
    HudPanel(c, ui, sp, "panels/button_white", x0 + 1860 - mw, 62, mw, 56);
    HudText(c, ui, f, x0 + 1860 - mw / 2, 74, 32, mode, Ink, 1);
}

// The front page: which Pokedex (both, when the player can switch), Seen and Caught.
inline void DrawFront(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, const View& v, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float x0 = ui.W / ui.s / 2 - 960;
    Header(c, ui, sp, f, art, v, x0);
    const float cw = 900, cx = x0 + 960 - cw / 2, cy = 220, ch = 600;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    HudSprite(c, ui, sp, "people/chip_ring_red", cx + cw / 2 - 90, cy + 40, 180, 180);
    HudSprite(c, ui, sp, "icons/icon_dex", cx + cw / 2 - 48, cy + 82, 96, 96);
    HudText(c, ui, f, cx + cw / 2, cy + 250, 48, v.national ? "National Pok\xC3\xA9" "dex" : "Johto Pok\xC3\xA9" "dex", Ink, 1, true);
    if (v.canSwitch)
    {
        // The two, as the game's dial turns between them (left: National, right: Johto).
        const char* names[2] = { "National", "Johto" };
        for (int k = 0; k < 2; k++)
        {
            const bool on = (k == 0) == v.national;
            const float bw = 240, bx = cx + cw / 2 - bw - 10 + k * (bw + 20);
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", bx, cy + 330, bw, 56);
            HudText(c, ui, f, bx + bw / 2, cy + 342, 32, names[k], Ink, 1);
        }
    }
    const float rw = 360, ry = cy + 430;
    HudScreens::StatRow(c, ui, sp, f, cx + cw / 2 - rw - 10, ry, rw, "Seen", std::to_string(v.seen));
    HudScreens::StatRow(c, ui, sp, f, cx + cw / 2 + 10, ry, rw, "Caught", std::to_string(v.caught));
    DrawHintBar(c, ui, sp, f, hints);
}

// The list and the card of the Pokemon under the cursor.
inline void DrawList(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, const uint8_t* rom, size_t romSize,
    const View& v, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float x0 = ui.W / ui.s / 2 - 960;
    Header(c, ui, sp, f, art, v, x0);

    // The page: five places by three, as the game lays them out (its d-pad moves across them).
    const float lx = x0 + 60, ly = 160, lw = 1000, lh = 820;
    HudPanel(c, ui, sp, "panels/card", lx, ly, lw, lh);
    const float tw = 176, th = 220, gap = 14;
    for (int k = 0; k < 15; k++)
    {
        const View::Place& p = v.places[k];
        const bool here = k == v.pos;
        const float tx = lx + 30 + (k % 5) * (tw + gap), ty = ly + 30 + (k / 5) * (th + gap);
        HudPanel(c, ui, sp, here ? "panels/row_selected" : p.species ? "panels/row" : "panels/row_disabled", tx, ty, tw, th);
        if (here) HudSprite(c, ui, sp, "cursor/cursor_small", tx + 6, ty + 92, 30, 32);
        if (v.results && !p.number)
        {
            // The search's own place (back to it), and the places after the last result.
            if (v.page == 0 && k == 0)
            {
                HudSprite(c, ui, sp, "icons/icon_search", tx + tw / 2 - 40, ty + 50, 80, 80);
                HudText(c, ui, f, tx + tw / 2, ty + th - 50, 32, "Search", Ink, 1);
            }
            continue;
        }
        const float nx = tx + tw / 2 + (p.caught ? 18 : 0);
        if (p.caught) HudPixelArt(c, ui, art.ItemIcon(4), nx - 72, ty + 10, 40, 40);
        HudText(c, ui, f, nx, ty + 16, 32, Number(p.number), p.species ? Ink : Muted, 1);
        if (!p.species)
        {
            HudText(c, ui, f, tx + tw / 2, ty + 110, 32, "---", Muted, 1);
            continue;
        }
        if (const Image* ic = art.PokemonIcon(p.species))
            Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(tx + tw / 2 - 56), ui(ty + 52), ui(112), ui(112), false,
                p.caught ? 0xFFFFFFFF : 0x80FFFFFF);
        std::string name = Party_SpeciesName(p.species);
        float px = 32;
        if (HudTextWidth(ui, f, px, name) > tw - 16) px = 24;
        HudText(c, ui, f, tx + tw / 2, ty + th - 50, px, name, p.caught ? Ink : Muted, 1);
    }
    // The pages, under them.
    const float py = ly + 30 + 3 * (th + gap) + 8;
    HudSprite(c, ui, sp, "buttons/button_l", lx + 30, py + 10, 52, 38);
    HudText(c, ui, f, lx + lw / 2, py + 12, 32,
        (v.results ? std::to_string(v.found) + " found \xC2\xB7 Page " : "Page ") + std::to_string(v.page + 1) + " / " + std::to_string(v.pages), Ink, 1);
    HudSprite(c, ui, sp, "buttons/button_r", lx + lw - 82, py + 10, 52, 38);

    // The card: the Pokemon under the cursor, as much as the game tells of it (all when
    // caught; seen only: its name; not seen: its number).
    const View::Place& p = v.places[v.pos];
    const float cx = x0 + 1100, cy = 160, cw = 760, ch = 820;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    HudSprite(c, ui, sp, "people/chip_ring_green", cx + 30, cy + 30, 250, 250);
    if (p.species)
        if (const Image* ic = art.PokemonIcon(p.species))
            Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(cx + 59), ui(cy + 59), ui(192), ui(192), false, p.caught ? 0xFFFFFFFF : 0xB0FFFFFF);
    const float rx = cx + 310;
    HudText(c, ui, f, rx, cy + 50, 32, "No. " + Number(p.number), Muted);
    HudText(c, ui, f, rx, cy + 96, 48, p.species ? Party_SpeciesName(p.species) : "-----", Ink, 0, true);
    if (p.species)
        HudText(c, ui, f, rx, cy + 162, 32, p.caught ? Party_Text(kCategory, p.species) : "????? Pok\xC3\xA9mon", Ink);
    if (p.caught)
    {
        // Types: from the species' data (a/0/0/2, +6 and +7), as the summary does.
        float tx = rx;
        int types[2] = { -1, -1 };
        static std::vector<std::pair<uint32_t, uint32_t>> personal;
        if (personal.empty()) personal = NdsNarcFiles(rom, romSize, "a/0/0/2");
        if (p.species < (int)personal.size() && personal[p.species].second >= 8)
        {
            types[0] = rom[personal[p.species].first + 6];
            types[1] = rom[personal[p.species].first + 7];
        }
        for (int t = 0; t < 2; t++)
            if (types[t] >= 0 && types[t] < 18 && (t == 0 || types[1] != types[0]))
                tx += HudParty::Pill(c, ui, f, tx, cy + 214, 46, HudParty::TypeName(types[t]), HudParty::TypeColour(types[t])) + 12;
        HudScreens::StatRow(c, ui, sp, f, cx + 30, cy + 300, (cw - 76) / 2, "Height", Party_Text(kHeight, p.species));
        HudScreens::StatRow(c, ui, sp, f, cx + 30 + (cw - 76) / 2 + 16, cy + 300, (cw - 76) / 2, "Weight", Party_Text(kWeight, p.species));
        // The entry, in the dialogue box.
        // HeartGold's game codes are IPK*, SoulSilver's IPG*.
        const bool heartGold = romSize > 0x10 && rom[0x0E] == 'K';
        std::vector<std::string> lines = HudWrap(ui, f, 32, Party_Text(heartGold ? kEntryHeartGold : kEntryHeartGold + 1, p.species), cw - 60 - 60);
        if (lines.size() > 6) lines.resize(6);
        const float dy = cy + 390;
        HudPanel(c, ui, sp, "panels/dialogue_box", cx + 30, dy, cw - 60, 40 + 50.0f * lines.size());
        for (size_t i = 0; i < lines.size(); i++) HudText(c, ui, f, cx + 60, dy + 22 + 50.0f * i, 32, lines[i], Ink);
    }
    else if (p.species)
    {
        HudScreens::StatRow(c, ui, sp, f, cx + 30, cy + 300, (cw - 76) / 2, "Height", "???");
        HudScreens::StatRow(c, ui, sp, f, cx + 30 + (cw - 76) / 2 + 16, cy + 300, (cw - 76) / 2, "Weight", "???");
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
