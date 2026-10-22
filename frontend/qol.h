#pragma once
// Helpers: small conveniences in the Game tab of the pause menu, each Off until it is chosen.
//
// Most of them change the game's memory between two frames, the same way on the console
// that plays and, under --lockstep, on the interpreter's console as well (Mem writes both),
// so that the recompiled code and the interpreter still see the same game. They find what
// they need through the FieldSystem's save data (the blocks of include/constants/save_arrays.h)
// and, in a battle, through the BattleContext, with the structs of pokeheartgold.
//
//   Exp. Share for all   the party members that are not fainted take part in every battle's EXP
//   Infinite Repel       the Repel's steps (RoamerSaveData +0x65) are topped up
//   Infinite TMs         a TM used from the Bag is put back
//   Level limit          EXP stops at the next Gym Leader's top level (Level cap); No grind lifts
//                        the party up to it
//   Fixed-speed HP bars  the redrawn battle boxes run an HP bar down at one speed (FixedHp)
//   Auto-Heal Kit        heals the party with medicine from the Bag, cheapest first
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ndsrom.h"
#include "party.h"

namespace Qol
{

struct Options
{
    bool expShare = false, infRepel = false, infTm = false, fixedHp = false;
    int levelMode = 0;        // 0 off, 1 level cap, 2 no grind
    bool autoHeal = false;

