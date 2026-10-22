#pragma once
// The Pokégear redrawn in the HUD style (online-HUD design 24-ss-pokegear): the tabs as
// discs along the top, the clock in a pill, and the phone's contact list or the region map
// with the player (and, online, the other players of the session) on it. The game keeps
// taking the input; everything shown is read from its memory each frame
// (src/application/pokegear in the decompilation), and any step that cannot be mirrored
// (calls, the phone's menus, the map zoomed or being marked, the radio, the settings)
// leaves the game's own picture on screen.
#include "hudbag.h"
#include "hudparty.h"
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

// A menu of the game's buttons on the touch screen (TouchscreenListMenu): its items' text
// and the one under the cursor (or chosen). No items: none shown.
struct Menu
{
    std::vector<std::string> items;
    int cursor = 0;
};

// The radio: the tuning cursor, the channel button chosen with A, the stations on offer,
// the station tuned (255 none) and how clearly, its show and the lines on screen.
struct Radio
{
    int x = 128, y = 92, button = 0, stations = 0, signal = 0, station = 255;
    std::string title, host;
    std::vector<std::string> lines;
};

// A phone call (PokegearPhoneCallContext): who, how far it is, and the lines its text box
// shows; `waiting` while the game waits for a button before going on.
struct Call
{
    bool on = false, incoming = false, waiting = false;
    int caller = 0;
    enum { Ringing, Talking, Ended } phase = Ringing;
    std::vector<std::string> lines;
};

struct State
{
    int mode = 0;         // 0 the Pokégear, 1 the map to choose where to fly, 2 the Town Map
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
    // Flying: the towns one can fly to now (Place::map names them), and the one under the
    // cursor (its map, -1 none).
    std::vector<Place> fly;
    int flyHere = -1;
    // What the game's map picture shows (ov101_021EAF40): how much of Kanto is open (0-2),
    // its +0x13D bits (1 Cianwood flown to, 2 the Safari Zone, 4 at the Sinjoh Ruins, 8 on
    // the S.S. Aqua), the fly points' flags (bit by FlyPoint::flag) and the map's palette
    // (a/1/4/4 member: the Pokégear's skin's, or flying's own).
    int unlock = 2, extra = 0, mapPal = 63;
    uint64_t flown = ~0ull;
    Menu menu;
    Call call;
    Radio radio;
    int skins = 0, skin = 0, skinCursor = 0; // the settings: the skins had (bits), in use, under the cursor
    int moving = -1; // the phone's contact being moved (its place in the list), -1 none
};

// A TouchscreenListMenu at `m` (src/touchscreen_list_menu.c): its items (ListMenuItem, a
// String and a value each, +0x10), how many (+0x18), the cursor (+0x24) and, once one is
// chosen and flashing (+0x25), that one (+0x2A). Destroying it clears it, so a menu gone
// does not check out.
template <class Read>
bool ReadMenu(Read rd, uint32_t m, Menu& out)
{
    using HudBag::InRam;
    out = Menu();
    if (!InRam(m) || !InRam(rd(m))) return false;
    const uint32_t items = rd(m + 0x10);
    const int n = rd(m + 0x18) & 0xFF, cur = rd(m + 0x24) & 0xFF, chosen = rd(m + 0x2A) & 0xFF;
    if (!InRam(items) || n < 1 || n > 7 || cur >= n || chosen >= n) return false;
    for (int i = 0; i < n; i++)
    {
        std::string s;
        if (!HudScreens::GameString([&](uint32_t a) { return rd(a) & 0xFFFF; }, rd(items + 8 * i), s)) return false;
        out.items.push_back(HudScreens::TitleCase(s));
    }
    out.cursor = (rd(m + 0x25) & 0xFF) == 1 ? chosen : cur;
    return true;
}

// The lines a text box shows once the first `n` codes of a message are printed
// (src/render_text.c): a new line at 0xE000; at 0x25BC the printer waits for a button and
// starts a fresh page, at 0x25BD it waits and scrolls one line up. While it waits at one,
// the page before it stays (`waiting`). The last `keep` lines are shown, as the box holds.
inline std::vector<std::string> PrintedLines(const std::vector<uint16_t>& codes, size_t n, size_t keep, bool& waiting)
{
    std::vector<std::vector<uint16_t>> lines(1);
    int pending = 0;
    for (size_t i = 0; i < n && i < codes.size() && codes[i] != 0xFFFF; i++)
    {
        const uint16_t c = codes[i];
        if (c == 0xFFFE) // a text command: the command, how many arguments, those
        {
            i += 2 + (i + 2 < codes.size() ? codes[i + 2] : 0);
            continue;
        }
        if (pending == 1) lines.assign(1, {});
        if (pending == 2) lines.emplace_back();
        pending = 0;
        if (c == 0xE000) lines.emplace_back();
        else if (c == 0x25BC) pending = 1;
        else if (c == 0x25BD) pending = 2;
        else lines.back().push_back(c);
    }
    waiting = pending != 0;
    std::vector<std::string> out;
    for (size_t i = lines.size() > keep ? lines.size() - keep : 0; i < lines.size(); i++) out.push_back(NdsText(lines[i].data(), lines[i].size()));
    return out;
}

