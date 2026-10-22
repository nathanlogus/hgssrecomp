#pragma once
// The overworld HUD: shown while the player walks about freely, faded out for anything else
// (messages, menus, battles, cutscenes, fades). Top left the party: icons, HP bars, status;
// top right a round map of the area around the player: its map blocks drawn from above with
// the game's own textures (TopDown), or coloured by each tile's permissions (grass, water,
// walls, doors) until they are; the place and the game's clock under it, a few badges
// (Repel steps left, an EXP. Share held) and the other players' faces down the right edge;
// along the bottom quick buttons for the Bag, the Pokemon, the menu and the friends (main.cpp
// opens them as the pause menu does). Everything is read from the game's memory, video memory
// and the ROM; nothing is written to the game's memory.
//
// The HUD is drawn into a canvas of its own and sent to the screen in a few small pieces
// (the corners and the bottom row) with the fade as the texture's alpha, so it costs little
// more than what it draws. Only main.cpp includes this, after SDL.
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <vector>
#include "hudonline.h"
#include "hudparty.h"
#include "hudworld.h"

namespace HudOverworld
{
using HudParty::Ram;

// The quick buttons along the bottom, and the map (for a click on it).
enum Quick { Q_BAG, Q_PARTY, Q_MENU, Q_FRIENDS, Q_COUNT, Q_MAP = Q_COUNT };

// Another player on this map: where they are (fx32 world units) and who.
struct Other
{
    const PartyPeer* peer = nullptr;
    double x = 0, z = 0;
};

// What main.cpp tells the HUD each frame.
struct Input
{
    bool show = false;          // the player is free on the overworld with nothing over it
    float top = 32;             // design y of the right column's top (below on-screen controls)
    float partyTop = 32;        // design y of the party strip (below the session badge)
    float bottomLift = 0;       // design pixels to keep free at the bottom (on-screen controls)
    bool hideParty = false;     // something else needs the top left now (the place's name, a battle watched)
    int hour = -1, minute = 0;  // the game's clock
    std::string place;          // the place's name
    std::vector<Other> others;  // other players on this map
    std::vector<const PartyPeer*> faces; // other players in the session (chips down the right edge)
    std::string glyphs[Q_COUNT + 1]; // the controller button of each quick button and of the zoom ("": none)
};

// Map blocks drawn from straight above with the game's own textures, for the round map: a
// small interpreter of the DS's geometry commands (the matrices, vertices and texture
// coordinates WorldBlocks::BlockFromAbove gives, in world coordinates) and a rasteriser that
// keeps the highest surface, textured from the textures in video memory (no lighting).
struct TopDown
{
    static constexpr int Px = 8, Size = 32 * Px; // pixels a tile, a block's picture
    const uint8_t* tex = nullptr; // video memory as the 3D engine sees it (GPU::VRAMFlat_Texture, _TexPal)
    const uint8_t* pal = nullptr;

    static uint32_t Rgb(uint16_t c, uint32_t a)
    {
        return a << 24 | ((c & 31) * 255 / 31) << 16 | ((c >> 5 & 31) * 255 / 31) << 8 | (c >> 10 & 31) * 255 / 31;
    }

