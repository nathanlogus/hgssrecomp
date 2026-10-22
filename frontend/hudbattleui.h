#pragma once
// The rest of a battle's top screen redrawn in the HUD style (hudbattle.h has the touch
// screen's menus), on a wide view where battlewide.h builds the picture:
//  - the HP boxes: a card per Pokémon with its name, gender, level, status, HP bar (and for
//    the player's, the HP in numbers and the EXP bar), sliding in and out and bobbing as the
//    game moves its own boxes, the bars running down and filling as the game animates them;
//  - the message box: the band under the field that the menus use, with a dialogue box
//    showing what the game has printed in its own box so far, letter by letter, scrolling
//    and with its arrow, as the game draws it (copied from the box's pixels);
//  - the stats shown after a level up, in a card.
// The game's own boxes are left out of the picture (BattleWide::HideBoxes) or covered.
// Everything is read from the game's memory every frame (pokeheartgold names below), and
// only plain wild and trainer battles are redrawn (the same as hudbattle.h's menus).
#include <cstdio>
#include <functional>
#include "hudbattle.h"
#include "hudparty.h"
#include "merged.h"

namespace HudBattleUi
{
struct Mem
{
    const uint8_t* ram;
    uint32_t mask;
    static bool In(uint32_t a) { return a >= 0x02000000 && a < 0x02400000; }
    uint32_t r32(uint32_t a) const { return In(a) ? *(const uint32_t*)&ram[a & mask & ~3u] : 0; }
    uint32_t r16(uint32_t a) const { return In(a) ? *(const uint16_t*)&ram[a & mask & ~1u] : 0; }
    uint32_t r8(uint32_t a) const { return In(a) ? ram[a & mask] : 0; }
};

// The battle's BattleSystem (FieldSystem -> process manager -> the battle application's
// data), if a plain single or double wild or trainer battle runs; else 0.
inline uint32_t Battle(const Mem& m)
{
    const uint32_t fs = m.r32(0x021D4158), pm = m.r32(fs), app = m.r32(pm + 4);
    if (!Mem::In(fs) || !Mem::In(pm) || !Mem::In(app) || m.r32(app + 0xC) != 12) return 0;
    const uint32_t bs = m.r32(app + 0x1C);
    if (!Mem::In(bs) || !Mem::In(m.r32(bs + 0x30))) return 0;
    const uint32_t type = m.r32(bs + 0x2C), count = m.r32(bs + 0x44);
    if ((type & ~0x143u) != 0 || count != ((type & 2) ? 4u : 2u)) return 0;
    return bs;
}

// The top screen's background graphics as the game has put them in video memory: `bgCnt`
// of each background, and a byte of the backgrounds' memory (as the 2D engine addresses it,
// the display control's character base included).
struct Vram
{
    uint16_t bgCnt[4] = {};
    uint32_t charOffset = 0;
    std::function<uint8_t(uint32_t)> read8;
};

// A text window of the game (bg_window.h Window: its BG, place and size in tiles, the
// first of its tiles), with the colour index of every pixel as the screen shows it (its
// tiles in video memory: the game's own copy of them is sometimes out of step).
struct Window
{
    int bg = -1, left = 0, top = 0, w = 0, h = 0; // w, h in pixels
    std::vector<uint8_t> px;
    bool Read(const Mem& m, uint32_t a, const Vram& v)
    {
        bg = (int)m.r8(a + 4);
        left = (int)m.r8(a + 5) * 8;
        top = (int)m.r8(a + 6) * 8;
        const int tw = (int)m.r8(a + 7), th = (int)m.r8(a + 8);
        const uint32_t tile = m.r16(a + 10) & 0x7FFF;
        w = tw * 8;
        h = th * 8;
        if (!Mem::In(m.r32(a)) || !Mem::In(m.r32(a + 12)) || bg > 7 || tw == 0 || th == 0 || tw > 32 || th > 24 || !v.read8 || (v.bgCnt[bg & 3] & 0x80))
            return false;
        const uint32_t base = v.charOffset + ((v.bgCnt[bg & 3] >> 2) & 0xF) * 0x4000 + tile * 32;
        px.resize((size_t)w * h);
        for (int ty = 0; ty < th; ty++)
            for (int tx = 0; tx < tw; tx++)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x += 2)
                    {
                        const uint8_t b = v.read8(base + (ty * tw + tx) * 32 + y * 4 + x / 2);
                        uint8_t* p = &px[(size_t)(ty * 8 + y) * w + tx * 8 + x];
                        p[0] = b & 0xF;
                        p[1] = b >> 4;
                    }
        return true;
    }
};

