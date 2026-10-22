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
#include <array>
#include <map>
#include <string>
#include <vector>
#include "NDS.h"
#include "ndsrom.h"
#include "GPU3D_Wide.h"
#include "nightfx.h"
#include "hud.h"
#include "plazamap.h"

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
        LooksMade = false;
        LookList.clear();
        melonDS::Wide.ExternalTextures.clear();
        PlazaLoaded = false;
        PlazaModels.clear();
        PlazaPieces.clear();
        PlazaPoses.clear();
        PlazaTexCache.clear();
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

    // The commands that draw map block (x, z) of the map the player is on as Build does (the
    // ground and its props), but with no camera: in world coordinates, for a picture from
    // above (the overworld HUD's map). `key` changes with the block, its height and the
    // textures bound (only the key without `out`). False when it cannot be drawn now.
    bool BlockFromAbove(melonDS::NDS& nds, int x, int z, std::vector<uint32_t>* out, uint64_t& key)
    {
        if (Land.empty()) return false;
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        const uint32_t fs = Read32(FieldSystemPtr);
        if (!InRam(fs)) return false;
        const uint32_t loader = Read32(fs + 0x2C), matrix = Read32(fs + 0x30), area = Read32(fs + 0x34);
        if (!InRam(loader) || !InRam(matrix) || !InRam(area) || Read32(fs + 0x70) > 1) return false;
        const bool props = Read32(fs + 0x70) == 0;
        const int width = Read8(matrix), height = Read8(matrix + 1);
        if (width < 1 || height < 1 || width * height > 799 || x < 0 || z < 0 || x >= width || z >= height) return false;
        const uint32_t altitudes = matrix + 4 + 2 + 799 * 2, models = (altitudes + 799 + 1) & ~1u;
        uint32_t propModels = 0;
        for (int k = 0; k < 4 && !propModels; k++)
        {
            const uint32_t slot = Read32(loader + 0x90 + 4 * k);
            const uint32_t mgr = InRam(slot) ? Read32(slot + 0x868) : 0;
            if (InRam(mgr) && InRam(Read32(mgr + 0x700))) propModels = Read32(mgr + 0x700);
        }
        const uint32_t tex0 = Read32(area + 0x8A4);
        if (!InRam(tex0) || memcmp(&Ram[tex0 & RamMask], "TEX0", 4) != 0 || !BindTextures(tex0)) return false;
        if (propModels != PropManager)
        {
            PropManager = propModels;
            Props.clear();
        }
        const int cell = z * width + x;
        const uint16_t id = Read16(models + 2 * cell);
        const Block* b = GetBlock(id);
        if (!b) return false;
        key = (uint64_t)id | (uint64_t)Read8(altitudes + cell) << 16 | (uint64_t)BoundKeys << 24 ^ (uint64_t)PropManager << 32;
        if (!out) return true;
        const int32_t bx = x * 0x200000 + 0x100000, bz = z * 0x200000 + 0x100000;
        // EmitPlaced starts with the camera (MTX_LOAD_4x3): made the identity.
        auto place = [&](const Model& m, int32_t px, int32_t py, int32_t pz, const uint32_t* scale) {
            const size_t at = out->size();
            EmitPlaced(m, px, py, pz, scale, *out);
            for (int i = 0; i < 12; i++) (*out)[at + 1 + i] = i % 4 == 0 ? 0x1000 : 0;
        };
        place(b->ground, bx, Read8(altitudes + cell) << 15, bz, nullptr);
        if (props && PropManager)
            for (const PropPlace& p : b->props)
                if (const Model* m = GetProp(p.buildModel)) place(*m, bx + p.pos[0], p.pos[1], bz + p.pos[2], p.scale);
        return true;
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
                float u, v;
                if (!Place(viewWidth, zoom, fx * 16, (Read8(altitudes + cell) << 15) / 4096.0 + fy * 16, fz * 16, u, v)) continue;
                if (u < 0 || u > 1 || v < 0 || v > 1) continue;
                out.push_back({ u, v, brightness });
            }
    }

    // Street lamps and lanterns around the camera (props with a lamp's build model), as
    // places in the wide picture like Fireflies: the oval their light makes on the ground
    // and the lamp's head.
    // The game's camera (NNS_G3dGlb's camera and projection matrices, as numbers) and the
    // view's width and zoom: read when a frame of 3D is flushed, so that what is drawn over
    // the picture (Place, Lights, Fireflies) uses the camera that picture was drawn with.
    struct Camera
    {
        bool valid = false;
        double cam[12] = {}, proj[16] = {};
        float viewWidth = 256, zoom = 1;
    };
    Camera ReadCamera(melonDS::NDS& nds, float viewWidth, float zoom)
    {
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        Camera c;
        c.valid = true;
        for (int i = 0; i < 12; i++) c.cam[i] = (int32_t)Read32(Glb + 0x4C + 4 * i) / 4096.0;
        for (int i = 0; i < 16; i++) c.proj[i] = (int32_t)Read32(Glb + 0x08 + 4 * i) / 4096.0;
        c.viewWidth = viewWidth;
        c.zoom = zoom;
        return c;
    }
    // Halfway (t) from a to b: the camera of an in-between picture.
    static Camera Lerp(const Camera& a, const Camera& b, double t)
    {
        Camera c = b;
        for (int i = 0; i < 12; i++) c.cam[i] = a.cam[i] + (b.cam[i] - a.cam[i]) * t;
        for (int i = 0; i < 16; i++) c.proj[i] = a.proj[i] + (b.proj[i] - a.proj[i]) * t;
        return c;
    }
    // The camera of the picture on the screen; without one, the game's camera now.
    Camera Shown;

    // A place in the world (world units: a tile is 16) as a place in the wide picture (0 to
    // 1 across and down), with the camera of the picture shown; false behind the camera.
    bool PlaceInView(melonDS::NDS& nds, float viewWidth, float zoom, double x, double y, double z, float& u, float& v)
    {
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        return Place(viewWidth, zoom, x, y, z, u, v);
    }

    // A person's frame (`frame` 0-3 of the walk) as a picture (for the HUD), or false. Needs
    // the overworld running.
    bool SpriteImage(melonDS::NDS& nds, uint16_t sprite, int facing, Image& out, int frame = 0)
    {
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        if (Sprites.empty() || !InRam(Read32(FieldSystemPtr)) || Read16(SpriteGfx) != 1) return false;
        return TextureImage(GetSpriteTexture(sprite, facing & 3, frame & 3), out);
    }

    // The archive member of the sprite whose textures are named `stem` (".1" to ".16"), or
    // -1: "hero" is the boy, "heroine" the girl.
    int MemberByStem(const std::string& stem) const
    {
        for (uint32_t m = 0; m < Sprites.size(); m++)
            if (MemberStem(m) == stem) return (int)m;
        return -1;
    }

    // A frame of an archive member (a look, see Looks()) as a picture, from the ROM alone
    // (before the game runs too).
    bool MemberImage(int member, int facing, int frame, Image& out)
    {
        return member >= 0 && TextureImage(MemberTexture((uint32_t)member, facing & 3, frame & 3), out);
    }

    // The looks a player can choose (members of the overworld sprite archive): the people
    // who walk in all four ways, i.e. with textures ".1" to ".16", 32 x 32, whose step
    // frames differ from the standing ones (so no one-pose people, objects or the 8-frame
    // Pokemon). The boy and the girl first, each look once.
    const std::vector<int>& Looks()
    {
        if (LooksMade) return LookList;
        LooksMade = true;
        std::vector<std::string> stems = { "hero", "heroine" };
        for (const std::string& first : stems)
        {
            const int m = MemberByStem(first);
            if (m >= 0 && Walks((uint32_t)m)) LookList.push_back(m);
        }
        for (uint32_t m = 0; m < Sprites.size(); m++)
        {
            const std::string stem = MemberStem(m);
            if (stem.empty() || std::find(stems.begin(), stems.end(), stem) != stems.end() || !Walks(m)) continue;
            LookList.push_back((int)m);
            stems.push_back(stem);
        }
        return LookList;
    }
    bool IsLook(int member) { return member >= 0 && std::count(Looks().begin(), Looks().end(), member) > 0; }

    // An external texture as a picture.
    static bool TextureImage(int tex, Image& out)
    {
        if (tex < 0) return false;
        const auto& t = melonDS::Wide.ExternalTextures[tex];
        out.w = t.W;
        out.h = t.H;
        out.px.resize(t.Texels.size());
        for (size_t i = 0; i < t.Texels.size(); i++)
        {
            const uint32_t v = t.Texels[i];
            const uint32_t r = (v & 63) * 255 / 63, g = (v >> 8 & 63) * 255 / 63, b = (v >> 16 & 63) * 255 / 63;
            out.px[i] = (v >> 24 & 31) ? 0xFF000000 | r << 16 | g << 8 | b : 0;
        }
        return true;
    }

    // Other players' trainers (online), drawn as the game draws a person.
    struct Trainer
    {
        int sprite, facing, frame; // the game's sprite id; 0 north, 1 south, 2 west, 3 east; 0-3 of the walk
        int32_t x, y, z;           // world position (fx32), as the game's map objects keep it
        int member = -1;           // a look (Looks()) drawn instead of the sprite id, or -1
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
            const int tex = t.member >= 0 ? MemberTexture((uint32_t)t.member, t.facing & 3, t.frame & 3)
                : GetSpriteTexture((uint16_t)t.sprite, t.facing & 3, t.frame & 3);
            if (tex < 0) continue;
            EmitBillboard(tex, t.x, t.y, (int64_t)t.z + 0x6000, out);
            drawn++;
        }
        if (!drawn) out.resize(start);
    }

    // The Wi-Fi Plaza (plazamap.h): on its map, the room, its floor and its furniture, from
    // the Plaza's own models and textures in the ROM (set 0 of a/2/0/1), placed as the
    // Plaza program places them on its grid.
    bool Plaza = false; // the ROM was set up for it
    bool OnPlaza(melonDS::NDS& nds)
    {
        Ram = nds.MainRAM;
        RamMask = nds.MainRAMMask;
        const uint32_t fs = Read32(FieldSystemPtr);
        if (!Plaza || !InRam(fs) || Read32(fs + 0x70) != 0) return false;
        const uint32_t location = Read32(fs + 0x20);
        return InRam(location) && Read32(location) == (uint32_t)PlazaMap::MapId;
    }
    void EmitPlaza(melonDS::NDS& nds, std::vector<uint32_t>& out)
    {
        if (!OnPlaza(nds)) return;
        if (!PlazaLoaded) LoadPlaza();
        if (PlazaPieces.empty()) return;
        EmitProjection(out);
        Colours[2] = Read32(Glb + 0x9C);
        // Its animations run one frame for each of the game's 3D frames, as the Plaza's own
        // program steps them; pieces of the same model move together, so each model is posed
        // once.
        const uint64_t clock = melonDS::Wide.FlushCount;
        for (size_t i = 0; i < PlazaModels.size(); i++)
            PlazaPose(PlazaModels[i], clock, PlazaPoses[i]);
        for (const PlazaPiece& p : PlazaPieces)
            EmitPlaced(PlazaModels[p.model], p.x, 0, p.z, nullptr, out, &PlazaPoses[p.model]);
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
        double live[28];
        const double *cam = Shown.cam, *proj = Shown.proj;
        if (Shown.valid)
        {
            viewWidth = Shown.viewWidth;
            zoom = Shown.zoom;
        }
        else
        {
            for (int i = 0; i < 12; i++) live[i] = (int32_t)Read32(Glb + 0x4C + 4 * i) / 4096.0;
            for (int i = 0; i < 16; i++) live[12 + i] = (int32_t)Read32(Glb + 0x08 + 4 * i) / 4096.0;
            cam = live;
            proj = live + 12;
        }
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
    bool LooksMade = false;
    std::vector<int> LookList;

    // The name of a sprite archive member's textures without the frame ("hero" of
    // "hero.1"), or empty.
    std::string MemberStem(uint32_t m) const
    {
        if (m >= Sprites.size()) return "";
        const uint8_t* b = Rom + Sprites[m].first;
        const uint32_t size = Sprites[m].second;
        if (size < 0x40 || memcmp(b, "BTX0", 4) != 0 || Get32(b + 0x10) + 0x3C > size) return "";
        const uint8_t* t = b + Get32(b + 0x10);
        const auto names = Dict(t + Get16(t + 0x0E));
        const size_t dot = names.empty() ? std::string::npos : names[0].first.find('.');
        return dot == std::string::npos ? "" : names[0].first.substr(0, dot);
    }

    // Whether a member walks in all four ways (see Looks()).
    bool Walks(uint32_t m)
    {
        for (int facing = 0; facing < 4; facing++)
        {
            const int stand = MemberTexture(m, facing, 0), step = MemberTexture(m, facing, 1);
            if (stand < 0 || step < 0 || MemberTexture(m, facing, 3) < 0) return false;
            const auto& a = melonDS::Wide.ExternalTextures[stand];
            const auto& b = melonDS::Wide.ExternalTextures[step];
            if (a.W != 32 || a.H != 32 || b.W != 32 || b.H != 32 || a.Texels == b.Texels) return false;
        }
        return true;
    }

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
        return MemberTexture(member, facing, frame);
    }

    int MemberTexture(uint32_t member, int facing, int frame)
    {
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
        bool own = false;   // a model with textures of its own (ext) and its own colours
        int ext = -1;       // its texture as an external texture (Wide.ExternalTextures)
    };
    struct Node
    {
        bool move = false, scale = false;
        uint32_t mtx[12] = {}; // rotation rows and translation, as MTX_MULT_4x3 takes them
        uint32_t s[3] = {};
    };
    enum { OpMat, OpShape, OpNode, OpRestore, OpPosScale };
    struct Op { int kind, a, b, c; };
    // NitroSystem animations of the Plaza's models (BTA0, BTP0, BCA0 files). A track is a
    // constant or one value per frame (or per 2 or 4 frames, in between interpolated).
    struct Track
    {
        bool constant = true;
        int32_t value = 0;
        const uint8_t* data = nullptr;
        int size = 4;       // bytes per value: 2 (fx16) or 4 (fx32); a pair for scales
        int stride = 4;
        int shift = 0;      // log2 of the frames per value
        int lastInterp = 0; // from this frame on, one value per frame
    };
    struct SrtAnim { int mat; Track scaleS, scaleT, transS, transT; }; // texture placement
    struct PatAnim { int mat; std::vector<std::pair<int, int>> keys; };  // (frame, texture) by frame
    struct JntAnim { int node; uint16_t flags; Track t[3], r, s[3]; };    // a node's move
    struct Anim
    {
        int frames = 1;
        bool loop = true; // else held at frame 0 (how the Plaza leaves posts and ramps when idle)
        std::vector<SrtAnim> srt;
        std::vector<PatAnim> pat;
        std::vector<JntAnim> jnt;
        const uint8_t *rot3 = nullptr, *rot5 = nullptr; // a JNT0's rotation tables
    };
    // A model's state at a frame: its nodes, and per material a texture and its scroll.
    struct Pose
    {
        std::vector<Node> nodes;
        std::vector<int> ext;                  // -2: the material's own
        std::vector<std::array<int32_t, 4>> srt; // scale S, scale T, move S, move T (fx32); scale S 0: none
    };
    struct Model
    {
        int32_t posScale = 0x1000;
        std::vector<Material> mats;
        std::vector<Node> nodes;
        std::vector<std::vector<uint32_t>> shapes; // display lists as commands and parameters
        std::vector<Op> ops;                       // the structure byte code, in drawing order
        // Plaza models: their file (for textures an animation picks) and animations.
        const uint8_t *file = nullptr, *tex0 = nullptr, *end = nullptr;
        std::vector<Anim> anims;
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

    // The Plaza's pieces: models (from a/2/0/1) and where each stands (world units, fx32).
    struct PlazaPiece
    {
        int model;
        int32_t x, z;
    };
    bool PlazaLoaded = false;
    std::vector<Model> PlazaModels;
    std::vector<PlazaPiece> PlazaPieces;
    std::vector<Pose> PlazaPoses;                 // per model, this frame
    std::map<std::string, int> PlazaTexCache;     // file, texture and palette -> external texture

    void LoadPlaza()
    {
        PlazaLoaded = true;
        const auto files = NarcMembers("a/2/0/1"), maps = NarcMembers("a/2/0/0");
        if (files.empty() || maps.size() < 2 || maps[0].second != PlazaMap::GridW * PlazaMap::GridH * 4 || maps[1].second < 0x1A0) return;
        const uint8_t* grid = Rom + maps[0].first;
        const uint8_t* table = Rom + maps[1].first; // the first of the Plaza's looks: object kind -> member, then the room
        std::map<uint32_t, int> loaded;
        // A piece, with the animations the Plaza plays on it while nothing happens (members;
        // one equal to the model's own means none).
        auto place = [&](uint32_t member, int gx, int gz, std::vector<uint32_t> anims, bool loop) {
            auto it = loaded.find(member);
            if (it == loaded.end())
            {
                Model m;
                bool ok = member < files.size() && ParsePlazaModel(files[member].first, files[member].second, m);
                if (ok)
                    for (uint32_t a : anims)
                        if (a != member && a < files.size())
                            ParsePlazaAnim(Rom + files[a].first, files[a].second, loop, m);
                it = loaded.emplace(member, ok ? (PlazaModels.push_back(std::move(m)), (int)PlazaModels.size() - 1) : -1).first;
            }
            if (it->second >= 0)
                PlazaPieces.push_back({ it->second, (gx + PlazaMap::OffX) * 0x10000, (gz + PlazaMap::OffZ) * 0x10000 });
        };
        // The room with its three looping animations (the screens' scrolling, the moving
        // parts, the flags and the warp pad), and its floor (pokeplatinum ov70_02260B44.c;
        // the fifth, for an event, is left out).
        place(Get32(table + 0x180), 0, 0, { Get32(table + 0x18C), Get32(table + 0x190), Get32(table + 0x194) }, true);
        place(Get32(table + 0x184), 0, 0, { Get32(table + 0x198) }, true);
        // Furniture: the grid's event numbers and the kind of object standing there
        // (pokeplatinum ov70_02261E10.c), leaving out the fireworks (shown during the parade).
        static const uint8_t kinds[][2] = { { 24, 0 }, { 25, 1 }, { 26, 2 }, { 27, 3 }, { 28, 4 }, { 33, 5 }, { 70, 6 },
            { 71, 7 }, { 72, 8 }, { 76, 9 }, { 73, 10 }, { 74, 10 }, { 75, 10 }, { 77, 11 }, { 78, 12 }, { 98, 17 } };
        for (int gz = 0; gz < PlazaMap::GridH; gz++)
            for (int gx = 0; gx < PlazaMap::GridW; gx++)
            {
                const uint32_t id = Get32(grid + 4 * (gz * PlazaMap::GridW + gx)) >> 16;
                for (const auto& k : kinds)
                {
                    if (k[0] != id) continue;
                    // Up to three animations per kind (table + 0x48). Idle, the centre piece
                    // plays all of its own and the minigame domes their first; the posts and
                    // the ramps hold their first at frame 0. The stands only bounce when
                    // someone walks by.
                    const uint8_t* an = table + 0x48 + 12 * k[1];
                    std::vector<uint32_t> anims;
                    bool loop = true;
                    if (k[1] == 0) anims = { Get32(an), Get32(an + 4), Get32(an + 8) };
                    else if (k[1] >= 6 && k[1] <= 8) anims = { Get32(an) };
                    else if (k[1] >= 10 && k[1] <= 12) { anims = { Get32(an) }; loop = false; }
                    place(Get32(table + 4 * k[1]), gx, gz, anims, loop);
                }
            }
        PlazaPoses.resize(PlazaModels.size());
        size_t anims = 0;
        for (const Model& m : PlazaModels) anims += m.anims.size();
        fprintf(stderr, "[plaza] %zu models, %zu pieces, %zu animations\n", PlazaModels.size(), PlazaPieces.size(), anims);
    }

    // A model of the Plaza with its own textures, decoded as external textures.
    bool ParsePlazaModel(uint32_t off, uint32_t size, Model& m)
    {
        const uint8_t* b = Rom + off;
        if (!ParseModel(b, size, false, m)) return false;
        const uint8_t* t = Get16(b + 0x0E) >= 2 ? b + Get32(b + 0x14) : nullptr;
        if (!t || t + 0x3C > b + size || memcmp(t, "TEX0", 4) != 0) t = nullptr;
        m.file = b;
        m.tex0 = t;
        m.end = b + size;
        for (Material& mat : m.mats)
        {
            mat.own = true;
            mat.ext = PlazaTextureByName(m, mat.tex.empty() ? mat.name : mat.tex, mat.pltt);
        }
        return true;
    }

    // A texture of a Plaza model's own by name, with a palette by name, decoded once.
    int PlazaTextureByName(const Model& m, const std::string& tex, const std::string& pltt)
    {
        if (!m.tex0) return -1;
        const std::string key = std::to_string((uintptr_t)m.file) + "/" + tex + "/" + pltt;
        auto it = PlazaTexCache.find(key);
        if (it != PlazaTexCache.end()) return it->second;
        const uint8_t* t = m.tex0;
        int ext = -1;
        for (const auto& e : Dict(t + Get16(t + 0x0E)))
        {
            if (e.first != tex) continue;
            const uint8_t* pal = nullptr;
            for (const auto& q : Dict(t + Get16(t + 0x34)))
                if (q.first == pltt) pal = t + Get32(t + 0x38) + (Get16(q.second) << 3);
            ext = PlazaTexture(t, m.end, Get32(e.second), pal);
        }
        return PlazaTexCache[key] = ext;
    }

    // An animation file (BTA0: texture placement, BTP0: texture pattern, BCA0: nodes) for
    // model m, its materials and nodes found by name and number.
    void ParsePlazaAnim(const uint8_t* f, uint32_t size, bool loop, Model& m)
    {
        const uint8_t* end = f + size;
        if (size < 0x20 || Get32(f + 0x10) + 0x10 > size) return;
        const uint8_t* blk = f + Get32(f + 0x10);
        const auto anims = Dict(blk + 8);
        if (anims.empty()) return;
        const uint8_t* a = blk + Get32(anims[0].second);
        if (a + 0x14 > end) return;
        Anim an;
        an.loop = loop;
        an.frames = std::max<int>(1, Get16(a + 4));
        auto in = [&](const uint8_t* p, size_t n) { return p >= f && p + n <= end; };
        auto matIndex = [&](const std::string& name) {
            for (size_t i = 0; i < m.mats.size(); i++)
                if (m.mats[i].name == name) return (int)i;
            return -1;
        };
        if (!memcmp(f, "BTA0", 4) && !memcmp(a, "M\0AT", 4))
        {
            // Per material five tracks (scale S, T, rotation, move S, T): an info word (bit
            // 29 constant, 28 fx16 values, 30-31 frames per value, 0-15 the last frame
            // interpolated) and the constant or the values' place.
            for (const auto& e : Dict(a + 8))
            {
                const int mi = matIndex(e.first);
                if (mi < 0 || !in(e.second, 40)) continue;
                auto track = [&](int k) {
                    Track t;
                    const uint32_t info = Get32(e.second + 8 * k), v = Get32(e.second + 8 * k + 4);
                    t.size = t.stride = (info & 0x10000000) ? 2 : 4;
                    t.constant = (info & 0x20000000) != 0;
                    t.value = t.size == 2 ? (int16_t)v : (int32_t)v;
                    t.shift = std::min(2u, info >> 30);
                    t.lastInterp = info & 0xFFFF;
                    t.data = a + v;
                    if (!t.constant && !in(t.data, TrackBytes(t))) { t.constant = true; t.value = 0; }
                    return t;
                };
                SrtAnim s = { mi, track(0), track(1), track(3), track(4) };
                if (!track(2).constant || track(2).value != 0x10000000) fprintf(stderr, "[plaza] texture rotation not shown (%s)\n", e.first.c_str());
                an.srt.push_back(s);
            }
        }
        else if (!memcmp(f, "BTP0", 4) && !memcmp(a, "M\0PT", 4))
        {
            // Per material keys (frame, texture name, palette name), the names in two lists.
            const int ntex = a[6], npal = a[7];
            const uint8_t *texNames = a + Get16(a + 8), *palNames = a + Get16(a + 10);
            if (!in(texNames, 16 * ntex) || !in(palNames, 16 * npal)) return;
            auto name = [](const uint8_t* p) { return std::string((const char*)p, strnlen((const char*)p, 16)); };
            for (const auto& e : Dict(a + 12))
            {
                const int mi = matIndex(e.first);
                const int nkeys = Get16(e.second);
                const uint8_t* keys = a + Get16(e.second + 6);
                if (mi < 0 || !in(keys, 4 * nkeys)) continue;
                PatAnim p;
                p.mat = mi;
                for (int k = 0; k < nkeys; k++)
                {
                    const uint8_t* key = keys + 4 * k;
                    const int ext = key[2] < ntex && key[3] < npal ? PlazaTextureByName(m, name(texNames + 16 * key[2]), name(palNames + 16 * key[3])) : -1;
                    p.keys.push_back({ Get16(key), ext });
                }
                an.pat.push_back(std::move(p));
            }
        }
        else if (!memcmp(f, "BCA0", 4) && !memcmp(a, "J\0AC", 4))
        {
            // Per node: flags, then translation, rotation and scale as present. Tracks have
            // an info word (bits 0-15 first frame, 16-28 the last frame interpolated, 29 fx16
            // values, 30-31 frames per value) and the values' place. Rotations are numbers
            // into two tables: bit 15 set, a "pivot" one (3 halfwords), else a 3x3 matrix
            // packed in 5 halfwords.
            const int nodes = Get16(a + 6);
            an.rot3 = a + Get32(a + 0x0C);
            an.rot5 = a + Get32(a + 0x10);
            for (int i = 0; i < nodes; i++)
            {
                const uint8_t* d = a + Get16(a + 0x14 + 2 * i);
                if (!in(d, 4)) continue;
                JntAnim j;
                j.flags = Get16(d);
                j.node = d[3];
                const uint8_t* p = d + 4;
                auto track = [&](int valueSize, bool constant, int constSize) {
                    Track t;
                    t.constant = constant;
                    if (!in(p, 8)) return t;
                    if (constant)
                    {
                        t.value = (int32_t)Get32(p);
                        p += constSize;
                        return t;
                    }
                    const uint32_t info = Get32(p);
                    t.size = (info & 0x20000000) ? 2 : 4;
                    t.stride = t.size * valueSize;
                    t.shift = std::min(2u, info >> 30);
                    t.lastInterp = (info >> 16) & 0x1FFF;
                    t.data = a + Get32(p + 4);
                    if (!in(t.data, TrackBytes(t))) { t.constant = true; t.value = 0; }
                    p += 8;
                    return t;
                };
                const uint16_t fl = j.flags;
                if (!(fl & 1))
                {
                    if (!(fl & 6))
                        for (int k = 0; k < 3; k++) j.t[k] = track(1, (fl & (8 << k)) != 0, 4);
                    if (!(fl & 0xC0))
                    {
                        j.r = track(1, (fl & 0x100) != 0, 4);
                        if (!j.r.constant) j.r.size = j.r.stride = 2; // numbers into the tables
                    }
                    if (!(fl & 0x600))
                        for (int k = 0; k < 3; k++) j.s[k] = track(2, (fl & (0x800 << k)) != 0, 8);
                }
                if (j.node < (int)m.nodes.size()) an.jnt.push_back(j);
            }
        }
        else
            return;
        m.anims.push_back(std::move(an));
    }

    // The bytes of a track's values: one per frame, or per 2 or 4 frames and the last.
    static size_t TrackBytes(const Track& t) { return (size_t)t.stride * (t.shift ? (t.lastInterp >> t.shift) + 1 : std::max(1, t.lastInterp)); }

    // A track's value at a frame (rotations: the number, not interpolated).
    static int32_t TrackValue(const Track& t, int frame, bool interpolate = true)
    {
        if (t.constant) return t.value;
        auto at = [&](int i) { const uint8_t* p = t.data + (size_t)t.stride * i; return t.size == 2 ? (int32_t)(int16_t)Get16(p) : (int32_t)Get32(p); };
        // (Every frame has a value up to the last; with fewer, the last is held after it.)
        if (!t.shift) return at(std::min(frame, t.lastInterp - 1));
        if (frame >= t.lastInterp) return at(t.lastInterp >> t.shift);
        const int i = frame >> t.shift, part = frame & ((1 << t.shift) - 1);
        if (!part || !interpolate) return at(i);
        const int32_t v0 = at(i), v1 = at(i + 1);
        return v0 + (int32_t)((int64_t)(v1 - v0) * part >> t.shift);
    }

    // A JNT0 rotation by its number, as rows.
    static void Rotation(const Anim& an, uint32_t index, int32_t r[9])
    {
        if (index & 0x8000)
        {
            const uint8_t* p = an.rot3 + 6 * (index & 0x7FFF);
            const uint16_t info = Get16(p);
            const int32_t a = (int16_t)Get16(p + 2), b = (int16_t)Get16(p + 4);
            const int sel = info & 0xF, neg = (info >> 4) & 0xF;
            static const uint8_t rest[9][4] = { {4,5,7,8}, {3,5,6,8}, {3,4,6,7}, {1,2,7,8}, {0,2,6,8},
                                                {0,1,6,7}, {1,2,4,5}, {0,2,3,5}, {0,1,3,4} };
            for (int i = 0; i < 9; i++) r[i] = 0;
            if (sel >= 9) { r[0] = r[4] = r[8] = 0x1000; return; }
            r[sel] = (neg & 1) ? -0x1000 : 0x1000;
            r[rest[sel][0]] = a;
            r[rest[sel][1]] = b;
            r[rest[sel][2]] = (neg & 2) ? -b : b;
            r[rest[sel][3]] = (neg & 4) ? -a : a;
            return;
        }
        // Five entries in the top 13 bits of each halfword, the sixth from their low 3 bits;
        // the last row is the cross product of the first two.
        const uint8_t* p = an.rot5 + 10 * index;
        uint32_t low = 0;
        for (int i = 0; i < 5; i++)
        {
            const uint16_t h = Get16(p + 2 * i);
            r[i] = (int16_t)h >> 3;
            low = low << 3 | (h & 7);
        }
        r[5] = (int32_t)(low << 17) >> 17;
        r[6] = (r[1] * r[5] - r[2] * r[4]) >> 12;
        r[7] = (r[2] * r[3] - r[0] * r[5]) >> 12;
        r[8] = (r[0] * r[4] - r[1] * r[3]) >> 12;
    }

    // A Plaza model's state at the game's 3D frame `clock`.
    static void PlazaPose(const Model& m, uint64_t clock, Pose& pose)
    {
        pose.nodes = m.nodes;
        pose.ext.assign(m.mats.size(), -2);
        pose.srt.assign(m.mats.size(), { 0, 0, 0, 0 });
        for (const Anim& an : m.anims)
        {
            const int frame = an.loop ? (int)(clock % (uint64_t)an.frames) : 0;
            for (const SrtAnim& s : an.srt)
                pose.srt[s.mat] = { TrackValue(s.scaleS, frame), TrackValue(s.scaleT, frame), TrackValue(s.transS, frame), TrackValue(s.transT, frame) };
            for (const PatAnim& p : an.pat)
                for (const auto& k : p.keys)
                    if (k.first <= frame) pose.ext[p.mat] = k.second;
            for (const JntAnim& j : an.jnt)
            {
                Node& n = pose.nodes[j.node];
                const Node& base = m.nodes[j.node];
                const uint16_t fl = j.flags;
                int32_t r[9] = { 0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000 }, t[3] = { 0, 0, 0 };
                if (fl & 1)
                {
                    n.move = n.scale = false;
                    continue;
                }
                if (fl & 4) for (int k = 0; k < 3; k++) t[k] = (int32_t)base.mtx[9 + k];
                else if (!(fl & 2)) for (int k = 0; k < 3; k++) t[k] = TrackValue(j.t[k], frame);
                if (fl & 0x80) for (int k = 0; k < 9; k++) r[k] = (int32_t)base.mtx[k];
                else if (!(fl & 0x40)) Rotation(an, (uint32_t)TrackValue(j.r, frame, false) & 0xFFFF, r);
                for (int k = 0; k < 9; k++) n.mtx[k] = (uint32_t)r[k];
                for (int k = 0; k < 3; k++) n.mtx[9 + k] = (uint32_t)t[k];
                n.move = true;
                if (fl & 0x400) { n.scale = base.scale; for (int k = 0; k < 3; k++) n.s[k] = base.s[k]; }
                else if (fl & 0x200) n.scale = false;
                else
                {
                    n.scale = true;
                    for (int k = 0; k < 3; k++) n.s[k] = (uint32_t)TrackValue(j.s[k], frame);
                }
            }
        }
    }

    // A texture of a TEX0 block (param: its TEXIMAGE_PARAM there) as an external texture.
    static int PlazaTexture(const uint8_t* t, const uint8_t* end, uint32_t param, const uint8_t* pal)
    {
        const int w = 8 << ((param >> 20) & 7), h = 8 << ((param >> 23) & 7), fmt = (param >> 26) & 7;
        static const int bits[8] = { 0, 8, 2, 4, 8, 0, 8, 16 };
        const uint8_t* pix = t + Get32(t + 0x14) + ((param & 0xFFFF) << 3);
        const int colours = fmt == 1 ? 32 : fmt == 6 ? 8 : 1 << bits[fmt];
        if (!bits[fmt] || pix + w * h * bits[fmt] / 8 > end || (fmt != 7 && (!pal || pal + 2 * colours > end)))
            return -1;
        const bool clear0 = param & (1 << 29);
        auto colour = [&](uint16_t c, uint32_t a) {
            uint32_t r = (c << 1) & 0x3E, g = (c >> 4) & 0x3E, bl = (c >> 9) & 0x3E;
            if (r) r++;
            if (g) g++;
            if (bl) bl++;
            return 0x80000000 | r | (g << 8) | (bl << 16) | (a << 24);
        };
        melonDS::WideState::ExternalTexture tex;
        tex.W = w;
        tex.H = h;
        tex.Texels.resize((size_t)w * h);
        for (int i = 0; i < w * h; i++)
        {
            uint32_t v;
            if (fmt == 7) { const uint16_t c = Get16(pix + 2 * i); v = colour(c, c & 0x8000 ? 31 : 0); }
            else if (fmt == 1) { const uint8_t c = pix[i]; v = colour(Get16(pal + 2 * (c & 31)), (c >> 5) * 4 + (c >> 5) / 2); }
            else if (fmt == 6) { const uint8_t c = pix[i]; v = colour(Get16(pal + 2 * (c & 7)), c >> 3); }
            else
            {
                const int k = bits[fmt], c = (pix[i * k / 8] >> (i * k % 8)) & ((1 << k) - 1);
                v = colour(Get16(pal + 2 * c), clear0 && c == 0 ? 0 : 31);
            }
            tex.Texels[i] = v;
        }
        melonDS::Wide.ExternalTextures.push_back(std::move(tex));
        return (int)melonDS::Wide.ExternalTextures.size() - 1;
    }

    // A model at a place: camera, then translation, scale, then the model's own byte code.
    void EmitPlaced(const Model& m, int32_t x, int32_t y, int32_t z, const uint32_t* scale, std::vector<uint32_t>& out,
        const Pose* pose = nullptr) const
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
            case OpMat:
                if (pose) EmitMaterial(m.mats[op.a], out, pose->ext[op.a], pose->srt[op.a][0] ? pose->srt[op.a].data() : nullptr);
                else EmitMaterial(m.mats[op.a], out);
                break;
            case OpShape: out.insert(out.end(), m.shapes[op.a].begin(), m.shapes[op.a].end()); break;
            case OpRestore: out.push_back(0x14); out.push_back(op.a); break;
            case OpNode:
            {
                if (op.c >= 0) { out.push_back(0x14); out.push_back(op.c); }
                const Node& n = pose ? pose->nodes[op.a] : m.nodes[op.a];
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

    // (Plaza models: `ext` a texture an animation chose (-2: the material's own), and `srt`
    // the scale and move of its texture, fx32, 1.0 its whole width or height.)
    void EmitMaterial(const Material& m, std::vector<uint32_t>& out, int ext = -2, const int32_t* srt = nullptr) const
    {
        if (m.own)
        {
            if (ext == -2) ext = m.ext;
            uint32_t tex = ext < 0 ? 0 : (melonDS::WideState::ExternalTexParam + (uint32_t)ext) | (m.texImage & 0x000F0000);
            if (ext >= 0 && srt)
            {
                // The texture matrix, applied to the texture coordinates (in 16ths of a
                // texel) as they are given: S' = S * scale - move * width.
                const auto& e = melonDS::Wide.ExternalTextures[ext];
                const int32_t mtx[16] = { srt[0], 0, 0, 0, 0, srt[1], 0, 0, 0, 0, 0x1000, 0,
                    (int32_t)(-(int64_t)srt[2] * e.W * 16), (int32_t)(-(int64_t)srt[3] * e.H * 16), 0, 0x1000 };
                out.push_back(0x10); out.push_back(3);
                out.push_back(0x16);
                out.insert(out.end(), (const uint32_t*)mtx, (const uint32_t*)mtx + 16);
                out.push_back(0x10); out.push_back(2);
                tex |= 0x40000000; // texture coordinates through the matrix
            }
            out.push_back(0x30); out.push_back(m.difAmb);
            out.push_back(0x31); out.push_back(m.speEmi);
            // Lit by the map's first light: the others the Plaza sets for itself are dark on
            // the overworld.
            const uint32_t attr = (m.polyAttr & m.polyMask) | (Colours[2] & ~m.polyMask);
            out.push_back(0x29); out.push_back((attr & ~0xFu) | ((attr & 0xF) ? 1 : 0));
            // Wrapping and flipping as the material has them.
            out.push_back(0x2A); out.push_back(tex);
            out.push_back(0x2B); out.push_back(0);
            return;
        }
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