    uint32_t Texel(uint32_t param, uint32_t plt, int s, int t) const
    {
        const int w = 8 << ((param >> 20) & 7), h = 8 << ((param >> 23) & 7), fmt = (param >> 26) & 7;
        auto wrap = [](int v, int n, bool repeat, bool flip) {
            if (!repeat) return std::clamp(v, 0, n - 1);
            const int m = ((v % (2 * n)) + 2 * n) % (2 * n);
            return m < n ? m : flip ? 2 * n - 1 - m : m - n;
        };
        s = wrap(s, w, param >> 16 & 1, param >> 18 & 1);
        t = wrap(t, h, param >> 17 & 1, param >> 19 & 1);
        const uint32_t addr = (param & 0xFFFF) << 3, i = (uint32_t)(t * w + s);
        auto t8 = [&](uint32_t a) -> uint32_t { return tex[a & 0x7FFFF]; };
        auto colour = [&](uint32_t pa, uint32_t a) { return Rgb((uint16_t)(pal[pa & 0x1FFFF] | pal[(pa + 1) & 0x1FFFF] << 8), a); };
        const uint32_t pbase = plt << 4;
        const bool clear0 = param >> 29 & 1;
        switch (fmt)
        {
        case 1: { const uint32_t c = t8(addr + i); return (c >> 5) ? colour(pbase + 2 * (c & 31), (c >> 5) * 36) : 0; }
        case 6: { const uint32_t c = t8(addr + i); return (c >> 3) ? colour(pbase + 2 * (c & 7), (c >> 3) * 255 / 31) : 0; }
        case 2: { const uint32_t c = t8(addr + i / 4) >> (i % 4 * 2) & 3; return c || !clear0 ? colour((plt << 3) + 2 * c, 255) : 0; }
        case 3: { const uint32_t c = t8(addr + i / 2) >> (i % 2 * 4) & 15; return c || !clear0 ? colour(pbase + 2 * c, 255) : 0; }
        case 4: { const uint32_t c = t8(addr + i); return c || !clear0 ? colour(pbase + 2 * c, 255) : 0; }
        case 7: { const uint16_t c = (uint16_t)(t8(addr + 2 * i) | t8(addr + 2 * i + 1) << 8); return c & 0x8000 ? Rgb(c, 255) : 0; }
        case 5:
        {
            // 4x4 blocks: two bits a texel in slot 0 or 2, and per block a palette offset and
            // mode in slot 1.
            const uint32_t block = addr + (uint32_t)((t / 4) * (w / 4) + s / 4) * 4;
            const uint32_t word = t8(block) | t8(block + 1) << 8 | t8(block + 2) << 16 | t8(block + 3) << 24;
            const uint32_t c = word >> ((t % 4) * 8 + (s % 4) * 2) & 3;
            const uint32_t ia = 0x20000 + ((block & 0x1FFFF) >> 1) + ((block & 0x40000) ? 0x10000 : 0);
            const uint32_t info = t8(ia) | t8(ia + 1) << 8;
            const uint32_t pa = pbase + (info & 0x3FFF) * 4;
            const int mode = (int)(info >> 14);
            if (c < 2 || mode == 2 || (c == 2 && mode == 0)) return colour(pa + 2 * c, 255);
            if (c == 3 && mode < 2) return 0;
            const uint32_t a = colour(pa, 255), b = colour(pa + 2, 255);
            const uint32_t ka = mode == 1 ? 4 : c == 2 ? 5 : 3, kb = 8 - ka;
            uint32_t out = 0xFF000000;
            for (int sh = 0; sh <= 16; sh += 8) out |= (((a >> sh & 255) * ka + (b >> sh & 255) * kb) / 8) << sh;
            return out;
        }
        }
        return 0;
    }

