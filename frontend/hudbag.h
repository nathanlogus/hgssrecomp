#pragma once
// The bag redrawn in the HUD style (online-HUD design 19-ss-bag): the pockets as discs along
// the top, the current page of the pocket in the yellow frame and the item under the cursor
// on a card. The game keeps taking the input; everything shown is read from its memory each
// frame (src/application/bag in the decompilation), and anything that does not check out
// leaves the game's own picture on screen instead.
#include "hudscreens.h"

namespace HudBag
{
using HudScreens::TitleCase;

// What the game's bag application shows, read from its memory.
struct State
{
    int procState = 0;                       // BagAppState
    bool giving = false;                     // opened to choose an item for a Pokemon to hold
    int pocket = 0;                          // the pocket shown (POCKET_ITEMS...)
    int cursor = 0;                          // BagAppCursorPos: 0-7 pockets, 8-13 items, 14/15 pages, 16 cancel
    int scroll = 0;                          // the first item of the page (pages of 6)
    bool present[8] = {};                    // the pockets this bag has
    std::vector<std::pair<int, int>> items;  // the pocket's items: id, quantity
    // What to do with the item (its context menu), while open: each of its five buttons as
    // the line of the bag's text bank naming it (-1 an empty button), and the cursor.
    bool menu = false;
    int menuText[5] = {};
    int menuSel = 0;
    // How many to trash, while it is chosen: the number, and the most.
    bool quantity = false;
    int qty = 0, qtyMax = 0;
    // The message about the item (trashing it, or why it cannot be used), and the yes/no
    // under it: its cursor, -1 when there is none.
    std::string message;
    int yesNo = -1;
};

constexpr int kCapacity[8] = { 165, 40, 24, 101, 64, 12, 30, 50 }; // sPocketCapacities

inline bool InRam(uint32_t a) { return a >= 0x02000000 && a < 0x02400000; }

// The OverlayManager running the bag: its template holds the bag's three functions (the
// Thumb bit set or not) and it is running the per-frame one (exec_state 2). The copy of the
// template in the overlay's own data is told apart by what follows it. `cache` keeps the
// address while it checks out, so the memory is searched only when the bag starts.
template <class Read>
uint32_t FindManager(Read rd, uint32_t init, uint32_t exec, uint32_t exit, uint32_t& cache)
{
    auto looks = [&](uint32_t a) {
        return (rd(a + 4) & ~1u) == exec && (rd(a) & ~1u) == init && (rd(a + 8) & ~1u) == exit && rd(a + 0x10) == 2 &&
            InRam(rd(a + 0x18)) && InRam(rd(a + 0x1C));
    };
    if (cache && looks(cache)) return cache;
    cache = 0;
    for (uint32_t a = 0x02000000; a < 0x02400000; a += 4)
        if (looks(a)) return cache = a;
    return 0;
}

// sContextMenuFuncs, in the bag's overlay: the function each action of the item's menu runs
// (BagItemContextMenuAction order). The ways of using an item (Use, Walk, Check, Plant, the
// Poffin Case, Stop) share one, Cancel and Sell have none, the rest are functions of their
// own: found by that shape (once each time the bag starts) and kept while it still matches.
template <class Read>
uint32_t FindMenuActions(Read rd, uint32_t& cache)
{
    auto looks = [&](uint32_t a) {
        const uint32_t use = rd(a);
        if (!InRam(use) || !(use & 1) || rd(a + 4 * 11) || rd(a + 4 * 13)) return false;
        for (int i : { 1, 2, 3, 4, 15 })
            if (rd(a + 4 * i) != use) return false;
        static const int own[] = { 5, 6, 7, 8, 9, 10, 12, 14 };
        for (int i : own)
        {
            const uint32_t f = rd(a + 4 * i);
            if (!InRam(f) || !(f & 1) || f == use) return false;
            for (int j : own)
                if (j < i && rd(a + 4 * j) == f) return false;
        }
        return true;
    };
    if (cache && looks(cache)) return cache;
    cache = 0;
    for (uint32_t a = 0x02000000; a < 0x02400000; a += 4)
        if (looks(a)) return cache = a;
    return 0;
}

// The context menu's buttons as the game set them up (BagApp_PrepareContextMenu): the
// function behind each (BagAppData +0x7F0, five), told apart through sContextMenuFuncs, and
// named with the bag's text as the game names them. The one way of using an item is named
// by what the game looked at to choose its name (the pocket, the bicycle being ridden); not
// for the GB Sounds, whose name depends on whether they play, which is not read. False when
// anything does not match.
template <class Read>
bool ReadMenu(Read rd, uint32_t view, uint32_t app, uint32_t& actionsCache, int item, State& st)
{
    const uint32_t actions = FindMenuActions(rd, actionsCache);
    if (!actions || (int)(rd(view + 0x66) & 0xFFFF) != item) return false;
    // Each button may hold only what the game puts there: using, then trashing,
    // (de)registering, giving, moving; Cancel has no function.
    static const int allowed[4][3] = { { 0, -1, -1 }, { 5, 6, 7 }, { 8, -1, -1 }, { 12, -1, -1 } };
    static const int name[16] = { 0, 6, 16, 98, 99, 1, 2, 18, 3, 4, 5, 8, 75, 86, 0, 128 };
    if (rd(app + 0x7F0 + 16)) return false;
    st.menuText[4] = 8;
    for (int b = 0; b < 4; b++)
    {
        const uint32_t f = rd(app + 0x7F0 + 4 * b);
        st.menuText[b] = -1;
        if (!f) continue;
        for (int k : allowed[b])
            if (k >= 0 && rd(actions + 4 * k) == f) st.menuText[b] = name[k];
        if (st.menuText[b] < 0) return false;
    }
    if (st.menuText[0] == 0)
    {
        const int pocketId = (int)(rd(view + 4 + 12 * st.pocket + 8) & 0xFF);
        if (item == 502) return false; // ITEM_GB_SOUNDS: "Use" or "Stop"
        st.menuText[0] = pocketId == 5 ? 16 : item == 450 && (rd(view + 0x76) & 1) ? 6 : 0; // mail: Check; the bicycle: Walk
    }
    st.menuSel = (int)rd(app + 0x66C);
    if (st.menuSel < 0 || st.menuSel > 4) return false;
    st.menu = true;
    return true;
}

// Reads the bag's state through its OverlayManager: args is the BagView, data the
// BagAppData (whose bagView, at 0x234, must point back at it). False when anything is out
// of the ranges the game keeps it in. `actions`: where sContextMenuFuncs was found (FindMenuActions).
template <class Read>
bool ReadState(Read rd, uint32_t man, State& st, uint32_t& actions)
{
    auto rd8 = [&](uint32_t a) { return (int)(rd(a) & 0xFF); };
    auto rd16 = [&](uint32_t a) { return (int)(rd(a) & 0xFFFF); };
    const uint32_t view = rd(man + 0x18), app = rd(man + 0x1C);
    if (rd(app + 0x234) != view) return false;
    st.procState = (int)rd(man + 0x14);
    st.pocket = rd8(view + 0x64);
    st.cursor = (int)rd(app + 0x644);
    // Only the bag opened from the menu or to give a Pokemon an item (not selling or the berry pots).
    st.giving = rd8(view + 0x65) == 1;
    if (st.procState < 0 || st.procState > 37 || st.pocket >= 8 || rd8(view + 0x65) > 1 || st.cursor < 0 || st.cursor > 20)
        return false;
    int pockets = 0;
    for (int i = 0; i < 8; i++)
    {
        const uint32_t p = view + 4 + 12 * i, slots = rd(p);
        st.present[i] = slots != 0;
        if (!slots) continue;
        if (!InRam(slots) || rd8(p + 8) != i) return false;
        pockets++;
    }
    if (!st.present[st.pocket] || rd8(app + 0x614) != pockets) return false;
    const uint32_t p = view + 4 + 12 * st.pocket, slots = rd(p);
    const int count = rd8(p + 9);
    st.scroll = (int16_t)rd16(p + 6);
    if (count > kCapacity[st.pocket] || st.scroll < 0 || st.scroll % 6 || st.scroll > std::max(0, (count - 1) / 6 * 6)) return false;
    st.items.clear();
    for (int k = 0; k < count; k++)
    {
        const uint32_t w = rd(slots + 4 * k);
        const int id = (int)(w & 0xFFFF), qty = (int)(w >> 16);
        if (id < 1 || id > 536 || qty < 1 || qty > 999) return false;
        st.items.push_back({ id, qty });
    }
    // The slot after the list is empty, or the bag has changed since the game counted it.
    if (count < kCapacity[st.pocket])
    {
        const uint32_t w = rd(slots + 4 * count);
        if ((w & 0xFFFF) && (w >> 16)) return false;
    }
    // The item's menu while it takes the input (4) or a button chosen in it is animated (35,
    // then 28 or 29: spriteAnimTransitionMgr.nextState at +0x944); how many to trash (5, or
    // 35 then 6 or 7; BagAppData quantity +0x680 of maxQuantity +0x682, the item's quantity);
    // the messages about trashing it, why it cannot be used or held (8-12, 15; formattedStrbuf
    // +0x5E4), with the yes/no of 9 (its YesNoPrompt +0x804, placed by BagApp_CreateYesNoPrompt
    // at 25, 6; the cursor at +0x75).
    const int next = (int)rd(app + 0x944);
    const bool inMenu = st.procState == 4 || (st.procState == 35 && (next == 28 || next == 29));
    const bool inQuantity = st.procState == 5 || (st.procState == 35 && (next == 6 || next == 7));
    const bool inMessage = (st.procState >= 8 && st.procState <= 12) || st.procState == 15;
    if (inMenu || inQuantity || inMessage)
    {
        const int slot = st.scroll + st.cursor - 8;
        if (st.cursor < 8 || st.cursor > 13 || slot >= count) return false;
        if (inMenu) return ReadMenu(rd, view, app, actions, st.items[slot].first, st);
        if (inMessage)
        {
            if (!HudScreens::GameString([&](uint32_t a) { return rd16(a); }, rd(app + 0x5E4), st.message) || st.message.empty())
                return false;
            if (st.procState != 9) return true;
            // Once answered, the prompt is freed a few frames before the state moves on: the
            // message alone then.
            const uint32_t yn = rd(app + 0x804);
            st.yesNo = rd8(yn + 0x75);
            if (!InRam(yn) || rd8(yn + 0x70) != 25 || rd8(yn + 0x71) != 6 || st.yesNo > 1) st.yesNo = -1;
            return true;
        }
        st.qty = (int16_t)rd16(app + 0x680);
        st.qtyMax = rd16(app + 0x682);
        if (st.qty < 1 || st.qty > st.qtyMax || st.qtyMax != st.items[slot].second) return false;
        st.quantity = true;
    }
    return true;
}

// Whether the redrawn bag is shown in this state: while the list, the item's menu, the
// quantity to trash or the messages about it take the input and the pages turn, and on the
// way in and out from there; moving an item, registering it and the berry pots are the
// game's own picture. `before`: shown the frame before.
inline bool Shown(const State& st, bool before)
{
    switch (st.procState)
    {
    case 0:  // fading in
    case 1:  // the list
    case 4:  // the item's menu
    case 5:  // how many to trash
    case 8:  // asking whether to trash them, then yes or no
    case 9:
    case 10: // trashed
    case 11:
    case 12: // why the item cannot be used
    case 14: // choosing an item to give a Pokemon
    case 15: // why it cannot be held
    case 30: // turning a page
    case 31: return true;
    case 6:  // asking whether to trash them
    case 7:  // not trashing after all
    case 27: // opening the item's menu
    case 28: // closing it
    case 29: // doing what was chosen in it
    case 35: // a button's animation, then the list or a sub-menu
    case 36: // closing
    case 37: return before;
    default: return false;
    }
}

// The ROM's text, kept once read.
class Texts
{
public:
    Texts(const uint8_t* rom, size_t size) : rom(rom), size(size) {}
    const std::string& Get(int bank, int index)
    {
        auto it = cache.find(bank << 16 | index);
        if (it != cache.end()) return it->second;
        return cache[bank << 16 | index] = NdsMessage(rom, size, bank, index);
    }

private:
    const uint8_t* rom;
    size_t size;
    std::map<int, std::string> cache;
};

// Text banks: the bag's messages, item descriptions, item names, move names.
constexpr int kBagMsg = 10, kItemDesc = 221, kItemName = 222, kMoveName = 750;

// The pockets' names. The game's own (bank 225) are drawn in a font of their own, so not
// readable as text; the ROM is the US one only.
constexpr const char* kPocketName[8] = { "Items", "Medicine", "Pok\xC3\xA9 Balls", "TMs & HMs", "Berries", "Mail", "Battle Items",
    "Key Items" };

// A pocket's description (bank 10 from line 120), without its first line, the pocket's
// name ("Medicine Pocket").
inline std::string PocketDescription(Texts& tx, int pocket)
{
    const std::string& s = tx.Get(kBagMsg, 120 + pocket);
    const size_t at = s.find("Pocket ");
    return at == std::string::npos ? s : s.substr(at + 7);
}

// An item standing for each pocket on its disc: Repel, Potion, Poké Ball, TM01, Oran Berry,
// Grass Mail, X Attack, Bicycle.
constexpr int kPocketItem[8] = { 79, 17, 4, 328, 155, 137, 57, 450 };

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, Texts& tx, const State& st,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_lime", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;
    const int count = (int)st.items.size();

