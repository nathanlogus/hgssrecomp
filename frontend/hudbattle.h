#pragma once
// The battle's commands and moves redrawn in the HUD style (online-HUD design, 18-ss-battle),
// in place of the strip of touch-screen buttons the single-screen view shows under the
// field ("battle-cmd" and "battle-moves" scenes in main.cpp): a row of discs (FIGHT,
// POKéMON, BAG, RUN), the "What will X do?" dialogue beside them and, once FIGHT is chosen, a
// card with the moves and their PP over the field. The game still takes the input (main.cpp
// touches the buttons for it); everything shown is read from the game's memory every frame,
// and nothing is shown when what is read does not check out.
#include <cmath>
#include <map>
#include "hudmenu.h"
#include "ndsrom.h"
#include "party.h"
#include "romart.h"

namespace HudBattle
{
// The discs from left to right, as indices of main.cpp's battle strip (its order is the
// touch screen's: FIGHT, BAG, POKéMON, RUN).
constexpr int CmdOrder[4] = { 0, 2, 1, 3 };

// What the battle's touch screen is asking, as read from the game.
struct State
{
    bool moves = false; // the move list (FIGHT chosen), else the commands
    bool idle = false;  // neither: between two menus
    std::string name, foe;
    int species = 0, level = 0, hp = 0, maxHp = 0, foeSpecies = 0, foeLevel = 0;
    bool wild = false;
    uint16_t move[4] = {};
    uint8_t pp[4] = {}, ppMax[4] = {};
};

// Reads the battle from main RAM. Where things are (pokeheartgold): FieldSystem (pointer at
// 0x021D4158) -> its process manager -> the child application, the battle (overlay 12),
// whose data is the BattleSystem; that holds the BattleContext (+0x30) and the BattleInput
// (+0x19C), the bottom screen's state: the menu shown (curMenuId, +0x68B) and the data it was
// given (+0x1C; for the moves: numbers, PP and maximum PP). The Pokémon in battle are in the
// context (battleMons, +0x2D40, 0xC0 bytes each). Only plain single battles are accepted (no
// double, link, Safari, Bug-Catching Contest, Pal Park or tutorial battles), and the menu's
// data must match the Pokémon in battle.
inline bool Read(const uint8_t* ram, uint32_t mask, State& s)
{
    auto in = [](uint32_t a) { return a >= 0x02000000 && a < 0x02400000; };
    auto r32 = [&](uint32_t a) -> uint32_t { return in(a) ? *(const uint32_t*)&ram[a & mask & ~3u] : 0; };
    auto r16 = [&](uint32_t a) -> uint32_t { return in(a) ? *(const uint16_t*)&ram[a & mask & ~1u] : 0; };
    auto r8 = [&](uint32_t a) -> uint32_t { return in(a) ? ram[a & mask] : 0; };
    const uint32_t fs = r32(0x021D4158), pm = r32(fs), app = r32(pm + 4);
    if (!in(fs) || !in(pm) || !in(app) || r32(app + 0xC) != 12) return false;
    const uint32_t bs = r32(app + 0x1C), ctx = r32(bs + 0x30), bi = r32(bs + 0x19C);
    if (!in(bs) || !in(ctx) || !in(bi) || r32(bi) != bs) return false;
    // Allowed: trainer (bit 0), AI (6) and roaming Pokémon (8) battles.
    const uint32_t type = r32(bs + 0x2C);
    if ((type & ~0x141u) != 0 || r32(bs + 0x44) != 2) return false;
    const int menu = (int8_t)r8(bi + 0x68B);
    // 1-4: the four commands (sliding in or not), 11: the moves. (5-8 have other buttons.)
    // -1 and 0: none, as between two menus.
    if ((menu < -1 || menu > 4) && menu != 11) return false;

    // The Pokémon in battle: the player's (battler 0) and the opponent's (1).
    auto mon = [&](int b, int& species, int& level, std::string& name) {
        const uint32_t m = ctx + 0x2D40 + 0xC0 * b;
        species = (int)r16(m);
        level = (int)r8(m + 0x34);
        uint16_t nick[11];
        for (int i = 0; i < 11; i++) nick[i] = (uint16_t)r16(m + 0x36 + 2 * i);
        name = NdsText(nick, 11);
        return species >= 1 && species <= 493 && level >= 1 && level <= 100 && !name.empty() && name.size() < 40;
    };
    if (!mon(0, s.species, s.level, s.name) || !mon(1, s.foeSpecies, s.foeLevel, s.foe)) return false;
    const uint32_t me = ctx + 0x2D40;
    s.hp = (int)r32(me + 0x4C);
    s.maxHp = (int)r32(me + 0x50);
    if (s.hp < 0 || s.maxHp <= 0 || s.hp > s.maxHp || s.maxHp > 999) return false;
    s.wild = !(type & 1);
    s.moves = menu == 11;
    s.idle = menu <= 0;
    if (s.idle) return true;
    const uint32_t d = bi + 0x1C;
    // BattleInputMainMenu: the HP it shows is the Pokémon's.
    const bool mainData = r8(d + 1) == 0 && (int)(int16_t)r16(d + 4) == s.hp && (int)r16(d + 6) == s.maxHp;
    if (!s.moves && mainData) return true;
    // BattleInputFightMenu: the moves must be the Pokémon's, with its PP. (Back from the moves
    // the commands come up without new data, so the moves' data is still there then.)
    int count = 0;
    for (int i = 0; i < 4; i++)
    {
        s.move[i] = (uint16_t)r16(d + 2 * i);
        s.pp[i] = (uint8_t)r8(d + 8 + i);
        s.ppMax[i] = (uint8_t)r8(d + 12 + i);
        if (s.move[i] != r16(me + 0xC + 2 * i) || s.move[i] > 467 || s.pp[i] != r8(me + 0x2C + i) || s.pp[i] > s.ppMax[i] ||
            s.ppMax[i] > 64 || (s.move[i] != 0) != (s.ppMax[i] != 0))
            return false;
        count += s.move[i] != 0;
    }
    return r8(d + 16) == r8(bi + 0x68A) && count > 0;
}

// Any single battle, link battles too, as the others in a session are told about it
// (PartyBattle): each side's Pokémon and HP, the turn (BattleContext totalTurns, +0x150) and
// whether a command is being chosen. Which battler is this player's comes from the battler
// type of its OpponentData (BattleSystem +0x34, the type at +0x195; even types are the
// player's side), as in a link battle both games number the battlers alike.
inline bool ReadLive(const uint8_t* ram, uint32_t mask, PartyBattle& b)
{
    auto in = [](uint32_t a) { return a >= 0x02000000 && a < 0x02400000; };
    auto r32 = [&](uint32_t a) -> uint32_t { return in(a) ? *(const uint32_t*)&ram[a & mask & ~3u] : 0; };
    auto r16 = [&](uint32_t a) -> uint32_t { return in(a) ? *(const uint16_t*)&ram[a & mask & ~1u] : 0; };
    auto r8 = [&](uint32_t a) -> uint32_t { return in(a) ? ram[a & mask] : 0; };
    const uint32_t fs = r32(0x021D4158), pm = r32(fs), app = r32(pm + 4);
    if (!in(fs) || !in(pm) || !in(app) || r32(app + 0xC) != 12) return false;
    const uint32_t bs = r32(app + 0x1C), ctx = r32(bs + 0x30), bi = r32(bs + 0x19C);
    if (!in(bs) || !in(ctx) || !in(bi) || r32(bi) != bs || r32(bs + 0x44) != 2) return false;
    int mine = -1;
    for (int k = 0; k < 2; k++)
    {
        const uint32_t od = r32(bs + 0x34 + 4 * k);
        if (!in(od)) return false;
        if (!(r8(od + 0x195) & 1)) mine = k;
    }
    if (mine < 0) return false;
    auto mon = [&](int k, int& species, int& hp) {
        const uint32_t m = ctx + 0x2D40 + 0xC0 * k;
        species = (int)r16(m);
        const int cur = (int)r32(m + 0x4C), most = (int)r32(m + 0x50);
        if (species < 1 || species > 493 || most <= 0 || most > 999 || cur < 0 || cur > most) return false;
        hp = (cur * 100 + most - 1) / most; // 1% while any is left
        return true;
    };
    if (!mon(mine, b.species, b.hp) || !mon(1 - mine, b.foeSpecies, b.foeHp)) return false;
    const int menu = (int8_t)r8(bi + 0x68B);
    b.choosing = (menu >= 1 && menu <= 4) || menu == 11;
    b.link = r32(bs + 0x2C) & 4;
    b.turn = (int)std::min<uint32_t>(r32(ctx + 0x150), 254) + 1;
    return true;
}

// Move data from the ROM: names (text bank 750) and the move table (a/0/1/1: category,
// power, type and accuracy at bytes 2-5 of each move's entry).
class Moves
{
public:
    void SetRom(const uint8_t* r, size_t n)
    {
        rom = r;
        size = n;
        table = NdsNarcFiles(rom, size, "a/0/1/1");
    }
    struct Info
    {
        int type = 0, power = 0, accuracy = 0, category = 0;
    };
    bool Get(int move, Info& in) const
    {
        if (move <= 0 || move >= (int)table.size() || table[move].second < 6) return false;
        const uint8_t* e = rom + table[move].first;
        in.category = e[2];
        in.power = e[3];
        in.type = e[4];
        in.accuracy = e[5];
        return in.type < 18 && in.category < 3;
    }
    const std::string& Name(int move)
    {
        auto it = names.find(move);
        if (it == names.end()) it = names.emplace(move, rom ? NdsMessage(rom, size, 750, move) : "").first;
        return it->second;
    }

private:
    const uint8_t* rom = nullptr;
    size_t size = 0;
    std::vector<std::pair<uint32_t, uint32_t>> table;
    std::map<int, std::string> names;
};

inline const char* TypeName(int t)
{
    static const char* n[18] = { "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel", "???", "Fire",
        "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark" };
    return t >= 0 && t < 18 ? n[t] : "???";
}
inline uint32_t TypeColour(int t)
{
    static const uint32_t c[18] = { 0xFFA8A878, 0xFFC03028, 0xFFA890F0, 0xFFA040A0, 0xFFE0C068, 0xFFB8A038, 0xFFA8B820, 0xFF705898,
        0xFFB8B8D0, 0xFF68A090, 0xFFF08030, 0xFF6890F0, 0xFF78C850, 0xFFF8D030, 0xFFF85888, 0xFF98D8D8, 0xFF7038F8, 0xFF705848 };
    return t >= 0 && t < 18 ? c[t] : 0xFF68A090;
}

// A filled rectangle with round corners of radius `r`, in canvas pixels, its edges smoothed.
inline void RoundRect(Canvas& c, float x, float y, float w, float h, float r, uint32_t argb)
{
    const int x0 = std::max(0, (int)std::floor(x)), y0 = std::max(0, (int)std::floor(y));
    const int x1 = std::min(c.w, (int)std::ceil(x + w)), y1 = std::min(c.h, (int)std::ceil(y + h));
    if (x1 <= x0 || y1 <= y0) return;
    c.Mark(x0, y0, x1, y1);
    r = std::min({ r, w / 2, h / 2 });
    const float cx = x + w / 2, cy = y + h / 2, hx = w / 2 - r, hy = h / 2 - r;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
        {
            // Signed distance to the rounded rectangle's edge.
            const float qx = std::fabs(xx + 0.5f - cx) - hx, qy = std::fabs(yy + 0.5f - cy) - hy;
            const float d = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
            const float cov = std::clamp(0.5f - d, 0.0f, 1.0f);
            if (cov <= 0) continue;
            uint32_t& p = c.px[(size_t)yy * c.w + xx];
            p = BlendOver(p, (uint32_t)((argb >> 24) * cov) << 24 | (argb & 0xFFFFFF));
        }
}

// A type as a pill in its colour with its name (design pixels).
inline void TypePill(Canvas& c, const Ui& ui, const RomFont& f, float x, float y, float w, float h, int type)
{
    RoundRect(c, ui(x), ui(y), ui(w), ui(h), ui(h / 2), TypeColour(type));
    // Dark letters on the light types.
    const bool light = type == 13 || type == 15 || type == 4;
    HudText(c, ui, f, x + w / 2, y + (h - 32) / 2, 32, TypeName(type), light ? HudColour::Ink : HudColour::White, 1);
}

// The commands strip and, for the moves, the card. `sel`: the chosen button as an index of
// main.cpp's strip (-1 none). The strip covers the view from `top` (design pixels) down.
inline void DrawAll(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, Moves& mv, const State& s, int sel, const Image* myIcon,
    const Image* foeIcon, const std::vector<HudHint>& hints, float top)
{
    using namespace HudColour;
    const float wd = ui.W / ui.s;
    // The strip: a pale band under the field.
    FillBlend(c, 0, ui(top), (float)c.w, (float)c.h - ui(top), 0xFFEAF4FA);
    FillBlend(c, 0, ui(top), (float)c.w, ui(4), 0xFFFFFFFF);

    // The dialogue: what the game asks, and who is fighting.
    const float discsW = 4 * 207 + 40, bx = 40, by = top + 30, bw = wd - discsW - 80, bh = 190;
    HudPanel(c, ui, sp, "panels/dialogue_box", bx, by, bw, bh);
    HudText(c, ui, f, bx + 40, by + 34, 48, "What will " + s.name + " do?", Ink);
    float lx = bx + 34;
    const float ly = by + 112;
    if (myIcon)
    {
        HudPixelArt(c, ui, myIcon, lx, ly - 18, 64, 64);
        lx += 70;
    }
    std::string line = "Lv " + std::to_string(s.level) + " \xC2\xB7 HP " + std::to_string(s.hp) + "/" + std::to_string(s.maxHp) + "   \xC2\xB7   " +
        (s.wild ? "wild " : "") + s.foe + " Lv " + std::to_string(s.foeLevel);
    HudText(c, ui, f, lx, ly, 32, line, Muted);
    lx += HudTextWidth(ui, f, 32, line) + 8;
    if (foeIcon && lx + 64 < bx + bw - 20) HudPixelArt(c, ui, foeIcon, lx, ly - 18, 64, 64);

    // The discs; the chosen one larger with its name on a yellow pill. While the moves are
    // shown FIGHT stays chosen; between menus none is, and the others are faded.
    static const char* icon[4] = { "battle/command_fight", "battle/command_bag", "battle/command_pokemon", "battle/command_run" };
    static const char* label[4] = { "FIGHT", "BAG", "POK\xC3\xA9MON", "RUN" };
    const int chosen = s.idle ? -1 : s.moves ? 0 : sel;
    for (int k = 0; k < 4; k++)
    {
        const int i = CmdOrder[k];
        const bool on = i == chosen;
        const float cx = wd - 140 - (3 - k) * 207, cy = top + 104, d = on ? 176 : 140;
        HudSprite(c, ui, sp, std::string(icon[i]) + (on ? "_selected" : ""), cx - d / 2, cy - d / 2, d, d, (s.moves || s.idle) && !on ? 0xA0FFFFFF : 0xFFFFFFFF);
        const float lw = HudTextWidth(ui, f, 32, label[i]) + 36, ly2 = cy + 66;
        HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", cx - lw / 2, ly2, lw, 40);
        HudText(c, ui, f, cx, ly2 + 4, 32, label[i], Ink, 1);
    }
    DrawHintBar(c, ui, sp, f, hints);
    if (!s.moves) return;

    // The moves: a card over the field.
    const float w = 780, x = wd / 2 - w / 2, y = 30, rowX = x + 22, rowW = w - 44;
    HudPanel(c, ui, sp, "panels/card", x, y, w, 446);
    RoundRect(c, ui(rowX), ui(y + 22), ui(rowW), ui(52), ui(16), 0xFFE05252);
    HudSprite(c, ui, sp, "icons/white/icon_fight", rowX + 18, y + 28, 40, 40);
    HudText(c, ui, f, rowX + 72, y + 32, 32, "FIGHT", White);
    HudText(c, ui, f, rowX + rowW - 20, y + 32, 32, s.name, White, 2);
    for (int i = 0; i < 4; i++)
    {
        const float ry = y + 86 + i * 74, rh = 62;
        if (!s.move[i])
        {
            HudPanel(c, ui, sp, "panels/row_disabled", rowX, ry, rowW, rh);
            HudText(c, ui, f, rowX + 200, ry + 15, 32, "-", Muted);
            continue;
        }
        Moves::Info in;
        const bool known = mv.Get(s.move[i], in);
        const bool on = i == sel, empty = s.pp[i] == 0;
        HudPanel(c, ui, sp, on ? "panels/row_selected" : empty ? "panels/row_disabled" : "panels/row", rowX, ry, rowW, rh);
        if (on) HudSprite(c, ui, sp, "cursor/cursor", rowX + 12, ry + 11, 36, 40);
        if (known) TypePill(c, ui, f, rowX + 58, ry + 11, 128, 40, in.type);
        HudText(c, ui, f, rowX + 204, ry + 15, 32, mv.Name(s.move[i]), empty && !on ? Muted : Ink);
        // PP: a bar teal above half, orange above a quarter, red below.
        const float frac = (float)s.pp[i] / s.ppMax[i], barW = 180, barX = rowX + rowW - 160 - barW, barY = ry + 20;
        HudSprite(c, ui, sp, "bars/bar_frame", barX, barY, barW, 22);
        if (s.pp[i])
            HudSprite(c, ui, sp, frac > 0.5f ? "bars/fill_teal" : frac > 0.25f ? "bars/fill_yellow" : "bars/fill_red", barX + 6, barY + 4,
                std::max(16.0f, (barW - 12) * frac), 14, frac > 0.5f || frac <= 0.25f ? 0xFFFFFFFF : 0xFFFFB070);
        HudText(c, ui, f, rowX + rowW - 24, ry + 15, 32, std::to_string(s.pp[i]) + "/" + std::to_string(s.ppMax[i]),
            frac <= 0.25f ? Danger : Ink, 2);
    }
    // The chosen move's numbers.
    Moves::Info in;
    if (sel >= 0 && sel < 4 && s.move[sel] && mv.Get(s.move[sel], in))
    {
        const float fy = y + 86 + 4 * 74 + 4;
        TypePill(c, ui, f, rowX + 6, fy, 128, 40, in.type);
        static const char* cat[3] = { "Physical", "Special", "Status" };
        const std::string dot = " \xC2\xB7 ";
        const std::string text = std::string("Power ") + (in.power > 1 ? std::to_string(in.power) : "-") + dot + "Acc " +
            (in.accuracy ? std::to_string(in.accuracy) : "-") + dot + cat[in.category];
        HudText(c, ui, f, rowX + 154, fy + 4, 32, text, Muted);
    }
}

// Draws into `c`, redrawing only when something shown changes: the last picture is kept in a
// layer of its own and put back (the panels take a good part of a frame to draw).
class Hud
{
public:
    void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, Moves& mv, const State& s, int sel, const Image* myIcon,
        const Image* foeIcon, const std::vector<HudHint>& hints, float top)
    {
        std::string k = s.name + "|" + s.foe + "|" + std::to_string(s.moves) + std::to_string(s.idle) + std::to_string(sel) + "|" + std::to_string(s.level) + "," +
            std::to_string(s.hp) + "," + std::to_string(s.maxHp) + "," + std::to_string(s.species) + "," + std::to_string(s.foeSpecies) + "," +
            std::to_string(s.foeLevel) + "|" + std::to_string(c.w) + "x" + std::to_string(c.h) + "|" + std::to_string(top);
        for (int i = 0; i < 4; i++) k += "|" + std::to_string(s.move[i]) + "," + std::to_string(s.pp[i]) + "," + std::to_string(s.ppMax[i]);
        for (const HudHint& h : hints)
            for (const std::string& g : h.glyphs) k += "|" + g + h.label;
        if (k != key)
        {
            key = k;
            layer.Resize(c.w, c.h);
            std::fill(layer.px.begin(), layer.px.end(), 0);
            DrawAll(layer, ui, sp, f, mv, s, sel, myIcon, foeIcon, hints, top);
        }
        // The strip is opaque: copied. The card is blended over the field.
        const int y0 = std::clamp((int)ui(top), 0, c.h);
        std::copy(layer.px.begin() + (size_t)y0 * c.w, layer.px.end(), c.px.begin() + (size_t)y0 * c.w);
        c.Mark(0, y0, c.w, c.h);
        if (s.moves)
        {
            const float wd = ui.W / ui.s;
            const int x0 = std::max(0, (int)ui(wd / 2 - 410)), x1 = std::min(c.w, (int)std::ceil(ui(wd / 2 + 410)));
            const int ya = std::max(0, (int)ui(10)), yb = std::min(y0, (int)std::ceil(ui(500)));
            for (int y = ya; y < yb; y++)
                for (int x = x0; x < x1; x++)
                {
                    const size_t p = (size_t)y * c.w + x;
                    c.px[p] = BlendOver(c.px[p], layer.px[p]);
                }
            c.Mark(x0, ya, x1, yb);
        }
    }

private:
    Canvas layer;
    std::string key;
};
}
