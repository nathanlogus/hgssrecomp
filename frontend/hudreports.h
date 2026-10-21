#pragma once
// Two of the game's documents redrawn in the HUD style: the Unown Report (the Key Item: its
// cover, the pages of Unown caught with their words, and the researchers' notes) and the
// certificates from Game Freak's office (the completed Pokedexes, and the Shiny Leaf
// crown). The game keeps the input; all is read from its memory each frame (the UnownReport
// application, asm/overlay_113.s; the Certificates one, src/certificates_app.c).
#include "hudparty.h"
#include "romart.h"

namespace HudReports
{
// The Unown Report's data (0x160 bytes): its heap +0 (0x98), its arguments +4, the page shown
// +0x1A of +0x1B (the cover, then the pages of Unown, 14 each, +0x1C of them, then the notes,
// +0x1D), the kinds of Unown seen +0x1F and which +0x20, the report's level +0x3C (how far the
// researchers' notes go) and the player's name +0x5C.
struct Unown
{
    int page = 0, pages = 0, unownPages = 0, notes = 0, level = 0;
    std::vector<int> kinds;
    std::string name;
};

inline bool ReadUnown(const HudParty::Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, Unown& u)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t d = ram.R32(m + 0x1C);
        return ram.Ok(d, 0x160) && ram.R32(d) == 0x98 && ram.R32(d + 4) == ram.R32(m + 0x18);
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2 || ram.R32(mgr + 0x14) > 3) return false;
    const uint32_t d = ram.R32(mgr + 0x1C);
    u = Unown();
    u.page = (int)ram.R8(d + 0x1A);
    u.pages = (int)ram.R8(d + 0x1B);
    u.unownPages = (int)ram.R8(d + 0x1C);
    u.notes = (int)ram.R8(d + 0x1D);
    u.level = (int)ram.R8(d + 0x3C);
    const int n = (int)ram.R8(d + 0x1F);
    if (n > 28 || u.notes > 6 || u.unownPages != (n + 13) / 14 || u.pages != u.unownPages + u.notes + 1 || u.page >= u.pages) return false;
    for (int i = 0; i < n; i++)
    {
        const int k = (int)ram.R8(d + 0x20 + i);
        if (k >= 28) return false;
        u.kinds.push_back(k);
    }
    return HudParty::ReadString(ram, ram.R32(d + 0x5C), u.name);
}

// The researchers' notes, a page each (ov113_021E6BD0): the top's and the bottom's first line
// of text bank 1, how many lines from it, and the report's level from which they are written.
struct Note
{
    int line, count, level;
};
constexpr Note kNotes[6][2] = { { { 6, 1, 1 }, { 7, 1, 1 } }, { { 8, 1, 2 }, { 9, 1, 3 } }, { { 10, 1, 4 }, { 11, 1, 4 } },
    { { 12, 1, 5 }, { 13, 1, 6 } }, { { 14, 3, 7 }, { 17, 1, 7 } }, { { 18, 1, 8 }, { 19, 1, 8 } } };

// An Unown's letter (its form: A to Z, then ! and ?).
inline std::string Letter(int k) { return k < 26 ? std::string(1, (char)('A' + k)) : k == 26 ? "!" : "?"; }