    bool Load(const std::string& k, const std::string& v)
    {
        if (k == "helper_exp_share") expShare = v == "1";
        else if (k == "helper_repel") infRepel = v == "1";
        else if (k == "helper_tm") infTm = v == "1";
        else if (k == "helper_hp_bars") fixedHp = v == "1";
        else if (k == "helper_level") levelMode = std::clamp(atoi(v.c_str()), 0, 2);
        else if (k == "helper_heal") autoHeal = v == "1";
        else return false;
        return true;
    }
    void Save(std::ostream& f) const
    {
        f << "helper_exp_share=" << expShare << "\nhelper_repel=" << infRepel << "\nhelper_tm=" << infTm << "\nhelper_hp_bars=" << fixedHp
          << "\nhelper_level=" << levelMode << "\nhelper_heal=" << autoHeal << "\n";
    }
};

// The top level of the next Gym Leader's team (the game's trainer data: Falkner 13, Bugsy 17,
// Whitney 19, Morty 25, Chuck 31, Jasmine 35, Pryce 34, Clair 41; then the Elite Four and Lance
// up to 50 with Lt. Surge's 53 as the next after them, and Kanto's Leaders 53, 55, 54, 56, 50,
// 54, 59 and Blue's 60), for the badges the player has, Johto's and Kanto's together. The
// cap never goes down as badges come in.
inline int LevelCap(int badges)
{
    static const int cap[17] = { 13, 17, 19, 25, 31, 35, 35, 41, 53, 55, 55, 56, 56, 56, 59, 60, 100 };
    return cap[std::clamp(badges, 0, 16)];
}

// The HP bar of a redrawn battle box, at one speed whatever the Pokemon's HP: a whole bar takes
// 48 frames to run down. The game's own bar (it moves a point a frame for a small HP and
// faster for a big one) goes on underneath, so this only shows the new HP sooner when the
// game's is slower; never later. `shown` is the game's value for the box, `target` the HP
// it is heading for, `most` the Pokemon's full HP.
inline bool FixedHpBars = false;
inline int FixedHp(int slot, int shown, int target, int most)
{
    static float v[4] = { -1, -1, -1, -1 };
    if (slot < 0 || slot > 3 || most <= 0) return shown;
    float& f = v[slot];
    if (f < 0 || shown == target || std::abs(f - target) > most) f = (float)shown;
    if (shown == target) return shown;
    const float step = most / 48.0f;
    f += f < target ? std::min(step, target - f) : -std::min(step, f - target);
    // Never behind the game's own bar.
    if (target > shown) f = std::max(f, (float)shown);
    else f = std::min(f, (float)shown);
    const int out = (int)(f + (target > shown ? 0.0f : 0.999f));
    return std::clamp(out, std::min(shown, target), std::max(shown, target));
}

// The game's memory (main RAM), written to the reference console of --lockstep as well.
struct Mem
{
    uint8_t* ram = nullptr;
    uint8_t* ref = nullptr;
    uint32_t mask = 0;
    static bool In(uint32_t a) { return a >= 0x02000000 && a < 0x02400000; }
    uint32_t r8(uint32_t a) const { return In(a) ? ram[a & mask] : 0; }
    uint32_t r16(uint32_t a) const { return In(a) ? (uint32_t)(ram[a & mask] | ram[(a + 1) & mask] << 8) : 0; }
    uint32_t r32(uint32_t a) const { return In(a) ? r16(a) | r16(a + 2) << 16 : 0; }
    void w8(uint32_t a, uint32_t v) const
    {
        if (!In(a)) return;
        ram[a & mask] = (uint8_t)v;
        if (ref) ref[a & mask] = (uint8_t)v;
    }
    void w16(uint32_t a, uint32_t v) const { w8(a, v), w8(a + 1, v >> 8); }
    void w32(uint32_t a, uint32_t v) const { w16(a, v), w16(a + 2, v >> 16); }
};

// What the frontend knows and the helpers need.
struct Env
{
    Mem mem;
    uint32_t frame = 0;
    bool idle = false;                // the player walking about, nothing of the game open
    uint32_t partyItem = 0;           // the item the party menu is applying (using an item), else 0
    const uint8_t* rom = nullptr;
    size_t romSize = 0;
    std::function<const uint8_t*(int, uint32_t&)> itemBytes; // an item's data (RomArt::ItemBytes)
};

// A Pokemon of the party (236 bytes) with its parts decrypted, to read and change, and put
// back as the game does: the party's part (status, level, HP and stats) encrypted with the
// personality, the box part (four blocks in an order the personality gives) with its checksum.
struct Mon
{
    uint8_t d[236];
    uint32_t addr = 0, pid = 0;
    uint8_t* blk[4] = {};            // blocks A to D, plain
    static void Crypt(uint8_t* p, int words, uint32_t seed)
    {
        for (int w = 0; w < words; w++)
        {
            seed = seed * 0x41C64E6D + 0x6073;
            const uint32_t v = (p[2 * w] | p[2 * w + 1] << 8) ^ (seed >> 16);
            p[2 * w] = (uint8_t)v;
            p[2 * w + 1] = (uint8_t)(v >> 8);
        }
    }
    bool Load(const Mem& m, uint32_t a)
    {
        if (!Mem::In(a) || !Mem::In(a + 235)) return false;
        for (int i = 0; i < 236; i++) d[i] = (uint8_t)m.r8(a + i);
        addr = a;
        // (Decrypted in place, or a Bad Egg: the game is in the middle of something with it.)
        if (RomGet16(d + 4) & 7) return false;
        pid = RomGet32(d);
        const uint32_t check = RomGet16(d + 6);
        Crypt(d + 8, 64, check);
        uint32_t sum = 0;
        for (int w = 0; w < 64; w++) sum += RomGet16(d + 8 + 2 * w);
        if ((sum & 0xFFFF) != check) return false;
        Crypt(d + 0x88, 50, pid);
        static const char* orders[24] = { "ABCD", "ABDC", "ACBD", "ACDB", "ADBC", "ADCB", "BACD", "BADC", "BCAD", "BCDA", "BDAC", "BDCA",
            "CABD", "CADB", "CBAD", "CBDA", "CDAB", "CDBA", "DABC", "DACB", "DBAC", "DBCA", "DCAB", "DCBA" };
        const char* order = orders[((pid >> 13) & 31) % 24];
        for (int k = 0; k < 4; k++) blk[order[k] - 'A'] = d + 8 + 32 * k;
        return species() >= 1 && species() <= 493;
    }
    // Encrypts it again and writes it back.
    void Store(const Mem& m)
    {
        Crypt(d + 0x88, 50, pid);
        uint32_t sum = 0;
        for (int w = 0; w < 64; w++) sum += RomGet16(d + 8 + 2 * w);
        d[6] = (uint8_t)sum;
        d[7] = (uint8_t)(sum >> 8);
        Crypt(d + 8, 64, sum & 0xFFFF);
        for (int i = 6; i < 236; i++) m.w8(addr + i, d[i]);
    }
    uint32_t species() const { return RomGet16(blk[0]); }
    uint32_t exp() const { return RomGet32(blk[0] + 8); }
    void setExp(uint32_t v) { for (int i = 0; i < 4; i++) blk[0][8 + i] = (uint8_t)(v >> (8 * i)); }
    bool egg() const { return RomGet32(blk[1] + 0x10) >> 30 & 1; }
    uint32_t status() const { return RomGet32(d + 0x88); }
    void setStatus(uint32_t v) { for (int i = 0; i < 4; i++) d[0x88 + i] = (uint8_t)(v >> (8 * i)); }
    int level() const { return d[0x8C]; }
    int hp() const { return (int)RomGet16(d + 0x8E); }
    void setHp(int v) { d[0x8E] = (uint8_t)v, d[0x8F] = (uint8_t)(v >> 8); }
    int stat(int i) const { return (int)RomGet16(d + 0x90 + 2 * i); } // 0 HP (most), then Attack, Defense, Speed, Sp. Atk, Sp. Def
    void setStat(int i, int v) { d[0x90 + 2 * i] = (uint8_t)v, d[0x91 + 2 * i] = (uint8_t)(v >> 8); }
};

class Helpers
{
public:
    // The pause menu rows, ids from 0 (the caller adds its own base).
    struct Row
    {
        int id;
        std::string label, value, desc;
        bool disabled = false;
    };

