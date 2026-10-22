#pragma once
// Pictures from the player's ROM for the HUD (nothing of the game is shipped): the Pokemon
// party icons (a/0/2/0), with the palette each species uses, which a table in the game's
// code gives (found in memory by its first entries, as the hooks find the game's code).
#include <cstring>
#include <map>
#include <vector>
#include "hud.h"

// The colours of an NCLR palette file (RGB555), as ARGB.
inline std::vector<uint32_t> NdsPalette(const uint8_t* d, uint32_t size)
{
    std::vector<uint32_t> out;
    for (uint32_t t = 0; t + 24 <= size; t += 4)
        if (!memcmp(d + t, "TTLP", 4))
        {
            const uint32_t n = RomGet32(d + t + 16), off = RomGet32(d + t + 20);
            for (uint32_t i = 0; i < n / 2 && t + 8 + off + 2 * i + 2 <= size; i++)
            {
                const uint32_t c = RomGet16(d + t + 8 + off + 2 * i);
                out.push_back(0xFF000000 | ((c & 31) * 255 / 31) << 16 | ((c >> 5 & 31) * 255 / 31) << 8 | (c >> 10 & 31) * 255 / 31);
            }
            break;
        }
    return out;
}

// The 4-bit tiles of an NCGR file, as w x h pixels in 8x8 tiles laid out row by row (as
// the game's 1D-mapped sprites are), coloured from `pal` (index 0 transparent).
inline Image NdsTiles(const uint8_t* d, uint32_t size, int w, int h, const uint32_t* pal, int palSize)
{
    Image img;
    for (uint32_t t = 0; t + 32 <= size; t += 4)
        if (!memcmp(d + t, "RAHC", 4))
        {
            const uint32_t dataSize = RomGet32(d + t + 24), off = RomGet32(d + t + 28);
            const uint8_t* px = d + t + 8 + off;
            if (t + 8 + off + dataSize > size || (uint32_t)(w * h / 2) > dataSize) break;
            img.w = w;
            img.h = h;
            img.px.assign((size_t)w * h, 0);
            const int tilesX = w / 8;
            for (int tile = 0; tile < tilesX * (h / 8); tile++)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                    {
                        const uint8_t b = px[tile * 32 + y * 4 + x / 2];
                        const int v = x & 1 ? b >> 4 : b & 15;
                        if (v && v < palSize) img.px[(size_t)((tile / tilesX) * 8 + y) * w + (tile % tilesX) * 8 + x] = pal[v];
                    }
            break;
        }
    return img;
}

// Nintendo's "backwards LZ" (BLZ): read from the end towards the start; the last 8 bytes
// give how much is compressed and how much larger the result is (as tools/ndsrom.py).
inline std::vector<uint8_t> BlzDecompress(const uint8_t* d, size_t n)
{
    std::vector<uint8_t> out(d, d + n);
    if (n < 8) return out;
    const uint32_t bounds = RomGet32(d + n - 8), extra = RomGet32(d + n - 4);
    const size_t headerLen = bounds >> 24, packedLen = bounds & 0xFFFFFF;
    if (!extra || headerLen > n || packedLen > n) return out;
    out.resize(n + extra);
    size_t src = n - headerLen, dst = n + extra;
    const size_t stop = n - packedLen;
    while (src > stop)
    {
        uint8_t flags = d[--src];
        for (int k = 0; k < 8 && src > stop; k++, flags <<= 1)
            if (flags & 0x80)
            {
                if (src < 2) return out;
                src -= 2;
                const uint32_t info = d[src] | d[src + 1] << 8;
                const size_t len = (info >> 12) + 3, disp = (info & 0xFFF) + 3;
                for (size_t i = 0; i < len && dst > 0; i++, dst--)
                    if (dst - 1 + disp < out.size()) out[dst - 1] = out[dst - 1 + disp];
            }
            else if (dst > 0)
                out[--dst] = d[--src];
    }
    return out;
}

