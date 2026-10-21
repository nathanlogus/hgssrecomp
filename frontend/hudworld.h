#pragma once
// The overworld's message box redrawn in the HUD style: what people, signs and scripts say
// (the script environment's window, src/field/scrcmd_message.c), with the text appearing as
// the game prints it and the "more" arrow while it waits for a button. The game keeps
// printing into its own box and taking the input; this mirrors its text printer each frame
// and, while it does, main.cpp leaves the game's box (its BG3) out of the picture. Whenever
// anything does not check out the game's own box shows.
#include <string>
#include <vector>
#include "hudparty.h"
#include "merged.h"

namespace HudWorld
{
using HudParty::Ram;

constexpr uint32_t kFieldSystem = 0x021D4158;   // FieldSystem *
constexpr uint32_t kPrinterTasks = 0x021D1F74;  // text.c sTextPrinterTasks[8] (SysTask *)
constexpr uint32_t kRunTextPrinter = 0x020202ED; // text.c RunTextPrinter (Thumb), a printer task's function
constexpr uint32_t kScriptMagic = 222271;       // ScriptEnvironment.check (Unk80_10_C_MAGIC)

// The message box's frame on the top screen, in DS pixels (dialog_box.c: the window at tiles
// 2,19 of 27x4, its frame around it and the arrow's column at its right).
constexpr int kBoxX = 2, kBoxY = 144, kBoxW = 252, kBoxH = 48;

struct Dialogue
{
    std::vector<std::string> lines; // the page as the game shows it now (one or two lines)
    bool more = false;              // waiting for a button before the next page
    int sign = -1;                  // the kind of sign it is on (0 and 1 point the way), -1 none
    std::string Key() const
    {
        std::string k = (more ? "+" : "-") + std::to_string(sign);
        for (const std::string& l : lines) k += l + "\n";
        return k;
    }
};

// The page shown after the printer has taken `done` of the message's `codes` (the whole
// message once it has finished), with the printer in `state` (render_text.c: 2 waiting to
// clear the box, 3 waiting to scroll a line, 4 scrolling). False for anything this does not
// lay out as the game does.
inline bool Page(const std::vector<uint16_t>& codes, size_t done, int state, Dialogue& d)
{
    d.lines.assign(1, "");
    d.more = false;
    for (size_t i = 0; i < done && i < codes.size(); i++)
    {
        const uint16_t c = codes[i];
        // A wait for a button: the printer stops just after it, in state 2 or 3.
        auto wait = [&](bool scroll, size_t end) {
            if (end >= done && state == (scroll ? 3 : 2)) d.more = true;
            else if (scroll)
            {
                d.lines.erase(d.lines.begin());
                d.lines.push_back("");
            }
            else
                d.lines.assign(1, "");
        };
        if (c == 0xFFFF) break;
        if (c == 0xE000) d.lines.push_back("");
        else if (c == 0x25BC) wait(false, i + 1);
        else if (c == 0x25BD) wait(true, i + 1);
        else if (c == 0xF0FD) i++;
        else if (c == 0xFFFE)
        {
            // A control code: 0xFFFE, its kind, its number of fields, the fields.
            if (i + 2 >= codes.size()) return false;
            const uint16_t kind = codes[i + 1];
            const size_t end = i + 3 + codes[i + 2];
            if (kind == 0xFE06 && end > i + 3 && (codes[i + 3] == 0xFE00 || codes[i + 3] == 0xFE01))
                wait(codes[i + 3] == 0xFE00, end);
            // Colours, the font's glyph table, pauses, sounds and the icon pointing at the
            // touch screen's buttons (0x200, in the last three tiles) change nothing laid out here.
            else if (kind != 0xFF00 && kind != 0xFF01 && kind != 0x0200 && kind != 0x0201 && kind != 0x0202)
                return false;
            i = end - 1;
        }
        else
        {
            const std::string ch = NdsCharToUtf8(c);
            if (ch.empty()) return false;
            d.lines.back() += ch;
        }
        if (d.lines.size() > 2) return false;
    }
    if (d.lines.size() == 2 && d.lines[1].empty() && !d.more) d.lines.pop_back();
    return true;
}

// Whether the box's text window (4bpp, `width` tiles across, two 16-pixel lines) has any ink
// in line `line`, left of the last three tiles.
inline bool LineInk(const Ram& r, uint32_t pixels, int width, int line)
{
    for (int ty = line * 2; ty < line * 2 + 2; ty++)
        for (int tx = 0; tx < width - 3; tx++)
        {
            const uint32_t t = pixels + (uint32_t)(ty * width + tx) * 32;
            for (uint32_t k = 0; k < 32; k += 4)
                if (r.R32(t + k) != 0xFFFFFFFFu) return true;
        }
    return false;
}

// The overworld script's environment (the field's TaskManager +0x0C). While a task the
// script started runs (the Pokemon Center's healing) the TaskManager holds that task's
// instead, so the last one seen is kept for as long as it checks out.
inline uint32_t ScriptEnv(const Ram& r, uint32_t fs)
{
    static uint32_t last = 0;
    const uint32_t env = r.R32(r.R32(fs + 0x10) + 0x0C);
    if (r.Ptr(env) && r.R32(env) == kScriptMagic) last = env;
    else if (!r.Ptr(last) || r.R32(last) != kScriptMagic) last = 0;
    return last;
}

// The message box of the overworld's script, read from the game's memory: the script's
// own window, or else a sign's (FieldSystem +0x68, a Signpost: its window at +0, its kind
// +0x12, shown while bit 7 of +0x13 is set; kinds 0 and 1 have a picture of the way at the
// left, src/field/signpost.c). Signs print the same message into theirs.
inline bool ReadDialogue(const Ram& r, Dialogue& d)
{
    const uint32_t fs = r.R32(kFieldSystem);
    if (!r.Ptr(fs) || !(r.R32(fs + 0xD0) >> 16 & 0x40)) return false; // FieldSystem.textbox_open
    const uint32_t env = ScriptEnv(r, fs);
    if (!env) return false;
    uint32_t win = env + 0x14;
    int left = 2, width = 27;
    d.sign = -1;
    if (r.R8(env + 8) != 1)
    {
        const uint32_t sign = r.R32(fs + 0x68);
        if (!r.Ptr(sign) || !(r.R8(sign + 0x13) & 0x80)) return false;
        win = sign;
        d.sign = (int)r.R8(sign + 0x12);
        if (d.sign <= 1) left = 9, width = 20;
    }
    // The window where DialogBox_AddWindowToLayer3(WithXOffset) puts it.
    const uint32_t pixels = r.R32(win + 0x0C);
    if (r.R8(win + 4) != 3 || (int)r.R8(win + 5) != left || r.R8(win + 6) != 19 || (int)r.R8(win + 7) != width || r.R8(win + 8) != 4 ||
        !r.Ptr(pixels))
        return false;
    // The message (stringBuffer0, +0x48: a String, its codes from +8).
    const uint32_t s = r.R32(env + 0x48);
    if (!r.Ptr(s) || r.R32(s + 4) != 0xB6F8D2ECu) return false;
    const uint32_t n = r.R16(s + 2);
    if (n > r.R16(s) || n > 1024) return false;
    std::vector<uint16_t> codes(n);
    for (uint32_t i = 0; i < n; i++) codes[i] = (uint16_t)r.R16(s + 8 + 2 * i);
    // Its printer, while it prints into this window (the task of textPrinterNum, +5); after
    // that the whole message is in the box.
    size_t done = n;
    int state = 0;
    const uint32_t num = r.R8(env + 5);
    if (num < 8)
    {
        const uint32_t task = r.R32(kPrinterTasks + 4 * num), p = task ? r.R32(task + 0x10) : 0;
        if (task && r.R32(task + 0x14) == kRunTextPrinter && r.Ptr(p) && r.R32(p + 4) == win)
        {
            const uint32_t cur = r.R32(p);
            if (cur < s + 8 || cur > s + 8 + 2 * n || (cur & 1)) return false;
            done = (cur - s - 8) / 2;
            state = (int)r.R8(p + 0x28);
        }
    }
    if (!Page(codes, done, state, d)) return false;
    // The box's pixels agree: ink on the lines that have text and none on the others (not
    // while a line scrolls away).
    if (state != 4)
        for (int l = 0; l < 2; l++)
        {
            const bool text = l < (int)d.lines.size() && d.lines[l].find_first_not_of(' ') != std::string::npos;
            if (text != LineInk(r, pixels, width, l)) return false;
        }
    return true;
}

// The top row of the message box on the top screen's BG3, or -1 unless BG3 shows the box and
// nothing else (no menu or other window beside it), from the renderer's per-pixel layer flags.
// The box is still in memory once the game has stopped showing it (a sign slid away), and
// must not be drawn then. A sign slides in and out: its top is lower then.
inline int BoxTop(const uint8_t* flags)
{
    if (!flags) return -1;
    int inBox = 0, top = -1;
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 256; x++)
            if (LayerClass(flags[y * 256 + x]) == LAYER_BG3)
            {
                if (x < kBoxX || x >= kBoxX + kBoxW || y < kBoxY || y >= kBoxY + kBoxH) return -1;
                if (top < 0) top = y;
                inBox++;
            }
    return inBox > 16 * 8 ? top : -1;
}
// A fingerprint of what the top screen shows of the box (its BG3 pixels), never 0.
inline uint64_t BoxPicture(const ScreenImage& top)
{
    if (!top.pixels || !top.flags) return 0;
    uint64_t h = 1469598103934665603ull;
    for (int y = kBoxY; y < kBoxY + kBoxH; y++)
        for (int x = kBoxX; x < kBoxX + kBoxW; x++)
            if (LayerClass(top.flags[y * 256 + x]) == LAYER_BG3) h = (h ^ (top.pixels[y * 256 + x] & 0xFFFFFF) ^ (uint64_t)(y * 256 + x) << 32) * 1099511628211ull;
    return h | 1;
}