    // Draws the commands into a block's picture; the block at cell (cx, cz) of the matrix.
    void Render(const std::vector<uint32_t>& c, int cx, int cz, Image& img) const
    {
        img.w = img.h = Size;
        img.px.assign((size_t)Size * Size, 0);
        std::vector<float> depth((size_t)Size * Size, -1e30f);
        double m[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 }, stack[32][12] = {};
        int mode = 2, prim = 0, sp = 0, alpha = 31;
        uint32_t param = 0, plt = 0;
        double vx = 0, vy = 0, vz = 0, s = 0, t = 0;
        struct V { double x, y, z, s, t; };
        std::vector<V> vs;
        auto fx = [](uint32_t v) { return (int32_t)v / 4096.0; };
        // m = p * m (rows of 3; with a translation row when `rows` is 4).
        auto mult = [&](const double* p, int rows) {
            double r[12];
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 3; j++)
                {
                    if (i == 3 && rows == 3) { r[9 + j] = m[9 + j]; continue; }
                    r[i * 3 + j] = p[i * 3] * m[j] + p[i * 3 + 1] * m[3 + j] + p[i * 3 + 2] * m[6 + j] + (i == 3 ? m[9 + j] : 0);
                }
            std::copy(r, r + 12, m);
        };
        auto tri = [&](const V& a, const V& b, const V& d) {
            if (alpha == 0 || !param) return;
            const double ox = cx * 512.0, oz = cz * 512.0, k = Px / 16.0;
            const double ax = (a.x - ox) * k, az = (a.z - oz) * k, bx = (b.x - ox) * k, bz = (b.z - oz) * k, dx = (d.x - ox) * k, dz = (d.z - oz) * k;
            const double area = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
            if (std::fabs(area) < 1e-6) return;
            const int x0 = std::max(0, (int)std::floor(std::min({ ax, bx, dx }))), x1 = std::min(Size - 1, (int)std::ceil(std::max({ ax, bx, dx })));
            const int y0 = std::max(0, (int)std::floor(std::min({ az, bz, dz }))), y1 = std::min(Size - 1, (int)std::ceil(std::max({ az, bz, dz })));
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                {
                    const double px = x + 0.5, pz = y + 0.5;
                    const double w0 = ((bx - px) * (dz - pz) - (bz - pz) * (dx - px)) / area;
                    const double w1 = ((dx - px) * (az - pz) - (dz - pz) * (ax - px)) / area;
                    const double w2 = 1 - w0 - w1;
                    if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                    const float h = (float)(w0 * a.y + w1 * b.y + w2 * d.y);
                    float& dp = depth[(size_t)y * Size + x];
                    if (h < dp) continue;
                    const uint32_t col = Texel(param, plt, (int)std::floor(w0 * a.s + w1 * b.s + w2 * d.s), (int)std::floor(w0 * a.t + w1 * b.t + w2 * d.t));
                    if ((col >> 24) < 128) continue;
                    uint32_t& o = img.px[(size_t)y * Size + x];
                    if (alpha < 31 && o) o = BlendOver(o, (uint32_t)(alpha * 255 / 31) << 24 | (col & 0xFFFFFF));
                    else
                    {
                        o = col | 0xFF000000;
                        dp = h;
                    }
                }
        };
        auto vertex = [&](double x, double y, double z) {
            vx = x, vy = y, vz = z;
            vs.push_back({ x * m[0] + y * m[3] + z * m[6] + m[9], x * m[1] + y * m[4] + z * m[7] + m[10], x * m[2] + y * m[5] + z * m[8] + m[11], s, t });
            const size_t n = vs.size();
            if (prim == 0 && n == 3) tri(vs[0], vs[1], vs[2]), vs.clear();
            else if (prim == 1 && n == 4) tri(vs[0], vs[1], vs[2]), tri(vs[0], vs[2], vs[3]), vs.clear();
            else if (prim == 2 && n >= 3) tri(vs[n - 3], vs[n - 2], vs[n - 1]);
            else if (prim == 3 && n >= 4 && n % 2 == 0) tri(vs[n - 4], vs[n - 3], vs[n - 1]), tri(vs[n - 4], vs[n - 1], vs[n - 2]);
        };
        static const uint8_t numParams[0x80] = {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,0,1,1,1,0,16,12,16,12,9,3,3,0,0,0,
            1,1,1,2,1,1,1,1,1,1,1,1,0,0,0,0, 1,1,1,1,32,0,0,0,0,0,0,0,0,0,0,0,
            1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 3,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0 };
        for (size_t i = 0; i < c.size();)
        {
            const uint32_t cmd = c[i] & 0x7F;
            const size_t n = numParams[cmd];
            if (i + 1 + n > c.size()) break;
            const uint32_t* p = c.data() + i + 1;
            i += 1 + n;
            const bool pos = mode == 1 || mode == 2;
            double q[12];
            switch (cmd)
            {
            case 0x10: mode = p[0] & 3; break;
            case 0x11: if (pos && sp < 31) std::copy(m, m + 12, stack[sp++]); break;
            case 0x12: if (pos) { sp = std::clamp(sp - ((int32_t)(p[0] << 26) >> 26), 0, 31); std::copy(stack[sp], stack[sp] + 12, m); } break;
            case 0x13: if (pos) std::copy(m, m + 12, stack[p[0] & 31]); break;
            case 0x14: if (pos) std::copy(stack[p[0] & 31], stack[p[0] & 31] + 12, m); break;
            case 0x15: if (pos) { const double id[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 }; std::copy(id, id + 12, m); } break;
            case 0x16: case 0x18:
                if (!pos) break;
                for (int r = 0; r < 4; r++) for (int k = 0; k < 3; k++) q[r * 3 + k] = fx(p[r * 4 + k]);
                if (cmd == 0x16) std::copy(q, q + 12, m); else mult(q, 4);
                break;
            case 0x17: case 0x19:
                if (!pos) break;
                for (int k = 0; k < 12; k++) q[k] = fx(p[k]);
                if (cmd == 0x17) std::copy(q, q + 12, m); else mult(q, 4);
                break;
            case 0x1A: if (pos) { for (int k = 0; k < 9; k++) q[k] = fx(p[k]); mult(q, 3); } break;
            case 0x1B: if (pos) for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) m[r * 3 + k] *= fx(p[r]); break;
            case 0x1C: if (pos) for (int k = 0; k < 3; k++) m[9 + k] += fx(p[0]) * m[k] + fx(p[1]) * m[3 + k] + fx(p[2]) * m[6 + k]; break;
            case 0x22: s = (int16_t)(p[0] & 0xFFFF) / 16.0; t = (int16_t)(p[0] >> 16) / 16.0; break;
            case 0x23: vertex((int16_t)(p[0] & 0xFFFF) / 4096.0, (int16_t)(p[0] >> 16) / 4096.0, (int16_t)(p[1] & 0xFFFF) / 4096.0); break;
            case 0x24: vertex((int16_t)(p[0] << 6) / 4096.0, (int16_t)((p[0] >> 10) << 6) / 4096.0, (int16_t)((p[0] >> 20) << 6) / 4096.0); break;
            case 0x25: vertex((int16_t)(p[0] & 0xFFFF) / 4096.0, (int16_t)(p[0] >> 16) / 4096.0, vz); break;
            case 0x26: vertex((int16_t)(p[0] & 0xFFFF) / 4096.0, vy, (int16_t)(p[0] >> 16) / 4096.0); break;
            case 0x27: vertex(vx, (int16_t)(p[0] & 0xFFFF) / 4096.0, (int16_t)(p[0] >> 16) / 4096.0); break;
            case 0x28:
                vertex(vx + ((int32_t)(p[0] << 22) >> 22) / 4096.0, vy + ((int32_t)(p[0] << 12) >> 22) / 4096.0, vz + ((int32_t)(p[0] << 2) >> 22) / 4096.0);
                break;
            case 0x29: alpha = p[0] >> 16 & 31; break;
            case 0x2A: param = p[0]; break;
            case 0x2B: plt = p[0] & 0x1FFF; break;
            case 0x40: prim = p[0] & 3; vs.clear(); break;
            case 0x41: vs.clear(); break;
            }
        }
    }
};