    // Once per game frame, after the console has run it.
    void Tick(const Options& o, const Env& e)
    {
        const Mem& m = e.mem;
        const uint32_t fs = m.r32(FieldSystemPtr);
        if (!Mem::In(fs) || !Mem::In(m.r32(fs + 0x0C))) return;
        if (!logged && getenv("SSR_QOL_LOG")) Log(m), logged = true;
        if (o.infRepel)
        {
            const uint32_t roam = Block(m, 21, 0);
            if (roam && m.r8(roam + 0x65) != 255) m.w8(roam + 0x65, 255);
        }
        if (o.expShare) ExpShare(m);
        if (o.levelMode) Levels(o, e);
        if (o.infTm) KeepTms(m, e);
        if (o.expShare != lastExpShare || o.levelMode != lastLevelMode)
        {
            lastExpShare = o.expShare;
            lastLevelMode = o.levelMode;
        }
    }

    std::vector<Row> Rows(const Options& o, const Env& e)
    {
        auto onOff = [](bool v) { return std::string(v ? "On" : "Off"); };
        std::vector<Row> r;
        r.push_back({ H_HEADER, "Helpers", "", "Conveniences for a relaxed game. Each one is Off until you turn it on.", true });
        r.push_back({ H_EXPSHARE, "Exp. Share for all", onOff(o.expShare),
            "Every Pok\xC3\xA9mon in your party that has not fainted takes part in each battle's experience, shared out equally. Select + R turns it on and off on the overworld." });
        r.push_back({ H_REPEL, "Infinite Repel", onOff(o.infRepel), "Keeps a Repel working all the time, without using up the item." });
        r.push_back({ H_TM, "Infinite TMs", onOff(o.infTm), "A TM stays in your Bag after you teach it. HMs always did." });
        const int badges = Badges(e.mem);
        r.push_back({ H_LEVEL, "Level limit", o.levelMode == 1 ? "Level cap" : o.levelMode == 2 ? "No grind" : "Off",
            std::string("Level cap: your Pok\xC3\xA9mon stop gaining experience at the top level of the next Gym Leader's team (now ") +
            std::to_string(LevelCap(badges)) + "). No grind: they are lifted up to it as well, at once." });
        r.push_back({ H_HPBARS, "Fixed-speed HP bars", onOff(o.fixedHp),
            "The HP bars of the new battle boxes run up or down at one speed, whatever the Pok\xC3\xA9mon's HP." });
        r.push_back({ H_HEAL, "Auto-Heal Kit", onOff(o.autoHeal),
            "Adds an entry that heals your party with the medicine in your Bag, the cheapest first. It never revives." });
        if (o.autoHeal)
            r.push_back({ H_HEALNOW, "Heal my party", "", "Uses Potions, Antidotes and the rest from your Bag on the Pok\xC3\xA9mon that need them." });
        return r;
    }

