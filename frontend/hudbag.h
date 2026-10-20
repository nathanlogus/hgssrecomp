#pragma once
// The bag redrawn in the HUD style (online-HUD design 19-ss-bag): the pockets as discs along
// the top, the current page of the pocket in the yellow frame and the item under the cursor
// on a card. The game keeps taking the input; everything shown is read from its memory each
// frame (src/application/bag in the decompilation), and anything that does not check out
// leaves the game's own picture on screen instead.
#include "hudscreens.h"

namespace HudBag
{
// What the game's bag application shows, read from its memory.
struct State
{
    int procState = 0;                       // BagAppState
    int pocket = 0;                          // the pocket shown (POCKET_ITEMS...)
    int cursor = 0;                          // BagAppCursorPos: 0-7 pockets, 8-13 items, 14/15 pages, 16 cancel
    int scroll = 0;                          // the first item of the page (pages of 6)
    bool present[8] = {};                    // the pockets this bag has
    std::vector<std::pair<int, int>> items;  // the pocket's items: id, quantity
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

// Reads the bag's state through its OverlayManager: args is the BagView, data the
// BagAppData (whose bagView, at 0x234, must point back at it). False when anything is out
// of the ranges the game keeps it in.
template <class Read>
bool ReadState(Read rd, uint32_t man, State& st)
{
    auto rd8 = [&](uint32_t a) { return (int)(rd(a) & 0xFF); };
    auto rd16 = [&](uint32_t a) { return (int)(rd(a) & 0xFFFF); };
    const uint32_t view = rd(man + 0x18), app = rd(man + 0x1C);
    if (rd(app + 0x234) != view) return false;
    st.procState = (int)rd(man + 0x14);
    st.pocket = rd8(view + 0x64);
    st.cursor = (int)rd(app + 0x644);
    // Only the bag opened from the menu (not giving an item, selling or the berry pots).
    if (st.procState < 0 || st.procState > 37 || st.pocket >= 8 || rd8(view + 0x65) != 0 || st.cursor < 0 || st.cursor > 20)
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
    return true;
}

// Whether the redrawn bag is shown in this state: while the list takes the input and the
// pages turn, and on the way in and out from there; the sub-menus (what to do with an item,
// how many, messages, yes/no, moving an item) are the game's own picture. `before`: shown
// the frame before.
inline bool Shown(const State& st, bool before)
{
    switch (st.procState)
    {
    case 0:  // fading in
    case 1:  // the list
    case 30: // turning a page
    case 31: return true;
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
        HudPanel(c, ui, sp, st.cursor == 14 + k ? "panels/button_yellow" : "panels/button_white", ax, by, 80, 60);
        HudSprite(c, ui, sp, k ? "cursor/value_arrow_right" : "cursor/value_arrow_left", ax + 24, by + 10, 32, 40);
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
    if (!desc.empty())
    {
        std::vector<std::string> lines = HudWrap(ui, f, 32, desc, cw - 72 - 60);
        if (lines.size() > 5) lines.resize(5);
        const float dy = cy + 370;
        HudPanel(c, ui, sp, "panels/dialogue_box", cx + 36, dy, cw - 72, 40 + 50.0f * lines.size());
        for (size_t i = 0; i < lines.size(); i++) HudText(c, ui, f, cx + 66, dy + 22 + 50.0f * i, 32, lines[i], Ink);
    }
    DrawHintBar(c, ui, sp, f, hints);
}
}
