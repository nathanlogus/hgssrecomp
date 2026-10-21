#pragma once
// The Battle Frontier's own screens redrawn in the HUD style. First the Battle Factory's
// rental screen: the six Pokemon on offer, everything about the one under the cursor, the
// team chosen so far, the Summary / Rent / Cancel buttons and the "are these OK?" question.
// The game keeps the input; everything is read each frame from its application (overlay
// 81, not decompiled; it is Platinum's BattleFactoryApp with a GridInputHandler for the
// cursor) and anything that does not check out leaves the game's own picture on screen.
#include "hudparty.h"

namespace HudFrontier
{
using HudParty::Ram;

// The Battle Factory's application (overlay 81): its OverlayManager template.
constexpr uint32_t kFactoryInit = 0x0223DD60, kFactoryExec = 0x0223DEA8, kFactoryExit = 0x0223E234;
// The cursor's places (rentals 0-5, then Summary, Rent / Remove, Cancel) while choosing the
// first rentals: the table its GridInputHandler is made with.
constexpr uint32_t kRentalGrid = 0x022436F0;

struct Factory
{
    int step = 0;           // the application's state: 3 choosing, 4 the "are these OK?" question
    bool openLevel = false; // Open Level, or Level 50
    int count = 0;          // the rentals on offer (6)
    PartyMon mons[6];
    int chosen[3] = {};     // the rentals chosen, in order
    int numChosen = 0, toChoose = 3;
    int cursor = 0;         // 0-5 a rental, 6 Summary, 7 Rent or Remove, 8 Cancel
    bool menu = false;      // the Summary / Rent / Cancel buttons are up
    int shown = 0;          // the rental the card shows: under the cursor, or the one the buttons are for
    int yesNo = -1;         // the cursor of "are these OK?", -1 when it is not up
    bool IsChosen(int i) const
    {
        for (int k = 0; k < numChosen; k++)
            if (chosen[k] == i) return true;
        return false;
    }
};

// The application's data (BattleFactoryApp, as Platinum's up to +0x13): challenge type
// +0x09 (0 single, 1 double: three to choose; the link ones are left to the game), Open
// Level +0x0A, exchange mode +0x0B, how many chosen +0x11, the rentals offered +0x12; the
// rentals (a Party) +0x3C0, the chosen ones' places (u16) +0x3C8, the cursor's
// GridInputHandler +0x464 (its table +0, the place +0x0D, which places take input +0x10:
// the buttons' 6-8 only while they are up), the yes/no +0x46C (a YesNoPrompt, its cursor
// at +0x75). Only the first choice of rentals, not the swaps between battles.
inline bool ReadFactory(const Ram& ram, uint32_t& cache, Factory& s)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, kFactoryInit, kFactoryExec, kFactoryExit, [&](uint32_t m) {
        return ram.Ok(ram.R32(m + 0x1C), 0x48C);
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2) return false;
    const uint32_t d = ram.R32(mgr + 0x1C);
    s.step = (int)ram.R32(mgr + 0x14);
    if ((s.step != 3 && s.step != 4) || ram.R8(d + 0x09) > 1 || ram.R8(d + 0x0B) != 0 || ram.R8(d + 0x12) != 6) return false;
    s.openLevel = ram.R8(d + 0x0A) != 0;
    s.numChosen = (int)ram.R8(d + 0x11);
    if (s.numChosen > 3 || (s.step == 4) != (s.numChosen == 3)) return false;
    for (int k = 0; k < s.numChosen; k++)
    {
        s.chosen[k] = (int)ram.R16(d + 0x3C8 + 2 * k);
        if (s.chosen[k] > 5) return false;
    }
    if (!HudParty::ReadParty(ram, ram.R32(d + 0x3C0), s.count, s.mons) || s.count != 6) return false;
    const uint32_t grid = ram.R32(d + 0x464);
    if (!ram.Ptr(grid) || ram.R32(grid) != kRentalGrid) return false;
    s.cursor = (int)ram.R8(grid + 0x0D);
    s.menu = (ram.R32(grid + 0x10) & 0x1C0) == 0x1C0;
    if (s.cursor > 8 || (s.cursor >= 6 && !s.menu)) return false;
    // While the buttons are up the cursor is on them; they are for the rental it was on.
    static int last = 0;
    if (s.cursor < 6) last = s.cursor;
    s.shown = last;
    s.yesNo = -1;
    if (s.step == 4)
    {
        const uint32_t yn = ram.R32(d + 0x46C);
        if (!ram.Ptr(yn) || ram.R8(yn + 0x75) > 1) return false;
        s.yesNo = (int)ram.R8(yn + 0x75);
    }
    return true;
}

