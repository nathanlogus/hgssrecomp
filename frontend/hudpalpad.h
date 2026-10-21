#pragma once
// The Pal Pad redrawn in the HUD style: its menu and the player's own Friend Code page. The
// friend roster, registering a Friend Code and the game's messages stay the game's own
// picture (the roster's entries are not read). The game keeps taking the input.
#include "hudbag.h"

namespace HudPalPad
{
struct State
{
    int screen = 0; // 0 the menu, 3 the player's Friend Code
    int cursor = 0; // the menu's button
};

// PalPad (overlay 43, not decompiled), found by comparing memory while using it: its data
// (+0x1C of its OverlayManager) holds the screen shown (+0: 0 the menu, 1 the roster, 2
// registering, 3 the player's Friend Code), the step on that screen (+0x21C: 3 while the
// menu takes the input; others show messages or change screens) and the menu's cursor
// (+0x280, 0-3).
template <class Read>
bool ReadState(Read rd, uint32_t init, uint32_t exec, uint32_t exit, uint32_t& cache, State& st)
{
    const uint32_t man = HudBag::FindManager(rd, init, exec, exit, cache);
    if (!man) return false;
    const uint32_t d = rd(man + 0x1C);
    st.screen = (int)rd(d);
    st.cursor = rd(d + 0x280) & 0xFF;
    const int step = rd(d + 0x21C) & 0xFF;
    if (st.cursor > 3) return false;
    return (st.screen == 0 && step == 3) || st.screen == 3;
}

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, HudBag::Texts& tx, const std::string& me, const State& st,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    using HudScreens::TitleCase;
    DrawBackdrop(c, sp, "backgrounds/bg_sunny", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, cw = 1000, cx = wd / 2 - cw / 2;
    // Bank 779: the Pal Pad's words.
    std::string own = me + TitleCase(tx.Get(779, 3)); // "'S FRIEND CODE" after the name
    if (own.find("'S ") != std::string::npos) own.replace(own.find("'S "), 3, "'s ");
    if (st.screen == 0)
    {
        HudPanel(c, ui, sp, "panels/card", cx, 160, cw, 640);
        HudOnline::TitledCard(c, ui, sp, f, cx + 24, 184, cw - 48, 60, TitleCase(tx.Get(779, 0)));
        const std::string items[4] = { TitleCase(tx.Get(779, 1)), TitleCase(tx.Get(779, 2)), own, TitleCase(tx.Get(779, 4)) };
        for (int i = 0; i < 4; i++)
        {
            const bool on = i == st.cursor;
            const float y = 290 + i * 120;
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", cx + 80, y, cw - 160, 84);
            if (on) HudSprite(c, ui, sp, "cursor/cursor_small", cx + 100, y + 26, 30, 32);
            HudText(c, ui, f, cx + cw / 2, y + 26, 32, items[i], Ink, 1);
        }
    }
    else
    {
        // No Nintendo Wi-Fi Connection here: the game's words on how a Friend Code is had.
        HudPanel(c, ui, sp, "panels/card", cx, 220, cw, 420);
        HudOnline::TitledCard(c, ui, sp, f, cx + 24, 244, cw - 48, 60, own);
        HudPanel(c, ui, sp, "panels/code_cell", cx + cw / 2 - 260, 340, 520, 90);
        HudText(c, ui, f, cx + cw / 2, 360, 48, "\xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94", Muted, 1, true);
        const std::vector<std::string> lines = HudWrap(ui, f, 32, tx.Get(779, 54), cw - 120);
        for (size_t i = 0; i < lines.size() && i < 3; i++) HudText(c, ui, f, cx + cw / 2, 470 + 48.0f * i, 32, lines[i], Ink, 1);
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
