#pragma once
// The battle's touch-screen menus redrawn in the HUD style (online-HUD design, 18-ss-battle),
// in place of the strip of touch-screen buttons the single-screen view shows under the field
// ("battle-cmd", "battle-moves" and "battle-pick" scenes in main.cpp), all in one band under
// the field so that nothing of the field is covered:
//  - the commands: a row of discs (FIGHT, POKéMON, BAG, RUN or CANCEL) beside a dialogue
//    asking "What will X do?";
//  - the moves (FIGHT chosen): a 2 x 2 grid of buttons in the move's type colour with its PP,
//    where the discs were, and the chosen move's type, power, accuracy and category in the
//    dialogue;
//  - in a double battle, the target: the four places on the field as a 2 x 2 grid, the foes
//    above, as the game's touch screen has them;
//  - the questions with two answers (Use next Pokémon / Flee, Switch Pokémon / Keep
//    battling, Yes / No, keeping or forgetting a move): the question in the dialogue, the
//    answers as two rows.
// The game still takes the input (main.cpp touches the buttons for it); everything shown is
// read from the game's memory every frame, and nothing is shown when what is read does not
// check out.
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

// What the battle's touch screen is asking.
enum Kind { Idle, Commands, Fight, Target, Choice };

// The target menu's four buttons, as the touch screen numbers them (and main.cpp touches
// them): 0 the player's left Pokémon, 1 the foes' right one, 2 the player's right one, 3 the
// foes' left one. As a grid, foes above: TargetGrid[row][column].
constexpr int TargetGrid[2][2] = { { 3, 1 }, { 0, 2 } };

struct Mon
{
    int species = 0, level = 0, hp = 0, maxHp = 0;
    std::string name;
};

// What the battle's touch screen is asking, as read from the game.
struct State
{
    int kind = Idle;
    bool moves = false; // kind == Fight
    bool idle = false;  // kind == Idle: between two menus
    bool wild = false, doubles = false;
    bool cancel = false; // the commands' last button is CANCEL (a double battle's second Pokémon)
    Mon me;              // the Pokémon whose turn it is (in a choice: the player's first)
    Mon foe[2];          // the opponent's Pokémon in battle (the second only in a double battle)
    uint16_t move[4] = {};
    uint8_t pp[4] = {}, ppMax[4] = {};
    // The target menu: the four places (see TargetGrid), whether a Pokémon stands there and
    // whether the move can be aimed at it; `multi`: the move hits all of those at once.
    Mon box[4];
    bool shown[4] = {}, hit[4] = {}, multi = false;
    // A choice: the game's menu (13 yes/no, 14 forget/keep a move, 15 give up on a move,
    // 16 next Pokémon/flee, 17 switch/keep battling), the move it names, the question asked.
    int choice = 0, choiceMove = 0;
    std::string question;

    // Everything shown, for knowing when to draw again.
    std::string Key() const
    {
        std::string k = std::to_string(kind) + (wild ? "w" : "") + (doubles ? "d" : "") + (cancel ? "c" : "") + "|" + std::to_string(choice) + "," +
            std::to_string(choiceMove) + "|" + question;
        auto add = [&](const Mon& m) {
            k += "|" + m.name + "," + std::to_string(m.species) + "," + std::to_string(m.level) + "," + std::to_string(m.hp) + "," +
                std::to_string(m.maxHp);
        };
        add(me);
        for (const Mon& m : foe) add(m);
        for (int i = 0; i < 4; i++)
        {
            k += "|" + std::to_string(move[i]) + "," + std::to_string(pp[i]) + "," + std::to_string(ppMax[i]) + (shown[i] ? "s" : "") + (hit[i] ? "h" : "");
            add(box[i]);
        }
        return k + (multi ? "m" : "");
    }
};

// Which places a target menu's move reaches (pokeheartgold sMoveRangeHitMons), by the game's
// selection type, in the order of the touch screen's buttons. Types 1, 2, 3, 5 and 7 hit all
// of their places at once.
constexpr uint8_t TargetHits[12][4] = { { 1, 1, 1, 1 }, { 0, 1, 0, 1 }, { 0, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 0, 0, 0 }, { 1, 0, 1, 0 },
    { 0, 0, 1, 0 }, { 1, 1, 0, 1 }, { 0, 1, 1, 1 }, { 1, 1, 0, 1 }, { 1, 0, 1, 0 }, { 0, 1, 0, 1 } };

