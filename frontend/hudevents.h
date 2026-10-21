#pragma once
// The messages of the game's event scenes redrawn in the HUD style: a Pokemon evolving
// ("What? X is evolving!", the moves it learns after, with their yes/no questions) and an
// egg hatching (its message and the nickname question). Each scene prints into the same box
// at the foot of its screen as the overworld's (tiles 2,19 of 27x4, hudworld.h) through one
// of the game's text printers; this mirrors that printer's text, as hudworld.h does the
// script's, and draws the same box. The scene itself (the Pokemon, the flashes, the egg) is
// the game's own and keeps playing, and the game keeps the input. Whenever anything does not
// check out the game's own box shows.
#include <algorithm>
#include <string>
#include <vector>
#include "hudworld.h"

namespace HudEvents
{
using HudParty::Ram;

constexpr uint32_t kEvolutionTask = 0x02075D09;  // sub_02075D08, the evolution's SysTask function (Thumb)
constexpr uint32_t kEvolutionPrint = 0x02077341; // sub_02077340, the callback of its printer
constexpr int kWinBytes = 27 * 4 * 32;           // the box's window: 27x4 tiles, 4bpp

// A String (pm_string.c: its most +0, its size +2, its magic +4, its codes from +8) whose
// codes `cur` points into; also one just deleted (its magic one more), whose memory the
// game's printer goes on reading (the egg's scene deletes its strings once printing starts).
inline uint32_t StringAround(const Ram& r, uint32_t cur)
{
    if (!r.Ok(cur, 2) || cur < 0x02000808u) return 0;
    for (uint32_t s = (cur - 8) & ~3u; s + 8 + 2048 >= cur; s -= 4)
    {
        const uint32_t magic = r.R32(s + 4);
        if ((magic == 0xB6F8D2ECu || magic == 0xB6F8D2EDu) && r.R16(s + 2) <= r.R16(s) && cur <= s + 8 + 2 * r.R16(s + 2)) return s;
    }
    return 0;
}

// Whether `win` is a Window (bg_window.h: its background +4, left +5, top +6, width +7,
// height +8, pixels +0x0C) at the box's place.
inline bool BoxWindow(const Ram& r, uint32_t win)
{
    return r.Ok(win, 0x10) && r.R8(win + 4) < 8 && r.R8(win + 5) == 2 && r.R8(win + 6) == 19 && r.R8(win + 7) == 27 && r.R8(win + 8) == 4 &&
        r.Ptr(r.R32(win + 0x0C));
}

// The text of a box: followed from the printer that prints into its window and, once the
// printer is done, kept for as long as the window's pixels stay as the printer left them.
struct Message
{
    std::vector<uint16_t> codes; // the message
    uint32_t win = 0;            // the Window it was printed into
    uint32_t sum = 0;            // the window's pixels once printed (0 until then)

    static uint32_t Sum(const Ram& r, uint32_t pixels)
    {
        uint32_t h = 2166136261u;
        for (int i = 0; i < kWinBytes; i += 4) h = (h ^ r.R32(pixels + i)) * 16777619u;
        return h | 1;
    }

    bool Read(const Ram& r, uint32_t window, HudWorld::Dialogue& d, uint32_t str = 0)
    {
        if (!BoxWindow(r, window)) return false;
        if (window != win) codes.clear(), sum = 0, win = window, last = {};
        for (uint32_t num = 0; num < 8; num++)
        {
            const uint32_t task = r.R32(HudWorld::kPrinterTasks + 4 * num), p = task ? r.R32(task + 0x10) : 0;
            if (!task || r.R32(task + 0x14) != HudWorld::kRunTextPrinter || !r.Ptr(p) || r.R32(p + 4) != win) continue;
            const uint32_t cur = r.R32(p), s = StringAround(r, cur);
            codes.clear();
            if (!s || (cur & 1)) return false;
            const uint32_t n = r.R16(s + 2);
            for (uint32_t i = 0; i < n; i++) codes.push_back((uint16_t)r.R16(s + 8 + 2 * i));
            sum = 0;
            return Show(r, (cur - s - 8) / 2, (int)r.R8(p + 0x28), d);
        }
        // Printed at once, without a printer: the message is the scene's String `str`.
        if (str)
        {
            codes.clear();
            const uint32_t n = r.R16(str + 2);
            if (!r.Ptr(str) || r.R32(str + 4) != 0xB6F8D2ECu || n > r.R16(str) || n > 1024) return false;
            for (uint32_t i = 0; i < n; i++) codes.push_back((uint16_t)r.R16(str + 8 + 2 * i));
            return Show(r, codes.size(), 0, d);
        }
        // No printer: the whole message, while the box is as the printer left it.
        if (codes.empty()) return false;
        const uint32_t now = Sum(r, r.R32(win + 0x0C));
        if (!sum) sum = now;
        if (now != sum || !Show(r, codes.size(), 0, d))
        {
            codes.clear();
            return false;
        }
        return true;
    }

