#pragma once
// Extra map blocks for the widescreen world. The game keeps only the 2x2 map blocks
// around the player loaded, which is all the DS's own view ever reaches; a wide or
// zoomed-out view reaches past them. Here the blocks around those are read from the ROM
// and drawn into the wide picture only (WideState::Extra), with the camera, lighting
// colours and texture placement the game uses for its own blocks this frame: the ground
// models from the ROM, and the buildings and other props with the game's own copies of
// their models (already in memory for the whole area). People standing on neighbouring
// maps, which the game only creates once the player enters their map, are drawn too, as
// "ghosts" (only seen, never there for the game): their sprites from the ROM, standing
// where the map's event data puts them. Nothing is written to the game's memory.
//
// Addresses and layouts are those of HeartGold/SoulSilver (US) and Definitive HeartGold,
// found with the pret decompilation; everything is checked before use, and anything that
// does not look as expected (another game, a cutscene, a map being loaded) draws nothing.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "NDS.h"
#include "ndsrom.h"
#include "GPU3D_Wide.h"
#include "nightfx.h"

class WorldBlocks
{
public:
    int Radius = 1; // blocks around the camera's block: 1 = 3x3

    void SetRom(const std::vector<uint8_t>& rom)
    {
        Rom = rom.data();
        RomSize = rom.size();
        Land.clear();
        Blocks.clear();
        Land = NdsNarcFiles(Rom, RomSize, "a/0/6/5");
        if (Land.empty()) return;
        DrawLoadedToo = getenv("SSR_FARMAP_ALL") != nullptr;
        Events = NarcMembers("a/0/3/2");
        Sprites = NarcMembers("a/0/8/1");
        ZoneObjects.clear();
        SpriteTex.clear();
        melonDS::Wide.ExternalTextures.clear();
    }

    void Build(melonDS::NDS& nds, std::vector<uint32_t>& out)
    {
        if (Land.empty()) return;
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        const uint32_t fs = Read32(FieldSystemPtr);
        if (!InRam(fs)) return;
        const uint32_t loader = Read32(fs + 0x2C), matrix = Read32(fs + 0x30), area = Read32(fs + 0x34);
        if (!InRam(loader) || !InRam(matrix) || !InRam(area)) return;
        // The overworld (towns, routes, caves) and the Safari Zone; not the Battle Tower,
        // the colosseums or the Union Room.
        const uint32_t loadType = Read32(fs + 0x70);
        if (loadType > 1) return;
        const bool props = loadType == 0; // the Safari Zone's props come from the save
        const int width = Read8(matrix), height = Read8(matrix + 1);
        if (width < 1 || height < 1 || width * height > 799) return;
        const uint32_t altitudes = matrix + 4 + 2 + 799 * 2, models = (altitudes + 799 + 1) & ~1u;

        // Blocks the game has loaded (and draws itself), and its prop model manager.
        int loaded[4];
        uint32_t propModels = 0;
        for (int k = 0; k < 4; k++)
        {
            const uint32_t slot = Read32(loader + 0x90 + 4 * k);
            loaded[k] = InRam(slot) && Read32(slot + 0x864) == 1 ? (int32_t)Read32(slot + 0x860) : -1;
            if (InRam(slot) && !propModels)
            {
                const uint32_t mgr = Read32(slot + 0x868);
                if (InRam(mgr) && InRam(Read32(mgr + 0x700))) propModels = Read32(mgr + 0x700);
            }
        }
        // The block the camera looks at.
        const int32_t tx = (int32_t)Read32(Glb + 0x258), tz = (int32_t)Read32(Glb + 0x260);
        const int cx = (tx >> 16) / 32, cz = (tz >> 16) / 32;
        if (tx < 0 || tz < 0 || cx >= width || cz >= height) return;

        const uint32_t tex0 = Read32(area + 0x8A4);
        if (!InRam(tex0) || memcmp(&Ram[tex0 & RamMask], "TEX0", 4) != 0) return;
        if (!BindTextures(tex0)) return;
        if (propModels != PropManager)
        {
            PropManager = propModels;
            Props.clear();
        }

        const size_t start = out.size();
        EmitProjection(out);
        Colours[0] = Read32(Glb + 0x94);
        Colours[1] = Read32(Glb + 0x98);
        Colours[2] = Read32(Glb + 0x9C);

        int drawn = 0;
        for (int z = cz - Radius; z <= cz + Radius; z++)
            for (int x = cx - Radius; x <= cx + Radius; x++)
            {
                if (x < 0 || z < 0 || x >= width || z >= height) continue;
                const int cell = z * width + x;
                if (!DrawLoadedToo && (cell == loaded[0] || cell == loaded[1] || cell == loaded[2] || cell == loaded[3])) continue;
                const Block* b = GetBlock(Read16(models + 2 * cell));
                if (!b) continue;
                const int32_t bx = x * 0x200000 + 0x100000, bz = z * 0x200000 + 0x100000;
                // The ground: camera, then the block's place (as the game does it).
                EmitPlaced(b->ground, bx, Read8(altitudes + cell) << 15, bz, nullptr, out);
                // Its props, at their place in the block (the game adds only x and z).
                if (props && PropManager)
                    for (const PropPlace& p : b->props)
                        if (const Model* m = GetProp(p.buildModel))
                            EmitPlaced(*m, bx + p.pos[0], p.pos[1], bz + p.pos[2], p.scale, out);
                drawn++;
            }
        if (props) drawn += EmitGhosts(fs, matrix, width, height, cx, cz, out);
        if (!drawn) out.resize(start);
    }