    // The pockets: the one shown on an orange ring, the cursor beside the one under it.
    HudText(c, ui, f, x0 + 60, 66, 48, "BAG", Ink, 0, true);
    HudSprite(c, ui, sp, "buttons/button_l", x0 + 250, 73, 52, 38);
    float px = x0 + 330;
    for (int i = 0; i < 8; i++)
    {
        const bool cur = i == st.pocket;
        const float d = cur ? 104 : 84, dx = px + (104 - d) / 2, dy = 92 - d / 2;
        HudSprite(c, ui, sp, !st.present[i] ? "people/chip_empty" : cur ? "people/chip_ring_orange" : "people/chip_ring_grey", dx, dy, d, d);
        if (st.present[i]) HudPixelArt(c, ui, art.ItemIcon(kPocketItem[i]), dx + d / 2 - 32, dy + d / 2 - 32, 64, 64);
        if (st.cursor == i) HudSprite(c, ui, sp, "cursor/cursor_small", dx - 28, 76, 30, 32);
        px += 116;
    }
    HudSprite(c, ui, sp, "buttons/button_r", px + 4, 73, 52, 38);
    const std::string pocketLabel = std::string(kPocketName[st.pocket]) + " \xC2\xB7 " + std::to_string(count) + (count == 1 ? " kind" : " kinds");
    const float pw = HudTextWidth(ui, f, 32, pocketLabel) + 56;
    HudPanel(c, ui, sp, "panels/button_white", x0 + 1860 - pw, 64, pw, 56);
    HudText(c, ui, f, x0 + 1860 - pw / 2, 76, 32, pocketLabel, Ink, 1);