    // An activated row; `dir` is -1 or +1 for left and right, 0 for A. True when the rows change.
    bool Activate(int id, int dir, Options& o, const Env& e, std::string& say)
    {
        switch (id)
        {
        case H_EXPSHARE: o.expShare = !o.expShare; return true;
        case H_REPEL: o.infRepel = !o.infRepel; return true;
        case H_TM: o.infTm = !o.infTm; return true;
        case H_LEVEL: o.levelMode = (o.levelMode + (dir < 0 ? 2 : 1)) % 3; return true;
        case H_HPBARS: o.fixedHp = !o.fixedHp; return true;
        case H_HEAL: o.autoHeal = !o.autoHeal; return true;
        case H_HEALNOW:
            if (dir == 0) say = HealParty(e);
            return false;
        }
        return false;
    }

    static constexpr int RowCount = 16;

    // Test aid: SSR_QOL_DO="heal@2100,expshare@2200" does those at those frames.
    void TestActions(Options& o, const Env& e, std::string& say)
    {
        static const char* spec = getenv("SSR_QOL_DO");
        if (!spec) return;
        std::string s = spec;
        size_t at = 0;
        while (at < s.size())
        {
            size_t end = s.find(',', at);
            if (end == std::string::npos) end = s.size();
            const std::string item = s.substr(at, end - at);
            at = end + 1;
            const size_t eq = item.find('@');
            if (eq == std::string::npos || (uint32_t)atoi(item.c_str() + eq + 1) != e.frame) continue;
            const std::string what = item.substr(0, eq);
            if (what == "heal") say = HealParty(e);
            else if (what == "expshare") o.expShare = !o.expShare;
            if (!say.empty()) fprintf(stderr, "[helpers] frame %u: %s -> %s\n", e.frame, what.c_str(), say.c_str());
        }
    }


private:
    enum { H_HEADER, H_EXPSHARE, H_REPEL, H_TM, H_LEVEL, H_HPBARS, H_HEAL, H_HEALNOW };
    static constexpr uint32_t FieldSystemPtr = 0x021D4158;

    bool logged = false, lastExpShare = false;
    int lastLevelMode = 0;
    // The data of the ROM, kept once read.
    std::vector<std::pair<uint32_t, uint32_t>> personal, growth;
    const uint8_t* cachedRom = nullptr;

    // The save data's block `id` (SaveArray index) when it has `size` bytes (0: any size).
    static uint32_t Block(const Mem& m, uint32_t id, uint32_t size)
    {
        const uint32_t save = m.r32(m.r32(FieldSystemPtr) + 0x0C), h = save + 0x23014 + 16 * id;
        if (!Mem::In(save) || m.r32(h) != id || (size && m.r32(h + 4) != size)) return 0;
        return save + 0x10 + m.r32(h + 8);
    }
    static int Badges(const Mem& m)
    {
        // PlayerProfile (the start of block 1): Johto's badges at +0x1A and Kanto's at +0x1F, a bit each.
        const uint32_t p = Block(m, 1, 0);
        return p ? __builtin_popcount(m.r8(p + 0x1A)) + __builtin_popcount(m.r8(p + 0x1F)) : 0;
    }
    static int PartyCount(const Mem& m, uint32_t party)
    {
        return party && m.r32(party) == 6 && m.r32(party + 4) >= 1 && m.r32(party + 4) <= 6 ? (int)m.r32(party + 4) : 0;
    }

