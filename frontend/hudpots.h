#pragma once
// The Berry Pots (src/berry_pots_app.c) redrawn in the HUD style: the four pots with what
// grows in them, how wet the soil is and its mulch, the game's line about the pot and its
// menu. The game keeps taking the input; everything shown is read from its memory each
// frame, and anything that does not check out leaves the game's own picture on screen.
#include "hudparty.h"
#include "hudgear.h"

namespace HudPots
{
struct Pot
{
    int berry = 0;  // BerryIdToItemId: the item, 0 none
    int stage = 0;  // BERRY_POT_GROWTH_STAGE_*: 0 none, 1 planted, 2 sprouted, 3 growing, 4 in bloom, 5 berries
    int water = 0;  // 0-100
    int mulch = 0;  // the mulch's item, 0 none
    int yield = 0;  // berries to pick
};

struct State
{
    Pot pots[4];
    int cursor = 0;
    int step = 0;   // BerryPotsApp_Run's step: 0 fading in, 1 out, 2 choosing a pot, 3-8 its menus and messages
    std::string line;
    HudGear::Menu menu;
};

// The Berry Pots item runs BerryPots (overlay 16), whose data (+0x1C of its OverlayManager)
// holds the pots' own application (+8) while it runs (BerryPots step 1). That one's data
// (BerryPotsAppData) points back at it (+4) and holds the pots as shown (+0x20, 0x14 bytes
// each: berry, stage, -, water, mulch, yield), the cursor (+0x7C), the line in its window
// (a String, +0xA4) and its menu (+0x1A8, steps 3-6).
template <class Read>
bool ReadState(Read rd, uint32_t init, uint32_t exec, uint32_t exit, uint32_t& cache, State& st)
{
    using HudBag::InRam;
    const uint32_t outerMan = HudBag::FindManager(rd, init, exec, exit, cache);
    if (!outerMan || rd(outerMan + 0x14) != 1) return false;
    const uint32_t outer = rd(outerMan + 0x1C), man = rd(outer + 8);
    if (!InRam(man) || rd(man + 0x18) != outer || rd(man + 0x10) != 2) return false;
    const uint32_t d = rd(man + 0x1C);
    if (!InRam(d) || rd(d + 4) != outer) return false;
    st.step = (int)rd(man + 0x14);
    st.cursor = rd(d + 0x7C) & 0xFF;
    if (st.step < 0 || st.step > 8 || st.cursor > 3) return false;
    for (int i = 0; i < 4; i++)
    {
        const uint32_t w = rd(d + 0x20 + 0x14 * i), w2 = rd(d + 0x24 + 0x14 * i);
        Pot& p = st.pots[i];
        const int berry = w & 0xFF;
        p.stage = w >> 8 & 0xFF;
        p.water = w >> 24 & 0xFF;
        const int mulch = w2 & 0xFF;
        p.yield = w2 >> 8 & 0xFF;
        if (p.stage > 5 || berry > 64 || mulch > 4 || p.water > 100) return false;
        p.berry = berry ? berry + 148 : 0;
        p.mulch = mulch ? mulch + 94 : 0;
    }
    if (!HudScreens::GameString([&](uint32_t a) { return rd(a) & 0xFFFF; }, rd(d + 0xA4), st.line)) st.line.clear();
    if (st.step >= 3 && st.step <= 6) HudGear::ReadMenu(rd, rd(d + 0x1A8), st.menu);
    return true;
}

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, HudBag::Texts& tx, const State& st,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_lime", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 900;
    HudText(c, ui, f, x0 + 20, 50, 48, "BERRY POTS", Ink, 0, true);
    static const char* stages[6] = { "Empty", "Planted", "Sprouted", "Growing", "In bloom", "Berries!" };
    // The four pots, the one under the cursor in yellow.
    for (int i = 0; i < 4; i++)
    {
        const Pot& p = st.pots[i];
        const bool on = i == st.cursor;
        const float w = 420, h = 560, x = x0 + i * (w + 40), y = 130;
        HudPanel(c, ui, sp, on ? "panels/frame_yellow" : "panels/card", x, y, w, h);
        if (on) HudSprite(c, ui, sp, "cursor/cursor_small", x + 16, y + 20, 30, 32);
        HudText(c, ui, f, x + w / 2, y + 24, 32, "Pot " + std::to_string(i + 1), Muted, 1);
        const float d = 200, cx = x + w / 2;
        HudSprite(c, ui, sp, p.stage == 5 ? "people/chip_ring_orange" : p.stage ? "people/chip_ring_grey" : "people/chip_empty", cx - d / 2, y + 70, d, d);
        if (p.berry) HudPixelArtCentred(c, ui, art.ItemIcon(p.berry), cx, RingCentreY(y + 70, d), 100);
        HudText(c, ui, f, cx, y + 290, 32, p.berry ? tx.Get(HudBag::kItemName, p.berry) : "No Berry", p.berry ? Ink : Muted, 1);
        HudText(c, ui, f, cx, y + 332, 32, p.stage == 5 ? std::to_string(p.yield) + (p.yield == 1 ? " Berry to pick" : " Berries to pick") : stages[p.stage],
            p.stage == 5 ? Danger : Muted, 1);
        // How wet the soil is (it dries as the Berry grows), and its mulch.
        HudText(c, ui, f, x + 30, y + 390, 32, "Soil", Muted);
        HudParty::Bar(c, ui, sp, x + 110, y + 396, w - 140, p.water / 100.0f, p.water > 50 ? "bars/fill_blue" : p.water > 20 ? "bars/fill_yellow" : "bars/fill_red");
        HudText(c, ui, f, x + 30, y + 450, 32, "Mulch", Muted);
        HudText(c, ui, f, x + w - 30, y + 450, 32, p.mulch ? tx.Get(HudBag::kItemName, p.mulch) : "None", Ink, 2);
    }
    // The game's line about the pot, and its menu beside it.
    const float by = 740, bw = st.menu.items.empty() ? 1800 : 1340;
    HudPanel(c, ui, sp, "panels/dialogue_box", x0, by, bw, 150);
    const std::vector<std::string> lines = HudWrap(ui, f, 48, st.line, bw - 80);
    for (size_t i = 0; i < lines.size() && i < 2; i++) HudText(c, ui, f, x0 + 40, by + 20 + 60.0f * i, 48, lines[i], Ink);
    for (size_t i = 0; i < st.menu.items.size(); i++)
    {
        const bool on = (int)i == st.menu.cursor;
        const float mx = x0 + 1380, my = by - 80 * ((int)st.menu.items.size() - 1) + 80.0f * i + 40;
        HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", mx, my, 420, 64);
        if (on) HudSprite(c, ui, sp, "cursor/cursor_small", mx + 14, my + 16, 30, 32);
        HudText(c, ui, f, mx + 210, my + 16, 32, st.menu.items[i], Ink, 1);
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
