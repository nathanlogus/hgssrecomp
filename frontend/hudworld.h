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
    std::string Key() const
    {
        std::string k = more ? "+" : "-";
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

// The message box of the overworld's script, read from the game's memory.
inline bool ReadDialogue(const Ram& r, Dialogue& d)
{
    const uint32_t fs = r.R32(kFieldSystem);
    if (!r.Ptr(fs) || !(r.R32(fs + 0xD0) >> 16 & 0x40)) return false; // FieldSystem.textbox_open
    const uint32_t env = ScriptEnv(r, fs);
    if (!env || r.R8(env + 8) != 1) return false;
    // Its window (+0x14) where DialogBox_AddWindowToLayer3 puts it.
    const uint32_t win = env + 0x14, pixels = r.R32(win + 0x0C);
    if (r.R8(win + 4) != 3 || r.R8(win + 5) != 2 || r.R8(win + 6) != 19 || r.R8(win + 7) != 27 || r.R8(win + 8) != 4 || !r.Ptr(pixels))
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
            if (text != LineInk(r, pixels, 27, l)) return false;
        }
    return true;
}

// Whether the top screen's BG3 shows nothing but the message box (no menu or other window
// beside it), from the renderer's per-pixel layer flags.
inline bool OnlyTheBox(const uint8_t* flags)
{
    if (!flags) return false;
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 256; x++)
            if (LayerClass(flags[y * 256 + x]) == LAYER_BG3 && (x < kBoxX || x >= kBoxX + kBoxW || y < kBoxY || y >= kBoxY + kBoxH))
                return false;
    return true;
}

// A script's yes/no question (ScrCmd_GetMenuChoice): the touch screen's buttons
// (overlay 27, run by the field's bottom-screen task at FieldSystem +0xD8), which the d-pad
// moves too. Its data: +0 its step (4 waiting for the answer, 5 the answer taken), +4 where
// the answer goes, +0x24 the FieldSystem, +0x394 the cursor (0 yes, 1 no). -1 when none.
inline int ReadYesNo(const Ram& r)
{
    const uint32_t fs = r.R32(kFieldSystem);
    if (!r.Ptr(fs)) return -1;
    const uint32_t env = ScriptEnv(r, fs);
    if (!env) return -1;
    // A context of the script (+0x38, three) waits for it in sub_020477C0 (its native function +4).
    uint32_t ctx = 0;
    for (uint32_t k = 0; k < 3 && !ctx; k++)
    {
        const uint32_t c = r.R32(env + 0x38 + 4 * k);
        if (r.Ptr(c) && (r.R32(c + 4) & ~1u) == 0x020477C0) ctx = c;
    }
    if (!ctx) return -1;
    const uint32_t d = r.R32(r.R32(fs + 0xD8) + 0x10);
    if (!r.Ptr(d) || r.R8(d) != 3) return -1;
    const uint32_t a = r.R32(r.R32(d + 4) + 0x10), out = r.R32(a + 4);
    if (!r.Ptr(a) || r.R32(a + 0x24) != fs || out < ctx || out >= ctx + 0x100) return -1; // the answer goes to the context's data
    const uint32_t step = r.R32(a), cur = r.R32(a + 0x394);
    return (step == 4 || step == 5) && cur <= 1 ? (int)cur : -1;
}

// The box over the game's own (x, y, w, h in design pixels); `frame` moves the arrow.
// `yesNo`: the cursor of a yes/no question to show above the box, or -1.
inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Dialogue& d, float x, float y, float w, float h, uint32_t frame,
    int yesNo = -1)
{
    HudPanel(c, ui, sp, "panels/dialogue_box", x, y, w, h);
    const float pitch = 78, ty = y + (h - pitch * 2) / 2 + 2;
    for (size_t i = 0; i < d.lines.size(); i++) HudText(c, ui, f, x + 48, ty + pitch * i, 64, d.lines[i], HudColour::Ink);
    if (d.more)
    {
        const int phase = frame / 8 % 4;
        const float bob = phase == 2 ? 8.0f : phase ? 4.0f : 0.0f;
        HudSprite(c, ui, sp, "icons/icon_arrow_down", x + w - 84, y + h - 76 + bob, 44, 44, 0xFF6F7BF7);
    }
    if (yesNo >= 0)
    {
        const float bw = 340, bh = 86, gap = 18, bx = x + w - bw;
        for (int i = 0; i < 2; i++)
        {
            const float by = y - 30 - (2 - i) * (bh + gap) + gap;
            HudPanel(c, ui, sp, i == yesNo ? "panels/row_selected" : "panels/row", bx, by, bw, bh);
            if (i == yesNo) HudSprite(c, ui, sp, "cursor/cursor", bx + 24, by + 23, 36, 40);
            HudText(c, ui, f, bx + bw / 2, by + 15, 56, i ? "No" : "Yes", HudColour::Ink, 1);
        }
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