    void Log(const Mem& m)
    {
        const uint32_t fs = m.r32(FieldSystemPtr), save = m.r32(fs + 0x0C);
        fprintf(stderr, "[helpers] save %08X; blocks:", save);
        for (uint32_t id : { 1u, 2u, 3u, 5u, 21u })
        {
            const uint32_t h = save + 0x23014 + 16 * id;
            fprintf(stderr, " %u(size %X at %X)", id, m.r32(h + 4), m.r32(h + 8));
        }
        fprintf(stderr, "\n[helpers] badges %d, cap %d, repel %u, last spawn %u\n", Badges(m), LevelCap(Badges(m)),
            Block(m, 21, 0) ? m.r8(Block(m, 21, 0) + 0x65) : 0, Block(m, 5, 0) ? m.r16(Block(m, 5, 0) + 0x68) : 0);
    }

    // ---- Exp. Share for all ------------------------------------------------------------
    // In a battle the game's BattleContext keeps, for each side, which party slots took part
    // (unk_A4, at +0xA4, one bit a slot) and the EXP of a fainted foe goes to those and to
    // the ones holding an Exp. Share. Marking every Pokemon that is not fainted as taking part
    // makes the game share the EXP equally among them, with its own messages and level-ups;
    // nothing is held, so no item is lost or shown.
    static uint32_t BattleContext(const Mem& m)
    {
        const uint32_t fs = m.r32(FieldSystemPtr), pm = m.r32(fs), app = m.r32(pm + 4);
        if (!Mem::In(fs) || !Mem::In(pm) || !Mem::In(app) || m.r32(app + 0xC) != 12) return 0;
        const uint32_t bs = m.r32(app + 0x1C), ctx = m.r32(bs + 0x30), bi = m.r32(bs + 0x19C);
        if (!Mem::In(bs) || !Mem::In(ctx) || !Mem::In(bi) || m.r32(bi) != bs || m.r32(bs + 0x44) < 1) return 0;
        return ctx;
    }
    void ExpShare(const Mem& m)
    {
        const uint32_t ctx = BattleContext(m), party = Block(m, 2, 0);
        const int n = PartyCount(m, party);
        if (!ctx || !n) return;
        uint32_t alive = 0;
        for (int i = 0; i < n; i++)
        {
            Mon mon;
            if (mon.Load(m, party + 8 + 236 * i) && !mon.egg() && mon.hp() > 0) alive |= 1u << i;
        }
        for (int side = 0; side < 2; side++)
        {
            const uint32_t a = ctx + 0xA4 + 4 * side, now = m.r32(a);
            if ((now | alive) != now) m.w32(a, now | alive);
        }
    }