// A round badge with a number (the order a rental was chosen in).
inline void Badge(Canvas& c, const Ui& ui, const RomFont& f, float x, float y, float s, int n)
{
    HudParty::FillRound(c, ui, x, y, s, s, s / 2, 0xFFF08A24);
    HudText(c, ui, f, x + s / 2, y + (s - 32) / 2, 32, std::to_string(n), HudColour::White, 1, true);
}

inline void DrawFactory(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Factory& s, const HudParty::MonIconFn& icon,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    using HudParty::FillRound;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;

    // The title and the level.
    HudText(c, ui, f, x0 + 60, 56, 48, "BATTLE FACTORY", Ink, 0, true);
    const std::string level = s.openLevel ? "Open Level" : "Level 50";
    const float lw = HudTextWidth(ui, f, 32, level, true) + 60;
    HudPanel(c, ui, sp, "panels/button_white", x0 + 1860 - lw, 54, lw, 56);
    HudText(c, ui, f, x0 + 1860 - lw / 2, 66, 32, level, Ink, 1, true);

    // The rentals on offer, two columns of three, then the team chosen so far.
    const float fx = x0 + 60, fy = 140, fw = 960, fh = 850;
    HudPanel(c, ui, sp, "panels/frame_yellow", fx, fy, fw, fh);
    HudText(c, ui, f, fx + 50, fy + 40, 32, "Rental Pok\xC3\xA9mon", Muted);
    for (int k = 0; k < 6; k++)
    {
        const PartyMon& m = s.mons[k];
        const bool here = s.cursor == k || (s.menu && s.shown == k);
        const float tw = 420, th = 150, tx = fx + 50 + (k % 2) * (tw + 20), ty = fy + 90 + (k / 2) * 166;
        HudPanel(c, ui, sp, here ? "panels/row_selected" : "panels/row", tx, ty, tw, th);
        if (here) HudSprite(c, ui, sp, "cursor/cursor_small", tx + 8, ty + th / 2 - 16, 30, 32);
        HudPixelArt(c, ui, icon(m), tx + 40, ty + 11, 128, 128);
        HudText(c, ui, f, tx + 172, ty + 34, 32, m.name, Ink);
        HudParty::Gender(c, ui, tx + 172 + HudTextWidth(ui, f, 32, m.name) + 10, ty + 38, m.gender, 26);
        HudText(c, ui, f, tx + 172, ty + 84, 32, "Lv " + std::to_string(m.level), Muted);
        for (int n = 0; n < s.numChosen; n++)
            if (s.chosen[n] == k) Badge(c, ui, f, tx + tw - 64, ty + 14, 48, n + 1);
    }
    const float ty0 = fy + 90 + 3 * 166 + 20;
    HudText(c, ui, f, fx + 50, ty0, 32, "Your team", Muted);
    for (int n = 0; n < 3; n++)
    {
        const float sw = (fw - 100 - 40) / 3, sx = fx + 50 + n * (sw + 20), sy = ty0 + 50;
        if (n >= s.numChosen)
        {
            HudPanel(c, ui, sp, "panels/row_disabled", sx, sy, sw, 150, 0x80FFFFFF);
            Badge(c, ui, f, sx + sw / 2 - 24, sy + 51, 48, n + 1);
            continue;
        }
        const PartyMon& m = s.mons[s.chosen[n]];
        HudPanel(c, ui, sp, "panels/row", sx, sy, sw, 150);
        Badge(c, ui, f, sx + 14, sy + 14, 40, n + 1);
        HudPixelArt(c, ui, icon(m), sx + sw / 2 - 48, sy + 6, 96, 96);
        HudText(c, ui, f, sx + sw / 2, sy + 102, 32, m.name, Ink, 1);
    }

    // The card: the rental under the cursor (or the last one looked at, while the buttons
    // are up the cursor is on them, and the game keeps showing that one).
    const float cx = x0 + 1080, cw = 780, cy = 140, ch = 850;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    const int shown = s.shown;
    const PartyMon& m = s.mons[s.step == 4 ? s.chosen[2] : shown];
    HudSprite(c, ui, sp, "people/chip_ring_orange", cx + cw / 2 - 90, cy + 26, 180, 180);
    HudPixelArt(c, ui, icon(m), cx + cw / 2 - 64, cy + 52, 128, 128);
    const float nw = HudTextWidth(ui, f, 48, m.name, true);
    HudText(c, ui, f, cx + cw / 2 - 16, cy + 220, 48, m.name, Ink, 1, true);
    HudParty::Gender(c, ui, cx + cw / 2 - 16 + nw / 2 + 14, cy + 230, m.gender);
    {
        std::vector<std::pair<std::string, uint32_t>> pills = { { "Lv " + std::to_string(m.level), 0xFF2F3542 } };
        for (int t = 0; t < 2; t++)
            if (m.types[t] >= 0 && (t == 0 || m.types[1] != m.types[0]))
                pills.push_back({ HudParty::TypeName(m.types[t]), HudParty::TypeColour(m.types[t]) });
        float total = 0;
        for (auto& p : pills) total += HudTextWidth(ui, f, 32, p.first) + 36 + 12;
        float px = cx + cw / 2 - (total - 12) / 2;
        for (auto& p : pills) px += HudParty::Pill(c, ui, f, px, cy + 286, 46, p.first, p.second) + 12;
    }
    const std::pair<std::string, std::string> rows[3] = { { "Ability", Party_Text(720, m.ability) },
        { "Nature", Party_Text(34, m.nature) }, { "Item", m.item ? Party_Text(222, m.item) : "None" } };
    for (int i = 0; i < 3; i++)
    {
        const float ry = cy + 352 + 52 * i;
        HudPanel(c, ui, sp, "panels/row", cx + 36, ry, cw - 72, 44);
        HudText(c, ui, f, cx + 56, ry + 6, 32, rows[i].first, Muted);
        HudText(c, ui, f, cx + cw - 56, ry + 6, 32, rows[i].second, Ink, 2);
    }
    const float mw = (cw - 72 - 16) / 2;
    for (int k = 0; k < 4; k++)
    {
        const float mx = cx + 36 + (k % 2) * (mw + 16), my = cy + 518 + (k / 2) * 74;
        if (!m.moves[k])
        {
            FillRound(c, ui, mx, my, mw, 62, 31, 0xFFE9EDF2);
            HudText(c, ui, f, mx + 30, my + 15, 32, "-", Muted);
            continue;
        }
        FillRound(c, ui, mx, my, mw, 62, 31, HudParty::TypeColour(m.moveType[k]));
        HudText(c, ui, f, mx + 26, my + 15, 32, Party_Text(750, m.moves[k]), White);
        HudText(c, ui, f, mx + mw - 24, my + 15, 32, std::to_string(m.ppMax[k]), White, 2);
    }

    // What the game says, and its buttons or question under it.
    const bool buttons = s.menu || s.step == 4;
    const float by = cy + ch - 36 - 60, my0 = buttons ? by - 16 - 64 : by - 4;
    std::string msg;
    if (s.step == 4) msg = "Are these Pok\xC3\xA9mon OK?";
    else if (s.menu) msg = "What about " + m.name + "?";
    else msg = "Please choose Pok\xC3\xA9mon no. " + std::to_string(s.numChosen + 1) + ".";
    HudPanel(c, ui, sp, "panels/dialogue_box", cx + 36, my0, cw - 72, 64);
    HudText(c, ui, f, cx + 66, my0 + 16, 32, msg, Ink);
    if (buttons)
    {
        const bool yn = s.step == 4;
        const int n = yn ? 2 : 3;
        const float bw = (cw - 72 - 16 * (n - 1)) / n;
        const std::string labels[3] = { yn ? "Yes" : "Summary", yn ? "No" : s.IsChosen(shown) ? "Remove" : "Rent", "Cancel" };
        for (int k = 0; k < n; k++)
        {
            const bool on = yn ? s.yesNo == k : s.cursor == 6 + k;
            const float bx = cx + 36 + k * (bw + 16);
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", bx, by, bw, 60);
            if (on) HudSprite(c, ui, sp, "cursor/cursor_small", bx + 12, by + 14, 30, 32);
            HudText(c, ui, f, bx + bw / 2, by + 14, 32, labels[k], Ink, 1);
        }
    }
    DrawHintBar(c, ui, sp, f, hints);
}

