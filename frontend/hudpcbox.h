#pragma once
// The PC's Pokemon storage redrawn in the HUD style (online-HUD design 23-ss-pcbox): the
// party in a column, the box shown as a grid of 30 cells and the Pokemon under the cursor
// (or the one being moved) on a card. The game keeps taking the input; every frame the
// redrawn screen mirrors the storage application (overlay 14, still assembly in the
// decompilation) as it holds itself in memory. On any screen or step not mirrored here (the
// tabs' own menu, messages and yes/no questions, the item mode, wallpapers, naming a box) or when
// anything does not check out, nothing is drawn and the game's own picture shows.
#include "hudparty.h"

namespace HudPcBox
{
using HudParty::Ram;

// The application's data (0x38 bytes): its arguments +0 (the save, the menu state, then
// the mode at +8: 0 deposit, 1 withdraw, 2 move Pokemon, 3 move items), the PC storage +4,
// the party +8, the box shown +0x1F, the place last chosen +0x21 (a cell 0-29 or a party
// slot 30-35; 0xFF none), whether a Pokemon is being picked up from its menu +0x2A and its
// cell +0x2B, the work data +0x34. The work data holds the d-pad's GridInputHandler at
// +0x2C (its d-pad table +4, the cursor +0xD) and whether a Pokemon is held at +0x44B.
//
// The d-pad tables of the screens mirrored (the overlay's own data, at fixed addresses),
// and what the cursor's numbers stand for in each:
constexpr uint32_t kGridMove = 0x021F90A0;     // cells 0-29, 30 the box's name, 33 party, 34 move, 35 return, 36-40 menu, 41 its exit
constexpr uint32_t kGridMoveParty = 0x021F91F0; // cells 0-29, 30-35 party, 36 exit, 37-42 the boxes' tabs, 43 on their menu
constexpr uint32_t kGridParty = 0x021F8AA0;    // the "Party Pokemon" panel: party 0-5, 6 switch, 7 exit, 8-13 menu
constexpr uint32_t kGridWithdraw = 0x021F8F68; // cells 0-29, 30 the box's name, 33 return, 34-37 menu, 38 its exit
constexpr uint32_t kGridDeposit = 0x021F8A40;  // party 0-5, 6 return, 7-10 menu, 11 its exit

struct Screen
{
    int step = 0, mode = 0;
    int next = 0;                    // the step that follows an animation (step 5), +0x30
    uint32_t grid = 0;
    int cursor = 0;
    int box = 0;
    int chosen = -1;                 // the place last chosen (+0x21): the menu's Pokemon while it is open
    int held = -1;                   // where the Pokemon being moved is from (0-29 a cell, 30-35 the party), or -1
    std::string boxName;
    int count = 0;                   // Pokemon in the box
    std::string headName;            // the box the header names: the one shown, or the tab pointed at
    int headCount = 0;
    bool has[30] = {};
    PartyMon mons[30];
    int partyCount = 0;
    PartyMon party[6];
};

// A slot of the storage: false when it cannot be read; `has` false when it is empty (an
// empty slot is all zeros: no personality, no checksum).
inline bool ReadSlot(const Ram& ram, uint32_t a, bool& has, PartyMon& m)
{
    has = ram.R32(a) != 0 || ram.R16(a + 6) != 0;
    return !has || Party_ReadMon(a, m, true);
}

inline bool Read(const Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, Screen& s)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t d = ram.R32(m + 0x1C), args = ram.R32(m + 0x18);
        return ram.Ok(d, 0x38) && ram.R32(d) == args && ram.Ptr(ram.R32(d + 4)) && ram.R32(ram.R32(d + 8)) == 6;
    });
    if (!mgr || ram.R32(mgr + 0x10) != 2) return false;
    const uint32_t d = ram.R32(mgr + 0x1C), args = ram.R32(d), storage = ram.R32(d + 4), work = ram.R32(d + 0x34);
    if (!ram.Ok(work, 0x88E0) || !ram.Ok(storage, 0x122EC)) return false;
    const uint32_t grid = ram.R32(work + 0x2C);
    if (!ram.Ok(grid, 0x20)) return false;
    s.step = (int)ram.R32(mgr + 0x14);
    s.next = (int)ram.R32(d + 0x30);
    s.mode = (int)ram.R32(args + 8);
    s.grid = ram.R32(grid + 4);
    s.cursor = (int)ram.R8(grid + 0xD);
    s.box = (int)ram.R8(d + 0x1F);
    const int chosen = (int)ram.R8(d + 0x21);
    s.chosen = chosen < 36 ? chosen : -1;
    // Held: picked up (the place chosen last), or being lifted from the menu's Move.
    s.held = ram.R8(work + 0x44B) == 1 ? s.chosen : ram.R8(d + 0x2A) == 1 && ram.R8(d + 0x2B) < 30 ? (int)ram.R8(d + 0x2B) : -1;
    const bool known = (s.mode == 2 && (s.grid == kGridMove || s.grid == kGridMoveParty || s.grid == kGridParty)) ||
        (s.mode == 1 && s.grid == kGridWithdraw) ||
        (s.mode == 0 && s.grid == kGridDeposit);
    if (!known || s.box >= 18) return false;
    // The box: its name (20 characters at 0x12008, after the 18 boxes) and its Pokemon.
    uint16_t name[20];
    for (int i = 0; i < 20; i++) name[i] = (uint16_t)ram.R16(storage + 0x12008 + 40 * s.box + 2 * i);
    s.boxName = HudParty::TitleCase(NdsText(name, 20));
    s.count = 0;
    for (int i = 0; i < 30; i++)
    {
        if (!ReadSlot(ram, storage + 0x1000 * s.box + 136 * i, s.has[i], s.mons[i])) return false;
        s.count += s.has[i];
    }
    // With the boxes' tabs out the header names the box whose tab was pointed at last
    // (+0x25), which need not be the one shown.
    s.headName = s.boxName, s.headCount = s.count;
    if (s.grid == kGridMoveParty)
    {
        const int b = (int)ram.R8(d + 0x25);
        if (b >= 18) return false;
        for (int i = 0; i < 20; i++) name[i] = (uint16_t)ram.R16(storage + 0x12008 + 40 * b + 2 * i);
        s.headName = HudParty::TitleCase(NdsText(name, 20));
        s.headCount = 0;
        for (int i = 0; i < 30; i++)
        {
            const uint32_t a = storage + 0x1000 * b + 136 * i;
            s.headCount += ram.R32(a) != 0 || ram.R16(a + 6) != 0;
        }
    }
    if (!HudParty::ReadParty(ram, ram.R32(d + 8), s.partyCount, s.party)) return false;
    if (s.held >= 0 && !(s.held < 30 ? s.has[s.held] : s.held - 30 < s.partyCount)) return false;
    return true;
}

