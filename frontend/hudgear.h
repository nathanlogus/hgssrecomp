#pragma once
// The Pokégear redrawn in the HUD style (online-HUD design 24-ss-pokegear): the tabs as
// discs along the top, the clock in a pill, and the phone's contact list or the region map
// with the player (and, online, the other players of the session) on it. The game keeps
// taking the input; everything shown is read from its memory each frame
// (src/application/pokegear in the decompilation), and any step that cannot be mirrored
// (calls, the phone's menus, the map zoomed or being marked, the radio, the settings)
// leaves the game's own picture on screen.
#include "hudbag.h"
#include "party.h"

namespace HudGear
{
// The Pokégear's applications (PokegearAppId): its tabs, and the close button.
enum { APP_CONFIGURE, APP_RADIO, APP_MAP, APP_PHONE, APP_CANCEL };

// A place on the region map (PokegearMapLocationSpec): its map and its cells.
struct Place
{
    int map = 0, x = 0, y = 0, w = 1, h = 1;
};

struct State
{
    int app = 0;          // the tab running (APP_MAP or APP_PHONE)
    int cards = 0;        // the cards registered: 1 map, 2 radio
    bool inTabs = false;  // the cursor is on the tab buttons
    int tabCursor = 0;    // the tab (an app) under it
    int hour = 0, minute = 0;
    // The phone: the contacts in the list's order, the first one shown and the cursor's row.
    std::vector<int> contacts;
    int first = 0, row = 0;
    // The map: its places (those the game shows).
    std::vector<Place> places;
    // The cursor's cell, the player's, and the place under the cursor: its map (-1: none) and
    // its line of text bank 273.
    int curX = 0, curY = 0, meX = 0, meY = 0, selMap = -1, selText = 0;
};

// The phone contacts' text banks (sPhoneMessageGmm): line 0 is the contact's name.
constexpr uint16_t kContactBank[75] = { 664, 716, 666, 662, 663, 643, 661, 660, 641, 665, 675, 712, 680, 684, 711, 642, 706, 644,
    648, 704, 685, 688, 713, 714, 667, 691, 645, 649, 646, 650, 651, 647, 656, 657, 658, 655, 652, 653, 654, 659, 674, 705, 677, 698,
    696, 708, 672, 690, 671, 695, 689, 700, 693, 694, 669, 676, 715, 703, 710, 670, 692, 681, 697, 687, 702, 699, 707, 682, 673, 686,
    683, 679, 701, 709, 678 };

// The Pokégear's OverlayManager (as HudBag finds the bag's), its data (PokegearAppData)
// and the tab's own: GearPhone's PokegearPhoneAppData or GearMap's PokegearMapAppData, each
// pointing back at the Pokégear. `app(name)` gives an application's RcApp (or null).
// `step`: the tab's own step (its OverlayManager's state). False when anything is out of
// the ranges the game keeps it in; then `between` says whether the Pokégear is only
// changing tabs (one tab ending or the next starting), when the last picture can stay.
template <class Read, class App>
bool ReadState(Read rd, App app, uint32_t& cache, State& st, int& step, bool& between)
{
    between = false;
    using HudBag::InRam;
    auto rd8 = [&](uint32_t a) { return (int)(rd(a) & 0xFF); };
    auto rd16 = [&](uint32_t a) { return (int)(rd(a) & 0xFFFF); };
    const auto *gear = app("Pokegear"), *phone = app("GearPhone"), *map = app("GearMap");
    if (!gear || !phone || !map) return false;
    const uint32_t man = HudBag::FindManager(rd, gear->initAddr, gear->execAddr, gear->exitAddr, cache);
    if (!man) return false;
    const uint32_t data = rd(man + 0x1C);
    if (rd(data + 0x20) != rd(man + 0x18)) return false;
    // The Pokégear's own step: launching or running a tab (POKEGEAR_APP_MAIN_STATE_*).
    const int gearStep = (int)rd(man + 0x14);
    if (gearStep < 2 || gearStep > 11) return false;
    st.app = rd8(data + 4);
    st.cards = rd8(data + 5) & 0x7F;
    st.inTabs = rd8(data + 6) == 1;
    st.hour = (int)rd(data + 0x80);
    st.minute = (int)rd(data + 0x84);
    if (st.hour < 0 || st.hour > 23 || st.minute < 0 || st.minute > 59 || st.cards > 3) return false;
    // The tab buttons' cursor (PokegearCursorManager's last cursor, 12 bytes a button).
    if (st.inTabs)
    {
        const uint32_t cm = rd(data + 0x7C);
        const uint32_t cur = InRam(cm) ? rd(cm + 8) : 0;
        if (!InRam(cur)) return false;
        const int pos = rd8(cur + 1), last = rd8(cur + 3);
        const uint32_t grid = rd(cur + 4);
        if (pos > last || last > 4 || !InRam(grid)) return false;
        st.tabCursor = rd16(grid + 12 * pos);
        if (st.tabCursor > APP_CANCEL) return false;
    }
    // The tab's own application, run by the Pokégear (childApplication) in its per-frame step.
    const auto* tab = st.app == APP_PHONE ? phone : st.app == APP_MAP ? map : nullptr;
    const uint32_t child = rd(data + 0x70);
    if (!tab || !InRam(child) || (rd(child) & ~1u) != tab->initAddr || (rd(child + 4) & ~1u) != tab->execAddr ||
        (rd(child + 8) & ~1u) != tab->exitAddr || rd(child + 0x18) != data)
    {
        between = true;
        return false;
    }
    const uint32_t own = rd(child + 0x1C);
    step = (int)rd(child + 0x14);
    // Starting (0), ending (2) or done (the phone's 14, the map's 13), or not yet in its
    // per-frame function: between tabs.
    if (rd(child + 0x10) != 2 || step == 0 || step == 2 || step == (st.app == APP_PHONE ? 14 : 13))
    {
        between = true;
        return false;
    }
    if (!InRam(own) || step < 0 || step > 13) return false;
    if (st.app == APP_PHONE)
    {
        // PokegearPhoneAppData: not a call the game started (isIncomingCall), and its list
        // (PhoneContactListUI at 0xE0, a slot of 12 bytes per contact from 0xC) in step
        // with its contacts (each slot's list node holds the contact at +1).
        if (rd(own + 0x0C) != data || rd8(own + 0xCB) != 0) return false;
        const uint32_t ui = own + 0xE0;
        const int n = rd8(ui);
        st.row = rd8(ui + 1);
        st.first = rd8(ui + 3);
        if (n < 1 || n > 75 || n != rd8(own + 0xCD) || st.first >= n || st.row > 5 || st.first + st.row >= n || rd8(ui + 6) >> 1)
            return false; // movingContacts
        st.contacts.clear();
        for (int i = 0; i < n; i++)
        {
            const uint32_t slot = ui + 0xC + 12 * i, node = rd(slot + 4);
            const int id = rd8(slot + 8);
            if (rd8(slot) != i || id >= 75 || !InRam(node) || rd8(node + 1) != id) return false;
            st.contacts.push_back(id);
        }
        return true;
    }
    // PokegearMapAppData: the Pokégear's map (not flying or the Town Map), not zoomed in and
    // not marking places, with its places (16 bytes each).
    const int flags = rd8(own + 0x138), extra = rd8(own + 0x13D);
    if (rd(own + 0x10) != data || rd8(own + 0x0D) != 0 || rd8(own + 0x0C) != 0 || (flags & 1)) return false;

    st.meX = (int16_t)rd16(own + 0x108);
    st.meY = (int16_t)rd16(own + 0x10A);
    st.curX = (int16_t)rd16(own + 0x110);
    st.curY = (int16_t)rd16(own + 0x112);
    const int unlock = flags >> 5 & 3, maxX = rd16(own + 0x102);
    const uint32_t specs = rd(own + 0x214), sel = rd(own + 0x118);
    const int count = rd8(own + 0x136);
    if (rd8(own + 0x0E) > 2 || unlock > 2 || maxX > 45 || !InRam(specs) || count < 1 || count > 200 || st.curX < 0 || st.curX > 45 ||
        st.curY < 0 || st.curY > 20)
        return false;
    st.selMap = -1;
    if (sel)
    {
        if (sel < specs || sel >= specs + 16 * count || (sel - specs) % 16) return false;
        st.selMap = rd16(sel);
        st.selText = rd8(sel + 6);
    }
    st.places.clear();
    for (int i = 0; i < count; i++)
    {
        const uint32_t a = specs + 16 * i;
        Place p;
        p.map = rd16(a);
        p.x = rd8(a + 2);
        p.y = rd8(a + 3);
        p.w = rd8(a + 4) & 15;
        p.h = rd8(a + 4) >> 4;
        if (p.map >= 540 || p.w < 1 || p.h < 1 || p.x > 45 || p.y > 20) return false;
        // Hidden as the game hides them (PokegearMap_ShouldLocationBeHidden, and Kanto before
        // it is open): the Safari Zone, the Sinjoh Ruins and the S.S. Aqua.
        if (((p.map == 151 || p.map == 152 || p.map == 174) && !(extra & 2)) || (p.map == 521 && !(extra & 4)) ||
            (p.map == 307 && !(extra & 8)))
            continue;
        if (p.x > maxX || (unlock == 0 && p.x >= 22 && !(p.x == 25 && p.y == 10))) continue;
        st.places.push_back(p);
    }
    return true;
}

// Whether the redrawn Pokégear is shown at this step of the tab: while it takes the input
// and as it comes in; while it leaves only if it was shown the frame before (`before`).
inline bool Shown(const State& st, int step, bool before)
{
    if (st.app == APP_PHONE)
        switch (step)
        {
        case 1:  // the list
        case 10: // fading in
        case 12: return true; // wiping in from another tab
        case 11: // fading out to close
        case 13: return before; // wiping out to another tab
        default: return false; // calls, menus, sorting
        }
    switch (step)
    {
    case 1:  // the map
    case 4:  // fading in
    case 6: return true;
    case 5:  // fading out
    case 7: return before;
    default: return false; // the markings, the word picker
    }
}

// The region of a cell (Pokegear_RegionFromCoords, with the map's 2 rows on top): 0 Kanto,
// 1 the Indigo Plateau, 2 Johto.
inline int RegionOf(int x, int y)
{
    y -= 2;
    if (x <= 21 || (x == 25 && y == 8)) return 2;
    return (x == 28 && y == 6) || (x == 28 && y > 8 && y < 13) ? 1 : 0;
}

// The ROM's phone book (tel/pmtel_book.dat: a count, then 20 bytes a contact): their
// trainer class (+3) and map (+6).
class Book
{
public:
    Book(const uint8_t* rom, size_t size)
    {
        uint32_t off, len;
        if (NdsRomFile(rom, size, "tel/pmtel_book.dat", off, len) && len >= 4)
        {
            const uint32_t n = RomGet32(rom + off);
            for (uint32_t i = 0; i < n && 4 + 20 * (i + 1) <= len; i++)
                entries.push_back({ rom[off + 4 + 20 * i + 3], (int)RomGet16(rom + off + 4 + 20 * i + 6) });
        }
    }
    // A contact's trainer class, as the phone prints it (PhoneContact_GetClass).
    std::string Class(HudBag::Texts& tx, int id) const
    {
        if (id < 0 || id >= (int)entries.size() || entries[id].first == 200) return ""; // Mom
        const int tc = entries[id].first;
        return tc >= 201 ? tx.Get(271, tc - 201 + 38) : tx.Get(730, tc);
    }
    std::string Place(int id) const { return id >= 0 && id < (int)entries.size() ? Party_PlaceName(entries[id].second) : ""; }

private:
    std::vector<std::pair<int, int>> entries;
};

inline std::string Name(HudBag::Texts& tx, int id) { return id >= 0 && id < 75 ? tx.Get(kContactBank[id], 0) : ""; }

// "Cherrygrove City" -> "Cherrygrove", as a label on the map.
inline std::string ShortPlace(const std::string& s)
{
    for (const char* end : { " City", " Town" })
        if (s.size() > strlen(end) && s.compare(s.size() - strlen(end), std::string::npos, end) == 0) return s.substr(0, s.size() - strlen(end));
    return s;
}

inline bool IsTown(const std::string& s)
{
    for (const char* end : { " City", " Town", " Island", " Plateau" })
        if (s.size() > strlen(end) && s.compare(s.size() - strlen(end), std::string::npos, end) == 0) return true;
    return false;
}

// Where another player is on the region map, as the game places its own player
// (Pokegear's args): on the outdoor map by their position, else by the place their map is
// in, else by their map's world map cell. False when not known.
inline bool PeerCell(const State& st, const PartyPeer& p, uint32_t mapHeaders, const std::function<uint32_t(uint32_t)>& rd, int& x, int& y)
{
    if (p.walk.shown && p.walk.matrix == 0 && p.walk.x >= 0 && p.walk.z >= 0)
    {
        x = p.walk.x >> 21; // 32 tiles of 16 units (fx32) a cell
        y = (p.walk.z >> 21) + 2;
        return true;
    }
    if (p.map < 0 || p.map >= 540) return false;
    for (const Place& pl : st.places)
        if (pl.map == p.map)
        {
            x = pl.x + (pl.w - 1) / 2;
            y = pl.y + (pl.h - 1) / 2;
            return true;
        }
    const uint32_t w = rd(mapHeaders + 24 * p.map + 2) & 0xFFFF;
    x = w >> 4 & 63;
    y = (w >> 10 & 63) + 2;
    return x || y - 2;
}

// What main.cpp gives: this player (name, colour), the day of the week (0 Sunday) and the
// game's memory for the other players' maps.
struct Env
{
    PartyPeer me;
    int weekday = 0;
    std::function<uint32_t(uint32_t)> rd;
};

constexpr uint32_t kMapHeaders = 0x020F6BE0; // sMapHeaders (as party.cpp), 24 bytes a map

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, HudBag::Texts& tx, const Book& book, const State& st,
    const Env& env, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_dusk", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;

