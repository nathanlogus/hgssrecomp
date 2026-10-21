#pragma once
// The online HUD (online-HUD design: 03-overworld, 06-lobby, 07-toasts, 11-menu-title,
// 16-menu-session): the session badge with the signal bars, the player chips, the card
// shown while a lost connection is retried, the session screen (players and the code),
// code entry, the start screen and the summary shown on leaving. Data from party.h.
#include <cmath>
#include <functional>
#include <map>
#include "hudtoast.h"
#include "party.h"
#include "romart.h"

namespace HudOnline
{
// A species' icon (from the ROM), or null.
using IconFn = std::function<const Image*(int species)>;
inline const char* RingOf(int colour)
{
    static const char* rings[] = { "people/chip_ring_blue", "people/chip_ring_green", "people/chip_ring_orange", "people/chip_ring_red" };
    return rings[colour & 3];
}
// Player colours for text on white (darker than the rings, to read well).
inline uint32_t TextColourOf(int colour)
{
    static const uint32_t c[] = { 0xFF3D6FD1, 0xFF2F9E3F, 0xFFC87A1E, 0xFFC83C3C };
    return c[colour & 3];
}
inline const char* ColourName(int colour)
{
    static const char* n[] = { "Blue", "Green", "Orange", "Red" };
    return n[colour & 3];
}

inline std::string PillOf(int colour)
{
    static const char* n[] = { "people/name_pill_blue", "people/name_pill_green", "people/name_pill_orange", "people/name_pill_red" };
    return n[colour & 3];
}

// "Nathan" -> "Na", "AJ" -> "AJ".
inline std::string Initials(const std::string& name)
{
    if (name.empty()) return "?";
    std::string s = name.substr(0, 1);
    for (size_t i = 1; i < name.size(); i++)
        if (name[i - 1] == ' ' && name[i] != ' ') return s + name[i];
    if (name.size() > 1) s += name[1];
    return s;
}

inline const char* SignalOf(int rtt, bool lost)
{
    if (lost || rtt < 0) return "bars/signal_lost";
    return rtt < 80 ? "bars/signal_good" : rtt < 200 ? "bars/signal_fair" : "bars/signal_poor";
}

// A player's chip: their initials in a ring of their colour, `size` design pixels.
inline void Chip(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, float size, const PartyPeer& p)
{
    HudSprite(c, ui, sp, RingOf(p.colour), x, y, size, size);
    HudText(c, ui, f, x + size / 2, y + size / 2 - 16, 32, Initials(p.name), HudColour::Ink, 1);
}

// The navy session badge: ONLINE, players, signal bars and the round trip.
inline void SessionBadge(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, bool showPing)
{
    const PartyLink link = Party_Link();
    const bool lost = link == PartyLink::Lost;
    const std::string label = link == PartyLink::Connecting ? "JOINING" : lost ? "OFFLINE" : "ONLINE";
    // The session's code too, while there is room for more: it is what others need to join.
    const std::string count = std::to_string(Party_Count()) + "/" + std::to_string(Party_Max()) +
        (link == PartyLink::Online && Party_Count() < Party_Max() && !Party_Code().empty() ? "  " + Party_Code() : "");
    const int rtt = Party_RttMs();
    const std::string ms = !showPing ? "" : lost ? "- -" : rtt < 0 ? "..." : std::to_string(rtt) + " ms";
    float w = 20 + HudTextWidth(ui, f, 32, label) + 14 + HudTextWidth(ui, f, 32, count) + 14 + 40 + (ms.empty() ? 0 : 14 + HudTextWidth(ui, f, 32, ms)) + 20;
    const float h = 60;
    // The badge's shadow is its own: a 6 px darker strip under it.
    FillBlend(c, ui(x + 6), ui(y + h - 4), ui(w - 12), ui(10), 0xFF0B1222);
    HudSprite(c, ui, sp, "panels/session_badge", x - 2, y - 2, w + 4, h + 4);
    float cx = x + 20;
    HudText(c, ui, f, cx, y + 14, 32, label, 0xFFFFD75E);
    cx += HudTextWidth(ui, f, 32, label) + 14;
    HudText(c, ui, f, cx, y + 14, 32, count, HudColour::White);
    cx += HudTextWidth(ui, f, 32, count) + 14;
    HudSprite(c, ui, sp, SignalOf(rtt, lost), cx, y + 14, 40, 32);
    cx += 40 + 14;
    if (!ms.empty()) HudText(c, ui, f, cx, y + 14, 32, ms, 0xFFC8D0E0);
}

// The player chips in a white tray at the top right: a crown over the host, a burst on
// whoever is battling, the free places as dashed rings.
inline void ChipTray(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float right, float y)
{
    const PartyPeer* peers = Party_Peers();
    const int max = std::max(Party_Max(), Party_Count());
    const float chip = 76, gap = 14, w = 24 + max * chip + (max - 1) * gap + 24, h = 100;
    const float x = right - w;
    HudPanel(c, ui, sp, "panels/card_glass", x, y, w, h);
    float cx = x + 24;
    int shown = 0;
    for (int i = 0; i < 4 && shown < max; i++)
    {
        if (!peers[i].present) continue;
        Chip(c, ui, sp, f, cx, y + 12, chip, peers[i]);
        if (peers[i].host) HudSprite(c, ui, sp, "people/crown_gold", cx + chip / 2 - 20, y - 16, 40, 37);
        if (peers[i].state == PS_BATTLE) HudSprite(c, ui, sp, "emotes/emote_battle", cx + chip - 30, y + 46, 40, 40);
        cx += chip + gap;
        shown++;
    }
    for (; shown < max; shown++)
    {
        HudSprite(c, ui, sp, "people/chip_empty", cx, y + 12, chip, chip);
        cx += chip + gap;
    }
}

// While a lost connection is retried: a card in the middle with the time left.
inline void LostCard(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::string& host)
{
    const float wd = ui.W / ui.s, w = 800, h = 300, x = (wd - w) / 2, y = 600;
    HudPanel(c, ui, sp, "panels/dialogue_box", x, y, w, h);
    const std::string title = "Connection lost";
    const float tw = HudTextWidth(ui, f, 48, title);
    HudSprite(c, ui, sp, "emotes/emote_exclaim", x + w / 2 - tw / 2 - 72, y + 28, 60, 60);
    HudText(c, ui, f, x + w / 2 - tw / 2 + 4, y + 40, 48, title, HudColour::Ink);
    const float t = Party_LostSeconds();
    const int left = std::max(0, 30 - (int)t);
    const std::vector<std::string> lines = HudWrap(ui, f, 32,
        "Reconnecting to " + (host.empty() ? std::string("the session") : host + "'s session") + "... " + std::to_string(left) +
            " s. You can keep playing.", w - 80);
    for (size_t i = 0; i < lines.size() && i < 2; i++)
        HudText(c, ui, f, x + w / 2, y + 112 + 44.0f * i, 32, lines[i], HudColour::Ink, 1);
    const float bw = 600, bx = x + (w - bw) / 2, by = y + 220;
    HudSprite(c, ui, sp, "bars/bar_frame", bx, by, bw, 22);
    HudSprite(c, ui, sp, "bars/fill_purple", bx + 6, by + 4, std::max(16.0f, (bw - 12) * std::min(1.0f, t / 30)), 14);
}

// A card with a teal title band (card_header_teal: the band is its top 66 design pixels).
inline void TitledCard(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, float w, float h, const std::string& title,
    const std::string& right = "")
{
    HudPanel(c, ui, sp, "panels/card_header_teal", x, y, w, h);
    HudText(c, ui, f, x + 24, y + 13, 32, title, HudColour::White);
    if (!right.empty()) HudText(c, ui, f, x + w - 24, y + 13, 32, right, HudColour::White, 2);
}

// The session screen (06-lobby): the party's code and settings on the left, the players on
// the right.
inline void SessionScreen(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Menu& m, const std::vector<HudHint>& hints,
    const IconFn& icon)
{
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float wd = ui.W / ui.s;
    HudSprite(c, ui, sp, "menu/menu_online_selected", 120, 50, 110, 110);
    HudText(c, ui, f, 250, 76, 64, Party_Link() == PartyLink::Online ? "Your session" : "Joining...", HudColour::White, 0, true);

    // Left: the settings, as menu rows (m: the rows of this screen), and the code.
    const float lx = 120, lw = 780, ly = 180;
    const std::vector<int> rows = m.Rows();
    const float lh = 66 + 16 + rows.size() * 64.0f + 80 + 16;
    TitledCard(c, ui, sp, f, lx, ly, lw, lh, "SETTINGS");
    float ry = ly + 66 + 16;
    for (int k = 0; k < (int)rows.size(); k++)
    {
        const MenuItem& it = m.items[rows[k]];
        const bool sel = k == m.selected;
        HudPanel(c, ui, sp, sel ? "panels/row_selected" : it.disabled ? "panels/row_disabled" : "panels/row", lx + 23, ry, lw - 46, 52);
        if (sel) HudSprite(c, ui, sp, "cursor/cursor", lx + 33, ry + 6, 36, 40);
        HudText(c, ui, f, lx + 23 + 60, ry + 10, 32, it.label, it.danger ? HudColour::Danger : it.disabled ? HudColour::Muted : HudColour::Ink);
        HudText(c, ui, f, lx + lw - 23 - 30, ry + 10, 32, it.value, it.disabled ? HudColour::Muted : HudColour::Ink, 2);
        ry += 64;
    }
    // The code, on navy like the session badge.
    const float cy = ry + 6;
    FillBlend(c, ui(lx + 23), ui(cy), ui(lw - 46), ui(60), 0xFF1C2A44);
    HudText(c, ui, f, lx + 45, cy + 14, 32, "Code", 0xFFC8D0E0);
    HudText(c, ui, f, lx + 130, cy + 4, 48, Party_Code().empty() ? "..." : Party_Code(), 0xFFFFD75E);

    // Right: the players.
    const PartyPeer* peers = Party_Peers();
    const int max = std::max(Party_Max(), Party_Count());
    const float rx = 940, rw = wd - rx - 120, rh = 66 + 16 + max * 98.0f + 16;
    TitledCard(c, ui, sp, f, rx, ly, rw, rh, "PLAYERS", std::to_string(Party_Count()) + " / " + std::to_string(Party_Max()));
    float py = ly + 66 + 16;
    int shown = 0;
    for (int i = 0; i < 4 && shown < max; i++)
    {
        if (!peers[i].present) continue;
        const PartyPeer& p = peers[i];
        HudPanel(c, ui, sp, p.host ? "panels/row_gold" : i == Party_Me() ? "panels/row_teal" : "panels/row", rx + 23, py, rw - 46, 86);
        Chip(c, ui, sp, f, rx + 43, py + 7, 72, p);
        HudText(c, ui, f, rx + 135, py + 8, 32, p.name.empty() ? "..." : p.name, HudColour::Ink);
        std::string info = p.host ? "Host" : "";
        const std::string badges = std::to_string(p.badges) + (p.badges == 1 ? " badge" : " badges");
        info += (info.empty() ? "" : " \xC2\xB7 ") + badges;
        if (p.rtt >= 0 && i != Party_Me()) info += " \xC2\xB7 " + std::to_string(p.rtt) + " ms";
        if (i == Party_Me()) info += " \xC2\xB7 you";
        HudText(c, ui, f, rx + 135, py + 44, 32, info, HudColour::Muted);
        if (p.host) HudSprite(c, ui, sp, "people/crown_gold", rx + rw - 23 - 80, py + 18, 56, 52);
        else if (p.state == PS_BATTLE) HudSprite(c, ui, sp, "emotes/emote_battle", rx + rw - 23 - 80, py + 8, 64, 64);
        if (p.partner && icon) HudPixelArt(c, ui, icon(p.partner), rx + rw - 23 - 170, py + 3, 80, 80);
        py += 98;
        shown++;
    }
    for (; shown < max; shown++)
    {
        HudPanel(c, ui, sp, "panels/row_disabled", rx + 23, py, rw - 46, 86);
        HudSprite(c, ui, sp, "people/chip_empty", rx + 43, py + 7, 72, 72);
        HudText(c, ui, f, rx + 135, py + 27, 32, "Open slot \xC2\xB7 share the code", HudColour::Muted);
        py += 98;
    }
    DrawHintBar(c, ui, sp, f, hints);
}

// Code entry: SSR- and four cells; up and down change the letter of the chosen cell.
inline void CodeEntry(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::string& code, int cell, const std::string& title,
    const std::string& note, float x, float y)
{
    const float w = 640, h = 330;
    HudPanel(c, ui, sp, "panels/dialogue_box", x, y, w, h);
    HudText(c, ui, f, x + 26, y + 26, 32, title, HudColour::Ink);
    float cx = x + 84;
    const std::string prefix = "SSR";
    for (int i = 0; i < 3; i++)
    {
        HudSprite(c, ui, sp, "panels/code_cell", cx, y + 70, 54, 64);
        HudText(c, ui, f, cx + 27, y + 86, 32, std::string(1, prefix[i]), HudColour::Ink, 1);
        cx += 64;
    }
    HudText(c, ui, f, cx + 4, y + 86, 32, "-", HudColour::Muted);
    cx += 30;
    for (int i = 0; i < 4; i++)
    {
        HudSprite(c, ui, sp, i == cell ? "panels/code_cell_selected" : "panels/code_cell", cx, y + 70, 54, 64);
        const std::string ch = i < (int)code.size() ? code.substr(i, 1) : "_";
        HudText(c, ui, f, cx + 27, y + 86, 32, ch, HudColour::Ink, 1);
        cx += 64;
    }
    const std::vector<std::string> lines = HudWrap(ui, f, 32, note, w - 52);
    for (size_t i = 0; i < lines.size() && i < 2; i++)
        HudText(c, ui, f, x + 26, y + 160 + 40.0f * i, 32, lines[i], HudColour::Muted);
    HudPanel(c, ui, sp, "panels/button_yellow", x + 26, y + 250, 150, 56);
    HudSprite(c, ui, sp, "buttons/button_a", x + 34, y + 256, 44, 44);
    HudText(c, ui, f, x + 90, y + 262, 32, "Join", HudColour::Ink);
}

// Typing a short text with the controller: up and down change the letter at the cursor,
// left and right move it.
inline void TextEntry(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::string& title, const std::string& text, int cursor,
    const std::string& note, const std::vector<HudHint>& hints)
{
    const float wd = ui.W / ui.s, w = 900, h = 300, x = (wd - w) / 2, y = 330;
    FillBlend(c, 0, 0, (float)c.w, (float)c.h, 0x80000000);
    HudPanel(c, ui, sp, "panels/dialogue_box", x, y, w, h);
    HudText(c, ui, f, x + 32, y + 26, 48, title, HudColour::Ink);
    const float fx = x + 32, fy = y + 100, fw = w - 64, fh = 64;
    HudPanel(c, ui, sp, "panels/row_selected", fx, fy, fw, fh);
    HudText(c, ui, f, fx + 24, fy + 16, 32, text, HudColour::Ink);
    // The cursor: a bar under the letter it is on (or after the text).
    const float cx = fx + 24 + HudTextWidth(ui, f, 32, text.substr(0, cursor));
    const float cw = cursor < (int)text.size() ? std::max(10.0f, HudTextWidth(ui, f, 32, text.substr(cursor, 1))) : 16;
    FillBlend(c, ui(cx), ui(fy + 50), ui(cw), ui(4), HudColour::Ink);
    const std::vector<std::string> lines = HudWrap(ui, f, 32, note, w - 64);
    for (size_t i = 0; i < lines.size() && i < 2; i++)
        HudText(c, ui, f, x + 32, y + 190 + 40.0f * i, 32, lines[i], HudColour::Muted);
    DrawHintBar(c, ui, sp, f, hints);
}

// The start screen (11-menu-title): big discs; the selected one larger with the halo.
struct StartItem
{
    std::string label, icon;
};
// The game's logo (assets/logo.png) centred at the top, `h` design pixels tall, or the
// title in text without it.
inline float DrawLogo(Canvas& c, const Ui& ui, const RomFont& f, const Image* logo, const std::string& title, float y, float h)
{
    const float wd = ui.W / ui.s;
    if (!logo || logo->px.empty())
    {
        HudText(c, ui, f, wd / 2, y + h / 2 - 40, 80, title, HudColour::Ink, 1, true);
        return y + h;
    }
    const float w = h * logo->w / logo->h;
    BlitShadow(c, *logo, ui(wd / 2 - w / 2), ui(y), ui(w), ui(h));
    Blit(c, *logo, 0, 0, (float)logo->w, (float)logo->h, ui(wd / 2 - w / 2), ui(y), ui(w), ui(h));
    return y + h;
}

// The save on the start screen (17-save): the trainer and their first Pokemon on the
// left, the numbers in rows, the badges on the right.
inline void SaveCard(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, float w, const std::string& name,
    const PartyCard& card, const Image* trainer, const Image* partner, const std::string& note = "")
{
    const float h = 400, lw = 260;
    // A line under the card (where Continue resumes from, autosave.h).
    if (!note.empty()) HudText(c, ui, f, x + w / 2, y + h + 22, 32, note, HudColour::Muted, 1);
    HudPanel(c, ui, sp, "panels/card", x, y, w, h);
    FillBlend(c, ui(x + 24), ui(y + 24), ui(lw), ui(h - 48), 0xFFFDF6E4);
    if (trainer) HudPixelArt(c, ui, trainer, x + 24 + lw / 2 - 96, y + 40, 192, 192);
    if (partner) HudPixelArt(c, ui, partner, x + 24 + lw / 2 - 64, y + 236, 128, 128);
    char id[16], time[16];
    snprintf(id, sizeof id, "%05u", card.id & 0xFFFF);
    snprintf(time, sizeof time, "%d:%02d", card.hours, card.minutes);
    std::string money = std::to_string(card.money);
    for (int i = (int)money.size() - 3; i > 0; i -= 3) money.insert((size_t)i, ",");
    const std::pair<std::string, std::string> rows[] = { { "Name", name }, { "ID No.", id },
        { "Pok\xC3\xA9" "dex", std::to_string(std::max(0, card.dex)) + " Pok\xC3\xA9mon" }, { "Money", "$" + money }, { "Play time", time } };
    const float rx = x + 24 + lw + 30, rw = w - lw - 30 - 24 - 300;
    float ry = y + 34;
    for (const auto& r : rows)
    {
        HudText(c, ui, f, rx, ry, 32, r.first, HudColour::Muted);
        HudText(c, ui, f, rx + rw, ry, 32, r.second, HudColour::Ink, 2);
        FillBlend(c, ui(rx), ui(ry + 50), ui(rw), ui(2), 0xFFF1E3C8);
        ry += 68;
    }
    // Johto's badges, two by four.
    const float bx = x + w - 24 - 260;
    HudText(c, ui, f, bx + 130, y + 30, 32, "Badges", HudColour::Muted, 1);
    for (int i = 0; i < 8; i++)
    {
        const float cx = bx + 20 + (i % 2) * 120, cy = y + 76 + (i / 2) * 76, s = 68;
        if (card.johto >> i & 1)
        {
            HudSprite(c, ui, sp, "people/chip_ring_orange", cx + 10, cy, s, s);
            HudSprite(c, ui, sp, "icons/icon_star", cx + 10 + s / 2 - 20, cy + s / 2 - 20, 40, 40);
        }
        else
            HudSprite(c, ui, sp, "people/chip_ring_grey", cx + 10, cy, s, s, 0x80FFFFFF);
    }
}

inline void StartScreen(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::vector<StartItem>& items, int sel,
    const Image* logo, const std::string& title, const std::string& sub, const std::vector<HudHint>& hints, const std::string& footer,
    const std::function<void(float y)>& below = nullptr)
{
    DrawBackdrop(c, sp, "backgrounds/bg_sunny", 0xFFFFFFFF);
    const float wd = ui.W / ui.s;
    const float top = DrawLogo(c, ui, f, logo, title, 20, 230);
    if (!sub.empty()) HudText(c, ui, f, wd / 2, top + 4, 32, sub, 0xFF5B6270, 1);
    const float pitch = 260, x0 = wd / 2 - pitch * (items.size() - 1) / 2, iy = top + 150;
    for (size_t i = 0; i < items.size(); i++)
    {
        const bool s = (int)i == sel;
        const float d = s ? 150 : 116, cx = x0 + pitch * i;
        HudSprite(c, ui, sp, items[i].icon + (s ? "_selected" : ""), cx - d / 2, iy - d / 2, d, d);
        const float ly = iy + 116 / 2 + (s ? 26 : 12);
        if (s)
        {
            const float lw = HudTextWidth(ui, f, 32, items[i].label) + 32;
            HudPanel(c, ui, sp, "panels/tab", cx - lw / 2, ly - 2, lw, 40);
        }
        HudText(c, ui, f, cx, ly, 32, items[i].label, HudColour::Ink, 1);
    }
    if (below) below(iy + 116 / 2 + 70);
    DrawHintBar(c, ui, sp, f, hints);
    if (!footer.empty()) HudText(c, ui, f, 32, 1080 - 56 + 12, 32, footer, HudColour::Muted);
}

// While the game starts behind it: the logo and a line about what is happening.
inline void LoadingScreen(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Image* logo, const std::string& title,
    const std::string& text, uint32_t frame)
{
    DrawBackdrop(c, sp, "backgrounds/bg_sunny", 0xFFFFFFFF);
    const float wd = ui.W / ui.s;
    const float y = DrawLogo(c, ui, f, logo, title, 200, 380);
    HudText(c, ui, f, wd / 2, y + 60, 32, text + std::string((frame / 20) % 4, '.'), 0xFF5B6270, 1);
}

struct SessionStats
{
    int linkBattles = 0, trades = 0;
    int caught[4] = {};      // catches each player made this session
    int lastCaught[4] = {};  // and the species of their last one
};

// The co-op overworld (03-overworld, 04-tags): emotes, name pills over the trainers and
// pointers to those off the screen. The emotes in the order of the 'W' message.
inline const char* EmoteSprite(int e)
{
    static const char* n[] = { "emotes/emote_hi", "emotes/emote_question", "emotes/emote_heart", "emotes/emote_thanks",
        "emotes/emote_angry", "emotes/emote_music", "emotes/emote_sweat", "emotes/emote_sleep" };
    return e >= 0 && e < 8 ? n[e] : nullptr;
}

// A player's name pill centred on x with its bottom at y, and their emote (or the battle
// burst while they are in a battle) beside it.
inline void NamePill(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, const PartyPeer& p, int emote)
{
    const float w = HudTextWidth(ui, f, 32, p.name) + 48, h = 48;
    DrawSprite(c, sp.Get(PillOf(p.colour)), ui(x - w / 2), ui(y - h), ui(w), ui(h), ui.s);
    HudText(c, ui, f, x, y - h + 8, 32, p.name, HudColour::White, 1);
    const char* bubble = emote >= 0 ? EmoteSprite(emote) : p.state == PS_BATTLE ? "emotes/emote_battle" : nullptr;
    if (bubble) HudSprite(c, ui, sp, bubble, x + w / 2 - 12, y - h - 52, 72, 72);
}

// The pointer sprite (an arrow to the right in a white disc) turned by `angle` radians,
// in 32 steps, made once each.
inline const Image* TurnedPointer(Sprites& sp, float angle)
{
    const Sprite* s = sp.Get("people/pointer");
    if (!s) return nullptr;
    const int step = (int)std::lround(angle / 6.2831853f * 32) & 31;
    static std::map<std::pair<const Sprite*, int>, Image> turned;
    Image& img = turned[{ s, step }];
    if (!img.px.empty()) return &img;
    const Image& src = s->img;
    img.w = src.w;
    img.h = src.h;
    img.px.assign((size_t)src.w * src.h, 0);
    const float a = step * 6.2831853f / 32, ca = std::cos(a), sa = std::sin(a);
    const float cx = src.w / 2.0f, cy = src.h / 2.0f;
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++)
        {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const int sx = (int)std::floor(ca * dx + sa * dy + cx), sy = (int)std::floor(-sa * dx + ca * dy + cy);
            if (sx >= 0 && sy >= 0 && sx < src.w && sy < src.h) img.px[(size_t)y * src.w + x] = src.px[(size_t)sy * src.w + sx];
        }
    return &img;
}