    // ---- Level cap and No grind --------------------------------------------------------
    bool RomTables(const Env& e)
    {
        if (!e.rom) return false;
        if (cachedRom != e.rom)
        {
            personal = NdsNarcFiles(e.rom, e.romSize, "a/0/0/2");
            growth = NdsNarcFiles(e.rom, e.romSize, "a/0/0/3");
            cachedRom = e.rom;
        }
        return personal.size() > 493 && growth.size() >= 8;
    }
    // The experience a level starts at for a species (its growth rate: byte 0x13 of its data).
    uint32_t ExpAt(const Env& e, int species, int level) const
    {
        const uint8_t* ps = e.rom + personal[species].first;
        const auto& g = growth[std::min<int>(ps[0x13], (int)growth.size() - 1)];
        return level < 1 ? 0 : RomGet32(e.rom + g.first + 4 * std::min(level, 100));
    }
    // The stats a Pokemon has at a level (Gen IV's formulas); false if its numbers are not
    // what they would be at its own level, so that something about it is not understood (a
    // form with data of its own, say) and it is left alone.
    bool Stats(const Env& e, const Mon& mon, int level, int out[6]) const
    {
        const uint8_t* ps = e.rom + personal[mon.species()].first;
        const uint32_t ivs = RomGet32(mon.blk[1] + 0x10), nature = mon.pid % 25;
        const int inc = (int)nature / 5 + 1, dec = (int)nature % 5 + 1; // stat index 1-5 (Attack, Defense, Speed, Sp. Atk, Sp. Def)
        for (int i = 0; i < 6; i++)
        {
            const int base = ps[i], iv = ivs >> (5 * i) & 31, ev = mon.blk[0][0x10 + i];
            const int core = (2 * base + iv + ev / 4) * level / 100;
            if (i == 0) out[0] = mon.species() == 292 ? 1 : core + level + 10;
            else
            {
                int v = core + 5;
                if (inc != dec) v = inc == i ? v * 110 / 100 : dec == i ? v * 90 / 100 : v;
                out[i] = v;
            }
        }
        return true;
    }
    void Levels(const Options& o, const Env& e)
    {
        const Mem& m = e.mem;
        const uint32_t party = Block(m, 2, 0);
        const int n = PartyCount(m, party);
        if (!n || !RomTables(e)) return;
        const int cap = LevelCap(Badges(m));
        const uint32_t ctx = BattleContext(m);
        for (int i = 0; i < n; i++)
        {
            Mon mon;
            if (!mon.Load(m, party + 8 + 236 * i) || mon.egg() || personal.size() <= mon.species()) continue;
            int level = mon.level();
            if (level < 1 || level > 100) continue;
            bool changed = false;
            // Level cap: EXP stops at the last point of the cap's level (a Pokemon already above it
            // is left as it is).
            const uint32_t top = cap >= 100 ? ExpAt(e, mon.species(), 100) : ExpAt(e, mon.species(), cap + 1) - 1;
            if (level <= cap && mon.exp() > top)
            {
                mon.setExp(top);
                changed = true;
                if (ctx)
                    for (int k = 0; k < 4; k++)
                        if (m.r32(ctx + 0x2D40 + 0xC0 * k + 0x68) == mon.pid && m.r16(ctx + 0x2D40 + 0xC0 * k) == mon.species())
                            m.w32(ctx + 0x2D40 + 0xC0 * k + 0x64, top);
            }
            // No grind: up to the cap at once, on the overworld only.
            if (o.levelMode == 2 && e.idle && level < cap && cap < 100)
            {
                int now[6], then[6];
                if (Stats(e, mon, level, now) && now[0] == mon.stat(0) && now[1] == mon.stat(1) && now[2] == mon.stat(2) && now[3] == mon.stat(3) &&
                    now[4] == mon.stat(4) && now[5] == mon.stat(5) && Stats(e, mon, cap, then))
                {
                    const int hp = mon.hp(), most = mon.stat(0);
                    for (int s = 0; s < 6; s++) mon.setStat(s, then[s]);
                    mon.setHp(hp > 0 ? std::min(then[0], hp + std::max(0, then[0] - most)) : 0);
                    mon.d[0x8C] = (uint8_t)cap;
                    mon.setExp(std::max(mon.exp(), ExpAt(e, mon.species(), cap)));
                    changed = true;
                    fprintf(stderr, "[helpers] no grind: %s lifted from level %d to %d\n", Party_SpeciesName(mon.species()).c_str(), level, cap);
                }
            }
            if (changed) mon.Store(m);
        }
    }

