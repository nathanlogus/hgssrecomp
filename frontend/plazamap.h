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
//   - land block 378 (the room's only block, used by no other map): who may walk where,
//     from the Plaza's own grid (a/2/0/0 member 0); the block's model draws nothing, the
//     room is drawn by the frontend (WorldBlocks::EmitPlaza) from the Plaza's models.
//   - events 468: no people, one trigger across the bottom of the entrance corridor.
//   - scripts 147: that trigger's script, the game's own way back from the Plaza (warp
//     to the basement the player came from, whose header then walks them out of the
//     door); header 400: none (the room's walk to the Plaza program is gone).
//   - scripts 164 (every basement's receptionist): the warp lands at the corridor.
// The map is one block (32 x 32 tiles) and the Plaza's room with its corridor 35 x 36:
// the outermost columns and the end of the corridor are not walkable, but drawn.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "ndsrom.h"

namespace PlazaMap
{
constexpr int MapId = 516;
// Plaza grid tile (x, z) is map tile (x + OffX, z + OffZ).
constexpr int OffX = -2, OffZ = -6;
constexpr int GridW = 35, GridH = 42;
// Where the receptionist's warp lands (facing north) and the row that leads out, in
// grid tiles: the corridor below the room.
constexpr int ArriveX = 18, ArriveZ = 36, ExitX = 17, ExitW = 3, ExitZ = 37;

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
    const auto events = NdsNarcFiles(r, n, "a/0/3/2");
    const auto scripts = NdsNarcFiles(r, n, "a/0/1/2");
    auto fail = [](const char* why) { fprintf(stderr, "[plaza] not set up: %s\n", why); return false; };
    if (grid.empty() || grid[0].second != GridW * GridH * 4 || land.size() <= 378 || events.size() <= 468 || scripts.size() <= 400)
        return fail("not the expected files");

    // The land block: sizes of permissions, props, model and terrain (BDHC).
    uint8_t* blk = r + land[378].first;
    const uint32_t blkSize = land[378].second;
    uint32_t perm = RomGet32(blk), props = RomGet32(blk + 4), mdl = RomGet32(blk + 8), bdhc = RomGet32(blk + 12);
    uint32_t p = 16;
    if (RomGet16(blk + p) == 0x1234) p += 4 + RomGet16(blk + p + 2);
    if (perm != 32 * 32 * 2 || (uint64_t)p + perm + props + mdl + bdhc > blkSize || memcmp(blk + p + perm + props, "BMD0", 4) != 0)
        return fail("land block");
    // The model's structure code (NitroSystem MDL0), to end at its first byte.
    const uint8_t* model = blk + p + perm + props;
    const uint32_t mdl0 = RomGet32(model + 0x10);
    if (mdl0 + 0x20 > mdl || memcmp(model + mdl0, "MDL0", 4) != 0) return fail("land model");
    const uint8_t* dict = model + mdl0 + 8;
    const uint32_t mo = mdl0 + RomGet32(dict + RomGet16(dict + 6) + 4);
    const uint32_t sbc = mo + RomGet32(model + mo + 4);
    if (sbc >= mdl) return fail("land model");

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

    // The exit trigger's script (each command a 16-bit number and its arguments).
    const uint16_t exitScript[] = {
        2, 0, 0xFD13,          // the offset of script 1 (from after the offset), end of list
        96,                    // LockAll
        174, 6, 1, 0, 0,       // FadeScreen to black
        175,                   // WaitFade
        41, 0x4137, 1,         // SetVar VAR_UNK_4137, 1: the basement walks the player out
        176, 0x4056, 0, 5, 2, 1, // Warp to VAR_UNK_4056 (the basement it was entered from), 5, 2, south
        174, 6, 1, 1, 0,       // FadeScreen from black
        175,                   // WaitFade
        97,                    // ReleaseAll
        2,                     // End
    };
    // Zone events: no background events, people or warps; one trigger (script 1, x, z,
    // width, height, y, value, variable: runs while VAR_TEMP_x4003 is 0, as on arrival).
    uint8_t ev[32] = {};
    put32(ev + 12, 1);
    const uint16_t coord[8] = { 1, ExitX + OffX, ExitZ + OffZ, ExitW, 1, 0, 0, 0x4003 };
    for (int i = 0; i < 8; i++) put16(ev + 16 + 2 * i, coord[i]);
    if (events[468].second < sizeof(ev) || scripts[147].second < sizeof(exitScript) || scripts[399].second > scripts[400].second)
        return fail("event or script sizes");

    // Everything checked: write. Each file keeps its size; what is left over is zeroes.
    auto replace = [&](const std::pair<uint32_t, uint32_t>& m, const void* data, size_t size) {
        memmove(r + m.first, data, size);
        memset(r + m.first + size, 0, m.second - size);
    };
    std::vector<uint8_t> block(blk, blk + p);
    put32(block.data() + 4, 0); // no props
    for (int z = 0; z < 32; z++)
        for (int x = 0; x < 32; x++)
        {
            const int gx = x - OffX, gz = z - OffZ;
            const bool open = gx >= 0 && gz >= 0 && gx < GridW && gz < GridH && !(RomGet32(r + grid[0].first + 4 * (gz * GridW + gx)) & 1);
            block.push_back(0);
            block.push_back(open ? 0 : 0x80);
        }
    const size_t modelAt = block.size();
    block.insert(block.end(), model, model + mdl);
    block[modelAt + sbc] = 0x01; // the structure code's "return": the block draws nothing
    block.insert(block.end(), model + mdl, model + mdl + bdhc);
    replace(land[378], block.data(), block.size());
    replace(events[468], ev, sizeof(ev));
    std::vector<uint8_t> script(sizeof(exitScript));
    for (size_t i = 0; i < sizeof(exitScript) / 2; i++) put16(&script[2 * i], exitScript[i]);
    replace(scripts[147], script.data(), script.size());
    const std::vector<uint8_t> noHeader(r + scripts[399].first, r + scripts[399].first + scripts[399].second);
    replace(scripts[400], noHeader.data(), noHeader.size());
    put16(warp + 6, ArriveX + OffX);
    put16(warp + 8, ArriveZ + OffZ);
    fprintf(stderr, "[plaza] the Wi-Fi Plaza is map %d\n", MapId);
    return true;
}
}
