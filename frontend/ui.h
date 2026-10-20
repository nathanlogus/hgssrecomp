// Software-drawn overlay: pause menu, touch cursor and status messages.
// Drawn into an ARGB buffer the size of the output, so the window and the
// headless screenshots show the same thing.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct Canvas
{
    int w = 0, h = 0;
    std::vector<uint32_t> px; // ARGB; alpha 0 means "show the game underneath"

    void Resize(int nw, int nh)
    {
        if (nw != w || nh != h)
        {
            w = nw;
            h = nh;
            px.assign((size_t)w * h, 0);
            x0d = y0d = 0;
            x1d = w;
            y1d = h;
        }
    }
    // The rectangle that has been drawn into since the last Clear. Only that part is
    // cleared and sent to the screen: the canvas is as large as the window.
    int x0d = 0, y0d = 0, x1d = 0, y1d = 0;
    bool Dirty() const { return x1d > x0d && y1d > y0d; }
    void Clear()
    {
        for (int yy = y0d; yy < y1d; yy++)
            std::fill(px.begin() + (size_t)yy * w + x0d, px.begin() + (size_t)yy * w + x1d, 0);
        x0d = w;
        y0d = h;
        x1d = y1d = 0;
    }

    // Adds the (clipped) rectangle x0..x1, y0..y1 to the area drawn into.
    void Mark(int x0, int y0, int x1, int y1)
    {
        if (x0 < x0d) x0d = x0;
        if (y0 < y0d) y0d = y0;
        if (x1 > x1d) x1d = x1;
        if (y1 > y1d) y1d = y1;
    }

    void Fill(int x, int y, int rw, int rh, uint32_t argb)
    {
        int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
        int x1 = x + rw > w ? w : x + rw, y1 = y + rh > h ? h : y + rh;
        if (x1 <= x0 || y1 <= y0) return;
        Mark(x0, y0, x1, y1);
        for (int yy = y0; yy < y1; yy++)
            for (int xx = x0; xx < x1; xx++)
                px[(size_t)yy * w + xx] = argb;
    }

    // 5x7 glyphs, one row per byte, bit 4 is the leftmost pixel.
    static const uint8_t* Glyph(char c)
    {
        static const uint8_t font[][7] = {
            { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // A
            { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, // B
            { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, // C
            { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, // D
            { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, // E
            { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, // F
            { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, // G
            { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // H
            { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, // I
            { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, // J
            { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, // K
            { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, // L
            { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, // M
            { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, // N
            { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // O
            { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, // P
            { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, // Q
            { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, // R
            { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, // S
            { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, // T
            { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // U
            { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, // V
            { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 }, // W
            { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, // X
            { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, // Y
            { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, // Z
            { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, // 0
            { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, // 1
            { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, // 2
            { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E }, // 3
            { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, // 4
            { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, // 5
            { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, // 6
            { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, // 7
            { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, // 8
            { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, // 9
            { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 }, // :
            { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 }, // -
            { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 }, // +
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06 }, // .
            { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 }, // /
            { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, // <
            { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }, // >
            { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, // !
            { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 }, // (
            { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }, // )
        };
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'Z') return font[c - 'A'];
        if (c >= '0' && c <= '9') return font[26 + c - '0'];
        static const char punct[] = ":-+./<>!()";
        const char* p = c ? strchr(punct, c) : nullptr;
        return p ? font[36 + (p - punct)] : nullptr;
    }

    static int TextWidth(const std::string& s, int scale) { return (int)s.size() * 6 * scale; }

    void Text(int x, int y, const std::string& s, int scale, uint32_t argb)
    {
        for (char c : s)
        {
            if (const uint8_t* g = Glyph(c))
                for (int row = 0; row < 7; row++)
                    for (int col = 0; col < 5; col++)
                        if (g[row] & (0x10 >> col))
                            Fill(x + col * scale, y + row * scale, scale, scale, argb);
            x += 6 * scale;
        }
    }
};

// Button presses the menu understands, edge-triggered.
struct MenuInput
{
    bool up = false, down = false, left = false, right = false, accept = false, back = false;
};

struct MenuItem
{
    std::string label;
    std::string value; // shown right-aligned; empty for plain actions
};

struct Menu
{
    bool open = false;
    int selected = 0;
    std::vector<MenuItem> items;

    // Returns the index of an item that was activated this frame, or -1.
    // dir is -1/+1 when left/right was used on it, 0 for accept.
    int Update(const MenuInput& in, int& dir)
    {
        dir = 0;
        if (!open || items.empty()) return -1;
        int n = (int)items.size();
        if (in.up) selected = (selected + n - 1) % n;
        if (in.down) selected = (selected + 1) % n;
        if (in.accept) return selected;
        if (in.left && !items[selected].value.empty())
        {
            dir = -1;
            return selected;
        }
        if (in.right && !items[selected].value.empty())
        {
            dir = 1;
            return selected;
        }
        return -1;
    }

    void Draw(Canvas& c, const std::string& title, const std::string& hint) const
    {
        int scale = c.h / 240;
        if (scale < 1) scale = 1;
        // Rows close up a little when there are too many to fit the screen.
        int lineH = std::min(11 * scale, (c.h - 8 * scale) / ((int)items.size() + 3));
        int boxW = 190 * scale;
        if (boxW > c.w - 8) boxW = c.w - 8;
        int boxH = lineH * ((int)items.size() + 3) + 6 * scale;
        int bx = (c.w - boxW) / 2, by = (c.h - boxH) / 2;

        c.Fill(0, 0, c.w, c.h, 0xA0000000);
        c.Fill(bx - scale, by - scale, boxW + 2 * scale, boxH + 2 * scale, 0xFFE8E8E8);
        c.Fill(bx, by, boxW, boxH, 0xFF1C2A44);
        c.Text(bx + (boxW - Canvas::TextWidth(title, scale)) / 2, by + 3 * scale, title, scale, 0xFFFFD75E);

        int y = by + lineH + 5 * scale;
        for (int i = 0; i < (int)items.size(); i++)
        {
            bool sel = i == selected;
            if (sel) c.Fill(bx + 2 * scale, y - 2 * scale, boxW - 4 * scale, lineH, 0xFF3D6FD1);
            uint32_t col = sel ? 0xFFFFFFFF : 0xFFC8D0E0;
            c.Text(bx + 6 * scale, y, items[i].label, scale, col);
            if (!items[i].value.empty())
            {
                std::string v = "< " + items[i].value + " >";
                c.Text(bx + boxW - 6 * scale - Canvas::TextWidth(v, scale), y, v, scale, col);
            }
            y += lineH;
        }
        c.Text(bx + (boxW - Canvas::TextWidth(hint, scale)) / 2, y + 4 * scale, hint, scale, 0xFF8FA0C0);
    }
};
