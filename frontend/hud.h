#pragma once
// Drawing toolkit for the HUD and menus: PNG sprites from assets/ui (original art made by
// assets/ui/gen.py), drawn with alpha blending into the overlay Canvas (ui.h), and the
// game's own font, read from the player's ROM. Sizes are given on a 1920x1080 design
// canvas and scaled to the output (Ui::s).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>
#include <string>
#include <vector>
#include <zlib.h>
#include "ndsrom.h"
#include "ui.h"
#include "thirdparty/stb_truetype.h"

// A picture: ARGB, straight (not premultiplied) alpha.
struct Image
{
    int w = 0, h = 0;
    std::vector<uint32_t> px;
};

// PNG files as assets/ui holds them: 8 bits per channel, RGB or RGBA, not interlaced.
inline bool DecodePng(const uint8_t* d, size_t n, Image& img)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (n < 8 || memcmp(d, sig, 8) != 0) return false;
    auto be32 = [](const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; };
    uint32_t w = 0, h = 0;
    int type = -1;
    std::vector<uint8_t> z;
    for (size_t p = 8; p + 12 <= n;)
    {
        const uint32_t len = be32(d + p);
        if (len > n - p - 12) return false;
        const uint8_t* t = d + p + 4;
        const uint8_t* body = d + p + 8;
        if (!memcmp(t, "IHDR", 4) && len >= 13)
        {
            w = be32(body);
            h = be32(body + 4);
            if (body[8] != 8 || body[12] != 0 || (body[9] != 2 && body[9] != 6)) return false;
            type = body[9];
        }
        else if (!memcmp(t, "IDAT", 4))
            z.insert(z.end(), body, body + len);
        else if (!memcmp(t, "IEND", 4))
            break;
        p += 12 + len;
    }
    if (type < 0 || !w || !h || w > 8192 || h > 8192) return false;
    const int bpp = type == 6 ? 4 : 3;
    const size_t stride = (size_t)w * bpp;
    std::vector<uint8_t> raw((stride + 1) * h);
    uLongf rawLen = (uLongf)raw.size();
    if (uncompress(raw.data(), &rawLen, z.data(), (uLong)z.size()) != Z_OK || rawLen != raw.size()) return false;
    // Undo the per-row filters, in place.
    for (uint32_t y = 0; y < h; y++)
    {
        uint8_t* row = &raw[y * (stride + 1) + 1];
        const uint8_t* up = y ? row - (stride + 1) : nullptr;
        const int f = row[-1];
        for (size_t x = 0; x < stride; x++)
        {
            const int a = x >= (size_t)bpp ? row[x - bpp] : 0, b = up ? up[x] : 0, c = up && x >= (size_t)bpp ? up[x - bpp] : 0;
            int v;
            switch (f)
            {
            case 0: v = 0; break;
            case 1: v = a; break;
            case 2: v = b; break;
            case 3: v = (a + b) / 2; break;
            case 4:
            {
                const int pp = a + b - c, pa = std::abs(pp - a), pb = std::abs(pp - b), pc = std::abs(pp - c);
                v = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                break;
            }
            default: return false;
            }
            row[x] = (uint8_t)(row[x] + v);
        }
    }
    img.w = (int)w;
    img.h = (int)h;
    img.px.resize((size_t)w * h);
    for (uint32_t y = 0; y < h; y++)
    {
        const uint8_t* row = &raw[y * (stride + 1) + 1];
        for (uint32_t x = 0; x < w; x++)
        {
            const uint8_t* s = row + x * bpp;
            img.px[(size_t)y * w + x] = (uint32_t)(bpp == 4 ? s[3] : 255) << 24 | s[0] << 16 | s[1] << 8 | s[2];
        }
    }
    return true;
}

// The canvas pixel `d` with `s` drawn over it ("over", straight alpha), so the game still
// shows through what is translucent.
inline uint32_t BlendOver(uint32_t d, uint32_t s)
{
    const uint32_t sa = s >> 24;
    if (sa == 255) return s;
    if (sa == 0) return d;
    const uint32_t da = d >> 24, dw = da * (255 - sa) / 255, oa = sa + dw;
    uint32_t out = oa << 24;
    for (int sh = 0; sh <= 16; sh += 8)
        out |= (((s >> sh & 0xFF) * sa + (d >> sh & 0xFF) * dw) / oa) << sh;
    return out;
}

