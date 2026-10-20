#pragma once
// Files in a DS ROM: the file name table and allocation table, and the NARC archives
// most of the game's data is packed in.
#include <cstdint>
#include <string>
#include <vector>

inline uint32_t RomGet16(const uint8_t* p) { return p[0] | p[1] << 8; }
inline uint32_t RomGet32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// The place of the file at `path` ("a/0/6/5", "pbr/font.narc") in the ROM.
inline bool NdsRomFile(const uint8_t* rom, size_t romSize, const char* path, uint32_t& off, uint32_t& size)
{
    if (!rom || romSize < 0x200) return false;
    const uint32_t fnt = RomGet32(rom + 0x40), fat = RomGet32(rom + 0x48);
    if (fnt >= romSize || fat >= romSize) return false;
    uint32_t dir = 0xF000;
    std::string rest = path;
    while (true)
    {
        const size_t slash = rest.find('/');
        const std::string name = rest.substr(0, slash);
        if (fnt + (dir & 0xFFF) * 8 + 8 > romSize) return false;
        const uint32_t sub = fnt + RomGet32(rom + fnt + (dir & 0xFFF) * 8);
        uint32_t fid = RomGet16(rom + fnt + (dir & 0xFFF) * 8 + 4);
        uint32_t p = sub;
        bool found = false;
        while (p < romSize && rom[p])
        {
            const uint8_t t = rom[p++];
            const uint32_t len = t & 0x7F;
            if (p + len + 2 > romSize) return false;
            const std::string entry((const char*)rom + p, len);
            p += len;
            if (t & 0x80)
            {
                const uint16_t subdir = (uint16_t)RomGet16(rom + p);
                p += 2;
                if (entry == name && slash != std::string::npos) { dir = subdir; found = true; break; }
            }
            else
            {
                if (entry == name && slash == std::string::npos)
                {
                    if (fat + fid * 8 + 8 > romSize) return false;
                    const uint32_t s = RomGet32(rom + fat + fid * 8), e = RomGet32(rom + fat + fid * 8 + 4);
                    if (e < s || e > romSize) return false;
                    off = s;
                    size = e - s;
                    return true;
                }
                fid++;
            }
        }
        if (!found) return false;
        rest = rest.substr(slash + 1);
    }
}

// The members of the NARC archive at `path` in the ROM, as (offset in the ROM, size) pairs;
// empty if there is no such archive.
inline std::vector<std::pair<uint32_t, uint32_t>> NdsNarcFiles(const uint8_t* rom, size_t romSize, const char* path)
{
    std::vector<std::pair<uint32_t, uint32_t>> out;
    uint32_t off, size;
    if (!NdsRomFile(rom, romSize, path, off, size) || size < 0x1C + 8) return out;
    const uint8_t* n = rom + off;
    if (n[0] != 'N' || n[1] != 'A' || n[2] != 'R' || n[3] != 'C') return out;
    // File allocation table (BTAF), name table (BTNF), then the data (GMIF).
    const uint32_t fatSize = RomGet32(n + 16 + 4), count = RomGet32(n + 16 + 8);
    const uint32_t fnt = 16 + fatSize;
    if (fnt + 8 > size || (uint64_t)count * 8 + 12 > fatSize) return out;
    const uint32_t gmif = fnt + RomGet32(n + fnt + 4) + 8;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t a = RomGet32(n + 16 + 12 + 8 * i), b = RomGet32(n + 16 + 16 + 8 * i);
        if (b < a || (uint64_t)gmif + b > size) break;
        out.push_back({ off + gmif + a, b - a });
    }
    return out;
}
