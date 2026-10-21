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

// The Factory's swap after a won battle: choosing one of your three to give (state 6),
// then one of the last opponent's three to take (state 8), with "Will you accept this
// Pokemon?".
struct Swap
{
    bool receiving = false;           // choosing the one to take
    int count = 0, theirCount = 0;
    PartyMon mine[6], theirs[6];
    int give = -1;                    // yours being given, once chosen
    int cursor = 0;                   // 0-2 a Pokemon; giving: 3 Cancel, 4-6 the menu; taking: 4 Back, 5 Cancel, 6-7 the menu
    bool menu = false;
    int shown = 0;                    // the Pokemon the card shows
    int yesNo = -1;
};

// The grids' tables (GridInputHandler +0) while giving and while taking.
constexpr uint32_t kGiveGrid = 0x02243668, kTakeGrid = 0x022436A8;

// BattleFactoryApp in exchange mode (+0x0B): your Pokemon +0x3C0, the opponent's +0x3C4,
// how many chosen +0x11 and which (+0x3C8), the cursor's GridInputHandler +0x464 (the menu
// is up when its places 4 / 6 take input: bits of +0x10), the yes/no +0x46C (a
// YesNoPrompt placed at 25, 10 while it is up; its cursor at +0x75).
inline bool ReadSwap(const Ram& ram, uint32_t& cache, Swap& s)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, kFactoryInit, kFactoryExec, kFactoryExit, [&](uint32_t m) {
        return ram.Ok(ram.R32(m + 0x1C), 0x48C);
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2) return false;
    const uint32_t d = ram.R32(mgr + 0x1C);
    const int step = (int)ram.R32(mgr + 0x14);
    if ((step != 6 && step != 8) || ram.R8(d + 0x09) > 1 || ram.R8(d + 0x0B) != 1 || ram.R8(d + 0x12) != 3) return false;
    s.receiving = step == 8;
    if (!HudParty::ReadParty(ram, ram.R32(d + 0x3C0), s.count, s.mine) || !HudParty::ReadParty(ram, ram.R32(d + 0x3C4), s.theirCount, s.theirs) ||
        s.count != 3 || s.theirCount != 3)
        return false;
    const uint32_t grid = ram.R32(d + 0x464);
    if (!ram.Ptr(grid) || ram.R32(grid) != (s.receiving ? kTakeGrid : kGiveGrid)) return false;
    s.cursor = (int)ram.R8(grid + 0x0D);
    const uint32_t on = ram.R32(grid + 0x10);
    s.menu = s.receiving ? (on >> 6 & 1) != 0 : (on >> 4 & 1) != 0;
    const int most = s.receiving ? 7 : 6;
    if (s.cursor > most || (s.receiving && s.cursor == 3) || (!s.menu && s.cursor >= (s.receiving ? 6 : 4))) return false;
    s.give = -1;
    if (ram.R8(d + 0x11) == 1)
    {
        s.give = (int)ram.R16(d + 0x3C8);
        if (s.give > 2) return false;
    }
    if (s.receiving && s.give < 0) return false;
    static int last = 0;
    if (s.cursor < 3) last = s.cursor;
    s.shown = last;
    s.yesNo = -1;
    const uint32_t yn = ram.R32(d + 0x46C);
    if (s.receiving && ram.Ptr(yn) && ram.R8(yn + 0x70) == 25 && ram.R8(yn + 0x71) == 10)
    {
        s.yesNo = (int)ram.R8(yn + 0x75);
        if (s.yesNo > 1) return false;
    }
    return true;
}