// Multiplies a colour by a tint (alpha too): used to fade and recolour sprites.
inline uint32_t Tint(uint32_t c, uint32_t t)
{
    if (t == 0xFFFFFFFF) return c;
    uint32_t out = 0;
    for (int sh = 0; sh <= 24; sh += 8)
        out |= ((c >> sh & 0xFF) * (t >> sh & 0xFF) / 255) << sh;
    return out;
}

// Draws the part (sx, sy, sw, sh) of `img` into the rectangle (dx, dy, dw, dh) of the
// canvas, in floating-point canvas pixels: bilinear filtering for UI art (`smooth`), or the
// nearest pixel for pixel art.
inline void Blit(Canvas& c, const Image& img, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh,
    bool smooth = true, uint32_t tint = 0xFFFFFFFF)
{
    if (img.px.empty() || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    const int x0 = std::max(0, (int)std::floor(dx)), y0 = std::max(0, (int)std::floor(dy));
    const int x1 = std::min(c.w, (int)std::ceil(dx + dw)), y1 = std::min(c.h, (int)std::ceil(dy + dh));
    if (x1 <= x0 || y1 <= y0) return;
    c.Mark(x0, y0, x1, y1);
    const float kx = sw / dw, ky = sh / dh;
    for (int y = y0; y < y1; y++)
    {
        // Coverage of the destination's edge rows and columns, for edges between pixels.
        const float cy = std::min(1.0f, std::min(y + 1.0f, dy + dh) - std::max((float)y, dy));
        float fy = sy + (y + 0.5f - dy) * ky - 0.5f;
        for (int x = x0; x < x1; x++)
        {
            const float cx = std::min(1.0f, std::min(x + 1.0f, dx + dw) - std::max((float)x, dx));
            float fx = sx + (x + 0.5f - dx) * kx - 0.5f;
            uint32_t s;
            if (!smooth)
            {
                const int ix = std::clamp((int)std::floor(fx + 0.5f), 0, img.w - 1), iy = std::clamp((int)std::floor(fy + 0.5f), 0, img.h - 1);
                s = img.px[(size_t)iy * img.w + ix];
            }
            else
            {
                // Bilinear on premultiplied values, so transparent edges do not darken.
                const float cfx = std::clamp(fx, sx, sx + sw - 1), cfy = std::clamp(fy, sy, sy + sh - 1);
                const int ix = std::clamp((int)cfx, 0, img.w - 1), iy = std::clamp((int)cfy, 0, img.h - 1);
                const int ix1 = std::min(ix + 1, img.w - 1), iy1 = std::min(iy + 1, img.h - 1);
                const float ax = cfx - ix, ay = cfy - iy;
                const uint32_t p[4] = { img.px[(size_t)iy * img.w + ix], img.px[(size_t)iy * img.w + ix1],
                    img.px[(size_t)iy1 * img.w + ix], img.px[(size_t)iy1 * img.w + ix1] };
                const float wgt[4] = { (1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay };
                float a = 0, r = 0, g = 0, b = 0;
                for (int k = 0; k < 4; k++)
                {
                    const float pa = (p[k] >> 24) * wgt[k];
                    a += pa;
                    r += (p[k] >> 16 & 0xFF) * pa;
                    g += (p[k] >> 8 & 0xFF) * pa;
                    b += (p[k] & 0xFF) * pa;
                }
                if (a < 0.5f) continue;
                s = (uint32_t)(a + 0.5f) << 24 | (uint32_t)(r / a + 0.5f) << 16 | (uint32_t)(g / a + 0.5f) << 8 | (uint32_t)(b / a + 0.5f);
            }
            s = Tint(s, tint);
            const float cov = cx * cy;
            if (cov < 1) s = (uint32_t)((s >> 24) * cov) << 24 | (s & 0xFFFFFF);
            uint32_t& d = c.px[(size_t)y * c.w + x];
            d = BlendOver(d, s);
        }
    }
}

// A rectangle in a flat colour, blended (Canvas::Fill overwrites).
inline void FillBlend(Canvas& c, float x, float y, float w, float h, uint32_t argb)
{
    const int x0 = std::max(0, (int)std::lround(x)), y0 = std::max(0, (int)std::lround(y));
    const int x1 = std::min(c.w, (int)std::lround(x + w)), y1 = std::min(c.h, (int)std::lround(y + h));
    if (x1 <= x0 || y1 <= y0) return;
    c.Mark(x0, y0, x1, y1);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
        {
            uint32_t& d = c.px[(size_t)yy * c.w + xx];
            d = BlendOver(d, argb);
        }
}

// A sprite with the size it has on the 1920x1080 design canvas.
struct Sprite
{
    Image img;
    float w = 0, h = 0;             // design size
    int inset[4] = { 0, 0, 0, 0 };  // 9-slice insets, top right bottom left, design pixels
    bool nine = false;
};

// The sprites of assets/ui, found by "folder/name" ("panels/row_selected"), loaded when first
// used. The @2x files are used when the UI is drawn larger than the design size.
class Sprites
{
public:
    // `read` returns a file of the sprite folder ("manifest.json", "panels/card.png"), or
    // nothing.
    using Reader = std::vector<uint8_t> (*)(const std::string& name);

    bool Load(Reader r)
    {
        read = r;
        const std::vector<uint8_t> m = read("manifest.json");
        if (m.empty()) return false;
        ParseManifest(std::string(m.begin(), m.end()));
        return !meta.empty();
    }

    // The @2x pictures: for a UI drawn larger than the design size (Ui::s above 1).
    bool HiRes = false;

    // A picture of the player's in place of the set's sprite `name` (at that sprite's design
    // size); an empty image puts the set's back.
    void Replace(const std::string& name, const Image& img)
    {
        if (img.px.empty())
        {
            replaced.erase(name);
            return;
        }
        Sprite& s = replaced[name];
        s = Sprite();
        auto m = meta.find(name);
        s.w = m != meta.end() ? m->second.w : (float)img.w;
        s.h = m != meta.end() ? m->second.h : (float)img.h;
        s.img = img;
    }

    const Sprite* Get(const std::string& name)
    {
        if (auto r = replaced.find(name); r != replaced.end()) return &r->second;
        const std::string key = HiRes ? name + "@2x" : name;
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.img.px.empty() ? nullptr : &it->second;
        Sprite& s = cache[key];
        auto m = meta.find(name);
        if (m == meta.end() || !read)
        {
            fprintf(stderr, "[hud] no sprite %s\n", name.c_str());
            return nullptr;
        }
        s = m->second;
        std::vector<uint8_t> f = read(name + (HiRes ? "@2x.png" : ".png"));
        if (f.empty() && HiRes) f = read(name + ".png");
        if (f.empty() || !DecodePng(f.data(), f.size(), s.img))
        {
            fprintf(stderr, "[hud] cannot read sprite %s\n", name.c_str());
            s.img = Image();
            return nullptr;
        }
        return &s;
    }

private:
    Reader read = nullptr;
    std::map<std::string, Sprite> meta, cache, replaced;

    // manifest.json as gen.py writes it: a list of objects with name, folder, size and
    // nine_slice (or null). Read with a small scanner rather than a JSON library.
    void ParseManifest(const std::string& j)
    {
        size_t p = 0;
        auto field = [&](size_t from, size_t to, const char* key) -> size_t {
            const size_t k = j.find(std::string("\"") + key + "\"", from);
            return k == std::string::npos || k >= to ? std::string::npos : j.find(':', k) + 1;
        };
        auto str = [&](size_t at) {
            const size_t a = j.find('"', at), b = j.find('"', a + 1);
            return j.substr(a + 1, b - a - 1);
        };
        auto nums = [&](size_t at, int* out, int count) {
            size_t q = j.find_first_not_of(" \n\r\t", at);
            if (q == std::string::npos || j[q] != '[') return false;
            q++;
            for (int i = 0; i < count; i++)
            {
                out[i] = atoi(j.c_str() + q);
                q = j.find_first_of(",]", q) + 1;
            }
            return true;
        };
        while ((p = j.find("\"name\"", p)) != std::string::npos)
        {
            size_t end = j.find("\"name\"", p + 6);
            if (end == std::string::npos) end = j.size();
            const std::string name = str(j.find(':', p) + 1);
            const size_t fo = field(p, end, "folder"), sz = field(p, end, "size"), ns = field(p, end, "nine_slice");
            p = end;
            if (fo == std::string::npos || sz == std::string::npos) continue;
            Sprite s;
            int size[2];
            if (!nums(sz, size, 2)) continue;
            s.w = (float)size[0];
            s.h = (float)size[1];
            if (ns != std::string::npos) s.nine = nums(ns, s.inset, 4);
            meta[str(fo) + "/" + name] = s;
        }
    }
};

// Draws a sprite over the rectangle (x, y, w, h) in canvas pixels, scaling it as it goes; see
// DrawSprite, which keeps the result.
inline void DrawSpriteScaled(Canvas& c, const Sprite* s, float x, float y, float w, float h, float scale, uint32_t tint = 0xFFFFFFFF)
{
    if (!s) return;
    if (!s->nine)
    {
        Blit(c, s->img, 0, 0, (float)s->img.w, (float)s->img.h, x, y, w, h, true, tint);
        return;
    }
    // Corners keep their size; edges stretch along one axis, the middle along both.
    const float k = s->img.w / s->w; // image pixels per design pixel
    float it = s->inset[0], ir = s->inset[1], ib = s->inset[2], il = s->inset[3];
    float dt = it * scale, dr = ir * scale, db = ib * scale, dl = il * scale;
    // Smaller than its corners: the corners shrink to fit.
    if (dl + dr > w && dl + dr > 0) { const float f = w / (dl + dr); dl *= f; dr *= f; }
    if (dt + db > h && dt + db > 0) { const float f = h / (dt + db); dt *= f; db *= f; }
    const float sx[4] = { 0, il * k, s->img.w - ir * k, (float)s->img.w };
    const float sy[4] = { 0, it * k, s->img.h - ib * k, (float)s->img.h };
    // Slice edges on whole pixels: a seam between two slices would otherwise be drawn
    // twice at partial coverage and show as a faint line.
    const float dxs[4] = { std::round(x), std::round(x + dl), std::round(x + w - dr), std::round(x + w) };
    const float dys[4] = { std::round(y), std::round(y + dt), std::round(y + h - db), std::round(y + h) };
    for (int r = 0; r < 3; r++)
        for (int col = 0; col < 3; col++)
            Blit(c, s->img, sx[col], sy[r], sx[col + 1] - sx[col], sy[r + 1] - sy[r], dxs[col], dys[r],
                dxs[col + 1] - dxs[col], dys[r + 1] - dys[r], true, tint);
}

// Draws a sprite over the rectangle (x, y, w, h) in canvas pixels (on whole pixels); `scale`
// is canvas pixels per design pixel (Ui::s), used for the fixed corners of 9-slice sprites.
// Scaling and smoothing a sprite pixel by pixel is slow and the HUD draws the same sprite at
// the same size over and over (a menu's rows), so each size and tint is made once and kept;
// drawing is then only blending the finished picture in.
inline void DrawSprite(Canvas& c, const Sprite* s, float x, float y, float w, float h, float scale, uint32_t tint = 0xFFFFFFFF)
{
    if (!s) return;
    const int x0 = (int)std::lround(x), y0 = (int)std::lround(y);
    const int iw = (int)std::lround(x + w) - x0, ih = (int)std::lround(y + h) - y0;
    if (iw <= 0 || ih <= 0) return;
    struct Key
    {
        const Sprite* s;
        int w, h, scale;
        uint32_t tint;
        bool operator<(const Key& o) const
        {
            return std::tie(s, w, h, scale, tint) < std::tie(o.s, o.w, o.h, o.scale, o.tint);
        }
    };
    static std::map<Key, std::vector<uint32_t>> made;
    if (made.size() > 600) made.clear(); // a different screen size or many sizes: start again
    const Key key = { s, iw, ih, (int)std::lround(scale * 1000), tint };
    auto it = made.find(key);
    if (it == made.end())
    {
        Canvas t;
        t.Resize(iw, ih);
        DrawSpriteScaled(t, s, 0, 0, (float)iw, (float)ih, scale, tint);
        it = made.emplace(key, std::move(t.px)).first;
    }
    const std::vector<uint32_t>& px = it->second;
    const int cx0 = std::max(0, x0), cy0 = std::max(0, y0), cx1 = std::min(c.w, x0 + iw), cy1 = std::min(c.h, y0 + ih);
    if (cx1 <= cx0 || cy1 <= cy0) return;
    c.Mark(cx0, cy0, cx1, cy1);
    for (int yy = cy0; yy < cy1; yy++)
    {
        const uint32_t* src = &px[(size_t)(yy - y0) * iw + (cx0 - x0)];
        uint32_t* dst = &c.px[(size_t)yy * c.w + cx0];
        for (int xx = cx0; xx < cx1; xx++, src++, dst++)
        {
            const uint32_t a = *src >> 24;
            if (a == 255) *dst = *src;
            else if (a) *dst = BlendOver(*dst, *src);
        }
    }
}

// The game's fonts (pbr/font.narc in the ROM): 16x16 cells of 2 bits per pixel in four 8x8
// tiles, and a width per glyph. 1 is the letter, 2 its shadow. Font 0 is the dialogue font,
// font 2 the bold one (3 is the Unown alphabet). Drawn at whole multiples of the cell only, as the pixel art wants.
// A font file of the player's (assets/fonts, Text: New font), drawn smooth instead of the
// game's pixel font, at the size the pixel font would have (its 16-pixel cell at `scale`).
// Letters are rasterised once per size and kept.
class VectorFont
{
public:
    bool Load(std::vector<uint8_t> d)
    {
        data = std::move(d);
        ok = !data.empty() && stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0));
        if (ok) stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
        return ok;
    }
    bool Ok() const { return ok; }

    int Width(const std::vector<uint32_t>& text, float scale, bool bold) const
    {
        const float k = Scale(scale);
        float w = 0;
        for (size_t i = 0; i < text.size(); i++)
        {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&info, (int)text[i], &adv, &lsb);
            w += adv * k;
            if (i + 1 < text.size()) w += stbtt_GetCodepointKernAdvance(&info, (int)text[i], (int)text[i + 1]) * k;
        }
        return (int)std::lround(w + (bold ? Embolden(scale) : 0));
    }

    void Draw(Canvas& c, int x, int y, const std::vector<uint32_t>& text, float scale, uint32_t ink, bool bold) const
    {
        const float k = Scale(scale);
        // The baseline where the pixel font has it (row 12 of its cell), the letters a little
        // smaller than the cell, as they are drawn with less space around them.
        const int baseline = y + (int)std::lround(12.5f * scale);
        float pen = (float)x;
        const int thick = bold ? Embolden(scale) : 0;
        for (size_t i = 0; i < text.size(); i++)
        {
            const Glyph& g = Get(text[i], k);
            for (int t = 0; t <= thick; t++)
                Blend(c, (int)std::lround(pen) + g.x0 + t, baseline + g.y0, g, ink);
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&info, (int)text[i], &adv, &lsb);
            pen += adv * k;
            if (i + 1 < text.size()) pen += stbtt_GetCodepointKernAdvance(&info, (int)text[i], (int)text[i + 1]) * k;
        }
    }