// A player off the screen: the pointer at (x, y) turned towards them, and a pill with their
// name and place on the side towards the middle of the screen.
inline void OffscreenPointer(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, float angle,
    const PartyPeer& p, const std::string& place, float screenW)
{
    if (const Image* img = TurnedPointer(sp, angle)) Blit(c, *img, 0, 0, (float)img->w, (float)img->h, ui(x - 32), ui(y - 32), ui(64), ui(64));
    const std::string text = place.empty() ? p.name : p.name + " Â· " + place;
    const float w = HudTextWidth(ui, f, 32, text) + 48, h = 48;
    const float px = x < screenW / 2 ? x + 44 : x - 44 - w;
    DrawSprite(c, sp.Get(PillOf(p.colour)), ui(px), ui(y - h / 2), ui(w), ui(h), ui.s);
    HudText(c, ui, f, px + w / 2, y - h / 2 + 8, 32, text, HudColour::White, 1);
}

// The emote wheel around this player's trainer (at x, y): one emote for each way of the D-pad.
inline void EmoteWheel(Canvas& c, const Ui& ui, Sprites& sp, float x, float y)
{
    const float r = 120, s = 80;
    HudSprite(c, ui, sp, EmoteSprite(0), x - s / 2, y - r - s / 2, s, s);
    HudSprite(c, ui, sp, EmoteSprite(1), x - r - s / 2, y - s / 2, s, s);
    HudSprite(c, ui, sp, EmoteSprite(2), x + r - s / 2, y - s / 2, s, s);
    HudSprite(c, ui, sp, EmoteSprite(3), x - s / 2, y + r - s / 2, s, s);
}