    // ---- Infinite TMs ------------------------------------------------------------------
    // The Bag (block 3, include/bag_types_def.h): item slots of an id and a count, 4 bytes
    // each; the pockets in the order items 165, key items 50, TMs and HMs 101, mail 12,
    // medicine 40, berries 64, balls 24, battle items 30.
    static constexpr uint32_t TmPocket = (165 + 50) * 4, TmSlots = 101, MedicinePocket = (165 + 50 + 101 + 12) * 4, MedicineSlots = 40;
    std::vector<std::pair<int, int>> tmSnap; // the TM pocket's slots (item, count) as they were a frame ago
    uint32_t tmUseFrame = 0;
    int tmUseItem = 0;
    void KeepTms(const Mem& m, const Env& e)
    {
        const uint32_t bag = Block(m, 3, 0);
        if (!bag) return;
        if (e.partyItem >= 328 && e.partyItem < 428)
        {
            tmUseFrame = e.frame;
            tmUseItem = (int)e.partyItem;
        }
        std::vector<std::pair<int, int>> now;
        for (uint32_t i = 0; i < TmSlots; i++) now.push_back({ (int)m.r16(bag + TmPocket + 4 * i), (int)m.r16(bag + TmPocket + 4 * i + 2) });
        // A TM used a moment ago (the party menu was applying it) that has lost one: back it goes.
        if (tmUseItem && e.frame - tmUseFrame < 1800 && tmSnap.size() == TmSlots)
        {
            int before = 0, after = 0, at = -1;
            for (uint32_t i = 0; i < TmSlots; i++)
            {
                if (tmSnap[i].first == tmUseItem) before = tmSnap[i].second, at = (int)i;
                if (now[i].first == tmUseItem) after = now[i].second;
            }
            if (before > 0 && after < before && after == before - 1)
            {
                // Put the slot back where it was, moving the later ones along if it is gone.
                if (after == 0 && at >= 0)
                {
                    for (int i = (int)TmSlots - 1; i > at; i--) now[i] = now[i - 1];
                    now[at] = { tmUseItem, before };
                }
                else
                    for (auto& s : now)
                        if (s.first == tmUseItem) s.second = before;
                for (uint32_t i = 0; i < TmSlots; i++)
                {
                    m.w16(bag + TmPocket + 4 * i, now[i].first);
                    m.w16(bag + TmPocket + 4 * i + 2, now[i].second);
                }
                fprintf(stderr, "[helpers] TM %d put back (%d)\n", tmUseItem - 327, before);
                tmUseItem = 0;
            }
        }
        tmSnap = now;
    }