// One Pokemon in full: name, level, types, ability, nature, item and moves.
inline void MonCard(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const PartyMon& m, const HudParty::MonIconFn& icon,
    float cx, float cy, float cw)
{
    using namespace HudColour;
    HudSprite(c, ui, sp, "people/chip_ring_orange", cx + cw / 2 - 90, cy + 26, 180, 180);
    HudPixelArt(c, ui, icon(m), cx + cw / 2 - 64, cy + 52, 128, 128);
    const float nw = HudTextWidth(ui, f, 48, m.name, true);
    HudText(c, ui, f, cx + cw / 2 - 16, cy + 220, 48, m.name, Ink, 1, true);
    HudParty::Gender(c, ui, cx + cw / 2 - 16 + nw / 2 + 14, cy + 230, m.gender);
    std::vector<std::pair<std::string, uint32_t>> pills = { { "Lv " + std::to_string(m.level), 0xFF2F3542 } };
    for (int t = 0; t < 2; t++)
        if (m.types[t] >= 0 && (t == 0 || m.types[1] != m.types[0])) pills.push_back({ HudParty::TypeName(m.types[t]), HudParty::TypeColour(m.types[t]) });
    float total = 0;
    for (auto& p : pills) total += HudTextWidth(ui, f, 32, p.first) + 36 + 12;
    float px = cx + cw / 2 - (total - 12) / 2;
    for (auto& p : pills) px += HudParty::Pill(c, ui, f, px, cy + 286, 46, p.first, p.second) + 12;
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
            HudParty::FillRound(c, ui, mx, my, mw, 62, 31, 0xFFE9EDF2);
            continue;
        }
        HudParty::FillRound(c, ui, mx, my, mw, 62, 31, HudParty::TypeColour(m.moveType[k]));
        HudText(c, ui, f, mx + 26, my + 15, 32, Party_Text(750, m.moves[k]), White);
        HudText(c, ui, f, mx + mw - 24, my + 15, 32, std::to_string(m.ppMax[k]), White, 2);
    }
}

inline void DrawSwap(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Swap& s, const HudParty::MonIconFn& icon,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;
    HudText(c, ui, f, x0 + 60, 56, 48, "BATTLE FACTORY", Ink, 0, true);
    const std::string what = "Swap a Pok\xC3\xA9mon";
    const float lw = HudTextWidth(ui, f, 32, what, true) + 60;
    HudPanel(c, ui, sp, "panels/button_white", x0 + 1860 - lw, 54, lw, 56);
    HudText(c, ui, f, x0 + 1860 - lw / 2, 66, 32, what, Ink, 1, true);

    // Your team, then the opponent's: the row being chosen from is the bright one.
    const float fx = x0 + 60, fy = 140, fw = 960, fh = 850;
    HudPanel(c, ui, sp, "panels/frame_yellow", fx, fy, fw, fh);
    for (int row = 0; row < 2; row++)
    {
        const bool active = row == (s.receiving ? 1 : 0);
        const float ry = fy + 40 + row * 330;
        HudText(c, ui, f, fx + 50, ry, 32, row ? "Their team" : "Your team", Muted);
        for (int k = 0; k < 3; k++)
        {
            const PartyMon& m = row ? s.theirs[k] : s.mine[k];
            const float tw = (fw - 100 - 40) / 3, tx = fx + 50 + k * (tw + 20), ty = ry + 50;
            const bool here = active && (s.cursor == k || (s.menu && s.shown == k));
            const bool given = row == 0 && s.give == k;
            HudPanel(c, ui, sp, here ? "panels/row_selected" : active || given ? "panels/row" : "panels/row_disabled", tx, ty, tw, 220,
                active || given ? 0xFFFFFFFF : 0xA0FFFFFF);
            if (here) HudSprite(c, ui, sp, "cursor/cursor_small", tx + 8, ty + 20, 30, 32);
            HudPixelArt(c, ui, icon(m), tx + tw / 2 - 64, ty + 10, 128, 128);
            HudText(c, ui, f, tx + tw / 2, ty + 140, 32, m.name, Ink, 1);
            HudText(c, ui, f, tx + tw / 2, ty + 180, 32, "Lv " + std::to_string(m.level), Muted, 1);
            if (given) HudParty::Pill(c, ui, f, tx + 10, ty + 10, 40, "Giving", 0xFFF08A24);
        }
    }
    // The buttons under the rows: Cancel while giving; Back and Cancel while taking.
    const std::vector<std::pair<std::string, int>> under = s.receiving ? std::vector<std::pair<std::string, int>>{ { "Back", 4 }, { "Cancel", 5 } }
                                                                       : std::vector<std::pair<std::string, int>>{ { "Cancel", 3 } };
    for (size_t k = 0; k < under.size(); k++)
    {
        const bool on = !s.menu && s.cursor == under[k].second;
        const float bx = fx + fw - 50 - 260 - (under.size() - 1 - k) * 280, by = fy + fh - 110;
        HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", bx, by, 260, 60);
        if (on) HudSprite(c, ui, sp, "cursor/cursor_small", bx + 10, by + 14, 30, 32);
        HudText(c, ui, f, bx + 130, by + 14, 32, under[k].first, Ink, 1);
    }

    // The card: the Pokemon under the cursor, the game's words and its buttons.
    const float cx = x0 + 1080, cw = 780, cy = 140, ch = 850;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    MonCard(c, ui, sp, f, s.receiving ? s.theirs[s.shown] : s.mine[s.shown], icon, cx, cy, cw);
    std::string msg = s.receiving ? "Choose the Pok\xC3\xA9mon you want to receive." : "Choose the Pok\xC3\xA9mon to be exchanged.";
    std::vector<std::string> buttons;
    int sel = -1;
    if (s.yesNo >= 0) msg = "Will you accept this Pok\xC3\xA9mon?", buttons = { "Yes", "No" }, sel = s.yesNo;
    else if (s.menu && s.receiving) buttons = { "Receive", "Close" }, sel = s.cursor - 6;
    else if (s.menu) buttons = { "Summary", "Exchange", "Close" }, sel = s.cursor - 4;
    const float by = cy + ch - 36 - 60, my0 = buttons.empty() ? by - 4 : by - 16 - 64;
    HudPanel(c, ui, sp, "panels/dialogue_box", cx + 36, my0, cw - 72, 64);
    HudText(c, ui, f, cx + 66, my0 + 16, 32, msg, Ink);
    for (size_t k = 0; k < buttons.size(); k++)
    {
        const float bw = (cw - 72 - 16 * (buttons.size() - 1)) / buttons.size(), bx = cx + 36 + k * (bw + 16);
        const bool on = (int)k == sel;
        HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", bx, by, bw, 60);
        if (on) HudSprite(c, ui, sp, "cursor/cursor_small", bx + 12, by + 14, 30, 32);
        HudText(c, ui, f, bx + bw / 2, by + 14, 32, buttons[k], Ink, 1);
    }
    DrawHintBar(c, ui, sp, f, hints);
}