// The Battle Hall's type board (overlay 82, Platinum's BattleHallApp): the twenty places
// of its 4 x 5 grid, the types in the game's order, the Pokemon (17 and 18, its summary)
// and "???" (19).
constexpr uint32_t kHallInit = 0x0223DD60, kHallExec = 0x0223DE20, kHallExit = 0x0223DF74;
constexpr int kHallTypes[17] = { 0, 10, 11, 13, 12, 15, 1, 3, 4, 2, 14, 6, 5, 7, 16, 17, 8 };

struct Hall
{
    int cursor = 0;          // 0-16 a type, 17 and 18 the Pokemon, 19 "???"
    int ranks[17] = {};      // each type's rank, 0-9 (10: all its battles won)
    int count = 0, mon = 0;  // the Pokemon entered (and which is shown)
    PartyMon mons[6];
    int yesNo = -1;          // the cursor of "battle this type?", -1 when it is not up
};

// BattleHallApp: challenge type +0x09, the cursor +0x0D, the yes/no up +0x0F, the yes/no
// +0x8C (a YesNoPrompt, its cursor at +0x75), the Pokemon shown +0x20C, the Pokemon (a
// Party) +0x214, the ranks (a nybble each, by the board's order) +0x218. Shown while the
// application waits for a choice (its state 2), not in the link challenges.
inline bool ReadHall(const Ram& ram, uint32_t& cache, Hall& s)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, kHallInit, kHallExec, kHallExit, [&](uint32_t m) {
        return ram.Ok(ram.R32(m + 0x1C), 0x2A0);
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2 || ram.R32(mgr + 0x14) != 2) return false;
    const uint32_t d = ram.R32(mgr + 0x1C);
    if (ram.R8(d + 0x09) > 1) return false;
    s.cursor = (int)ram.R8(d + 0x0D);
    if (s.cursor > 19) return false;
    const uint32_t ranks = ram.R32(d + 0x218);
    if (!ram.Ok(ranks, 9)) return false;
    for (int i = 0; i < 17; i++) s.ranks[i] = (int)(ram.R8(ranks + i / 2) >> (4 * (i % 2)) & 0xF);
    if (!HudParty::ReadParty(ram, ram.R32(d + 0x214), s.count, s.mons)) return false;
    s.mon = (int)ram.R32(d + 0x20C);
    if (s.mon < 0 || s.mon >= s.count) return false;
    s.yesNo = -1;
    if (ram.R8(d + 0x0F))
    {
        const uint32_t yn = ram.R32(d + 0x8C);
        if (!ram.Ptr(yn) || ram.R8(yn + 0x75) > 1 || s.cursor > 16) return false;
        s.yesNo = (int)ram.R8(yn + 0x75);
    }
    return true;
}