    HudWorld::Dialogue last;     // the last page with text

    bool Show(const Ram& r, size_t done, int state, HudWorld::Dialogue& d)
    {
        if (!HudWorld::Page(codes, done, state, d)) return false;
        d.sign = -1;
        // Ink on the lines that have text and none on the others (not while a line scrolls away).
        if (state != 4)
            for (int l = 0; l < 2; l++)
            {
                const bool text = l < (int)d.lines.size() && d.lines[l].find_first_not_of(' ') != std::string::npos;
                if (text != HudWorld::LineInk(r, r.R32(win + 0x0C), 27, l)) return false;
            }
        // A message that ends waiting for a button clears its window once pressed, but the
        // screen keeps its last page until the scene moves on (the window is not copied again).
        bool blank = true;
        for (const std::string& l : d.lines) blank &= l.find_first_not_of(' ') == std::string::npos;
        if (!blank) last = d;
        else if (done + 1 >= codes.size() && !last.lines.empty()) d = last, d.more = false;
        return true;
    }
};

// What one of the scenes shows in its box.
struct Box
{
    HudWorld::Dialogue d;
    int bg = -1;             // the window's background: 0-3 the main engine's, 4-7 the other's
    int yesNo = -1;          // a question's cursor (0 the first answer), -1 none
    std::string answers[2];  // its answers
    bool canStop = false;    // B stops the evolution now
};

// The evolution (unk_020755E8.s, not decompiled; Platinum's evolution.c is much the same):
// its SysTask's data, 0xBC bytes: its window +4 (the box), its message String +0x10, its step
// +0x64, whether B may stop it +0x7C (bit 0, while step 8 plays the change), the question's
// stage +0x8A (0 while it waits for the answer) and cursor +0x8B (1 the first, 2 the second).
// The task is looked for only while one of its messages prints, and kept while it runs.
inline bool ReadEvolution(const Ram& r, const std::vector<uint8_t>& rom, uint32_t& task, Message& msg, Box& b)
{
    auto alive = [&](uint32_t t) { return r.Ptr(t) && r.R32(t + 0x14) == kEvolutionTask && r.Ok(r.R32(t + 0x10), 0xBC); };
    if (!alive(task))
    {
        task = 0;
        bool printing = false;
        for (uint32_t num = 0; num < 8; num++)
        {
            const uint32_t t = r.R32(HudWorld::kPrinterTasks + 4 * num), p = t ? r.R32(t + 0x10) : 0;
            printing |= t && r.R32(t + 0x14) == HudWorld::kRunTextPrinter && r.Ptr(p) && r.R32(p + 0x1C) == kEvolutionPrint;
        }
        if (!printing) return false;
        for (uint32_t off = 0x14; off + 4 <= r.size && !task; off += 4)
            if (RomGet32(r.p + off) == kEvolutionTask && alive(0x02000000 + off - 0x14)) task = 0x02000000 + off - 0x14;
        if (!task) return false;
    }
    const uint32_t e = r.R32(task + 0x10), win = r.R32(e + 4);
    if (!msg.Read(r, win, b.d)) return false;
    b.bg = (int)r.R8(win + 4);
    const uint32_t step = r.R8(e + 0x64), cursor = r.R8(e + 0x8B);
    b.canStop = step == 8 && (r.R32(e + 0x7C) & 1);
    // Step 21 asks whether to delete a move for the new one, 35 whether to give the new one up
    // (battle text 0x49D/0x49E, and 0x4A2/0x4A3 with the move's name, +0x6C, on its buttons).
    if ((step == 21 || step == 35) && r.R8(e + 0x8A) == 0 && (cursor == 1 || cursor == 2))
    {
        const std::string move = step == 35 ? Party_Text(750, (int)r.R16(e + 0x6C)) : "";
        b.yesNo = (int)cursor - 1;
        b.answers[0] = NdsMessage(rom.data(), rom.size(), 197, step == 21 ? 0x49D : 0x4A2, move);
        b.answers[1] = NdsMessage(rom.data(), rom.size(), 197, step == 21 ? 0x49E : 0x4A3, move);
        if (b.answers[0].empty() || b.answers[1].empty() || (step == 35 && move.empty())) b.yesNo = -1;
    }
    return true;
}

// An egg hatching (HatchEgg, overlay 95, not decompiled): its data (0x8C bytes): the
// arguments +0, the box's Window +0x18, the step +0x60 (12 while it asks whether to give the
// Pokemon a nickname) and the question's buttons +0x88 (their stage +0x18, 0 while waiting,
// and cursor +0x19, 1 Yes or 2 No; their text is messages 2 and 3 of bank 187).
inline bool ReadHatch(const Ram& r, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, Message& msg, Box& b)
{
    const uint32_t mgr = HudParty::FindApp(r, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t d = r.R32(m + 0x1C);
        return r.Ok(d, 0x8C) && r.R32(d) == r.R32(m + 0x18);
    });
    if (!mgr) return false;
    const uint32_t d = r.R32(mgr + 0x1C);
    if (!msg.Read(r, d + 0x18, b.d)) return false;
    b.bg = (int)r.R8(d + 0x18 + 4);
    const uint32_t q = r.R32(d + 0x88);
    if (r.R32(d + 0x60) == 12 && r.Ok(q, 0x1C) && r.R8(q + 0x18) == 0 && (r.R8(q + 0x19) == 1 || r.R8(q + 0x19) == 2))
    {
        b.yesNo = (int)r.R8(q + 0x19) - 1;
        b.answers[0] = Party_Text(187, 2);
        b.answers[1] = Party_Text(187, 3);
        if (b.answers[0].empty() || b.answers[1].empty()) b.yesNo = -1;
    }
    return true;
}

