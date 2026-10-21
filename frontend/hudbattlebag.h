#pragma once
// The bag of a battle (the touch screen's, overlay 8) redrawn in the HUD style, as the bag
// outside battles is (hudbag.h): its four pockets as discs with the last item used and
// Close beside them, a page of the pocket's items, and the item chosen on a card with its
// description and USE. The game keeps taking the input (its cursor moves with the d-pad);
// everything shown is read from its memory each frame. The overlay is not decompiled; its
// data is laid out as Platinum's battle bag (pokeplatinum battle_sub_menus/battle_bag.h),
// at these places in HeartGold and SoulSilver:
//  - the context (BattleBagContext): the BattleSystem (+0), the Bag (+8), the item last
//    used (+0x20) and its pocket (+0x1F), the page of each pocket (+0x2C + pocket);
//  - the bag (BattleBag): the context (+0), the cursor (+0x34: shown at +8, place at +9),
//    the items of each pocket (+0x3C, 36 of 4 bytes each), the screen (+0x114C: 0 pockets,
//    1 a pocket's items, 2 an item), the pocket (+0x114D), each pocket's count (+0x114F)
//    and its message box (+0x1C, a Window).
#include "hudbag.h"
#include "hudbattleui.h"

namespace HudBattleBag
{
using HudBattleUi::Mem;

struct State
{
    int screen = 0, pocket = 0, page = 0, cursor = -1; // cursor: the game's button, -1 hidden
    std::vector<std::pair<int, int>> items;           // the pocket's: id, quantity
    int lastItem = 0;
    int shown = 0; // the pocket whose items these are
    std::string message; // what the bag says in its message box, if it does
    int sel = 0;         // on the item screen: which of the page's items it is
    std::string Key() const
    {
        std::string k = std::to_string(screen) + "," + std::to_string(pocket) + "," + std::to_string(page) + "," + std::to_string(cursor) + "," +
            std::to_string(sel) + "," + std::to_string(shown) + "," + std::to_string(lastItem) + "|" + message;
        for (const auto& it : items) k += "|" + std::to_string(it.first) + "x" + std::to_string(it.second);
        return k;
    }
};

class Reader
{
public:
    // Finds the bag's data (searching the memory at most every 15 frames while it is not
    // found) and reads it; false if no battle bag is open.
    bool Read(const uint8_t* ram, uint32_t mask, unsigned frame, State& s)
    {
        const Mem m{ ram, mask };
        const uint32_t bs = HudBattleUi::Battle(m);
        if (!bs) return false;
        const uint32_t bag = m.r32(bs + 0x58);
        // A block of the game's heap in use has "UD" in its header (32 bytes before it: the
        // heap's header, then the game's own).
        auto live = [&](uint32_t a) { return m.r16(a - 0x20) == 0x5544; };
        auto good = [&](uint32_t o) {
            const uint32_t c = m.r32(o);
            return Mem::In(o) && live(o) && Mem::In(c) && live(c) && m.r32(c) == bs && m.r32(c + 8) == bag && m.r8(o + 0x114C) <= 2 &&
                m.r8(o + 0x114D) <= 3 && Mem::In(m.r32(o + 0x34));
        };
        if (!good(obj))
        {
            obj = 0;
            if (frame - lastSearch < 15) return false;
            lastSearch = frame;
            for (uint32_t c = 0x02000000; c < 0x02400000 && !obj; c += 4)
                if (m.r32(c) == bs && m.r32(c + 8) == bag && c != bs && live(c))
                    for (uint32_t o = 0x02000000; o < 0x02400000; o += 4)
                        if (m.r32(o) == c && good(o))
                        {
                            obj = o;
                            break;
                        }
            if (!obj) return false;
        }
        const uint32_t ctx = m.r32(obj), cur = m.r32(obj + 0x34);
        s.screen = (int)m.r8(obj + 0x114C);
        s.pocket = (int)m.r8(obj + 0x114D);
        s.cursor = m.r8(cur + 8) ? (int)m.r8(cur + 9) : -1;
        // The items of the pocket shown: the one open, or on the first screen the one under
        // the cursor.
        s.shown = s.screen == 0 && s.cursor >= 0 && s.cursor < 4 ? s.cursor : s.pocket;
        s.items.clear();
        for (int i = 0; i < std::min(36, (int)m.r8(obj + 0x114F + s.shown)); i++)
        {
            const uint32_t a = obj + 0x3C + (s.shown * 36 + i) * 4;
            const int id = (int)m.r16(a), qty = (int)m.r16(a + 2);
            if (id <= 0 || id > 600 || qty <= 0 || qty > 999) return false;
            s.items.push_back({ id, qty });
        }
        const int pages = std::max(1, ((int)s.items.size() + 5) / 6);
        s.page = s.screen == 0 ? 0 : std::min((int)m.r8(ctx + 0x2C + s.pocket), pages - 1);
        s.lastItem = (int)m.r16(ctx + 0x20);
        if (s.lastItem > 600) s.lastItem = 0;
        // On the item screen, the item is the one chosen on the page (BagCursor's position).
        s.sel = (int)m.r8(ctx + 0x27 + s.pocket);
        if (s.sel > 5) s.sel = 0;
        // The message box, when it has text in it (in the game's font, read back).
        s.message.clear();
        HudBattleUi::Window w;
        std::vector<HudBattleUi::TextLine> lines;
        if (words && w.Read(m, obj + 0x1C, vram) && std::count(w.px.begin(), w.px.end(), 1) > 8 && words->Read(w, lines))
            for (const auto& l : lines)
                if (l.y >= 0)
                    for (const auto& p : l.pieces) s.message += (s.message.empty() ? "" : " ") + p.text;
        return true;
    }
    // The bottom screen's video memory, for the message box, and the reader of its text.
    HudBattleUi::Vram vram;
    const HudBattleUi::WindowReader* words = nullptr;

private:
    uint32_t obj = 0;
    unsigned lastSearch = 0u - 100;
};

// The pockets' names and an item to stand for each on its disc (Potion, Antidote, Poké
// Ball, X Attack).
constexpr const char* kPocket[4] = { "HP/PP Restore", "Status Healers", "Pok\xC3\xA9 Balls", "Battle Items" };
constexpr int kPocketItem[4] = { 17, 18, 4, 57 };

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, HudBag::Texts& tx, const State& s,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_lime", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;
    auto name = [&](int id) { return tx.Get(HudBag::kItemName, id); };