class Hud
{
public:
    void SetRom(const uint8_t* rom, size_t size)
    {
        Rom = rom;
        RomSize = size;
        Land = NdsNarcFiles(rom, size, "a/0/6/5");
        Maps.clear();
    }

    // Shown enough to take buttons and clicks.
    bool Shown() const { return Alpha > 0.5f && Target; }
    bool Visible() const { return Alpha > 0; }
    // Design width kept at the top right (for the corner pills to sit left of the map).
    float RightReserve() const { return Visible() ? MapD + 24 : 0; }
    // Design y under the party strip (for the toasts).
    float PartyBottom() const { return Visible() && PartyShown && PartyAlpha > 0 ? PartyY + StripH + 16 : 0; }

    // The quick button (or the map, Q_MAP) at output pixel x, y, or -1.
    int HitTest(int x, int y) const
    {
        if (!Shown()) return -1;
        for (int i = 0; i <= Q_COUNT; i++)
            if (x >= Hits[i].x0 && x < Hits[i].x1 && y >= Hits[i].y0 && y < Hits[i].y1) return i;
        return -1;
    }

    // The map's zoom: 0 the widest, 2 the closest.
    void ZoomStep(int d) { Zoom = std::clamp(Zoom + d, 0, 2); }
    void ZoomCycle() { Zoom = (Zoom + 1) % 3; }

    // The map blocks from above (WorldBlocks::BlockFromAbove: commands, or the key alone
    // without them) and the 3D engine's video memory, for the round map's pictures.
    std::function<bool(int x, int z, std::vector<uint32_t>* cmds, uint64_t& key)> BlockFromAbove;
    TopDown Painter;

    // Reads the game and draws the HUD into its own canvas (Layer), the output's size.
    void Update(const Ram& r, const Input& in, uint32_t frame, int outW, int outH, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art)
    {
        Target = in.show;
        Alpha = std::clamp(Alpha + (Target ? 1.0f / 10 : -1.0f / 5), 0.0f, 1.0f);
        PartyAlpha = std::clamp(PartyAlpha + (in.hideParty ? -1.0f / 5 : 1.0f / 10), 0.0f, 1.0f);
        Layer.Resize(outW, outH);
        for (const Box& b : Boxes)
            for (int y = b.y0; y < b.y1; y++) std::fill(Layer.px.begin() + (size_t)y * Layer.w + b.x0, Layer.px.begin() + (size_t)y * Layer.w + b.x1, 0);
        Boxes.clear();
        for (Box& h : Hits) h = Box();
        Uploaded = false;
        if (!Visible()) return;
        // What is drawn stays as it was while the HUD fades out (the game has moved on).
        if (Target) Read(r, frame);
        const float W = ui.W / ui.s, H = ui.H / ui.s;
        if (PartyAlpha > 0) Region([&] { DrawParty(ui, sp, f, art, in.partyTop); }, &PartyAlpha);
        Region([&] { DrawRight(r, in, ui, sp, f, art, W); });
        Region([&] { DrawButtons(in, ui, sp, f, W, H); });
    }

    // Sends the HUD to the screen over the game's picture (SDL renderer of main.cpp).
    void Present(SDL_Renderer* rend, int outW, int outH)
    {
        if (!Visible() || Boxes.empty()) return;
        int tw = 0, th = 0;
        if (Tex) SDL_QueryTexture(Tex, nullptr, nullptr, &tw, &th);
        if (!Tex || tw != outW || th != outH)
        {
            if (Tex) SDL_DestroyTexture(Tex);
            Tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, outW, outH);
            SDL_SetTextureBlendMode(Tex, SDL_BLENDMODE_BLEND);
            Uploaded = false;
        }
        for (const Box& b : Boxes)
        {
            const SDL_Rect rc = { b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0 };
            if (!Uploaded) SDL_UpdateTexture(Tex, &rc, Layer.px.data() + (size_t)b.y0 * Layer.w + b.x0, Layer.w * 4);
            SDL_SetTextureAlphaMod(Tex, (Uint8)std::lround(Alpha * (b.alpha ? *b.alpha : 1) * 255));
            SDL_RenderCopy(rend, Tex, &rc, &rc);
        }
        Uploaded = true; // the pictures between the game's frames show the same
    }

    // The same into a canvas (headless screenshots).
    void Composite(Canvas& c) const
    {
        if (!Visible() || c.w != Layer.w || c.h != Layer.h) return;
        for (const Box& b : Boxes)
        {
            const uint32_t a = (uint32_t)std::lround(Alpha * (b.alpha ? *b.alpha : 1) * 255);
            c.Mark(b.x0, b.y0, b.x1, b.y1);
            for (int y = b.y0; y < b.y1; y++)
                for (int x = b.x0; x < b.x1; x++)
                {
                    const uint32_t s = Layer.px[(size_t)y * Layer.w + x];
                    if (s >> 24) c.px[(size_t)y * c.w + x] = BlendOver(c.px[(size_t)y * c.w + x], ((s >> 24) * a / 255) << 24 | (s & 0xFFFFFF));
                }
        }
    }