private:
    struct Glyph
    {
        int x0 = 0, y0 = 0, w = 0, h = 0;
        std::vector<uint8_t> alpha;
    };
    std::vector<uint8_t> data;
    stbtt_fontinfo info{};
    int ascent = 0, descent = 0, gap = 0;
    bool ok = false;
    mutable std::map<std::pair<uint32_t, int>, Glyph> cache;

    // Font units to canvas pixels: the em about 14 of the pixel font's pixels.
    float Scale(float scale) const { return stbtt_ScaleForMappingEmToPixels(&info, 14.0f * scale); }
    static int Embolden(float scale) { return std::max(1, (int)std::lround(scale * 0.5f)); }

    const Glyph& Get(uint32_t cp, float k) const
    {
        const auto key = std::make_pair(cp, (int)std::lround(k * 100000));
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        Glyph& g = cache[key];
        int x1, y1;
        stbtt_GetCodepointBitmapBox(&info, (int)cp, k, k, &g.x0, &g.y0, &x1, &y1);
        g.w = std::max(0, x1 - g.x0);
        g.h = std::max(0, y1 - g.y0);
        g.alpha.assign((size_t)g.w * g.h, 0);
        if (g.w && g.h) stbtt_MakeCodepointBitmap(&info, g.alpha.data(), g.w, g.h, g.w, k, k, (int)cp);
        return g;
    }

    static void Blend(Canvas& c, int x, int y, const Glyph& g, uint32_t ink)
    {
        const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(c.w, x + g.w), y1 = std::min(c.h, y + g.h);
        if (x1 <= x0 || y1 <= y0) return;
        c.Mark(x0, y0, x1, y1);
        const uint32_t inkA = ink >> 24, rgb = ink & 0xFFFFFF;
        for (int yy = y0; yy < y1; yy++)
            for (int xx = x0; xx < x1; xx++)
            {
                const uint32_t a = g.alpha[(size_t)(yy - y) * g.w + (xx - x)] * inkA / 255;
                if (!a) continue;
                uint32_t& d = c.px[(size_t)yy * c.w + xx];
                d = BlendOver(d, a << 24 | rgb);
            }
    }
};