    HudText(c, ui, f, x0 + 60, 66, 48, "BAG", Ink, 0, true);
    // The pockets, as the game's buttons are numbered: 0 and 1 on the left, 2 and 3 on the
    // right; the shown one on an orange ring. Then the item last used and Close.
    float px = x0 + 250;
    static const int order[4] = { 0, 1, 2, 3 };
    for (int k = 0; k < 4; k++)
    {
        const int p = order[k];
        const bool cur = p == s.pocket && s.screen > 0, here = s.screen == 0 && s.cursor == p;
        const float d = cur || here ? 104 : 84, dx = px + (104 - d) / 2, dy = 92 - d / 2;
        HudSprite(c, ui, sp, here ? "people/chip_ring_orange" : cur ? "people/chip_ring_green" : "people/chip_ring_grey", dx, dy, d, d);
        HudPixelArt(c, ui, art.ItemIcon(kPocketItem[p]), dx + d / 2 - 32, dy + d / 2 - 32, 64, 64);
        if (here) HudSprite(c, ui, sp, "cursor/cursor_small", dx - 28, 76, 30, 32);
        px += 124;
    }
    // The item last used (the game's grey bar), and Close.
    {
        const bool here = s.screen == 0 && s.cursor == 4;
        const std::string label = s.lastItem ? "Last used: " + name(s.lastItem) : "No item used yet";
        const float w = HudTextWidth(ui, f, 32, label) + 110;
        HudPanel(c, ui, sp, here ? "panels/button_yellow" : s.lastItem ? "panels/button_white" : "panels/row_disabled", px + 20, 62, w, 60);
        if (s.lastItem) HudPixelArt(c, ui, art.ItemIcon(s.lastItem), px + 34, 68, 48, 48);
        HudText(c, ui, f, px + 96, 76, 32, label, s.lastItem ? Ink : Muted);
        if (here) HudSprite(c, ui, sp, "cursor/cursor_small", px - 8, 76, 30, 32);
    }
    const int closeAt = s.screen == 0 ? 5 : s.screen == 1 ? 8 : 1;
    {
        const bool here = s.cursor == closeAt;
        HudPanel(c, ui, sp, here ? "panels/button_yellow" : "panels/button_white", x0 + 1860 - 220, 62, 220, 60);
        if (here) HudSprite(c, ui, sp, "cursor/cursor_small", x0 + 1860 - 208, 76, 30, 32);
        HudText(c, ui, f, x0 + 1860 - 110, 76, 32, s.screen == 0 ? "Close bag" : "Back", Ink, 1);
    }

