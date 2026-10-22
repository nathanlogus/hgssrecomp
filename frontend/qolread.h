#pragma once
// Small aids read from the game's memory and drawn by the frontend:
//  - a toast when a wild shiny Pokemon appears in a battle;
//  - a "..." bubble over the people on the overworld that you have not talked to yet, with
//    who was talked to kept in a file beside the save (never in the save);
//  - a waypoint chosen on the Town Map, with a pin on the minimap and the Town Map and an
//    arrow on the overworld towards it, kept in a file beside the save;
//  - sorting the bag's pocket (A to Z, by kind, newest first, or as it was).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "hudbattleui.h"
#include "hudparty.h"

namespace QolRead
{
using HudParty::Ram;

constexpr uint32_t kFieldSystem = 0x021D4158;

inline bool ReadLines(const std::string& path, std::vector<std::string>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof line, f))
    {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty()) out.push_back(s);
    }
    fclose(f);
    return true;
}

inline void WriteLines(const std::string& path, const std::vector<std::string>& lines)
{
    if (path.empty()) return;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return; // (a missing sidecar only costs the memory of it)
    for (const std::string& s : lines) fprintf(f, "%s\n", s.c_str());
    fclose(f);
}

// A wild shiny Pokemon in a battle ---------------------------------------------------------

// BattleMon.shiny of the foes (the game works it out from the IDs and personality when the
// battle starts). Update gives the species the moment one is seen, once for each battle.
class ShinyWatch
{
public:
    int Update(const HudBattleUi::Mem& m)
    {
        const uint32_t bs = HudBattleUi::Battle(m);
        if (!bs)
        {
            told = debugged = false;
            return 0;
        }
        if (told || (m.r32(bs + 0x2C) & 1)) return 0; // already said, or not a wild battle
        const uint32_t ctx = m.r32(bs + 0x30), n = m.r32(bs + 0x44);
        for (uint32_t i = 1; i < n && i < 4; i += 2)
        {
            const uint32_t mon = ctx + 0x2D40 + 0xC0 * i;
            const int species = (int)m.r16(mon), level = (int)m.r8(mon + 0x34);
            if (species < 1 || species > 493 || level < 1 || level > 100) continue;
            if (getenv("SSR_QOL_DEBUG") && !debugged)
                debugged = true, fprintf(stderr, "[shiny] foe %u: species %d level %d flag %d personality %08x id %08x\n", i, species, level,
                    (m.r8(mon + 0x26) >> 5) & 1, m.r32(mon + 0x6C), m.r32(mon + 0x78));
            if (!(m.r8(mon + 0x26) & 0x20)) continue;
            told = true;
            return species;
        }
        return 0;
    }

private:
    bool told = false, debugged = false;
};

// People you have not talked to ------------------------------------------------------------

struct Person
{
    uint32_t key = 0; // map << 16 | the object's id
    double x = 0, y = 0, z = 0; // world position (fx32)
};

class TalkTracker
{
public:
    std::vector<Person> Unread; // on this map, in view of the game, not talked to
    int Map = -1;

    void SetPath(const std::string& p)
    {
        path = p;
        std::vector<std::string> lines;
        if (ReadLines(path, lines))
            for (const std::string& s : lines) done.insert((uint32_t)strtoul(s.c_str(), nullptr, 16));
    }

    // Whether this object is somebody one talks to: a person (not rocks, trees, balls on the
    // ground, signs or a Pokemon following the player) with a script.
    // shortcut: the sprite numbers are the decompilation's; people with an unusual sprite
    // that are not talkable may get a bubble, and the odd talkable one may not.
    static bool Talkable(const Ram& r, uint32_t o)
    {
        const uint32_t sprite = r.R32(o + 0x10);
        return (r.R32(o) & 0x201) == 1 && sprite <= 330 && !(sprite >= 84 && sprite <= 87) && r.R32(o + 0x20) != 0 &&
            r.R32(o + 0x20) != 0xFFFF;
    }