class RomFont
{
public:
    // Text drawn with this font file instead (nullptr: the game's own font).
    void UseVector(const VectorFont* v) { vector = v && v->Ok() ? v : nullptr; }

    bool Load(const uint8_t* rom, size_t romSize)
    {
        const auto files = NdsNarcFiles(rom, romSize, "pbr/font.narc");
        for (int f = 0; f < 2; f++)
        {
            const int member = f ? 2 : 0;
            if ((int)files.size() <= member) return false;
            Face& face = faces[f];
            face.d.assign(rom + files[member].first, rom + files[member].first + files[member].second);
            if (face.d.size() < 16) return false;
            face.widths = RomGet32(&face.d[4]);
            face.count = RomGet16(&face.d[8]);
            if (face.widths < 16 || face.widths + face.count > face.d.size() || 16 + (size_t)face.count * 64 > face.widths) return false;
        }
        ok = true;
        return true;
    }
    bool Ok() const { return ok; }

    // Width in canvas pixels at `scale` (canvas pixels per font pixel).
    int Width(const std::string& text, float scale, bool bold = false) const
    {
        if (vector) return vector->Width(Utf8(text), scale, bold);
        if (!ok) return Canvas::TextWidth(text, std::max(1, (int)std::lround(scale)));
        int w = 0;
        for (uint32_t cp : Utf8(text))
            w += Advance(faces[bold], Glyph(cp));
        return (int)std::lround(w * scale);
    }