// The friends panel (05-friends): a teal band with the tabs, then a row for each player.
struct FriendRow
{
    PartyPeer who;          // name and colour (for the chip)
    std::string info;       // the second line
    std::string action;     // a button on the right ("Battle"), or empty
    bool dim = false;       // not online: greyed
};
inline void FriendsPanel(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Menu& m, const std::vector<FriendRow>& rows,
    const std::vector<HudHint>& hints)
{
    FillBlend(c, 0, 0, (float)c.w, (float)c.h, 0x80000000);
    const float wd = ui.W / ui.s, w = 1200, x = wd / 2 - w / 2, y = 60;
    const float h = 24 + 72 + 12 + std::max<size_t>(rows.size(), 1) * 102.0f + 12 + 64 + 24;
    HudPanel(c, ui, sp, "panels/card", x, y, w, h);
    FillBlend(c, ui(x + 24), ui(y + 24), ui(w - 48), ui(72), 0xFF2EC4C6);
    HudSprite(c, ui, sp, "icons/white/icon_wifi", x + 48, y + 36, 48, 48);
    HudText(c, ui, f, x + 112, y + 36, 48, "FRIENDS", HudColour::White, 0, true);
    // The tabs, the chosen one in a white pill.
    float tx = x + w - 48;
    for (int t = (int)m.tabs.size() - 1; t >= 0; t--)
    {
        const float tw = HudTextWidth(ui, f, 32, m.tabs[t].name) + 40;
        tx -= tw;
        if (t == m.tab) DrawSprite(c, sp.Get("panels/toast"), ui(tx), ui(y + 38), ui(tw), ui(46), ui.s);
        HudText(c, ui, f, tx + tw / 2, y + 44, 32, m.tabs[t].name, t == m.tab ? 0xFF1F9C9E : HudColour::White, 1);
        tx -= 16;
    }
    float ry = y + 24 + 72 + 12;
    if (rows.empty())
        HudText(c, ui, f, wd / 2, ry + 30, 32, m.tab == 0 ? "No one else is here yet: share your code"
            : m.tab == 1 ? "Add the players you meet in a session as friends" : "No friend requests", HudColour::Muted, 1);
    for (int k = 0; k < (int)rows.size(); k++)
    {
        const FriendRow& r = rows[k];
        const bool sel = k == m.selected;
        HudPanel(c, ui, sp, sel ? "panels/row_selected" : r.dim ? "panels/row_disabled" : "panels/row", x + 24, ry, w - 48, 90);
        if (sel) HudSprite(c, ui, sp, "cursor/cursor", x + 36, ry + 25, 36, 40);
        HudSprite(c, ui, sp, r.dim ? "people/chip_ring_grey" : RingOf(r.who.colour), x + 84, ry + 9, 72, 72);
        HudText(c, ui, f, x + 120, ry + 29, 32, Initials(r.who.name), r.dim ? HudColour::Muted : HudColour::Ink, 1);
        HudText(c, ui, f, x + 176, ry + 10, 32, r.who.name, r.dim ? HudColour::Muted : HudColour::Ink);
        HudText(c, ui, f, x + 176, ry + 46, 32, r.info, HudColour::Muted);
        if (!r.action.empty())
        {
            const float bw = HudTextWidth(ui, f, 32, r.action) + 100;
            DrawSprite(c, sp.Get("panels/toast"), ui(x + w - 48 - bw), ui(ry + 18), ui(bw), ui(54), ui.s);
            if (sel) HudSprite(c, ui, sp, "icons/icon_arrow_left", x + w - 48 - bw + 8, ry + 27, 36, 36);
            HudText(c, ui, f, x + w - 48 - bw / 2, ry + 29, 32, r.action, HudColour::Ink, 1);
            if (sel) HudSprite(c, ui, sp, "icons/icon_arrow_right", x + w - 48 - 44, ry + 27, 36, 36);
        }
        else if (r.who.state == PS_BATTLE && !r.dim)
            HudSprite(c, ui, sp, "emotes/emote_battle", x + w - 48 - 80, ry + 10, 72, 72);
        else if (r.dim)
            HudSprite(c, ui, sp, "emotes/emote_sleep", x + w - 48 - 80, ry + 10, 72, 72);
        ry += 102;
    }
    if (rows.empty()) ry += 102;
    // The session's code, on navy.
    FillBlend(c, ui(x + 24), ui(ry + 12), ui(w - 48), ui(64), 0xFF1C2A44);
    HudText(c, ui, f, x + 52, ry + 28, 32, Party_Link() == PartyLink::Off ? "Not in a session" : "Your session code", 0xFFC8D0E0);
    if (Party_Link() != PartyLink::Off) HudText(c, ui, f, x + 340, ry + 18, 48, Party_Code().empty() ? "..." : Party_Code(), 0xFFFFD75E);
    DrawHintBar(c, ui, sp, f, hints);
}