// The text in a window, read back from its pixels with the game's own fonts, so that it can
// be written in the HUD's font. The game prints a whole letter at a time, so every pixel of
// its letters (colour 1) is explained by letters of the font at their places, or the text
// is not read. Lines are 16 pixels apart (`scroll`: how far they have moved up as the box
// scrolls); a line is cut into pieces where the gap is wider than a few spaces (the stats'
// columns).
struct TextLine
{
    int y = 0; // the line's top in the window
    struct Piece
    {
        int x = 0;
        std::string text;
    };
    std::vector<Piece> pieces;
};

class WindowReader
{
public:
    void SetRom(const uint8_t* rom, size_t size)
    {
        const auto files = NdsNarcFiles(rom, size, "pbr/font.narc");
        for (int member = 0; member < 2 && member < (int)files.size(); member++)
        {
            const uint8_t* d = rom + files[member].first;
            const uint32_t n = files[member].second;
            if (n < 16) continue;
            const uint32_t widths = RomGet32(d + 4), count = RomGet16(d + 8);
            if (widths < 16 || widths + count > n || 16 + count * 64 > widths) continue;
            Font& f = fonts[nFonts++];
            for (uint32_t g = 0; g < count; g++)
            {
                const uint32_t code = g + 1;
                if (code < 0x121 || code > 0x1DE) continue;
                Glyph gl;
                gl.text = NdsCharToUtf8(code);
                gl.adv = std::max(1, (int)d[widths + g]);
                const uint8_t* cell = d + 16 + g * 64;
                for (int t = 0; t < 4; t++)
                    for (int ty = 0; ty < 8; ty++)
                    {
                        const uint32_t row = cell[t * 16 + ty * 2] | cell[t * 16 + ty * 2 + 1] << 8;
                        for (int tx = 0; tx < 8; tx++)
                            if (((row >> ((7 - tx) * 2)) & 3) == 1)
                            {
                                gl.col[(t & 1) * 8 + tx] |= (uint16_t)(1 << ((t >> 1) * 8 + ty));
                                gl.ink++;
                            }
                    }
                if (code == 0x1DE) f.space = gl.adv;
                if (gl.text.empty() || !gl.ink) continue;
                gl.first = 0;
                while (!gl.col[gl.first]) gl.first++;
                gl.span = 16;
                while (!gl.col[gl.span - 1]) gl.span--;
                f.glyphs.push_back(gl);
            }
        }
    }

    // The window's text, or false if some of its pixels are not letters of either font.
    bool Read(const Window& w, std::vector<TextLine>& out) const
    {
        for (int fi = 0; fi < nFonts; fi++)
            for (int scroll = 0; scroll < 16; scroll++)
                if (ReadWith(fonts[fi], w, scroll, out)) return true;
        return false;
    }

private:
    struct Glyph
    {
        std::string text;
        uint16_t col[16] = {}; // ink, column by column (bit: row)
        int adv = 0, ink = 0, first = 0, span = 0;
    };
    struct Font
    {
        std::vector<Glyph> glyphs;
        int space = 4;
    };
    Font fonts[2];
    int nFonts = 0;

