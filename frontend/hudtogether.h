#pragma once
// Playing together on the overworld: speech bubbles over the players (chat and quick lines),
// the chat log, the quick lines beside the emote wheel, the prompt for a friend standing in
// front of this player, and the card to challenge them. Data from party.h.
#include <algorithm>
#include <string>
#include <vector>
#include "hudmenu.h"
#include "hudonline.h"
#include "party.h"

namespace HudTogether
{
// A speech bubble centred on x with its bottom at y.
inline void Bubble(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, const std::string& text)
{
    const float w = std::min(HudTextWidth(ui, f, 32, text) + 56, 760.0f), h = 54;
    DrawSprite(c, sp.Get("panels/toast"), ui(x - w / 2), ui(y - h), ui(w), ui(h), ui.s);
    HudText(c, ui, f, x, y - h + 10, 32, text, HudColour::Ink, 1);
}

// The last words, for a few seconds, of a player (empty if none).
inline std::string Saying(const PartyPeer& p, uint32_t now)
{
    return !p.walk.say.empty() && now - p.walk.sayAt < 5000 ? p.walk.say : "";
}

// The chat log along the left edge: the lines of the last `seconds`, newest at the bottom.
inline void Log(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::deque<PartyChatLine>& lines, uint32_t now,
    float bottom, float seconds = 12, size_t most = 5)
{
    std::vector<const PartyChatLine*> shown;
    for (const PartyChatLine& l : lines)
        if (now - l.at < seconds * 1000) shown.push_back(&l);
    if (shown.size() > most) shown.erase(shown.begin(), shown.end() - most);
    if (shown.empty()) return;
    float w = 0;
    for (const PartyChatLine* l : shown) w = std::max(w, HudTextWidth(ui, f, 32, l->name + ": " + l->text));
    w = std::min(w + 40, 900.0f);
    const float h = shown.size() * 42.0f + 20, y = bottom - h;
    FillBlend(c, ui(24), ui(y), ui(w), ui(h), 0x99000000);
    for (size_t i = 0; i < shown.size(); i++)
    {
        const PartyChatLine& l = *shown[i];
        const float ly = y + 10 + i * 42.0f;
        HudText(c, ui, f, 44, ly, 32, l.name + ":", HudOnline::TextColourOf(l.colour));
        HudText(c, ui, f, 44 + HudTextWidth(ui, f, 32, l.name + ": "), ly, 32, l.text, HudColour::White);
    }
}

// The quick lines, as held R (and L for the second pair) offers them, beside the emote wheel.
inline void QuickKeys(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, bool both)
{
    static const char* keys[] = { "A", "B", "X", "Y" };
    const float w = 360, h = 4 * 46.0f + 24;
    FillBlend(c, ui(x), ui(y), ui(w), ui(h), 0xAA000000);
    for (int i = 0; i < 4; i++)
    {
        const int line = both ? 4 + i : i;
        if (line >= Party_QuickLines()) continue;
        HudText(c, ui, f, x + 24, y + 12 + i * 46.0f, 32, std::string(keys[i]) + "  " + Party_QuickLine(line), HudColour::White);
    }
    HudText(c, ui, f, x + w - 16, y + h - 40, 24, both ? "L off: HI!..." : "+ L: BATTLE?...", 0xFFC8D0E0, 2);
}

// A friend is standing in front of this player: what A does.
inline void FacePrompt(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::string& name, float bottom)
{
    const std::string text = "A  Challenge " + name;
    const float w = HudTextWidth(ui, f, 32, text) + 64, W = ui.W / ui.s;
    DrawSprite(c, sp.Get("panels/toast"), ui(W / 2 - w / 2), ui(bottom - 64), ui(w), ui(54), ui.s);
    HudText(c, ui, f, W / 2, bottom - 54, 32, text, HudColour::Ink, 1);
}

// The card for a challenge: the rows of m (Battle, Trade, ...).
inline void ChallengeCard(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Menu& m, const PartyPeer& to,
    const std::vector<HudHint>& hints)
{
    const float w = 740, x = 180, y = 250;
    const std::vector<int> rows = m.Rows();
    const float h = 24 + 90 + 16 + rows.size() * 64.0f + 24;
    HudPanel(c, ui, sp, "panels/dialogue_box", x, y, w, h);
    HudOnline::Avatar(c, ui, sp, f, HudOnline::RingOf(to.colour), x + 26, y + 26, 76, to);
    HudText(c, ui, f, x + 120, y + 26, 48, to.name, HudColour::Ink);
    HudText(c, ui, f, x + 120, y + 76, 32, "is right in front of you", HudColour::Muted);
    float ry = y + 24 + 90 + 16;
    for (int k = 0; k < (int)rows.size(); k++)
    {
        const MenuItem& it = m.items[rows[k]];
        const bool sel = k == m.selected;
        HudPanel(c, ui, sp, sel ? "panels/row_selected" : "panels/row", x + 26, ry, w - 52, 52);
        if (sel) HudSprite(c, ui, sp, "cursor/cursor", x + 36, ry + 6, 36, 40);
        HudText(c, ui, f, x + 26 + 60, ry + 10, 32, it.label, it.danger ? HudColour::Danger : HudColour::Ink);
        if (!it.desc.empty()) HudText(c, ui, f, x + w - 52, ry + 10, 28, it.desc, HudColour::Muted, 2);
        ry += 64;
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