// The ARM9 main program as it is in memory (decompressed), from the ROM.
inline std::vector<uint8_t> NdsArm9(const uint8_t* rom, size_t size)
{
    if (size < 0x30) return {};
    const uint32_t off = RomGet32(rom + 0x20), ram = RomGet32(rom + 0x28), len = RomGet32(rom + 0x2C);
    if ((size_t)off + len > size) return {};
    std::vector<uint8_t> main(rom + off, rom + off + len);
    // The module parameters, found by the "nitrocode" marker, say where the compressed
    // part ends (0: not compressed).
    for (size_t pos = 0x1C; pos + 4 <= main.size(); pos += 4)
        if (RomGet32(main.data() + pos) == 0xDEC00621)
        {
            const uint32_t end = RomGet32(main.data() + pos - 0x1C + 0x14);
            if (end > ram && end <= ram + main.size())
            {
                std::vector<uint8_t> out = BlzDecompress(main.data(), end - ram);
                out.insert(out.end(), main.begin() + (end - ram), main.end());
                return out;
            }
            break;
        }
    return main;
}

class RomArt
{
public:
    void SetRom(const uint8_t* rom, size_t size)
    {
        Rom = rom;
        RomSize = size;
        icons = NdsNarcFiles(rom, size, "a/0/2/0");
        if (!icons.empty()) iconPal = NdsPalette(rom + icons[0].first, icons[0].second);
        cache.clear();
        // The palette table, from the game's code in the ROM (so icons have their colours
        // before the game has started).
        const std::vector<uint8_t> arm9 = NdsArm9(rom, size);
        FindItemTables(arm9);
        palTable = nullptr;
        SetRam(arm9.data(), (uint32_t)arm9.size());
        if (palTable)
        {
            ownTable.assign(palTable, palTable + 544);
            palTable = ownTable.data();
        }
    }

    // The game's code in memory, for the palette table (call when it has started).
    void SetRam(const uint8_t* ram, uint32_t size)
    {
        if (palTable || !ram) return;
        // sPokemonPalNoBySpeciesAndForm starts like this (species 0 to 15).
        static const uint8_t start[] = { 0, 1, 1, 1, 0, 0, 0, 0, 2, 2, 1, 1, 0, 1, 2, 2 };
        for (uint32_t a = 0; a + 544 <= size && a < 0x200000; a++)
            if (!memcmp(ram + a, start, sizeof start))
            {
                bool ok = true;
                for (int i = 0; i < 544 && ok; i++) ok = ram[a + i] <= 2;
                if (ok)
                {
                    palTable = ram + a;
                    cache.clear(); // icons made before used the first palette
                    return;
                }
            }
    }

    // A species' party icon, 32 x 32 (the first of its two frames), or null: the form's own
    // where the game has one (Unown's letters, Deoxys, Burmy...), or an egg's. As
    // GetMonIconNaixEx and GetMonIconPaletteEx (src/pokemon_icon_idx.c) choose them.
    const Image* PokemonIcon(int species, int form = 0, bool egg = false)
    {
        if (species <= 0 || species > 493 || icons.size() < 8 + 493 || iconPal.size() < 48) return nullptr;
        // Each species with forms: its first form's icon, and palette (in the table, after
        // the species and the two eggs), and how many forms it has besides the first.
        struct Forms { int species, file, pal, most; };
        static const Forms forms[] = { { 386, 503, 496, 3 }, { 201, 507, 499, 27 }, { 412, 534, 527, 2 }, { 413, 536, 529, 2 },
            { 422, 538, 531, 1 }, { 423, 539, 532, 1 }, { 487, 540, 533, 1 }, { 492, 541, 534, 1 }, { 479, 542, 535, 5 } };
        int file = 7 + species, pal = species;
        if (egg)
        {
            file = species == 490 ? 502 : 501; // Manaphy's egg has its own
            pal = species == 490 ? 495 : 494;
        }
        else
            for (const Forms& fm : forms)
                if (fm.species == species && form > 0 && form <= fm.most)
                {
                    file = fm.file + form - 1;
                    pal = fm.pal + form - 1;
                }
        if ((int)icons.size() <= file) return nullptr;
        const int key = file; // the picture and its palette both follow from it
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.px.empty() ? nullptr : &it->second;
        const int palNo = palTable && pal < 544 ? palTable[pal] : 0;
        const auto& f = icons[file];
        Image both = NdsTiles(Rom + f.first, f.second, 32, 64, iconPal.data() + 16 * palNo, 16);
        Image& img = cache[key];
        if (both.px.size() == 32 * 64)
        {
            img.w = img.h = 32;
            img.px.assign(both.px.begin(), both.px.begin() + 32 * 32);
        }
        return img.px.empty() ? nullptr : &img;
    }

