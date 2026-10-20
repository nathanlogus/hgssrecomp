#pragma once
// The pause menu and the button-hint bar, drawn with the HUD's sprites and the game's font
// (hud.h) as the online-HUD design has them: menu discs for the tabs on the left, the
// selected row's description in a dialogue box at the top, the rows on a card below it,
// and the buttons that do something in a strip along the bottom. Positions are design
// pixels on 1920x1080.
#include "hud.h"

namespace HudColour
{
constexpr uint32_t Ink = 0xFF3B3F4A, Muted = 0xFF8A93A3, Danger = 0xFFC83C3C, Yellow = 0xFFFFE04A, White = 0xFFFFFFFF;
}

// Text `px` design pixels tall (16, 32, 48...), at design position (x, y) of its top left;
// align 0 left, 1 centre, 2 right of x.
inline void HudText(Canvas& c, const Ui& ui, const RomFont& f, float x, float y, float px, const std::string& s, uint32_t ink,
    int align = 0, bool bold = false)
{
    const float scale = ui.Font(px);
    int dx = (int)std::lround(ui(x));
    const int w = f.Width(s, scale, bold);
    if (align == 1) dx -= w / 2;
    else if (align == 2) dx -= w;
    f.Draw(c, dx, (int)std::lround(ui(y)), s, scale, ink, 0, bold);
}

// The design width of text, for laying out.
inline float HudTextWidth(const Ui& ui, const RomFont& f, float px, const std::string& s, bool bold = false)
{
    return f.Width(s, ui.Font(px), bold) / ui.s;
}

// A panel sprite whose visible box is (x, y, w, h): its soft shadow lies outside the box
// (12 left and right, 10 above, 14 below), so the 9-slice is drawn larger.
inline void HudPanel(Canvas& c, const Ui& ui, Sprites& sp, const char* name, float x, float y, float w, float h,
    uint32_t tint = 0xFFFFFFFF)
{
    DrawSprite(c, sp.Get(name), ui(x - 12), ui(y - 10), ui(w + 24), ui(h + 24), ui.s, tint);
}

inline void HudSprite(Canvas& c, const Ui& ui, Sprites& sp, const std::string& name, float x, float y, float w, float h,
    uint32_t tint = 0xFFFFFFFF)
{
    DrawSprite(c, sp.Get(name), ui(x), ui(y), ui(w), ui(h), ui.s, tint);
}

