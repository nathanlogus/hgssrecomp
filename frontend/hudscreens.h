#pragma once
// The game's own screens redrawn in the HUD style (online-HUD design, 18-25), shown in
// place of the game's picture while that screen runs; the game still takes the input. The
// trainer card (25-ss-card) comes first, as a trial: everything on it is read from the save.
#include "hudonline.h"

namespace HudScreens
{
// "36484" -> "36,484".
inline std::string Thousands(uint32_t v)
{
    std::string s = std::to_string(v);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return s;
}

// A label on the left and a value on the right in a row; gold for the online numbers.
inline void StatRow(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, float w, const std::string& label,
    const std::string& value, bool gold = false)
{
    HudPanel(c, ui, sp, gold ? "panels/row_gold" : "panels/row", x, y, w, 52);
    HudText(c, ui, f, x + 20, y + 10, 32, label, HudColour::Muted);
    HudText(c, ui, f, x + w - 20, y + 10, 32, value, HudColour::Ink, 2);
}

// The save screen (17-save): the trainer and partner on the left, the save's numbers and
// badges in a card, the two answers below and a line about the session. `phase`: 0 asking,
// 1 the game asking to write over the save file (answered for the player), 2 saving, 3
// saved. `sel`: the answer chosen (0 save, 1 back). `footer`: the place and the time.
inline void SaveScreen(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const std::string& name, const PartyCard& card, int dex,
    const Image* trainer, const Image* partner, int phase, int sel, const std::string& session, const std::string& footer,
    const std::vector<HudHint>& hints)
{
    DrawBackdrop(c, sp, "backgrounds/bg_sunny", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 780;
    // The trainer and partner.
    const float lx = x0, ly = 100, lw = 300, lh = 620;
    HudPanel(c, ui, sp, "panels/card", lx, ly, lw, lh);
    if (trainer) HudPixelArt(c, ui, trainer, lx + lw / 2 - 128, ly + 70, 256, 256);
    if (partner) HudPixelArt(c, ui, partner, lx + lw / 2 - 80, ly + 360, 160, 160);
    HudText(c, ui, f, lx + lw / 2, ly + lh - 70, 32, name, HudColour::Ink, 1);

    // The numbers and the badges.
    const float cx = x0 + 360, cw = 1200, cy = 100;
    const float rows = session.empty() ? 5 : 6;
    const float ch = 40 + rows * 64 + 30;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    char id[16], time[16];
    snprintf(id, sizeof id, "%05u", card.id & 0xFFFF);
    snprintf(time, sizeof time, "%d:%02d", card.hours, card.minutes);
    std::vector<std::pair<std::string, std::string>> list = { { "Name", name }, { "ID No.", id },
        { "Pok\xC3\xA9" "dex", (dex < 0 ? std::string("-") : std::to_string(dex)) + " Pok\xC3\xA9mon" },
        { "Money", "$" + Thousands(card.money) }, { "Play time", time } };
    if (!session.empty()) list.push_back({ "Session", session });
    const float rx = cx + 36, rw = cw - 36 - 320;
    float ry = cy + 36;
    for (size_t i = 0; i < list.size(); i++)
    {
        const bool gold = i == 5;
        if (gold) FillBlend(c, ui(rx - 10), ui(ry - 8), ui(rw + 20), ui(52), 0xFFFFF4C8);
        HudText(c, ui, f, rx, ry, 32, list[i].first, gold ? 0xFFC88A1E : HudColour::Muted);
        HudText(c, ui, f, rx + rw, ry, 32, list[i].second, HudColour::Ink, 2);
        if (!gold) FillBlend(c, ui(rx), ui(ry + 46), ui(rw), ui(2), 0xFFF1E3C8);
        ry += 64;
    }
    const float bx = cx + cw - 300;
    HudText(c, ui, f, bx + 130, cy + 30, 32, "Badges", HudColour::Muted, 1);
    for (int i = 0; i < 8; i++)
    {
        const float x = bx + 20 + (i % 2) * 120, y = cy + 78 + (i / 2) * 82, s = 72;
        if (card.johto >> i & 1)
        {
            HudSprite(c, ui, sp, "people/chip_ring_orange", x + 10, y, s, s);
            HudSprite(c, ui, sp, "icons/icon_star", x + 10 + s / 2 - 22, y + s / 2 - 22, 44, 44);
        }
        else
            HudSprite(c, ui, sp, "people/chip_ring_grey", x + 10, y, s, s, 0x80FFFFFF);
    }

    // The answers, or what is happening.
    const float by = cy + ch + 70;
    if (phase == 0)
    {
        const char* labels[2] = { "Save your progress", "Back to your adventure" };
        for (int k = 0; k < 2; k++)
        {
            const float y = by + k * 80;
            HudPanel(c, ui, sp, k == sel ? "panels/button_yellow" : "panels/button_white", cx, y, cw, 60);
            if (k == sel) HudSprite(c, ui, sp, "cursor/cursor", cx + 20, y + 10, 36, 40);
            HudText(c, ui, f, cx + cw / 2, y + 14, 32, labels[k], HudColour::Ink, 1);
        }
    }
    else
    {
        HudPanel(c, ui, sp, "panels/dialogue_box", cx, by, cw, 100);
        HudSprite(c, ui, sp, phase <= 2 ? "icons/icon_save" : "icons/icon_check", cx + 36, by + 28, 44, 44);
        HudText(c, ui, f, cx + 100, by + 32, 32,
            phase <= 2 ? "Saving... don't turn the power off." : name + " saved the game.", HudColour::Ink);
    }
    DrawHintBar(c, ui, sp, f, hints);
    if (!footer.empty()) HudText(c, ui, f, 32, 1080 - 56 + 12, 32, footer, HudColour::Muted);
}

// The trainer card: the trainer (their overworld sprite, larger) and partner on the left,
// the numbers on the right, the badges in a tray. `trainer` and `partner` may be null; the
// session's rows are shown when `online`.
inline void TrainerCard(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const PartyPeer& me, const PartyCard& card, int dex,
    const HudOnline::SessionStats& st, bool online, const Image* trainer, const Image* partner, const std::vector<HudHint>& hints)
{
    DrawBackdrop(c, sp, "backgrounds/bg_coral", 0xFFFFFFFF);
    // The card's height: the numbers' rows and the badge tray, or the trainer's box.
    const bool both = card.kanto != 0;
    const float th = both ? 260 : 170;
    const float wd = ui.W / ui.s, w = 1600, x = wd / 2 - w / 2, y = 60;
    const float h = std::max(206 + 66 * (online ? 3 : 2) + 8 + th, 120 + 470.0f) + 30;
    HudPanel(c, ui, sp, "panels/card", x, y, w, h);
    FillBlend(c, ui(x + 30), ui(y + 30), ui(w - 60), ui(64), 0xFFF0608A);
    HudSprite(c, ui, sp, "icons/white/icon_card", x + 54, y + 38, 48, 48);
    HudText(c, ui, f, x + 120, y + 38, 48, "TRAINER CARD", HudColour::White, 0, true);
    char id[16];
    snprintf(id, sizeof id, "%05u", card.id & 0xFFFF);
    HudText(c, ui, f, x + w - 56, y + 46, 32, std::string("ID No. ") + id, HudColour::White, 2);

    // Left: the trainer and their partner.
    const float lx = x + 30, ly = y + 120, lw = 380, lh = 470;
    FillBlend(c, ui(lx), ui(ly), ui(lw), ui(lh), 0xFFFDF0EE);
    if (trainer) HudPixelArt(c, ui, trainer, lx + lw / 2 - 128, ly + 20, 256, 256);
    if (partner)
    {
        HudSprite(c, ui, sp, HudOnline::RingOf(1), lx + lw / 2 - 70, ly + 290, 140, 140);
        HudPixelArt(c, ui, partner, lx + lw / 2 - 56, ly + 296, 112, 112);
        HudText(c, ui, f, lx + lw / 2, ly + 432, 32, "Partner", HudColour::Muted, 1);
    }

    // Right: the name and the numbers.
    const float rx = x + 440, rw = w - 470;
    const float pw = HudTextWidth(ui, f, 48, me.name) + 64;
    DrawSprite(c, sp.Get(HudOnline::PillOf(me.colour)), ui(rx), ui(y + 118), ui(pw), ui(64), ui.s);
    HudText(c, ui, f, rx + pw / 2, y + 126, 48, me.name, HudColour::White, 1);
    const float cw = (rw - 16) / 2;
    char time[16];
    snprintf(time, sizeof time, "%d:%02d", card.hours, card.minutes);
    float ry = y + 206;
    StatRow(c, ui, sp, f, rx, ry, cw, "Money", "$" + Thousands(card.money));
    StatRow(c, ui, sp, f, rx + cw + 16, ry, cw, "Pok\xC3\xA9" "dex", dex < 0 ? "-" : std::to_string(dex));
    ry += 66;
    int johto = 0, kanto = 0;
    for (int i = 0; i < 8; i++)
    {
        johto += card.johto >> i & 1;
        kanto += card.kanto >> i & 1;
    }
    StatRow(c, ui, sp, f, rx, ry, cw, "Play time", time);
    StatRow(c, ui, sp, f, rx + cw + 16, ry, cw, "Badges", std::to_string(johto + kanto));
    ry += 66;
    if (online)
    {
        StatRow(c, ui, sp, f, rx, ry, cw, "Link battles", std::to_string(st.linkBattles), true);
        StatRow(c, ui, sp, f, rx + cw + 16, ry, cw, "Trades", std::to_string(st.trades), true);
        ry += 66;
    }

    // The badges: Johto's, then Kanto's once any is earned.
    FillBlend(c, ui(rx), ui(ry + 8), ui(rw), ui(th), 0xFFFDF0EE);
    HudText(c, ui, f, rx + 22, ry + 18, 32, "Badges", 0xFFC0606A);
    for (int row = 0; row < (both ? 2 : 1); row++)
        for (int i = 0; i < 8; i++)
        {
            const bool has = ((row ? card.kanto : card.johto) >> i) & 1;
            const float bx = rx + 22 + i * (both ? 100 : 118), by = ry + 62 + row * 96, s = both ? 84 : 96;
            if (has)
            {
                HudSprite(c, ui, sp, "people/chip_ring_orange", bx, by, s, s);
                HudSprite(c, ui, sp, "icons/icon_star", bx + s / 2 - 24, by + s / 2 - 24, 48, 48);
            }
            else
                HudSprite(c, ui, sp, "people/chip_empty", bx, by, s, s);
        }
    DrawHintBar(c, ui, sp, f, hints);
}
}
