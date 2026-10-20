#pragma once
// The online HUD (online-HUD design: 03-overworld, 06-lobby, 07-toasts, 11-menu-title,
// 16-menu-session): the session badge with the signal bars, the player chips, the card
// shown while a lost connection is retried, the session screen (players and the code),
// code entry, the start screen and the summary shown on leaving. Data from party.h.
#include "hudtoast.h"
#include "party.h"

namespace HudOnline
{
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
    const std::string count = std::to_string(Party_Count()) + "/" + std::to_string(Party_Max());
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
inline void SessionScreen(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Menu& m, const std::vector<HudHint>& hints)
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
inline void StartScreen(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::vector<StartItem>& items, int sel,
    const std::string& title, const std::string& sub, const std::vector<HudHint>& hints, const std::string& footer)
{
    DrawBackdrop(c, sp, "backgrounds/bg_sunny", 0xFFFFFFFF);
    const float wd = ui.W / ui.s;
    HudText(c, ui, f, wd / 2, 60, 80, title, HudColour::Ink, 1, true);
    HudText(c, ui, f, wd / 2, 160, 32, sub, 0xFF5B6270, 1);
    const float pitch = 260, x0 = wd / 2 - pitch * (items.size() - 1) / 2;
    for (size_t i = 0; i < items.size(); i++)
    {
        const bool s = (int)i == sel;
        const float d = s ? 176 : 136, cx = x0 + pitch * i;
        HudSprite(c, ui, sp, items[i].icon + (s ? "_selected" : ""), cx - d / 2, 350 - d / 2, d, d);
        const float ly = 350 + 136 / 2 + (s ? 34 : 18);
        if (s)
        {
            const float lw = HudTextWidth(ui, f, 32, items[i].label) + 32;
            HudPanel(c, ui, sp, "panels/tab", cx - lw / 2, ly - 2, lw, 40);
        }
        HudText(c, ui, f, cx, ly, 32, items[i].label, HudColour::Ink, 1);
    }
    DrawHintBar(c, ui, sp, f, hints);
    if (!footer.empty()) HudText(c, ui, f, 32, 1080 - 56 + 12, 32, footer, HudColour::Muted);
}

// The summary shown before leaving a session (16-menu-session).
inline void Summary(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::vector<HudHint>& hints)
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
    TitledCard(c, ui, sp, f, lx, ly, lw, 66 + 16 + 3 * 60 + 16, "THIS SESSION");
    const std::pair<std::string, std::string> stats[] = { { "Play time", time },
        { "Caught", std::to_string(Party_CaughtThisSession()) }, { "Badges", std::to_string(Party_MyBadges()) } };
    const char* icons[] = { "icons/icon_clock", "icons/icon_star", "people/crown_gold" };
    float ry = ly + 66 + 16;
    for (int k = 0; k < 3; k++)
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
        py += 98;
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