    // Fireflies over tall grass and woods around the camera, placed in the world (so they
    // stay where they are while the view moves) and given as places in the wide picture,
    // whose view is `viewWidth` DS pixels wide and zoomed by `zoom` (see WideState).
    // Whether the player is outdoors, where the game shows the time of day: the overworld's
    // map matrix (number 0) or the Safari Zone. Houses, caves and other interiors have
    // matrices of their own and look the same at any hour.
    bool Outdoors(melonDS::NDS& nds)
    {
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        const uint32_t fs = Read32(FieldSystemPtr);
        if (!InRam(fs)) return false;
        const uint32_t matrix = Read32(fs + 0x30);
        return InRam(matrix) && (Read8(matrix + 2) == 0 || Read32(fs + 0x70) == 1);
    }

    void Fireflies(melonDS::NDS& nds, float seconds, float viewWidth, float zoom, std::vector<NightFly>& out)
    {
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        const uint32_t fs = Read32(FieldSystemPtr);
        if (Land.empty() || !InRam(fs)) return;
        const uint32_t matrix = Read32(fs + 0x30);
        if (!InRam(matrix) || Read32(fs + 0x70) > 1) return;
        const int width = Read8(matrix), height = Read8(matrix + 1);
        if (width < 1 || height < 1 || width * height > 799) return;
        const uint32_t altitudes = matrix + 4 + 2 + 799 * 2, models = (altitudes + 799 + 1) & ~1u;
        const int tx = (int32_t)Read32(Glb + 0x258) >> 16, tz = (int32_t)Read32(Glb + 0x260) >> 16;
        double cam[12], proj[16];
        for (int i = 0; i < 12; i++) cam[i] = (int32_t)Read32(Glb + 0x4C + 4 * i) / 4096.0;
        for (int i = 0; i < 16; i++) proj[i] = (int32_t)Read32(Glb + 0x08 + 4 * i) / 4096.0;
        const double sx = 256.0 / std::max(256.0f, viewWidth) * zoom;
        for (int z = tz - 22; z <= tz + 14; z++)
            for (int x = tx - 28; x <= tx + 28; x++)
            {
                if (x < 0 || z < 0 || x >= width * 32 || z >= height * 32) continue;
                uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)z * 19349663u;
                h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                if (h % 100 >= 10) continue;
                const int cell = (z / 32) * width + x / 32;
                const Block* b = GetBlock(Read16(models + 2 * cell));
                if (!b || b->behaviour.empty()) continue;
                const uint8_t beh = b->behaviour[(z % 32) * 32 + x % 32];
                if (beh != 2 && beh != 3 && beh != 6) continue; // tall grass, very tall grass, headbutt trees
                // Drifting, bobbing and blinking, each at its own pace.
                const float ph = (h >> 8 & 0xFF) / 40.0f, speed = 0.6f + (h >> 16 & 0x3F) / 90.0f;
                const double fx = x + (h >> 4 & 0xF) / 16.0 + 0.45 * std::sin(seconds * 0.7 * speed + ph);
                const double fz = z + (h >> 20 & 0xF) / 16.0 + 0.45 * std::cos(seconds * 0.55 * speed + ph * 1.3);
                const double fy = 0.4 + 0.5 * (h >> 24 & 0xF) / 15.0 + 0.15 * std::sin(seconds * 1.7 + ph);
                const float blink = 0.5f + 0.5f * std::sin(seconds * 2.3f * speed + ph * 2);
                const float brightness = blink * blink;
                if (brightness < 0.05f) continue;
                // World units: 1.0 = 0x1000, a tile is 16.
                const double w[3] = { fx * 16, (Read8(altitudes + cell) << 15) / 4096.0 + fy * 16, fz * 16 };
                double c[3], clip[4];
                for (int j = 0; j < 3; j++) c[j] = w[0] * cam[j] + w[1] * cam[3 + j] + w[2] * cam[6 + j] + cam[9 + j];
                for (int j = 0; j < 4; j++) clip[j] = c[0] * proj[j] + c[1] * proj[4 + j] + c[2] * proj[8 + j] + proj[12 + j];
                if (clip[3] <= 0) continue;
                const float u = (float)((sx * clip[0] / clip[3] + 1) / 2), v = (float)((1 - zoom * clip[1] / clip[3]) / 2);
                if (u < 0 || u > 1 || v < 0 || v > 1) continue;
                out.push_back({ u, v, brightness });
            }
    }

    // Street lamps and lanterns around the camera (props with a lamp's build model), as
    // places in the wide picture like Fireflies: the oval their light makes on the ground
    // and the lamp's head.
    // A place in the world (world units: a tile is 16) as a place in the wide picture (0 to
    // 1 across and down), with the camera of the frame last drawn; false behind the camera.
    bool PlaceInView(melonDS::NDS& nds, float viewWidth, float zoom, double x, double y, double z, float& u, float& v)
    {
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        return Place(viewWidth, zoom, x, y, z, u, v);
    }

    // Other players' trainers (online), drawn as the game draws a person.
    struct Trainer
    {
        int sprite, facing, frame; // the game's sprite id; 0 north, 1 south, 2 west, 3 east; 0-3 of the walk
        int32_t x, y, z;           // world position (fx32), as the game's map objects keep it
    };
    void EmitTrainers(melonDS::NDS& nds, const std::vector<Trainer>& trainers, std::vector<uint32_t>& out)
    {
        if (trainers.empty() || Sprites.empty()) return;
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        if (!InRam(Read32(FieldSystemPtr)) || Read16(SpriteGfx) != 1) return; // overlay 1 is not where expected
        const size_t start = out.size();
        EmitProjection(out);
        int drawn = 0;
        for (const Trainer& t : trainers)
        {
            const int tex = GetSpriteTexture((uint16_t)t.sprite, t.facing & 3, t.frame & 3);
            if (tex < 0) continue;
            EmitBillboard(tex, t.x, t.y, (int64_t)t.z + 0x6000, out);
            drawn++;
        }
        if (!drawn) out.resize(start);
    }

    void Lights(melonDS::NDS& nds, float viewWidth, float zoom, std::vector<NightLight>& out)
    {
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        const uint32_t fs = Read32(FieldSystemPtr);
        if (Land.empty() || !InRam(fs)) return;
        const uint32_t matrix = Read32(fs + 0x30);
        if (!InRam(matrix) || Read32(fs + 0x70) != 0) return;
        const int width = Read8(matrix), height = Read8(matrix + 1);
        if (width < 1 || height < 1 || width * height > 799) return;
        const uint32_t models = ((matrix + 4 + 2 + 799 * 2 + 799 + 1) & ~1u);
        const int cx = ((int32_t)Read32(Glb + 0x258) >> 16) / 32, cz = ((int32_t)Read32(Glb + 0x260) >> 16) / 32;
        auto place = [&](double x, double y, double z, float& u, float& v) { return Place(viewWidth, zoom, x, y, z, u, v); };
        for (int z = cz - Radius; z <= cz + Radius; z++)
            for (int x = cx - Radius; x <= cx + Radius; x++)
            {
                if (x < 0 || z < 0 || x >= width || z >= height) continue;
                const Block* b = GetBlock(Read16(models + 2 * (z * width + x)));
                if (!b) continue;
                for (const PropPlace& p : b->props)
                {
                    if (!IsLamp(p.buildModel)) continue;
                    const double wx = (x * 0x200000 + 0x100000 + p.pos[0]) / 4096.0, wy = p.pos[1] / 4096.0,
                                 wz = (z * 0x200000 + 0x100000 + p.pos[2]) / 4096.0;
                    const double reach = 16 * 3.2; // light reaches about 3 tiles
                    NightLight l;
                    float u0, v0, u1, v1, u2, v2, u3, v3;
                    if (!place(wx, wy, wz, l.u, l.v) || !place(wx - reach, wy, wz, u0, v0) || !place(wx + reach, wy, wz, u1, v1)
                        || !place(wx, wy, wz - reach, u2, v2) || !place(wx, wy, wz + reach, u3, v3)
                        || !place(wx, wy + 16 * 1.6, wz, l.hu, l.hv))
                        continue;
                    l.ru = std::fabs(u1 - u0) / 2;
                    l.rv = std::fabs(v3 - v2) / 2;
                    if (l.u + l.ru < 0 || l.u - l.ru > 1 || l.v + l.rv < 0 || l.v - l.rv > 1) continue;
                    out.push_back(l);
                }
            }
    }