// The call going on in the phone (PokegearPhoneAppData `own`, at its step `step`): while it
// is being set up (3, 5) the contact being called (+0xC8); then its context (+0xC4): the
// caller (+0xA0), the call's step (+0x88: 0 ringing, 1 talking, 2 waiting to hang up, 3 and
// 255-256 hanging up, 257-258 out of range) and the message being printed (+0x54) by a
// text printer (src/text.c) into its window (+0x0C). The printer is not kept anywhere the
// frontend can find, so it is looked for in memory (a TextPrinter: the next code to print,
// then the window, and +0x27 active) each time the message changes; the one furthest
// behind is the new one, older ones freed keep their last place. None: printed at once.
// The ringing and hanging up are short messages of bank 271 (30-32 and 33-35), printed one
// after another as +0x90 counts.
template <class Read>
bool ReadCall(Read rd, uint32_t own, int step, HudBag::Texts& tx, Call& call)
{
    using HudBag::InRam;
    call = Call();
    call.on = true;
    call.incoming = (rd(own + 0xCB) & 0xFF) != 0;
    call.caller = rd(own + 0xC8) & 0xFF;
    if (step != 6) return call.caller < 75;
    const uint32_t ctx = rd(own + 0xC4);
    if (!InRam(ctx) || rd(ctx + 4) != own) return false;
    call.caller = rd(ctx + 0xA0) & 0xFF;
    const int main = (int)rd(ctx + 0x88), tone = (int)rd(ctx + 0x90);
    if (call.caller >= 75) return false;
    // Each of them holds the ones before it too.
    auto said = [&](int first, int count) { call.lines = { tx.Get(271, first + std::min(std::max(count, 1), 3) - 1) }; };
    if (main == 0) return said(30, tone), true;
    if (main == 3 || main == 256 || main == 255)
    {
        call.phase = Call::Ended;
        return said(main == 255 ? 30 : 33, tone), true;
    }
    if (main != 1 && main != 2 && main != 257 && main != 258) return false;
    call.phase = main <= 2 ? Call::Talking : Call::Ended;
    const uint32_t str = rd(ctx + 0x54), win = rd(ctx + 0x0C);
    if (!InRam(str) || rd(str + 4) != 0xB6F8D2ECu) return false;
    const uint32_t len = rd(str) >> 16;
    if (len > 1081) return false;
    std::vector<uint16_t> codes(len);
    uint32_t hash = len * 31 + (uint32_t)main * 7 + rd(ctx + 0x8C);
    for (uint32_t i = 0; i < len; i++) hash = hash * 33 + (codes[i] = (uint16_t)(rd(str + 8 + 2 * i) & 0xFFFF));
    static uint32_t lastHash = 0, printer = 0;
    const uint32_t from = str + 8, to = from + 2 * len;
    auto looks = [&](uint32_t a) {
        const uint32_t at = rd(a);
        return rd(a + 4) == win && at >= from && at <= to && !(at & 1) && (rd(a + 0x24) >> 24) == 1;
    };
    if (hash != lastHash)
    {
        lastHash = hash;
        printer = 0;
        for (uint32_t a = 0x02000000; a < 0x02400000; a += 4)
            if (looks(a) && (!printer || rd(a) < rd(printer))) printer = a;
    }
    const size_t n = printer && looks(printer) ? (rd(printer) - from) / 2 : len;
    call.lines = PrintedLines(codes, n, 2, call.waiting);
    call.waiting = call.waiting || main == 2 || main == 258;
    return true;
}

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
template <class Read>
bool ReadMap(Read rd, uint32_t own, State& st);

// The radio (PokegearRadioAppData `own`): the tuning cursor (+0x28, +0x2A: DS pixels, in a
// circle of 52 around 128, 92), the channel button last chosen with A (+0x24 bits 5-7),
// the stations on offer (+0x26 bits 0-3: Johto, Kanto, Kanto with the expansion, none, the
// Unown, Team Rocket, Mahogany), the signal (bits 4-5: 0 none, 1 with static, 2 clear),
// the station tuned (+0x27, 255 none) and its show (RadioShow +0x60): its title (+0x4C)
// and host (+0x50), the message (+0x6C) of which +0x62 lines are printed, two at a time in
// its window, which scrolls them all off between segments (+0x5E 2 and 3).
template <class Read>
bool ReadRadio(Read rd, uint32_t own, Radio& r)
{
    using HudBag::InRam;
    auto rd16 = [&](uint32_t a) { return rd(a) & 0xFFFF; };
    r.x = (int16_t)rd16(own + 0x28);
    r.y = (int16_t)rd16(own + 0x2A);
    r.button = rd(own + 0x24) >> 5 & 7;
    const uint32_t w = rd(own + 0x24);
    r.stations = w >> 16 & 15;
    r.signal = w >> 20 & 3;
    r.station = w >> 24 & 0xFF;
    r.title.clear();
    r.host.clear();
    r.lines.clear();
    if (r.stations > 6 || r.x < 60 || r.x > 196 || r.y < 30 || r.y > 154) return false;
    static uint32_t lastShow = 0, lastHash = 0;
    static int lastLine = 0, lastStation = -1;
    static std::vector<std::string> shown;
    const uint32_t show = rd(own + 0x60);
    if (r.station == 255 || !InRam(show))
    {
        lastShow = 0;
        shown.clear();
        return true;
    }
    if (!HudScreens::GameString(rd16, rd(show + 0x4C), r.title) || !HudScreens::GameString(rd16, rd(show + 0x50), r.host)) return false;
    const uint32_t str = rd(show + 0x6C);
    if (!InRam(str) || rd(str + 4) != 0xB6F8D2ECu) return false;
    const uint32_t len = rd(str) >> 16;
    if (len > 1024) return false;
    // The message's lines (split at 0xE000), as String_GetLineN gives them.
    std::vector<std::vector<uint16_t>> lines(1);
    uint32_t hash = len;
    for (uint32_t i = 0; i < len; i++)
    {
        const uint16_t c = (uint16_t)rd16(str + 8 + 2 * i);
        hash = hash * 33 + c;
        if (c == 0xFFFF) break;
        if (c == 0xFFFE) // a text command: the command, how many arguments, those
        {
            i += 2 + rd16(str + 8 + 2 * (i + 2));
            continue;
        }
        if (c == 0xE000) lines.emplace_back();
        else lines.back().push_back(c);
    }
    const int printed = std::min((int)(rd(show + 0x60) >> 16 & 0xFF), (int)lines.size()), run = (int)(rd(show + 0x5C) >> 16 & 0xFF);
    // Another station, or the window scrolled clear: nothing on it yet.
    const bool other = show != lastShow || r.station != lastStation;
    if (other || run >= 2) shown.clear();
    // The message left from the station before stays in the show's buffer until the new
    // one prints: its lines are not shown again.
    if (other && hash == lastHash) lastLine = printed;
    else if (other || hash != lastHash) lastLine = 0;
    lastStation = r.station;
    // The newest line as printed (+0x48): with static, the game blots letters out of it.
    for (; lastLine < printed && run < 2; lastLine++)
    {
        std::string line;
        if (lastLine != printed - 1 || !HudScreens::GameString(rd16, rd(show + 0x48), line))
            line = NdsText(lines[lastLine].data(), lines[lastLine].size());
        shown.push_back(line);
    }
    lastShow = show;
    lastHash = hash;
    if (shown.size() > 2) shown.erase(shown.begin(), shown.end() - 2);
    r.lines = shown;
    return true;
}