    // ---- Auto-Heal Kit -----------------------------------------------------------------
    struct Medicine
    {
        uint32_t slot;
        int item, qty, price, heal;     // heal: 255 all, 254 half, 253 a quarter, or the points
        uint32_t cures;                 // status bits it cures (sleep 1, poison 2, burn 4, freeze 8, paralysis 16)
    };
    std::string HealParty(const Env& e)
    {
        const Mem& m = e.mem;
        if (!e.idle) return "Not now.";
        const uint32_t bag = Block(m, 3, 0), party = Block(m, 2, 0);
        const int n = PartyCount(m, party);
        if (!bag || !n || !e.itemBytes) return "Not now.";
        // The medicine pocket: pure healing items only (not Revives, candies, Elixirs or vitamins),
        // from the ROM's data of each (ItemData: price, the status it cures at +0xE, the flags
        // for revive/level/evolve at +0xF, PP and EV effects at +0x12-0x14, HP restore at
        // +0x13 bit 2 and its amount at +0x1B).
        std::vector<Medicine> kit;
        for (uint32_t i = 0; i < MedicineSlots; i++)
        {
            const int item = (int)m.r16(bag + MedicinePocket + 4 * i), qty = (int)m.r16(bag + MedicinePocket + 4 * i + 2);
            uint32_t size = 0;
            const uint8_t* d = item && qty ? e.itemBytes(item, size) : nullptr;
            if (!d || size < 0x1C) continue;
            const bool hp = d[0x13] & 4, other = (d[0xE] & 0x80) || d[0xF] || d[0x10] || d[0x11] || d[0x12] || (d[0x13] & ~4) || (d[0x14] & 1);
            const uint32_t cures = d[0xE] & 0x1F;
            if (other || (!hp && !cures)) continue;
            kit.push_back({ i, item, qty, (int)RomGet16(d), hp ? d[0x1B] : 0, cures });
        }
        std::stable_sort(kit.begin(), kit.end(), [](const Medicine& a, const Medicine& b) { return a.price < b.price; });
        std::map<int, int> used;
        int healed = 0;
        for (int i = 0; i < n; i++)
        {
            Mon mon;
            if (!mon.Load(m, party + 8 + 236 * i) || mon.egg() || mon.hp() <= 0) continue; // never a fainted one
            const int most = mon.stat(0);
            int hp = mon.hp();
            uint32_t status = mon.status();
            const int hp0 = hp;
            const uint32_t status0 = status;
            auto use = [&](Medicine& k) {
                k.qty--;
                used[k.item]++;
            };
            // The status first: the cheapest medicine that cures it.
            const uint32_t major = (status & 7 ? 1u : 0u) | (status & 0x88 ? 2u : 0u) | (status & 0x10 ? 4u : 0u) | (status & 0x20 ? 8u : 0u) | (status & 0x40 ? 16u : 0u);
            if (major)
                for (Medicine& k : kit)
                    if (k.qty > 0 && (k.cures & major) == major)
                    {
                        use(k);
                        status &= ~0xFFu;
                        if (k.heal) hp = std::min(most, hp + Amount(k.heal, most));
                        break;
                    }
            for (Medicine& k : kit)
                while (k.heal && k.qty > 0 && hp < most)
                {
                    use(k);
                    hp = std::min(most, hp + Amount(k.heal, most));
                    if (k.cures & (status ? 31u : 0u)) status &= ~0xFFu;
                }
            if (hp != hp0 || status != status0)
            {
                mon.setHp(hp);
                mon.setStatus(status);
                mon.Store(m);
                healed++;
            }
        }
        if (used.empty()) return healed ? "Done." : "Nothing to heal, or nothing in the Bag to heal with.";
        for (const Medicine& k : kit)
        {
            m.w16(bag + MedicinePocket + 4 * k.slot + 2, k.qty);
            if (k.qty <= 0) m.w16(bag + MedicinePocket + 4 * k.slot, 0);
        }
        // The pocket has no gaps (the game keeps it so): later slots move up.
        std::vector<std::pair<int, int>> keep;
        for (uint32_t i = 0; i < MedicineSlots; i++)
            if (m.r16(bag + MedicinePocket + 4 * i) && m.r16(bag + MedicinePocket + 4 * i + 2))
                keep.push_back({ (int)m.r16(bag + MedicinePocket + 4 * i), (int)m.r16(bag + MedicinePocket + 4 * i + 2) });
        for (uint32_t i = 0; i < MedicineSlots; i++)
        {
            m.w16(bag + MedicinePocket + 4 * i, i < keep.size() ? keep[i].first : 0);
            m.w16(bag + MedicinePocket + 4 * i + 2, i < keep.size() ? keep[i].second : 0);
        }
        std::string s = "Healed " + std::to_string(healed) + (healed == 1 ? " Pok\xC3\xA9mon with" : " Pok\xC3\xA9mon with");
        bool first = true;
        for (const auto& [item, count] : used)
        {
            s += std::string(first ? " " : ", ") + std::to_string(count) + " " + Party_Text(222, item);
            first = false;
        }
        return s + ".";
    }
    static int Amount(int param, int most)
    {
        return most == 1 ? 1 : param == 255 ? most : param == 254 ? most / 2 : param == 253 ? most / 4 : param;
    }

};

}