    // An item's icon (a/0/1/8), 32 x 32, or null.
    const Image* ItemIcon(int item)
    {
        if (item <= 0 || item >= (int)itemNarc.size() / 4) return nullptr;
        auto it = itemCache.find(item);
        if (it != itemCache.end()) return it->second.px.empty() ? nullptr : &it->second;
        Image& img = itemCache[item];
        const uint32_t g = itemNarc[4 * item + 1], p = itemNarc[4 * item + 2];
        if (g < itemIcons.size() && p < itemIcons.size())
        {
            const std::vector<uint32_t> pal = NdsPalette(Rom + itemIcons[p].first, itemIcons[p].second);
            if (pal.size() >= 16) img = NdsTiles(Rom + itemIcons[g].first, itemIcons[g].second, 32, 32, pal.data(), 16);
        }
        return img.px.empty() ? nullptr : &img;
    }

    // A piece of the Pokégear map's screen of pieces copied over the map, as the game's
    // CopyToBgTilemapRect does: where it goes (dx, dy), its size and where it is in the
    // screen (sx, sy), in tiles.
    struct MapPiece
    {
        int dx, dy, w, h, sx, sy;
    };

    // The region map of the Pokégear, the Town Map and flying (a/1/4/4,
    // application/pokegear/map/pgmap_gra), as the game builds its BG layer: the first 47 x 20
    // tiles of the screen of pieces (member 11, 64 x 64 tiles of member 10's, 8 bits a
    // pixel), with `pieces` copied over them, in the colours of palette member `pal`.
    // 376 x 160 pixels: a tile is a cell of the map (PokegearMapLocationSpec's x and y).
    const Image* RegionMap(int pal, const std::vector<MapPiece>& pieces)
    {
        std::vector<int> key{ pal };
        for (const MapPiece& p : pieces) key.insert(key.end(), { p.dx, p.dy, p.w, p.h, p.sx, p.sy });
        auto it = mapCache.find(key);
        if (it != mapCache.end()) return it->second.px.empty() ? nullptr : &it->second;
        Image& img = mapCache[key];
        const auto files = NdsNarcFiles(Rom, RomSize, "a/1/4/4");
        if ((int)files.size() <= std::max(pal, 11)) return nullptr;
        const uint8_t *g = Rom + files[10].first, *s = Rom + files[11].first;
        const uint32_t gn = files[10].second, sn = files[11].second;
        const std::vector<uint32_t> colours = NdsPalette(Rom + files[pal].first, files[pal].second);
        // The tiles (RAHC: 8 bits a pixel is depth 4) and the screen (NRCS: its size in pixels).
        if (gn < 0x30 || sn < 0x24 || memcmp(g + 0x10, "RAHC", 4) || memcmp(s + 0x10, "NRCS", 4) || RomGet32(g + 0x1C) != 4 || colours.empty())
            return nullptr;
        const uint8_t* tiles = g + 0x18 + RomGet32(g + 0x2C);
        const uint32_t nTiles = std::min(RomGet32(g + 0x28), gn - (uint32_t)(tiles - g)) / 64;
        const int sw = RomGet16(s + 0x18) / 8, sh = RomGet16(s + 0x1A) / 8;
        if ((uint32_t)(0x24 + 2 * sw * sh) > sn) return nullptr;
        auto scr = [&](int x, int y) { return (uint16_t)RomGet16(s + 0x24 + 2 * (y * sw + x)); };
        constexpr int W = 47, H = 20;
        std::vector<uint16_t> map(W * H);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) map[y * W + x] = scr(x, y);
        for (const MapPiece& p : pieces)
            for (int y = 0; y < p.h; y++)
                for (int x = 0; x < p.w; x++)
                    if (p.dx + x >= 0 && p.dx + x < W && p.dy + y >= 0 && p.dy + y < H && p.sx + x < sw && p.sy + y < sh)
                        map[(p.dy + y) * W + p.dx + x] = scr(p.sx + x, p.sy + y);
        img.w = 8 * W;
        img.h = 8 * H;
        img.px.assign((size_t)img.w * img.h, colours[0]);
        for (int i = 0; i < W * H; i++)
        {
            const uint16_t e = map[i];
            if ((e & 0x3FF) >= nTiles) continue;
            const uint8_t* t = tiles + 64 * (e & 0x3FF);
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                {
                    const uint8_t v = t[(e & 0x800 ? 7 - y : y) * 8 + (e & 0x400 ? 7 - x : x)];
                    if (v < colours.size()) img.px[(size_t)((i / W) * 8 + y) * img.w + (i % W) * 8 + x] = colours[v];
                }
        }
        return &img;
    }

    // An item's price (the first field of its data in a/0/1/7), or -1 if unknown.
    int ItemPrice(int item) const
    {
        if (item <= 0 || item >= (int)itemNarc.size() / 4 || itemNarc[4 * item] >= itemData.size()) return -1;
        const auto& f = itemData[itemNarc[4 * item]];
        return f.second >= 2 ? (int)RomGet16(Rom + f.first) : -1;
    }

    // An item's data as the game keeps it (a/0/1/7, ItemData), or null if unknown.
    const uint8_t* ItemBytes(int item, uint32_t& size) const
    {
        if (item <= 0 || item >= (int)itemNarc.size() / 4 || itemNarc[4 * item] >= itemData.size()) return nullptr;
        const auto& f = itemData[itemNarc[4 * item]];
        size = f.second;
        return Rom + f.first;
    }

    // The move a TM or HM teaches (ITEM_TM01 is 328: 92 TMs, then 8 HMs), or 0.
    int TmMove(int item) const { return item >= 328 && item < 428 && tmMoves.size() == 100 ? tmMoves[item - 328] : 0; }