    // The pocket's page (or, on the first screen, the pocket under the cursor), two columns
    // of three as the game lays them out.
    const int shown = s.shown;
    const float fx = x0 + 60, fy = 170, fw = 960, fh = 690;
    HudPanel(c, ui, sp, "panels/frame_yellow", fx, fy, fw, fh);
    const int count = (int)s.items.size();
    const int first = s.screen > 0 ? s.page * 6 : 0;
    const int pages = std::max(1, (count + 5) / 6);
    HudText(c, ui, f, fx + 50, fy + 34, 40, kPocket[shown], Ink, 0, true);
    const std::string kinds = std::to_string(count) + (count == 1 ? " kind" : " kinds");
    HudText(c, ui, f, fx + fw - 50, fy + 40, 32, kinds, Muted, 2);
    for (int k = 0; k < 6; k++)
    {
        const int idx = first + k;
        const bool here = s.screen == 1 && s.cursor == k, has = idx < (int)s.items.size();
        const float tw = 420, th = 140, tx0 = fx + 50 + (k % 2) * (tw + 20), ty = fy + 110 + (k / 2) * 156;
        HudPanel(c, ui, sp, here ? "panels/row_selected" : has ? "panels/row" : "panels/row_disabled", tx0, ty, tw, th,
            here || has ? 0xFFFFFFFF : 0x80FFFFFF);
        if (here) HudSprite(c, ui, sp, "cursor/cursor_small", tx0 + 8, ty + th / 2 - 16, 30, 32);
        if (!has) continue;
        const int id = s.items[idx].first;
        HudPixelArt(c, ui, art.ItemIcon(id), tx0 + 40, ty + 22, 96, 96);
        HudText(c, ui, f, tx0 + 152, ty + 30, 32, name(id), Ink);
        HudText(c, ui, f, tx0 + 152, ty + 78, 32, "\xC3\x97" + std::to_string(s.items[idx].second), Muted);
    }
    if (count == 0) HudText(c, ui, f, fx + fw / 2, fy + fh / 2 - 16, 32, "Nothing in this pocket", Muted, 1);
    // The page arrows (6 and 7 on the items' screen) and the page.
    const float by = fy + fh - 100;
    for (int k = 0; k < 2; k++)
    {
        const float ax = fx + 50 + k * 300;
        const bool here = s.screen == 1 && s.cursor == 6 + k;
        const uint32_t tint = s.screen == 1 && pages > 1 ? 0xFFFFFFFF : 0x60FFFFFF;
        HudPanel(c, ui, sp, here ? "panels/button_yellow" : "panels/button_white", ax, by, 80, 60, tint);
        HudSprite(c, ui, sp, k ? "cursor/value_arrow_right" : "cursor/value_arrow_left", ax + 24, by + 10, 32, 40, tint);
    }
    char page[32];
    snprintf(page, sizeof page, "Page %d / %d", s.screen > 0 ? s.page + 1 : 1, pages);
    HudText(c, ui, f, fx + 50 + 190, by + 14, 32, page, Ink, 1);

    // The card: the item on the item screen (or under the cursor), else the pocket.
    const float cx = x0 + 1080, cw = 780, cy = 170, ch = 690;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    int item = 0, qty = 0;
    const int at = s.screen == 2 ? s.page * 6 + s.sel : s.screen == 1 && s.cursor >= 0 && s.cursor < 6 ? first + s.cursor : -1;
    if (at >= 0 && at < (int)s.items.size()) item = s.items[at].first, qty = s.items[at].second;
    else if (s.screen == 0 && s.cursor == 4) item = s.lastItem;
    HudSprite(c, ui, sp, "people/chip_ring_orange", cx + cw / 2 - 90, cy + 36, 180, 180);
    HudPixelArt(c, ui, art.ItemIcon(item ? item : kPocketItem[shown]), cx + cw / 2 - 64, cy + 62, 128, 128);
    HudText(c, ui, f, cx + cw / 2, cy + 240, 48, item ? name(item) : kPocket[shown], Ink, 1, true);
    if (qty) HudText(c, ui, f, cx + cw / 2, cy + 306, 32, "\xC3\x97" + std::to_string(qty), Muted, 1);
    std::string desc = !s.message.empty() ? s.message : item ? tx.Get(HudBag::kItemDesc, item) : "";
    if (!desc.empty())
    {
        std::vector<std::string> lines = HudWrap(ui, f, 32, desc, cw - 72 - 60);
        lines.resize(std::min(lines.size(), (size_t)4));
        const float dy = cy + 370;
        HudPanel(c, ui, sp, "panels/dialogue_box", cx + 36, dy, cw - 72, 40 + 50.0f * lines.size());
        for (size_t i = 0; i < lines.size(); i++) HudText(c, ui, f, cx + 66, dy + 22 + 50.0f * i, 32, lines[i], Ink);
    }
    if (s.screen == 2)
    {
        // USE (the game's button 0).
        const bool here = s.cursor == 0;
        const float bw = cw - 72, bx = cx + 36, byy = cy + ch - 30 - 70;
        HudPanel(c, ui, sp, here ? "panels/button_yellow" : "panels/button_white", bx, byy, bw, 70);
        if (here) HudSprite(c, ui, sp, "cursor/cursor_small", bx + 16, byy + 19, 30, 32);
        HudText(c, ui, f, bx + bw / 2, byy + 15, 40, "Use", Ink, 1, true);
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