private:
    // Build models that are lamps (pokeheartgold files/fielddata/build_model/bm_field.narc):
    // kk_tourou (Violet's stone lanterns), d15_light, yado_light, ko_light, poke_c_light,
    // d17_light, kbc_sl, en_light, as_light, cl_slht, tafl_light and the Kanto towns' *_sl.
    static bool IsLamp(uint32_t id)
    {
        switch (id)
        {
        case 55: case 68: case 73: case 95: case 182: case 186: case 223: case 227: case 248:
        case 302: case 327: case 329: case 330: case 331: case 332: case 333: case 334:
            return true;
        }
        return false;
    }

    // The game's projection, as it loaded it for its own blocks.
    void EmitProjection(std::vector<uint32_t>& out)
    {
        out.push_back(0x10); out.push_back(0);
        out.push_back(0x16);
        for (int i = 0; i < 16; i++) out.push_back(Read32(Glb + 0x08 + 4 * i));
        out.push_back(0x10); out.push_back(2);
        out.push_back(0x60); out.push_back(0xBFFF0000); // the whole screen
    }

    bool Place(float viewWidth, float zoom, double x, double y, double z, float& u, float& v)
    {
        double cam[12], proj[16];
        for (int i = 0; i < 12; i++) cam[i] = (int32_t)Read32(Glb + 0x4C + 4 * i) / 4096.0;
        for (int i = 0; i < 16; i++) proj[i] = (int32_t)Read32(Glb + 0x08 + 4 * i) / 4096.0;
        const double sx = 256.0 / std::max(256.0f, viewWidth) * zoom;
        double c[3], clip[4];
        for (int j = 0; j < 3; j++) c[j] = x * cam[j] + y * cam[3 + j] + z * cam[6 + j] + cam[9 + j];
        for (int j = 0; j < 4; j++) clip[j] = c[0] * proj[j] + c[1] * proj[4 + j] + c[2] * proj[8 + j] + proj[12 + j];
        if (clip[3] <= 0) return false;
        u = (float)((sx * clip[0] / clip[3] + 1) / 2);
        v = (float)((1 - zoom * clip[1] / clip[3]) / 2);
        return true;
    }

    static constexpr uint32_t FieldSystemPtr = 0x021D1110; // the game's FieldSystem pointer
    static constexpr uint32_t MapHeaders = 0x020F6BE0;     // sMapHeaders: 24 bytes per map
    static constexpr uint32_t SpriteGfx = 0x022074A8;      // overlay 1: sprite id -> overworld graphics

    struct Person
    {
        int x, z;       // tile
        int32_t y;      // height, world units
        int facing;     // 0 north, 1 south, 2 west, 3 east
        uint16_t sprite, flag;
    };
    std::vector<std::pair<uint32_t, uint32_t>> Events, Sprites; // ROM archives' members
    std::map<uint16_t, std::vector<Person>> ZoneObjects;         // by events bank
    std::map<uint32_t, int> SpriteTex;                           // (member << 4 | facing << 2 | frame) -> external texture, -1 if none

    std::vector<std::pair<uint32_t, uint32_t>> NarcMembers(const char* path) const { return NdsNarcFiles(Rom, RomSize, path); }

    // The people of a map, from its zone event file: background events, then the objects
    // (32 bytes each: id, sprite, movement, type, flag, script, facing, ..., x, z, y).
    const std::vector<Person>& GetPeople(uint16_t bank)
    {
        auto it = ZoneObjects.find(bank);
        if (it != ZoneObjects.end()) return it->second;
        std::vector<Person>& people = ZoneObjects[bank];
        if (bank >= Events.size()) return people;
        const uint8_t* f = Rom + Events[bank].first;
        const uint32_t size = Events[bank].second;
        if (size < 8) return people;
        const uint32_t bgs = Get32(f);
        uint32_t p = 4 + bgs * 20;
        if (p + 4 > size) return people;
        const uint32_t objs = Get32(f + p);
        p += 4;
        for (uint32_t i = 0; i < objs && p + 32 <= size; i++, p += 32)
        {
            const uint8_t* o = f + p;
            const uint16_t script = Get16(o + 0x0A);
            if (script == 0xFFFF) continue; // its flag field holds a map id: not a person
            Person person;
            person.sprite = Get16(o + 2);
            person.flag = Get16(o + 8);
            person.facing = (int16_t)Get16(o + 0x0C) & 3;
            person.x = Get16(o + 0x18);
            person.z = Get16(o + 0x1A);
            person.y = (int32_t)Get32(o + 0x1C);
            people.push_back(person);
        }
        return people;
    }

    // A person's frame as an external texture (Wide.ExternalTextures), from the overworld
    // sprite archive: 16 frames of 32 x 32, four for each way they face (".1" to ".4" north,
    // ".5" south, ".9" west and ".13" east): standing, a step, standing, the other step.
    // 16 colours with colour 0 transparent.
    int GetSpriteTexture(uint16_t sprite, int facing, int frame = 0)
    {
        // The game's table: sprite id, archive member, and how it is drawn (bits 5-9: 15
        // is a 3D model, not a billboard).
        uint32_t member = 0xFFFFFFFF;
        for (int i = 0; i < 1024; i++)
        {
            const uint32_t e = SpriteGfx + 6 * i;
            const uint16_t id = Read16(e);
            if (id == 0xFFFF) break;
            if (id == sprite)
            {
                if (((Read16(e + 4) >> 5) & 0x1F) != 15) member = Read16(e + 2);
                break;
            }
        }
        if (member >= Sprites.size() || member > 0x3FFF) return -1;
        const uint32_t key = member << 4 | (uint32_t)facing << 2 | (uint32_t)frame;
        auto it = SpriteTex.find(key);
        if (it != SpriteTex.end()) return it->second;
        int& index = SpriteTex[key];
        index = -1;
        const uint8_t* b = Rom + Sprites[member].first;
        const uint32_t size = Sprites[member].second;
        if (size < 0x40 || memcmp(b, "BTX0", 4) != 0) return -1;
        const uint32_t t0 = Get32(b + 0x10);
        if (t0 + 0x3C > size) return -1;
        const uint8_t* t = b + t0;
        const uint32_t ofsTex = Get32(t + 0x14), ofsPltt = Get32(t + 0x38);
        const std::string wanted = "." + std::to_string(facing * 4 + 1 + frame);
        const uint8_t* texEntry = nullptr;
        for (const auto& e : Dict(t + Get16(t + 0x0E)))
        {
            const size_t dot = e.first.rfind('.');
            if (dot != std::string::npos && e.first.substr(dot) == wanted) texEntry = e.second;
        }
        const auto pals = Dict(t + Get16(t + 0x34));
        if (!texEntry || pals.empty()) return -1;
        const uint32_t param = Get32(texEntry);
        const int w = 8 << ((param >> 20) & 7), h = 8 << ((param >> 23) & 7);
        if (((param >> 26) & 7) != 3) return -1; // 16 colours, as all of them are
        const uint8_t* pix = t + ofsTex + ((param & 0xFFFF) << 3);
        const uint8_t* pal = t + ofsPltt + (Get16(pals[0].second) << 3);
        if (pix + w * h / 2 > b + size || pal + 32 > b + size) return -1;
        melonDS::WideState::ExternalTexture tex;
        tex.W = w;
        tex.H = h;
        tex.Texels.resize((size_t)w * h);
        for (int i = 0; i < w * h; i++)
        {
            const int c = (pix[i / 2] >> ((i & 1) * 4)) & 15;
            const uint16_t col = Get16(pal + 2 * c);
            uint32_t r = (col << 1) & 0x3E, g = (col >> 4) & 0x3E, bl = (col >> 9) & 0x3E;
            if (r) r++;
            if (g) g++;
            if (bl) bl++;
            tex.Texels[i] = 0x80000000 | r | (g << 8) | (bl << 16) | (c ? 31u << 24 : 0);
        }
        index = (int)melonDS::Wide.ExternalTextures.size();
        melonDS::Wide.ExternalTextures.push_back(std::move(tex));
        return index;
    }

    // People of the maps around the camera other than the current one, drawn standing,
    // as the game draws a person: a 32 x 32 quad (2 x 2 tiles) facing the screen, its
    // bottom at the middle of the person's tile, a little towards the camera, lit by the
    // game's light. Returns how many were drawn.
    int EmitGhosts(uint32_t fs, uint32_t matrix, int width, int height, int cx, int cz, std::vector<uint32_t>& out)
    {
        const uint32_t location = Read32(fs + 0x20);
        if (!InRam(location) || Events.empty() || Sprites.empty()) return 0;
        const uint32_t current = Read32(location);
        const uint32_t flags = location - 0x1234 + 0x10C4; // the save's flags, next to the location
        if (Read16(SpriteGfx) != 1) return 0;             // overlay 1 is not where expected
        std::vector<uint16_t> maps;
        for (int z = cz - Radius; z <= cz + Radius; z++)
            for (int x = cx - Radius; x <= cx + Radius; x++)
            {
                if (x < 0 || z < 0 || x >= width || z >= height) continue;
                const uint16_t header = Read16(matrix + 6 + 2 * (z * width + x));
                if (header != current && header < 540 && std::find(maps.begin(), maps.end(), header) == maps.end())
                    maps.push_back(header);
            }
        const int x0 = (cx - Radius) * 32, x1 = (cx + Radius + 1) * 32, z0 = (cz - Radius) * 32, z1 = (cz + Radius + 1) * 32;
        const uint32_t altitudes = matrix + 4 + 2 + 799 * 2;
        const int64_t groundY = (int32_t)Read32(Glb + 0x25C);
        const int playerCell = cz * width + cx;
        int drawn = 0;
        for (uint16_t header : maps)
        {
            const uint16_t bank = Read16(MapHeaders + 24 * header + 0x10);
            for (const Person& person : GetPeople(bank))
            {
                if (person.x < x0 || person.x >= x1 || person.z < z0 || person.z >= z1) continue;
                if (person.sprite >= 101 && person.sprite <= 117) continue; // chosen by the game at run time
                // Hidden by a flag (map-temporary flags 1-63 start cleared on every map).
                if (person.flag >= 64 && person.flag < 0x4000 && (Read8(flags + person.flag / 8) >> (person.flag % 8) & 1)) continue;
                const int tex = GetSpriteTexture(person.sprite, person.facing);
                if (tex < 0) continue;
                // Bottom centre in camera space.
                // The game puts people on the terrain, not at the event data's height (mostly
                // 0): the height of the ground the player stands on (the camera's target),
                // moved by the difference in block altitude.
                // shortcut: flat blocks only, a person on a hill or a bridge stands at the block's
                // base height; read the block's terrain heights (BDHC) if that shows.
                const int ghostCell = (person.z / 32) * width + person.x / 32;
                const int64_t wy = person.y ? person.y
                    : groundY + ((int64_t)(Read8(altitudes + ghostCell) - Read8(altitudes + playerCell)) << 15);
                const int64_t wx = (int64_t)person.x * 0x10000 + 0x8000;
                const int64_t wz = (int64_t)person.z * 0x10000 + 0x8000 + 0x6000;
                EmitBillboard(tex, wx, wy, wz, out);
                drawn++;
            }
        }
        return drawn;
    }
    // A person's 32 x 32 quad (2 x 2 tiles) facing the screen, its bottom centre at world
    // position (wx, wy, wz) (fx32), lit by the game's light.
    void EmitBillboard(int tex, int64_t wx, int64_t wy, int64_t wz, std::vector<uint32_t>& out)
    {
        int64_t v[3];
        for (int j = 0; j < 3; j++)
        {
            const int64_t c0 = (int32_t)Read32(Glb + 0x4C + 4 * j), c1 = (int32_t)Read32(Glb + 0x4C + 12 + 4 * j);
            const int64_t c2 = (int32_t)Read32(Glb + 0x4C + 24 + 4 * j), c3 = (int32_t)Read32(Glb + 0x4C + 36 + 4 * j);
            v[j] = ((wx * c0 + wy * c1 + wz * c2) >> 12) + c3;
        }
        // Facing the screen: the quad is laid out in camera space.
        out.push_back(0x10); out.push_back(2);
        const uint32_t m[12] = { 0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000, (uint32_t)v[0], (uint32_t)v[1], (uint32_t)v[2] };
        out.push_back(0x17);
        out.insert(out.end(), m, m + 12);
        out.push_back(0x10); out.push_back(1);
        out.push_back(0x1B); out.push_back(0x10000); out.push_back(0x10000); out.push_back(0x10000);
        out.push_back(0x10); out.push_back(2);
        // White ambient, no diffuse: the colour is that of the game's first light.
        out.push_back(0x30); out.push_back(0x7FFF0000);
        out.push_back(0x31); out.push_back(0);
        out.push_back(0x29); out.push_back(0x001F80C1); // light 0, both sides, opaque, fog
        out.push_back(0x2A); out.push_back(melonDS::WideState::ExternalTexParam + (uint32_t)tex);
        out.push_back(0x2B); out.push_back(0);
        out.push_back(0x21); out.push_back(0x1FF00000); // facing the camera
        out.push_back(0x40); out.push_back(1);
        const int16_t corner[4][2] = { { -0x1000, 0x2000 }, { -0x1000, 0 }, { 0x1000, 0 }, { 0x1000, 0x2000 } };
        const uint16_t st[4][2] = { { 0, 0 }, { 0, 512 }, { 512, 512 }, { 512, 0 } };
        for (int k = 0; k < 4; k++)
        {
            out.push_back(0x22); out.push_back((uint32_t)st[k][0] | ((uint32_t)st[k][1] << 16));
            out.push_back(0x23);
            out.push_back((uint16_t)corner[k][0] | ((uint32_t)(uint16_t)corner[k][1] << 16));
            out.push_back(0);
        }
        out.push_back(0x41);
    }

    static constexpr uint32_t Glb = 0x021DA49C;            // NitroSystem's NNS_G3dGlb (camera, projection)

    struct Material
    {
        std::string name, tex, pltt;
        uint32_t difAmb = 0, speEmi = 0, polyAttr = 0, polyMask = 0, texImage = 0;
        uint16_t plttBase = 0;
        bool bound = false; // texImage and plttBase already hold the video memory places
    };
    struct Node
    {
        bool move = false, scale = false;
        uint32_t mtx[12] = {}; // rotation rows and translation, as MTX_MULT_4x3 takes them
        uint32_t s[3] = {};
    };
    enum { OpMat, OpShape, OpNode, OpRestore, OpPosScale };
    struct Op { int kind, a, b, c; };
    struct Model
    {
        int32_t posScale = 0x1000;
        std::vector<Material> mats;
        std::vector<Node> nodes;
        std::vector<std::vector<uint32_t>> shapes; // display lists as commands and parameters
        std::vector<Op> ops;                       // the structure byte code, in drawing order
    };
    struct PropPlace
    {
        uint32_t buildModel;
        int32_t pos[3];
        uint32_t scale[3];
    };
    struct Block
    {
        Model ground;
        std::vector<PropPlace> props;
        std::vector<uint8_t> behaviour; // per tile (32 x 32): tall grass, trees, water...
    };

    const uint8_t* Rom = nullptr;
    size_t RomSize = 0;
    bool DrawLoadedToo = false;
    std::vector<std::pair<uint32_t, uint32_t>> Land; // ROM offset and size of each map block file
    std::map<uint16_t, Block> Blocks;                 // parsed on first use
    std::map<uint16_t, bool> BadBlocks;
    std::map<uint32_t, Model> Props;                  // build model -> the game's model, parsed from RAM
    std::map<uint32_t, bool> BadProps;
    uint32_t PropManager = 0;
    uint8_t* Ram = nullptr;
    uint32_t RamMask = 0;
    uint32_t Colours[3] = {};
    uint32_t BoundTex0 = 0, BoundKeys = 0;
    std::map<std::string, uint32_t> TexParams; // texture name -> its TEXIMAGE_PARAM placement bits
    std::map<std::string, uint32_t> Palettes;  // palette name -> its address in palette memory

    static uint32_t Get32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
    static uint16_t Get16(const uint8_t* p) { uint16_t v; memcpy(&v, p, 2); return v; }
    bool InRam(uint32_t a) const { return a >= 0x02000000 && a < 0x02400000; }
    uint32_t Read32(uint32_t a) const { return Get32(&Ram[a & RamMask & ~3u]); }
    uint16_t Read16(uint32_t a) const { return Get16(&Ram[a & RamMask & ~1u]); }
    uint8_t Read8(uint32_t a) const { return Ram[a & RamMask]; }

    // A file of the ROM's file system by path.
    bool RomFile(const char* path, uint32_t& off, uint32_t& size) const { return NdsRomFile(Rom, RomSize, path, off, size); }

    // NitroSystem resource dictionary at `d`: (name, entry data) pairs.
    static std::vector<std::pair<std::string, const uint8_t*>> Dict(const uint8_t* d)
    {
        std::vector<std::pair<std::string, const uint8_t*>> out;
        const int count = d[1];
        const uint8_t* info = d + Get16(d + 6);
        const uint16_t unit = Get16(info), names = Get16(info + 2);
        for (int i = 0; i < count; i++)
        {
            const char* nm = (const char*)info + names + 16 * i;
            out.push_back({ std::string(nm, strnlen(nm, 16)), info + 4 + unit * i });
        }
        return out;
    }

    const Block* GetBlock(uint16_t id)
    {
        auto it = Blocks.find(id);
        if (it != Blocks.end()) return &it->second;
        if (id >= Land.size() || BadBlocks.count(id)) return nullptr;
        Block b;
        if (!ParseBlock(Land[id].first, Land[id].second, b))
        {
            BadBlocks[id] = true;
            return nullptr;
        }
        return &(Blocks[id] = std::move(b));
    }

    bool ParseBlock(uint32_t off, uint32_t size, Block& b) const
    {
        if (size < 20) return false;
        const uint8_t* f = Rom + off;
        const uint32_t perm = Get32(f), props = Get32(f + 4), mdlSize = Get32(f + 8);
        uint32_t p = 16;
        if (Get16(f + p) == 0x1234) p += 4 + Get16(f + p + 2);
        if (perm >= 32 * 32 * 2 && (uint64_t)p + perm <= size)
        {
            b.behaviour.resize(32 * 32);
            for (int i = 0; i < 32 * 32; i++) b.behaviour[i] = f[p + 2 * i];
        }
        p += perm;
        if ((uint64_t)p + props + mdlSize > size) return false;
        // Props: build model, translation, rotation (not used by the game), scale; 0x30 each.
        for (uint32_t k = 0; k + 0x30 <= props && b.props.size() < 32; k += 0x30)
        {
            const uint8_t* e = f + p + k;
            PropPlace pp;
            pp.buildModel = Get32(e);
            for (int i = 0; i < 3; i++)
            {
                pp.pos[i] = (int32_t)Get32(e + 4 + 4 * i);
                pp.scale[i] = Get32(e + 28 + 4 * i);
            }
            b.props.push_back(pp);
        }
        p += props;
        return ParseModel(f + p, mdlSize, false, b.ground);
    }

    // The game's own copy of a prop's model (in its model manager, with its textures bound).
    const Model* GetProp(uint32_t buildModel)
    {
        auto it = Props.find(buildModel);
        if (it != Props.end()) return &it->second;
        if (BadProps.count(buildModel)) return nullptr;
        const uint32_t indexMax = Read32(PropManager + 0x14), files = Read32(PropManager + 4);
        uint32_t file = 0;
        if (buildModel < indexMax && buildModel < 0x400 && InRam(files)) file = Read32(files + 4 * buildModel);
        Model m;
        bool ok = InRam(file) && memcmp(&Ram[file & RamMask], "BMD0", 4) == 0;
        if (ok)
        {
            const uint32_t size = Read32(file + 8);
            ok = size >= 0x40 && size < 0x100000 && (file & RamMask) + size <= RamMask + 1;
            if (ok)
            {
                const uint8_t* b = &Ram[file & RamMask];
                // The model part only: the textures were loaded into video memory and bound.
                ok = ParseModel(b, size, true, m);
            }
        }
        if (!ok)
        {
            BadProps[buildModel] = true;
            return nullptr;
        }
        return &(Props[buildModel] = std::move(m));
    }

    bool ParseModel(const uint8_t* b, uint32_t size, bool bound, Model& m) const
    {
        if (size < 0x40 || memcmp(b, "BMD0", 4) != 0) return false;
        const uint8_t* end = b + size;
        const uint8_t* mdl0 = b + Get32(b + 0x10);
        if (mdl0 + 0x20 > end || memcmp(mdl0, "MDL0", 4) != 0) return false;
        const auto models = Dict(mdl0 + 8);
        if (models.empty()) return false;
        const uint8_t* mo = mdl0 + Get32(models[0].second);
        const uint8_t* sbc = mo + Get32(mo + 4);
        const uint8_t* mat = mo + Get32(mo + 8);
        const uint8_t* shp = mo + Get32(mo + 12);
        if (sbc >= end || mat >= end || shp >= end) return false;
        m.posScale = (int32_t)Get32(mo + 20 + 8);

        // Node transforms (the model info is 0x2C bytes, the node dictionary follows it).
        const uint8_t* nodeInfo = mo + 20 + 0x2C;
        for (const auto& e : Dict(nodeInfo))
        {
            const uint8_t* d = nodeInfo + Get32(e.second);
            if (d + 4 > end) return false;
            m.nodes.push_back(ReadNode(d));
        }
        for (const auto& e : Dict(mat + 4))
        {
            const uint8_t* d = mat + Get32(e.second);
            Material x;
            x.name = e.first;
            x.difAmb = Get32(d + 4);
            x.speEmi = Get32(d + 8);
            x.polyAttr = Get32(d + 12);
            x.polyMask = Get32(d + 16);
            x.texImage = Get32(d + 20);
            x.plttBase = Get16(d + 28);
            x.bound = bound;
            m.mats.push_back(x);
        }
        // Texture and palette names, per material.
        for (int which = 0; which < 2; which++)
            for (const auto& e : Dict(mat + Get16(mat + 2 * which)))
            {
                const uint16_t list = Get16(e.second);
                const int n = e.second[2];
                for (int k = 0; k < n; k++)
                {
                    const int mi = mat[list + k];
                    if (mi < (int)m.mats.size()) (which ? m.mats[mi].pltt : m.mats[mi].tex) = e.first;
                }
            }
        for (const auto& e : Dict(shp))
        {
            const uint8_t* s = shp + Get32(e.second);
            const uint8_t* dl = s + Get32(s + 8);
            const uint32_t dlSize = Get32(s + 12);
            if (dl + dlSize > end) return false;
            m.shapes.push_back(UnpackDisplayList(dl, dlSize));
        }
        // The structure byte code: nodes, materials and shapes in drawing order.
        for (const uint8_t* c = sbc; c < mat;)
        {
            const uint8_t op = *c & 0x1F, flags = *c >> 5;
            if (op == 0x01) break;
            switch (op)
            {
            case 0x00: c += 1; break;
            case 0x02: c += 3; break;
            case 0x03: m.ops.push_back({ OpRestore, c[1], 0, 0 }); c += 2; break;
            case 0x04: if (c[1] < m.mats.size()) m.ops.push_back({ OpMat, c[1], 0, 0 }); c += 2; break;
            case 0x05: if (c[1] < m.shapes.size()) m.ops.push_back({ OpShape, c[1], 0, 0 }); c += 2; break;
            case 0x06:
            {
                // node, parent, flags, then the stack slot to store to and/or restore from
                int k = 4, dest = -1, src = -1;
                if (flags & 1) dest = c[k++];
                if (flags & 2) src = c[k++];
                if (c[1] < m.nodes.size()) m.ops.push_back({ OpNode, c[1], dest, src });
                c += k;
                break;
            }
            case 0x07: case 0x08: c += 2 + ((flags & 1) ? 1 : 0) + ((flags & 2) ? 1 : 0); break; // billboards: drawn as is
            case 0x0B: if (!(flags & 1)) m.ops.push_back({ OpPosScale, 0, 0, 0 }); c += 1; break;
            default: return false; // skinning, display list calls, environment maps
            }
        }
        return !m.ops.empty();
    }

    // NitroSystem node data: flags, then translation, rotation and scale as present.
    static Node ReadNode(const uint8_t* d)
    {
        Node n;
        const uint16_t flag = Get16(d);
        const int32_t r00 = (int16_t)Get16(d + 2);
        const uint8_t* p = d + 4;
        int32_t t[3] = { 0, 0, 0 };
        int32_t r[9] = { 0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000 };
        if (!(flag & 1)) { for (int i = 0; i < 3; i++) t[i] = (int32_t)Get32(p + 4 * i); p += 12; }
        if (!(flag & 2))
        {
            if (flag & 8)
            {
                // A "pivot" rotation: one entry is +-1, the others in its row and column 0,
                // and the remaining 2x2 is (a b / c d) with c = +-b and d = +-a.
                const int32_t a = (int16_t)Get16(p), b = (int16_t)Get16(p + 2);
                p += 4;
                const int sel = (flag >> 4) & 0xF, neg = (flag >> 8) & 0xF;
                const int32_t one = (neg & 1) ? -0x1000 : 0x1000, c = (neg & 2) ? -b : b, dd = (neg & 4) ? -a : a;
                static const uint8_t rest[9][4] = { {4,5,7,8}, {3,5,6,8}, {3,4,6,7}, {1,2,7,8}, {0,2,6,8},
                                                    {0,1,6,7}, {1,2,4,5}, {0,2,3,5}, {0,1,3,4} };
                for (int i = 0; i < 9; i++) r[i] = 0;
                if (sel < 9)
                {
                    r[sel] = one;
                    r[rest[sel][0]] = a; r[rest[sel][1]] = b; r[rest[sel][2]] = c; r[rest[sel][3]] = dd;
                }
            }
            else
            {
                r[0] = r00;
                for (int i = 1; i < 9; i++) r[i] = (int16_t)Get16(p + 2 * (i - 1));
                p += 16;
            }
        }
        n.move = !(flag & 1) || !(flag & 2);
        for (int i = 0; i < 9; i++) n.mtx[i] = (uint32_t)r[i];
        for (int i = 0; i < 3; i++) n.mtx[9 + i] = (uint32_t)t[i];
        if (!(flag & 4))
        {
            n.scale = true;
            for (int i = 0; i < 3; i++) n.s[i] = Get32(p + 4 * i);
        }
        return n;
    }

    // Packed display list -> command, parameters, command, parameters, ...
    static std::vector<uint32_t> UnpackDisplayList(const uint8_t* dl, uint32_t size)
    {
        static const uint8_t numParams[0x80] = {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,0,1,1,1,0,16,12,16,12,9,3,3,0,0,0,
            1,1,1,2,1,1,1,1,1,1,1,1,0,0,0,0, 1,1,1,1,32,0,0,0,0,0,0,0,0,0,0,0,
            1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 3,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0 };
        std::vector<uint32_t> out;
        uint32_t p = 0;
        while (p + 4 <= size)
        {
            const uint32_t cmds = Get32(dl + p);
            p += 4;
            for (int k = 0; k < 4; k++)
            {
                const uint32_t c = (cmds >> (8 * k)) & 0xFF;
                if (!c) continue;
                const uint32_t n = numParams[c & 0x7F];
                if (p + 4 * n > size) return out;
                out.push_back(c);
                for (uint32_t j = 0; j < n; j++) out.push_back(Get32(dl + p + 4 * j));
                p += 4 * n;
            }
        }
        return out;
    }

    // Where the area's textures and palettes were put in video memory (what NitroSystem's
    // binding writes into a model's materials), from the area's texture set in RAM.
    bool BindTextures(uint32_t tex0)
    {
        const uint32_t texKey = Read32(tex0 + 0x08), tex4Key = Read32(tex0 + 0x18), plttKey = Read32(tex0 + 0x2C);
        const uint32_t keys = texKey ^ (tex4Key * 3) ^ (plttKey * 7);
        if (tex0 == BoundTex0 && keys == BoundKeys) return !TexParams.empty();
        BoundTex0 = tex0;
        BoundKeys = keys;
        TexParams.clear();
        Palettes.clear();
        // Another area: its blocks' models are bound afresh, and its props are other models.
        Props.clear();
        BadProps.clear();
        const uint32_t size = Read32(tex0 + 4);
        if (size < 0x40 || size > 0x80000 || ((tex0 & RamMask) + size) > RamMask + 1) return false;
        const uint8_t* t = &Ram[tex0 & RamMask];
        const uint32_t texAddr = (texKey & 0xFFFF) << 3, tex4Addr = (tex4Key & 0xFFFF) << 3, plttAddr = (plttKey & 0xFFFF) << 3;
        for (const auto& e : Dict(t + Get16(t + 0x0E)))
        {
            const uint32_t param = Get32(e.second);
            const uint32_t base = ((param >> 26) & 7) == 5 ? tex4Addr : texAddr;
            TexParams[e.first] = param + (base >> 3);
        }
        for (const auto& e : Dict(t + Get16(t + 0x34)))
            Palettes[e.first] = plttAddr + (Get16(e.second) << 3);
        return !TexParams.empty();
    }

    // A model at a place: camera, then translation, scale, then the model's own byte code.
    void EmitPlaced(const Model& m, int32_t x, int32_t y, int32_t z, const uint32_t* scale, std::vector<uint32_t>& out) const
    {
        out.push_back(0x17);
        for (int i = 0; i < 12; i++) out.push_back(Read32(Glb + 0x4C + 4 * i));
        const uint32_t pos[12] = { 0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000, (uint32_t)x, (uint32_t)y, (uint32_t)z };
        out.push_back(0x19);
        out.insert(out.end(), pos, pos + 12);
        if (scale)
        {
            out.push_back(0x1B);
            out.insert(out.end(), scale, scale + 3);
        }
        for (const Op& op : m.ops)
            switch (op.kind)
            {
            case OpMat: EmitMaterial(m.mats[op.a], out); break;
            case OpShape: out.insert(out.end(), m.shapes[op.a].begin(), m.shapes[op.a].end()); break;
            case OpRestore: out.push_back(0x14); out.push_back(op.a); break;
            case OpNode:
            {
                if (op.c >= 0) { out.push_back(0x14); out.push_back(op.c); }
                const Node& n = m.nodes[op.a];
                if (n.move) { out.push_back(0x19); out.insert(out.end(), n.mtx, n.mtx + 12); }
                if (n.scale) { out.push_back(0x1B); out.insert(out.end(), n.s, n.s + 3); }
                if (op.b >= 0) { out.push_back(0x13); out.push_back(op.b); }
                break;
            }
            case OpPosScale:
                out.push_back(0x1B);
                for (int i = 0; i < 3; i++) out.push_back((uint32_t)m.posScale);
                break;
            }
    }

    void EmitMaterial(const Material& m, std::vector<uint32_t>& out) const
    {
        // The game lights its map with its own (time of day) colours, not the model's.
        out.push_back(0x30); out.push_back(Colours[0] | (m.difAmb & 0x8000));
        out.push_back(0x31); out.push_back(Colours[1]);
        out.push_back(0x29); out.push_back((m.polyAttr & m.polyMask) | (Colours[2] & ~m.polyMask));
        uint32_t tex = 0, pal = 0;
        if (m.bound)
        {
            tex = m.texImage;
            pal = m.plttBase;
        }
        else
        {
            // Materials whose texture an animation chooses have no texture name: their
            // texture has the material's own name.
            auto t = TexParams.find(m.tex.empty() ? m.name : m.tex);
            if (t != TexParams.end())
            {
                tex = m.texImage | t->second;
                auto p = Palettes.find(m.pltt.empty() ? m.name : m.pltt);
                if (p == Palettes.end() && m.pltt.empty()) p = Palettes.find(m.name + "_pl");
                if (p != Palettes.end()) pal = ((tex >> 26) & 7) == 2 ? p->second >> 3 : p->second >> 4;
            }
        }
        // Scrolling water: the texture coordinates and matrix the game used for the same
        // texture this frame.
        auto anim = melonDS::Wide.TexAnim.find(tex & 0x3FFFFFFF);
        if (tex && anim != melonDS::Wide.TexAnim.end())
        {
            tex = (uint32_t)anim->second[0];
            out.push_back(0x10); out.push_back(3);
            out.push_back(0x16);
            for (int i = 0; i < 16; i++) out.push_back((uint32_t)anim->second[1 + i]);
            out.push_back(0x10); out.push_back(2);
        }
        out.push_back(0x2A); out.push_back(tex);
        out.push_back(0x2B); out.push_back(pal);
    }
};