// Whether the redrawn screen is shown at this step: while the cursor takes the input (12
// moving Pokemon, 41 the same with the party out, 115 holding one, 36 the "Party Pokemon"
// panel, 81 withdrawing, 91 depositing), and through the short animations from there (5 the
// cursor, a menu or a panel moving, 8 and 140-180 a button pressed) when it was shown the
// frame before. Messages, questions (7), the tabs' menu (cursor 43 on) and the fades in and
// out are the game's own picture.
inline bool OnTabs(const Screen& s) { return s.grid == kGridMoveParty && s.cursor >= 37 && s.cursor <= 42; }
inline bool PastTabs(const Screen& s) { return s.grid == kGridMoveParty && s.cursor > 42; }

// `sinceMsg` is the frames since a message or question (step 6 or 7) was last up: once one
// closes (step 14, or 5 as the window clears and the menu slides back) the game's screen is
// back after the window's 2 frames of clearing, and the redraw returns with it.
inline bool Shown(const Screen& s, bool before, uint32_t sinceMsg = ~0u)
{
    // Step 5 going on to 53-56 (the box's menu, A on its name) or 92-97 (Deposit's "Deposit
    // where?"): the game slides in a screen of its own, shown from its first frame rather
    // than a still redraw.
    if (PastTabs(s) || (s.step == 5 && ((s.next >= 53 && s.next <= 56) || (s.next >= 92 && s.next <= 97)))) return false;
    if (s.step == 12 || s.step == 41 || s.step == 115 || s.step == 36 || s.step == 81 || s.step == 91) return true;
    if (sinceMsg >= 3 && sinceMsg <= 40 && (s.step == 14 || s.step == 5)) return true;
    return before && (s.step == 5 || s.step == 8 || (s.step >= 140 && s.step <= 180));
}