template <class Read, class App>
bool ReadState(Read rd, App app, HudBag::Texts& tx, uint32_t& cache, State& st, int& step, bool& between)
{
    between = false;
    using HudBag::InRam;
    auto rd8 = [&](uint32_t a) { return (int)(rd(a) & 0xFF); };
    auto rd16 = [&](uint32_t a) { return (int)(rd(a) & 0xFFFF); };
    const auto *gear = app("Pokegear"), *phone = app("GearPhone"), *map = app("GearMap"), *radio = app("GearRadio"), *conf = app("GearConfigure");
    if (!gear || !phone || !map || !radio || !conf) return false;
    const uint32_t man = HudBag::FindManager(rd, gear->initAddr, gear->execAddr, gear->exitAddr, cache);
    if (!man) return false;
    const uint32_t data = rd(man + 0x1C);
    if (rd(data + 0x20) != rd(man + 0x18)) return false;
    // The Pokégear's own step: launching or running a tab (POKEGEAR_APP_MAIN_STATE_*).
    const int gearStep = (int)rd(man + 0x14);
    if (gearStep < 2 || gearStep > 11) return false;
    st.app = rd8(data + 4);
    st.cards = rd8(data + 5) & 0x7F;
    st.skin = rd8(data + 8); // the skin chosen in the settings tab
    if (st.skin > 5) return false;
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
    const auto* tab = st.app == APP_PHONE ? phone : st.app == APP_MAP ? map : st.app == APP_RADIO ? radio : st.app == APP_CONFIGURE ? conf : nullptr;
    const uint32_t child = rd(data + 0x70);
    if (!tab || !InRam(child) || (rd(child) & ~1u) != tab->initAddr || (rd(child + 4) & ~1u) != tab->execAddr ||
        (rd(child + 8) & ~1u) != tab->exitAddr || rd(child + 0x18) != data)
    {
        between = true;
        return false;
    }
    const uint32_t own = rd(child + 0x1C);
    step = (int)rd(child + 0x14);
    // Starting (0), ending (2) or done (the phone's 14, the map's 13, the radio's and the
    // settings' 9), or not yet in its per-frame function: between tabs.
    if (rd(child + 0x10) != 2 || step == 0 || step == 2 || step == (st.app == APP_PHONE ? 14 : st.app == APP_MAP ? 13 : 9))
    {
        between = true;
        return false;
    }
    if (!InRam(own) || step < 0 || step > 13) return false;
    if (st.app == APP_PHONE)
    {
        // PokegearPhoneAppData: its list (PhoneContactListUI at 0xE0, a slot of 12 bytes
        // per contact from 0xC) in step with its contacts (each slot's list node holds the
        // contact at +1); a call (calling or called, steps 3, 5, 6) or the menu of a contact
        // or of sorting (7, 8: +0x504, gone a frame before the step moves on).
        if (rd(own + 0x0C) != data) return false;
        if ((step == 3 || step == 5 || step == 6) && !ReadCall(rd, own, step, tx, st.call)) return false;
        if (step == 6 || step == 7 || step == 8) ReadMenu(rd, rd(own + 0x504), st.menu);
        const uint32_t ui = own + 0xE0;
        const int n = rd8(ui);
        st.row = rd8(ui + 1);
        st.first = rd8(ui + 3);
        if (n < 1 || n > 75 || n != rd8(own + 0xCD) || st.first >= n || st.row > 5 || st.first + st.row >= n) return false;
        // Moving a contact (Sort, then by hand): the one being moved (selectedIndex +2).
        st.moving = rd8(ui + 6) >> 1 ? rd8(ui + 2) : -1;
        if (st.moving >= n) return false;
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
    if (st.app == APP_RADIO) return rd(own + 0x0C) == data && ReadRadio(rd, own, st.radio);
    if (st.app == APP_CONFIGURE)
    {
        // PokegearConfigureAppData: the skins had (+0x12, 9 bits) and the one in use (the 7
        // bits above), the one chosen for the menu (+0x11) and the menu asking whether to
        // use it (+0x40, step 3); the cursor on the six skins is the Pokégear's last cursor.
        if (rd(own + 0x0C) != data) return false;
        const int w = rd16(own + 0x12);
        st.skins = w & 0x3F;
        st.skin = w >> 9;
        st.skinCursor = rd8(own + 0x11);
        if (!st.inTabs)
        {
            const uint32_t cm = rd(data + 0x7C), cur = InRam(cm) ? rd(cm + 8) : 0;
            if (!InRam(cur)) return false;
            st.skinCursor = rd8(cur + 1);
        }
        if (step == 3) ReadMenu(rd, rd(own + 0x40), st.menu);
        return st.skin < 6 && st.skinCursor < 6;
    }
    // PokegearMapAppData: the Pokégear's map (not flying or the Town Map).
    st.mapPal = 20 + rd8(data + 8); // the Pokégear's skin
    return rd(own + 0x10) == data && rd8(own + 0x0D) == 0 && rd8(data + 8) < 6 && ReadMap(rd, own, st);
}

// The towns one can fly to (gMapFlypointParams): the map named, its fly point (flag 0x9B0
// on), its cells (without the map's 2 rows on top), and the piece of the map's screen that
// covers it until it is flown to (RomArt::MapPiece's sx, sy, w, h, and its corner's place
// from the town's: none when 0 wide). Indigo Plateau and Victory Road (to Route 26) can be
// flown to from either region; the others from their own.
struct FlyPoint
{
    int map, flag, x, y, w, h, sx, sy, cw, ch, cx, cy;
};
constexpr FlyPoint kFlyPoints[27] = { { 49, 0, 32, 11, 1, 1, 0, 20, 3, 3, 1, 1 }, { 50, 1, 31, 7, 2, 2, 5, 20, 4, 4, 1, 1 },
    { 51, 2, 32, 2, 2, 2, 10, 20, 4, 4, 1, 1 }, { 52, 3, 40, 3, 2, 2, 15, 20, 4, 4, 1, 1 }, { 53, 4, 44, 7, 1, 1, 20, 20, 3, 3, 1, 1 },
    { 54, 5, 40, 9, 2, 2, 25, 20, 4, 4, 1, 1 }, { 55, 6, 37, 7, 2, 2, 30, 20, 4, 4, 1, 1 }, { 56, 7, 37, 12, 2, 2, 35, 20, 4, 4, 1, 1 },
    { 57, 8, 32, 15, 1, 1, 0, 24, 3, 3, 1, 1 }, { 58, 9, 28, 6, 1, 1 }, { 59, 10, 40, 6, 2, 2, 5, 24, 4, 4, 1, 1 },
    { 60, 11, 21, 12, 1, 1, 11, 25, 1, 1, 0, 0 }, { 67, 12, 16, 12, 2, 1, 15, 24, 4, 3, 1, 1 }, { 73, 13, 14, 7, 2, 2, 20, 24, 3, 4, 0, 1 },
    { 74, 14, 12, 14, 2, 1, 25, 24, 4, 3, 1, 1 }, { 75, 15, 5, 10, 1, 2, 30, 24, 3, 4, 1, 1 }, { 76, 16, 9, 10, 3, 2, 35, 24, 5, 4, 1, 1 },
    { 77, 17, 8, 7, 2, 2, 0, 28, 4, 4, 1, 1 }, { 78, 18, 11, 4, 2, 2, 5, 28, 4, 4, 1, 1 }, { 87, 19, 16, 5, 1, 1, 10, 28, 3, 3, 1, 1 },
    { 89, 21, 20, 4, 2, 2, 15, 28, 4, 4, 1, 1 }, { 88, 20, 15, 1, 3, 2 }, { 90, 22, 25, 8, 1, 1 }, { 174, 30, 2, 8, 2, 2 },
    { 272, 27, 6, 6, 2, 2 }, { 96, 35, 10, 6, 2, 2 }, { 124, 33, 28, 7, 1, 2 } };

// A PokegearMapAppData (the Pokégear's map, or flying or the Town Map), not zoomed in and
// not marking places, with its places (16 bytes each).
template <class Read>
bool ReadMap(Read rd, uint32_t own, State& st)
{
    using HudBag::InRam;
    auto rd8 = [&](uint32_t a) { return (int)(rd(a) & 0xFF); };
    auto rd16 = [&](uint32_t a) { return (int)(rd(a) & 0xFFFF); };
    const int flags = rd8(own + 0x138), extra = rd8(own + 0x13D);
    if (rd8(own + 0x0C) != 0 || (flags & 1)) return false;

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
    // The fly points' flags are the save's (the Pokégear's +0x2C, after 0x170 variables).
    const uint32_t gear = rd(own + 0x10), vars = InRam(gear) ? rd(gear + 0x2C) : 0;
    if (!InRam(vars)) return false;
    st.unlock = unlock;
    st.extra = extra;
    st.flown = 0;
    for (const FlyPoint& p : kFlyPoints)
        if (rd(vars + 0x2E0 + (0x9B0 + p.flag) / 8) >> ((0x9B0 + p.flag) % 8) & 1) st.flown |= 1ull << p.flag;
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
        case 3:  // a call: setting up, closing the menu, the call
        case 5:
        case 6:
        case 7:  // the contact's menu, the sorting menu
        case 8:
        case 10: // fading in
        case 12: return true; // wiping in from another tab
        case 11: // fading out to close
        case 13: return before; // wiping out to another tab
        case 9: return true; // moving a contact
        default: return false;
        }
    if (st.app == APP_RADIO || st.app == APP_CONFIGURE)
        switch (step) // PokegearRadio_Main, PokegearConfigure_Main (3 its menu, 4 changing skins)
        {
        case 1:  // tuning, choosing
        case 3:
        case 4:
        case 5:  // fading in
        case 7: return true; // wiping in from another tab
        case 6:  // fading out to close
        case 8: return before; // wiping out to another tab
        default: return false;
        }
    if (st.mode)
        switch (step) // FlyMap_Main
        {
        case 1:  // the map
        case 4:  // fading in
        case 12: return true; // asking to fly there
        case 5: return before; // fading out
        default: return false;
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

// The pieces the game copies over its map picture (ov101_021EAF40): Kanto closed off as far
// as it is not open yet, the towns not flown to yet greyed (Kanto's only once it is all
// open; never Cianwood, whose island has its own pieces with the Safari Zone), and the
// Sinjoh Ruins and the S.S. Aqua while there.
inline std::vector<RomArt::MapPiece> MapPieces(const State& st)
{
    std::vector<RomArt::MapPiece> v;
    if (st.unlock == 0) v.push_back({ 22, 0, 6, 20, 48, 0 });
    if (st.unlock == 1) v.push_back({ 29, 0, 3, 20, 54, 0 });
    for (const FlyPoint& p : kFlyPoints)
        if (p.cw && p.map != 75 && !(st.flown >> p.flag & 1) && !(st.unlock < 2 && RegionOf(p.x, p.y + 2) == 0))
            v.push_back({ p.x - p.cx, p.y - p.cy + 2, p.cw, p.ch, p.sx, p.sy });
    const bool cianwood = st.extra & 1, safari = st.extra & 2;
    if (!cianwood || !safari) v.push_back({ 1, 9, 5, 6, safari ? 48 : 41, !safari && cianwood ? 20 : 27 });
    if (st.extra & 4) v.push_back({ 19, 1, 3, 4, 55, 20 });
    if (st.extra & 8) v.push_back({ 24, 15, 3, 3, 55, 24 });
    return v;
}

// The map to choose where to fly, or the Town Map (the TownMap application, FlyMap_*): its
// PokegearMapAppData with a Pokégear of its own (+0x10, whose childAppdata +0x64 is it),
// its kind (+0x0D: 1 flying, 2 the Town Map) and region (+0x0E), and while asking whether
// to fly there (step 12) its menu (+0xC4).
template <class Read, class App>
bool ReadFly(Read rd, App app, uint32_t& cache, State& st, int& step)
{
    using HudBag::InRam;
    const auto* town = app("TownMap");
    if (!town) return false;
    const uint32_t man = HudBag::FindManager(rd, town->initAddr, town->execAddr, town->exitAddr, cache);
    if (!man) return false;
    const uint32_t own = rd(man + 0x1C), gear = rd(own + 0x10);
    step = (int)rd(man + 0x14);
    st = State();
    st.app = APP_MAP;
    st.mode = rd(own + 0x0C) >> 8 & 0xFF;
    if (!InRam(gear) || rd(gear + 0x64) != own || st.mode < 1 || st.mode > 2 || step < 0 || step > 13 || !ReadMap(rd, own, st)) return false;
    const int region = rd(own + 0x0C) >> 16 & 0xFF;
    for (const FlyPoint& p : kFlyPoints)
    {
        if (!(st.flown >> p.flag & 1)) continue;
        if (p.map != 58 && p.map != 124 && region != 1 && RegionOf(p.x, p.y + 2) != region) continue;
        st.fly.push_back({ p.map, p.x, p.y + 2, p.w, p.h });
    }
    // The fly point under the cursor, as the game finds it when A is pressed (its +0x0F is
    // only updated as the cursor moves).
    for (const Place& p : st.fly)
        if (st.mode == 1 && st.curX >= p.x && st.curX < p.x + p.w && st.curY >= p.y && st.curY < p.y + p.h) st.flyHere = p.map;
    if (step == 12) ReadMenu(rd, rd(own + 0xC4), st.menu);
    return true;
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
    RomArt* art = nullptr; // for the game's map picture
    int wayX = -1, wayY = -1; // the waypoint's cell on the region map (-1: none)
};

constexpr uint32_t kMapHeaders = 0x020F6BE0; // sMapHeaders (as party.cpp), 24 bytes a map

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, HudBag::Texts& tx, const Book& book, const State& st,
    const Env& env, const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    // The skin chosen in the settings tab colours the backdrop, so choosing one still shows.
    static const uint32_t skinTint[6] = { 0xFFFFFFFF, 0xFFFFB8C8, 0xFFB8F0C0, 0xFFFFE0A0, 0xFFD8C0FF, 0xFFA8E8F0 };
    DrawBackdrop(c, sp, "backgrounds/bg_dusk", skinTint[st.mode ? 0 : st.skin]);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;

    // The tabs in the game's order (its d-pad walks them so), the one running large and
    // yellow, the cursor beside the one under it while it is on them.
    static const char* labels[5] = { "SETTINGS", "RADIO", "MAP", "PHONE", "CLOSE" };
    static const char* icons[5] = { "icons/icon_cog", "icons/icon_radio", "icons/icon_map", "icons/icon_phone", "icons/icon_cross" };
    if (st.mode) HudText(c, ui, f, x0 + 60, 66, 48, st.mode == 1 ? "FLY" : "TOWN MAP", White, 0, true);
    for (int i = 0; i < 5 && !st.mode; i++)
    {
        const bool cur = i == st.app, has = !((i == APP_MAP && !(st.cards & 1)) || (i == APP_RADIO && !(st.cards & 2)));
        const float cx = x0 + 130 + i * 170 + (i == 4 ? 40 : 0), d = cur ? 112 : 88, dy = 84 - d / 2;
        HudSprite(c, ui, sp, !has ? "people/chip_empty" : cur ? "people/chip_ring_orange" : "people/chip_ring_grey", cx - d / 2, dy, d, d,
            has ? 0xFFFFFFFF : 0x80FFFFFF);
        if (has) HudSprite(c, ui, sp, icons[i], cx - 24, RingCentreY(dy, d) - 24, 48, 48);
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
    if (!st.mode)
    {
        HudPanel(c, ui, sp, "panels/button_white", px, 52, pw, 72);
        HudSprite(c, ui, sp, "icons/icon_clock", px + 28, 64, 48, 48);
        HudText(c, ui, f, px + 100, 64, 48, time, Ink, 0, true);
        HudText(c, ui, f, px + pw - 32, 76, 32, when, Muted, 2); // on the time's baseline
    }

    // The other players of the session, for both tabs.
    std::vector<const PartyPeer*> peers;
    if (Party_Link() == PartyLink::Online)
        for (int i = 0; i < 4; i++)
            if (Party_Peers()[i].present && i != Party_Me()) peers.push_back(&Party_Peers()[i]);

    const float mx = x0 + 60, my = 196, mw = 1180, mh = 790;
    const float sx = x0 + 1290, sw = 570;
    HudPanel(c, ui, sp, "panels/card", mx, my, mw, mh);
    if (st.app == APP_PHONE && st.call.on)
    {
        // A call: who, how it is going, and the lines of the game's text box, with the arrow
        // while it waits for a button.
        const Call& k = st.call;
        const char* status = k.phase == Call::Ringing ? (k.incoming ? "Incoming call" : "Calling\xE2\x80\xA6")
            : k.phase == Call::Talking                ? "On the phone"
                                                      : "Call ended";
        HudOnline::TitledCard(c, ui, sp, f, mx + 24, my + 24, mw - 48, 60, "CALL", status);
        const std::string name = Name(tx, k.caller);
        const float d = 200, cx = mx + mw / 2, cy = my + 130;
        HudSprite(c, ui, sp, k.phase == Call::Talking ? "people/chip_ring_orange" : "people/chip_ring_grey", cx - d / 2, cy, d, d);
        HudText(c, ui, f, cx, cy + d / 2 - 24, 48, HudOnline::Initials(name), Ink, 1, true);
        HudText(c, ui, f, cx, cy + d + 24, 48, name, Ink, 1, true);
        const std::string cls = book.Class(tx, k.caller), place = book.Place(k.caller);
        HudText(c, ui, f, cx, cy + d + 84, 32, cls.empty() ? place : place.empty() ? cls : cls + " \xC2\xB7 " + place, Muted, 1);
        const float bx = mx + 60, by = my + mh - 260, bw = mw - 120;
        HudPanel(c, ui, sp, "panels/dialogue_box", bx, by, bw, 200);
        for (size_t i = 0; i < k.lines.size(); i++) HudText(c, ui, f, bx + 40, by + 34 + 72.0f * i, 48, k.lines[i], Ink);
        if (k.waiting) HudSprite(c, ui, sp, "icons/icon_arrow_down", bx + bw - 76, by + 130, 40, 40);
    }
    else if (st.app == APP_PHONE)
    {
        // The list: the page of six the game shows, the cursor on its row.
        const int n = (int)st.contacts.size();
        HudOnline::TitledCard(c, ui, sp, f, mx + 24, my + 24, mw - 48, 60, "PHONE", std::to_string(n) + (n == 1 ? " contact" : " contacts"));
        for (int k = 0; k < 6 && st.first + k < n; k++)
        {
            const int id = st.contacts[st.first + k];
            const bool here = !st.inTabs && k == st.row;
            const float ry = my + 112 + k * 108, rx = mx + 40, rw = mw - 120;
            // A contact being moved: lifted out in teal, with arrows to say it moves.
            const bool lifted = st.first + k == st.moving;
            HudPanel(c, ui, sp, lifted ? "panels/row_teal" : here ? "panels/row_selected" : "panels/row", rx, ry, rw, 92);
            if (lifted)
            {
                HudSprite(c, ui, sp, "icons/icon_arrow_up", rx + rw - 120, ry + 26, 40, 40);
                HudSprite(c, ui, sp, "icons/icon_arrow_down", rx + rw - 70, ry + 26, 40, 40);
            }
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
    else if (st.app == APP_CONFIGURE)
    {
        // The settings: the Pokégear's six skins as the game lays them out (two rows of
        // three), the one in use marked, those not had yet empty.
        HudOnline::TitledCard(c, ui, sp, f, mx + 24, my + 24, mw - 48, 60, "SETTINGS", "Pok\xC3\xA9gear skin");
        for (int i = 0; i < 6; i++)
        {
            const bool has = st.skins >> i & 1, cur = !st.inTabs && i == st.skinCursor, used = i == st.skin;
            const float bw = 300, bh = 260, x = mx + 100 + (i % 3) * (bw + 40), y = my + 140 + (i / 3) * (bh + 50);
            HudPanel(c, ui, sp, cur ? "panels/button_yellow" : has ? "panels/button_white" : "panels/row_disabled", x, y, bw, bh,
                has || cur ? 0xFFFFFFFF : 0x80FFFFFF);
            if (cur) HudSprite(c, ui, sp, "cursor/cursor_small", x + 14, y + 20, 30, 32);
            const float d = 120;
            HudSprite(c, ui, sp, !has ? "people/chip_empty" : used ? "people/chip_ring_orange" : "people/chip_ring_grey", x + bw / 2 - d / 2, y + 40, d, d);
            if (has) HudText(c, ui, f, x + bw / 2, y + 40 + d / 2 - 24, 48, std::to_string(i + 1), Ink, 1, true);
            HudText(c, ui, f, x + bw / 2, y + bh - 70, 32, !has ? "Not yet" : used ? "In use" : "Skin " + std::to_string(i + 1), has ? Ink : Muted, 1);
            // The colour the skin gives the Pokégear's backdrop.
            static const uint32_t swatch[6] = { 0xFF3C4C8C, 0xFF8C4C70, 0xFF3C7C64, 0xFF8C7040, 0xFF6C4C9C, 0xFF347C8C };
            if (has) FillBlend(c, ui(x + 40), ui(y + bh - 26), ui(bw - 80), ui(10), swatch[i]);
        }
    }
    else if (st.app == APP_RADIO)
    {
        // The radio: the tuning dial with the game's channels as numbered rings and its
        // cursor, the show on air beside it and its words below; A steps through the channels.
        const Radio& r = st.radio;
        const char* signal = r.station == 255 ? "No signal" : r.signal == 1 ? "Faint signal" : "Clear signal";
        HudOnline::TitledCard(c, ui, sp, f, mx + 24, my + 24, mw - 48, 60, "RADIO", signal);
        // The dial: the game's circle of 52 around (128, 92), four design pixels to one.
        const float k = 4, rad = 52 * k, dcx = mx + 70 + rad, dcy = my + 140 + rad;
        HudSprite(c, ui, sp, "people/chip_ring_grey", dcx - rad - 30, dcy - rad - 30, 2 * rad + 60, 2 * rad + 60);
        // The channels' centres (sTuningHitboxes_*) for the stations on offer.
        static const int chan[5][2] = { { 112, 76 }, { 152, 76 }, { 96, 108 }, { 136, 116 }, { 128, 48 } };
        const int nChan = r.stations == 0 ? 4 : r.stations == 1 ? 3 : r.stations == 2 ? 5 : 0;
        for (int i = 0; i < nChan; i++)
        {
            const float px = dcx + (chan[i][0] - 128) * k, py = dcy + (chan[i][1] - 92) * k;
            HudSprite(c, ui, sp, i == r.button ? "people/chip_ring_orange" : "people/chip_ring_grey", px - 36, py - 36, 72, 72);
            HudText(c, ui, f, px, py - 16, 32, std::to_string(i + 1), Ink, 1);
        }
        const float cx = dcx + (r.x - 128) * k, cy = dcy + (r.y - 92) * k;
        HudSprite(c, ui, sp, "people/chip_ring_orange", cx - 30, cy - 30, 60, 60);
        HudSprite(c, ui, sp, "icons/icon_radio", cx - 20, cy - 20, 40, 40);
        // The show, beside the dial.
        const float tx0 = dcx + rad + 60, tw = mx + mw - 40 - tx0;
        if (r.station != 255)
        {
            const std::vector<std::string> t = HudWrap(ui, f, 48, r.title, tw);
            for (size_t i = 0; i < t.size() && i < 2; i++) HudText(c, ui, f, tx0, my + 160 + 60.0f * i, 48, t[i], Ink, 0, true);
            // The host just under the title, however many lines it took.
            HudText(c, ui, f, tx0, my + 160 + 60.0f * std::min<size_t>(std::max<size_t>(t.size(), 1), 2) + 10, 32, r.host, Muted);
        }
        else
            HudText(c, ui, f, tx0, my + 160, 32, "Move the dial to a station", Muted);
        const float bx = mx + 40, by = my + mh - 200, bw = mw - 80;
        if (r.station != 255) HudPanel(c, ui, sp, "panels/dialogue_box", bx, by, bw, 160);
        for (size_t i = 0; i < r.lines.size(); i++) HudText(c, ui, f, bx + 40, by + 20 + 66.0f * i, 48, r.lines[i], Ink);
    }
    else
    {
        // The region map: the game's own picture of the region the cursor is in (Kanto with
        // the Indigo Plateau), from the ROM, with the place under the cursor lit; without it,
        // the places drawn: routes as roads, towns as discs with their names.
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
        // Johto's picture runs to Mount Silver, Kanto's from Route 27. It fills the card below
        // the title to the card's inner edges (2 in, its corners 22 round).
        const Image* art = env.art ? env.art->RegionMap(st.mapPal, MapPieces(st)) : nullptr;
        const float ax = art ? mx + 2 : mx + 40, ay = art ? my + 100 : my + 110, aw = art ? mw - 4 : mw - 80, ah = art ? mh - 102 : mh - 190;
        if (!art) FillBlend(c, ui(ax), ui(ay), ui(aw), ui(ah), 0xFFE6F5DA);
        if (shown.empty()) x1 = y1 = 0, x2 = y2 = 1;
        float cell = std::min((aw - 80) / (x2 - x1), (ah - 80) / (y2 - y1));
        float ox = ax + (aw - cell * (x2 - x1)) / 2 - x1 * cell, oy = ay + (ah - cell * (y2 - y1)) / 2 - y1 * cell;
        // Zoomed to fill the panel, following the cursor.
        static float pan[2] = { -1, -1 };
        if (art)
            HudRegionMap(c, ui, *art, region == 2 ? 0 : 22, region == 2 ? 26 : 47, ax, ay, aw, ah, (float)st.curX, (float)st.curY, 3, 20, pan,
                cell, ox, oy);
        // What is drawn on the map stays inside the panel.
        auto box = [&](float x, float y, float w, float h, uint32_t col) {
            const float l = std::max(x, ax), t = std::max(y, ay), r = std::min(x + w, ax + aw), b = std::min(y + h, ay + ah);
            if (r > l && b > t) FillBlend(c, ui(l), ui(t), ui(r - l), ui(b - t), col);
        };
        auto inView = [&](float x, float y) { return !art || (x >= ax && x <= ax + aw && y >= ay && y <= ay + ah); };
        auto centre = [&](float x, float y, float& cx, float& cy) {
            cx = ox + (x + 0.5f) * cell;
            cy = oy + (y + 0.5f) * cell;
        };
        // A frame around cells, so the game's picture inside stays seen.
        auto frame = [&](float x, float y, float w, float h, float t, uint32_t col) {
            box(ox + x * cell - t, oy + y * cell - t, w * cell + 2 * t, t, col);
            box(ox + x * cell - t, oy + (y + h) * cell, w * cell + 2 * t, t, col);
            box(ox + x * cell - t, oy + y * cell, t, h * cell, col);
            box(ox + (x + w) * cell, oy + y * cell, t, h * cell, col);
        };
        // Flying, the towns one can fly to are ringed in orange (more under the cursor).
        auto canFly = [&](int map) {
            for (const Place& p : st.fly)
                if (p.map == map) return true;
            return false;
        };
        for (int pass = 0; pass < 3; pass++)
            for (const Place* p : shown)
            {
                const std::string name = Party_PlaceName(p->map);
                const bool fly = st.mode == 1 && canFly(p->map), town = IsTown(name) || fly;
                float cx, cy;
                centre(p->x + (p->w - 1) / 2.0f, p->y + (p->h - 1) / 2.0f, cx, cy);
                if (pass == 0 && art && p->map == st.selMap && !st.inTabs)
                    box(ox + p->x * cell, oy + p->y * cell, p->w * cell, p->h * cell, 0x70FFF5A0);
                else if (pass == 0 && !town && !art)
                {
                    const float t = cell * 0.36f;
                    FillBlend(c, ui(ox + p->x * cell + cell / 2 - t / 2), ui(oy + p->y * cell + cell / 2 - t / 2),
                        ui((p->w - 1) * cell + t), ui((p->h - 1) * cell + t), 0xFFF2DCA0);
                }
                else if (pass == 1 && art && fly)
                    frame(p->x, p->y, p->w, p->h, p->map == st.flyHere ? 8 : 5, 0xFFF08030);
                else if (pass == 1 && town && !art)
                {
                    const float d = std::max(28.0f, cell * (p->map == st.flyHere ? 1.3f : 0.9f));
                    HudSprite(c, ui, sp, fly ? "people/chip_ring_orange" : "people/chip_ring_grey", cx - d / 2, cy - d / 2, d, d);
                }
                else if (pass == 2 && town && !art)
                    HudText(c, ui, f, cx, cy + std::max(28.0f, cell * 0.9f) / 2 + 2, 32, ShortPlace(name), Ink, 1);
            }
        // The game's cursor, then the players: the others in their colours, this one last.
        float cx, cy;
        if (!st.inTabs)
        {
            centre(st.curX, st.curY, cx, cy);
            const float d = art ? cell + 12 : std::max(48.0f, cell * 1.4f);
            if (art)
                frame(st.curX, st.curY, 1, 1, 6, 0xFFFFD23C);
            else
            {
                FillBlend(c, ui(cx - cell / 2), ui(cy - cell / 2), ui(cell), ui(cell), 0x90FFD23C);
                HudSprite(c, ui, sp, "people/chip_ring_orange", cx - d / 2, cy - d / 2, d, d, 0xC0FFFFFF);
            }
            HudSprite(c, ui, sp, "cursor/cursor_small", cx - d / 2 - 30, cy - 16, 30, 32);
        }
        auto player = [&](const PartyPeer& p, int x, int y, const std::string& label) {
            if ((RegionOf(x, y) == 2) != (region == 2)) return;
            float px, py;
            centre(x, y, px, py);
            if (!inView(px, py)) return;
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
        if (env.wayX >= 0 && (RegionOf(env.wayX, env.wayY) == 2) == (region == 2))
        {
            // The waypoint: a pin on its cell.
            float px, py;
            centre(env.wayX, env.wayY, px, py);
            if (inView(px, py))
            {
                const float s = 22;
                HudParty::FillConvex(c, ui, { { px - s * 0.42f, py - s * 0.62f }, { px + s * 0.42f, py - s * 0.62f }, { px, py + cell / 2 } }, 0xFF8E1F26);
                HudParty::FillRound(c, ui, px - s * 0.5f, py + cell / 2 - s * 1.26f, s, s, s / 2, 0xFF8E1F26);
                HudParty::FillRound(c, ui, px - s * 0.42f, py + cell / 2 - s * 1.18f, s * 0.84f, s * 0.84f, s * 0.42f, 0xFFF0505A);
                HudParty::FillRound(c, ui, px - s * 0.16f, py + cell / 2 - s * 0.92f, s * 0.32f, s * 0.32f, s * 0.16f, 0xFFFFFFFF);
            }
        }
        if (!peers.empty()) HudText(c, ui, f, mx + mw - 40, my + mh - 64, 32, "Friends shown live", Muted, 2);
    }

    // The side card: the contact under the phone's cursor, or the place under the map's
    // with the game's words about it; then where everyone is (the session's players online).
    std::string title, sub;
    std::vector<std::string> about;
    if (st.app == APP_PHONE)
    {
        const int id = st.call.on ? st.call.caller : st.contacts[st.first + st.row];
        title = Name(tx, id);
        sub = book.Class(tx, id);
    }
    else if (st.app == APP_CONFIGURE && !st.inTabs)
    {
        title = "Skin " + std::to_string(st.skinCursor + 1);
        sub = !(st.skins >> st.skinCursor & 1) ? "Not had yet" : st.skinCursor == st.skin ? "In use" : "Ready to use";
    }
    else if (st.app == APP_RADIO)
    {
        title = st.radio.title;
        sub = st.radio.host;
    }
    else if (st.selMap >= 0 && !st.inTabs)
    {
        title = Party_PlaceName(st.selMap);
        about = HudWrap(ui, f, 32, tx.Get(273, st.selText), sw - 80);
        if (about.size() > 4) about.resize(4);
    }
    // Flying: whether one can fly from here to the place under the cursor.
    if (st.mode == 1) sub = st.flyHere >= 0 ? "You can fly here" : title.empty() ? "Choose where to fly" : "You can't fly here";
    if (st.mode == 1 && title.empty()) title = "Fly";
    // The game's menu (the contact's, sorting, in a call, flying there): its buttons below.
    const float menuH = st.menu.items.empty() ? 0 : 76.0f * st.menu.items.size() + 8;
    const float top = title.empty() ? 0 : 160 + (sub.empty() ? 0 : 40) + 44.0f * about.size() + (about.empty() ? 0 : 16) + menuH;
    HudPanel(c, ui, sp, "panels/card", sx, my, sw, top + 120 + 104.0f * (1 + peers.size()));
    float y = my + 24;
    if (!title.empty())
    {
        HudOnline::TitledCard(c, ui, sp, f, sx + 24, y, sw - 48, 60, st.app == APP_PHONE ? "CONTACT" : st.app == APP_RADIO ? "ON AIR" : st.app == APP_CONFIGURE ? "SKIN" : "PLACE");
        HudText(c, ui, f, sx + 40, y + 84, 48, title, Ink, 0, true);
        float ty = y + 148;
        if (!sub.empty()) HudText(c, ui, f, sx + 40, ty, 32, sub, st.flyHere >= 0 ? Ink : Muted), ty += 40;
        for (const std::string& line : about) HudText(c, ui, f, sx + 40, ty, 32, line, Ink), ty += 44;
        ty += 8;
        for (size_t i = 0; i < st.menu.items.size(); i++, ty += 76)
        {
            const bool on = (int)i == st.menu.cursor;
            HudPanel(c, ui, sp, on ? "panels/button_yellow" : "panels/button_white", sx + 40, ty, sw - 80, 60);
            if (on) HudSprite(c, ui, sp, "cursor/cursor_small", sx + 52, ty + 14, 30, 32);
            HudText(c, ui, f, sx + sw / 2, ty + 14, 32, st.menu.items[i], Ink, 1);
        }
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