    bool ReadWith(const Font& f, const Window& w, int scroll, std::vector<TextLine>& out) const
    {
        out.clear();
        std::vector<uint16_t> col(w.w + 16), rem;
        for (int ly = -scroll; ly < w.h; ly += 16)
        {
            // The ink of the line, column by column, and which of its rows are in the window.
            uint16_t vis = 0;
            for (int r = 0; r < 16; r++)
                if (ly + r >= 0 && ly + r < w.h) vis |= (uint16_t)(1 << r);
            std::fill(col.begin(), col.end(), 0);
            bool any = false;
            for (int r = 0; r < 16; r++)
                if (vis >> r & 1)
                    for (int x = 0; x < w.w; x++)
                        if (w.px[(size_t)(ly + r) * w.w + x] == 1)
                        {
                            col[x] |= (uint16_t)(1 << r);
                            any = true;
                        }
            if (!any) continue;
            rem = col;
            TextLine line;
            line.y = ly;
            int pen = 0;
            while (true)
            {
                int c = pen;
                while (c < w.w && !rem[c]) c++;
                if (c >= w.w) break;
                // The letter whose ink is all there, and that explains all the ink in its
                // width; the one with the most ink if several do.
                const Glyph* best = nullptr;
                int bestAt = 0;
                for (const Glyph& g : f.glyphs)
                {
                    const int p = c - g.first;
                    if (p < pen || p + g.span > w.w + 16) continue;
                    bool ok = (g.col[g.first] & vis) != 0;
                    for (int x = 0; ok && x < std::max(g.adv, g.span); x++)
                    {
                        const uint16_t gm = x < 16 ? g.col[x] & vis : 0;
                        if ((gm & ~rem[p + x]) || (x < g.adv && (rem[p + x] & ~gm))) ok = false;
                    }
                    if (ok && (!best || g.ink > best->ink))
                    {
                        best = &g;
                        bestAt = p;
                    }
                }
                if (!best) return false;
                const int gap = bestAt - pen;
                if (line.pieces.empty() || gap > 3 * f.space)
                    line.pieces.push_back({ bestAt, "" });
                else
                    for (int k = 0; k < (gap + f.space / 2) / f.space; k++) line.pieces.back().text += ' ';
                line.pieces.back().text += best->text;
                for (int x = 0; x < best->span; x++) rem[bestAt + x] &= (uint16_t)~best->col[x];
                pen = bestAt + best->adv;
            }
            out.push_back(line);
        }
        return true;
    }
};