// The menu of a Pokemon, as each screen has it, and where it starts on the cursor.
inline std::vector<std::string> MenuOf(const Screen& s, int& first)
{
    if (s.grid == kGridMove || s.grid == kGridParty)
    {
        first = s.grid == kGridMove ? 36 : 8;
        return { "Move", "Summary", "Held items", "Marking", "Release", "Exit" };
    }
    if (s.grid == kGridWithdraw)
    {
        first = 34;
        return { "Withdraw", "Summary", "Marking", "Release", "Exit" };
    }
    if (s.grid == kGridDeposit)
    {
        first = 7;
        return { "Deposit", "Summary", "Marking", "Release", "Exit" };
    }
    first = 0;
    return {};
}

// The buttons beside the box, as each screen (its d-pad table) has them: their cursor
// numbers and names.
inline std::vector<std::pair<int, std::string>> ButtonsOf(uint32_t grid)
{
    if (grid == kGridMove) return { { 33, "Party Pok\xC3\xA9mon" }, { 34, "Move" }, { 35, "Return" } };
    if (grid == kGridMoveParty) return { { 36, "Exit" } };
    if (grid == kGridParty) return { { 6, "Switch" }, { 7, "Exit" } };
    if (grid == kGridWithdraw) return { { 33, "Return" } };
    return { { 6, "Return" } };
}

// The panels the game slides in or out when a button changes the screen: the step it takes
// for the button (149 "Party Pokemon", 150 "Move", 178 the menu's Move, 157 Move or Switch
// on the party panel, 159 leaving the party panel, 161 leaving the party moving), the screen
// it goes to (0: back where the party moving came from) and the slide's frames, as timed.
// The slide's position is not in main RAM, so the frames since it began stand for it.
inline bool SlideOf(int step, uint32_t& to, int& frames)
{
    switch (step)
    {
    case 149: to = kGridParty, frames = 30; return true;
    case 150: to = kGridMoveParty, frames = 30; return true;
    case 178: to = kGridMoveParty, frames = 38; return true;
    case 157: to = kGridMoveParty, frames = 28; return true;
    case 159: to = kGridMove, frames = 20; return true;
    case 161: to = 0, frames = 24; return true; // 24 to 31: the shortest, so it never jumps at the end
    }
    return false;
}

// Positions on the design's 1920 x 1080 (x from the left of the centred design).
constexpr float kPartyX = 60, kPartyY = 40, kBoxX = 400, kBoxY = 40, kCardX = 1300, kCardY = 40;
inline float CellX(int i) { return kBoxX + 40 + 132 * (i % 6); }
inline float CellY(int i) { return kBoxY + 102 + 132 * (i / 6); }
inline float SlotX(int i) { return kPartyX + 28 + 128 * (i % 2); }
inline float SlotY(int i) { return kPartyY + 82 + 128 * (i / 2); }