// Reads the battle from main RAM. Where things are (pokeheartgold): FieldSystem (pointer at
// 0x021D4158) -> its process manager -> the child application, the battle (overlay 12),
// whose data is the BattleSystem: battle type (+0x2C), the context (+0x30), the battlers'
// display data (+0x34, each with its place, battlerType, at +0x195), their number (+0x44), the
// last message printed (+0x18, a String) and the BattleInput (+0x19C), the bottom screen's
// state: the menu shown (curMenuId, +0x68B), whose turn it is (battlerType, +0x68A), the
// target menu's selection type (+0x68C), whether the commands end in CANCEL (+0x68F) and the
// data the menu was given (+0x1C). The Pokémon in battle are in the context (battleMons,
// +0x2D40, 0xC0 bytes each). Only plain single and double battles are accepted (no link,
// multi, tag, Safari, Pal Park, Bug-Catching Contest or tutorial battles), and the menu's
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
    // Allowed: trainer (bit 0), double (1), AI (6) and roaming Pokémon (8) battles.
    const uint32_t type = r32(bs + 0x2C), count = r32(bs + 0x44);
    s.doubles = type & 2;
    s.wild = !(type & 1);
    if ((type & ~0x143u) != 0 || count != (s.doubles ? 4u : 2u)) return false;
    const int menu = (int8_t)r8(bi + 0x68B);
    // -1 and 0: none, as between two menus; 1-4: the commands (sliding in or not); 11: the
    // moves; 12: the target; 13-17: two answers. (5-10 are Safari and Pal Park's.)
    s.kind = menu >= -1 && menu <= 0 ? Idle : menu >= 1 && menu <= 4 ? Commands : menu == 11 ? Fight : menu == 12 ? Target : menu >= 13 && menu <= 17 ? Choice : -1;
    if (s.kind < 0 || (s.kind == Target && !s.doubles)) return false;
    s.moves = s.kind == Fight;
    s.idle = s.kind == Idle;

    auto mon = [&](int b, Mon& m) {
        const uint32_t a = ctx + 0x2D40 + 0xC0 * b;
        m.species = (int)r16(a);
        m.level = (int)r8(a + 0x34);
        uint16_t nick[11];
        for (int i = 0; i < 11; i++) nick[i] = (uint16_t)r16(a + 0x36 + 2 * i);
        m.name = NdsText(nick, 11);
        m.hp = (int)r32(a + 0x4C);
        m.maxHp = (int)r32(a + 0x50);
        return m.species >= 1 && m.species <= 493 && m.level >= 1 && m.level <= 100 && !m.name.empty() && m.name.size() < 40 && m.hp >= 0 &&
            m.maxHp > 0 && m.hp <= m.maxHp && m.maxHp <= 999;
    };
    // The battler shown at a place (battlerType: 0/1 in a single battle, 2-5 in a double).
    auto battlerAt = [&](uint32_t place) {
        for (int i = 0; i < (int)count; i++)
        {
            const uint32_t od = r32(bs + 0x34 + 4 * i);
            if (in(od) && r8(od + 0x195) == place) return i;
        }
        return -1;
    };
    if (!mon(1, s.foe[0])) return false;
    s.foe[1] = Mon();
    if (s.doubles && !mon(3, s.foe[1])) s.foe[1] = Mon();
    s.cancel = s.doubles && r8(bi + 0x68F) == 1;
    const uint32_t d = bi + 0x1C;
    // Whose turn it is; the player's are the even battlers.
    int who = 0;
    if (s.kind == Commands || s.kind == Fight || s.kind == Target)
    {
        who = battlerAt(r8(bi + 0x68A));
        if (who < 0 || (who & 1)) return false;
    }
    if (!mon(who, s.me)) return false;
    if (s.kind == Idle) return true;
    if (s.kind == Choice)
    {
        s.choice = menu;
        s.choiceMove = menu == 15 ? (int)r16(d) : 0;
        if (s.choiceMove > 467) return false;
        // The question is the battle's last message (a String: size, length, magic, text).
        s.question.clear();
        const uint32_t str = r32(bs + 0x18);
        const uint32_t len = r16(str + 2);
        if (in(str) && r32(str + 4) == 0xB6F8D2EC && len <= r16(str) && len < 200)
        {
            uint16_t text[200];
            for (uint32_t i = 0; i < len; i++) text[i] = (uint16_t)r16(str + 8 + 2 * i);
            s.question = NdsText(text, len);
        }
        return true;
    }
    if (s.kind == Target)
    {
        // BattleInputTargetMenu: per battler its Pokémon's HP (and whether it is there), then
        // whose turn it is and the selection type.
        const uint32_t sel = r8(d + 33);
        if (r8(d + 32) != r8(bi + 0x68A) || sel != r8(bi + 0x68C) || sel >= 12) return false;
        s.multi = sel == 1 || sel == 2 || sel == 3 || sel == 5 || sel == 7;
        bool any = false;
        for (int i = 0; i < 4; i++)
        {
            const int b = battlerAt(2 + i);
            if (b < 0) return false;
            const uint32_t t = d + 8 * b;
            s.shown[i] = (r8(t + 1) >> 2) & 1;
            s.box[i] = Mon();
            if (s.shown[i] && (!mon(b, s.box[i]) || s.box[i].hp != (int)(int16_t)r16(t + 4) || s.box[i].maxHp != (int)r16(t + 6))) return false;
            s.hit[i] = s.shown[i] && TargetHits[sel][i];
            any |= s.hit[i];
        }
        return any;
    }
    // BattleInputMainMenu: the HP it shows is the Pokémon's.
    const bool mainData = (int)r8(d + 1) == who && battlerAt(r8(d)) == who && (int)(int16_t)r16(d + 4) == s.me.hp && (int)r16(d + 6) == s.me.maxHp;
    if (s.kind == Commands && mainData) return true;
    // BattleInputFightMenu: the moves must be the Pokémon's, with its PP. (Back from the moves
    // the commands come up without new data, so the moves' data is still there then.)
    const uint32_t me = ctx + 0x2D40 + 0xC0 * who;
    int n = 0;
    for (int i = 0; i < 4; i++)
    {
        s.move[i] = (uint16_t)r16(d + 2 * i);
        s.pp[i] = (uint8_t)r8(d + 8 + i);
        s.ppMax[i] = (uint8_t)r8(d + 12 + i);
        if (s.move[i] != r16(me + 0xC + 2 * i) || s.move[i] > 467 || s.pp[i] != r8(me + 0x2C + i) || s.pp[i] > s.ppMax[i] ||
            s.ppMax[i] > 64 || (s.move[i] != 0) != (s.ppMax[i] != 0))
            return false;
        n += s.move[i] != 0;
    }
    return r8(d + 16) == r8(bi + 0x68A) && n > 0;
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
// Types light enough to want dark letters.
inline bool LightType(int t)
{
    return t == 13 || t == 15 || t == 4 || t == 8;
}