// The Battle Castle's screen of your own Pokemon (overlay 83, Platinum's
// BattleCastleSelfApp): your Pokemon with their HP and items, your Castle Points and
// ranks, the game's message and its menus (Heal / Rental / Cancel and those under them).
// The grid of items to rent is left to the game.
constexpr uint32_t kCastleInit = 0x0223DD60, kCastleExec = 0x0223DE60, kCastleExit = 0x0223DFBC;

struct Castle
{
    int count = 0;
    PartyMon mons[6];
    int cursor = 0, exitSlot = 0; // a Pokemon, or Exit (exitSlot)
    int shown = 0;                // the Pokemon the card shows
    int cp = 0, ranks[3] = {};    // Castle Points; the healing, rental and information ranks (1-3)
    std::string message;
    std::vector<std::string> menu; // the menu's entries, when one is up
    int sel = 0;
    // The Berries to rent, when their grid is up: the page's (six at most), the cursor on
    // them (6: Return), the page and how many pages, and the "Is that OK?" cursor.
    bool grid = false;
    std::vector<int> items, costs;
    int gridCursor = 0, page = 0, pages = 1, yesNo = -1;
};

// The Berries the Castle rents, in its order, and their prices in CP (as Platinum's
// sBerriesForRent; the first eight at the lower ranks).
constexpr int kCastleBerries[32] = { 149, 150, 151, 152, 153, 156, 157, 158, 201, 202, 203, 204, 205, 206, 207, 184,
    185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200 };

// "RESTORE HP" -> "Restore HP".
inline std::string MenuCase(const std::string& s)
{
    std::string out = HudParty::TitleCase(s);
    for (const char* w : { "Hp", "Pp", "Cp" })
        for (size_t at; (at = out.find(w)) != std::string::npos && (at + 2 == out.size() || out[at + 2] == ' ');)
            out[at + 1] = (char)(out[at + 1] - 'a' + 'A');
    return out;
}

