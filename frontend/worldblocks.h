#pragma once
// Extra map blocks for the widescreen world. The game keeps only the 2x2 map blocks
// around the player loaded, which is all the DS's own view ever reaches; a wide or
// zoomed-out view reaches past them. Here the blocks around those are read from the ROM
// and drawn into the wide picture only (WideState::Extra), with the camera, lighting
// colours and texture placement the game uses for its own blocks this frame. Nothing is
// written to the game's memory.
//
// Addresses and layouts are those of HeartGold/SoulSilver (US) and Definitive HeartGold,
// found with the pret decompilation; everything is checked before use, and anything that
// does not look as expected (another game, a cutscene, a map being loaded) draws nothing.
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "NDS.h"

class WorldBlocks
{
public:
    int Radius = 1; // blocks around the camera's block: 1 = 3x3

    void SetRom(const std::vector<uint8_t>& rom)
    {
        Rom = rom.data();
        RomSize = rom.size();
        Land.clear();
        Models.clear();
        uint32_t off, size;
        if (!RomFile("a/0/6/5", off, size) || size < 16 || memcmp(Rom + off, "NARC", 4) != 0) return;
        // NARC: file allocation table (BTAF), name table (BTNF), then the data (GMIF).
        const uint8_t* n = Rom + off;
        const uint32_t fatSize = Get32(n + 16 + 4), count = Get32(n + 16 + 8);
        const uint32_t fnt = 16 + fatSize;
        const uint32_t gmif = fnt + Get32(n + fnt + 4) + 8;
        for (uint32_t i = 0; i < count; i++)
        {
            const uint32_t a = Get32(n + 16 + 12 + 8 * i), b = Get32(n + 16 + 16 + 8 * i);
            Land.push_back({ off + gmif + a, b - a });
        }
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
        // Only the overworld matrix: towns and routes (caves and houses have their own).
        const int width = Read8(matrix), height = Read8(matrix + 1);
        if (Read8(matrix + 2) != 0 || width < 2 || height < 2 || width * height > 799) return;
        const uint32_t altitudes = matrix + 4 + 2 + 799 * 2, models = (altitudes + 799 + 1) & ~1u;

        // Blocks the game has loaded (and draws itself).
        int loaded[4];
        for (int k = 0; k < 4; k++)
        {
            const uint32_t slot = Read32(loader + 0x90 + 4 * k);
            loaded[k] = InRam(slot) && Read32(slot + 0x864) == 1 ? (int32_t)Read32(slot + 0x860) : -1;
        }
        // The block the camera looks at.
        const int32_t tx = (int32_t)Read32(Glb + 0x258), tz = (int32_t)Read32(Glb + 0x260);
        const int cx = (tx >> 16) / 32, cz = (tz >> 16) / 32;
        if (tx < 0 || tz < 0 || cx >= width || cz >= height) return;

        const uint32_t tex0 = Read32(area + 0x8A4);
        if (!InRam(tex0) || memcmp(&Ram[tex0 & RamMask], "TEX0", 4) != 0) return;
        if (!BindTextures(tex0)) return;

        const size_t start = out.size();
        // The game's projection and camera, as it loaded them for its own blocks.
        out.push_back(0x10); out.push_back(0);
        out.push_back(0x16);
        for (int i = 0; i < 16; i++) out.push_back(Read32(Glb + 0x08 + 4 * i));
        out.push_back(0x10); out.push_back(2);
        out.push_back(0x60); out.push_back(0xBFFF0000); // the whole screen
        const uint32_t difAmb = Read32(Glb + 0x94), speEmi = Read32(Glb + 0x98), polyAttr = Read32(Glb + 0x9C);

        int drawn = 0;
        for (int z = cz - Radius; z <= cz + Radius; z++)
            for (int x = cx - Radius; x <= cx + Radius; x++)
            {
                if (x < 0 || z < 0 || x >= width || z >= height) continue;
                const int cell = z * width + x;
                if (cell == loaded[0] || cell == loaded[1] || cell == loaded[2] || cell == loaded[3]) continue;
                const uint16_t id = Read16(models + 2 * cell);
                const Model* m = GetModel(id);
                if (!m) continue;
                // Camera, then the block's place (as the game does), then the model's scale.
                out.push_back(0x17);
                for (int i = 0; i < 12; i++) out.push_back(Read32(Glb + 0x4C + 4 * i));
                const uint32_t pos[12] = { 0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000,
                    (uint32_t)(x * 0x200000 + 0x100000), (uint32_t)(Read8(altitudes + cell) << 15), (uint32_t)(z * 0x200000 + 0x100000) };
                out.push_back(0x19);
                out.insert(out.end(), pos, pos + 12);
                out.push_back(0x1B);
                for (int i = 0; i < 3; i++) out.push_back((uint32_t)m->posScale);
                for (const auto& op : m->ops)
                {
                    if (op.first == 0) EmitMaterial(m->mats[op.second], difAmb, speEmi, polyAttr, out);
                    else out.insert(out.end(), m->shapes[op.second].begin(), m->shapes[op.second].end());
                }
                drawn++;
            }
        if (!drawn) out.resize(start);
    }

private:
    static constexpr uint32_t FieldSystemPtr = 0x021D1110; // the game's FieldSystem pointer
    static constexpr uint32_t Glb = 0x021DA49C;            // NitroSystem's NNS_G3dGlb (camera, projection)