// `a` mixed with `b`, `k` of the way (0-256), keeping a's alpha.
inline uint32_t Mix(uint32_t a, uint32_t b, int k)
{
    uint32_t out = a & 0xFF000000;
    for (int sh = 0; sh < 24; sh += 8)
        out |= (uint32_t)((((a >> sh) & 0xFF) * (256 - k) + ((b >> sh) & 0xFF) * k) >> 8) << sh;
    return out;
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
    HudText(c, ui, f, x + w / 2, y + (h - 32) / 2, 32, TypeName(type), LightType(type) ? HudColour::Ink : HudColour::White, 1);
}

// A button of the grids: a white rim around its colour, and a yellow ring outside when chosen
// (design pixels).
inline void GridButton(Canvas& c, const Ui& ui, float x, float y, float w, float h, uint32_t colour, bool on)
{
    if (on)
    {
        RoundRect(c, ui(x - 7), ui(y - 7), ui(w + 14), ui(h + 14), ui(30), 0xFF3B3F4A);
        RoundRect(c, ui(x - 5), ui(y - 5), ui(w + 10), ui(h + 10), ui(28), HudColour::Yellow);
    }
    RoundRect(c, ui(x), ui(y), ui(w), ui(h), ui(24), 0xFFFFFFFF);
    RoundRect(c, ui(x + 4), ui(y + 4), ui(w - 8), ui(h - 8), ui(20), colour);
}