private:
    // sItemNarcIds (per item: its data, icon tiles and icon palette in the archives, and its
    // Gen 3 number) and the TMs' moves, found in the game's code by their first entries.
    void FindItemTables(const std::vector<uint8_t>& arm9)
    {
        itemNarc.clear();
        tmMoves.clear();
        itemCache.clear();
        itemIcons = NdsNarcFiles(Rom, RomSize, "a/0/1/8");
        itemData = NdsNarcFiles(Rom, RomSize, "a/0/1/7");
        static const uint8_t none[] = { 0, 0, 0x19, 3, 0x1A, 3 }, master[] = { 1, 0, 2, 0, 3, 0 }; // data, tiles, palette
        static const uint8_t tms[] = { 0x08, 1, 0x51, 1, 0x60, 1, 0x5B, 1 }; // Focus Punch, Dragon Claw, Water Pulse, Calm Mind
        const size_t n = arm9.size();
        for (size_t a = 0; a + 537 * 8 <= n && itemNarc.empty(); a += 2)
            if (!memcmp(arm9.data() + a, none, sizeof none) && !memcmp(arm9.data() + a + 8, master, sizeof master))
                for (int i = 0; i < 537 * 4; i++) itemNarc.push_back((uint16_t)RomGet16(arm9.data() + a + 2 * i));
        for (size_t a = 0; a + 200 <= n && tmMoves.empty(); a += 2)
            if (!memcmp(arm9.data() + a, tms, sizeof tms))
                for (int i = 0; i < 100; i++) tmMoves.push_back((uint16_t)RomGet16(arm9.data() + a + 2 * i));
    }

    std::vector<std::pair<uint32_t, uint32_t>> itemIcons, itemData;
    std::vector<uint16_t> itemNarc, tmMoves;
    std::map<int, Image> itemCache;
    const uint8_t* Rom = nullptr;
    size_t RomSize = 0;
    std::vector<std::pair<uint32_t, uint32_t>> icons;
    std::vector<uint32_t> iconPal;
    const uint8_t* palTable = nullptr;
    std::vector<uint8_t> ownTable; // the table, when read from the ROM
    std::map<int, Image> cache;
    std::map<std::vector<int>, Image> mapCache; // region maps by palette and pieces
};

