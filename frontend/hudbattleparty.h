#pragma once
// The party of a battle (the touch screen's, overlay 8) redrawn as the party outside
// battles is (hudparty.h): the list of Pokémon, the choice for one (Shift, Summary, Check
// moves) and its summary. The game keeps the input; everything is read from its memory each
// frame. The overlay is not decompiled; its data is laid out as Platinum's battle party
// (pokeplatinum battle_sub_menus/battle_party.h), at these places in HeartGold and
// SoulSilver:
//  - the context: the party (+0), the BattleSystem (+8), the Pokémon chosen (+0x11);
//  - the party screen: the context (+0), each Pokémon as shown (+4, 0x50 each: the Pokémon
//    +0, the HP +0x10, the level +0x16), its message boxes (+0x2050 and +0x2060, Windows),
//    the screen (+0x207A: 0 the list, 1 a Pokémon's choice, 2 its summary, 3 on its moves
//    and the rest) and its cursor (+0x2088: shown at +8, place at +9).
// The moves' screens and those for learning a move or restoring PP stay the game's own.
#include "hudbattleui.h"
#include "hudparty.h"

namespace HudBattleParty
{
using HudBattleUi::Mem;

struct State
{
    int screen = 0, cursor = -1; // the game's screen, and its button under the cursor (-1 hidden)
    int chosen = 0;              // the Pokémon chosen (its place on the list)
    HudParty::PartyScreen list;  // as hudparty.h draws it
};

class Reader
{
public:
    // The bottom screen's video memory (for the message boxes) and the reader of their text.
    HudBattleUi::Vram vram;
    const HudBattleUi::WindowReader* words = nullptr;

    // Finds the party screen's data (searching the memory at most every 15 frames while it
    // is not found) and reads it; false if no battle party is open or a screen it does not
    // redraw is up.
    bool Read(const uint8_t* ram, uint32_t mask, unsigned frame, State& s)
    {
        const Mem m{ ram, mask };
        const uint32_t bs = HudBattleUi::Battle(m);
        if (!bs) return false;
        // A block of the game's heap in use has "UD" 32 bytes before it (see hudbattlebag.h).
        auto live = [&](uint32_t a) { return m.r16(a - 0x20) == 0x5544; };
        auto ctxOk = [&](uint32_t c) { return Mem::In(c) && live(c) && m.r32(c + 8) == bs && m.r32(m.r32(c)) == 6; };
        auto good = [&](uint32_t o) {
            const uint32_t sp = m.r16(o + 8);
            return Mem::In(o) && live(o) && ctxOk(m.r32(o)) && sp >= 1 && sp <= 493 && m.r8(o + 0x207A) <= 9 && Mem::In(m.r32(o + 0x2088));
        };
        if (!good(obj))
        {
            obj = 0;
            if (frame - lastSearch < 15) return false;
            lastSearch = frame;
            for (uint32_t c = 0x02000000; c < 0x02400000 && !obj; c += 4)
                if (m.r32(c + 8) == bs && ctxOk(c))
                    for (uint32_t o = 0x02000000; o < 0x02400000; o += 4)
                        if (m.r32(o) == c && good(o))
                        {
                            obj = o;
                            break;
                        }
            if (!obj) return false;
        }
        const uint32_t ctx = m.r32(obj), party = m.r32(ctx), cur = m.r32(obj + 0x2088);
        s.screen = (int)m.r8(obj + 0x207A);
        if (s.screen > 2) return false;
        s.cursor = m.r8(cur + 8) ? (int)m.r8(cur + 9) : -1;
        // The Pokémon as the battle lists them (its own on the field first), each the party's
        // Pokémon its entry points to, with the HP and level the battle has.
        PartyMon mons[6];
        int count = 0;
        if (!HudParty::ReadParty({ ram, mask + 1 }, party, count, mons)) return false;
        HudParty::PartyScreen& l = s.list;
        l = HudParty::PartyScreen();
        l.count = count;
        for (int i = 0; i < count; i++)
        {
            const uint32_t e = obj + 4 + 0x50 * i, mon = m.r32(e);
            const int slot = (int)(mon - (party + 8)) / 236;
            if (mon < party + 8 || slot >= count || (mon - (party + 8)) % 236) return false;
            l.mons[i] = mons[slot];
            l.mons[i].hp = (int)m.r16(e + 0x10);
            l.mons[i].level = (int)(m.r8(e + 0x16) & 0x7F);
            if (l.mons[i].species != (int)m.r16(e + 4) || l.mons[i].hp > l.mons[i].stats[0]) return false;
        }
        s.chosen = std::min((int)m.r8(ctx + 0x11), count - 1);
        // The message boxes: the prompt at the top of the list, or what the game says.
        std::string said;
        for (uint32_t w = 0x2050; w <= 0x2060 && words; w += 0x10)
        {
            HudBattleUi::Window win;
            std::vector<HudBattleUi::TextLine> lines;
            if (!win.Read(m, obj + w, vram) || std::count(win.px.begin(), win.px.end(), 1) <= 8 || !words->Read(win, lines)) continue;
            std::string t;
            for (const auto& ln : lines)
                if (ln.y >= 0)
                    for (const auto& p : ln.pieces) t += (t.empty() ? "" : " ") + p.text;
            if (!t.empty()) said = t;
        }
        if (s.screen == 0)
        {
            l.cursor = s.cursor < 0 ? 0 : s.cursor;
            if (said.size() > 32) l.message = HudParty::PokemonCase(said);
            else if (!said.empty()) l.prompt = HudParty::PokemonCase(said);
        }
        else if (s.screen == 1)
        {
            l.cursor = s.chosen;
            l.items = { "Shift", "Summary", "Check moves", "Cancel" };
            l.sel = std::max(0, s.cursor);
        }
        return true;
    }

private:
    uint32_t obj = 0;
    unsigned lastSearch = 0u - 100;
};
}
