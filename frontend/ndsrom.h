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

// A character of the game's text encoding as UTF-8 ("" for codes with no letter).
inline std::string NdsCharToUtf8(uint32_t code)
{
    auto utf8 = [](uint32_t cp) {
        std::string s;
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | cp >> 6); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xE0 | cp >> 12); s += (char)(0x80 | (cp >> 6 & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        return s;
    };
    if (code >= 0x121 && code <= 0x12A) return std::string(1, (char)('0' + code - 0x121));
    if (code >= 0x12B && code <= 0x144) return std::string(1, (char)('A' + code - 0x12B));
    if (code >= 0x145 && code <= 0x15E) return std::string(1, (char)('a' + code - 0x145));
    if (code >= 0x15F && code <= 0x19E) return utf8(0xC0 + code - 0x15F);
    static const std::pair<uint32_t, uint32_t> extra[] = {
        { 0x1A8, '$' }, // the Pokédollar sign, written as the HUD writes money
        { 0x1AB, '!' }, { 0x1AC, '?' }, { 0x1AD, ',' }, { 0x1AE, '.' }, { 0x1AF, 0x2026 }, { 0x1B1, '/' },
        { 0x1B2, '\'' }, { 0x1B3, '\'' }, { 0x1B4, '"' }, { 0x1B5, '"' }, { 0x1B9, '(' }, { 0x1BA, ')' },
        { 0x1BB, 0x2642 }, { 0x1BC, 0x2640 }, { 0x1BD, '+' }, { 0x1BE, '-' }, { 0x1BF, '*' }, { 0x1C0, '#' },
        { 0x1C1, '=' }, { 0x1C2, '&' }, { 0x1C3, '~' }, { 0x1C4, ':' }, { 0x1C5, ';' }, { 0x1D0, '@' },
        { 0x1D2, '%' }, { 0x1DE, ' ' }, { 0xE000, ' ' }, { 0x25BC, ' ' }, { 0x25BD, ' ' }, // line and page breaks
    };
    for (const auto& e : extra)
        if (e.first == code) return utf8(e.second);
    return "";
}

// Text in the game's encoding (up to `n` codes, ending at 0xFFFF) as UTF-8.
inline std::string NdsText(const uint16_t* codes, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n && codes[i] != 0xFFFF; i++) s += NdsCharToUtf8(codes[i]);
    return s;
}

// Line `index` of the text bank `bank` (a member of the message archive, a/0/2/7 in
// HeartGold and SoulSilver), decrypted. A placeholder for a name (a control code of kind
// 0x01xx) becomes `arg`.
inline std::string NdsMessage(const uint8_t* rom, size_t romSize, int bank, int index, const std::string& arg = "")
{
    const auto files = NdsNarcFiles(rom, romSize, "a/0/2/7");
    if (bank < 0 || bank >= (int)files.size() || index < 0) return "";
    const uint8_t* b = rom + files[bank].first;
    const uint32_t size = files[bank].second;
    if (size < 4) return "";
    const uint32_t count = RomGet16(b), key = RomGet16(b + 2);
    if ((uint32_t)index >= count || 4 + count * 8 > size) return "";
    const uint32_t i = index + 1;
    uint32_t allocKey = (765 * i * key) & 0xFFFF;
    allocKey |= allocKey << 16;
    const uint32_t off = RomGet32(b + 4 + index * 8) ^ allocKey, len = RomGet32(b + 8 + index * 8) ^ allocKey;
    if ((uint64_t)off + len * 2 > size || len > 256) return "";
    std::vector<uint16_t> codes(len);
    uint16_t k = (uint16_t)(i * 596947);
    for (uint32_t c = 0; c < len; c++)
    {
        codes[c] = (uint16_t)(RomGet16(b + off + 2 * c) ^ k);
        k = (uint16_t)(k + 18749);
    }
    std::string s;
    for (uint32_t c = 0; c < len && codes[c] != 0xFFFF; c++)
        if (codes[c] == 0xFFFE && c + 2 < len)
        {
            if (codes[c + 1] >> 8 == 1) s += arg;
            c += 2 + codes[c + 2];
        }
        else
            s += NdsCharToUtf8(codes[c]);
    return s;
}