    void Update(const Ram& r, bool busy)
    {
        Unread.clear();
        const uint32_t fs = r.R32(kFieldSystem);
        const uint32_t loc = r.R32(fs + 0x20), mgr = r.R32(fs + 0x3C), me = r.R32(r.R32(fs + 0x40) + 0x30);
        const uint32_t count = r.R32(mgr + 4), objs = r.R32(mgr + 0x124);
        if (!r.Ptr(fs) || !r.Ptr(loc) || !r.Ptr(mgr) || !r.Ptr(me) || !r.Ptr(objs) || count > 255)
        {
            talking = false;
            return;
        }
        Map = (int)r.R32(loc);
        // Talking: the player has stopped for a conversation (the same flag the frontend's own
        // walk-and-talk waits for). The person is the one on the tile ahead, or the next one
        // (behind a counter).
        const bool nowTalking = (r.R32(me) & 0x40) != 0;
        if (nowTalking && !talking)
        {
            static const int dx[4] = { 0, 0, -1, 1 }, dz[4] = { -1, 1, 0, 0 };
            const int face = (int)r.R32(me + 0x28) & 3, px = (int)r.R32(me + 0x64), pz = (int)r.R32(me + 0x6C);
            bool found = false;
            for (int d = 1; d <= 2 && !found; d++)
                for (uint32_t i = 0; i < count && !found; i++)
                {
                    const uint32_t o = objs + i * 0x12C;
                    if (o == me || !Talkable(r, o)) continue;
                    if ((int)r.R32(o + 0x64) == px + dx[face] * d && (int)r.R32(o + 0x6C) == pz + dz[face] * d)
                    {
                        found = true;
                        Mark(Key(r, o));
                    }
                }
        }
        talking = nowTalking;
        if (busy) return;
        for (uint32_t i = 0; i < count; i++)
        {
            const uint32_t o = objs + i * 0x12C;
            if (o == me || !Talkable(r, o) || done.count(Key(r, o))) continue;
            Person p;
            p.key = Key(r, o);
            p.x = (int32_t)r.R32(o + 0x70);
            p.y = (int32_t)r.R32(o + 0x74);
            p.z = (int32_t)r.R32(o + 0x78);
            Unread.push_back(p);
        }
    }

private:
    std::string path;
    std::set<uint32_t> done;
    bool talking = false;

    uint32_t Key(const Ram& r, uint32_t o) const { return (uint32_t)Map << 16 | (r.R32(o + 8) & 0xFFFF); }
    void Mark(uint32_t key)
    {
        if (!done.insert(key).second) return;
        std::vector<std::string> lines;
        char b[16];
        for (uint32_t k : done) snprintf(b, sizeof b, "%x", k), lines.push_back(b);
        WriteLines(path, lines);
    }
};

// A speech bubble with "..." in it, its point at (x, y) design pixels.
inline void TalkBubble(Canvas& c, const Ui& ui, float x, float y)
{
    const float w = 60, h = 40;
    HudParty::FillRound(c, ui, x - w / 2 - 2, y - h - 14 - 2, w + 4, h + 4, h / 2 + 2, 0xFF3B3F4A);
    HudParty::FillConvex(c, ui, { { x - 11, y - 15 }, { x + 11, y - 15 }, { x, y } }, 0xFF3B3F4A);
    HudParty::FillRound(c, ui, x - w / 2, y - h - 14, w, h, h / 2, 0xFFFFFFFF);
    HudParty::FillConvex(c, ui, { { x - 9, y - 17 }, { x + 9, y - 17 }, { x, y - 3 } }, 0xFFFFFFFF);
    for (int i = -1; i <= 1; i++) HudParty::FillRound(c, ui, x + i * 14 - 4, y - h / 2 - 14 - 4, 8, 8, 4, 0xFF3B3F4A);
}

// A waypoint --------------------------------------------------------------------------------

struct Waypoint
{
    bool set = false;
    double x = 0, z = 0; // tiles, on the outdoor map
    std::string name;

    void SetPath(const std::string& p)
    {
        path = p;
        std::vector<std::string> lines;
        if (!ReadLines(path, lines) || lines.size() < 2) return;
        x = atof(lines[0].c_str());
        z = atof(lines[1].c_str());
        name = lines.size() > 2 ? lines[2] : "";
        set = true;
    }
    void Put(double tx, double tz, const std::string& label)
    {
        set = true, x = tx, z = tz, name = label;
        char a[32], b[32];
        snprintf(a, sizeof a, "%.1f", x), snprintf(b, sizeof b, "%.1f", z);
        WriteLines(path, { a, b, name });
    }
    void Clear()
    {
        set = false;
        if (!path.empty()) remove(path.c_str());
    }
    // The Town Map's cell (with its two rows on top) the waypoint is in: a cell is 32 tiles.
    int CellX() const { return (int)std::floor(x / 32); }
    int CellY() const { return (int)std::floor(z / 32) + 2; }

private:
    std::string path;
};

// A map pin, its point at (x, y) design pixels.
inline void Pin(Canvas& c, const Ui& ui, float x, float y, float s)
{
    HudParty::FillConvex(c, ui, { { x - s * 0.42f, y - s * 0.62f }, { x + s * 0.42f, y - s * 0.62f }, { x, y } }, 0xFF8E1F26);
    HudParty::FillRound(c, ui, x - s * 0.5f, y - s * 1.26f, s, s, s / 2, 0xFF8E1F26);
    HudParty::FillRound(c, ui, x - s * 0.42f, y - s * 1.18f, s * 0.84f, s * 0.84f, s * 0.42f, 0xFFF0505A);
    HudParty::FillRound(c, ui, x - s * 0.16f, y - s * 0.92f, s * 0.32f, s * 0.32f, s * 0.16f, 0xFFFFFFFF);
}