inline void DrawUnown(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, const Unown& u, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_dusk", 0xFFFFFFFF);
    const float x0 = ui.W / ui.s / 2 - 960;
    HudSprite(c, ui, sp, "people/chip_ring_grey", x0 + 60, 40, 96, 96);
    HudSprite(c, ui, sp, "icons/icon_book", x0 + 84, 64, 48, 48);
    HudText(c, ui, f, x0 + 176, 64, 48, "UNOWN REPORT", White, 0, true);
    const std::string pageText = "Page " + std::to_string(u.page + 1) + " / " + std::to_string(u.pages);
    const float pw = HudTextWidth(ui, f, 32, pageText) + 56;
    HudPanel(c, ui, sp, "panels/button_white", x0 + 1860 - pw, 62, pw, 56);
    HudText(c, ui, f, x0 + 1860 - pw / 2, 74, 32, pageText, Ink, 1);

    const float cx = x0 + 160, cy = 170, cw = 1600, ch = 800;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    if (u.page == 0)
    {
        // The cover: the report's title, whose it is, and the kinds found.
        HudSprite(c, ui, sp, "people/chip_ring_grey", cx + cw / 2 - 130, cy + 70, 260, 260);
        if (const Image* ic = art.PokemonIcon(201, u.kinds.empty() ? 0 : u.kinds[0]))
            Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(cx + cw / 2 - 100), ui(cy + 100), ui(200), ui(200));
        HudText(c, ui, f, cx + cw / 2, cy + 380, 64, "Unown Report", Ink, 1, true);
        HudText(c, ui, f, cx + cw / 2, cy + 470, 48, u.name, Ink, 1);
        HudScreens::StatRow(c, ui, sp, f, cx + cw / 2 - 260, cy + 580, 520, "Kinds of Unown", std::to_string(u.kinds.size()));
    }
    else if (u.page <= u.unownPages)
    {
        // Fourteen Unown a page, in two rows of seven, each with its word.
        const int first = (u.page - 1) * 14;
        for (int i = 0; i < 14 && first + i < (int)u.kinds.size(); i++)
        {
            const int k = u.kinds[first + i];
            const float tw = 200, th = 330, tx = cx + 50 + (i % 7) * (tw + 14), ty = cy + 50 + (i / 7) * (th + 40);
            HudPanel(c, ui, sp, "panels/row", tx, ty, tw, th);
            HudText(c, ui, f, tx + tw / 2, ty + 18, 48, Letter(k), Muted, 1, true);
            if (const Image* ic = art.PokemonIcon(201, k))
                Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(tx + tw / 2 - 80), ui(ty + 80), ui(160), ui(160));
            const std::string word = Party_Text(1, 20 + k);
            HudText(c, ui, f, tx + tw / 2, ty + th - 64, HudTextWidth(ui, f, 32, word) > tw - 16 ? 24 : 32, word, Ink, 1);
        }
    }
    else
    {
        // The researchers' notes: as much as the report's level has written.
        const int p = u.page - u.unownPages - 1;
        float y = cy + 60;
        for (int half = 0; half < 2 && p < 6; half++)
        {
            const Note& n = kNotes[p][half];
            if (n.level > u.level) continue;
            for (int l = 0; l < n.count; l++)
            {
                for (const std::string& line : HudWrap(ui, f, 48, Party_Text(1, n.line + l), cw - 160))
                {
                    HudText(c, ui, f, cx + 80, y, 48, line, Ink);
                    y += 64;
                }
                y += 24;
            }
            y += 40;
        }
    }
    DrawHintBar(c, ui, sp, f, hints);
}

// A certificate (CertificatesApp_Data): which (+4: the Johto Pokedex, the National Pokedex,
// the Shiny Leaf crown) and the party's first Pokemon able to fight (+0x10) for the crown.
struct Certificate
{
    int id = 0;
    PartyMon mon;
};

inline bool ReadCertificate(const HudParty::Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, Certificate& ct)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t d = ram.R32(m + 0x1C), args = ram.R32(m + 0x18);
        return ram.Ok(d, 0x5C) && ram.R32(d + 8) == ram.R32(args) && ram.R32(d + 4) == ram.R32(args + 4);
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2 || ram.R32(mgr + 0x14) > 5) return false;
    const uint32_t d = ram.R32(mgr + 0x1C);
    ct = Certificate();
    ct.id = (int)ram.R32(d + 4);
    if (ct.id > 2) return false;
    return ct.id != 2 || Party_ReadMon(ram.R32(d + 0x10), ct.mon);
}

inline void DrawCertificate(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, const Certificate& ct,
    const std::string& player, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_sunny", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, cw = 1300, cx = wd / 2 - cw / 2, cy = 120, ch = 820;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    HudPanel(c, ui, sp, "panels/frame_yellow", cx + 30, cy + 30, cw - 60, ch - 60);
    HudSprite(c, ui, sp, ct.id == 2 ? "people/crown_gold" : "people/chip_ring_red", cx + cw / 2 - 80, cy + 70, 160, 160);
    if (ct.id != 2) HudSprite(c, ui, sp, "icons/icon_dex", cx + cw / 2 - 44, cy + 106, 88, 88);
    HudText(c, ui, f, cx + cw / 2, cy + 260, 64, "CERTIFICATE", Ink, 1, true);
    // The game's words: who it is for, then what for (text bank 4).
    const std::string who = ct.id == 2 ? player + " and " + ct.mon.name : "Player: " + player;
    HudText(c, ui, f, cx + cw / 2, cy + 360, 48, who, Ink, 1);
    if (ct.id == 2)
        if (const Image* ic = art.PokemonIcon(ct.mon.species, ct.mon.form))
            Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(cx + cw - 260), ui(cy + 80), ui(180), ui(180));
    float y = cy + 450;
    for (const std::string& line : HudWrap(ui, f, 48, Party_Text(4, ct.id == 0 ? 1 : ct.id == 1 ? 2 : 5), cw - 240))
    {
        HudText(c, ui, f, cx + cw / 2, y, 48, line, Ink, 1);
        y += 62;
    }
    if (ct.id != 2) HudText(c, ui, f, cx + cw - 120, cy + ch - 120, 48, Party_Text(4, 3), Muted, 2, true);
    DrawHintBar(c, ui, sp, f, hints);
}
}
