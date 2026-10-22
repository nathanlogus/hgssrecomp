#pragma once
// The Union Room's trade screen (online-HUD design 10-trade) redrawn in the HUD style while
// the game's own application (WirelessTradeSelectMon, overlay 65) runs: both parties side by
// side, the Pokemon each cursor is on in a card in the middle, the game's menu and yes/no
// along the bottom. The game keeps the input; each frame mirrors what its application holds
// in memory. Whenever anything does not check out, or the game is on a step this screen does
// not draw (registering a friend in the Pal Pad, the trade itself), nothing is drawn and the
// game's own picture shows. The Poke Balls flying across are the game's (TradeSequence).
//
// Overlay 65 is not decompiled; its layout is that of Platinum's trade_room
// (pokeplatinum src/applications/trade_room), read off asm/overlay_65.s.
#include "hudparty.h"

namespace HudTrade
{
struct View
{
    int count[2] = {};
    PartyMon mons[2][6];
    int cursor = 0;                  // this player's: 0-5 their Pokemon, 6-11 the other's, 12 Cancel
    int theirs = 6;                  // the other player's (6-11)
    enum Step { Browse, Menu, Quit, Waiting, Offered, Confirm, Notice } step = Browse;
    std::vector<std::string> items;  // the menu (Summary, Trade, Cancel), when open
    int sel = 0;                     // its cursor
    int yesNo = -1;                  // the yes/no's cursor, -1 none
    std::string message;             // what the game says
    bool ready = false;              // the other player has chosen (or said yes) first
    std::string me, partner;         // the trainers' names, as the game has them
};

// A trainer's name from their PlayerProfile (the name in the game's text, then the id at
// +0x10); empty unless it reads.
inline std::string ProfileName(const HudParty::Ram& ram, uint32_t p)
{
    if (!ram.Ok(p, 0x14) || (p & 1)) return "";
    uint16_t codes[8];
    for (int i = 0; i < 8; i++) codes[i] = (uint16_t)ram.R16(p + 2 * i);
    return NdsText(codes, 8);
}

// The work of overlay 65 (0x36CC bytes): its arguments +8, the phase +0x54 (1 choosing, 5
// the summary, 2 closing), the two statuses each player sent (+0x6C, +0x70: 2 chose to trade,
// 3 said yes), the message shown with the notice step +0x74, the cursors +0x94 (this
// player's) and +0x98 (the other's), the touch-screen menu +0x36C8, the yes/no +0x36C0,
// the step's function +0x2220 and the parties +0x2224 (this player's, the save's) and
// +0x2228 (the other's, as received).
inline bool Read(const HudParty::Ram& ram, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, View& v)
{
    const uint32_t mgr = HudParty::FindApp(ram, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t w = ram.R32(m + 0x1C), args = ram.R32(m + 0x18);
        return ram.Ok(w, 0x36CC) && ram.R32(w + 8) == args && ram.R32(w + 0x2224) == ram.R32(args + 8);
    });
    // Running (step 1 of its main function), choosing (phase 1) or fading to and from the
    // summary (3, 4, 6, 7): the summary itself is the party screens' (hudparty.h).
    if (!mgr || ram.R32(mgr + 0x10) != 2 || ram.R32(mgr + 0x14) != 1) return false;
    const uint32_t w = ram.R32(mgr + 0x1C), args = ram.R32(mgr + 0x18);
    const uint32_t phase = ram.R32(w + 0x54);
    if (phase != 1 && phase != 3 && phase != 4 && phase != 6 && phase != 7) return false;
    if (!HudParty::ReadParty(ram, ram.R32(w + 0x2224), v.count[0], v.mons[0]) ||
        !HudParty::ReadParty(ram, ram.R32(w + 0x2228), v.count[1], v.mons[1]))
        return false;
    v.cursor = (int)ram.R32(w + 0x94);
    v.theirs = (int)ram.R32(w + 0x98);
    // The other's cursor is on one of their Pokemon or on their Cancel (12).
    if (v.cursor < 0 || v.cursor > 12 || (v.cursor < 12 && v.cursor % 6 >= v.count[v.cursor / 6]) ||
        v.theirs < 6 || (v.theirs >= 6 + v.count[1] && v.theirs != 12))
        return false;