// An HP box (BattleHpBar, in each battler's OpponentData at +0x28).
struct Box
{
    bool shown = false;
    int type = 0;       // 0 single player, 1 single foe, 2/4 double player left/right, 3/5 double foe
    int dx = 0, dy = 0; // where the game's box is against where it rests (sliding in or out, bobbing)
    int species = 0, level = 0, gender = 2, status = 0, hp = 0, maxHp = 1;
    bool caught = false;
    bool shiny = false; // BattleMon.shiny, which the game works out when the battle starts
    float exp = -1; // the EXP bar, 0-1 (the player's)
    int fadeMode = 0, fadeF = 0; // how the game's box looks faded on the screen (see Look)
    std::string name;
    bool Player() const { return !(type & 1); }
    std::string Key() const
    {
        char b[96];
        snprintf(b, sizeof b, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%.3f|", type, species, level, gender, status, hp, maxHp, caught, shiny, exp);
        return b + name;
    }
};

struct State
{
    Box box[4];
    int count = 0;
    bool wild = false;
    Window msg, stats; // the message box; the stats after a level up (if shown)
    bool statsShown = false;
};

// Where the game's boxes rest (the box sprites' places in their templates).
constexpr int RestX[6] = { 0xC0, 0x3A, 0xC0, 0x40, 0xC6, 0x3A }, RestY[6] = { 0x74, 0x24, 0x67, 0x10, 0x84, 0x2D };

// The value a bar shows while the game runs it down or up (BattleHpBar_CalculatePixelsChangeFrame):
// `calc` is where it is, 0x80000000 before it starts and 0 once it has finished (when `cur`
// has become the new value); in 256ths when the bar is wider in pixels than `most`. At 0 it
// has only reached 0 (for a frame, before it is done) if it was running towards 0 a frame ago
// (`last`, the value shown then, was neither `cur`, 0 nor unknown).
inline int Shown(int cur, int gained, int calc, int most, int pixels, int last)
{
    if (calc == (int)0x80000000) return cur;
    const bool sub = most < pixels;
    if (calc == 0) return last > 0 && last != cur && cur - gained <= 0 ? 0 : cur;
    int v = sub ? calc >> 8 : calc;
    if (sub && gained > 0 && (calc & 0xFF)) v++;
    const int to = std::clamp(cur - gained, 0, most);
    return std::clamp(v, std::min(cur, to), std::max(cur, to));
}

class Reader
{
public:
    // Reads the boxes and windows; false when no plain battle runs.
    bool Read(const uint8_t* ram, uint32_t mask, const Vram& vram, State& s)
    {
        const Mem m{ ram, mask };
        const uint32_t bs = Battle(m);
        if (!bs)
        {
            for (int& v : lastHp) v = -1;
            return false;
        }
        const uint32_t ctx = m.r32(bs + 0x30);
        s.wild = !(m.r32(bs + 0x2C) & 1);
        s.count = (int)m.r32(bs + 0x44);
        for (int i = 0; i < s.count; i++)
        {
            Box& b = s.box[i];
            b = Box();
            const uint32_t od = m.r32(bs + 0x34 + 4 * i), h = od + 0x28, obj = m.r32(h + 4), spr = m.r32(obj);
            if (!Mem::In(od) || !Mem::In(obj) || !Mem::In(spr)) continue;
            b.type = (int)m.r8(h + 0x25);
            const int battler = (int)m.r8(h + 0x24);
            if (b.type > 5 || battler != i) continue;
            b.dx = ((int)m.r32(spr) >> 12) - RestX[b.type];
            b.dy = ((int)m.r32(spr + 4) >> 12) - RestY[b.type];
            b.maxHp = (int)m.r32(h + 0x2C);
            const int hp = (int)m.r32(h + 0x28);
            b.level = (int)m.r8(h + 0x48);
            b.gender = (int)m.r8(h + 0x49);
            b.status = (int)m.r8(h + 0x4A);
            b.caught = m.r8(h + 0x4B) == 1;
            if (b.maxHp <= 0 || b.maxHp > 999 || hp < 0 || hp > b.maxHp || b.level < 1 || b.level > 100 || b.status > 5) continue;
            b.hp = Shown(hp, (int)m.r32(h + 0x30), (int)m.r32(h + 0x34), b.maxHp, 48, lastHp[i]);
            lastHp[i] = b.hp;
            // (The EXP bar: only a single battle's box has one; in a double battle the game does not keep it.)
            if (b.type == 0)
            {
                const int most = (int)m.r32(h + 0x3C), exp = (int)m.r32(h + 0x38);
                if (most > 0 && exp >= 0 && exp <= most)
                    b.exp = (float)Shown(exp, (int)m.r32(h + 0x40), (int)m.r32(h + 0x44), most, 96, lastExp[i]) / most;
                lastExp[i] = (int)(b.exp * most + 0.5f);
            }
            // The name and species: the Pokémon in battle (BattleContext battleMons).
            const uint32_t mon = ctx + 0x2D40 + 0xC0 * i;
            b.species = (int)m.r16(mon);
            b.shiny = (m.r8(mon + 0x26) & 0x20) != 0;
            uint16_t nick[11];
            for (int k = 0; k < 11; k++) nick[k] = (uint16_t)m.r16(mon + 0x36 + 2 * k);
            b.name = NdsText(nick, 11);
            b.shown = m.r8(spr + 0x34) != 0 && b.species >= 1 && b.species <= 493 && !b.name.empty() && b.name.size() < 40 &&
                std::abs(b.dx) <= 256 && std::abs(b.dy) <= 32;
        }
        const uint32_t win = m.r32(bs + 8);
        if (!s.msg.Read(m, win, vram) || s.msg.bg != 1 || s.msg.w != 216 || s.msg.h != 32) s.msg.w = 0;
        s.statsShown = s.stats.Read(m, win + 16, vram) && s.stats.bg == 2 && s.stats.w == 112 && s.stats.h == 96;
        return true;
    }

private:
    int lastHp[4] = { -1, -1, -1, -1 }, lastExp[4] = { -1, -1, -1, -1 };
};

// The game's fade of the top screen (master brightness, or a brightness effect on the
// message box's layer): 0 none, 1 to white, 2 to black; and how far (0-16).
struct Fade
{
    int mode = 0, f = 0;
    uint32_t Apply(uint32_t c) const
    {
        if (!mode || !f) return c;
        uint32_t out = c & 0xFF000000;
        for (int sh = 0; sh < 24; sh += 8)
        {
            int v = (c >> sh) & 0xFF;
            v = mode == 1 ? v + (255 - v) * f / 16 : v - v * f / 16;
            out |= (uint32_t)v << sh;
        }
        return out;
    }
};

// Something drawn once into a canvas of its own and put on the screen each frame (moved,
// faded), until what it shows changes.
struct Piece
{
    Canvas c;
    std::string key;
    // `draw` draws it with its top left at design (0, 0), `w` x `h` canvas pixels.
    void Make(const std::string& k, int w, int h, const std::function<void(Canvas&)>& draw)
    {
        if (k == key && c.w == w && c.h == h) return;
        key = k;
        c.Resize(w, h);
        std::fill(c.px.begin(), c.px.end(), 0);
        draw(c);
    }
    void Put(Canvas& dst, int x, int y, const Fade& fade) const
    {
        const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(dst.w, x + c.w), y1 = std::min(dst.h, y + c.h);
        if (x1 <= x0 || y1 <= y0) return;
        dst.Mark(x0, y0, x1, y1);
        for (int yy = y0; yy < y1; yy++)
        {
            const uint32_t* s = &c.px[(size_t)(yy - y) * c.w + (x0 - x)];
            uint32_t* d = &dst.px[(size_t)yy * dst.w + x0];
            for (int xx = x0; xx < x1; xx++, s++, d++)
                if (*s >> 24) *d = BlendOver(*d, fade.Apply(*s));
        }
    }
};

// The pixels of a game window drawn as text: its letters (colour 1) in `ink`, `k` design
// pixels to one of the game's, with its top left at design (x, y). The letters' shadow (2),
// the background (15) and anything else are left out.
inline void WindowText(Canvas& c, const Ui& ui, const Window& w, int sx, int sy, int sw, int sh, float x, float y, float k, uint32_t ink)
{
    for (int yy = sy; yy < sy + sh && yy < w.h; yy++)
        for (int xx = sx; xx < sx + sw && xx < w.w; xx++)
        {
            const int v = w.px[(size_t)yy * w.w + xx];
            if (v != 1) continue;
            const float x0 = std::round(ui(x + (xx - sx) * k)), y0 = std::round(ui(y + (yy - sy) * k));
            const float x1 = std::round(ui(x + (xx - sx + 1) * k)), y1 = std::round(ui(y + (yy - sy + 1) * k));
            FillBlend(c, x0, y0, x1 - x0, y1 - y0, ink);
        }
}

// A small Poké Ball (a caught species), `s` design pixels across.
inline void Ball(Canvas& c, const Ui& ui, float x, float y, float s)
{
    HudParty::FillRound(c, ui, x, y, s, s, s / 2, 0xFF3B3F4A);
    HudParty::FillRound(c, ui, x + 2, y + 2, s - 4, s / 2 - 2, (s - 4) / 2, 0xFFF0505A);
    HudParty::FillRound(c, ui, x + 2, y + s / 2 + 1, s - 4, s / 2 - 3, (s - 4) / 2, 0xFFFFFFFF);
    HudParty::FillRound(c, ui, x + 2, y + s / 2 - 1.5f, s - 4, 3, 1.5f, 0xFF3B3F4A);
    HudParty::FillRound(c, ui, x + s / 2 - 5, y + s / 2 - 5, 10, 10, 5, 0xFF3B3F4A);
    HudParty::FillRound(c, ui, x + s / 2 - 3, y + s / 2 - 3, 6, 6, 3, 0xFFFFFFFF);
}

// A box's size in design pixels.
inline void BoxSize(const Box& b, bool doubles, float& w, float& h)
{
    w = doubles ? 460 : 500;
    h = b.Player() && !doubles ? 150 : 108;
}

// An HP box with its top left at design (0, 0) (its shadow outside).
inline void DrawBox(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Box& b, bool doubles, bool wild, float ox, float oy)
{
    using namespace HudColour;
    float w, h;
    BoxSize(b, doubles, w, h);
    const float x = ox, y = oy;
    HudPanel(c, ui, sp, "panels/card", x, y, w, h);
    // The name, the gender and the level; the status before the level.
    const float px = 40;
    HudText(c, ui, f, x + 28, y + 16, px, b.name, Ink);
    const float nw = HudTextWidth(ui, f, px, b.name);
    HudParty::Gender(c, ui, x + 36 + nw, y + 22, b.gender, 28);
    if (b.shiny) HudParty::Star(c, ui, x + 36 + nw + (b.gender < 2 ? 28 + 24 : 14), y + 36, 13);
    float right = x + w - 26;
    const std::string lv = "Lv " + std::to_string(b.level);
    HudText(c, ui, f, right, y + 22, 32, lv, Ink, 2);
    right -= HudTextWidth(ui, f, 32, lv) + 14;
    static const char* st[6] = { "", "SLP", "PSN", "BRN", "FRZ", "PAR" };
    static const uint32_t stc[6] = { 0, 0xFF8E6BE8, 0xFFA040A0, 0xFFF0505A, 0xFF3E8EF0, 0xFFE8B400 };
    if (b.hp == 0)
    {
        const float pw = HudTextWidth(ui, f, 28, "FNT") + 30;
        HudParty::Pill(c, ui, f, right - pw, y + 20, 38, "FNT", 0xFF8A93A3, White, 28);
        right -= pw + 10;
    }
    else if (b.status)
    {
        const float pw = HudTextWidth(ui, f, 28, st[b.status]) + 30;
        HudParty::Pill(c, ui, f, right - pw, y + 20, 38, st[b.status], stc[b.status], White, 28);
        right -= pw + 10;
    }
    if (wild && !b.Player() && b.caught) Ball(c, ui, right - 32, y + 22, 32);
    // HP: a bar, and for the player's the numbers.
    const float by = y + 70;
    HudText(c, ui, f, x + 28, by - 6, 32, "HP", Muted);
    const float barX = x + 80, barW = w - 80 - (b.Player() ? 160 : 28);
    HudParty::Bar(c, ui, sp, barX, by, barW, (float)b.hp / b.maxHp, HudParty::HpFill(b.hp, b.maxHp));
    if (b.Player())
    {
        HudText(c, ui, f, x + w - 26, by - 6, 32, std::to_string(b.hp) + "/" + std::to_string(b.maxHp), b.hp * 5 <= b.maxHp ? Danger : Ink, 2);
        if (b.exp >= 0)
        {
            // The EXP bar: thin, along the bottom.
            const float ey = y + h - (doubles ? 30 : 36), ew = w - 108;
            HudText(c, ui, f, x + 28, ey - 9, 24, "EXP", Muted);
            HudParty::FillRound(c, ui, x + 80, ey, ew, 12, 6, 0xFFDCE3EC);
            if (b.exp > 0) HudParty::FillRound(c, ui, x + 80, ey, std::max(12.0f, ew * std::min(b.exp, 1.0f)), 12, 6, 0xFF3E8EF0);
        }
    }
}

// Draws the HP boxes, the message band and the level-up stats. `left`, `right`: the view's
// edges in canvas pixels; `y(ds)`: a row of the screen in canvas pixels; `unit`: canvas
// pixels to a screen pixel; `top`: the band's top (design pixels). `band`: the message box
// is shown (the touch screen's menus do not have the band).
class Hud
{
public:
    void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const State& s, float left, float right, float originY, float unit,
        float top, bool band, const Fade& fade)
    {
        const bool doubles = s.count == 4;
        // The design pixels of the screen's rows and columns.
        const float du = unit / ui.s, dl = left / ui.s, dr = right / ui.s, dy0 = originY / ui.s;
        // Where each box rests: the foes' at the left, the player's at the right above the band.
        static const float restY[6] = { 112, 36, 92, 17, 120, 45 };
        for (int i = 0; i < s.count; i++)
        {
            const Box& b = s.box[i];
            if (!b.shown) continue;
            float w, h;
            BoxSize(b, doubles, w, h);
            float bx = b.Player() ? dr - 40 - w : dl + 40, by = dy0 + restY[b.type] * du - h / 2;
            bx += b.dx * du;
            by += b.dy * du;
            if (b.Player()) by = std::min(by, top - 22 - h); // above the band
            const int m = (int)std::ceil(ui(16)); // room for the shadow
            piece[i].Make(b.Key() + (doubles ? "d" : "") + (s.wild ? "w" : "") + std::to_string(ui.s), (int)std::ceil(ui(w)) + 2 * m, (int)std::ceil(ui(h)) + 2 * m,
                [&](Canvas& pc) { DrawBox(pc, ui, sp, f, b, doubles, s.wild, m / ui.s, m / ui.s); });
            piece[i].Put(c, (int)std::lround(ui(bx)) - m, (int)std::lround(ui(by)) - m, Fade{ b.fadeMode, b.fadeF });
        }
        if (s.statsShown)
        {
            // The stats after a level up, in a card over the game's window (from 128 to the
            // right edge of the screen, from 48 down; its lower part is behind the band).
            auto col = [&](float sx) { return dl + (sx + ((dr - dl) / du - 256) / 2) * du; };
            const float x = col(126), y = dy0 + 46 * du, w = col(258) - x, h = top - 6 - y;
            const float k = std::clamp(std::floor((h - 40) / 96), 2.0f, 4.0f);
            const int m = (int)std::ceil(ui(16));
            std::string key = std::to_string(w) + "," + std::to_string(h) + "," + std::to_string(ui.s) + "|";
            key.append(s.stats.px.begin(), s.stats.px.end());
            if (w > 112 * k && h > 96 * k)
            {
                stats.Make(key, (int)std::ceil(ui(w)) + 2 * m, (int)std::ceil(ui(h)) + 2 * m, [&](Canvas& pc) {
                    const float o = m / ui.s;
                    HudPanel(pc, ui, sp, "panels/card", o, o, w, h);
                    Text(pc, ui, f, s.stats, o + (w - 112 * k) / 2, o + (h - 96 * k) / 2, k);
                });
                stats.Put(c, (int)std::lround(ui(x)) - m, (int)std::lround(ui(y)) - m, fade);
            }
        }
        if (band && s.msg.w)
        {
            const float wd = ui.W / ui.s;
            std::string key = std::to_string(c.w) + "x" + std::to_string(c.h) + "," + std::to_string(top) + "|";
            key.append(s.msg.px.begin(), s.msg.px.end());
            const int y0 = std::clamp((int)ui(top), 0, c.h);
            msg.Make(key, c.w, c.h - y0, [&](Canvas& pc) {
                // As hudbattle.h's band, with the dialogue box across it.
                FillBlend(pc, 0, 0, (float)pc.w, (float)pc.h, 0xFFEAF4FA);
                FillBlend(pc, 0, 0, (float)pc.w, ui(4), 0xFFFFFFFF);
                // The hint bar's strip, empty (as DrawHintBar).
                const float hb = 1080 - 56 - top;
                HudSprite(pc, ui, sp, "panels/hint_bar", 0, hb - 3, wd, 59);
                FillBlend(pc, 0, ui(hb - 3), (float)pc.w, ui(3), 0xFFE3E9F1);
                const float bx = 40, by = 30, bw = wd - 80, bh = 190;
                HudPanel(pc, ui, sp, "panels/dialogue_box", bx, by, bw, bh);
                // The game's two lines (16 pixels each) at the size of the menus' question.
                const float k = 3;
                Text(pc, ui, f, s.msg, bx + 40, by + 34, k);
            });
            msg.Put(c, 0, y0, fade);
        }
    }

private:
    Piece piece[4], stats, msg;
    WindowReader reader;
    std::vector<TextLine> lines;