// An invite from another player (09-invite): a card that takes the focus, with what they
// ask, the answers (m) and a bar for the time left (0 to 1).
inline void InviteCard(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Menu& m, const PartyPeer& from, int invite,
    float left, float seconds, const std::vector<HudHint>& hints)
{
    const float w = 740, x = 180, y = 250;
    const std::vector<int> rows = m.Rows();
    const float h = 24 + 90 + 16 + rows.size() * 64.0f + 70;
    HudPanel(c, ui, sp, "panels/dialogue_box", x, y, w, h);
    HudSprite(c, ui, sp, RingOf(from.colour), x + 26, y + 26, 76, 76);
    HudText(c, ui, f, x + 64, y + 48, 32, Initials(from.name), HudColour::Ink, 1);
    HudText(c, ui, f, x + 120, y + 26, 48, from.name + (invite == PI_SESSION ? " invites you!" : invite == PI_TRADE ? " wants to trade!" : " wants to battle!"),
        HudColour::Ink);
    HudText(c, ui, f, x + 120, y + 76, 32, invite == PI_SESSION ? "To play in their session" : invite == PI_TRADE ? "In the Union Room" : "In the Colosseum",
        HudColour::Muted);
    float ry = y + 24 + 90 + 16;
    for (int k = 0; k < (int)rows.size(); k++)
    {
        const MenuItem& it = m.items[rows[k]];
        const bool sel = k == m.selected;
        HudPanel(c, ui, sp, sel ? "panels/row_selected" : "panels/row", x + 26, ry, w - 52, 52);
        if (sel) HudSprite(c, ui, sp, "cursor/cursor", x + 36, ry + 6, 36, 40);
        HudText(c, ui, f, x + 26 + 60, ry + 10, 32, it.label, it.danger ? HudColour::Danger : HudColour::Ink);
        ry += 64;
    }
    HudSprite(c, ui, sp, "icons/icon_clock", x + 30, ry + 6, 40, 40);
    FillBlend(c, ui(x + 86), ui(ry + 18), ui(300), ui(16), 0xFF1C2A44);
    FillBlend(c, ui(x + 86), ui(ry + 18), ui(300 * std::clamp(left, 0.0f, 1.0f)), ui(16), 0xFF9C7CF0);
    HudText(c, ui, f, x + 406, ry + 10, 32, std::to_string((int)std::ceil(seconds)) + " s", HudColour::Muted);
    DrawHintBar(c, ui, sp, f, hints);
}