    // The step, by its function in overlay 65.
    const uint32_t step = ram.R32(w + 0x2220) & ~1u;
    const int s0 = (int)ram.R32(w + 0x6C), s1 = (int)ram.R32(w + 0x70);
    switch (step)
    {
    case 0x0221E050: v.step = View::Browse; break;                     // choosing
    case 0x0221E9A8: case 0x0221EA74:                                   // a Pokemon of theirs: Summary, Trade, Cancel
    case 0x0221F3F4: case 0x0221F4D4: v.step = View::Menu; break;      // one of the other's: Summary, Cancel
    case 0x0221E144: case 0x0221E194: v.step = View::Quit; break;      // "Cancel trading?"
    case 0x0221E8EC: v.step = View::Waiting; v.message = "Waiting for your friend to finish..."; break;
    case 0x0221EB9C: case 0x0221EBF8: v.step = View::Waiting; break;   // waiting for the other to choose
    case 0x0221EC34: case 0x0221EE18: v.step = View::Offered; break;   // the two Pokemon brought together
    case 0x0221F028: case 0x0221F120: v.step = View::Confirm; break;   // "Trade X in return for Y?"
    case 0x0221F258: case 0x0221F2A8:                                   // Yes with a Ball Capsule on it
        v.step = View::Confirm;
        v.message = Party_Text(183, 0x24); // "The Ball Capsule will become detached. Is that OK?"
        break;
    // Waiting for the other's answer. (With a Bad Egg in either party the game says here that a
    // Pokemon cannot be traded, 0x25 or 0x26; such a party does not read, so the game's own
    // picture shows.)
    case 0x0221F354: v.step = View::Offered; break;
    case 0x0221E270: case 0x0221E2C0:                                   // a notice
    {
        // "The trade has been canceled.", "Your friend apparently wants to trade..." and "If
        // you trade that Pokemon, you'll get stuck..." (all this player's others are Eggs).
        const int id = (int)ram.R32(w + 0x74);
        if (id != 0x18 && id != 0x1D && id != 0x22) return false;
        v.step = View::Notice;
        v.message = Party_Text(183, id);
        if (v.message.empty()) return false;
        break;
    }
    default: return false; // registering a friend, the trade itself
    }
    for (char& ch : v.message)
        if (ch == '\n') ch = ' ';
    // The cursor stays on the other's Pokemon only to look at it; a trade is one of this
    // player's for one of theirs.
    if ((v.step == View::Offered || v.step == View::Confirm) && (v.cursor >= 6 || v.theirs == 12)) return false;
    // The other player is ahead: chose to trade while this one still looks, or said yes.
    if (v.step == View::Browse || v.step == View::Menu) v.ready = (s0 == 2) != (s1 == 2) && (s0 == 0 || s1 == 0);
    if (v.step == View::Confirm) v.ready = (s0 == 3) != (s1 == 3) && (s0 == 0 || s1 == 0);

    if (v.step == View::Menu)
    {
        // TouchscreenListMenu: its items +0x10 (a String and a value each), how many +0x18,
        // the cursor +0x24. Until it is made (one frame), the screen without it.
        const uint32_t m = ram.R32(w + 0x36C8);
        const int n = ram.Ptr(m) ? (int)ram.R8(m + 0x18) : 0;
        const uint32_t list = ram.R32(m + 0x10);
        v.sel = (int)ram.R8(m + 0x24);
        if (n < 2 || n > 3 || v.sel >= n || !ram.Ptr(list)) v.step = View::Browse;
        else
            for (int i = 0; i < n; i++)
            {
                std::string t;
                if (!HudParty::ReadString(ram, ram.R32(list + 8 * i), t)) return false;
                v.items.push_back(HudScreens::TitleCase(t));
            }
    }
    if (v.step == View::Quit || v.step == View::Confirm)
    {
        // As overlay 65 places its YesNoPrompt (26, 10); the cursor +0x75.
        const uint32_t yn = ram.R32(w + 0x36C0);
        if (ram.Ptr(yn) && ram.R8(yn + 0x70) == 26 && ram.R8(yn + 0x71) == 10 && ram.R8(yn + 0x75) <= 1) v.yesNo = (int)ram.R8(yn + 0x75);
    }

    // The names: this player's from the arguments (+4), the other's from the link's own
    // table of trainers (sub_02034818: *0x021D4130, a profile +0x34C and whether that
    // player is there +0x39C, for each).
    v.me = ProfileName(ram, ram.R32(args + 4));
    const uint32_t myId = ram.R32(ram.R32(args + 4) + 0x10), link = ram.R32(0x021D4130);
    for (int n = 0; ram.Ptr(link) && n < 5 && v.partner.empty(); n++)
    {
        const uint32_t there = ram.R8(link + 0x39C + n), p = ram.R32(link + 0x34C + 4 * n);
        if (there >= 1 && there <= 3 && ram.Ptr(p) && ram.R32(p + 0x10) != myId) v.partner = ProfileName(ram, p);
    }
    if (v.me.empty()) return false;
    if (v.partner.empty()) v.partner = "Your friend";
    return true;
}