    // Draws `text` with its top left at (x, y): the letters in `ink` and their shadow in
    // `shadow` (0 for none). Without the ROM's font the 5x7 one stands in.
    // A scale between whole numbers gives some font pixels one canvas pixel more than
    // others; the letters stay sharp.
    void Draw(Canvas& c, int x, int y, const std::string& text, float scale, uint32_t ink, uint32_t shadow, bool bold = false) const
    {
        if (vector)
        {
            vector->Draw(c, x, y, Utf8(text), scale, ink, bold);
            return;
        }
        if (!ok)
        {
            const int k = std::max(1, (int)std::lround(scale));
            c.Text(x, y + 4 * k, text, k, ink);
            return;
        }
        int pen = 0; // in font pixels, so rounding does not add up along the line
        const Face& f = faces[bold];
        for (uint32_t cp : Utf8(text))
        {
            const int g = Glyph(cp);
            if (g >= 0)
            {
                const uint8_t* cell = &f.d[16 + (size_t)g * 64];
                for (int t = 0; t < 4; t++)
                    for (int ty = 0; ty < 8; ty++)
                    {
                        const uint32_t row = cell[t * 16 + ty * 2] | cell[t * 16 + ty * 2 + 1] << 8;
                        for (int tx = 0; tx < 8; tx++)
                        {
                            const int v = (row >> ((7 - tx) * 2)) & 3;
                            const uint32_t col = v == 1 ? ink : v == 2 ? shadow : 0;
                            if (col >> 24)
                            {
                                const float px0 = x + std::round((pen + (t & 1) * 8 + tx) * scale), py0 = y + std::round(((t >> 1) * 8 + ty) * scale);
                                const float px1 = x + std::round((pen + (t & 1) * 8 + tx + 1) * scale), py1 = y + std::round(((t >> 1) * 8 + ty + 1) * scale);
                                FillBlend(c, px0, py0, px1 - px0, py1 - py0, col);
                            }
                        }
                    }
            }
            pen += Advance(f, g);
        }
    }

private:
    struct Face
    {
        std::vector<uint8_t> d;
        uint32_t widths = 0, count = 0;
    };
    Face faces[2];
    bool ok = false;
    const VectorFont* vector = nullptr;

