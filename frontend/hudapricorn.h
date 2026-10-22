#pragma once
// The Apricorn Box redrawn in the HUD style: the seven Apricorns as the box holds them, how
// many of each, and the one under the cursor with the game's words about it. The game
// keeps taking the input; everything shown is read from its memory each frame, and anything
// that does not check out leaves the game's own picture on screen.
#include "hudbag.h"

namespace HudApricorn
{
struct State
{
    int count[7] = {}; // by Apricorn (Red, Yellow, Blue, Green, Pink, White, Black)
    int col = 0, row = 0;
};

// The Apricorn of the box's place: the left column holds Red, Blue, Pink and Black, the
// right one (half a row lower) Yellow, Green and White.
inline int ApricornAt(int col, int row) { return col ? 1 + 2 * row : 2 * row; }

// The item ApricornBox (overlay 58, not decompiled) runs the box itself in overlay 59 as a
// child (its data +8) at its step 2, with the box's arguments (+4, the first byte 0: only
// looking in the box). The child's data points back at it (+4) and holds the Apricorns
// (SaveApricornBox, +0x10: a count each), the cursor's column (+0x49) and row (+0x4A); it
// takes the input at its step 3. Found by comparing memory while moving the cursor.
template <class Read>
bool ReadState(Read rd, uint32_t init, uint32_t exec, uint32_t exit, uint32_t& cache, State& st)
{
    using HudBag::InRam;
    const uint32_t outerMan = HudBag::FindManager(rd, init, exec, exit, cache);
    if (!outerMan || rd(outerMan + 0x14) != 2) return false;
    const uint32_t outer = rd(outerMan + 0x1C), man = rd(outer + 8), args = rd(outer + 4);
    if (!InRam(man) || !InRam(args) || (rd(args) & 0xFF) != 0 || rd(man + 0x18) != outer || rd(man + 0x10) != 2 || rd(man + 0x14) != 3)
        return false;
    const uint32_t d = rd(man + 0x1C), box = rd(d + 0x10);
    if (!InRam(d) || rd(d + 4) != outer || !InRam(box)) return false;
    const uint32_t cursor = rd(d + 0x48);
    st.col = cursor >> 8 & 0xFF;
    st.row = cursor >> 16 & 0xFF;
    if (st.col > 1 || st.row > (st.col ? 2 : 3)) return false;
    for (int i = 0; i < 7; i++)
    {
        st.count[i] = rd(box + i) & 0xFF;
        if (st.count[i] > 99) return false;
    }
    return true;
}

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, HudBag::Texts& tx, const State& st,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_coral", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 900;
    HudText(c, ui, f, x0 + 20, 50, 48, "APRICORN BOX", Ink, 0, true);
    // The box: two columns as the game lays them out, the right one half a row lower.
    const float bx = x0, by = 130, bw = 900, bh = 860;
    HudPanel(c, ui, sp, "panels/card", bx, by, bw, bh);
    for (int col = 0; col < 2; col++)
        for (int row = 0; row < (col ? 3 : 4); row++)
        {
            const int a = ApricornAt(col, row), n = st.count[a];
            const bool on = col == st.col && row == st.row;
            const float w = 380, h = 170, x = bx + 50 + col * (w + 40), y = by + 40 + row * 200 + col * 100;
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : n ? "panels/button_white" : "panels/row_disabled", x, y, w, h,
                n || on ? 0xFFFFFFFF : 0x90FFFFFF);
            if (on) HudSprite(c, ui, sp, "cursor/cursor_small", x + 12, y + 14, 30, 32);
            HudPixelArtCentred(c, ui, art.ItemIcon(485 + a), x + 104, y + 85, 100);
            HudText(c, ui, f, x + w - 40, y + 60, 48, n ? "\xC3\x97" + std::to_string(n) : "\xE2\x80\x94", n ? Ink : Muted, 2, true);
        }
    // The Apricorn under the cursor: what the game says about it (nothing when there is none).
    const int a = ApricornAt(st.col, st.row), n = st.count[a];
    const float sx = x0 + 960, sw = 840;
    HudPanel(c, ui, sp, "panels/card", sx, by, sw, 520);
    HudOnline::TitledCard(c, ui, sp, f, sx + 24, by + 24, sw - 48, 60, "APRICORN", n ? std::to_string(n) + " in the box" : "None in the box");
    HudPixelArtCentred(c, ui, art.ItemIcon(485 + a), sx + 120, by + 190, 128);
    HudText(c, ui, f, sx + 230, by + 150, 48, tx.Get(HudBag::kItemName, 485 + a), n ? Ink : Muted, 0, true);
    if (n)
    {
        const std::vector<std::string> about = HudWrap(ui, f, 32, tx.Get(HudBag::kItemDesc, 485 + a), sw - 80);
        for (size_t i = 0; i < about.size() && i < 4; i++) HudText(c, ui, f, sx + 40, by + 300 + 48.0f * i, 32, about[i], Ink);
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