// The trade animation (TradeSequence, overlay 71, not decompiled; also the trades with
// people in the game): its data (0x158 bytes): the arguments +0, and the data of the part
// it plays now +0x154 (sending, receiving), which holds the box's Window and, at +0x14 from
// it, the String printed into it at once: the Window at +0x58 or +0x5C, by the part.
inline bool ReadTrade(const Ram& r, uint32_t& cache, uint32_t init, uint32_t exec, uint32_t exit, Message& msg, Box& b)
{
    const uint32_t mgr = HudParty::FindApp(r, cache, init, exec, exit, [&](uint32_t m) {
        const uint32_t d = r.R32(m + 0x1C);
        return r.Ok(d, 0x158) && r.R32(d) == r.R32(m + 0x18);
    });
    if (!mgr) return false;
    const uint32_t part = r.R32(r.R32(mgr + 0x1C) + 0x154);
    if (!r.Ok(part, 0x80)) return false;
    for (uint32_t win : { part + 0x58, part + 0x5C })
        if (BoxWindow(r, win) && msg.Read(r, win, b.d, r.R32(win + 0x14)))
        {
            b.bg = (int)r.R8(win + 4);
            return true;
        }
    return false;
}

// The top row of the box on a screen whose background `layer` (a LAYER_BG class) shows the
// box and nothing else, from the renderer's per-pixel layer flags; -1 otherwise. (The
// scenes fill the rest of the box's rows on that background, beside the box, with black.)
inline int BoxTop(const uint8_t* flags, uint8_t layer)
{
    if (!flags) return -1;
    int inBox = 0, top = -1;
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 256; x++)
            if (LayerClass(flags[y * 256 + x]) == layer)
            {
                if (y < HudWorld::kBoxY || y >= HudWorld::kBoxY + HudWorld::kBoxH) return -1;
                if (top < 0) top = y;
                inBox++;
            }
    return inBox > 16 * 8 ? top : -1;
}

// Whether the box's paper (its brightest pixels along the window's last row, under the
// letters) is at full brightness on the screen `px` (XRGB): not while the scene fades out,
// when the game's own box fades with it.
inline bool BoxLit(const uint32_t* px)
{
    uint32_t best = 0;
    for (int x = 24; x < 232; x += 2)
    {
        const uint32_t p = px[182 * 256 + x];
        best = std::max(best, std::min({ p >> 16 & 0xFF, p >> 8 & 0xFF, p & 0xFF }));
    }
    return best >= 0xD0;
}

// The box over the game's own (x, y, w, h in design pixels), its question's answers above it
// as the overworld's yes/no, and while B may stop an evolution, that button (`stopGlyph`).
inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Box& b, float x, float y, float w, float h, uint32_t frame,
    const std::string& stopGlyph)
{
    HudWorld::Draw(c, ui, sp, f, b.d, x, y, w, h, frame);
    if (b.yesNo >= 0)
    {
        const float bw = std::max({ 340.0f, HudTextWidth(ui, f, 48, b.answers[0]) + 140, HudTextWidth(ui, f, 48, b.answers[1]) + 140 });
        const float bh = 86, gap = 18, bx = x + w - bw;
        for (int i = 0; i < 2; i++)
        {
            const float by = y - 30 - (2 - i) * (bh + gap) + gap;
            HudPanel(c, ui, sp, i == b.yesNo ? "panels/row_selected" : "panels/row", bx, by, bw, bh);
            if (i == b.yesNo) HudSprite(c, ui, sp, "cursor/cursor", bx + 24, by + 23, 36, 40);
            HudText(c, ui, f, bx + bw / 2 + 20, by + 18, 48, b.answers[i], HudColour::Ink, 1);
        }
    }
    if (b.canStop && !b.d.more)
    {
        const char* label = "Stop";
        const float lw = HudTextWidth(ui, f, 40, label), lx = x + w - 48 - lw;
        HudText(c, ui, f, lx, y + h - 74, 40, label, HudColour::Ink);
        HudSprite(c, ui, sp, stopGlyph, lx - 56, y + h - 78, 44, 44);
    }
}
}
