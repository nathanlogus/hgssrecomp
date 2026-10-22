#pragma once
// The Wi-Fi Plaza as a place on the overworld, with no online login: the receptionist in
// a Pokemon Center's basement lets the player into a map laid out like the Plaza's room,
// where players of a party see each other as anywhere else (party.cpp); stepping back
// onto the bottom of the entrance corridor leads out again.
//
// The game's own Plaza starts from map 516 (MAP_WIFI_PLAZA_WARP_ROOM in the pret
// decompilation): a small room whose script hands over to the Plaza program. That map
// becomes the Plaza itself by changing only its own data in the ROM image, before the
// consoles are made (so --lockstep's two consoles run on the same data); no code and no
// table of the game changes. The game keeps the ROM's file table in memory from boot on,
// so a save state made before would read moved files from their old places: everything
// is rewritten inside the files and archive members it replaces, with the same sizes.
//   - map matrix 280 (the room's, used by no other map): 2 x 2 blocks instead of 1, the
//     same 18 bytes with a shorter name. Its land blocks: 378 (the room's) and three that
//     no matrix and no code of the game uses (checked against every matrix below).
//   - those four land blocks: who may walk where, from the Plaza's own grid (a/2/0/0
//     member 0); their models draw nothing, the room is drawn by the frontend
//     (WorldBlocks::EmitPlaza) from the Plaza's models.
//   - events 468: no people, one trigger across the bottom of the entrance corridor.
//   - scripts 147: that trigger's script, the game's own way back from the Plaza (warp
//     to the basement the player came from, whose header then walks them out of the
//     door), and the room's arrival script (header 400 runs it once): the Plaza's music.
//   - scripts 164 (every basement's receptionist): the warp lands at the corridor. Her
//     save before going in stays: the game has no menu in the room (its place is a
//     "Mystery Zone"), so no save is ever made there and a game or emulator without this
//     data never starts in a Plaza it doesn't have; that save is also the one a broken
//     link restarts from after a trip to the Union Room (party.cpp's hand-off).
//   - text bank 267: the receptionist speaks of the Plaza, not of Nintendo WFC.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "ndsrom.h"