    // The page: two columns of three, as the game lays them out (its d-pad moves across the
    // columns), then the page arrows and the button that closes the bag.
    const float fx = x0 + 60, fy = 170, fw = 960, fh = 690;
    HudPanel(c, ui, sp, "panels/frame_yellow", fx, fy, fw, fh);
    const int sel = st.cursor >= 8 && st.cursor <= 13 && st.scroll + st.cursor - 8 < count ? st.scroll + st.cursor - 8 : -1;
    for (int k = 0; k < 6; k++)
    {
        // Empty places are drawn too, as the game's cursor can stand on them.
        const int idx = st.scroll + k;
        const bool here = st.cursor == 8 + k;
        const float tw = 420, th = 150, tx0 = fx + 50 + (k % 2) * (tw + 20), ty = fy + 46 + (k / 2) * 166;
        HudPanel(c, ui, sp, here ? "panels/row_selected" : idx < count ? "panels/row" : "panels/row_disabled", tx0, ty, tw, th,
            here || idx < count ? 0xFFFFFFFF : 0x80FFFFFF);
        if (here) HudSprite(c, ui, sp, "cursor/cursor_small", tx0 + 8, ty + th / 2 - 16, 30, 32);
        if (idx >= count) continue;
        const int id = st.items[idx].first, qty = st.items[idx].second;
        HudPixelArt(c, ui, art.ItemIcon(id), tx0 + 40, ty + 27, 96, 96);
        const int move = art.TmMove(id);
        HudText(c, ui, f, tx0 + 152, ty + 34, 32, move ? tx.Get(kMoveName, move) : tx.Get(kItemName, id), Ink);
        const std::string q = "\xC3\x97" + std::to_string(qty);
        HudText(c, ui, f, tx0 + 152, ty + 84, 32, move ? tx.Get(kItemName, id) + "  " + q : q, Muted);
    }
    const int pages = std::max(1, (count + 5) / 6);
    const float by = fy + fh - 110;
    char page[32];
    snprintf(page, sizeof page, "Page %d / %d", st.scroll / 6 + 1, pages);
    for (int k = 0; k < 2; k++)
    {
        const float ax = fx + 50 + k * 300;
        // Dimmed with one page, when the game hides its arrows.
        const uint32_t tint = pages > 1 ? 0xFFFFFFFF : 0x60FFFFFF;
        HudPanel(c, ui, sp, st.cursor == 14 + k ? "panels/button_yellow" : "panels/button_white", ax, by, 80, 60, tint);
        HudSprite(c, ui, sp, k ? "cursor/value_arrow_right" : "cursor/value_arrow_left", ax + 24, by + 10, 32, 40, tint);
    }
    HudText(c, ui, f, fx + 50 + 190, by + 14, 32, page, Ink, 1);
    HudPanel(c, ui, sp, st.cursor == 16 ? "panels/button_yellow" : "panels/button_white", fx + fw - 50 - 260, by, 260, 60);
    if (st.cursor == 16) HudSprite(c, ui, sp, "cursor/cursor_small", fx + fw - 50 - 250, by + 14, 30, 32);
    HudText(c, ui, f, fx + fw - 50 - 130, by + 14, 32, "Close bag", Ink, 1);