inline bool OnlyTheBox(const uint8_t* flags)
{
    const int top = BoxTop(flags);
    return top >= 0 && top <= kBoxY + 2;
}

// A String of the game (its codes from +8) as one line of text; false for anything but
// plain characters.
inline bool StringText(const Ram& r, uint32_t s, std::string& out)
{
    out.clear();
    if (!r.Ptr(s) || r.R32(s + 4) != 0xB6F8D2ECu) return false;
    const uint32_t n = r.R16(s + 2);
    if (n > r.R16(s) || n > 64) return false;
    for (uint32_t i = 0; i < n; i++)
    {
        const uint16_t c = (uint16_t)r.R16(s + 8 + 2 * i);
        if (c == 0xFFFF) break;
        const std::string ch = NdsCharToUtf8(c);
        if (ch.empty() || c == 0xE000 || c == 0x25BC || c == 0x25BD) return false;
        out += ch;
    }
    return !out.empty();
}

// A script's question with answers on the touch screen, which the d-pad works too: a yes/no
// (ScrCmd_GetMenuChoice, the script waiting in sub_020477C0) or a list of choices
// (ScrCmd_MenuInit/MenuItemAdd/MenuExec, waiting in sub_020478D0), as the shop clerk's BUY,
// SELL, SEE YA! Overlay 27 shows them, run by the field's bottom-screen task (FieldSystem
// +0xD8: its data, +0 the mode, 3 for these; +4 the task of overlay 27). Overlay 27's data:
// +0 its step (yes/no: 4 waiting for the answer, 5 taken; a list: 8 and 9), +4 where the
// answer goes, +0xC the list, +0x24 the FieldSystem, +0x394 the cursor. The list
// (overlay 1's, MoveTutorMenu_SetListItem): +0x9B how many choices, +0x1C4 + 8i their texts.
struct Choice
{
    std::vector<std::string> items;
    int cursor = -1;
};

