#pragma once
// The Pokédex page shown after a catch ("Pokédex registration completed.") redrawn as the
// Pokédex's card for a Pokémon (huddex.h). The battle draws that page on the top screen;
// it is told by its picture (the header bar, amber or red as it flashes, over the white
// grid), and the Pokémon is the wild one in battle. The page fades in and out: the grid's
// brightness says how far, and the card fades with it.
#include "huddex.h"
#include "hudbattleui.h"

namespace HudBattleDex
{
// Whether the top screen (`px`, 256 x 192) shows the page, and how bright it is (0-1).
inline bool Shown(const uint32_t* px, float& bright)
{
    if (!px) return false;
    auto rgb = [&](int x, int y, int& r, int& g, int& b) {
        const uint32_t c = px[y * 256 + x];
        r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    };
    int r1, g1, b1, r2, g2, b2, r3, g3, b3, r4, g4, b4;
    rgb(100, 4, r1, g1, b1);
    rgb(200, 4, r2, g2, b2);
    rgb(8, 100, r3, g3, b3);
    rgb(40, 40, r4, g4, b4);
    const bool header = r1 == r2 && g1 == g2 && b1 == b2 && r1 >= 16 && r1 > g1 && g1 >= b1 && r1 - b1 > r1 / 2;
    const bool grid = r3 == r4 && g3 == g4 && b3 == b4 && r3 == g3 && g3 == b3 && r3 >= 16 && r3 >= r1 / 2;
    if (!header || !grid) return false;
    bright = std::min(1.0f, r3 / 251.0f);
    return true;
}

// The card: the Pokémon's number (in the Johto Pokédex when it is in it), name, category,
// types, height, weight and entry.
inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, const uint8_t* rom, size_t romSize, int species, float bright,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    using namespace HudDex;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, cw = 1100, cx = wd / 2 - cw / 2;
    const bool heartGold = romSize > 0x10 && rom[0x0E] == 'K';
    std::vector<std::string> lines = HudWrap(ui, f, 40, Party_Text(heartGold ? kEntryHeartGold : kEntryHeartGold + 1, species), cw - 80 - 60);
    if (lines.size() > 6) lines.resize(6);
    const float ch = 400 + 50 + 60.0f * lines.size() + 40, cy = std::max(150.0f, (1080 - 56 - ch) / 2 + 40);
    const std::string title = "Pok\xC3\xA9" "dex registration completed";
    const float tw = HudTextWidth(ui, f, 32, title) + 80;
    HudPanel(c, ui, sp, "panels/button_yellow", wd / 2 - tw / 2, 48, tw, 64);
    HudText(c, ui, f, wd / 2, 64, 32, title, Ink, 1);
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    HudSprite(c, ui, sp, "people/chip_ring_green", cx + 40, cy + 40, 300, 300);
    if (const Image* ic = art.PokemonIcon(species)) Blit(c, *ic, 0, 0, (float)ic->w, (float)ic->h, ui(cx + 62), ui(cy + 62), ui(256), ui(256), false);
    const auto& johto = JohtoNumbers(rom, romSize);
    const int number = species < (int)johto.size() && johto[species] ? johto[species] : species;
    const float rx = cx + 380;
    HudText(c, ui, f, rx, cy + 60, 32, "No. " + Number(number), Muted);
    HudText(c, ui, f, rx, cy + 108, 48, Party_SpeciesName(species), Ink, 0, true);
    HudText(c, ui, f, rx, cy + 176, 32, Party_Text(kCategory, species), Ink);
    static std::vector<std::pair<uint32_t, uint32_t>> personal;
    if (personal.empty()) personal = NdsNarcFiles(rom, romSize, "a/0/0/2");
    float tx = rx;
    if (species < (int)personal.size() && personal[species].second >= 8)
    {
        const int t0 = rom[personal[species].first + 6], t1 = rom[personal[species].first + 7];
        for (int k = 0; k < 2; k++)
        {
            const int t = k ? t1 : t0;
            if (t < 18 && (k == 0 || t1 != t0)) tx += HudParty::Pill(c, ui, f, tx, cy + 232, 46, HudParty::TypeName(t), HudParty::TypeColour(t)) + 12;
        }
    }
    const float sw = (cw - rx + cx - 40 - 16) / 2;
    HudScreens::StatRow(c, ui, sp, f, rx, cy + 300, sw, "Height", Party_Text(kHeight, species));
    HudScreens::StatRow(c, ui, sp, f, rx + sw + 16, cy + 300, sw, "Weight", Party_Text(kWeight, species));
    const float dy = cy + 400;
    HudPanel(c, ui, sp, "panels/dialogue_box", cx + 40, dy, cw - 80, 50 + 60.0f * lines.size());
    for (size_t i = 0; i < lines.size(); i++) HudText(c, ui, f, cx + 70, dy + 26 + 60.0f * i, 40, lines[i], Ink);
    DrawHintBar(c, ui, sp, f, hints);
    // The game's fade.
    if (bright < 1) FillBlend(c, 0, 0, (float)c.w, (float)c.h, (uint32_t)((1 - bright) * 255) << 24);
}
}