    struct Material
    {
        uint32_t difAmb, speEmi, polyAttr, polyMask, texImage;
        std::string tex, pltt;
    };
    struct Model
    {
        int32_t posScale = 0x1000;
        std::vector<Material> mats;
        std::vector<std::vector<uint32_t>> shapes; // display lists as commands and parameters
        std::vector<std::pair<int, int>> ops;      // in drawing order: (0, material) or (1, shape)
    };

    const uint8_t* Rom = nullptr;
    size_t RomSize = 0;
    std::vector<std::pair<uint32_t, uint32_t>> Land; // ROM offset and size of each map block file
    std::map<uint16_t, Model> Models;                 // parsed on first use
    std::map<uint16_t, bool> BadModels;
    uint8_t* Ram = nullptr;
    uint32_t RamMask = 0;
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
    bool RomFile(const char* path, uint32_t& off, uint32_t& size) const
    {
        if (RomSize < 0x200) return false;
        const uint32_t fnt = Get32(Rom + 0x40), fat = Get32(Rom + 0x48);
        uint32_t dir = 0xF000;
        std::string rest = path;
        while (true)
        {
            const size_t slash = rest.find('/');
            const std::string name = rest.substr(0, slash);
            const uint32_t sub = fnt + Get32(Rom + fnt + (dir & 0xFFF) * 8);
            uint32_t fid = Get16(Rom + fnt + (dir & 0xFFF) * 8 + 4);
            uint32_t p = sub;
            bool found = false;
            while (p < RomSize && Rom[p])
            {
                const uint8_t t = Rom[p++];
                const uint32_t len = t & 0x7F;
                const std::string entry((const char*)Rom + p, len);
                p += len;
                if (t & 0x80)
                {
                    const uint16_t subdir = Get16(Rom + p);
                    p += 2;
                    if (entry == name && slash != std::string::npos) { dir = subdir; found = true; break; }
                }
                else
                {
                    if (entry == name && slash == std::string::npos)
                    {
                        const uint32_t s = Get32(Rom + fat + fid * 8), e = Get32(Rom + fat + fid * 8 + 4);
                        if (e < s || e > RomSize) return false;
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

    const Model* GetModel(uint16_t id)
    {
        auto it = Models.find(id);
        if (it != Models.end()) return &it->second;
        if (id >= Land.size() || BadModels.count(id)) return nullptr;
        Model m;
        if (!ParseBlock(Land[id].first, Land[id].second, m))
        {
            BadModels[id] = true;
            return nullptr;
        }
        return &(Models[id] = std::move(m));
    }

    bool ParseBlock(uint32_t off, uint32_t size, Model& m) const
    {
        if (size < 20) return false;
        const uint8_t* f = Rom + off;
        const uint32_t perm = Get32(f), props = Get32(f + 4), mdlSize = Get32(f + 8);
        uint32_t p = 16;
        if (Get16(f + p) == 0x1234) p += 4 + Get16(f + p + 2);
        p += perm + props;
        if ((uint64_t)p + mdlSize > size || mdlSize < 0x40) return false;
        const uint8_t* b = f + p;
        if (memcmp(b, "BMD0", 4) != 0) return false;
        const uint8_t* mdl0 = b + Get32(b + 0x10);
        if (memcmp(mdl0, "MDL0", 4) != 0) return false;
        const auto models = Dict(mdl0 + 8);
        if (models.empty()) return false;
        const uint8_t* mo = mdl0 + Get32(models[0].second);
        const uint8_t* sbc = mo + Get32(mo + 4);
        const uint8_t* mat = mo + Get32(mo + 8);
        const uint8_t* shp = mo + Get32(mo + 12);
        m.posScale = (int32_t)Get32(mo + 20 + 8);

        for (const auto& e : Dict(mat + 4))
        {
            const uint8_t* d = mat + Get32(e.second);
            Material x;
            x.difAmb = Get32(d + 4);
            x.speEmi = Get32(d + 8);
            x.polyAttr = Get32(d + 12);
            x.polyMask = Get32(d + 16);
            x.texImage = Get32(d + 20);
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
            if (dl + dlSize > f + size) return false;
            m.shapes.push_back(UnpackDisplayList(dl, dlSize));
        }
        // The structure byte code: which material and shape come in which order.
        for (const uint8_t* c = sbc; c < mat;)
        {
            const uint8_t op = *c & 0x1F, flags = *c >> 5;
            if (op == 0x01) break;
            switch (op)
            {
            case 0x00: c += 1; break;
            case 0x02: c += 3; break;
            case 0x03: c += 2; break;
            case 0x04: if (c[1] < m.mats.size()) m.ops.push_back({ 0, c[1] }); c += 2; break;
            case 0x05: if (c[1] < m.shapes.size()) m.ops.push_back({ 1, c[1] }); c += 2; break;
            case 0x06: c += 4 + ((flags & 1) ? 1 : 0) + ((flags & 2) ? 1 : 0); break;
            case 0x07: case 0x08: c += 2 + ((flags & 1) ? 1 : 0) + ((flags & 2) ? 1 : 0); break;
            case 0x0B: c += 1; break;
            default: return false; // skinning, display list calls, environment maps: not map blocks
            }
        }
        return !m.ops.empty();
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

    void EmitMaterial(const Material& m, uint32_t difAmb, uint32_t speEmi, uint32_t polyAttr, std::vector<uint32_t>& out) const
    {
        // The game lights its blocks with its own (time of day) colours, not the model's.
        out.push_back(0x30); out.push_back(difAmb | (m.difAmb & 0x8000));
        out.push_back(0x31); out.push_back(speEmi);
        out.push_back(0x29); out.push_back((m.polyAttr & m.polyMask) | (polyAttr & ~m.polyMask));
        uint32_t tex = 0, pal = 0;
        auto t = TexParams.find(m.tex);
        if (t != TexParams.end())
        {
            tex = m.texImage | t->second;
            auto p = Palettes.find(m.pltt);
            if (p != Palettes.end()) pal = ((tex >> 26) & 7) == 2 ? p->second >> 3 : p->second >> 4;
        }
        out.push_back(0x2A); out.push_back(tex);
        out.push_back(0x2B); out.push_back(pal);
    }
};