inline bool ReadChoice(const Ram& r, Choice& ch)
{
    ch = Choice();
    const uint32_t fs = r.R32(kFieldSystem);
    if (!r.Ptr(fs)) return false;
    const uint32_t env = ScriptEnv(r, fs);
    if (!env) return false;
    // The context of the script (+0x38, three) that waits for the answer (its native function +4).
    uint32_t ctx = 0, waits = 0;
    for (uint32_t k = 0; k < 3 && !ctx; k++)
    {
        const uint32_t c = r.R32(env + 0x38 + 4 * k), w = r.R32(c + 4) & ~1u;
        if (r.Ptr(c) && (w == 0x020477C0 || w == 0x020478D0)) ctx = c, waits = w;
    }
    if (!ctx) return false;
    const uint32_t d = r.R32(r.R32(fs + 0xD8) + 0x10);
    if (!r.Ptr(d) || r.R8(d) != 3) return false;
    const uint32_t a = r.R32(r.R32(d + 4) + 0x10), step = r.R32(a), cur = r.R32(a + 0x394);
    if (!r.Ptr(a) || r.R32(a + 0x24) != fs) return false;
    if (waits == 0x020477C0)
    {
        // The answer goes to the context's data.
        const uint32_t out = r.R32(a + 4);
        if (out < ctx || out >= ctx + 0x100 || (step != 4 && step != 5) || cur > 1) return false;
        ch.items = { "Yes", "No" };
    }
    else
    {
        const uint32_t list = r.R32(a + 0x0C), n = r.R8(list + 0x9B);
        // shortcut: a list longer than eight (the move tutor's pages) is left to the game.
        if (!r.Ptr(list) || (step != 8 && step != 9) || n < 1 || n > 8 || cur >= n) return false;
        for (uint32_t i = 0; i < n; i++)
        {
            std::string t;
            if (!StringText(r, r.R32(list + 0x1C4 + 8 * i), t)) return false;
            ch.items.push_back(t);
        }
    }
    ch.cursor = (int)cur;
    return true;
}