private:
    struct Box
    {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        const float* alpha = nullptr; // its own fade besides the HUD's
    };
    const uint8_t* Rom = nullptr;
    size_t RomSize = 0;
    std::vector<std::pair<uint32_t, uint32_t>> Land; // the map blocks' files (a/0/6/5)
    Canvas Layer;
    std::vector<Box> Boxes; // the parts of Layer drawn into
    Box Hits[Q_COUNT + 1];  // the quick buttons and the map, output pixels
    SDL_Texture* Tex = nullptr;
    bool Uploaded = false, Target = false;
    float Alpha = 0, PartyAlpha = 1;
    int Zoom = 1;

    // The party and the save's numbers, read every few frames.
    int Count = 0, Repel = 0;
    PartyMon Mons[6];
    bool PartyShown = false;
    uint32_t ReadAt = 0;
    float PartyY = 32;
    static constexpr float StripH = 104, MapD = 232;

    // The player: map matrix, tile position (fractional), facing.
    bool HaveMe = false;
    double MeX = 0, MeZ = 0;
    int Facing = 1;

    // Maps as pictures, a pixel a tile, by their matrix (the last few).
    struct MapPic
    {
        uint64_t key = 0;
        Image img;
    };
    std::vector<MapPic> Maps;
    const Image* MapNow = nullptr;
    // Blocks drawn from above (TopDown) by their key, and the 3 x 3 around the player's block
    // now (null where there is none yet).
    std::map<uint64_t, Image> Painted;
    const Image* Near[3][3] = {};
    int NearX = 0, NearZ = 0;

    template <class F> void Region(F draw, const float* alpha = nullptr)
    {
        Layer.x0d = Layer.w;
        Layer.y0d = Layer.h;
        Layer.x1d = Layer.y1d = 0;
        draw();
        if (Layer.Dirty()) Boxes.push_back({ Layer.x0d, Layer.y0d, Layer.x1d, Layer.y1d, alpha });
    }

    void Read(const Ram& r, uint32_t frame)
    {
        const uint32_t fs = r.R32(HudWorld::kFieldSystem), obj = r.R32(r.R32(fs + 0x40) + 0x30);
        HaveMe = r.Ptr(fs) && r.Ptr(obj);
        if (HaveMe)
        {
            // MapObject: the position (fx32, +0x70 x, +0x78 z; a tile is 16 units), facing +0x28.
            MeX = (int32_t)r.R32(obj + 0x70) / 65536.0;
            MeZ = (int32_t)r.R32(obj + 0x78) / 65536.0;
            Facing = (int)(r.R32(obj + 0x28) & 3);
        }
        MapNow = HaveMe ? MapImage(r, fs) : nullptr;
        PaintNear();
        if (frame - ReadAt < 20 && frame >= ReadAt && PartyShown) return;
        ReadAt = frame;
        // The save data (FieldSystem +0x0C, SaveData in include/save.h): its blocks' headers
        // at +0x23014 (id, size, offset, 16 bytes each), their data from +0x10.
        const uint32_t save = r.R32(fs + 0x0C);
        auto block = [&](uint32_t id, uint32_t size) -> uint32_t {
            const uint32_t h = save + 0x23014 + 16 * id;
            if (!r.Ptr(save) || r.R32(h) != id || (size && r.R32(h + 4) != size)) return 0;
            return save + 0x10 + r.R32(h + 8);
        };
        PartyShown = HudParty::ReadParty(r, block(2, 0), Count, Mons);        // RoamerSaveData (block 21, 0x68 bytes): the Repel's steps left at +0x65.
        const uint32_t roam = block(21, 0x68);
        Repel = roam ? (int)r.R8(roam + 0x65) : 0;
    }

    // The map the player is on, a pixel for each tile coloured by its permissions; matrices
    // as worldblocks.h reads them (FieldSystem +0x30: width, height, ..., the blocks' files).
    const Image* MapImage(const Ram& r, uint32_t fs)
    {
        const uint32_t matrix = r.R32(fs + 0x30);
        if (!r.Ptr(matrix) || Land.empty()) return nullptr;
        const int w = (int)r.R8(matrix), h = (int)r.R8(matrix + 1);
        if (w < 1 || h < 1 || w * h > 799) return nullptr;
        const uint32_t models = (matrix + 4 + 2 + 799 * 2 + 799 + 1) & ~1u;
        uint64_t key = 1469598103934665603ull ^ (uint64_t)(w << 8 | h);
        for (int i = 0; i < w * h; i++) key = (key ^ r.R16(models + 2 * i)) * 1099511628211ull;
        for (MapPic& m : Maps)
            if (m.key == key) return &m.img;
        if (Maps.size() >= 6) Maps.erase(Maps.begin());
        Maps.push_back({ key, Image() });
        Image& img = Maps.back().img;
        img.w = w * 32;
        img.h = h * 32;
        img.px.assign((size_t)img.w * img.h, 0);
        for (int cell = 0; cell < w * h; cell++)
        {
            const uint16_t id = (uint16_t)r.R16(models + 2 * cell);
            if (id >= Land.size()) continue;
            // A block's file: the permissions' size, ...; a header of its own (0x1234) at 16,
            // then a u16 per tile: the behaviour, and the wall bit (0x8000).
            const uint8_t* d = Rom + Land[id].first;
            const uint32_t size = Land[id].second;
            if (size < 20) continue;
            const uint32_t perm = RomGet32(d);
            uint32_t p = 16;
            if (RomGet16(d + p) == 0x1234) p += 4 + RomGet16(d + p + 2);
            if (perm < 2048 || (uint64_t)p + 2048 > size) continue;
            const int bx = cell % w * 32, bz = cell / w * 32;
            for (int t = 0; t < 1024; t++)
                img.px[(size_t)(bz + t / 32) * img.w + bx + t % 32] = TileColour(d[p + 2 * t], d[p + 2 * t + 1]);
        }
        return &img;
    }

    // The blocks around the player from above: those already drawn, and one more a frame
    // (the player's own first).
    void PaintNear()
    {
        for (auto& row : Near)
            for (auto& n : row) n = nullptr;
        if (!HaveMe || !BlockFromAbove || !Painter.tex) return;
        NearX = (int)std::floor(MeX / 32);
        NearZ = (int)std::floor(MeZ / 32);
        static const int order[9][2] = { { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
        bool painted = false;
        for (const auto& o : order)
        {
            const int x = NearX + o[0], z = NearZ + o[1];
            uint64_t key;
            if (!BlockFromAbove(x, z, nullptr, key)) continue;
            auto it = Painted.find(key);
            if (it == Painted.end())
            {
                std::vector<uint32_t> cmds;
                if (painted || !BlockFromAbove(x, z, &cmds, key)) continue;
                painted = true;
                if (Painted.size() >= 40) Painted.clear();
                it = Painted.emplace(key, Image()).first;
                static const bool prof = getenv("SSR_OWHUD_PROF") != nullptr;
                const uint64_t t0 = prof ? SDL_GetPerformanceCounter() : 0;
                Painter.Render(cmds, x, z, it->second);
                if (prof)
                    fprintf(stderr, "[hud] map block %d,%d from above: %zu words, %.2f ms\n", x, z, cmds.size(),
                        (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency());
            }
            Near[o[1] + 1][o[0] + 1] = &it->second;
        }
    }

    // Behaviours (include/constants/metatile_behavior.h): water that can be surfed, tall
    // grass, ledges, doors and other ways out.
    static uint32_t TileColour(uint8_t b, uint8_t hi)
    {
        if ((b >= 16 && b <= 21) || b == 25) return 0xFF5CA8EE;
        if (b == 2 || b == 3) return 0xFF6DBE4E;
        if (b >= 56 && b <= 59) return 0xFFC08A4E;
        if ((b >= 60 && b <= 62) || b == 94 || b == 95 || (b >= 98 && b <= 101) || b == 103 || b == 105 || (b >= 108 && b <= 111))
            return 0xFFF7A23A;
        if (hi & 0x80) return 0xFF7F9C88;
        return 0xFFF3ECD6;
    }

    void DrawParty(const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, float y)
    {
        PartyY = y;
        if (!PartyShown || Count < 1) return;
        const float slot = 80, x = 32, w = 12 + Count * slot + 4;
        HudPanel(Layer, ui, sp, "panels/card_glass", x, y, w, StripH);
        for (int i = 0; i < Count; i++)
        {
            const PartyMon& m = Mons[i];
            const float sx = x + 12 + i * slot;
            const bool fainted = !m.egg && m.hp == 0;
            if (const Image* icon = art.PokemonIcon(m.species, m.form, m.egg))
                Blit(Layer, *icon, 0, 0, 32, 32, ui(sx), ui(y + 2), ui(64), ui(64), false, fainted ? 0x70FFFFFF : 0xFFFFFFFF);
            if (m.egg) continue;
            const int most = std::max(1, m.stats[0]);
            const float bx = sx + 4, by = y + 72, bw = 60;
            HudParty::FillRound(Layer, ui, bx, by, bw, 12, 6, 0xFF1C2A44);
            if (m.hp > 0)
            {
                const uint32_t col = m.hp * 2 > most ? 0xFF5BD15B : m.hp * 5 > most ? 0xFFFFD23A : 0xFFF0505A;
                HudParty::FillRound(Layer, ui, bx + 2, by + 2, std::max(8.0f, (bw - 4) * std::min(1.0f, (float)m.hp / most)), 8, 4, col);
            }
            std::string s;
            uint32_t col;
            if (HudParty::StatusOf(m, s, col))
            {
                const float pw = HudTextWidth(ui, f, 16, s) + 10;
                HudParty::FillRound(Layer, ui, sx + 64 - pw + 2, y + 50, pw, 20, 10, col);
                HudText(Layer, ui, f, sx + 64 - pw / 2 + 2, y + 52, 16, s, HudColour::White, 1);
            }
        }
    }

    void DrawRight(const Ram& r, const Input& in, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, float W)
    {
        (void)r;
        const float d = MapD, mx = W - 32 - d, my = in.top;
        DrawMap(in, ui, sp, f, mx, my, d);
        // The place and the clock under the map, right-aligned to it.
        float y = my + d + 12;
        {
            char clock[16] = "";
            if (in.hour >= 0) snprintf(clock, sizeof clock, "%d:%02d", in.hour, in.minute);
            const std::string text = in.place.empty() ? clock : !clock[0] ? in.place : in.place + "  " + clock;
            if (!text.empty())
            {
                const float tw = HudTextWidth(ui, f, 32, text) + 40 + 40, h = 52, x = std::min(W - 32 - tw, mx + d / 2 - tw / 2);
                HudPanel(Layer, ui, sp, "panels/toast", x, y, tw, h);
                HudSprite(Layer, ui, sp, "icons/icon_clock", x + 14, y + 10, 32, 32);
                HudText(Layer, ui, f, x + 54, y + 10, 32, text, HudColour::Ink);
                y += h + 16;
            }
        }
        // Badges under it, right-aligned (the corner pills, FPS and saving, sit left of the
        // map): what the save says about the walk.
        auto badge = [&](const Image* icon, const std::string& text) {
            const float h = 48, tw = HudTextWidth(ui, f, 32, text) + 20 + 44, x = W - 32 - tw;
            HudPanel(Layer, ui, sp, "panels/toast", x, y, tw, h);
            if (icon) Blit(Layer, *icon, 0, 0, (float)icon->w, (float)icon->h, ui(x + 8), ui(y + 4), ui(40), ui(40), false);
            HudText(Layer, ui, f, x + 52, y + 8, 32, text, HudColour::Ink);
            y += h + 14;
        };
        if (Repel > 0) badge(art.ItemIcon(79), "Repel " + std::to_string(Repel));
        for (int i = 0; PartyShown && i < Count; i++)
            if (!Mons[i].egg && Mons[i].item == 216)
            {
                badge(art.ItemIcon(216), "EXP. Share");
                break;
            }
        // The other players' faces down the right edge (the session's chips).
        for (const PartyPeer* p : in.faces)
        {
            if (y > ui.H / ui.s - 200) break;
            const float s = 68, x = W - 32 - s;
            HudOnline::Chip(Layer, ui, sp, f, x, y, s, *p);
            if (p->host) HudSprite(Layer, ui, sp, "people/crown_gold", x + s / 2 - 16, y - 12, 32, 30);
            if (p->state == PS_BATTLE) HudSprite(Layer, ui, sp, "emotes/emote_battle", x - 14, y + 30, 36, 36);
            y += s + 10;
        }
    }

    // The round map: the tiles around the player, north up, the player an arrow in the middle,
    // the others their faces (at the edge when they are farther away).
    void DrawMap(const Input& in, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, float d)
    {
        static const float radii[3] = { 22, 14, 9 }; // tiles from the middle to the edge
        const float R = ui(d / 2), cx = ui(x + d / 2), cy = ui(y + d / 2), tiles = radii[Zoom], k = R / tiles;
        // A soft shadow, then the disc.
        HudParty::FillRound(Layer, ui, x - 2, y + 4, d + 4, d + 4, d / 2 + 2, 0x30101828);
        const int x0 = std::max(0, (int)(cx - R)), y0 = std::max(0, (int)(cy - R)), x1 = std::min(Layer.w, (int)std::ceil(cx + R));
        const int y1 = std::min(Layer.h, (int)std::ceil(cy + R));
        const float rim = std::max(2.0f, ui(5));
        const Image* img = MapNow;
        Layer.Mark(x0, y0, x1, y1);
        for (int py = y0; py < y1; py++)
        {
            const float dy = py + 0.5f - cy;
            for (int px = x0; px < x1; px++)
            {
                const float dx = px + 0.5f - cx, dist = std::sqrt(dx * dx + dy * dy);
                const float cov = std::clamp(R - dist + 0.5f, 0.0f, 1.0f);
                if (cov <= 0) continue;
                uint32_t col = 0xFF1C2A44;
                if (dist > R - rim) col = 0xFFFFFFFF;
                else if (HaveMe)
                {
                    // The block's picture from above where there is one, else the tile's colour.
                    const double u = MeX + dx / k, v = MeZ + dy / k, bu = std::floor(u / 32), bv = std::floor(v / 32);
                    const int bx = (int)bu - NearX + 1, bz = (int)bv - NearZ + 1;
                    const Image* pic = bx >= 0 && bz >= 0 && bx < 3 && bz < 3 ? Near[bz][bx] : nullptr;
                    uint32_t t = 0;
                    if (pic)
                    {
                        const int ix = std::clamp((int)((u - bu * 32) * TopDown::Px), 0, TopDown::Size - 1);
                        const int iy = std::clamp((int)((v - bv * 32) * TopDown::Px), 0, TopDown::Size - 1);
                        t = pic->px[(size_t)iy * TopDown::Size + ix];
                    }
                    const int tx = (int)std::floor(u), tz = (int)std::floor(v);
                    // (Where a drawn block has nothing, nothing: the space around a room.)
                    if (!pic && img && tx >= 0 && tz >= 0 && tx < img->w && tz < img->h) t = img->px[(size_t)tz * img->w + tx];
                    if (t) col = t;
                }
                uint32_t& o = Layer.px[(size_t)py * Layer.w + px];
                o = BlendOver(o, (uint32_t)(255 * cov) << 24 | (col & 0xFFFFFF));
            }
        }
        const float ccx = x + d / 2, ccy = y + d / 2, kd = d / 2 / tiles; // design units
        // The others: their faces, kept inside the disc.
        for (const Other& o : in.others)
        {
            float ox = (float)((o.x / 65536.0 - MeX) * kd), oz = (float)((o.z / 65536.0 - MeZ) * kd);
            const float len = std::sqrt(ox * ox + oz * oz), most = d / 2 - 26;
            if (len > most) ox *= most / len, oz *= most / len;
            HudOnline::Chip(Layer, ui, sp, f, ccx + ox - 22, ccy + oz - 22, 44, *o.peer);
        }
        // The player: an arrow the way they face (0 north, 1 south, 2 west, 3 east).
        static const float ang[4] = { -1.5707963f, 1.5707963f, 3.1415927f, 0 };
        const float a = ang[Facing & 3], ca = std::cos(a), sa = std::sin(a);
        auto tri = [&](float s, uint32_t col) {
            auto pt = [&](float fx, float fy) { return std::make_pair(ccx + (fx * ca - fy * sa) * s, ccy + (fx * sa + fy * ca) * s); };
            HudParty::FillConvex(Layer, ui, { pt(1.0f, 0), pt(-0.7f, 0.75f), pt(-0.35f, 0), pt(-0.7f, -0.75f) }, col);
        };
        if (HaveMe)
        {
            tri(19, 0xFF1C2A44);
            tri(14, 0xFFFFD75E);
        }
        // North, on the rim.
        HudParty::FillRound(Layer, ui, ccx - 17, y - 10, 34, 34, 17, 0xFF1C2A44);
        HudText(Layer, ui, f, ccx, y - 1, 16, "N", HudColour::White, 1, true);
        // The zoom's button, on the rim at the bottom left.
        if (!in.glyphs[Q_MAP].empty()) HudSprite(Layer, ui, sp, in.glyphs[Q_MAP], x + d * 0.15f - 22, y + d * 0.85f - 22, 44, 44);
        Hits[Q_MAP] = { (int)ui(x), (int)ui(y), (int)ui(x + d), (int)ui(y + d) };
    }

    void DrawButtons(const Input& in, const Ui& ui, Sprites& sp, const RomFont& f, float W, float H)
    {
        static const char* labels[Q_COUNT] = { "BAG", "POK\xC3\xA9MON", "MENU", "FRIENDS" };
        static const char* icons[Q_COUNT] = { "icons/icon_bag", "icons/icon_party", "icons/icon_layers", "icons/icon_wifi" };
        const float h = 56, gap = 14, y = H - 24 - h - in.bottomLift;
        float widths[Q_COUNT], total = -gap;
        for (int i = 0; i < Q_COUNT; i++)
        {
            widths[i] = 20 + (in.glyphs[i].empty() ? 0 : 48) + 40 + 10 + HudTextWidth(ui, f, 32, labels[i]) + 22;
            total += widths[i] + gap;
        }
        float x = W / 2 - total / 2;
        for (int i = 0; i < Q_COUNT; i++)
        {
            HudPanel(Layer, ui, sp, "panels/toast", x, y, widths[i], h);
            float cx = x + 20;
            if (!in.glyphs[i].empty())
            {
                HudSprite(Layer, ui, sp, in.glyphs[i], cx, y + 8, 40, 40);
                cx += 48;
            }
            HudSprite(Layer, ui, sp, icons[i], cx, y + 8, 40, 40);
            HudText(Layer, ui, f, cx + 50, y + 12, 32, labels[i], HudColour::Ink);
            Hits[i] = { (int)ui(x), (int)ui(y), (int)ui(x + widths[i]), (int)ui(y + h) };
            x += widths[i] + gap;
        }
    }
};
}