// A bar of `frac` in a frame (design pixels): teal above half, orange above a quarter, red
// below; with `hp`, green, yellow and red as the game's HP bars.
inline void Bar(Canvas& c, const Ui& ui, Sprites& sp, float x, float y, float w, float frac, bool hp)
{
    HudSprite(c, ui, sp, "bars/bar_frame", x, y, w, 22);
    if (frac <= 0) return;
    const bool high = frac > 0.5f, mid = !high && frac > 0.25f;
    const char* fill = high ? (hp ? "bars/fill_green" : "bars/fill_teal") : mid ? "bars/fill_yellow" : "bars/fill_red";
    HudSprite(c, ui, sp, fill, x + 6, y + 4, std::max(16.0f, (w - 12) * frac), 14, mid && !hp ? 0xFFFFB070 : 0xFFFFFFFF);
}

// The band under the field. `sel`: the chosen button as main.cpp touches it (the commands and
// moves: an index of the touch screen's buttons; the target: of TargetGrid's; a choice: 0
// the upper answer, 1 the lower), -1 none. The band covers the view from `top` (design
// pixels) down.
inline void DrawAll(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, Moves& mv, RomArt& art, const State& s, int sel,
    const std::vector<HudHint>& hints, float top)
{
    using namespace HudColour;
    const float wd = ui.W / ui.s;
    FillBlend(c, 0, ui(top), (float)c.w, (float)c.h - ui(top), 0xFFEAF4FA);
    FillBlend(c, 0, ui(top), (float)c.w, ui(4), 0xFFFFFFFF);

    // The dialogue on the left; the buttons on the right, in the area of the four discs.
    const float areaW = 4 * 207, areaX = wd - areaW - 40, bx = 40, by = top + 30, bw = areaX - 40 - bx, bh = 190;
    HudPanel(c, ui, sp, "panels/dialogue_box", bx, by, bw, bh);
    const float ly = by + 112;
    const std::string dot = " \xC2\xB7 ";
    // A line of small text with Pokémon icons: who is fighting.
    auto fighters = [&]() {
        float lx = bx + 34;
        auto icon = [&](int species) {
            if (const Image* im = art.PokemonIcon(species); im && lx + 64 < bx + bw - 20)
            {
                HudPixelArtCentred(c, ui, im, lx + 32, ly + 15, 0, 64); // on the text's middle line
                lx += 70;
            }
        };
        // (What does not fit is left out, as a double battle's second foe with long names.)
        auto text = [&](const std::string& t) {
            const float w = HudTextWidth(ui, f, 32, t);
            if (lx + w > bx + bw - 24)
            {
                lx = bx + bw;
                return;
            }
            HudText(c, ui, f, lx, ly, 32, t, Muted);
            lx += w + 8;
        };
        icon(s.me.species);
        text("Lv " + std::to_string(s.me.level) + dot + "HP " + std::to_string(s.me.hp) + "/" + std::to_string(s.me.maxHp) + "   " + dot + "  " +
            (s.wild ? "wild " : "") + s.foe[0].name + (s.foe[1].species ? "" : " Lv " + std::to_string(s.foe[0].level)));
        icon(s.foe[0].species);
        // (Two foes: their names only, for room.)
        if (s.foe[1].species)
        {
            text("& " + s.foe[1].name);
            icon(s.foe[1].species);
        }
    };

    if (s.kind == Commands || s.kind == Idle)
    {
        HudText(c, ui, f, bx + 40, by + 34, 48, "What will " + s.me.name + " do?", Ink);
        fighters();
        // The discs; the chosen one larger with its name on a yellow pill. Between menus none
        // is, and the others are faded.
        static const char* icon[4] = { "battle/command_fight", "battle/command_bag", "battle/command_pokemon", "battle/command_run" };
        static const char* label[4] = { "FIGHT", "BAG", "POK\xC3\xA9MON", "RUN" };
        const int chosen = s.idle ? -1 : sel;
        for (int k = 0; k < 4; k++)
        {
            const int i = CmdOrder[k];
            const bool on = i == chosen;
            const float cx = wd - 140 - (3 - k) * 207, cy = top + 104, d = on ? 176 : 140;
            HudSprite(c, ui, sp, std::string(icon[i]) + (on ? "_selected" : ""), cx - d / 2, cy - d / 2, d, d, s.idle && !on ? 0xA0FFFFFF : 0xFFFFFFFF);
            const std::string name = i == 3 && s.cancel ? "CANCEL" : label[i];
            const float lw = HudTextWidth(ui, f, 32, name) + 36, ly2 = cy + 66;
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", cx - lw / 2, ly2, lw, 40);
            HudText(c, ui, f, cx, ly2 + 4, 32, name, Ink, 1);
        }
    }
    else if (s.kind == Fight)
    {
        // The chosen move's details in the dialogue.
        Moves::Info in;
        if (sel >= 0 && sel < 4 && s.move[sel] && mv.Get(s.move[sel], in))
        {
            HudText(c, ui, f, bx + 40, by + 34, 48, mv.Name(s.move[sel]), Ink);
            TypePill(c, ui, f, bx + 34, ly - 4, 140, 40, in.type);
            static const char* cat[3] = { "Physical", "Special", "Status" };
            HudText(c, ui, f, bx + 194, ly, 32,
                std::string("Power ") + (in.power > 1 ? std::to_string(in.power) : "-") + dot + "Accuracy " + (in.accuracy ? std::to_string(in.accuracy) : "-") +
                    dot + cat[in.category] + dot + "PP " + std::to_string(s.pp[sel]) + "/" + std::to_string(s.ppMax[sel]),
                Muted);
        }
        else
        {
            HudText(c, ui, f, bx + 40, by + 34, 48, "What will " + s.me.name + " do?", Ink);
            fighters();
        }
        // The moves as the touch screen has them: two rows of two.
        const float w = (areaW - 20) / 2, h = 86;
        for (int i = 0; i < 4; i++)
        {
            const float x = areaX + (i & 1) * (w + 20), y = by + (i >> 1) * (h + 18);
            if (!s.move[i])
            {
                RoundRect(c, ui(x), ui(y), ui(w), ui(h), ui(24), 0xFFD5DCE4);
                HudText(c, ui, f, x + w / 2, y + 27, 32, "-", Muted, 1);
                continue;
            }
            const bool known = mv.Get(s.move[i], in), empty = s.pp[i] == 0;
            // A move without PP left is dimmed.
            const uint32_t colour = known ? TypeColour(in.type) : 0xFF68A090;
            GridButton(c, ui, x, y, w, h, empty ? Mix(colour, 0xFFC8CDD4, 170) : colour, i == sel);
            const uint32_t ink = empty ? 0xFF6E7684 : known && LightType(in.type) ? Ink : White;
            HudText(c, ui, f, x + 26, y + 10, 32, mv.Name(s.move[i]), ink);
            // PP: a bar, and the numbers on a white chip in the bar's colour.
            const float frac = (float)s.pp[i] / s.ppMax[i];
            Bar(c, ui, sp, x + 24, y + 52, w - 172, frac, false);
            RoundRect(c, ui(x + w - 134), ui(y + 44), ui(112), ui(34), ui(17), 0xF0FFFFFF);
            HudText(c, ui, f, x + w - 78, y + 45, 32, std::to_string(s.pp[i]) + "/" + std::to_string(s.ppMax[i]),
                frac > 0.5f ? Ink : frac > 0.25f ? 0xFFD07010 : Danger, 1);
        }
    }
    else if (s.kind == Target)
    {
        HudText(c, ui, f, bx + 40, by + 34, 48, s.multi ? "This move hits them all" : "Which Pok\xC3\xA9mon will " + s.me.name + " aim at?", Ink);
        HudText(c, ui, f, bx + 40, ly, 32, s.multi ? "The Pok\xC3\xA9mon framed in yellow" : "Foes above, " + s.me.name + "'s side below", Muted);
        const float w = (areaW - 20) / 2, h = 86;
        for (int r = 0; r < 2; r++)
            for (int col = 0; col < 2; col++)
            {
                const int i = TargetGrid[r][col];
                const float x = areaX + col * (w + 20), y = by + r * (h + 18);
                if (!s.shown[i])
                {
                    RoundRect(c, ui(x), ui(y), ui(w), ui(h), ui(24), 0xFFD5DCE4);
                    HudText(c, ui, f, x + w / 2, y + 27, 32, "-", Muted, 1);
                    continue;
                }
                const Mon& m = s.box[i];
                const uint32_t colour = r == 0 ? 0xFFE05252 : 0xFF4A90E2;
                const bool on = s.hit[i] && (s.multi || i == sel);
                GridButton(c, ui, x, y, w, h, s.hit[i] ? colour : Mix(colour, 0xFFC8CDD4, 170), on);
                const Image* im = art.PokemonIcon(m.species);
                if (im) HudPixelArtCentred(c, ui, im, x + 46, y + h / 2, 0, 64);
                const uint32_t ink = s.hit[i] ? White : 0xFF6E7684;
                HudText(c, ui, f, x + 88, y + 10, 32, m.name, ink);
                HudText(c, ui, f, x + w - 24, y + 10, 32, "Lv " + std::to_string(m.level), ink, 2);
                Bar(c, ui, sp, x + 88, y + 52, w - 112, (float)m.hp / m.maxHp, true);
            }
    }
    else if (s.kind == Choice)
    {
        std::string q = s.question;
        if (q.empty()) q = s.choice == 16 ? "Use next Pok\xC3\xA9mon?" : s.choice == 17 ? "Will you switch Pok\xC3\xA9mon?" : "";
        const std::vector<std::string> lines = HudWrap(ui, f, 40, q, bw - 80);
        for (size_t i = 0; i < lines.size() && i < 3; i++) HudText(c, ui, f, bx + 40, by + 30 + 46 * i, 40, lines[i], Ink);
        // The answers, as the game words them.
        const std::string mvName = s.choiceMove ? mv.Name(s.choiceMove) : "the move";
        std::string a[2];
        switch (s.choice)
        {
        case 14: a[0] = "Forget a move!"; a[1] = "Keep old moves!"; break;
        case 15: a[0] = "Give up on " + mvName + "!"; a[1] = "Don't give up on " + mvName + "!"; break;
        case 16: a[0] = "Use next Pok\xC3\xA9mon"; a[1] = "Flee"; break;
        case 17: a[0] = "Switch Pok\xC3\xA9mon"; a[1] = "Keep battling"; break;
        default: a[0] = "Yes"; a[1] = "No"; break;
        }
        const float h = 86;
        for (int i = 0; i < 2; i++)
        {
            const float y = by + i * (h + 18);
            HudPanel(c, ui, sp, i == sel ? "panels/row_selected" : "panels/row", areaX, y, areaW, h);
            if (i == sel) HudSprite(c, ui, sp, "cursor/cursor", areaX + 20, y + 23, 36, 40);
            HudText(c, ui, f, areaX + areaW / 2, y + 23, 40, a[i], Ink, 1);
        }
    }
    DrawHintBar(c, ui, sp, f, hints);
}

// Draws into `c`, redrawing only when something shown changes: the last picture is kept in a
// layer of its own and put back (the panels take a good part of a frame to draw).
class Hud
{
public:
    void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, Moves& mv, RomArt& art, const State& s, int sel,
        const std::vector<HudHint>& hints, float top)
    {
        std::string k = s.Key() + "|" + std::to_string(sel) + "|" + std::to_string(c.w) + "x" + std::to_string(c.h) + "|" + std::to_string(top);
        for (const HudHint& h : hints)
            for (const std::string& g : h.glyphs) k += "|" + g + h.label;
        if (k != key)
        {
            key = k;
            layer.Resize(c.w, c.h);
            std::fill(layer.px.begin(), layer.px.end(), 0);
            DrawAll(layer, ui, sp, f, mv, art, s, sel, hints, top);
        }
        // The band is opaque: copied.
        const int y0 = std::clamp((int)ui(top), 0, c.h);
        std::copy(layer.px.begin() + (size_t)y0 * c.w, layer.px.end(), c.px.begin() + (size_t)y0 * c.w);
        c.Mark(0, y0, c.w, c.h);
    }

private:
    Canvas layer;
    std::string key;
};
}