// The box over the game's own (x, y, w, h in design pixels); `frame` moves the arrow.
// `choice`: the answers of a question to show above the box, or none.
inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Dialogue& d, float x, float y, float w, float h, uint32_t frame,
    const Choice* choice = nullptr)
{
    HudPanel(c, ui, sp, "panels/dialogue_box", x, y, w, h);
    const float pitch = 78, ty = y + (h - pitch * 2) / 2 + 2;
    float tx = x + 48;
    if (d.sign == 0 || d.sign == 1)
    {
        // A sign pointing the way: a map at its left, as the game's has the way's picture.
        HudSprite(c, ui, sp, "icons/icon_map", x + 44, y + h / 2 - 40, 80, 80, 0xFF4FA36B);
        tx += 110;
    }
    for (size_t i = 0; i < d.lines.size(); i++) HudText(c, ui, f, tx, ty + pitch * i, 64, d.lines[i], HudColour::Ink);
    if (d.more)
    {
        const int phase = frame / 8 % 4;
        const float bob = phase == 2 ? 8.0f : phase ? 4.0f : 0.0f;
        HudSprite(c, ui, sp, "icons/icon_arrow_down", x + w - 84, y + h - 76 + bob, 44, 44, 0xFF6F7BF7);
    }
    if (choice && choice->cursor >= 0)
    {
        // The answers in a column at the right above the box, as wide as the longest.
        const int n = (int)choice->items.size();
        const float bh = n > 4 ? 72 : 86, gap = n > 4 ? 12 : 18, px = n > 4 ? 48 : 56;
        float bw = 340;
        for (const std::string& t : choice->items) bw = std::max(bw, HudTextWidth(ui, f, px, t) + 140);
        const float bx = x + w - bw;
        for (int i = 0; i < n; i++)
        {
            const float by = y - 30 - (n - i) * (bh + gap) + gap;
            const bool on = i == choice->cursor;
            HudPanel(c, ui, sp, on ? "panels/row_selected" : "panels/row", bx, by, bw, bh);
            if (on) HudSprite(c, ui, sp, "cursor/cursor", bx + 24, by + bh / 2 - 20, 36, 40);
            HudText(c, ui, f, bx + bw / 2, by + (bh - px) / 2 - 4, px, choice->items[i], HudColour::Ink, 1);
        }
    }
}

// The game's own menu (X, src/start_menu.c) while it waits for a button: the buttons it draws
// on the touch screen, in the order the cursor counts them, and the cursor (FieldSystem
// +0xD3). StartMenuTaskData (the field task's data): +0x20 cursorActive, +0x26 state (3
// waiting), +0x2C how many actions, +0x3A the actions. An action is drawn when its button is
// unlocked (script flags in the save's general block, FieldSystem +0x0C); running shoes (6)
// and the second Pokégear entries (9, 10) never are.
struct XMenu
{
    std::vector<int> actions; // 0 Pokédex, 1 Pokémon, 2 Bag, 3 trainer card, 4 Save, 5 Options, 11 Pokégear
    int cursor = -1;
};