    // A window's text at design (x, y), `k` design pixels to one of the game's: in the HUD's
    // font where it can be read (lines moving out of the box as it scrolls are left out),
    // else its pixels.
    void Text(Canvas& c, const Ui& ui, const RomFont& f, const Window& w, float x, float y, float k)
    {
        if (!reader.Read(w, lines))
        {
            WindowText(c, ui, w, 0, 0, w.w, w.h, x, y, k, HudColour::Ink);
            return;
        }
        for (const TextLine& l : lines)
            if (l.y >= 0 && l.y + 16 <= w.h)
                for (const TextLine::Piece& p : l.pieces) HudText(c, ui, f, x + p.x * k, y + l.y * k, 16 * k, p.text, HudColour::Ink);
    }

public:
    void SetRom(const uint8_t* rom, size_t size) { reader.SetRom(rom, size); }
    const WindowReader& Words() const { return reader; }
};

// How dark the screen has gone, from the message box's background (white) as the screen
// shows it: the game darkens the screen in more ways than the registers read in main.cpp
// say (at the start of a battle). Only when that pixel is grey; else `fade` stays.
inline void FadeFromScreen(const Window& w, const uint32_t* pixels, Fade& fade)
{
    if (!w.w || !pixels || (fade.mode == 2 && fade.f)) return;
    for (int y = 0; y < w.h; y++)
        for (int x = 0; x < w.w; x++)
        {
            if (w.px[(size_t)y * w.w + x] != 15) continue;
            const int sx = w.left + x, sy = w.top + y;
            if (sx > 255 || sy > 191) return;
            const uint32_t c = pixels[sy * 256 + sx];
            const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            if (std::max({ r, g, b }) - std::min({ r, g, b }) > 24 || g >= 232) return;
            fade.mode = 2;
            fade.f = std::clamp((255 - g) * 16 / 248, 0, 16);
            return;
        }
}