    // The tabs in the game's order (its d-pad walks them so), the one running large and
    // yellow, the cursor beside the one under it while it is on them.
    static const char* labels[5] = { "SETTINGS", "RADIO", "MAP", "PHONE", "CLOSE" };
    static const char* icons[5] = { "icons/icon_cog", "icons/icon_radio", "icons/icon_map", "icons/icon_phone", "icons/icon_cross" };
    for (int i = 0; i < 5; i++)
    {
        const bool cur = i == st.app, has = !((i == APP_MAP && !(st.cards & 1)) || (i == APP_RADIO && !(st.cards & 2)));
        const float cx = x0 + 130 + i * 170 + (i == 4 ? 40 : 0), d = cur ? 112 : 88, dy = 84 - d / 2;
        HudSprite(c, ui, sp, !has ? "people/chip_empty" : cur ? "people/chip_ring_orange" : "people/chip_ring_grey", cx - d / 2, dy, d, d,
            has ? 0xFFFFFFFF : 0x80FFFFFF);
        if (has) HudSprite(c, ui, sp, icons[i], cx - 24, 84 - 24, 48, 48);
        HudText(c, ui, f, cx, 146, 32, labels[i], cur ? Yellow : has ? White : 0x80FFFFFF, 1);
        if (st.inTabs && st.tabCursor == i) HudSprite(c, ui, sp, "cursor/cursor_small", cx - d / 2 - 34, 68, 30, 32);
    }