inline bool ReadXMenu(const Ram& r, XMenu& m)
{
    m = XMenu();
    const uint32_t fs = r.R32(kFieldSystem), sm = r.R32(r.R32(fs + 0x10) + 0x0C), save = r.R32(fs + 0x0C);
    if (!r.Ptr(fs) || !r.Ptr(sm) || !r.Ptr(save) || r.R32(sm + 0x20) != 1 || r.R16(sm + 0x26) != 3) return false;
    auto flag = [&](uint32_t f) { return (r.R8(save + 0x10 + 0x10C4 + f / 8) >> (f % 8) & 1) != 0; };
    const uint32_t n = r.R32(sm + 0x2C);
    if (n < 1 || n > 10) return false;
    for (uint32_t i = 0; i < n; i++)
    {
        const int a = (int)r.R8(sm + 0x3A + i);
        static const uint32_t unlock[] = { 0x6B, 0x6A, 0x11B, 0x11C, 0x11D, 0x11E };
        if (a <= 5) { if (flag(unlock[a])) m.actions.push_back(a); }
        else if (a == 11 || a == 12) { if (flag(0x9C)) m.actions.push_back(11); }
        else if (a != 6 && a != 9 && a != 10) return false; // the Safari Zone's and others' buttons are left to the game
    }
    m.cursor = (int)r.R8(fs + 0xD3);
    return m.cursor < (int)m.actions.size();
}

// The menu in a card at (x, y, w, h), its buttons where the game has them: Pokédex,
// Pokémon, Bag and Pokégear down the left, the trainer card, Save and Options down the right.
// shortcut: the Pokédex's place (top left) is the one left free; not seen on a save that has it drawn.
inline void DrawXMenu(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const XMenu& m, const std::string& name, float x, float y,
    float w, float h)
{
    using namespace HudColour;
    HudPanel(c, ui, sp, "panels/card", x, y, w, h);
    HudText(c, ui, f, x + w / 2, y + 22, 40, "Menu", Ink, 1, true);
    const float gx = x + 24, gy = y + 90, cw = (w - 48 - 16) / 2, ch = std::min(150.0f, (h - 90 - 24 - 3 * 14) / 4);
    for (size_t i = 0; i < m.actions.size(); i++)
    {
        const int a = m.actions[i];
        int col = 0, row = 0;
        const char* icon = "";
        std::string label;
        switch (a)
        {
        case 0: row = 0; icon = "menu/menu_dex"; label = "Pok\xC3\xA9" "dex"; break;
        case 1: row = 1; icon = "menu/menu_party"; label = "Pok\xC3\xA9mon"; break;
        case 2: row = 2; icon = "menu/menu_bag"; label = "Bag"; break;
        case 11: row = 3; icon = "menu/menu_pokegear"; label = "Pok\xC3\xA9gear"; break;
        case 3: col = 1; row = 0; icon = "menu/menu_trainer"; label = name; break;
        case 4: col = 1; row = 1; icon = "menu/menu_save"; label = "Save"; break;
        default: col = 1; row = 2; icon = "menu/menu_options"; label = "Options"; break;
        }
        const bool on = (int)i == m.cursor;
        const float bx = gx + col * (cw + 16), by = gy + row * (ch + 14);
        HudPanel(c, ui, sp, on ? "panels/row_selected" : "panels/row", bx, by, cw, ch);
        const float is = std::min(96.0f, ch - 52);
        HudSprite(c, ui, sp, std::string(icon) + (on ? "_selected" : ""), bx + cw / 2 - is / 2, by + 8, is, is);
        HudText(c, ui, f, bx + cw / 2, by + ch - 42, 32, label, Ink, 1);
    }
}

// A check of Page on made-up messages (run with SSR_WORLDUI_SELFTEST=1).
inline bool SelfTest()
{
    auto codes = [](std::initializer_list<uint16_t> l) { return std::vector<uint16_t>(l); };
    const uint16_t H = 0x132, i = 0x14D; // "H", "i"
    Dialogue d;
    bool ok = Page(codes({ H, i, 0xE000, i, 0x25BD, H, 0xFFFF }), 5, 3, d) && d.more && d.lines.size() == 2 && d.lines[1] == "i";
    ok = ok && Page(codes({ H, i, 0xE000, i, 0x25BD, H, 0xFFFF }), 6, 0, d) && !d.more && d.lines.size() == 2 && d.lines[0] == "i" && d.lines[1] == "H";
    ok = ok && Page(codes({ H, 0x25BC, i, 0xFFFF }), 3, 0, d) && d.lines.size() == 1 && d.lines[0] == "i";
    ok = ok && Page(codes({ 0xFFFE, 0xFF00, 1, 2, H, 0xFFFF }), 5, 0, d) && d.lines[0] == "H";
    ok = ok && !Page(codes({ 0xFFFE, 0x0203, 1, 40, H }), 5, 0, d);
    return ok;
}
}