inline void DrawHall(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Hall& s, const HudParty::MonIconFn& icon,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    using HudParty::FillRound;
    DrawBackdrop(c, sp, "backgrounds/bg_lavender", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;
    HudText(c, ui, f, x0 + 60, 56, 48, "BATTLE HALL", Ink, 0, true);
    const std::string choose = "Please select what type of Pok\xC3\xA9mon you\xE2\x80\x99" "d like to battle.";

    // The board: four columns of types, their ranks as ten pips; the types already beaten
    // ten times are greyed. The last row: Steel, the Pokemon, "???".
    const float bx = x0 + 60, by = 140, cw = 270, chh = 150, gap = 14;
    for (int i = 0; i < 20; i++)
    {
        if (i == 18) continue; // the Pokemon spans 17 and 18
        const float x = bx + (i % 4) * (cw + gap), y = by + (i / 4) * (chh + gap);
        const bool here = s.cursor == i || (i == 17 && s.cursor == 18);
        if (i == 17 || i == 19)
        {
            const float w = i == 17 ? 2 * cw + gap : cw;
            HudPanel(c, ui, sp, here ? "panels/row_selected" : "panels/row", x, y, w, chh);
            if (here) HudSprite(c, ui, sp, "cursor/cursor_small", x + 8, y + chh / 2 - 16, 30, 32);
            if (i == 19)
            {
                HudText(c, ui, f, x + cw / 2, y + chh / 2 - 24, 48, "???", Muted, 1, true);
                continue;
            }
            const PartyMon& m = s.mons[s.mon];
            HudPixelArt(c, ui, icon(m), x + 44, y + 11, 128, 128);
            HudText(c, ui, f, x + 190, y + 36, 32, m.name, Ink);
            HudText(c, ui, f, x + 190, y + 84, 32, "Summary", Muted);
            continue;
        }
        const int type = kHallTypes[i], rank = std::min(s.ranks[i], 10);
        const bool done = rank >= 10;
        if (here) FillRound(c, ui, x - 8, y - 8, cw + 16, chh + 16, 30, 0xFFF5B82E);
        FillRound(c, ui, x, y, cw, chh, 24, done ? 0xFFB8BEC8 : HudParty::TypeColour(type));
        HudText(c, ui, f, x + 24, y + 22, 32, HudParty::TypeName(type), White, 0, true);
        HudText(c, ui, f, x + cw - 24, y + 22, 32, done ? "Clear" : "Rank " + std::to_string(rank + 1), White, 2);
        for (int k = 0; k < 10; k++)
            FillRound(c, ui, x + 24 + k * 22, y + 100, 16, 16, 8, k < rank ? 0xFFFFFFFF : 0x50FFFFFF);
    }

    // The card: the Pokemon and the type under the cursor, the question with its answers.
    const float cx = x0 + 1210, cwd = 650, cy = 140, ch = 850;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cwd, ch);
    const PartyMon& m = s.mons[s.mon];
    HudSprite(c, ui, sp, "people/chip_ring_orange", cx + cwd / 2 - 90, cy + 26, 180, 180);
    HudPixelArt(c, ui, icon(m), cx + cwd / 2 - 64, cy + 52, 128, 128);
    const float nw = HudTextWidth(ui, f, 48, m.name, true);
    HudText(c, ui, f, cx + cwd / 2 - 16, cy + 220, 48, m.name, Ink, 1, true);
    HudParty::Gender(c, ui, cx + cwd / 2 - 16 + nw / 2 + 14, cy + 230, m.gender);
    HudText(c, ui, f, cx + cwd / 2, cy + 290, 32, "Lv " + std::to_string(m.level), Muted, 1);
    std::string msg = choose;
    if (s.cursor <= 16)
    {
        const int type = kHallTypes[s.cursor], rank = std::min(s.ranks[s.cursor], 10);
        const float ty = cy + 370;
        FillRound(c, ui, cx + 36, ty, cwd - 72, 140, 24, rank >= 10 ? 0xFFB8BEC8 : HudParty::TypeColour(type));
        HudText(c, ui, f, cx + cwd / 2, ty + 24, 48, HudParty::TypeName(type), White, 1, true);
        HudText(c, ui, f, cx + cwd / 2, ty + 86, 32, rank >= 10 ? "All ranks cleared" : "Rank " + std::to_string(rank + 1) + " of 10", White, 1);
        if (s.yesNo >= 0) msg = "Battle " + HudParty::TypeName(type) + "-type Pok\xC3\xA9mon?";
    }
    else if (s.cursor != 19)
        msg = "See " + m.name + "\xE2\x80\x99s summary.";
    const float by0 = cy + ch - 36 - 60;
    std::vector<std::string> lines = HudWrap(ui, f, 32, msg, cwd - 72 - 60);
    lines.resize(std::min(lines.size(), (size_t)3));
    const float mh = 40 + 50.0f * lines.size(), my = (s.yesNo >= 0 ? by0 - 16 : by0 + 60) - mh;
    HudPanel(c, ui, sp, "panels/dialogue_box", cx + 36, my, cwd - 72, mh);
    for (size_t i = 0; i < lines.size(); i++) HudText(c, ui, f, cx + 66, my + 22 + 50.0f * i, 32, lines[i], Ink);
    if (s.yesNo >= 0)
    {
        const float bw = (cwd - 72 - 16) / 2;
        for (int k = 0; k < 2; k++)
        {
            const float x = cx + 36 + k * (bw + 16);
            HudPanel(c, ui, sp, s.yesNo == k ? "panels/button_yellow" : "panels/button_white", x, by0, bw, 60);
            if (s.yesNo == k) HudSprite(c, ui, sp, "cursor/cursor_small", x + 12, by0 + 14, 30, 32);
            HudText(c, ui, f, x + bw / 2, by0 + 14, 32, k ? "No" : "Yes", Ink, 1);
        }
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