// The link battle's banner (08-battle): this player VS the others, in white at the top, and
// under it `note` (the turn timer), if any.
inline void VsBanner(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const PartyPeer& me, const std::vector<PartyPeer>& them,
    const std::string& note = "")
{
    if (them.empty()) return;
    std::vector<float> widths;
    float w = 24;
    auto pill = [&](const PartyPeer& p) { return HudTextWidth(ui, f, 32, p.name) + 48; };
    w += pill(me) + 20 + HudTextWidth(ui, f, 32, "VS") + 20;
    for (const PartyPeer& p : them) w += pill(p) + 8;
    w += 16;
    const float wd = ui.W / ui.s, x = wd / 2 - w / 2, y = 20, h = 72;
    HudPanel(c, ui, sp, "panels/toast", x, y, w, h);
    float px = x + 24;
    auto draw = [&](const PartyPeer& p) {
        const float pw = pill(p);
        DrawSprite(c, sp.Get(PillOf(p.colour)), ui(px), ui(y + 12), ui(pw), ui(48), ui.s);
        HudText(c, ui, f, px + pw / 2, y + 20, 32, p.name, HudColour::White, 1);
        px += pw;
    };
    draw(me);
    px += 20;
    HudText(c, ui, f, px, y + 20, 32, "VS", 0xFFE0457B);
    px += HudTextWidth(ui, f, 32, "VS") + 20;
    for (const PartyPeer& p : them)
    {
        draw(p);
        px += 8;
    }
    if (note.empty()) return;
    const float nw = HudTextWidth(ui, f, 32, note) + 92;
    HudPanel(c, ui, sp, "panels/toast", wd / 2 - nw / 2, y + h + 8, nw, 56);
    HudSprite(c, ui, sp, "icons/icon_clock", wd / 2 - nw / 2 + 20, y + h + 16, 40, 40);
    HudText(c, ui, f, wd / 2 - nw / 2 + 70, y + h + 20, 32, note, HudColour::Ink);
}