// A Pokemon in a party's list: its icon, name and level.
inline void Row(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, float w, const PartyMon* m, int look,
    const HudParty::MonIconFn& icon)
{
    using namespace HudColour;
    const float h = 72;
    if (!m)
    {
        HudSprite(c, ui, sp, "panels/slot_grid_empty", x, y, w, h);
        HudText(c, ui, f, x + 28, y + 20, 32, "-", Muted);
        return;
    }
    // 1: this player's cursor (yellow), 2: the other's (green).
    const uint32_t edge = look == 1 ? 0xFFF0B820 : look == 2 ? 0xFF4CB050 : 0xFFDDE3EC;
    const uint32_t fill = look == 1 ? 0xFFFFF0A0 : look == 2 ? 0xFFE6F6E0 : 0xFFFFFFFF;
    HudParty::FillRound(c, ui, x, y, w, h, 14, edge);
    HudParty::FillRound(c, ui, x + 3, y + 3, w - 6, h - 6, 12, fill);
    if (look == 1) HudSprite(c, ui, sp, "cursor/cursor", x + 14, y + 16, 36, 40);
    HudPixelArtCentred(c, ui, icon(*m), x + 88, y + 36, 0, 64);
    HudText(c, ui, f, x + 130, y + 20, 32, m->egg ? "Egg" : m->name, Ink);
    if (!m->egg) HudText(c, ui, f, x + w - 24, y + 20, 32, "Lv " + std::to_string(m->level), Muted, 2);
}

