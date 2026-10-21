#pragma once
// Buying at a Poké Mart (and the Goldenrod Department Store's counters, which are the same
// shop) redrawn in the HUD style: the items for sale with their prices, the item under the
// cursor with its description, how many and the total, the money and the shop's messages.
// The game keeps the input; everything is read each frame from its shop task
// (src/overlay_03/shop_menu.c, MartData) and anything that does not check out leaves the
// game's own picture on screen. Only the money shops: the Pokéathlon Dome's (Athlete
// Points) and the seal shops are left as the game draws them.
#include "hudbag.h"

namespace HudShop
{
struct State
{
    int step = 0;           // Task_Mart's state (TASK_MART_*)
    int next = 0;           // during a button's animation (21), the state after it
    std::vector<int> items; // the items for sale
    int page = 0;           // the first item shown (pages of 6)
    int cursor = 0;         // 0-5 the items of the page, 8 Cancel
    int item = 0, qty = 0, maxQty = 0, cost = 0;
    int inBag = 0;          // how many of the item under the cursor the bag holds
    uint32_t money = 0;
    int yesNo = -1;         // the cursor of "Is that OK?", -1 when it is not up
};

// The pockets in the save's order with their sizes (save's Bag: items, key items, TMs,
// mail, medicine, berries, balls, battle items), each slot an item and a quantity.
constexpr int kSlots = 165 + 50 + 101 + 12 + 40 + 64 + 24 + 30;

inline bool InRam(uint32_t a) { return a >= 0x02000000 && a < 0x02400000; }

// The bottom screen's work for the shop (overlay 31, not decompiled): it keeps the MartData
// at +0x14 and its yes/no prompt at +0x170 (a YesNoPrompt placed at 26, 6; its cursor at
// +0x75). Found by that shape while the prompt is up, and kept while it still matches.
template <class Rd>
int YesNo(Rd rd, uint32_t mart, uint32_t& cache)
{
    auto prompt = [&](uint32_t w) -> int {
        const uint32_t yn = rd(w + 0x170);
        if (rd(w + 0x14) != mart || !InRam(yn) || (rd(yn + 0x70) & 0xFFFF) != (6 << 8 | 26)) return -1;
        const int sel = (int)(rd(yn + 0x75) & 0xFF);
        return sel <= 1 ? sel : -1;
    };
    if (cache && prompt(cache) >= 0) return prompt(cache);
    cache = 0;
    for (uint32_t a = 0x02200000; a < 0x02400000; a += 4)
        if (rd(a + 0x14) == mart && prompt(a) >= 0) return prompt(cache = a);
    return -1;
}

// Reads the shop from the field's running task (FieldSystem +0x10: its function +4, its
// data +0x0C) when that is the shop's task (in overlay 3) on a MartData that checks out.
// `ynCache`: where the yes/no's work was found (YesNo).
template <class Rd>
bool Read(Rd rd, State& st, uint32_t& ynCache)
{
    auto rd8 = [&](uint32_t a) { return (int)(rd(a) & 0xFF); };
    auto rd16 = [&](uint32_t a) { return (int)(rd(a) & 0xFFFF); };
    const uint32_t fs = rd(0x021D4158);
    if (!InRam(fs)) return false;
    const uint32_t task = rd(fs + 0x10);
    if (!InRam(task)) return false;
    const uint32_t func = rd(task + 4), d = rd(task + 0x0C);
    if (!(func & 1) || func < 0x02253E20 || func >= 0x022598A0 || !InRam(d)) return false;
    // Its bg config is the field's, its first window the description's (main BG 2 at 13, 2,
    // 18 x 14 tiles; removed as the shop closes, from step 17), and it is a shop of items
    // bought with money.
    st.step = rd8(d + 0x272);
    const bool closing = st.step == 17 || st.step == 18 || st.step == 27; // (27: TASK_MART_EXIT)
    if (rd(d) != rd(fs + 0x08) || rd8(d + 0x283) != 0 ||
        (!closing && (rd(d + 0x0C) != (2u | 13u << 8 | 2u << 16 | 18u << 24) || rd8(d + 0x10) != 14)))
        return false;
    st.next = rd8(d + 0x29F);
    const int count = rd8(d + 0x270);
    st.page = rd8(d + 0x271);
    st.cursor = (int)rd(d + 0x290);
    const uint32_t items = rd(d + 0x268);
    if (count < 1 || count > 64 || !InRam(items) || st.page % 6 || st.page >= count || st.cursor < 0 || st.cursor > 8 ||
        st.cursor == 6 || st.cursor == 7 || rd8(d + 0x273) != 0)
        return false;
    st.items.clear();
    for (int k = 0; k < count; k++)
    {
        const int id = rd16(items + 2 * k);
        if (id < 1 || id > 536) return false;
        st.items.push_back(id);
    }
    st.item = rd16(d + 0x284);
    st.qty = (int16_t)rd16(d + 0x286);
    st.maxQty = rd16(d + 0x288);
    st.cost = (int)rd(d + 0x28C);
    const uint32_t profile = rd(d + 0x248), bag = rd(d + 0x24C);
    if (!InRam(profile) || !InRam(bag)) return false;
    st.money = rd(profile + 0x14);
    if (st.money > 999999) return false;
    // Once an item is chosen (5 onwards), it is the one under the cursor at its price.
    const int under = st.cursor < 6 && st.page + st.cursor < count ? st.items[st.page + st.cursor] : 0;
    const bool chosen = st.step >= 5 && st.step <= 15 && st.step != 6;
    if (chosen && (st.item != under || st.cost <= 0 || st.qty < 0 || st.qty > 99 || st.maxQty > 99)) return false;
    st.inBag = 0;
    for (int k = 0; k < kSlots && under; k++)
        if (rd16(bag + 4 * k) == under) st.inBag += rd16(bag + 4 * k + 2);
    st.yesNo = st.step == 11 ? YesNo(rd, d, ynCache) : -1;
    return true;
}

// While the bag is open to sell (the shop's task waiting on it, Task_Bag_SellMenu, with the
// MartData of a money shop opened to sell), the player's money; -1 if that does not check out.
template <class Rd>
int64_t SellingMoney(Rd rd)
{
    const uint32_t fs = rd(0x021D4158);
    const uint32_t task = InRam(fs) ? rd(fs + 0x10) : 0;
    const uint32_t d = InRam(task) ? rd(task + 0x0C) : 0;
    if (!InRam(d) || rd(d) != rd(fs + 0x08) || (rd(d + 0x270) >> 16 & 0xFF) != 24 || (rd(d + 0x270) >> 24) != 1 ||
        (rd(d + 0x280) >> 24) != 0)
        return -1;
    const uint32_t profile = rd(d + 0x248);
    const uint32_t money = InRam(profile) ? rd(profile + 0x14) : 0xFFFFFFFF;
    return money <= 999999 ? (int64_t)money : -1;
}

// Whether the redrawn shop is shown in this state: all the way from opening (1) to closing
// (18), so the game's own shop does not flash up on the way in or out; not selling (22-26),
// which is the bag.
inline bool Shown(const State& st) { return (st.step >= 1 && st.step <= 21) || st.step == 27; }

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, HudBag::Texts& tx, const State& st,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    using HudScreens::Thousands;
    DrawBackdrop(c, sp, "backgrounds/bg_aqua", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;
    const int count = (int)st.items.size();
    auto price = [&](int id) {
        const int p = art.ItemPrice(id);
        return p > 0 ? "$" + Thousands((uint32_t)p) : std::string();
    };
    auto name = [&](int id) {
        const int move = art.TmMove(id);
        return move ? tx.Get(HudBag::kItemName, id) + " " + tx.Get(HudBag::kMoveName, move) : tx.Get(HudBag::kItemName, id);
    };

    // The title and the money.
    HudText(c, ui, f, x0 + 60, 66, 48, "BUY", Ink, 0, true);
    const std::string money = "$" + Thousands(st.money);
    const float mw = HudTextWidth(ui, f, 32, money, true) + HudTextWidth(ui, f, 32, "Money") + 80;
    HudPanel(c, ui, sp, "panels/button_white", x0 + 1860 - mw, 64, mw, 56);
    HudText(c, ui, f, x0 + 1860 - mw + 28, 76, 32, "Money", Muted);
    HudText(c, ui, f, x0 + 1860 - 28, 76, 32, money, Ink, 2, true);

    // The page: two columns of three, as the game lays them out, then the page arrows and Cancel.
    const float fx = x0 + 60, fy = 170, fw = 960, fh = 690;
    HudPanel(c, ui, sp, "panels/frame_yellow", fx, fy, fw, fh);
    for (int k = 0; k < 6; k++)
    {
        const int idx = st.page + k;
        const bool here = st.cursor == k; // (empty places too: the game's cursor can stand on them)
        const float tw = 420, th = 150, tx0 = fx + 50 + (k % 2) * (tw + 20), ty = fy + 46 + (k / 2) * 166;
        HudPanel(c, ui, sp, here ? "panels/row_selected" : idx < count ? "panels/row" : "panels/row_disabled", tx0, ty, tw, th,
            here || idx < count ? 0xFFFFFFFF : 0x80FFFFFF);
        if (here) HudSprite(c, ui, sp, "cursor/cursor_small", tx0 + 8, ty + th / 2 - 16, 30, 32);
        if (idx >= count) continue;
        const int id = st.items[idx];
        HudPixelArt(c, ui, art.ItemIcon(id), tx0 + 40, ty + 27, 96, 96);
        HudText(c, ui, f, tx0 + 152, ty + 34, 32, tx.Get(HudBag::kItemName, id), Ink);
        HudText(c, ui, f, tx0 + 152, ty + 84, 32, price(id), Muted);
    }
    const int pages = (count + 5) / 6;
    const float by = fy + fh - 110;
    char page[32];
    snprintf(page, sizeof page, "Page %d / %d", st.page / 6 + 1, pages);
    for (int k = 0; k < 2; k++)
    {
        // Dimmed when there is no page that way, as the game hides its arrow.
        const bool on = k ? st.page + 6 < count : st.page > 0;
        const float ax = fx + 50 + k * 300;
        HudPanel(c, ui, sp, "panels/button_white", ax, by, 80, 60, on ? 0xFFFFFFFF : 0x60FFFFFF);
        HudSprite(c, ui, sp, k ? "cursor/value_arrow_right" : "cursor/value_arrow_left", ax + 24, by + 10, 32, 40, on ? 0xFFFFFFFF : 0x60FFFFFF);
    }
    HudText(c, ui, f, fx + 50 + 190, by + 14, 32, page, Ink, 1);
    HudPanel(c, ui, sp, st.cursor == 8 ? "panels/button_yellow" : "panels/button_white", fx + fw - 50 - 260, by, 260, 60);
    if (st.cursor == 8) HudSprite(c, ui, sp, "cursor/cursor_small", fx + fw - 50 - 250, by + 14, 30, 32);
    HudText(c, ui, f, fx + fw - 50 - 130, by + 14, 32, "Cancel", Ink, 1);

    // The card: the item under the cursor, its price and how many are in the bag, its
    // description or the shop's message about it, and how many to buy with the total.
    const float cx = x0 + 1080, cw = 780, cy = 170, ch = 690;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    const int item = st.cursor < 6 && st.page + st.cursor < count ? st.items[st.page + st.cursor] : 0;
    if (!item)
    {
        // Cancel, or an empty place (the game shows nothing about it).
        HudSprite(c, ui, sp, st.cursor == 8 ? "people/chip_ring_grey" : "people/chip_empty", cx + cw / 2 - 90, cy + 36, 180, 180);
        if (st.cursor == 8)
        {
            HudSprite(c, ui, sp, "icons/icon_exit", cx + cw / 2 - 40, cy + 86, 80, 80);
            HudText(c, ui, f, cx + cw / 2, cy + 240, 48, "Cancel", Ink, 1, true);
            HudText(c, ui, f, cx + cw / 2, cy + 306, 32, "Done shopping", Muted, 1);
        }
        DrawHintBar(c, ui, sp, f, hints);
        return;
    }
    HudSprite(c, ui, sp, "people/chip_ring_orange", cx + cw / 2 - 90, cy + 36, 180, 180);
    HudPixelArt(c, ui, art.ItemIcon(item), cx + cw / 2 - 64, cy + 62, 128, 128);
    HudText(c, ui, f, cx + cw / 2, cy + 240, 48, name(item), Ink, 1, true);
    HudText(c, ui, f, cx + cw / 2, cy + 306, 32, price(item) + " each \xC2\xB7 In bag \xC3\x97" + std::to_string(st.inBag), Muted, 1);

    // The shop's words at each step (its own, from its text bank 435, with the numbers in).
    const std::string what = tx.Get(HudBag::kItemName, item);
    std::string message;
    const bool picking = st.step == 5 || st.step == 7 || (st.step == 21 && (st.next == 8 || st.next == 9));
    if (picking) message = what + "? Certainly. How many would you like?";
    else if (st.step == 8 || st.step == 10 || st.step == 11)
        message = "For " + std::to_string(st.qty) + ", that will be $" + Thousands((uint32_t)(st.qty * st.cost)) + ", please.";
    else if ((st.step == 12 || st.step == 13) && st.qty > 0) message = "Here you are! Thank you! You put away the " + what + " in the Bag.";
    else if (st.step == 13) message = "You don\xE2\x80\x99t have enough room.";
    else if (st.step == 14) message = "You don\xE2\x80\x99t have enough money.";
    else if (st.step == 15) message = "You also get a Premier Ball as an added bonus.";
    const bool quantity = picking || (st.step >= 8 && st.step <= 13 && st.step != 9 && st.qty > 0) || st.step == 15;
    const bool below = quantity || st.yesNo >= 0;
    const std::string desc = message.empty() ? tx.Get(HudBag::kItemDesc, item) : message;
    std::vector<std::string> lines = HudWrap(ui, f, 32, desc, cw - 72 - 60);
    lines.resize(std::min(lines.size(), (size_t)(below ? 3 : 5)));
    const float dy = cy + 370;
    HudPanel(c, ui, sp, "panels/dialogue_box", cx + 36, dy, cw - 72, 40 + 50.0f * lines.size());
    for (size_t i = 0; i < lines.size(); i++) HudText(c, ui, f, cx + 66, dy + 22 + 50.0f * i, 32, lines[i], Ink);

    const float by0 = cy + ch - 30 - 60;
    if (quantity && st.yesNo < 0)
    {
        // The number with the arrows that change it (up and down by one, left and right by
        // ten), the most the money buys, and the total.
        const float qw = 300, qx = cx + 36;
        HudPanel(c, ui, sp, picking ? "panels/button_yellow" : "panels/button_white", qx, by0, qw, 60);
        if (picking)
        {
            HudSprite(c, ui, sp, "icons/icon_arrow_down", qx + 18, by0 + 10, 40, 40);
            HudSprite(c, ui, sp, "icons/icon_arrow_up", qx + qw - 58, by0 + 10, 40, 40);
        }
        HudText(c, ui, f, qx + qw / 2, by0 + 8, 40, "\xC3\x97" + std::to_string(st.qty), Ink, 1, true);
        HudText(c, ui, f, qx + qw + 20, by0 + 14, 32, "/ " + std::to_string(st.maxQty), Muted);
        const std::string total = "$" + Thousands((uint32_t)(st.qty * st.cost));
        HudText(c, ui, f, cx + cw - 36, by0 + 8, 40, total, Ink, 2, true);
        HudText(c, ui, f, cx + cw - 36 - HudTextWidth(ui, f, 40, total, true) - 16, by0 + 14, 32, "Total", Muted, 2);
    }
    else if (st.yesNo >= 0)
    {
        const float bw = (cw - 72 - 16) / 2;
        for (int k = 0; k < 2; k++)
        {
            const float x = cx + 36 + k * (bw + 16);
            HudPanel(c, ui, sp, st.yesNo == k ? "panels/button_yellow" : "panels/button_white", x, by0, bw, 60);
            if (st.yesNo == k) HudSprite(c, ui, sp, "cursor/cursor_small", x + 12, by0 + 14, 30, 32);
            HudText(c, ui, f, x + bw / 2, by0 + 14, 32, k ? "No" : "Yes", Ink, 1);
        }
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