// Draws with `draw` only inside x0..x1 (design pixels): what it draws beside that, on the
// rows y0..y1 it keeps to, is put back as it was.
template <class F> inline void Clipped(Canvas& c, const Ui& ui, float x0, float y0, float x1, float y1, F draw)
{
    const int X0 = std::clamp((int)ui(x0), 0, c.w), X1 = std::clamp((int)ui(x1), X0, c.w);
    const int Y0 = std::clamp((int)ui(y0), 0, c.h), Y1 = std::clamp((int)ui(y1), Y0, c.h);
    const std::vector<uint32_t> keep(c.px.begin() + (size_t)Y0 * c.w, c.px.begin() + (size_t)Y1 * c.w);
    draw();
    for (int yy = Y0; yy < Y1; yy++)
    {
        uint32_t* row = &c.px[(size_t)yy * c.w];
        const uint32_t* k = &keep[(size_t)(yy - Y0) * c.w];
        std::copy(k, k + X0, row);
        std::copy(k + X1, k + c.w, row + X1);
    }
}

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Screen& s, const HudOnline::IconFn& icon,
    const std::string& myName, uint32_t myId, const std::vector<HudHint>& hints, float slide = -1, uint32_t slideTo = 0,
    int glideFrom = -1, float glide = -1, const Screen* from = nullptr, float scroll = 0)
{
    using namespace HudColour;
    using HudParty::FillRound;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float x0 = ui.W / ui.s / 2 - 960;
    constexpr uint32_t Teal = 0xFF2EC4C6, Gold = 0xFFF5C518;
    int menuFirst = 0;
    const std::vector<std::string> menu = MenuOf(s, menuFirst);
    // The menu closes as soon as Withdraw is chosen (step 5 going on to 83-86: the game's
    // menu sliding away while the Pokemon goes to the party), though the cursor stays on it.
    const bool menuOpen = !menu.empty() && s.cursor >= menuFirst && s.cursor < menuFirst + (int)menu.size() && s.chosen >= 0 &&
        !(s.step == 5 && s.next >= 83 && s.next <= 86);
    // Where the cursor is, as a cell (0-29) or a party slot (30-35), or -1.
    const bool partyGrid = s.grid == kGridDeposit || s.grid == kGridParty;
    int at = -1;
    if (partyGrid ? s.cursor < 6 : s.cursor < 30) at = partyGrid ? 30 + s.cursor : s.cursor;
    else if (s.grid == kGridMoveParty && s.cursor < 36) at = s.cursor;
    // The Pokemon being held follows the cursor (over the header on the tabs); while it is
    // lifted from the menu it is still over its own cell.
    const bool tabs = OnTabs(s);
    const int heldAt = s.held < 0 || tabs ? -1 : at >= 0 ? at : s.held;
    auto monAt = [&](int place) -> const PartyMon* {
        if (place >= 0 && place < 30) return s.has[place] ? &s.mons[place] : nullptr;
        if (place >= 30 && place - 30 < s.partyCount) return &s.party[place - 30];
        return nullptr;
    };
    const bool gliding = glide >= 0 && glideFrom >= 0 && glideFrom < 30 && s.has[glideFrom] && s.partyCount < 6;
    const PartyMon* shown = s.held >= 0 ? monAt(s.held) : menuOpen ? monAt(s.chosen) : monAt(at);

    // A Pokemon's icon in a cell `size` wide, or an egg's word.
    auto iconIn = [&](const PartyMon& m, float x, float y, float size) {
        if (m.egg) HudText(c, ui, f, x + size / 2, y + size / 2 - 16, 32, "Egg", Muted, 1);
        else HudPixelArt(c, ui, icon(m.species), x + size / 2 - 48, y + size / 2 - 48, 96, 96);
    };

    // The party.
    {
        const float x = x0 + kPartyX, y = kPartyY, w = 300, h = 560;
        HudPanel(c, ui, sp, "panels/card", x, y, w, h);
        FillRound(c, ui, x + 24, y + 22, w - 48, 46, 16, Teal);
        HudText(c, ui, f, x + w / 2, y + 29, 32, "PARTY", White, 1);
        for (int i = 0; i < 6; i++)
        {
            const float sx = x0 + SlotX(i), sy = SlotY(i);
            const bool here = at == 30 + i || (menuOpen && s.chosen == 30 + i);
            if (i >= s.partyCount)
            {
                HudPanel(c, ui, sp, here ? "panels/slot_grid_selected" : "panels/slot_grid_empty", sx, sy, 116, 116, here ? 0xFFFFFFFF : 0x60FFFFFF);
                continue;
            }
            HudPanel(c, ui, sp, here ? "panels/slot_grid_selected" : "panels/slot_grid", sx, sy, 116, 116);
            if (s.held != 30 + i) iconIn(s.party[i], sx + 10, sy + 10, 96);
        }
        static const char* modes[3] = { "Deposit", "Withdraw", "Move Pok\xC3\xA9mon" };
        HudText(c, ui, f, x + w / 2, y + h - 54, 32, modes[s.mode], Muted, 1);
    }
    // The buttons under it. While the game slides its panels the buttons of this screen
    // drop away and those of the next rise in their place, in step with it.
    auto ease = [](float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); };
    const bool sliding = slide >= 0;
    auto buttonsAt = [&](uint32_t grid, float drop, bool live) {
        if (drop >= 1) return;
        const auto buttons = ButtonsOf(grid);
        const uint32_t a = (uint32_t)(255 * (1 - drop)) << 24;
        for (size_t k = 0; k < buttons.size(); k++)
        {
            const float bx = x0 + kPartyX, by = kPartyY + 600 + 76 * k + 160 * drop, bw = 300;
            const bool on = live && s.cursor == buttons[k].first;
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", bx, by, bw, 56, a | 0xFFFFFF);
            if (on) HudSprite(c, ui, sp, "cursor/cursor_small", bx + 12, by + 12, 30, 32, a | 0xFFFFFF);
            HudText(c, ui, f, bx + bw / 2 + (on ? 14 : 0), by + 12, 32, buttons[k].second, a | (Ink & 0xFFFFFF), 1);
        }
    };
    if (!sliding) buttonsAt(s.grid, 0, true);
    else
    {
        buttonsAt(s.grid, ease(slide / 0.45f), true);
        buttonsAt(slideTo, 1 - ease((slide - 0.55f) / 0.45f), false);
    }

    // The box: its name between arrows (left and right on the name change the box), its
    // cells and how full it is.
    {
        const float x = x0 + kBoxX, y = kBoxY, w = 860, h = 820;
        HudPanel(c, ui, sp, "panels/frame_aqua", x, y, w, h);
        const bool on = (s.cursor == 30 && (s.grid == kGridMove || s.grid == kGridWithdraw)) || tabs;
        FillRound(c, ui, x + 26, y + 26, w - 52, 60, 18, on ? Gold : Teal);
        HudSprite(c, ui, sp, "cursor/value_arrow_left", x + 120, y + 36, 32, 40, on ? 0xFFFFFFFF : 0x80FFFFFF);
        HudSprite(c, ui, sp, "cursor/value_arrow_right", x + w - 152, y + 36, 32, 40, on ? 0xFFFFFFFF : 0x80FFFFFF);
        auto name = [&](const std::string& n, float dx) { HudText(c, ui, f, x + w / 2 + dx, y + 32, 48, n, on ? Ink : White, 1); };
        auto cells = [&](const Screen& b, float dx, bool live) {
            for (int i = 0; i < 30; i++)
            {
                const float cx = x0 + CellX(i) + dx, cy = CellY(i);
                const bool here = live && (at == i || (menuOpen && s.chosen == i));
                HudPanel(c, ui, sp, here ? "panels/slot_grid_selected" : "panels/slot_grid", cx, cy, 118, 118,
                    here || b.has[i] ? 0xFFFFFFFF : 0xB0FFFFFF);
                if (b.has[i] && !(live && (s.held == i || (gliding && glideFrom == i)))) iconIn(b.mons[i], cx + 11, cy + 11, 96);
            }
        };
        if (!from || scroll == 0)
        {
            name(s.headName, 0);
            cells(s, 0, true);
        }
        else
        {
            // Changing box scrolls the old one out and the new one in, as the game does:
            // `scroll` is how far the new box still is from its place, in boxes (+ from the right).
            constexpr float span = 820;
            const float old = (scroll - (scroll > 0 ? 1 : -1)) * span;
            Clipped(c, ui, x + 160, y + 26, x + w - 160, y + 86, [&] { name(from->headName, old); name(s.headName, scroll * span); });
            Clipped(c, ui, x + 26, CellY(0) - 12, x + w - 26, CellY(24) + 130, [&] { cells(*from, old, false); cells(s, scroll * span, true); });
        }
        HudText(c, ui, f, x + w / 2, y + h - 58, 32, std::to_string(from && std::abs(scroll) > 0.5f ? from->count : s.count) + " / 30", 0xFF2A9DB0, 1);
        // The box steps back (the game greys it) while the party panel is out, fading with
        // the slide.
        float dim = s.grid == kGridParty ? 1.0f : 0.0f;
        if (sliding) dim += ((slideTo == kGridParty ? 1.0f : 0.0f) - dim) * ease(slide);
        if (dim > 0) FillRound(c, ui, x, y, w, h, 36, (uint32_t)(150 * dim) << 24 | 0xE8F6F8);
    }

    // The card: the Pokemon held, the one the menu is for or the one under the cursor.
    {
        const float x = x0 + kCardX, y = kCardY, w = 560;
        const float h = menuOpen ? 960 : 620;
        HudPanel(c, ui, sp, "panels/card", x, y, w, h);
        const float rx = x + w / 2 - 90;
        if (!shown)
        {
            HudSprite(c, ui, sp, "people/chip_empty", rx, y + 26, 180, 180, 0x80FFFFFF);
            const bool onName = (s.cursor == 30 && (s.grid == kGridMove || s.grid == kGridWithdraw)) || tabs;
            HudText(c, ui, f, x + w / 2, y + 240, 32, onName ? s.headName + " \xC2\xB7 " + std::to_string(s.headCount) + " Pok\xC3\xA9mon" : "",
                Muted, 1);
        }
        else
        {
            const PartyMon& m = *shown;
            HudSprite(c, ui, sp, "people/chip_ring_grey", rx, y + 26, 180, 180);
            if (!m.egg) HudPixelArt(c, ui, icon(m.species), rx + 10, y + 36, 160, 160);
            const std::string name = m.egg ? "Egg" : m.name;
            const float nw = HudTextWidth(ui, f, 48, name, true);
            const std::string lv = m.egg ? "" : "Lv " + std::to_string(m.level);
            const float lw = m.egg ? 0 : 40 + HudTextWidth(ui, f, 32, lv);
            const float nx = x + w / 2 - (nw + lw) / 2;
            HudText(c, ui, f, nx, y + 222, 48, name, Ink, 0, true);
            if (!m.egg)
            {
                HudParty::Gender(c, ui, nx + nw + 12, y + 232, m.gender, 26);
                HudText(c, ui, f, nx + nw + 44, y + 232, 32, lv, 0xFF3E8EF0);
                // The types.
                std::vector<int> types;
                for (int t = 0; t < 2; t++)
                    if (m.types[t] >= 0 && (t == 0 || m.types[1] != m.types[0])) types.push_back(m.types[t]);
                float total = 0;
                for (int t : types) total += HudTextWidth(ui, f, 32, HudParty::TypeName(t)) + 36 + 12;
                float px = x + w / 2 - (total - 12) / 2;
                for (int t : types) px += HudParty::Pill(c, ui, f, px, y + 290, 44, HudParty::TypeName(t), HudParty::TypeColour(t)) + 12;
                // Ability, item and who caught it.
                const float rw = w - 48, rxl = x + 24;
                auto row = [&](float ry, const char* panel, const std::string& label, const std::string& value) {
                    HudPanel(c, ui, sp, panel, rxl, ry, rw, 46);
                    HudText(c, ui, f, rxl + 18, ry + 7, 32, label, Muted);
                    HudText(c, ui, f, rxl + rw - 18, ry + 7, 32, value, Ink, 2);
                };
                row(y + 356, "panels/row", "Ability", Party_Text(720, m.ability));
                row(y + 414, "panels/row", "Item", m.item ? Party_Text(222, m.item) : "None");
                const bool mine = m.otName == myName && (m.otId & 0xFFFF) == (myId & 0xFFFF);
                row(y + 472, mine ? "panels/row" : "panels/row_gold", mine ? "Caught by" : "Traded from", mine ? "You" : m.otName);
                static const char* months[13] = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
                std::string met = Party_Text(279, m.metPlace);
                if (m.metMonth && m.metDay) met += (met.empty() ? "" : " \xC2\xB7 ") + std::to_string(m.metDay) + " " + months[m.metMonth];
                HudText(c, ui, f, x + w / 2, y + 540, 32, met, Muted, 1);
            }
        }
        // The game's menu for it, the option under the cursor in yellow.
        if (menuOpen)
            for (size_t k = 0; k < menu.size(); k++)
            {
                const float by = y + 600 + 58 * k, bw = w - 48, bx = x + 24;
                const bool on = s.cursor == menuFirst + (int)k;
                HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", bx, by, bw, 48);
                if (on) HudSprite(c, ui, sp, "cursor/cursor_small", bx + 14, by + 8, 30, 32);
                HudText(c, ui, f, bx + bw / 2, by + 8, 32, menu[k], menu[k] == "Release" ? Danger : Ink, 1);
            }
    }

    // The Pokemon held, over the place the cursor is on and a little up, with the hand.
    if (heldAt >= 0)
        if (const PartyMon* m = monAt(s.held))
        {
            const float hx = heldAt < 30 ? x0 + CellX(heldAt) + 11 : x0 + SlotX(heldAt - 30) + 10;
            const float hy = (heldAt < 30 ? CellY(heldAt) + 11 : SlotY(heldAt - 30) + 10) - 44;
            iconIn(*m, hx, hy, 96);
            HudSprite(c, ui, sp, "cursor/cursor", hx + 58, hy + 58, 36, 40);
        }
    // Withdrawn: the Pokemon glides from its cell to the party's first free slot.
    if (gliding)
    {
        const float t = std::clamp(glide, 0.0f, 1.0f);
        const float fx = CellX(glideFrom) + 11, fy = CellY(glideFrom) + 11;
        const float tx = SlotX(s.partyCount) + 10, ty = SlotY(s.partyCount) + 10;
        iconIn(s.mons[glideFrom], x0 + fx + (tx - fx) * t, fy + (ty - fy) * t, 96);
    }
    if (s.held >= 0 && tabs)
        if (const PartyMon* m = monAt(s.held))
        {
            const float hx = x0 + kBoxX + 430 + HudTextWidth(ui, f, 48, s.headName) / 2 + 16, hy = kBoxY - 6;
            iconIn(*m, hx, hy, 96);
            HudSprite(c, ui, sp, "cursor/cursor", hx + 58, hy + 58, 36, 40);
        }
    DrawHintBar(c, ui, sp, f, hints);
}
}