// The card in the middle: who offers (or looks at) which Pokemon.
inline void Offer(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, float w, float h, uint32_t edge,
    const std::string& label, const PartyMon* m, const HudParty::MonIconFn& icon)
{
    using namespace HudColour;
    HudParty::FillRound(c, ui, x, y, w, h, 24, edge);
    HudParty::FillRound(c, ui, x + 5, y + 5, w - 10, h - 10, 20, 0xFFFFFFFF);
    HudText(c, ui, f, x + w / 2, y + 24, 32, label, Muted, 1);
    if (!m) return;
    HudPixelArtCentred(c, ui, icon(*m), x + w / 2, y + 124, 0, 128);
    const std::string name = m->egg ? "Egg" : m->name, lv = m->egg ? "" : "  Lv " + std::to_string(m->level);
    const float nw = HudTextWidth(ui, f, 48, name) + HudTextWidth(ui, f, 48, lv);
    HudText(c, ui, f, x + w / 2 - nw / 2, y + 190, 48, name, Ink);
    if (!m->egg)
    {
        HudText(c, ui, f, x + w / 2 - nw / 2 + HudTextWidth(ui, f, 48, name), y + 190, 48, lv, Muted);
        HudParty::Gender(c, ui, x + w / 2 + nw / 2 + 12, y + 202, m->gender);
        if (m->shiny) HudParty::Star(c, ui, x + w / 2 - nw / 2 - 24, y + 218, 14);
        const std::string more = HudScreens::TitleCase(Party_Text(720, m->ability)) + " \xC2\xB7 " +
            (m->item ? Party_Text(222, m->item) : std::string("no item"));
        HudText(c, ui, f, x + w / 2, y + 248, 32, more, Muted, 1);
    }
}

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const View& v, int myColour, int theirColour,
    const HudParty::MonIconFn& icon, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;

    // Who trades with whom, at the top.
    const float pw1 = HudTextWidth(ui, f, 32, v.me) + 56, pw2 = HudTextWidth(ui, f, 32, v.partner) + 56;
    const float bw = pw1 + pw2 + 140, bx = wd / 2 - bw / 2;
    HudParty::FillRound(c, ui, bx, 30, bw, 70, 35, 0xFFFFFFFF);
    DrawSprite(c, sp.Get(HudOnline::PillOf(myColour)), ui(bx + 20), ui(43), ui(pw1), ui(44), ui.s);
    HudText(c, ui, f, bx + 20 + pw1 / 2, 49, 32, v.me, White, 1);
    HudSprite(c, ui, sp, "icons/icon_trade", bx + 20 + pw1 + 28, 41, 48, 48);
    DrawSprite(c, sp.Get(HudOnline::PillOf(theirColour)), ui(bx + bw - 20 - pw2), ui(43), ui(pw2), ui(44), ui.s);
    HudText(c, ui, f, bx + bw - 20 - pw2 / 2, 49, 32, v.partner, White, 1);

    // The two parties.
    const bool choosing = v.step == View::Browse || v.step == View::Menu || v.step == View::Quit;
    for (int side = 0; side < 2; side++)
    {
        const float px = side ? x0 + 1260 : x0 + 100, pw = 560;
        HudPanel(c, ui, sp, "panels/card", px, 140, pw, 565);
        HudParty::FillRound(c, ui, px + 20, 160, pw - 40, 44, 14, HudOnline::TextColourOf(side ? theirColour : myColour));
        std::string title = side ? v.partner + "'s party" : "Your party";
        for (char& ch : title) ch = (char)toupper((unsigned char)ch);
        HudText(c, ui, f, px + 40, 166, 32, title, White);
        for (int i = 0; i < 6; i++)
        {
            const int slot = side * 6 + i;
            const int look = choosing && v.cursor == slot ? 1 : (side ? v.theirs == slot : !choosing && v.cursor == slot) ? 2 : 0;
            Row(c, ui, sp, f, px + 20, 214 + i * 82, pw - 40, i < v.count[side] ? &v.mons[side][i] : nullptr, look, icon);
        }
    }
    // Cancel (the game's Quit), under this player's party.
    {
        const bool on = v.cursor == 12;
        HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", x0 + 120, 730, 300, 60);
        if (on) HudSprite(c, ui, sp, "cursor/cursor", x0 + 136, 740, 36, 40);
        HudText(c, ui, f, x0 + 270, 744, 32, "Cancel", Ink, 1);
    }

    // The offers in the middle: this player's choice and the other's.
    const bool offered = v.step == View::Offered || v.step == View::Confirm;
    const PartyMon* mine = v.cursor < 12 ? &v.mons[v.cursor / 6][v.cursor % 6] : nullptr;
    const std::string myLabel = v.cursor == 12 ? "Cancel" : offered || v.step == View::Waiting ? "You offer" : v.cursor >= 6 && v.cursor < 12 ? "You are looking at" : "Your choice";
    Offer(c, ui, sp, f, x0 + 710, 150, 500, 285, HudOnline::TextColourOf(myColour), myLabel, mine, icon);
    HudSprite(c, ui, sp, "icons/icon_arrow_up", x0 + 916, 456, 40, 40);
    HudSprite(c, ui, sp, "icons/icon_arrow_down", x0 + 964, 456, 40, 40);
    const PartyMon* theirMon = v.theirs < 12 ? &v.mons[1][v.theirs - 6] : nullptr;
    Offer(c, ui, sp, f, x0 + 710, 515, 500, 285, HudOnline::TextColourOf(theirColour),
        !theirMon ? v.partner + " is on Cancel" : offered || v.ready ? v.partner + " offers" : v.partner + " is looking at", theirMon, icon);

    // Along the bottom: whether the other is ready, what the game says, and its menu or yes/no.
    const float by = 860, bh = 110;
    HudPanel(c, ui, sp, "panels/card", x0 + 100, by, 1720, bh);
    float tx = x0 + 136;
    if (v.ready)
    {
        HudSprite(c, ui, sp, "icons/icon_check", tx, by + 33, 44, 44);
        const std::string r = v.partner + (v.step == View::Confirm ? " said yes" : " is ready");
        HudText(c, ui, f, tx + 56, by + 39, 32, r, 0xFF2E9E48);
        tx += 56 + HudTextWidth(ui, f, 32, r) + 40;
    }
    std::string say = v.message;
    if (say.empty())
        switch (v.step)
        {
        case View::Browse: say = v.cursor == 12 ? "Stop trading?" : "Choose a Pok\xC3\xA9mon to trade."; break;
        case View::Menu: say = "Do what with " + (mine ? mine->name : std::string("it")) + "?"; break;
        case View::Quit: say = "Cancel trading?"; break;
        case View::Waiting: say = "Waiting for " + v.partner + "..."; break;
        case View::Offered: say = "Communicating..."; break;
        case View::Confirm: say = "Trade " + (mine ? mine->name : std::string()) + " for " + v.mons[1][v.theirs - 6].name + "?"; break;
        default: break;
        }
    std::vector<std::string> choices = v.items;
    int on = v.sel;
    if (v.yesNo >= 0) choices = { "Yes", "No" }, on = v.yesNo;
    float right = x0 + 1820 - 30;
    for (int k = (int)choices.size() - 1; k >= 0; k--)
    {
        const float w = std::max(200.0f, HudTextWidth(ui, f, 32, choices[k]) + 100);
        right -= w;
        HudPanel(c, ui, sp, k == on ? "panels/button_yellow" : "panels/button_white", right, by + 25, w, 60);
        if (k == on) HudSprite(c, ui, sp, "cursor/cursor", right + 14, by + 35, 36, 40);
        HudText(c, ui, f, right + w / 2 + (k == on ? 16 : 0), by + 39, 32, choices[k], Ink, 1);
        right -= 20;
    }
    const std::vector<std::string> lines = HudWrap(ui, f, 32, say, right - tx - 20);
    for (size_t i = 0; i < lines.size() && i < 2; i++)
        HudText(c, ui, f, tx, by + (lines.size() > 1 ? 18 : 39) + 40.0f * i, 32, lines[i], v.step == View::Notice ? Danger : 0xFFC07A10);
    DrawHintBar(c, ui, sp, f, hints);
}
}