// Draws the columns x1 to x2 (map cells, a tile each) of a region map (RomArt::RegionMap) so
// it fills the design rectangle (ax, ay, aw, ah): zoomed to the next whole number of screen
// pixels a map pixel that covers it (crisp), cropped, and panned so the cell (fx, fy) stays
// at least `margin` cells from the edges, its bottom corners rounded. `pan` keeps the view's corner (map pixels) from
// one frame to the next, so it glides there; a negative x starts it at its place. Gives a
// cell's design size and where cell (0, 0) is, for the markers on top.
inline void HudRegionMap(Canvas& c, const Ui& ui, const Image& img, int x1, int x2, float ax, float ay, float aw, float ah, float fx,
    float fy, float margin, float radius, float* pan, float& cell, float& ox, float& oy)
{
    const float w = 8.0f * (x2 - x1), h = (float)img.h, pw = std::round(ui(aw)), ph = std::round(ui(ah));
    float k = std::max(pw / w, ph / h);
    if (k >= 1) k = std::ceil(k - 0.001f);
    const float vw = pw / k, vh = ph / k, cx = 8 * fx + 4, cy = 8 * fy + 4;
    const float mx = std::min(8 * margin, vw / 2), my = std::min(8 * margin, vh / 2);
    float tx = pan[0] < 0 ? cx - vw / 2 : pan[0], ty = pan[0] < 0 ? cy - vh / 2 : pan[1];
    tx = std::clamp(tx, cx + mx - vw, cx - mx);
    ty = std::clamp(ty, cy + my - vh, cy - my);
    tx = std::clamp(tx, 8.0f * x1, 8.0f * x1 + w - vw);
    ty = std::clamp(ty, 0.0f, h - vh);
    if (pan[0] < 0 || std::abs(tx - pan[0]) + std::abs(ty - pan[1]) < 1 / k) pan[0] = tx, pan[1] = ty;
    else pan[0] += (tx - pan[0]) * 0.25f, pan[1] += (ty - pan[1]) * 0.25f;
    // Whole screen pixels, so every map pixel stays k wide.
    const float sx = std::round(pan[0] * k) / k, sy = std::round(pan[1] * k) / k, px = std::round(ui(ax)), py = std::round(ui(ay));
    // Row by row, so the bottom corners can be rounded as the card's (`radius`, design).
    const float r = ui(radius);
    for (int y = 0; y < (int)ph; y++)
    {
        const float d = ph - y - 0.5f; // from the bottom edge
        const float in = d < r ? std::round(r - std::sqrt(std::max(0.0f, r * r - (r - d) * (r - d)))) : 0;
        if (pw > 2 * in) Blit(c, img, sx + in / k, sy + y / k, (pw - 2 * in) / k, 1 / k, px + in, py + y, pw - 2 * in, 1, false);
    }
    cell = 8 * k / ui.s;
    ox = (px - sx * k) / ui.s;
    oy = (py - sy * k) / ui.s;
}

// Draws a picture from the ROM (pixel art: nearest pixel) into the design rectangle.
inline void HudPixelArt(Canvas& c, const Ui& ui, const Image* img, float x, float y, float w, float h)
{
    if (img) Blit(c, *img, 0, 0, (float)img->w, (float)img->h, ui(x), ui(y), ui(w), ui(h), false);
}

// Draws pixel art centred on the design point (cx, cy) by its visible pixels, not by its
// picture's box (the game's icons sit anywhere in their cells, some small, some filling
// them), zoomed so the larger side of the visible part is `size` design pixels: icons then
// look alike in size and sit in the middle of what holds them. The zoom is never more than
// the picture's box filling `most` design pixels (default 1.5 x `size`), so a tiny icon is
// not blown up, and is a whole number of canvas pixels when it is close to one, to stay crisp.
// A `size` of 0 keeps the zoom of the box filling `most` (Pokémon, whose sizes differ on purpose).
inline void HudPixelArtCentred(Canvas& c, const Ui& ui, const Image* img, float cx, float cy, float size, float most = 0,
    uint32_t tint = 0xFFFFFFFF)
{
    if (!img || img->px.empty()) return;
    int x0 = img->w, y0 = img->h, x1 = 0, y1 = 0;
    for (int y = 0; y < img->h; y++)
        for (int x = 0; x < img->w; x++)
            if ((img->px[(size_t)y * img->w + x] >> 24) >= 64)
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x + 1), y1 = std::max(y1, y + 1);
    if (x1 <= x0) return;
    float k = ui(most > 0 ? most : 1.5f * size) / std::max(img->w, img->h);
    if (size > 0) k = std::min(k, ui(size) / std::max(x1 - x0, y1 - y0));
    if (std::round(k) >= 1 && std::fabs(k - std::round(k)) < 0.15f * k) k = std::round(k);
    // On whole canvas pixels, so each picture pixel keeps its width.
    const float dx = std::round(ui(cx) - (x0 + x1) * k / 2), dy = std::round(ui(cy) - (y0 + y1) * k / 2);
    Blit(c, *img, 0, 0, (float)img->w, (float)img->h, dx, dy, img->w * k, img->h * k, false, tint);
}

// Where the inside of a "people/chip_ring_*" sprite drawn at (x, y), `d` design pixels
// square, has its centre: its disc is the circle at (48, 46) of its 96 (a shadow below).
inline float RingCentreY(float y, float d) { return y + d * 46 / 96; }