    // The clock: the game's time, the day and the part of the day.
    static const char* days[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    const char* part = st.hour < 4 || st.hour >= 20 ? "Night" : st.hour < 10 ? "Morning" : st.hour < 17 ? "Day" : "Evening";
    char time[8];
    snprintf(time, sizeof time, "%d:%02d", st.hour, st.minute);
    const std::string when = std::string(days[env.weekday % 7]) + " \xC2\xB7 " + part;
    const float pw = 64 + 24 + HudTextWidth(ui, f, 48, time, true) + 24 + HudTextWidth(ui, f, 32, when) + 40;
    const float px = x0 + 1860 - pw;
    HudPanel(c, ui, sp, "panels/button_white", px, 52, pw, 72);
    HudSprite(c, ui, sp, "icons/icon_clock", px + 28, 64, 48, 48);
    HudText(c, ui, f, px + 100, 64, 48, time, Ink, 0, true);
    HudText(c, ui, f, px + pw - 32, 74, 32, when, Muted, 2);

    // The other players of the session, for both tabs.
    std::vector<const PartyPeer*> peers;
    if (Party_Link() == PartyLink::Online)
        for (int i = 0; i < 4; i++)
            if (Party_Peers()[i].present && i != Party_Me()) peers.push_back(&Party_Peers()[i]);

    const float mx = x0 + 60, my = 196, mw = 1180, mh = 790;
    const float sx = x0 + 1290, sw = 570;
    HudPanel(c, ui, sp, "panels/card", mx, my, mw, mh);
    if (st.app == APP_PHONE)
    {
        // The list: the page of six the game shows, the cursor on its row.
        const int n = (int)st.contacts.size();
        HudOnline::TitledCard(c, ui, sp, f, mx + 24, my + 24, mw - 48, 60, "PHONE", std::to_string(n) + (n == 1 ? " contact" : " contacts"));
        for (int k = 0; k < 6 && st.first + k < n; k++)
        {
            const int id = st.contacts[st.first + k];
            const bool here = !st.inTabs && k == st.row;
            const float ry = my + 112 + k * 108, rx = mx + 40, rw = mw - 120;
            HudPanel(c, ui, sp, here ? "panels/row_selected" : "panels/row", rx, ry, rw, 92);
            if (here) HudSprite(c, ui, sp, "cursor/cursor_small", rx + 10, ry + 30, 30, 32);
            const std::string name = Name(tx, id);
            HudSprite(c, ui, sp, here ? "people/chip_ring_orange" : "people/chip_ring_grey", rx + 50, ry + 10, 72, 72);
            HudText(c, ui, f, rx + 86, ry + 30, 32, HudOnline::Initials(name), Ink, 1);
            HudText(c, ui, f, rx + 146, ry + 10, 32, name, Ink);
            const std::string cls = book.Class(tx, id), place = book.Place(id);
            HudText(c, ui, f, rx + 146, ry + 48, 32, cls.empty() ? place : place.empty() ? cls : cls + " \xC2\xB7 " + place, Muted);
        }
        // The arrows the game shows when there is more above or below.
        if (st.first > 0) HudSprite(c, ui, sp, "icons/icon_arrow_up", mx + mw - 64, my + 112, 40, 40);
        if (st.first + 6 < n) HudSprite(c, ui, sp, "icons/icon_arrow_down", mx + mw - 64, my + 112 + 5 * 108 + 52, 40, 40);
    }
    else
    {
        // The region map: the places of the region the cursor is in (Kanto with the Indigo
        // Plateau), routes as roads, towns as discs with their names, other places as dots.
        const int region = RegionOf(st.curX, st.curY) == 2 ? 2 : 0;
        std::vector<const Place*> shown;
        int x1 = 99, y1 = 99, x2 = 0, y2 = 0;
        for (const Place& p : st.places)
            if ((RegionOf(p.x, p.y) == 2) == (region == 2))
            {
                shown.push_back(&p);
                x1 = std::min(x1, p.x);
                y1 = std::min(y1, p.y);
                x2 = std::max(x2, p.x + p.w);
                y2 = std::max(y2, p.y + p.h);
            }
        const std::string sel = st.selMap >= 0 && !st.inTabs ? Party_PlaceName(st.selMap) : "";
        HudOnline::TitledCard(c, ui, sp, f, mx + 24, my + 24, mw - 48, 60, region == 2 ? "JOHTO" : "KANTO", sel);
        const float ax = mx + 40, ay = my + 110, aw = mw - 80, ah = mh - 190;
        FillBlend(c, ui(ax), ui(ay), ui(aw), ui(ah), 0xFFE6F5DA);
        if (shown.empty()) x1 = y1 = 0, x2 = y2 = 1;
        const float cell = std::min((aw - 80) / (x2 - x1), (ah - 80) / (y2 - y1));
        const float ox = ax + (aw - cell * (x2 - x1)) / 2 - x1 * cell, oy = ay + (ah - cell * (y2 - y1)) / 2 - y1 * cell;
        auto centre = [&](float x, float y, float& cx, float& cy) {
            cx = ox + (x + 0.5f) * cell;
            cy = oy + (y + 0.5f) * cell;
        };
        for (int pass = 0; pass < 3; pass++)
            for (const Place* p : shown)
            {
                const std::string name = Party_PlaceName(p->map);
                const bool town = IsTown(name);
                float cx, cy;
                centre(p->x + (p->w - 1) / 2.0f, p->y + (p->h - 1) / 2.0f, cx, cy);
                if (pass == 0 && !town)
                {
                    const float t = cell * 0.36f;
                    FillBlend(c, ui(ox + p->x * cell + cell / 2 - t / 2), ui(oy + p->y * cell + cell / 2 - t / 2),
                        ui((p->w - 1) * cell + t), ui((p->h - 1) * cell + t), 0xFFF2DCA0);
                }
                else if (pass == 1 && town)
                {
                    const float d = std::max(28.0f, cell * 0.9f);
                    HudSprite(c, ui, sp, "people/chip_ring_grey", cx - d / 2, cy - d / 2, d, d);
                }
                else if (pass == 2 && town)
                    HudText(c, ui, f, cx, cy + std::max(28.0f, cell * 0.9f) / 2 + 2, 32, ShortPlace(name), Ink, 1);
            }
        // The game's cursor, then the players: the others in their colours, this one last.
        float cx, cy;
        if (!st.inTabs)
        {
            centre(st.curX, st.curY, cx, cy);
            const float d = std::max(48.0f, cell * 1.4f);
            FillBlend(c, ui(cx - cell / 2), ui(cy - cell / 2), ui(cell), ui(cell), 0x90FFD23C);
            HudSprite(c, ui, sp, "people/chip_ring_orange", cx - d / 2, cy - d / 2, d, d, 0xC0FFFFFF);
            HudSprite(c, ui, sp, "cursor/cursor_small", cx - d / 2 - 30, cy - 16, 30, 32);
        }
        auto player = [&](const PartyPeer& p, int x, int y, const std::string& label) {
            if ((RegionOf(x, y) == 2) != (region == 2)) return;
            float px, py;
            centre(x, y, px, py);
            HudOnline::Chip(c, ui, sp, f, px - 32, py - 32, 64, p);
            PartyPeer pill = p;
            pill.name = label;
            HudOnline::NamePill(c, ui, sp, f, px, py - 34, pill, -1);
        };
        for (const PartyPeer* p : peers)
        {
            int x, y;
            if (PeerCell(st, *p, kMapHeaders, env.rd, x, y)) player(*p, x, y, p->name);
        }
        player(env.me, st.meX, st.meY, "You");
        if (!peers.empty()) HudText(c, ui, f, mx + mw - 40, my + mh - 64, 32, "Friends shown live", Muted, 2);
    }

    // The side card: the contact under the phone's cursor, or the place under the map's
    // with the game's words about it; then where everyone is (the session's players online).
    std::string title, sub;
    std::vector<std::string> about;
    if (st.app == APP_PHONE)
    {
        const int id = st.contacts[st.first + st.row];
        title = Name(tx, id);
        sub = book.Class(tx, id);
    }
    else if (st.selMap >= 0 && !st.inTabs)
    {
        title = Party_PlaceName(st.selMap);
        about = HudWrap(ui, f, 32, tx.Get(273, st.selText), sw - 80);
        if (about.size() > 4) about.resize(4);
    }
    const float top = title.empty() ? 0 : 160 + (sub.empty() ? 0 : 40) + 44.0f * about.size() + (about.empty() ? 0 : 16);
    HudPanel(c, ui, sp, "panels/card", sx, my, sw, top + 120 + 104.0f * (1 + peers.size()));
    float y = my + 24;
    if (!title.empty())
    {
        HudOnline::TitledCard(c, ui, sp, f, sx + 24, y, sw - 48, 60, st.app == APP_PHONE ? "CONTACT" : "PLACE");
        HudText(c, ui, f, sx + 40, y + 84, 48, title, Ink, 0, true);
        float ty = y + 148;
        if (!sub.empty()) HudText(c, ui, f, sx + 40, ty, 32, sub, Muted), ty += 40;
        for (const std::string& line : about) HudText(c, ui, f, sx + 40, ty, 32, line, Ink), ty += 44;
        y += top;
    }
    HudOnline::TitledCard(c, ui, sp, f, sx + 24, y, sw - 48, 60, peers.empty() ? "YOU" : "SESSION");
    y += 84;
    auto row = [&](const PartyPeer& p, const std::string& place) {
        HudPanel(c, ui, sp, "panels/row", sx + 24, y, sw - 48, 88);
        HudOnline::Chip(c, ui, sp, f, sx + 36, y + 8, 72, p);
        HudText(c, ui, f, sx + 124, y + 8, 32, p.name, HudOnline::TextColourOf(p.colour));
        HudText(c, ui, f, sx + 124, y + 46, 32, p.state == PS_BATTLE ? "In a battle" : place, Muted);
        y += 104;
    };
    row(env.me, Party_PlaceName(env.me.map));
    for (const PartyPeer* p : peers) row(*p, Party_PlaceName(p->map));
    DrawHintBar(c, ui, sp, f, hints);
}
}