    // The card: the item under the cursor, or else the pocket (with its description when
    // the cursor is on its disc, as the game prints it).
    const float cx = x0 + 1080, cw = 780, cy = 170, ch = 690;
    HudPanel(c, ui, sp, "panels/card", cx, cy, cw, ch);
    const int item = sel >= 0 ? st.items[sel].first : 0;
    const int shownPocket = st.cursor < 8 ? st.cursor : st.pocket;
    HudSprite(c, ui, sp, "people/chip_ring_orange", cx + cw / 2 - 90, cy + 36, 180, 180);
    HudPixelArt(c, ui, art.ItemIcon(item ? item : kPocketItem[shownPocket]), cx + cw / 2 - 64, cy + 62, 128, 128);
    std::string title, sub, desc;
    if (item)
    {
        const int move = art.TmMove(item);
        title = tx.Get(kItemName, item);
        if (move) title += " " + tx.Get(kMoveName, move);
        sub = "\xC3\x97" + std::to_string(st.items[sel].second);
        const int price = art.ItemPrice(item);
        if (price > 0) sub += " \xC2\xB7 $" + HudScreens::Thousands((uint32_t)price) + " each";
        desc = tx.Get(kItemDesc, item);
    }
    else
    {
        title = kPocketName[shownPocket];
        if (st.cursor < 8) desc = PocketDescription(tx, shownPocket);
    }
    HudText(c, ui, f, cx + cw / 2, cy + 240, 48, title, Ink, 1, true);
    if (!sub.empty()) HudText(c, ui, f, cx + cw / 2, cy + 306, 32, sub, Muted, 1);
    // Under the description, the item's menu or the quantity to trash take the bottom of the
    // card (so the description keeps two lines then); a message about the item takes the
    // description's place, with its yes/no under it.
    const bool below = st.menu || st.quantity || st.yesNo >= 0;
    if (!st.message.empty()) desc = st.message;
    if (!desc.empty())
    {
        std::vector<std::string> lines = HudWrap(ui, f, 32, desc, cw - 72 - 60);
        lines.resize(std::min(lines.size(), (size_t)(below ? 2 : 5)));
        const float dy = cy + 370;
        HudPanel(c, ui, sp, "panels/dialogue_box", cx + 36, dy, cw - 72, 40 + 50.0f * lines.size());
        for (size_t i = 0; i < lines.size(); i++) HudText(c, ui, f, cx + 66, dy + 22 + 50.0f * i, 32, lines[i], Ink);
    }
    auto button = [&](float x, float y, float w, const std::string& label, bool on, bool empty, uint32_t ink) {
        HudPanel(c, ui, sp, on ? "panels/button_yellow" : empty ? "panels/row_disabled" : "panels/button_white", x, y, w, 60,
            empty && !on ? 0x80FFFFFF : 0xFFFFFFFF);
        if (on) HudSprite(c, ui, sp, "cursor/cursor_small", x + 12, y + 14, 30, 32);
        if (!empty) HudText(c, ui, f, x + w / 2, y + 14, 32, label, ink, 1);
    };
    const float by0 = cy + ch - 30 - 60 - 16 - 60; // two rows of buttons at the bottom of the card
    if (st.menu)
    {
        // As the game lays its buttons out: two above, two and Cancel below.
        const float bw2 = (cw - 72 - 16) / 2, bw3 = (cw - 72 - 32) / 3;
        for (int b = 0; b < 5; b++)
        {
            const bool top = b < 2;
            const float w = top ? bw2 : bw3, x = cx + 36 + (top ? b : b - 2) * (w + 16), y = top ? by0 : by0 + 76;
            const int t = st.menuText[b];
            button(x, y, w, t < 0 ? "" : TitleCase(tx.Get(kBagMsg, t)), b == st.menuSel, t < 0, t == 1 ? Danger : Ink);
        }
    }
    else if (st.quantity)
    {
        // The number, with the arrows that change it, then what A and B do.
        const float qw = 300, qx = cx + cw / 2 - qw / 2;
        HudPanel(c, ui, sp, "panels/button_white", qx, by0, qw, 60);
        HudSprite(c, ui, sp, "icons/icon_arrow_down", qx + 18, by0 + 10, 40, 40);
        HudSprite(c, ui, sp, "icons/icon_arrow_up", qx + qw - 58, by0 + 10, 40, 40);
        HudText(c, ui, f, cx + cw / 2, by0 + 8, 40, "\xC3\x97" + std::to_string(st.qty), Ink, 1, true);
        HudText(c, ui, f, qx + qw + 20, by0 + 14, 32, "/ " + std::to_string(st.qtyMax), Muted);
        const float bw = (cw - 72 - 16) / 2;
        button(cx + 36, by0 + 76, bw, TitleCase(tx.Get(kBagMsg, 1)), true, false, Danger);
        button(cx + 36 + bw + 16, by0 + 76, bw, TitleCase(tx.Get(kBagMsg, 8)), false, false, Ink);
    }
    else if (st.yesNo >= 0)
    {
        const float bw = (cw - 72 - 16) / 2;
        button(cx + 36, by0 + 76, bw, "Yes", st.yesNo == 0, false, Ink);
        button(cx + 36 + bw + 16, by0 + 76, bw, "No", st.yesNo == 1, false, Ink);
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