// Words of `text` in lines no wider than `width` design pixels.
inline std::vector<std::string> HudWrap(const Ui& ui, const RomFont& f, float px, const std::string& text, float width)
{
    std::vector<std::string> lines;
    std::string line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && HudTextWidth(ui, f, px, tryLine) > width)
        {
            lines.push_back(line);
            line = word;
        }
        else
            line = tryLine;
        word.clear();
    };
    for (char ch : text)
    {
        if (ch == ' ') flushWord();
        else word += ch;
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

struct HudHint
{
    std::vector<std::string> glyphs; // button sprites, e.g. "buttons/button_l"
    std::string label;
};

// The white strip along the bottom with what the buttons do, right-aligned.
inline void DrawHintBar(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::vector<HudHint>& hints)
{
    const float wd = ui.W / ui.s, top = 1080 - 56;
    HudSprite(c, ui, sp, "panels/hint_bar", 0, top - 3, wd, 59);
    FillBlend(c, 0, ui(top - 3), (float)c.w, ui(3), 0xFFE3E9F1);
    float x = wd - 32;
    for (int i = (int)hints.size() - 1; i >= 0; i--)
    {
        const HudHint& h = hints[i];
        x -= HudTextWidth(ui, f, 32, h.label);
        HudText(c, ui, f, x, top + 12, 32, h.label, HudColour::Ink);
        x -= 14;
        for (int g = (int)h.glyphs.size() - 1; g >= 0; g--)
        {
            const bool wide = h.glyphs[g].find("button_l") != std::string::npos || h.glyphs[g].find("button_r") != std::string::npos ||
                h.glyphs[g].find("button_z") != std::string::npos;
            const float gw = wide ? 52 : 40, gh = wide ? 38 : 40;
            x -= gw;
            HudSprite(c, ui, sp, h.glyphs[g], x, top + (56 - gh) / 2, gw, gh);
            x -= 12;
        }
        x -= 24;
    }
}

// The full-screen backdrop of a menu screen ("backgrounds/bg_lime"), over the game.
inline void DrawBackdrop(Canvas& c, Sprites& sp, const std::string& name, uint32_t tint = 0xF0FFFFFF)
{
    const Sprite* s = sp.Get(name);
    if (!s) return;
    // Cover the canvas, cropping the picture's sides or top and bottom.
    const float k = std::max((float)c.w / s->img.w, (float)c.h / s->img.h);
    const float sw = c.w / k, sh = c.h / k;
    Blit(c, s->img, (s->img.w - sw) / 2, (s->img.h - sh) / 2, sw, sh, 0, 0, (float)c.w, (float)c.h, true, tint);
}

// The pause menu (Menu in ui.h) as the online-HUD design draws it.
// A panel beside the rows (`sideW` design pixels wide, e.g. the Profile tab's trainer
// card); DrawPauseMenu says where it goes.
struct HudRect
{
    float x = 0, y = 0, w = 0, h = 0;
};

inline void DrawPauseMenu(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Menu& m, const std::vector<HudHint>& hints,
    float sideW = 0, HudRect* side = nullptr)
{
    using namespace HudColour;
    const float wd = ui.W / ui.s; // design width: 1920 at 16:9, more on wider screens
    // Over the game, dimmed and lightly tinted, so changes to the view show behind it. The
    // same every frame: made once for the canvas's size and copied (blending it each frame
    // took most of a frame's time).
    static std::vector<uint32_t> backdrop;
    static int backdropW = 0, backdropH = 0;
    if (backdropW != c.w || backdropH != c.h)
    {
        Canvas t;
        t.Resize(c.w, c.h);
        FillBlend(t, 0, 0, (float)c.w, (float)c.h, 0x59000000);
        DrawBackdrop(t, sp, "backgrounds/bg_lime", 0x48FFFFFF);
        backdrop = t.px;
        backdropW = c.w;
        backdropH = c.h;
    }
    std::copy(backdrop.begin(), backdrop.end(), c.px.begin());
    c.Mark(0, 0, c.w, c.h);

    // Tabs: a column of menu discs; the current one larger, with its name on a yellow pill.
    const float colX = 60, colY = 40, colW = 250, colH = 960;
    HudPanel(c, ui, sp, "panels/card_glass", colX, colY, colW, colH);
    const int nt = (int)m.tabs.size();
    const float pitch = std::min(126.0f, (colH - 60) / std::max(1, nt));
    float y = colY + (colH - (pitch * nt + 24)) / 2;
    for (int t = 0; t < nt; t++)
    {
        const bool sel = t == m.tab;
        const float d = sel ? 112 : 88, cx = colX + colW / 2;
        const float itemH = pitch + (sel ? 24 : 0);
        const float dy = y + (itemH - d - 40) / 2;
        HudSprite(c, ui, sp, m.tabs[t].icon + (sel ? "_selected" : ""), cx - d / 2, dy, d, d);
        const float ly = dy + d + 2;
        if (sel)
        {
            const float lw = HudTextWidth(ui, f, 32, m.tabs[t].name) + 28;
            HudPanel(c, ui, sp, "panels/button_yellow", cx - lw / 2, ly, lw, 36);
        }
        HudText(c, ui, f, cx, ly + 2, 32, m.tabs[t].name, sel ? Ink : Muted, 1);
        y += itemH;
    }

    // The selected row's description.
    const float boxX = 350, boxW = wd - boxX - 60;
    const MenuItem* cur = m.Selected();
    std::vector<std::string> lines = HudWrap(ui, f, 48, cur ? cur->desc : "", boxW - 66);
    if (lines.empty()) lines.push_back(" ");
    if (lines.size() > 2) lines.resize(2);
    const float boxH = 36 + 62.0f * lines.size() + 10;
    HudPanel(c, ui, sp, "panels/dialogue_box", boxX, colY, boxW, boxH);
    for (size_t i = 0; i < lines.size(); i++)
        HudText(c, ui, f, boxX + 33, colY + 23 + 62.0f * i, 48, lines[i], Ink);

    // The rows, scrolling to keep the selected one in view.
    const std::vector<int> rows = m.Rows();
    const float cardY = colY + boxH + 60, cardBottom = 1000;
    const float cardW = sideW > 0 ? boxW - sideW - 30 : boxW;
    if (side) *side = { boxX + cardW + 30, cardY, sideW, cardBottom - cardY };
    const float rowX = boxX + 23, rowW = cardW - 46, rowH = 50, rowPitch = 60;
    const int fit = std::max(1, (int)((cardBottom - cardY - 46) / rowPitch));
    const int n = (int)rows.size();
    const int first = std::clamp(m.selected - fit / 2, 0, std::max(0, n - fit));
    const int shown = std::min(fit, n);
    HudPanel(c, ui, sp, "panels/card_glass", boxX, cardY, cardW, 40 + shown * rowPitch - 10 + 6);
    const float valueRight = rowX + rowW - 49, valueW = 240, valueX = valueRight - valueW;
    for (int k = 0; k < shown; k++)
    {
        const int idx = first + k;
        const MenuItem& it = m.items[rows[idx]];
        const bool sel = idx == m.selected;
        const float ry = cardY + 23 + k * rowPitch;
        HudPanel(c, ui, sp, sel ? "panels/row_selected" : it.disabled ? "panels/row_disabled" : "panels/row", rowX, ry, rowW, rowH);
        if (sel) HudSprite(c, ui, sp, "cursor/cursor", rowX + 10, ry + (rowH - 40) / 2, 36, 40);
        const float ty = ry + (rowH - 32) / 2;
        HudText(c, ui, f, rowX + 68, ty, 32, it.label, it.danger ? Danger : it.disabled ? Muted : Ink);
        if (!it.glyph.empty())
            HudSprite(c, ui, sp, it.glyph, valueX + valueW / 2 - 20, ry + 5, 40, 40);
        else if (!it.value.empty())
        {
            const bool muted = it.value == "Off" || it.disabled;
            if (it.bar >= 0)
            {
                // A bar to the left of the value, as for the world zoom.
                const float bw = 280, bx = valueX - bw - 24, by = ry + (rowH - 22) / 2;
                HudSprite(c, ui, sp, "bars/bar_frame", bx, by, bw, 22);
                const float fw = std::max(16.0f, (bw - 12) * std::clamp(it.bar, 0.0f, 1.0f));
                HudSprite(c, ui, sp, "bars/fill_teal", bx + 6, by + 4, fw, 14);
            }
            HudText(c, ui, f, valueX + valueW / 2, ty, 32, it.value, muted ? Muted : Ink, 1);
        }
        if (sel && !it.value.empty() && !it.disabled)
        {
            HudSprite(c, ui, sp, "cursor/value_arrow_left", valueX - 26, ry + 5, 32, 40);
            HudSprite(c, ui, sp, "cursor/value_arrow_right", valueRight - 6, ry + 5, 32, 40);
        }
    }
    // More rows than fit: a scroll bar on the card's right edge.
    if (n > shown)
    {
        const float tx = boxX + cardW - 14, ty = cardY + 23, th = shown * rowPitch - 10;
        HudSprite(c, ui, sp, "cursor/scrollbar_track", tx, ty, 8, th);
        const float thumbH = std::max(30.0f, th * shown / n);
        HudSprite(c, ui, sp, "cursor/scrollbar_thumb", tx, ty + (th - thumbH) * first / std::max(1, n - shown), 8, thumbH);
    }
    DrawHintBar(c, ui, sp, f, hints);
}