// Watching another player's battle: who, against whom, both Pokemon with their HP and the
// turn, in a card under the session badge. `foe`: the other trainer's name, or empty.
inline void SpectatorCard(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, const PartyPeer& who,
    const std::string& foe, const IconFn& icon)
{
    const PartyBattle& b = who.battle;
    const float w = 620, h = 66 + 2 * 76 + 20;
    TitledCard(c, ui, sp, f, x, y, w, h, who.name + (foe.empty() ? " is battling" : " VS " + foe), "Turn " + std::to_string(b.turn));
    for (int k = 0; k < 2; k++)
    {
        const int species = k ? b.foeSpecies : b.species, hp = std::clamp(k ? b.foeHp : b.hp, 0, 100);
        const float ry = y + 66 + 10 + 76 * k;
        if (icon) HudPixelArt(c, ui, icon(species), x + 20, ry, 64, 64);
        HudText(c, ui, f, x + 100, ry + 14, 32, Party_SpeciesName(species), hp ? HudColour::Ink : HudColour::Muted);
        const float bx = x + 330, bw = 180;
        HudSprite(c, ui, sp, "bars/bar_frame", bx, ry + 20, bw, 22);
        if (hp)
            HudSprite(c, ui, sp, hp > 50 ? "bars/fill_teal" : hp > 25 ? "bars/fill_yellow" : "bars/fill_red", bx + 6, ry + 24,
                std::max(16.0f, (bw - 12) * hp / 100.0f), 14);
        HudText(c, ui, f, x + w - 24, ry + 14, 32, std::to_string(hp) + "%", hp > 25 ? HudColour::Ink : HudColour::Danger, 2);
    }
}