// The Town Map's target for a press: the place under the cursor if there is one (the middle
// of its cells), else the cursor's cell; in tiles of the outdoor map. Cells count two rows
// down from the top of the map.
template <class State>
inline void TownMapTarget(const State& st, double& tx, double& tz, std::string& name)
{
    double cx = st.curX + 0.5, cy = st.curY + 0.5;
    name = "";
    if (st.selMap >= 0)
        for (const auto& p : st.places)
            if (p.map == st.selMap)
            {
                cx = p.x + p.w / 2.0;
                cy = p.y + p.h / 2.0;
                name = Party_PlaceName(p.map);
                break;
            }
    tx = cx * 32;
    tz = (cy - 2) * 32;
}

// Sorting a pocket -------------------------------------------------------------------------

enum SortMode { SortGame, SortName, SortKind, SortNewest, SortCount };
inline const char* SortName_(int m)
{
    static const char* n[SortCount] = { "Game order", "A to Z", "By kind", "Newest first" };
    return n[m];
}

// The bag's pockets are arrays of 4 bytes an item (id, quantity) that the bag screen reads
// each time it draws: sorting rewrites the order of one pocket in memory, as moving the items
// one by one with the game's own Move would. (The game has no sort of its own to call.)
// `Remember` notes what is in the bag, so that Newest has something to go by; the stamps and
// the pocket's order before its first sort are kept (the stamps beside the save).
class BagSorter
{
public:
    void SetPath(const std::string& p)
    {
        path = p;
        std::vector<std::string> lines;
        if (ReadLines(path, lines))
            for (const std::string& s : lines)
            {
                unsigned id, stamp;
                if (sscanf(s.c_str(), "%u %u", &id, &stamp) == 2) stamps[id] = stamp, next = std::max(next, stamp + 1);
            }
    }

    // Gives new items their stamp, the next in the order they sit in the pocket.
    void Remember(const std::vector<std::pair<int, int>>& items)
    {
        bool added = false;
        for (const auto& it : items)
            if (!stamps.count(it.first)) stamps[it.first] = next++, added = true;
        if (!added) return;
        std::vector<std::string> lines;
        char b[48];
        for (const auto& s : stamps) snprintf(b, sizeof b, "%d %u", s.first, s.second), lines.push_back(b);
        WriteLines(path, lines);
    }

    // Reorders the pocket whose slots are at `slots` (count items) in memory `ram`.
    // `name(id)` is the item's name, for A to Z.
    template <class NameFn>
    void Apply(uint8_t* ram, uint32_t mask, uint32_t slots, int count, int pocket, SortMode mode, NameFn name)
    {
        if (count < 2 || (slots >> 24) != 0x02) return;
        uint8_t* base = ram + (slots & mask);
        std::vector<std::pair<int, int>> items;
        for (int k = 0; k < count; k++) items.push_back({ base[4 * k] | base[4 * k + 1] << 8, base[4 * k + 2] | base[4 * k + 3] << 8 });
        // The order the player had, noted before the first sort of this pocket this time.
        std::vector<int>& original = originals[pocket];
        if (original.empty())
            for (const auto& it : items) original.push_back(it.first);
        auto position = [&](int id) {
            const auto at = std::find(original.begin(), original.end(), id);
            return at == original.end() ? (int)original.size() : (int)(at - original.begin());
        };
        std::stable_sort(items.begin(), items.end(), [&](const std::pair<int, int>& a, const std::pair<int, int>& b) {
            switch (mode)
            {
            case SortName: return Lower(name(a.first)) < Lower(name(b.first));
            case SortKind: return a.first < b.first;
            case SortNewest: return stamps[a.first] > stamps[b.first];
            default: return position(a.first) < position(b.first);
            }
        });
        for (int k = 0; k < count; k++)
        {
            base[4 * k] = (uint8_t)items[k].first;
            base[4 * k + 1] = (uint8_t)(items[k].first >> 8);
            base[4 * k + 2] = (uint8_t)items[k].second;
            base[4 * k + 3] = (uint8_t)(items[k].second >> 8);
        }
        if (mode == SortGame) original.clear();
    }

    int mode[8] = {}; // the sort of each pocket, as chosen last

private:
    std::string path;
    std::map<int, unsigned> stamps;
    unsigned next = 1;
    std::map<int, std::vector<int>> originals;

    static std::string Lower(std::string s)
    {
        for (char& ch : s) ch = (char)tolower((unsigned char)ch);
        return s;
    }
};
}