// BattleCastleSelfApp: the BattleFrontier save +0x04 (its stats, u16 each: Castle Points
// of single and double challenges at +0x90 / +0xA0, the three ranks after them at +0x96 /
// +0xA6), challenge type +0x09, the cursor +0x0D, which of its windows are up +0x0E (bit
// 0 a list, 3 the grid of items, 4 a menu), the slots +0x14 (Exit +0x15), the message
// +0x28 (a String), the menu +0x840 (its entries' StringList +0x10, how many +0x04, the
// cursor +0x24), the Pokemon (a Party) +0x7A4. Shown while it waits for the player (its
// state 1), not in the link challenges.
inline bool ReadCastle(const Ram& ram, uint32_t& cache, Castle& s)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, kCastleInit, kCastleExec, kCastleExit, [&](uint32_t m) {
        return ram.Ok(ram.R32(m + 0x1C), 0x850);
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2 || ram.R32(mgr + 0x14) != 1) return false;
    const uint32_t d = ram.R32(mgr + 0x1C);
    const int type = (int)ram.R8(d + 0x09), flags = (int)ram.R8(d + 0x0E);
    if (type > 1 || (flags & 0x01)) return false;
    if (!HudParty::ReadParty(ram, ram.R32(d + 0x7A4), s.count, s.mons) || s.count != (int)ram.R8(d + 0x14)) return false;
    s.exitSlot = (int)ram.R8(d + 0x15);
    s.cursor = (int)ram.R8(d + 0x0D);
    if (s.cursor >= s.count && s.cursor != s.exitSlot) return false;
    static int last = 0;
    if (s.cursor < s.count) last = s.cursor;
    s.shown = std::min(last, s.count - 1);
    const uint32_t fr = ram.R32(d + 0x04);
    if (!ram.Ok(fr, 0xB0)) return false;
    s.cp = (int)ram.R16(fr + 0x90 + 0x10 * type);
    for (int k = 0; k < 3; k++) s.ranks[k] = (int)ram.R16(fr + 0x96 + 0x10 * type + 2 * k);
    if (s.cp > 9999 || s.ranks[0] < 1 || s.ranks[0] > 3 || s.ranks[1] < 1 || s.ranks[1] > 3 || s.ranks[2] < 1 || s.ranks[2] > 3) return false;
    // The menu entry last chosen from (the one under its cursor when it closed).
    static std::string lastPick;
    s.grid = (flags & 0x08) != 0;
    s.yesNo = -1;
    s.menu.clear();
    if (s.grid)
    {
        // The grid (+0x844, a GridInputHandler made with the table at 0x022485C8; its place
        // +0x0D), how many +0x861, the page +0x862; "Is that OK?" up +0x850, its YesNoPrompt
        // +0x84C (cursor +0x75). Only the Berries' grid is drawn: the one opened from the
        // menu's "Berries" (the items' prices are not known here).
        const uint32_t g = ram.R32(d + 0x844);
        const int total = (int)ram.R8(d + 0x861);
        s.page = (int)ram.R8(d + 0x862);
        if (!ram.Ptr(g) || ram.R32(g) != 0x022485C8 || lastPick != "Berries" || total < 1 || total > 32 || s.page * 6 >= total) return false;
        s.pages = (total + 5) / 6;
        s.items.clear();
        s.costs.clear();
        for (int k = s.page * 6; k < std::min(total, s.page * 6 + 6); k++)
        {
            s.items.push_back(kCastleBerries[k]);
            s.costs.push_back(k < 6 ? 2 : 5);
        }
        s.gridCursor = (int)ram.R8(g + 0x0D);
        if (s.gridCursor > 6 || (s.gridCursor < 6 && s.gridCursor >= (int)s.items.size())) return false;
        if (ram.R32(d + 0x850) == 1)
        {
            const uint32_t yn = ram.R32(d + 0x84C);
            if (!ram.Ptr(yn) || ram.R8(yn + 0x75) > 1 || s.gridCursor >= 6) return false;
            s.yesNo = (int)ram.R8(yn + 0x75);
            if (!HudParty::ReadString(ram, ram.R32(d + 0x28), s.message)) return false;
        }
        else
            s.message = s.gridCursor < 6 ? Party_Text(221, s.items[s.gridCursor]) : "";
        return true;
    }
    if (!HudParty::ReadString(ram, ram.R32(d + 0x28), s.message)) return false;
    if (flags & 0x10)
    {
        const uint32_t m = ram.R32(d + 0x840), list = ram.Ptr(m) ? ram.R32(m + 0x10) : 0;
        const int n = (int)ram.R8(m + 4);
        s.sel = (int)ram.R8(m + 0x24);
        if (!ram.Ptr(list) || n < 1 || n > 6 || s.sel >= n) return false;
        for (int k = 0; k < n; k++)
        {
            std::string t;
            if (!HudParty::ReadString(ram, ram.R32(list + 8 * k), t) || t.empty()) return false;
            s.menu.push_back(MenuCase(t));
        }
        lastPick = s.menu[s.sel];
    }
    return true;
}

