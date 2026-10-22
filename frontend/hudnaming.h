#pragma once
// The naming screen's keyboard (src/naming_screen.c) redrawn in the HUD style: the name being
// typed in its boxes, the keyboard's page as a grid of keys and its buttons above. The game
// keeps taking the input; everything shown is read from its memory each frame, and anything
// that does not check out leaves the game's own picture on screen.
#include "hudbag.h"

namespace HudNaming
{
// The page's buttons in the keyboard's top row (NAME_SCREEN_BUTTON_*), and a key not used.
constexpr uint16_t kUpper = 0xE002, kLower = 0xE003, kSymbols = 0xE004, kBack = 0xE007, kOk = 0xE008, kSkip = 0xD004;

struct State
{
    int type = 0, species = 0, form = 0, maxLen = 0;
    uint16_t keys[6][13] = {};
    int x = 0, y = 0;           // the keyboard's cursor (row 0: the buttons)
    std::vector<uint16_t> name; // what is typed so far
    int at = 0;                 // where the next letter goes
    std::string prompt;
    int step = 0;               // NamingScreenApp_Main's step: 0 fading in, 1-2 typing, 3 leaving
};

// NamingScreenAppData (the OverlayManager's data): its kind (+0: 1 a Pokemon), the
// Pokemon's species and form (+4, +8), the longest name (+0xC), the keyboard's cursor
// (+0x1C, +0x20), the keyboard (6 rows of 13 codes from +0x3A, the first the buttons), the
// name typed (+0xD8, ended by 0xFFFF), where the next letter goes (+0x158) and the
// question (a String, +0x178).
template <class Read>
bool ReadState(Read rd, uint32_t init, uint32_t exec, uint32_t exit, uint32_t& cache, State& st)
{
    using HudBag::InRam;
    const uint32_t man = HudBag::FindManager(rd, init, exec, exit, cache);
    if (!man) return false;
    const uint32_t d = rd(man + 0x1C);
    auto rd16 = [&](uint32_t a) { return rd(a) & 0xFFFF; };
    st.step = (int)rd(man + 0x14);
    st.type = (int)rd(d);
    st.species = (int)rd(d + 4);
    st.form = (int)rd(d + 8);
    st.maxLen = (int)rd(d + 0xC);
    st.x = (int)rd(d + 0x1C);
    st.y = (int)rd(d + 0x20);
    st.at = (int)rd16(d + 0x158);
    if (st.step < 0 || st.step > 3 || st.type > 7 || st.maxLen < 1 || st.maxLen > 16 || st.x < 0 || st.x > 12 || st.y < 0 || st.y > 5 ||
        st.at > st.maxLen)
        return false;
    for (int r = 0; r < 6; r++)
        for (int k = 0; k < 13; k++) st.keys[r][k] = (uint16_t)rd16(d + 0x3A + 2 * (13 * r + k));
    for (int k = 0; k < 13; k++)
        if (st.keys[0][k] != kSkip && (st.keys[0][k] < kUpper || st.keys[0][k] > kOk)) return false;
    st.name.clear();
    for (int i = 0; i < st.maxLen; i++)
    {
        const uint16_t c = (uint16_t)rd16(d + 0xD8 + 2 * i);
        if (c == 0xFFFF) break;
        st.name.push_back(c);
    }
    if (!HudScreens::GameString(rd16, rd(d + 0x178), st.prompt)) st.prompt.clear();
    return true;
}

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Image* icon, const State& st, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_lavender", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, cw = 1500, cx = wd / 2 - cw / 2;

    // The question and the name: a box for each letter, the next one to fill marked.
    // (Both cards end 30 above the hint bar, the same space inside them above and below.)
    HudPanel(c, ui, sp, "panels/card", cx, 40, cw, 240);
    if (icon) HudPixelArtCentred(c, ui, icon, cx + 104, 160, 0, 128);
    const float tx0 = cx + (icon ? 200 : 50);
    HudText(c, ui, f, tx0, 70, 48, st.prompt, Ink, 0, true);
    const float bw = 64, gap = 10, bx0 = tx0, by = 160;
    for (int i = 0; i < st.maxLen; i++)
    {
        const float x = bx0 + i * (bw + gap);
        const bool next = i == st.at;
        HudPanel(c, ui, sp, next ? "panels/code_cell_selected" : "panels/code_cell", x, by, bw, 84);
        if (i < (int)st.name.size()) HudText(c, ui, f, x + bw / 2, by + 18, 48, NdsText(&st.name[i], 1), Ink, 1);
    }

    // The buttons: the three pages, Back and OK, each as wide as the keys it covers.
    HudPanel(c, ui, sp, "panels/card", cx, 310, cw, 674);
    const float kx0 = cx + 40, kw = (cw - 80) / 13;
    const uint16_t page = st.keys[1][0] == 0x145 ? kLower : st.keys[1][0] == 0x12B ? kUpper : kSymbols;
    for (int k = 0; k < 13;)
    {
        const uint16_t b = st.keys[0][k];
        int n = 1;
        while (k + n < 13 && st.keys[0][k + n] == b) n++;
        if (b != kSkip)
        {
            const bool on = st.y == 0 && st.x >= k && st.x < k + n;
            const char* label = b == kUpper ? "ABC" : b == kLower ? "abc" : b == kSymbols ? "?!&" : b == kBack ? "Delete" : "OK";
            const float x = kx0 + k * kw + 4, w = n * kw - 8;
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : b == page ? "panels/button_lavender" : "panels/button_white", x, 340, w, 70);
            HudText(c, ui, f, x + w / 2, 359, 32, label, Ink, 1);
        }
        k += n;
    }
    // The keys of the page, the one under the cursor yellow.
    for (int r = 1; r < 6; r++)
        for (int k = 0; k < 13; k++)
        {
            const uint16_t key = st.keys[r][k];
            if (key == kSkip) continue;
            const float x = kx0 + k * kw + 4, y = 440 + (r - 1) * 106, w = kw - 8;
            const bool on = st.y == r && st.x == k;
            const bool blank = key == 0x1DE || key == 0x3000 || key == 1;
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", x, y, w, 90, blank && !on ? 0x60FFFFFF : 0xFFFFFFFF);
            if (!blank) HudText(c, ui, f, x + w / 2, y + 22, 48, NdsText(&key, 1), Ink, 1);
        }
    DrawHintBar(c, ui, sp, f, hints);
}
}