// The Profile tab's preview of what the others see (15-menu-profile's trainer card).
inline void TrainerCardPreview(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const HudRect& r, const PartyPeer& me, int dex,
    int badges, const SessionStats& st, const IconFn& icon)
{
    const float x = r.x, y = r.y, w = r.w;
    const float h = 66 + 16 + 260 + 4 * 56 + 60;
    HudPanel(c, ui, sp, "panels/card_header_teal", x, y, w, h, 0xFFFFFFFF);
    HudText(c, ui, f, x + w / 2, y + 13, 32, "TRAINER CARD \xC2\xB7 PREVIEW", HudColour::White, 1);
    const float d = 170, cx = x + w / 2;
    HudSprite(c, ui, sp, RingOf(me.colour), cx - d / 2, y + 90, d, d);
    HudText(c, ui, f, cx, y + 90 + d / 2 - 32, 64, Initials(me.name), HudColour::Ink, 1, true);
    if (me.partner && icon) HudPixelArt(c, ui, icon(me.partner), cx + d / 2 - 30, y + 90 + d - 80, 96, 96);
    const float pw = HudTextWidth(ui, f, 32, me.name) + 48;
    DrawSprite(c, sp.Get(PillOf(me.colour)), ui(cx - pw / 2), ui(y + 90 + d + 6), ui(pw), ui(48), ui.s);
    HudText(c, ui, f, cx, y + 90 + d + 14, 32, me.name, HudColour::White, 1);
    const std::pair<std::string, std::string> rows[] = { { "Badges", std::to_string(badges) },
        { "Pok\xC3\xA9" "dex", dex < 0 ? "-" : std::to_string(dex) }, { "Link battles", std::to_string(st.linkBattles) },
        { "Trades", std::to_string(st.trades) } };
    float ry = y + 90 + d + 76;
    for (const auto& row : rows)
    {
        HudPanel(c, ui, sp, "panels/row", x + 23, ry, w - 46, 46);
        HudText(c, ui, f, x + 45, ry + 7, 32, row.first, HudColour::Muted);
        HudText(c, ui, f, x + w - 45, ry + 7, 32, row.second, HudColour::Ink, 2);
        ry += 56;
    }
    HudSprite(c, ui, sp, "icons/icon_eye", cx - 120, ry + 8, 36, 36);
    HudText(c, ui, f, cx - 76, ry + 10, 32, "What the others see", HudColour::Muted);
}