inline void DrawCastle(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Castle& s, const HudParty::MonIconFn& icon,
    const std::function<const Image*(int)>& itemIcon, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    using HudParty::FillRound;
    DrawBackdrop(c, sp, "backgrounds/bg_coral", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;
    HudText(c, ui, f, x0 + 60, 56, 48, "BATTLE CASTLE", Ink, 0, true);
    const std::string cp = std::to_string(s.cp) + " CP";
    const float pw = HudTextWidth(ui, f, 32, cp, true) + HudTextWidth(ui, f, 32, "Castle Points") + 80;
    HudPanel(c, ui, sp, "panels/button_white", x0 + 1860 - pw, 54, pw, 56);
    HudText(c, ui, f, x0 + 1860 - pw + 28, 66, 32, "Castle Points", Muted);
    HudText(c, ui, f, x0 + 1860 - 28, 66, 32, cp, Ink, 2, true);

    // Your Pokemon, one row each, then Exit and the ranks.
    const float lx = x0 + 60, lw = 760;
    for (int i = 0; i < s.count; i++)
    {
        const PartyMon& m = s.mons[i];
        const float y = 140 + i * 176, h = 160;
        const bool here = s.cursor == i || ((!s.menu.empty() || s.grid) && s.shown == i);
        HudPanel(c, ui, sp, here ? "panels/party_card_selected" : "panels/card", lx, y, lw, h);
        HudSprite(c, ui, sp, here ? "people/chip_ring_orange" : "people/chip_ring_green", lx + 22, y + 15, 130, 130);
        HudPixelArt(c, ui, icon(m), lx + 23, y + 16, 128, 128);
        HudText(c, ui, f, lx + 176, y + 24, 32, m.name, Ink);
        HudParty::Gender(c, ui, lx + 176 + HudTextWidth(ui, f, 32, m.name) + 10, y + 28, m.gender, 26);
        HudText(c, ui, f, lx + lw - 30, y + 24, 32, "Lv " + std::to_string(m.level), Muted, 2);
        HudText(c, ui, f, lx + 176, y + 70, 32, "HP", Muted);
        HudParty::Bar(c, ui, sp, lx + 230, y + 75, lw - 230 - 190, (float)m.hp / std::max(1, m.stats[0]), HudParty::HpFill(m.hp, m.stats[0]));
        HudText(c, ui, f, lx + lw - 30, y + 70, 32, std::to_string(m.hp) + "/" + std::to_string(m.stats[0]), Ink, 2);
        HudSprite(c, ui, sp, "icons/icon_bag", lx + 176, y + 112, 32, 32);
        HudText(c, ui, f, lx + 220, y + 112, 32, m.item ? Party_Text(222, m.item) : "No item", m.item ? Ink : Muted);
    }
    const float ey = 140 + s.count * 176;
    const bool exitOn = s.cursor == s.exitSlot;
    HudPanel(c, ui, sp, exitOn ? "panels/button_yellow" : "panels/button_white", lx + lw - 260, ey, 260, 60);
    if (exitOn) HudSprite(c, ui, sp, "cursor/cursor_small", lx + lw - 250, ey + 14, 30, 32);
    HudText(c, ui, f, lx + lw - 130, ey + 14, 32, "Exit", Ink, 1);
    static const char* rankNames[3] = { "Healing", "Rental", "Info" };
    float rx = lx;
    for (int k = 0; k < 3; k++)
        rx += HudParty::Pill(c, ui, f, rx, ey + 80, 46, std::string(rankNames[k]) + " rank " + std::to_string(s.ranks[k]), 0xFFC0606A) + 12;

    // The card: the Pokemon under the cursor, as the game's top screen shows it; or the
    // Berries to rent, two columns of three a page.
    const PartyMon& m = s.mons[s.shown];
    const float cx = x0 + 860, cw = 1000, cy = 140, ch = 560;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    std::vector<std::string> menu = s.menu;
    int sel = s.sel;
    if (s.grid)
    {
        FillRound(c, ui, cx + 30, cy + 26, cw - 60, 52, 18, 0xFFC0606A);
        HudText(c, ui, f, cx + 54, cy + 36, 32, "Rent a Berry for " + m.name, White);
        const float tw = (cw - 60 - 20) / 2, th = 110;
        for (size_t k = 0; k < s.items.size(); k++)
        {
            const bool on = s.gridCursor == (int)k;
            const float tx = cx + 30 + (k % 2) * (tw + 20), ty = cy + 100 + (k / 2) * (th + 14);
            HudPanel(c, ui, sp, on ? "panels/row_selected" : "panels/row", tx, ty, tw, th);
            if (on) HudSprite(c, ui, sp, "cursor/cursor_small", tx + 8, ty + th / 2 - 16, 30, 32);
            HudPixelArt(c, ui, itemIcon(s.items[k]), tx + 44, ty + 15, 80, 80);
            HudText(c, ui, f, tx + 140, ty + 20, 32, Party_Text(222, s.items[k]), Ink);
            HudText(c, ui, f, tx + 140, ty + 62, 32, std::to_string(s.costs[k]) + " CP", s.costs[k] > s.cp ? Danger : Muted);
        }
        const float py = cy + ch - 30 - 60;
        char page[32];
        snprintf(page, sizeof page, "Page %d / %d", s.page + 1, s.pages);
        for (int k = 0; k < 2; k++)
        {
            const bool more = s.pages > 1;
            const float ax = cx + 30 + k * 300;
            HudPanel(c, ui, sp, "panels/button_white", ax, py, 80, 60, more ? 0xFFFFFFFF : 0x60FFFFFF);
            HudSprite(c, ui, sp, k ? "cursor/value_arrow_right" : "cursor/value_arrow_left", ax + 24, py + 10, 32, 40, more ? 0xFFFFFFFF : 0x60FFFFFF);
        }
        HudText(c, ui, f, cx + 30 + 190, py + 14, 32, page, Ink, 1);
        const bool back = s.gridCursor == 6;
        HudPanel(c, ui, sp, back ? "panels/button_yellow" : "panels/button_white", cx + cw - 30 - 260, py, 260, 60);
        if (back) HudSprite(c, ui, sp, "cursor/cursor_small", cx + cw - 30 - 250, py + 14, 30, 32);
        HudText(c, ui, f, cx + cw - 30 - 130, py + 14, 32, "Return", Ink, 1);
        if (s.yesNo >= 0) menu = { "Yes", "No" }, sel = s.yesNo;
    }
    else
    {
        HudSprite(c, ui, sp, "people/chip_ring_orange", cx + 30, cy + 30, 180, 180);
        HudPixelArt(c, ui, icon(m), cx + 56, cy + 56, 128, 128);
        HudText(c, ui, f, cx + 240, cy + 40, 48, m.name, Ink, 0, true);
        {
            float px = cx + 240;
            for (int t = 0; t < 2; t++)
                if (m.types[t] >= 0 && (t == 0 || m.types[1] != m.types[0]))
                    px += HudParty::Pill(c, ui, f, px, cy + 110, 46, HudParty::TypeName(m.types[t]), HudParty::TypeColour(m.types[t])) + 12;
        }
        const std::pair<std::string, std::string> rows[2] = { { "Ability", Party_Text(720, m.ability) }, { "Nature", Party_Text(34, m.nature) } };
        for (int i = 0; i < 2; i++)
        {
            HudPanel(c, ui, sp, "panels/row", cx + 240, cy + 170 + 52 * i, cw - 270, 44);
            HudText(c, ui, f, cx + 260, cy + 176 + 52 * i, 32, rows[i].first, Muted);
            HudText(c, ui, f, cx + cw - 50, cy + 176 + 52 * i, 32, rows[i].second, Ink, 2);
        }
        static const char* statNames[5] = { "Attack", "Defense", "Sp. Atk", "Sp. Def", "Speed" };
        static const int statIdx[5] = { 1, 2, 4, 5, 3 };
        for (int i = 0; i < 5; i++)
        {
            const float sx = cx + 30 + (i % 3) * 320, sy = cy + 290 + (i / 3) * 52;
            HudPanel(c, ui, sp, "panels/row", sx, sy, 300, 44);
            HudText(c, ui, f, sx + 18, sy + 6, 32, statNames[i], Muted);
            HudText(c, ui, f, sx + 282, sy + 6, 32, std::to_string(m.stats[statIdx[i]]), Ink, 2);
        }
        const float mw = (cw - 60 - 16) / 2;
        for (int k = 0; k < 4; k++)
        {
            const float mx = cx + 30 + (k % 2) * (mw + 16), my = cy + 410 + (k / 2) * 72;
            if (!m.moves[k])
            {
                FillRound(c, ui, mx, my, mw, 60, 30, 0xFFE9EDF2);
                continue;
            }
            FillRound(c, ui, mx, my, mw, 60, 30, HudParty::TypeColour(m.moveType[k]));
            HudText(c, ui, f, mx + 26, my + 14, 32, Party_Text(750, m.moves[k]), White);
            HudText(c, ui, f, mx + mw - 24, my + 14, 32, std::to_string(m.pp[k]) + "/" + std::to_string(m.ppMax[k]), White, 2);
        }
    }

    // The game's message, and its menu beside it.
    const float by = cy + ch + 30, bw = cw - (menu.empty() ? 0 : 340);
    std::vector<std::string> lines = HudWrap(ui, f, 32, s.message, bw - 60);
    lines.resize(std::min(lines.size(), (size_t)4));
    HudPanel(c, ui, sp, "panels/dialogue_box", cx, by, bw, 40 + 50.0f * std::max<size_t>(2, lines.size()));
    for (size_t i = 0; i < lines.size(); i++) HudText(c, ui, f, cx + 30, by + 22 + 50.0f * i, 32, lines[i], Ink);
    for (size_t k = 0; k < menu.size(); k++)
    {
        const bool on = (int)k == sel;
        const float x = cx + cw - 320, y = by + 66.0f * k;
        HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", x, y, 320, 56);
        if (on) HudSprite(c, ui, sp, "cursor/cursor_small", x + 12, y + 12, 30, 32);
        HudText(c, ui, f, x + 160, y + 12, 32, menu[k], Ink, 1);
    }
    DrawHintBar(c, ui, sp, f, hints);
}

// Choosing the Pokemon to enter in a Frontier challenge: the party menu (PartyMenu) in its
// Frontier context (23), with each Pokemon's place in the order, Confirm and Cancel.
struct Entry
{
    int count = 0;
    PartyMon mons[6];
    int order[6] = {};             // each Pokemon's place (1-3), 0 not entered
    int most = 3, entered = 0;
    int cursor = 0;                // 0-5 a Pokemon, 6 Confirm, 7 Cancel
    std::vector<std::string> menu; // Enter / No Entry / Summary, when up
    int sel = 0;
};

// PartyMenu (include/party_menu.h): its arguments (+0x654 of its data, as the manager's)
// hold the party +0, the context +0x24, the order chosen +0x30 (a slot + 1 for each place)
// and the most to enter +0x36 (high four bits); the cursor +0xC65 and the menu's cursor
// +0x824 (HudParty::ReadPartyScreen). Drawn while choosing (step 1) and in the menu (2).
inline bool ReadEntry(const Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, Entry& s)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t pm = ram.R32(m + 0x1C);
        return ram.Ok(pm, 0xCA8) && ram.R32(pm + 0x654) == ram.R32(m + 0x18);
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2) return false;
    const int step = (int)ram.R32(mgr + 0x14);
    const uint32_t pm = ram.R32(mgr + 0x1C), args = ram.R32(mgr + 0x18);
    if ((step != 1 && step != 2) || ram.R8(args + 0x24) != 23) return false;
    if (!HudParty::ReadParty(ram, ram.R32(args), s.count, s.mons)) return false;
    s.most = (int)(ram.R8(args + 0x36) >> 4);
    if (s.most < 1 || s.most > 6) return false;
    s.entered = 0;
    for (int i = 0; i < 6; i++) s.order[i] = 0;
    for (int k = 0; k < s.most; k++)
    {
        const int slot = (int)ram.R8(args + 0x30 + k);
        if (!slot) break;
        if (slot > s.count || s.order[slot - 1]) return false;
        s.order[slot - 1] = k + 1;
        s.entered++;
    }
    s.cursor = (int)ram.R8(pm + 0xC65);
    if (s.cursor > 7 || (s.cursor < 6 && s.cursor >= s.count)) return false;
    s.menu.clear();
    if (step == 2)
    {
        const uint32_t cur = ram.R32(pm + 0x824), list = ram.Ptr(cur) ? ram.R32(cur + 4) : 0;
        const int n = (int)ram.R8(cur + 2);
        s.sel = (int)ram.R8(cur + 1);
        if (!ram.Ptr(list) || n < 1 || n > 6 || s.sel >= n || s.cursor >= 6) return false;
        for (int i = 0; i < n; i++)
        {
            std::string t;
            if (!HudParty::ReadString(ram, ram.R32(list + 8 * i), t)) return false;
            s.menu.push_back(MenuCase(t));
        }
    }
    return true;
}

