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

    // A species' party icon, 32 x 32 (the first of its two frames), or null.
    const Image* PokemonIcon(int species)
    {
        if (species <= 0 || species > 493 || icons.size() < 8 + 493 || iconPal.size() < 48) return nullptr;
        auto it = cache.find(species);
        if (it != cache.end()) return it->second.px.empty() ? nullptr : &it->second;
        const int palNo = palTable ? palTable[species] : 0;
        const auto& f = icons[7 + species];
        Image both = NdsTiles(Rom + f.first, f.second, 32, 64, iconPal.data() + 16 * palNo, 16);
        Image& img = cache[species];
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

    // An item's price (the first field of its data in a/0/1/7), or -1 if unknown.
    int ItemPrice(int item) const
    {
        if (item <= 0 || item >= (int)itemNarc.size() / 4 || itemNarc[4 * item] >= itemData.size()) return -1;
        const auto& f = itemData[itemNarc[4 * item]];
        return f.second >= 2 ? (int)RomGet16(Rom + f.first) : -1;
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
};

// Draws a picture from the ROM (pixel art: nearest pixel) into the design rectangle.
inline void HudPixelArt(Canvas& c, const Ui& ui, const Image* img, float x, float y, float w, float h)
{
    if (img) Blit(c, *img, 0, 0, (float)img->w, (float)img->h, ui(x), ui(y), ui(w), ui(h), false);
}