// How the game's own HP boxes look on the screen just drawn (`pixels`, with each pixel's
// layer and sprite priority): a box with none of its pixels there is hidden (the game hides
// them in several ways), and one whose darkest pixel (its letters) has gone light or whose
// lightest has gone dark is fading (the game fades their palette, at the end of a battle).
inline void Look(State& s, const uint32_t* pixels, const uint8_t* flags, const uint8_t* objPrio)
{
    if (!pixels || !flags || !objPrio) return;
    for (int i = 0; i < s.count; i++)
    {
        Box& b = s.box[i];
        if (!b.shown) continue;
        // Around where the box is now, wide enough for a frame of its sliding.
        const int cx = RestX[b.type] + b.dx, cy = RestY[b.type] + b.dy;
        int lo = 255, hi = 0, n = 0;
        for (int y = std::max(0, cy - 14); y < std::min(192, cy + 14); y++)
            for (int x = std::max(0, cx - 90); x < std::min(256, cx + 90); x++)
            {
                const int k = y * 256 + x;
                if (LayerClass(flags[k]) != LAYER_OBJ || objPrio[k] != 0) continue;
                const uint32_t c = pixels[k];
                const int l = (int)((((c >> 16) & 0xFF) * 3 + ((c >> 8) & 0xFF) * 6 + (c & 0xFF)) / 10);
                lo = std::min(lo, l);
                hi = std::max(hi, l);
                n++;
            }
        if (n < 16)
        {
            b.shown = false;
            continue;
        }
        b.fadeMode = b.fadeF = 0;
        if (lo > 110)
        {
            b.fadeMode = 1;
            b.fadeF = std::clamp((lo - 70) * 16 / 185, 0, 16);
        }
        else if (hi < 200)
        {
            b.fadeMode = 2;
            b.fadeF = std::clamp((248 - hi) * 16 / 248, 0, 16);
        }
    }
}

// Whether the game's message box is on the screen: its window's layer shows at its left
// end (the stats after a level up cover its right end). `flags`: the top screen's layer of
// each pixel (merged.h).
inline bool MessageShown(const Window& w, const uint8_t* flags)
{
    if (!w.w || !flags) return false;
    const uint8_t want = (uint8_t)(LAYER_BG0 << w.bg);
    const int pts[4][2] = { { w.left + 2, w.top + 2 }, { w.left + 60, w.top + 2 }, { w.left + 2, w.top + w.h - 3 }, { w.left + 60, w.top + w.h - 3 } };
    for (const auto& p : pts)
        if (p[0] < 0 || p[0] > 255 || p[1] < 0 || p[1] > 191 || LayerClass(flags[p[1] * 256 + p[0]]) != want) return false;
    return true;
}
}