inline void DrawEntry(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Entry& s, const HudParty::MonIconFn& icon,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    using HudParty::FillRound;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;
    HudText(c, ui, f, x0 + 60, 56, 48, "ENTER POK\xC3\xA9MON", Ink, 0, true);
    static const char* places[6] = { "First", "Second", "Third", "Fourth", "Fifth", "Sixth" };

    // The party, two columns of three, each with its place in the order.
    const float cw = 560, ch = 220;
    for (int i = 0; i < 6; i++)
    {
        const float x = x0 + 60 + (i % 2) * (cw + 20), y = 140 + (i / 2) * (ch + 20);
        if (i >= s.count)
        {
            HudSprite(c, ui, sp, "panels/slot_grid_empty", x, y, cw, ch);
            continue;
        }
        const PartyMon& m = s.mons[i];
        const bool here = s.cursor == i;
        HudPanel(c, ui, sp, here ? "panels/party_card_selected" : "panels/card", x, y, cw, ch);
        HudSprite(c, ui, sp, here ? "people/chip_ring_orange" : "people/chip_ring_green", x + 26, y + 35, 150, 150);
        HudPixelArt(c, ui, icon(m), x + 37, y + 46, 128, 128);
        HudText(c, ui, f, x + 196, y + 36, 48, m.egg ? "Egg" : m.name, Ink);
        HudText(c, ui, f, x + 196, y + 108, 32, "Lv " + std::to_string(m.level), Muted);
        if (m.item)
        {
            HudSprite(c, ui, sp, "icons/icon_bag", x + 300, y + 104, 36, 36);
            HudText(c, ui, f, x + 344, y + 108, 32, Party_Text(222, m.item), Muted);
        }
        if (s.order[i]) HudParty::Pill(c, ui, f, x + 196, y + 154, 46, places[s.order[i] - 1], 0xFFF08A24);
        else HudText(c, ui, f, x + 196, y + 160, 32, "Not entered", Muted);
    }

    // The card on the right: what to do, how many are in, the menu or Confirm and Cancel.
    const float rx = x0 + 60 + 2 * cw + 60, rw = x0 + 1860 - rx;
    HudPanel(c, ui, sp, "panels/card", rx, 140, rw, 700);
    FillRound(c, ui, rx + 22, 162, rw - 44, 48, 16, 0xFF2EC4C6);
    HudText(c, ui, f, rx + 42, 170, 32, "Entered " + std::to_string(s.entered) + " / " + std::to_string(s.most), White);
    const std::string say = s.menu.empty() ? "Choose Pok\xC3\xA9mon and confirm." : "Do what with " + s.mons[s.cursor].name + "?";
    std::vector<std::string> lines = HudWrap(ui, f, 32, say, rw - 100);
    for (size_t i = 0; i < lines.size() && i < 3; i++) HudText(c, ui, f, rx + 42, 240 + 50.0f * i, 32, lines[i], Ink);
    const std::vector<std::string> buttons = s.menu.empty() ? std::vector<std::string>{ "Confirm", "Cancel" } : s.menu;
    for (size_t k = 0; k < buttons.size(); k++)
    {
        const bool on = s.menu.empty() ? s.cursor == 6 + (int)k : (int)k == s.sel;
        const float by = 840 - 30 - 62.0f * (buttons.size() - k);
        HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", rx + 22, by, rw - 44, 50);
        if (on) HudSprite(c, ui, sp, "cursor/cursor", rx + 34, by + 5, 36, 40);
        HudText(c, ui, f, rx + rw / 2, by + 9, 32, buttons[k], Ink, 1);
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
