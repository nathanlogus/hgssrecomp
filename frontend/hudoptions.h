#pragma once
// The game's Options menu (src/options_app.c) redrawn in the HUD style: its six settings as
// rows with their choices, and Confirm and Quit below. The game keeps taking the input;
// everything shown is read from its memory each frame, and anything that does not check out
// leaves the game's own picture on screen.
#include "hudbag.h"

namespace HudOptions
{
// What the Options menu shows: each setting's name, choices and the one chosen (Frame shows
// only the one chosen of its twenty), the row under the cursor (6: Confirm and Quit) and,
// on that row, which of them (1 Confirm, 0 Quit).
struct State
{
    std::string title, confirm, quit;
    std::string name[6];
    std::vector<std::string> choices[6];
    int value[6] = {};
    int row = 0, last = 0;
    int step = 0; // OptionsMenu_Main's step (+4): 1 fading in, 2 taking the input, 3-4 leaving
};

// OptionsApp_Data, the OverlayManager's data: the step (+4), the cursor's row (+0x10, bits
// 2-4) and the settings (OptionsApp_MenuEntry from +0x84, 0x54 bytes each: how many
// choices, the one chosen, their Strings). The names are lines 1-6 of bank 45, Confirm and
// Quit 7 and 8, the title 0.
template <class Read>
bool ReadState(Read rd, uint32_t init, uint32_t exec, uint32_t exit, uint32_t& cache, HudBag::Texts& tx, State& st)
{
    using HudBag::InRam;
    const uint32_t man = HudBag::FindManager(rd, init, exec, exit, cache);
    if (!man) return false;
    const uint32_t d = rd(man + 0x1C);
    st.step = (int)rd(d + 4);
    st.row = rd(d + 0x10) >> 2 & 7;
    if (st.step < 1 || st.step > 4 || st.row > 6) return false;
    auto rd16 = [&](uint32_t a) { return rd(a) & 0xFFFF; };
    for (int i = 0; i < 6; i++)
    {
        const uint32_t e = d + 0x84 + 0x54 * i;
        const int n = (int)rd16(e), v = (int)rd16(e + 2);
        static const int want[6] = { 3, 2, 2, 2, 2, 20 };
        if (n != want[i] || v >= n) return false;
        st.name[i] = HudScreens::TitleCase(tx.Get(45, 1 + i));
        st.value[i] = v;
        st.choices[i].clear();
        for (int j = 0; j < n; j++)
        {
            if (i == 5 && j != v) continue; // the frames: only the one chosen
            std::string s;
            if (!HudScreens::GameString(rd16, rd(e + 4 + 4 * j), s)) return false;
            st.choices[i].push_back(HudScreens::TitleCase(s));
        }
    }
    st.last = (int)rd16(d + 0x84 + 0x54 * 6 + 2);
    st.title = HudScreens::TitleCase(tx.Get(45, 0));
    st.confirm = HudScreens::TitleCase(tx.Get(45, 7));
    st.quit = HudScreens::TitleCase(tx.Get(45, 8));
    return true;
}

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const State& st, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, cw = 1300, cx = wd / 2 - cw / 2, cy = 70, ch = 900;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    HudOnline::TitledCard(c, ui, sp, f, cx + 24, cy + 24, cw - 48, 60, st.title);
    for (int i = 0; i < 6; i++)
    {
        const bool here = st.row == i;
        const float ry = cy + 110 + i * 110, rx = cx + 40, rw = cw - 80;
        HudPanel(c, ui, sp, here ? "panels/row_selected" : "panels/row", rx, ry, rw, 92);
        if (here) HudSprite(c, ui, sp, "cursor/cursor_small", rx + 10, ry + 30, 30, 32);
        HudText(c, ui, f, rx + 56, ry + 30, 32, st.name[i], Ink);
        // The choices as buttons from the right, the one chosen yellow; the frame, one of
        // twenty, between arrows.
        const int n = (int)st.choices[i].size();
        const float bw = 220, gap = 16, bx = rx + rw - 24 - n * bw - (n - 1) * gap - (i == 5 ? 60 : 0);
        for (int j = 0; j < n; j++)
        {
            const bool on = i == 5 || j == st.value[i];
            const float x = bx + j * (bw + gap);
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", x, ry + 16, bw, 60);
            HudText(c, ui, f, x + bw / 2, ry + 30, 32, st.choices[i][j], on ? Ink : Muted, 1);
        }
        if (i == 5)
        {
            HudSprite(c, ui, sp, "icons/icon_arrow_left", bx - 52, ry + 26, 40, 40);
            HudSprite(c, ui, sp, "icons/icon_arrow_right", bx + bw + 12, ry + 26, 40, 40);
        }
    }
    // Confirm and Quit; on their row the one under the cursor is yellow.
    const float by = cy + 110 + 6 * 110 + 20, bw = 300;
    const bool onRow = st.row == 6;
    const float x1 = cx + cw / 2 - bw - 12, x2 = cx + cw / 2 + 12;
    HudPanel(c, ui, sp, onRow && st.last == 1 ? "panels/button_yellow" : "panels/button_white", x1, by, bw, 64);
    HudPanel(c, ui, sp, onRow && st.last == 0 ? "panels/button_yellow" : "panels/button_white", x2, by, bw, 64);
    if (onRow) HudSprite(c, ui, sp, "cursor/cursor_small", (st.last == 1 ? x1 : x2) + 14, by + 16, 30, 32);
    HudText(c, ui, f, x1 + bw / 2, by + 16, 32, st.confirm, Ink, 1);
    HudText(c, ui, f, x2 + bw / 2, by + 16, 32, st.quit, Ink, 1);
    DrawHintBar(c, ui, sp, f, hints);
}
}