namespace PlazaMap
{
constexpr int MapId = 516;
// Plaza grid tile (x, z) is map tile (x + OffX, z + OffZ): the grid in the middle of the
// 64 x 64 tiles of 2 x 2 blocks.
constexpr int OffX = 14, OffZ = 12;
constexpr int GridW = 35, GridH = 42;
// Grid rows above the room's back wall are behind it: not walkable.
constexpr int TopZ = 6;
// Where the receptionist's warp lands (the Plaza's own start, facing north) and the row
// below it that leads out, in grid tiles.
constexpr int ArriveX = 18, ArriveZ = 39, ExitX = 17, ExitW = 5, ExitZ = 40;
// Land blocks: the room's own, then three no map uses (see Patch), in matrix order.
constexpr uint16_t Blocks[4] = { 378, 628, 629, 630 };
constexpr uint16_t PlazaMusic = 1151; // SEQ_GS_WIFIUNION, what the Plaza program plays

// The game's text encoding for the few characters the receptionist's lines use;
// \n, \r and \f are its line, scroll and page breaks.
inline std::vector<uint16_t> EncodeText(const char* s)
{
    std::vector<uint16_t> out;
    for (; *s; s++)
    {
        const char c = *s;
        if (c >= '0' && c <= '9') out.push_back(0x121 + c - '0');
        else if (c >= 'A' && c <= 'Z') out.push_back(0x12B + c - 'A');
        else if (c >= 'a' && c <= 'z') out.push_back(0x145 + c - 'a');
        else
        {
            static const std::pair<char, uint16_t> map[] = { { ' ', 0x1DE }, { '!', 0x1AB }, { '?', 0x1AC }, { ',', 0x1AD },
                { '.', 0x1AE }, { '\'', 0x1B3 }, { '-', 0x1BE }, { '\n', 0xE000 }, { '\r', 0x25BC }, { '\f', 0x25BD } };
            bool found = false;
            for (const auto& m : map)
                if (m.first == c) { out.push_back(m.second); found = true; }
            if (!found) return {};
        }
    }
    return out;
}

// Rewrites the files above in `rom`; false (and nothing changed) when they are not what
// HeartGold/SoulSilver (US) has.
inline bool Patch(std::vector<uint8_t>& rom)
{
    uint8_t* r = rom.data();
    const size_t n = rom.size();
    auto put16 = [](uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); };
    auto put32 = [](uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); };
    const auto grid = NdsNarcFiles(r, n, "a/2/0/0");
    const auto land = NdsNarcFiles(r, n, "a/0/6/5");
    const auto matrices = NdsNarcFiles(r, n, "a/0/4/1");
    const auto events = NdsNarcFiles(r, n, "a/0/3/2");
    const auto scripts = NdsNarcFiles(r, n, "a/0/1/2");
    const auto texts = NdsNarcFiles(r, n, "a/0/2/7");
    auto fail = [](const char* why) { fprintf(stderr, "[plaza] not set up: %s\n", why); return false; };
    if (grid.empty() || grid[0].second != GridW * GridH * 4 || land.size() <= 630 || matrices.size() <= 280 || events.size() <= 468
        || scripts.size() <= 400 || texts.size() <= 267)
        return fail("not the expected files");

    // The room's matrix: 1 x 1, no headers or heights, an 11-letter name, block 378.
    static const uint8_t room[18] = { 1, 1, 0, 0, 11, 'm', '_', 'h', 'i', 'r', 'o', 'b', 'a', '0', '1', '_', 0x7A, 0x01 };
    uint8_t* matrix = r + matrices[280].first;
    if (matrices[280].second != sizeof(room) || memcmp(matrix, room, sizeof(room)) != 0) return fail("map matrix");
    // Every block of every matrix: 378 only in the room's, the other three in none. The
    // game also puts blocks in matrices from its code (pokeheartgold map_matrix.c: 86 and
    // 89-100 by Mahogany Town and the Lake of Rage, 652-663 for the Safari Zone's areas),
    // none of them these.
    for (size_t m = 0; m < matrices.size(); m++)
    {
        const uint8_t* b = r + matrices[m].first;
        const uint32_t size = matrices[m].second;
        if (size < 5) continue;
        const uint32_t cells = b[0] * b[1];
        const uint32_t at = 5 + b[4] + (b[2] ? 2 * cells : 0) + (b[3] ? cells : 0);
        if (at + 2 * cells > size) return fail("map matrices");
        for (uint32_t c = 0; c < cells; c++)
            for (int k = 0; k < 4; k++)
                if (RomGet16(b + at + 2 * c) == Blocks[k] && (k > 0 || m != 280)) return fail("land blocks in use");
    }

    // The land block: sizes of permissions, props, model and terrain (BDHC).
    uint8_t* blk = r + land[378].first;
    uint32_t perm = RomGet32(blk), props = RomGet32(blk + 4), mdl = RomGet32(blk + 8), bdhc = RomGet32(blk + 12);
    uint32_t p = 16;
    if (RomGet16(blk + p) == 0x1234) p += 4 + RomGet16(blk + p + 2);
    if (perm != 32 * 32 * 2 || (uint64_t)p + perm + props + mdl + bdhc > land[378].second || memcmp(blk + p + perm + props, "BMD0", 4) != 0)
        return fail("land block");
    // The model's structure code (NitroSystem MDL0), to end at its first byte.
    const uint8_t* model = blk + p + perm + props;
    const uint32_t mdl0 = RomGet32(model + 0x10);
    if (mdl0 + 0x20 > mdl || memcmp(model + mdl0, "MDL0", 4) != 0) return fail("land model");
    const uint8_t* dict = model + mdl0 + 8;
    const uint32_t mo = mdl0 + RomGet32(dict + RomGet16(dict + 6) + 4);
    const uint32_t sbc = mo + RomGet32(model + mo + 4);
    if (sbc >= mdl) return fail("land model");
    const uint32_t blockSize = p + perm + mdl + bdhc;
    for (uint16_t b : Blocks)
        if (land[b].second < blockSize) return fail("land block sizes");

    // The receptionist's warp: Warp (176) map 516, 0, x 20, z 11, north.
    const uint8_t want[12] = { 176, 0, 0x04, 0x02, 0, 0, 20, 0, 11, 0, 0, 0 };
    uint8_t* recep = r + scripts[164].first;
    uint8_t* warp = nullptr;
    for (uint32_t i = 0; i + 12 <= scripts[164].second; i++) // commands have byte arguments too: any alignment
        if (memcmp(recep + i, want, 12) == 0)
        {
            if (warp) return fail("receptionist script");
            warp = recep + i;
        }
    if (!warp) return fail("receptionist script");

    // Scripts of the room (each command a 16-bit number and its arguments).
    const uint16_t roomScripts[] = {
        6, 0, 52, 0, 0xFD13,   // the offsets of scripts 1 and 2 (from after each), end of list
        // 1: the trigger at the bottom of the corridor.
        96,                    // LockAll
        174, 6, 1, 0, 0,       // FadeScreen to black
        175,                   // WaitFade
        82,                    // ResetBGM: the map's own (a Pokemon Center's), so the basement's starts
        41, 0x4137, 1,         // SetVar VAR_UNK_4137, 1: the basement walks the player out
        176, 0x4056, 0, 5, 2, 1, // Warp to VAR_UNK_4056 (the basement it was entered from), 5, 2, south
        174, 6, 1, 1, 0,       // FadeScreen from black
        175,                   // WaitFade
        97,                    // ReleaseAll
        2,                     // End
        // 2: on arrival (the header's frame table, while VAR_TEMP_x4003 is 0).
        41, 0x4003, 1,         // SetVar VAR_TEMP_x4003, 1: once (and the trigger works from now)
        83, PlazaMusic,        // the music the field comes back to (from a registered item's screen)
        80, PlazaMusic,        // PlayBGM
        2,                     // End
    };
    // The offsets: script 1 starts after the list (byte 10), script 2 after its 25 numbers.
    static_assert(sizeof(roomScripts) == 2 * (5 + 25 + 8), "script layout");
    // Zone events: no background events, people or warps; one trigger (script 1, x, z,
    // width, height, y, value, variable: runs once VAR_TEMP_x4003 is 1, after arrival).
    uint8_t ev[32] = {};
    put32(ev + 12, 1);
    const uint16_t coord[8] = { 1, ExitX + OffX, ExitZ + OffZ, ExitW, 1, 0, 1, 0x4003 };
    for (int i = 0; i < 8; i++) put16(ev + 16 + 2 * i, coord[i]);
    // Header 400: the frame table (VAR_TEMP_x4003 == 0 runs script 1), to run script 2.
    static const uint8_t header[16] = { 1, 1, 0, 0, 0, 0, 0x03, 0x40, 0, 0, 1, 0, 0, 0, 0, 0 };
    uint8_t* hdr = r + scripts[400].first;
    if (events[468].second < sizeof(ev) || scripts[147].second < sizeof(roomScripts) || scripts[400].second != sizeof(header)
        || memcmp(hdr, header, sizeof(header)) != 0)
        return fail("event or script files");

    // The receptionist's lines (text bank 267, each line encrypted with its own key): 18
    // her question, 22 what the Plaza is (then her question again). Each keeps its place
    // and may only get shorter; both end with the same code (a wait for the menu).
    struct Line { int index; const char* text; };
    static const Line lines[] = {
        { 18, "Welcome to the Wi-Fi Plaza!\rWould you like to go in? " },
        { 22, "In the Wi-Fi Plaza, you may meet\nthe other Trainers of your party.\rThe Plaza's games and Tap Toys\nare closed.\rWould you like to go in? " },
    };
    uint8_t* bank = r + texts[267].first;
    const uint32_t bankSize = texts[267].second;
    const uint32_t count = RomGet16(bank), key = RomGet16(bank + 2);
    struct Text { uint32_t at, keyed, off; std::vector<uint16_t> codes; };
    std::vector<Text> rewritten;
    for (const Line& l : lines)
    {
        if ((uint32_t)l.index >= count || 4 + count * 8 > bankSize) return fail("text");
        const uint32_t i = l.index + 1;
        uint32_t allocKey = (765 * i * key) & 0xFFFF;
        allocKey |= allocKey << 16;
        const uint32_t off = RomGet32(bank + 4 + l.index * 8) ^ allocKey, len = RomGet32(bank + 8 + l.index * 8) ^ allocKey;
        if ((uint64_t)off + 2 * len > bankSize || len < 5) return fail("text");
        std::vector<uint16_t> old(len);
        uint16_t k = (uint16_t)(i * 596947);
        for (uint32_t c = 0; c < len; c++, k = (uint16_t)(k + 18749)) old[c] = (uint16_t)(RomGet16(bank + off + 2 * c) ^ k);
        // The end: the wait for the menu (0xFFFE 0x0200, one argument 0), then 0xFFFF.
        static const uint16_t tail[5] = { 0xFFFE, 0x0200, 1, 0, 0xFFFF };
        std::vector<uint16_t> codes = EncodeText(l.text);
        if (codes.empty() || memcmp(&old[len - 5], tail, sizeof(tail)) != 0) return fail("text");
        codes.insert(codes.end(), tail, tail + 5);
        if (codes.size() > len) return fail("text length");
        rewritten.push_back({ 8 + (uint32_t)l.index * 8, allocKey, off, codes });
    }

    // Everything checked: write. Each file keeps its size; what is left over is zeroes.
    auto replace = [&](const std::pair<uint32_t, uint32_t>& m, const void* data, size_t size) {
        memmove(r + m.first, data, size);
        memset(r + m.first + size, 0, m.second - size);
    };
    // The blocks, from a copy of the room's (the original is overwritten first).
    const std::vector<uint8_t> source(blk, blk + p + perm + props + mdl + bdhc);
    for (int b = 0; b < 4; b++)
    {
        std::vector<uint8_t> block(source.begin(), source.begin() + p);
        put32(block.data() + 4, 0); // no props
        for (int z = 0; z < 32; z++)
            for (int x = 0; x < 32; x++)
            {
                const int gx = x + 32 * (b % 2) - OffX, gz = z + 32 * (b / 2) - OffZ;
                const bool open = gx >= 0 && gz >= TopZ && gx < GridW && gz < GridH && !(RomGet32(r + grid[0].first + 4 * (gz * GridW + gx)) & 1);
                block.push_back(0);
                block.push_back(open ? 0 : 0x80);
            }
        const size_t modelAt = block.size();
        const uint8_t* m = source.data() + p + perm + props;
        block.insert(block.end(), m, m + mdl);
        block[modelAt + sbc] = 0x01; // the structure code's "return": the block draws nothing
        block.insert(block.end(), m + mdl, m + mdl + bdhc); // flat, as the room's
        replace(land[Blocks[b]], block.data(), block.size());
    }
    // The matrix: 2 x 2, no headers or heights, a 5-letter name, the four blocks.
    uint8_t mat[18] = { 2, 2, 0, 0, 5, 'm', '_', 'h', 'i', 'r' };
    for (int b = 0; b < 4; b++) put16(mat + 10 + 2 * b, Blocks[b]);
    replace(matrices[280], mat, sizeof(mat));
    replace(events[468], ev, sizeof(ev));
    std::vector<uint8_t> script(sizeof(roomScripts));
    for (size_t i = 0; i < sizeof(roomScripts) / 2; i++) put16(&script[2 * i], roomScripts[i]);
    replace(scripts[147], script.data(), script.size());
    put16(hdr + 10, 2);
    put16(warp + 6, ArriveX + OffX);
    put16(warp + 8, ArriveZ + OffZ);
    for (const Text& t : rewritten)
    {
        put32(bank + t.at, (uint32_t)t.codes.size() ^ t.keyed); // the line's length field (t.at is from the bank's start)
        const uint32_t i = (t.at - 8) / 8 + 1;
        uint16_t k = (uint16_t)(i * 596947);
        for (size_t c = 0; c < t.codes.size(); c++, k = (uint16_t)(k + 18749)) put16(bank + t.off + 2 * c, t.codes[c] ^ k);
    }
    fprintf(stderr, "[plaza] the Wi-Fi Plaza is map %d\n", MapId);
    return true;
}
}