// The summary shown before leaving a session (16-menu-session).
inline void Summary(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::vector<HudHint>& hints, const SessionStats& st,
    const IconFn& icon)
{
    DrawBackdrop(c, sp, "backgrounds/bg_sunny", 0xFFFFFFFF);
    const float wd = ui.W / ui.s;
    const PartyPeer* peers = Party_Peers();
    std::string host;
    for (int i = 0; i < 4; i++)
        if (peers[i].present && peers[i].host) host = peers[i].name;
    HudText(c, ui, f, 100, 60, 64, host.empty() ? "Leave the session?" : "Leave " + host + "'s session?", HudColour::Ink, 0, true);

    const uint32_t secs = Party_SessionSeconds();
    char time[16];
    snprintf(time, sizeof time, "%u:%02u", secs / 3600, secs / 60 % 60);
    const float lx = 100, lw = 640, ly = 150;
    TitledCard(c, ui, sp, f, lx, ly, lw, 66 + 16 + 5 * 60 + 16, "THIS SESSION");
    const std::pair<std::string, std::string> stats[] = { { "Play time", time },
        { "Caught", std::to_string(Party_CaughtThisSession()) }, { "Badges", std::to_string(Party_MyBadges()) },
        { "Link battles", std::to_string(st.linkBattles) }, { "Trades", std::to_string(st.trades) } };
    const char* icons[] = { "icons/icon_clock", "icons/icon_star", "people/crown_gold", "icons/icon_fight", "icons/icon_trade" };
    float ry = ly + 66 + 16;
    for (int k = 0; k < 5; k++)
    {
        HudPanel(c, ui, sp, "panels/row", lx + 23, ry, lw - 46, 50);
        HudSprite(c, ui, sp, icons[k], lx + 40, ry + 7, 36, 36);
        HudText(c, ui, f, lx + 92, ry + 9, 32, stats[k].first, HudColour::Muted);
        HudText(c, ui, f, lx + lw - 23 - 22, ry + 9, 32, stats[k].second, HudColour::Ink, 2);
        ry += 60;
    }

    const float rx = 780, rw = wd - rx - 100;
    TitledCard(c, ui, sp, f, rx, ly, rw, 66 + 16 + Party_Count() * 98.0f + 16, "WHO WAS HERE");
    float py = ly + 66 + 16;
    for (int i = 0; i < 4; i++)
    {
        if (!peers[i].present) continue;
        const PartyPeer& p = peers[i];
        HudPanel(c, ui, sp, p.host ? "panels/row_gold" : "panels/row", rx + 23, py, rw - 46, 86);
        Chip(c, ui, sp, f, rx + 43, py + 7, 72, p);
        if (p.host) HudSprite(c, ui, sp, "people/crown_gold", rx + 130, py + 18, 48, 44);
        HudText(c, ui, f, rx + 190, py + 8, 32, p.name, HudColour::Ink);
        HudText(c, ui, f, rx + 190, py + 44, 32,
            std::string(p.host ? "Host \xC2\xB7 " : "") + std::to_string(p.badges) + (p.badges == 1 ? " badge" : " badges") +
                (i == Party_Me() ? " \xC2\xB7 you" : " \xC2\xB7 still here"),
            HudColour::Muted);
        if (st.caught[i])
        {
            const std::string n = std::to_string(st.caught[i]) + " caught";
            HudText(c, ui, f, rx + rw - 23 - 24, py + 27, 32, n, HudColour::Ink, 2);
            if (icon) HudPixelArt(c, ui, icon(st.lastCaught[i]), rx + rw - 23 - 24 - HudTextWidth(ui, f, 32, n) - 84, py + 3, 80, 80);
        }
        py += 98;
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