    static int Advance(const Face& f, int g)
    {
        if (g < 0 || (uint32_t)g >= f.count) return 6;
        return std::max(1, (int)f.d[f.widths + g]);
    }

    static std::vector<uint32_t> Utf8(const std::string& s)
    {
        std::vector<uint32_t> out;
        for (size_t i = 0; i < s.size();)
        {
            const uint8_t b = s[i];
            int n = b < 0x80 ? 0 : (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : -1;
            if (n < 0 || i + n >= s.size()) { out.push_back('?'); i++; continue; }
            uint32_t cp = n ? b & (0x3F >> n) : b;
            for (int k = 1; k <= n; k++) cp = cp << 6 | (s[i + k] & 0x3F);
            out.push_back(cp);
            i += n + 1;
        }
        return out;
    }

    // Glyph index of a character (the game's character code minus one), or -1.
    static int Glyph(uint32_t cp)
    {
        if (cp >= '0' && cp <= '9') return 288 + (cp - '0');
        if (cp >= 'A' && cp <= 'Z') return 298 + (cp - 'A');
        if (cp >= 'a' && cp <= 'z') return 324 + (cp - 'a');
        if (cp >= 0xC0 && cp <= 0xFF) return 350 + (cp - 0xC0);
        static const std::pair<uint32_t, int> extra[] = {
            { 0x152, 414 }, { 0x153, 415 }, { 0x15E, 416 }, { 0x15F, 417 }, { 0xAA, 418 }, { 0xBA, 419 }, { '$', 423 },
            { 0xA1, 424 }, { 0xBF, 425 }, { '!', 426 }, { '?', 427 }, { ',', 428 }, { '.', 429 }, { 0x2026, 430 },
            { 0xB7, 431 }, { '/', 432 }, { 0x2018, 433 }, { 0x2019, 434 }, { 0x201C, 435 }, { 0x201D, 436 },
            { 0x201E, 437 }, { 0xAB, 438 }, { 0xBB, 439 }, { '(', 440 }, { ')', 441 }, { 0x2642, 442 }, { 0x2640, 443 },
            { '+', 444 }, { '-', 445 }, { '*', 446 }, { '#', 447 }, { '=', 448 }, { '&', 449 }, { '~', 450 }, { ':', 451 },
            { ';', 452 }, { 0x2660, 453 }, { 0x2663, 454 }, { 0x2665, 455 }, { 0x2666, 456 }, { 0x2605, 457 },
            { 0x25CE, 458 }, { 0x25CB, 459 }, { 0x25A1, 460 }, { 0x25B3, 461 }, { 0x25C7, 462 }, { '@', 463 },
            { 0x266A, 464 }, { '%', 465 }, { 0x2191, 474 }, { 0x2193, 475 }, { ' ', 477 }, { '\'', 434 }, { '"', 436 },
            { '<', 438 }, { '>', 439 }, { 0x2014, 445 }, { 0x2013, 445 }, { 0x2190, 438 }, { 0x2192, 439 }, { 0xA0, 477 },
        };
        for (const auto& e : extra)
            if (e.first == cp) return e.second;
        return -1;
    }
};

// The UI's scale and anchoring: the design canvas is 1920x1080, so a design pixel is `s`
// canvas pixels, and text sizes of 16, 32, 48... design pixels are whole font scales.
struct Ui
{
    float s = 1;
    int W = 0, H = 0;
    void Set(int w, int h)
    {
        W = w;
        H = h;
        s = h / 1080.0f;
    }
    float operator()(float design) const { return design * s; }
    // The font scale for text `px` design pixels tall (a cell is 16): a whole number when it
    // is close to one, as the pixel art looks best then.
    float Font(float px) const
    {
        const float k = px * s / 16, r = std::round(k);
        return r >= 1 && std::fabs(k - r) < 0.15f ? r : std::max(1.0f, k);
    }
    // Positions measured from the right or bottom edge of the design canvas, for screens
    // wider or narrower than 16:9.
    float Right(float design) const { return W - design * s; }
    float Bottom(float design) const { return H - design * s; }
};
