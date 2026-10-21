#pragma once
// The Game Corner's Voltorb Flip (src/voltorb_flip/) redrawn in the HUD style on the one
// screen: the board with its cards, memos and the totals along each row and column, the
// level, the coins, the memo panel and the game's messages and menus. The game keeps the
// input (it already plays with the d-pad, A, X for the memo and B to quit); everything is
// read from its memory each frame, and anything that does not check out leaves the game's
// own picture on screen. Its three "Game Info" lessons are left as the game draws them.
#include <string>
#include <vector>
#include "hudbag.h"
#include "hudworld.h"
#include "romart.h"

namespace HudMinigames
{
using HudParty::Ram;

struct Voltorb
{
    int workflow = 0, wfState = 0, task = 0; // the task engine's workflow, its stage and the task's own step
    int type[25] = {}, memo[25] = {};
    bool flipped[25] = {};
    int points[2][5] = {}, voltorbs[2][5] = {}; // [0] columns, [1] rows
    bool totals = false;                         // the totals are shown (not yet in the menus)
    int level = 1;                               // as shown: 1-8
    int coins = 0, payout = 0;
    int focus = 0;           // the board's cursor: 0-24 a card, 25 the memo button, 26 Quit
    bool cursor = false;     // the board's cursor is shown
    bool memoOpen = false, memoFocused = false;
    int memoButton = 0;      // 0 Voltorb, 1-3 the numbers, 4 back to the board
    bool reveal = false;     // the whole board turned over at the end of a game
    int revealRows = 5;      // how many rows of it are still face up
    HudWorld::Dialogue message;
    bool messageShown = false;
    std::vector<std::string> menu; // the list the game shows, with its cursor
    int menuCursor = -1;
    int yesNo = -1;
    // One of the three lessons of Game Info (1-3), shown in place of the board.
    int Lesson() const { return workflow >= 4 && workflow <= 6 ? workflow - 3 : 0; }
};

// The text of a message printed by the game's text printer into `win` from the String `s`
// (its codes from +8): the page shown now. False when it is not being printed.
inline bool PrinterPage(const Ram& r, uint32_t s, uint32_t win, int printerId, HudWorld::Dialogue& d)
{
    if (!r.Ptr(s) || r.R32(s + 4) != 0xB6F8D2ECu || printerId < 0 || printerId >= 8) return false;
    const uint32_t n = r.R16(s + 2);
    if (n > r.R16(s) || n > 2048) return false;
    std::vector<uint16_t> codes(n);
    for (uint32_t i = 0; i < n; i++) codes[i] = (uint16_t)r.R16(s + 8 + 2 * i);
    size_t done = n;
    int state = 0;
    const uint32_t task = r.R32(HudWorld::kPrinterTasks + 4 * printerId), p = task ? r.R32(task + 0x10) : 0;
    if (task && r.R32(task + 0x14) == HudWorld::kRunTextPrinter && r.Ptr(p) && r.R32(p + 4) == win)
    {
        const uint32_t cur = r.R32(p);
        if (cur < s + 8 || cur > s + 8 + 2 * n || (cur & 1)) return false;
        done = (cur - s - 8) / 2;
        state = (int)r.R8(p + 0x28);
    }
    d.sign = -1;
    return HudWorld::Page(codes, done, state, d);
}

// The workflows (VoltorbFlipTaskId).
enum { WF_COIN_CASE_FULL, WF_NEW_ROUND, WF_MAIN_MENU, WF_GAME_INFO, WF_HOW_TO_PLAY, WF_HINT, WF_ABOUT_MEMO, WF_RENDER_BOARD,
    WF_BOARD, WF_FLIP, WF_WIN, WF_AWARD, WF_REVEAL, WF_MEMO_TOUCH, WF_MEMO_BUTTONS, WF_QUIT };

// VoltorbFlipAppData, the OverlayManager's data (+0x1C), and what it points to:
//   +0x08 the coins (u16 *), +0x20 the list menu, +0x24 windows[17] (0x10 each: 0 the main
//   message window, 1 the small one), +0x13C the yes/no prompt, +0x140 the main window's
//   String while it prints, +0x22B its printer, +0x22C the payout shown, +0x238 the memo
//   open, +0x23C the task engine (its data +4, workflow +8, stage +0x0D, task step +0x0E),
//   +0x240 the input handler (focus +0x0D, memo button +0x0E bits 0-3, memo focused +0x0F
//   bit 1), +0x244 the game (Card cards[5][5] of 12 bytes: type, memo, flipped; then the
//   points per column and row, Voltorbs per column and row, u8[5] each from +0x12C; the
//   payout +0x144, cards flipped +0x14C, level +0x14E: 0 is Lv. 8), +0x248 the flip
//   animation (its row +0x13).
inline bool ReadVoltorb(const Ram& r, uint32_t man, Voltorb& v)
{
    const uint32_t d = r.R32(man + 0x1C);
    const uint32_t wf = r.R32(d + 0x23C), in = r.R32(d + 0x240), g = r.R32(d + 0x244), coins = r.R32(d + 8);
    if (!r.Ptr(d) || !r.Ptr(wf) || !r.Ptr(in) || !r.Ptr(g) || !r.Ok(coins, 2) || r.R32(wf + 4) != d) return false;
    v.workflow = (int)r.R16(wf + 8);
    v.wfState = (int)r.R8(wf + 0x0D);
    v.task = (int)r.R8(wf + 0x0E);
    if (v.workflow > WF_QUIT || v.wfState > 4) return false;
    for (int i = 0; i < 25; i++)
    {
        const uint32_t c = g + 12 * i;
        v.type[i] = (int)r.R32(c);
        v.memo[i] = (int)r.R32(c + 4);
        const uint32_t f = r.R32(c + 8);
        if (v.type[i] > 4 || v.memo[i] > 15 || f > 1) return false;
        v.flipped[i] = f;
    }
    for (int a = 0; a < 2; a++)
        for (int k = 0; k < 5; k++)
        {
            v.points[a][k] = (int)r.R8(g + 0x12C + 5 * a + k);
            v.voltorbs[a][k] = (int)r.R8(g + 0x136 + 5 * a + k);
            if (v.points[a][k] > 15 || v.voltorbs[a][k] > 5) return false;
        }
    // The totals are printed as the board is drawn for a game (and zeroed once it is over).
    v.totals = v.workflow >= WF_RENDER_BOARD;
    const int level = (int)r.R8(g + 0x14E);
    if (level > 7) return false;
    // (Between one game and the next the game's state is cleared for a moment: Lv. 8.)
    static int shownLevel = 1;
    if (v.workflow != WF_COIN_CASE_FULL) shownLevel = 8 - level;
    v.level = shownLevel;
    v.coins = (int)r.R16(coins);
    v.payout = (int)r.R16(d + 0x22C);
    if (v.coins > 50000 || v.payout > 50000) return false;
    v.focus = (int)r.R8(in + 0x0D);
    if (v.focus > 26) return false;
    v.memoOpen = r.R32(d + 0x238) == 1;
    v.memoFocused = r.R8(in + 0x0F) >> 1 & 1;
    v.memoButton = (int)(r.R8(in + 0x0E) & 15);
    if (v.memoButton > 4) return false;
    v.cursor = v.workflow == WF_RENDER_BOARD || v.workflow == WF_BOARD || v.workflow == WF_FLIP || v.workflow == WF_MEMO_TOUCH ||
        v.workflow == WF_MEMO_BUTTONS || v.workflow == WF_QUIT;
    // The board turned over: all of it once the game shows it (task step 1 and 2), then
    // row by row back (step 3, the animation's row counting up).
    v.reveal = v.workflow == WF_REVEAL && v.task >= 1;
    v.revealRows = 5;
    if (v.workflow == WF_REVEAL && v.task == 3)
    {
        const int row = (int)r.R8(d + 0x248 + 0x13);
        v.revealRows = row > 5 ? 0 : 5 - row;
    }

    // The main window's message: read off the printer while it prints, then kept for as long
    // as the same workflow runs (each one that prints clears or replaces it).
    static HudWorld::Dialogue kept;
    static int keptFor = -1;
    if (keptFor != v.workflow) keptFor = -1;
    HudWorld::Dialogue page;
    if (PrinterPage(r, r.R32(d + 0x140), d + 0x24, (int)r.R8(d + 0x22B), page)) kept = page, keptFor = v.workflow;
    const bool mainWindow = v.workflow == WF_COIN_CASE_FULL || v.workflow == WF_NEW_ROUND || v.workflow == WF_MAIN_MENU ||
        v.workflow == WF_GAME_INFO || v.Lesson() || v.workflow == WF_WIN || (v.workflow == WF_AWARD && v.wfState < 3) ||
        (v.workflow == WF_QUIT && v.wfState < 3);
    v.messageShown = false;
    v.message = HudWorld::Dialogue();
    if (mainWindow && keptFor == v.workflow) v.message = kept, v.messageShown = true;

    // The small window's messages, printed at once (so not readable from a String): as the
    // card turns, by the same rules as the game.
    if (v.workflow == WF_FLIP)
    {
        const int id = v.focus < 25 ? v.focus : -1;
        if (id < 0) return false;
        const int col = id % 5, row = id / 5;
        auto flippedIn = [&](bool isRow, int k) {
            int n = 0;
            for (int j = 0; j < 5; j++) n += v.flipped[isRow ? k * 5 + j : j * 5 + k];
            return n;
        };
        const bool before = (v.wfState == 0 && v.task >= 1) || (v.wfState == 2 && v.task == 0);
        const bool after = v.wfState >= 2 && v.task >= 2;
        std::string text;
        if (before && !v.flipped[id] && v.voltorbs[0][col] && v.voltorbs[1][row])
        {
            const int fc = flippedIn(false, col), fr = flippedIn(true, row);
            if ((fc < 5 && 100 * v.voltorbs[0][col] / (5 - fc) >= 75) || (fr < 5 && 100 * v.voltorbs[1][row] / (5 - fr) >= 75))
                text = "Is this what you\xE2\x80\x99re expecting?!";
        }
        else if (after && v.type[id] == 4 && v.task >= 4)
            text = "Oh no! You get 0 Coins!";
        else if (after && v.type[id] >= 1 && v.type[id] <= 3)
        {
            const int total = (int)r.R16(g + 0x144), flips = (int)r.R8(g + 0x14C);
            if (flips == 1) text = std::to_string(v.type[id]) + "! Received " + std::to_string(v.type[id]) + " Coin(s)!";
            else if (v.type[id] > 1) text = "x" + std::to_string(v.type[id]) + "! Received " + std::to_string(total) + " Coins!";
        }
        if (!text.empty())
        {
            v.message.lines = { text };
            v.messageShown = true;
        }
    }

    // The game's lists: Play, Game Info, Quit; and the lessons to choose from.
    v.menu.clear();
    v.menuCursor = -1;
    if ((v.workflow == WF_MAIN_MENU || v.workflow == WF_GAME_INFO) && v.wfState == 2)
    {
        const uint32_t m = r.R32(d + 0x20);
        const int n = (int)r.R8(m + 0x18), cur = (int)r.R8(m + 0x24);
        const int want = v.workflow == WF_MAIN_MENU ? 3 : 4;
        if (!r.Ptr(m) || n != want || cur >= n) return false;
        if (v.workflow == WF_MAIN_MENU) v.menu = { "Play", "Game Info", "Quit" };
        else v.menu = { "How to Play", "Hint!", "About Memos", "Return" };
        v.menuCursor = cur;
    }
    // "Will you quit?": its yes/no (placed at 25, 10; its cursor at +0x75).
    v.yesNo = -1;
    if (v.workflow == WF_QUIT && v.wfState == 2)
    {
        const uint32_t yn = r.R32(d + 0x13C);
        if (r.Ptr(yn) && (r.R32(yn + 0x70) & 0xFFFF) == (10u << 8 | 25u))
        {
            const int sel = (int)r.R8(yn + 0x75);
            if (sel <= 1) v.yesNo = sel;
        }
    }
    return true;
}

// The colours of the rows and columns, as the game's board has them.
constexpr uint32_t kLineColour[5] = { 0xFFE8735A, 0xFF4FB35E, 0xFFEFA43A, 0xFF4C9BE8, 0xFFB46AD8 };

inline void DrawVoltorb(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, const Voltorb& v,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    using HudParty::FillRound;
    DrawBackdrop(c, sp, "backgrounds/bg_lime", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;
    const Image* voltorbIcon = art.PokemonIcon(100);
    // The party icon's Voltorb fills only the lower middle of its 32 x 32: drawn larger, so
    // that it fills the box (x, y, w, h) given.
    auto voltorbAt = [&](float x, float y, float w, float h) {
        if (!voltorbIcon) return;
        const float s = w * 1.9f;
        HudPixelArt(c, ui, voltorbIcon, x + w / 2 - s / 2, y + h / 2 - s * 0.66f, s, s);
    };

    const float pitch = 150, cs = 128, bx = x0 + 80, by = 52;
    FillRound(c, ui, bx - 30, by - 30, pitch * 6 + 40, pitch * 6 + 40, 36, 0xF0FFFFFF);
    if (const int lesson = v.Lesson())
    {
        // A lesson of Game Info: the game's own captions for its pictures, with an example.
        static const char* title[3] = { "How to Play", "Hint!", "About Memos" };
        static const char* caption[3] = {
            "If you flip the cards in this order, you\xE2\x80\x99ll collect: 3 \xC3\x97 1 \xC3\x97 2 \xC3\x97 1 \xC3\x97 3... A total of 18 Coins! "
            "If you select \xE2\x80\x9CQuit,\xE2\x80\x9D you\xE2\x80\x99ll keep those 18 Coins. But if you find Voltorb, you\xE2\x80\x99ll lose all your Coins!",
            "By looking at the numbers on the sides of the cards, you can see the hidden number and Voltorb totals.",
            "Open the Memo, then choose the cards to add and remove marks." };
        HudText(c, ui, f, bx + 20, by, 48, title[lesson - 1], Ink, 0, true);
        // The example: a row of five cards and its totals.
        static const int row[3][5] = { { 3, 1, 2, 1, 3 }, { 1, 4, 2, 1, 3 }, { 0, 0, 0, 0, 0 } };
        const float ey = by + 120;
        int points = 0, volts = 0;
        for (int k = 0; k < 5; k++)
        {
            const int t = row[lesson - 1][k];
            const float x = bx + k * pitch;
            points += t < 4 ? t : 0, volts += t == 4;
            if (t)
            {
                FillRound(c, ui, x, ey, cs, cs, 18, t == 4 ? 0xFFFFE3DC : 0xFFFFFBEA);
                if (t == 4) voltorbAt(x + 14, ey + 14, cs - 28, cs - 28);
                else HudText(c, ui, f, x + cs / 2, ey + 24, 80, std::to_string(t), Ink, 1, true);
            }
            else
            {
                FillRound(c, ui, x, ey, cs, cs, 18, 0xFF2F8F66);
                FillRound(c, ui, x + 10, ey + 10, cs - 20, cs - 20, 12, 0xFF3FA978);
                if (k == 1 || k == 3)
                {
                    FillRound(c, ui, x + 8, ey + 8, 38, 38, 10, 0xFFFFFFFF);
                    if (k == 1) voltorbAt(x + 11, ey + 11, 32, 32);
                    else HudText(c, ui, f, x + 27, ey + 11, 32, "2", Ink, 1, true);
                }
            }
        }
        if (lesson == 2)
        {
            const float x = bx + 5 * pitch;
            FillRound(c, ui, x, ey, cs, cs, 18, kLineColour[0]);
            char pts[8];
            snprintf(pts, sizeof pts, "%02d", points);
            HudText(c, ui, f, x + cs / 2, ey + 6, 48, pts, White, 1, true);
            voltorbAt(x + 12, ey + cs / 2 + 6, 44, 44);
            HudText(c, ui, f, x + cs - 34, ey + cs / 2 + 6, 48, std::to_string(volts), White, 1, true);
        }
        const std::vector<std::string> lines = HudWrap(ui, f, 40, caption[lesson - 1], pitch * 6 - 60);
        for (size_t i = 0; i < lines.size() && i < 9; i++) HudText(c, ui, f, bx + 20, ey + cs + 50 + 54 * i, 40, lines[i], Ink);
    }
    else
    {
    // The board: 5 x 5 cards, the totals of each row at its right and of each column below.
    // The lines joining the cards, in their row's and column's colours.
    for (int k = 0; k < 5; k++)
        for (int j = 0; j < 5; j++)
        {
            FillBlend(c, ui(bx + j * pitch + cs), ui(by + k * pitch + cs / 2 - 5), ui(pitch - cs), ui(10), kLineColour[k]);
            FillBlend(c, ui(bx + k * pitch + cs / 2 - 5), ui(by + j * pitch + cs), ui(10), ui(pitch - cs), kLineColour[k]);
        }
    for (int i = 0; i < 25; i++)
    {
        const float x = bx + (i % 5) * pitch, y = by + (i / 5) * pitch;
        const bool up = v.flipped[i] || (v.reveal && i / 5 < v.revealRows && v.type[i]);
        const bool here = v.cursor && v.focus == i;
        if (up)
        {
            // Turned over: its number, or a Voltorb; at the end those not turned in the game are dimmer.
            const bool missed = !v.flipped[i];
            FillRound(c, ui, x, y, cs, cs, 18, missed ? 0xFFE6E2D6 : v.type[i] == 4 ? 0xFFFFE3DC : 0xFFFFFBEA);
            if (v.type[i] == 4)
            {
                voltorbAt(x + 14, y + 14, cs - 28, cs - 28);
            }
            else
                HudText(c, ui, f, x + cs / 2, y + 24, 80, std::to_string(v.type[i]), missed ? Muted : Ink, 1, true);
        }
        else
        {
            FillRound(c, ui, x, y, cs, cs, 18, 0xFF2F8F66);
            FillRound(c, ui, x + 10, y + 10, cs - 20, cs - 20, 12, 0xFF3FA978);
            // The memo's marks in the corners, as the game puts them: Voltorb, 1 / 2, 3.
            const float mx[4] = { x + 8, x + cs - 46, x + 8, x + cs - 46 }, my[4] = { y + 8, y + 8, y + cs - 46, y + cs - 46 };
            for (int m = 0; m < 4; m++)
                if (v.memo[i] >> m & 1)
                {
                    FillRound(c, ui, mx[m], my[m], 38, 38, 10, 0xFFFFFFFF);
                    if (m == 0) voltorbAt(mx[m] + 3, my[m] + 3, 32, 32);
                    else if (m) HudText(c, ui, f, mx[m] + 19, my[m] + 3, 32, std::to_string(m), Ink, 1, true);
                }
        }
        if (here)
        {
            const uint32_t ring = v.memoOpen ? 0xFFFFC23A : 0xFFE84A3A;
            FillBlend(c, ui(x - 8), ui(y - 8), ui(cs + 16), ui(8), ring);
            FillBlend(c, ui(x - 8), ui(y + cs), ui(cs + 16), ui(8), ring);
            FillBlend(c, ui(x - 8), ui(y - 8), ui(8), ui(cs + 16), ring);
            FillBlend(c, ui(x + cs), ui(y - 8), ui(8), ui(cs + 16), ring);
        }
    }
    // The totals: the points hidden in the line above, and its Voltorbs below.
    for (int a = 0; a < 2; a++)
        for (int k = 0; k < 5; k++)
        {
            const float x = a ? bx + 5 * pitch : bx + k * pitch, y = a ? by + k * pitch : by + 5 * pitch;
            FillRound(c, ui, x, y, cs, cs, 18, kLineColour[k]);
            FillRound(c, ui, x + 6, y + cs / 2, cs - 12, cs / 2 - 6, 12, 0x40FFFFFF);
            char pts[8];
            snprintf(pts, sizeof pts, "%02d", v.totals ? v.points[a][k] : 0);
            HudText(c, ui, f, x + cs / 2, y + 6, 48, pts, White, 1, true);
            voltorbAt(x + 12, y + cs / 2 + 6, 44, 44);
            HudText(c, ui, f, x + cs - 34, y + cs / 2 + 6, 48, std::to_string(v.totals ? v.voltorbs[a][k] : 0), White, 1, true);
        }
    }

    // The right: the level and the coins, the memo, Quit and the game's messages.
    const float rx = x0 + 1100, rw = 760;
    HudText(c, ui, f, rx, 40, 48, "Voltorb Flip", Ink, 0, true);
    HudText(c, ui, f, rx + rw, 46, 40, "Level " + std::to_string(v.level), Ink, 2, true);
    HudPanel(c, ui, sp, "panels/card", rx, 124, rw, 200);
    auto amount = [&](float y, const char* label, int n) {
        HudText(c, ui, f, rx + 36, y + 6, 32, label, Muted);
        HudText(c, ui, f, rx + rw - 36, y, 48, HudScreens::Thousands((uint32_t)n), Ink, 2, true);
    };
    amount(150, "Coins", v.coins);
    FillBlend(c, ui(rx + 36), ui(222), ui(rw - 72), ui(2), 0xFFE3E9F1);
    amount(244, "This game", v.payout);

    const bool menuUp = !v.menu.empty() || v.yesNo >= 0;
    if (!menuUp)
    {
        // The memo: its button, and while it is open the four marks for the card under the
        // cursor (lit when the card has it) with Back when the memo has the cursor.
        const bool onMemo = v.cursor && v.focus == 25;
        HudPanel(c, ui, sp, onMemo ? "panels/button_yellow" : "panels/button_white", rx, 348, rw, 72);
        if (onMemo) HudSprite(c, ui, sp, "cursor/cursor_small", rx + 16, 368, 30, 32);
        HudSprite(c, ui, sp, "icons/icon_book", rx + 56, 364, 40, 40);
        HudText(c, ui, f, rx + rw / 2, 366, 32, v.memoOpen ? "Close Memo" : "Open Memo", Ink, 1);
        if (v.memoOpen)
        {
            const int card = v.focus < 25 ? v.focus : -1;
            HudPanel(c, ui, sp, "panels/card", rx, 432, rw, 196);
            const float tw = 140, gap = (rw - 48 - 5 * tw) / 4;
            for (int m = 0; m < 5; m++)
            {
                const float x = rx + 24 + m * (tw + gap), y = 460;
                const bool lit = m < 4 && card >= 0 && (v.memo[card] >> m & 1);
                const bool sel = v.memoFocused && v.memoButton == m;
                HudPanel(c, ui, sp, sel ? "panels/button_yellow" : lit ? "panels/button_lavender" : "panels/button_white", x, y, tw, 140,
                    m < 4 && card < 0 ? 0x80FFFFFF : 0xFFFFFFFF);
                if (m == 0) voltorbAt(x + tw / 2 - 40, y + 30, 80, 80);
                else if (m < 4) HudText(c, ui, f, x + tw / 2, y + 30, 64, std::to_string(m), lit ? Ink : Muted, 1, true);
                else HudText(c, ui, f, x + tw / 2, y + 52, 32, "Back", Ink, 1);
                if (sel) HudSprite(c, ui, sp, "cursor/cursor_small", x + 6, y + 8, 30, 32);
            }
        }
        const bool onQuit = v.cursor && v.focus == 26;
        HudPanel(c, ui, sp, onQuit ? "panels/button_yellow" : "panels/button_white", rx, 644, rw, 72);
        if (onQuit) HudSprite(c, ui, sp, "cursor/cursor_small", rx + 16, 664, 30, 32);
        HudSprite(c, ui, sp, "icons/icon_exit", rx + 56, 660, 40, 40);
        HudText(c, ui, f, rx + rw / 2, 662, 32, "Quit", Ink, 1);
    }
    else
    {
        // The game's list, or Yes and No.
        const std::vector<std::string> rows = v.yesNo >= 0 ? std::vector<std::string>{ "Yes", "No" } : v.menu;
        const int sel = v.yesNo >= 0 ? v.yesNo : v.menuCursor;
        const float rh = 80, gap = 14, top = 716 - rows.size() * (rh + gap);
        for (size_t i = 0; i < rows.size(); i++)
        {
            const float y = top + i * (rh + gap);
            HudPanel(c, ui, sp, (int)i == sel ? "panels/row_selected" : "panels/row", rx, y, rw, rh);
            if ((int)i == sel) HudSprite(c, ui, sp, "cursor/cursor", rx + 24, y + 20, 36, 40);
            HudText(c, ui, f, rx + rw / 2, y + 16, 48, rows[i], Ink, 1);
        }
    }
    if (v.messageShown)
    {
        HudPanel(c, ui, sp, "panels/dialogue_box", rx, 740, rw, 250);
        std::vector<std::string> lines;
        for (size_t i = 0; i < v.message.lines.size() && i < 2; i++)
            for (const std::string& l : HudWrap(ui, f, 40, v.message.lines[i], rw - 90)) lines.push_back(l);
        for (size_t i = 0; i < lines.size() && i < 4; i++) HudText(c, ui, f, rx + 44, 776 + 52 * i, 40, lines[i], Ink);
        if (v.message.more) HudSprite(c, ui, sp, "icons/icon_arrow_down", rx + rw - 76, 930, 40, 40, 0xFF6F7BF7);
    }
    DrawHintBar(c, ui, sp, f, hints);
}

// ---- The Ruins of Alph's sliding puzzles (src/alph_puzzle.c) ----
// The game plays them by touch only: a tap turns a piece, dragging moves it, Quit is in the
// corner. Here the d-pad moves a cursor over the 6 x 6 board, A picks a piece up and puts it
// down where the cursor is, X (or Y, L, R) turns it and B quits (or puts a carried piece
// back); each is given to the game as the touches it expects. The picture: the board as the
// game draws it (its pixels, so the pieces are the game's own), framed in the HUD style.

struct Alph
{
    int step = 0;   // AlphPuzzle_Main's state: 1 waiting, 2 picking up, 3 holding, 4 turning, 5 quit?, 6 solved
    int puzzle = 0; // Kabuto, Aerodactyl, Omanyte, Ho-Oh
    int x[16] = {}, y[16] = {}, rot[16] = {};
    bool fixed[16] = {};
    int Placed() const
    {
        int n = 0;
        for (int i = 0; i < 16; i++) n += rot[i] == 0 && x[i] == i % 4 + 1 && y[i] == i / 4 + 1;
        return n;
    }
    // The piece at a place of the board, or -1.
    int At(int cx, int cy) const
    {
        for (int i = 0; i < 16; i++)
            if (x[i] == cx && y[i] == cy) return i;
        return -1;
    }
};

// AlphPuzzleData (the OverlayManager's data): the puzzle +0x19, the pieces from +0xDC (8
// bytes each: x, y, rotation, fixed, the sprite). The step is the OverlayManager's (+0x14)
// while its main function runs (+0x10 = 2).
inline bool ReadAlph(const Ram& r, uint32_t man, Alph& a)
{
    const uint32_t d = r.R32(man + 0x1C);
    if (!r.Ptr(d) || r.R32(man + 0x10) != 2) return false;
    a.step = (int)r.R32(man + 0x14);
    a.puzzle = (int)r.R8(d + 0x19);
    if (a.step > 8 || a.puzzle > 3) return false;
    for (int i = 0; i < 16; i++)
    {
        const uint32_t t = d + 0xDC + 8 * i;
        a.x[i] = (int)r.R8(t), a.y[i] = (int)r.R8(t + 1), a.rot[i] = (int)r.R8(t + 2);
        a.fixed[i] = r.R8(t + 3) != 0;
        if (a.x[i] > 5 || a.y[i] > 5 || a.rot[i] > 3 || r.R8(t + 3) > 1 || !r.Ptr(r.R32(t + 4))) return false;
    }
    return true;
}

// The controller's side of it, kept from frame to frame.
struct AlphPad
{
    int cx = 1, cy = 1; // the cursor, on the board's 6 x 6 places
    int phase = 0;      // 0 free, 1 picking up, 2 carrying, 3 putting down, 4 turning, 5 Quit
    int timer = 0;
    int fromX = 0, fromY = 0;
    uint32_t prev = 0;
};

// DS key bits (as the frontend's `keys`).
constexpr uint32_t kA = 1u << 0, kB = 1u << 1, kRight = 1u << 4, kLeft = 1u << 5, kUp = 1u << 6, kDown = 1u << 7, kR = 1u << 8,
                   kL = 1u << 9, kX = 1u << 10, kY = 1u << 11;

// Turns this frame's buttons into the puzzle's touches. `keys` loses the buttons used here
// while the board takes input (the game ignores them there anyway; in the Quit question
// they go to the game). Returns whether it touches the screen, at tx, ty.
inline bool AlphInput(const Alph& a, AlphPad& p, uint32_t& keys, int& tx, int& ty)
{
    const uint32_t edge = keys & ~p.prev;
    p.prev = keys;
    auto centre = [&](int cx, int cy) { tx = 48 + 32 * cx, ty = 16 + 32 * cy; };
    const bool board = a.step >= 1 && a.step <= 4;
    if (!board && p.phase != 5) p.phase = 0;
    if (board || p.phase) keys &= ~(kA | kB | kX | kY | kL | kR | kUp | kDown | kLeft | kRight);
    auto move = [&]() {
        if (edge & kLeft) p.cx = std::max(0, p.cx - 1);
        if (edge & kRight) p.cx = std::min(5, p.cx + 1);
        if (edge & kUp) p.cy = std::max(0, p.cy - 1);
        if (edge & kDown) p.cy = std::min(5, p.cy + 1);
    };
    if (p.phase == 0)
    {
        if (a.step != 1) return false;
        move();
        const int here = a.At(p.cx, p.cy);
        const bool movable = here >= 0 && !a.fixed[here];
        if ((edge & kA) && movable) p.phase = 1, p.timer = 0, p.fromX = p.cx, p.fromY = p.cy;
        else if ((edge & (kX | kY | kL | kR)) && movable) p.phase = 4, p.timer = 0;
        else if (edge & kB) p.phase = 5, p.timer = 0;
        else return false;
        // (The touch starts this frame, below.)
    }
    switch (p.phase)
    {
    case 1: // held on the piece until the game holds it (3), or gives up
        centre(p.fromX, p.fromY);
        if (a.step == 3) p.phase = 2;
        else if (++p.timer > 30) { p.phase = 0; return false; }
        return true;
    case 2: // carried under the cursor
        if (a.step != 3) { p.phase = 0; return false; }
        move();
        if (edge & kB) p.cx = p.fromX, p.cy = p.fromY, p.phase = 3, p.timer = 0;
        else if (edge & kA) p.phase = 3, p.timer = 0;
        centre(p.cx, p.cy);
        return true;
    case 3: // a few more frames there, so that the game sees where, then let go
        centre(p.cx, p.cy);
        if (++p.timer <= 4) return true;
        if (a.step == 1) p.phase = 0;
        return false;
    case 4: // a tap: down for two frames, then wait for the turn to end
        centre(p.cx, p.cy);
        if (++p.timer <= 2) return true;
        if (a.step == 1 && p.timer > 4) p.phase = 0;
        return false;
    case 5: // a tap on Quit; the question that follows takes the buttons
        tx = 232, ty = 180;
        if (++p.timer <= 2) return true;
        if (a.step != 1 || p.timer > 20) p.phase = 0;
        return false;
    }
    return false;
}

// ---- The Battle Frontier's scratch-off cards (overlay 87, not decompiled) ----
// Touch only: three of four cards are chosen by touching them, then on each card three of
// its nine panels are scratched by rubbing them, NEXT CARD moves on and a yes/no asks
// whether to stop. Here: the d-pad chooses (a card, or a panel / NEXT CARD, or the answer)
// and A touches it; on a panel A rubs it clean, the stylus going over it row by row.
// Read from its OverlayManager: the step (+0x14) and, in its data, the step's own stage
// (+8) and the cards chosen so far (+0x0E, which ones from +0x15). Found by watching them.
struct Scratch
{
    int step = 0, stage = 0, chosen = 0;
    int picked[3] = { -1, -1, -1 };
};

inline bool ReadScratch(const Ram& r, uint32_t man, Scratch& s)
{
    const uint32_t d = r.R32(man + 0x1C);
    if (!r.Ptr(d) || r.R32(man + 0x10) != 2) return false;
    s.step = (int)r.R32(man + 0x14);
    s.stage = (int)r.R8(d + 8);
    s.chosen = (int)r.R8(d + 0x0E);
    if (s.step > 7 || s.chosen > 3) return false;
    for (int i = 0; i < 3; i++) s.picked[i] = i < s.chosen ? (int)r.R8(d + 0x15 + i) : -1;
    return true;
}

struct ScratchPad
{
    int card = 0;  // choosing: 0-3
    int spot = 0;  // scratching: 0-8 the panels, 9 NEXT CARD
    int answer = 0; // the question: 0 yes, 1 no
    int rub = -1;  // frames into rubbing a panel, or -1
    int tap = 0, tapX = 0, tapY = 0;
    uint32_t prev = 0;
    // Where it is on the touch screen (for the cursor), and how big.
    int curX = 0, curY = 0, curW = 0, curH = 0;
    bool shown = false;
};

// The touch screen's places: the cards (their hitboxes' middles), the panels and NEXT CARD,
// and the question's YES and NO.
constexpr int kCardX[4] = { 40, 113, 169, 229 }, kCardY = 98;
constexpr int kPanelX[3] = { 60, 128, 195 }, kPanelY[3] = { 36, 82, 127 }, kPanelW = 50, kPanelH = 37;

inline bool ScratchInput(const Scratch& s, ScratchPad& p, uint32_t& keys, int& tx, int& ty)
{
    const uint32_t edge = keys & ~p.prev;
    p.prev = keys;
    const bool choosing = s.step == 2 && s.stage == 1, scratching = s.step == 5 && s.stage == 3, asking = s.step == 5 && s.stage == 9;
    p.shown = false;
    if (p.tap > 0)
    {
        p.tap--;
        tx = p.tapX, ty = p.tapY;
        keys &= ~(kA | kB | kUp | kDown | kLeft | kRight);
        return true;
    }
    if (p.rub >= 0)
    {
        // Row by row over the panel, 5 pixels apart (the scratch is about 6 wide).
        const int cols = kPanelW / 5 + 1, rows = kPanelH / 5 + 1;
        const int k = p.rub++, row = k / cols, col = row % 2 ? cols - 1 - k % cols : k % cols;
        keys &= ~(kA | kB | kUp | kDown | kLeft | kRight);
        if (row >= rows || !scratching) { p.rub = -1; return false; }
        tx = kPanelX[p.spot % 3] - kPanelW / 2 + col * 5;
        ty = kPanelY[p.spot / 3] - kPanelH / 2 + row * 5;
        p.shown = true;
        p.curX = kPanelX[p.spot % 3] - kPanelW / 2, p.curY = kPanelY[p.spot / 3] - kPanelH / 2, p.curW = kPanelW, p.curH = kPanelH;
        return true;
    }
    auto start = [&](int x, int y) { p.tap = 5, p.tapX = x, p.tapY = y; };
    if (choosing)
    {
        auto taken = [&](int c) { return c == s.picked[0] || c == s.picked[1] || c == s.picked[2]; };
        if (taken(p.card))
            for (int c = 0; c < 4; c++)
                if (!taken(c)) { p.card = c; break; }
        for (int dir : { (edge & kRight) ? 1 : 0, (edge & kLeft) ? -1 : 0 })
            if (dir)
                for (int c = p.card + dir; c >= 0 && c < 4; c += dir)
                    if (!taken(c)) { p.card = c; break; }
        if (edge & kA) start(kCardX[p.card], kCardY);
        keys &= ~(kA | kUp | kDown | kLeft | kRight);
        p.shown = true;
        const int x0 = p.card ? kCardX[p.card] - 28 : 4;
        p.curX = x0, p.curY = 42, p.curW = 56, p.curH = 112;
    }
    else if (scratching)
    {
        if (p.spot < 9)
        {
            const int col = p.spot % 3, row = p.spot / 3;
            if ((edge & kLeft) && col > 0) p.spot--;
            if ((edge & kRight) && col < 2) p.spot++;
            if ((edge & kUp) && row > 0) p.spot -= 3;
            if (edge & kDown) p.spot = row < 2 ? p.spot + 3 : 9;
        }
        else if (edge & kUp) p.spot = 7;
        if (edge & kA)
        {
            if (p.spot == 9) start(128, 180);
            else p.rub = 0;
        }
        keys &= ~(kA | kUp | kDown | kLeft | kRight);
        p.shown = true;
        if (p.spot == 9) p.curX = 88, p.curY = 170, p.curW = 80, p.curH = 20;
        else p.curX = kPanelX[p.spot % 3] - kPanelW / 2, p.curY = kPanelY[p.spot / 3] - kPanelH / 2, p.curW = kPanelW, p.curH = kPanelH;
    }
    else if (asking)
    {
        if (edge & (kUp | kDown)) p.answer ^= 1;
        if (edge & kA) start(216, p.answer ? 112 : 81);
        else if (edge & kB) start(216, 112);
        keys &= ~(kA | kB | kUp | kDown | kLeft | kRight);
        p.shown = true;
        p.curX = 194, p.curY = (p.answer ? 112 : 81) - 11, p.curW = 44, p.curH = 22;
    }
    else
        p.answer = 0;
    if (p.tap > 0) return ScratchInput(s, p, keys, tx, ty);
    return false;
}

inline const char* AlphName(int puzzle)
{
    static const char* names[4] = { "Kabuto", "Aerodactyl", "Omanyte", "Ho-Oh" };
    return names[puzzle & 3];
}

// The redrawn puzzle: `bottom` is the touch screen's picture (256 x 192, xRGB).
inline void DrawAlph(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Alph& a, const AlphPad& p, const uint32_t* bottom,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    using HudParty::FillRound;
    DrawBackdrop(c, sp, "backgrounds/bg_sunny", 0xFFFFFFFF);
    const float wd = ui.W / ui.s, x0 = wd / 2 - 960;
    // The board: places 32 pixels apart from x 32, 192 x 192 of the screen.
    static Image board;
    board.w = board.h = 192;
    board.px.resize(192 * 192);
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 192; x++)
        {
            // (The Quit button reaches into the board's lower right corner, where no piece can
            // go: the lower left corner instead, mirrored.)
            const int sx = y >= 160 && x >= 160 ? 191 - x : x;
            board.px[y * 192 + x] = 0xFF000000 | bottom[y * 256 + 32 + sx];
        }
    const float bs = 864, bx = x0 + 140, by = 56, cell = bs / 6;
    FillRound(c, ui, bx - 24, by - 24, bs + 48, bs + 48, 36, 0xF0FFFFFF);
    HudPixelArt(c, ui, &board, bx, by, bs, bs);
    if (a.step >= 1 && a.step <= 4)
    {
        const float x = bx + p.cx * cell, y = by + p.cy * cell;
        const uint32_t ring = p.phase == 2 || p.phase == 3 ? 0xFFFFC23A : 0xFFFFFFFF;
        for (int k = 0; k < 2; k++)
        {
            const uint32_t col = k ? ring : 0xFF3B3F4A;
            const float o = k ? 4 : 8, t = k ? 6 : 14;
            FillBlend(c, ui(x - o), ui(y - o), ui(cell + 2 * o), ui(t), col);
            FillBlend(c, ui(x - o), ui(y + cell + o - t), ui(cell + 2 * o), ui(t), col);
            FillBlend(c, ui(x - o), ui(y - o), ui(t), ui(cell + 2 * o), col);
            FillBlend(c, ui(x + cell + o - t), ui(y - o), ui(t), ui(cell + 2 * o), col);
        }
    }
    // The right: which puzzle and how far along.
    const float rx = x0 + 1100, rw = 740;
    HudText(c, ui, f, rx, 52, 48, "Ruins of Alph", Ink, 0, true);
    HudPanel(c, ui, sp, "panels/card", rx, 130, rw, 260);
    HudText(c, ui, f, rx + 36, 156, 32, "Puzzle", Muted);
    HudText(c, ui, f, rx + rw - 36, 150, 48, AlphName(a.puzzle), Ink, 2, true);
    FillBlend(c, ui(rx + 36), ui(226), ui(rw - 72), ui(2), 0xFFE3E9F1);
    HudText(c, ui, f, rx + 36, 252, 32, "Pieces in place", Muted);
    HudText(c, ui, f, rx + rw - 36, 246, 48, std::to_string(a.Placed()) + " / 16", Ink, 2, true);
    const float bar = (rw - 72) * a.Placed() / 16.0f;
    FillRound(c, ui, rx + 36, 320, rw - 72, 24, 12, 0xFFE3E9F1);
    if (bar > 0) FillRound(c, ui, rx + 36, 320, std::max(24.0f, bar), 24, 12, 0xFF4FB35E);
    HudPanel(c, ui, sp, "panels/dialogue_box", rx, 420, rw, 250);
    const char* say = a.step == 6 ? "The puzzle is complete!"
        : p.phase == 2 || p.phase == 3 ? "Move the piece where it belongs, then put it down."
                                       : "Slide the pieces into the middle and turn them to make the picture.";
    const std::vector<std::string> lines = HudWrap(ui, f, 40, say, rw - 90);
    for (size_t i = 0; i < lines.size() && i < 4; i++) HudText(c, ui, f, rx + 44, 456 + 52 * i, 40, lines[i], Ink);
    DrawHintBar(c, ui, sp, f, hints);
}
}
