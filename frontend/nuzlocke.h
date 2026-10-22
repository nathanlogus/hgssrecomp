#pragma once
// A Nuzlocke built into the frontend (Off unless chosen): the first wild Pokemon met in each
// area is the only one that may be caught, and a Pokemon that faints is dead. The game is
// never changed; the frontend watches it and keeps the book.
//
//  - The Encounter Ledger: for each area (the map header's place) the first wild Pokemon met,
//    read from the battle (BattleSystem, as hudbattle.h) when the fight starts, and how it
//    ended (caught, got away, duplicate, ...). An encounter ends when the player is back in the
//    overworld and the Pokemon in the party and the PC are compared with those from before it.
//  - A Pokemon of the player's that faints in a battle is dead (the battle's BattleMon holds
//    its personality value, which names it); one that faints on the overworld is found by its
//    HP. Dead Pokemon are moved, once the overworld is quiet, from the party to the last PC
//    box, renamed Graveyard (a recoverable move: nothing is deleted). The same box takes a
//    Pokemon caught against the rules, so that the catch does not count.
//  - Hardlocke also blocks the Bag in battle (main.cpp, where the command buttons are pressed).
//  - Soul Link is not done: it needs the partner's deaths over the relay (see CHANGELOG).
//
// Enforced: dead Pokemon leave the party; catches that break the rules leave the party/PC;
// the Bag in a Hardlocke battle (buttons and keys). Only shown: the ledger's judgement of a
// second encounter *before* the ball is thrown (a toast says so), the types of a Monolocke
// or Duallocke starter, and gifts, trades and eggs (never judged).
#include "hudonline.h"
#include "hudparty.h"
#include <set>

namespace Nuz
{
enum Rule { Off, Nuzlocke, Hardlocke, Monolocke, SoulLink, Duallocke, RuleCount };
inline const char* RuleName(int r)
{
    static const char* n[RuleCount] = { "Off", "Nuzlocke", "Hardlocke", "Monolocke", "Soul Link", "Duallocke" };
    return r >= 0 && r < RuleCount ? n[r] : "Off";
}
inline const char* RuleAbout(int r)
{
    static const char* n[RuleCount] = { "Plain rules of the game.",
        "Only the first wild Pok\xC3\xA9mon in each area can be caught. A Pok\xC3\xA9mon that faints is dead and goes to the Graveyard box.",
        "The Nuzlocke, and no items in battle.",
        "The Nuzlocke, and every Pok\xC3\xA9mon must be of one type.",
        "The Nuzlocke, linked with a partner: their deaths are yours. (Not done yet: it plays as a Nuzlocke.)",
        "The Nuzlocke, and every Pok\xC3\xA9mon must be of one of two types." };
    return r >= 0 && r < RuleCount ? n[r] : "";
}

enum Status { Seen, Caught, Failed, Duplicate, WrongType, Released };
inline const char* StatusName(int s)
{
    static const char* n[] = { "Met", "Caught", "Got away", "Duplicate", "Wrong type", "Not allowed" };
    return s >= 0 && s <= Released ? n[s] : "";
}
inline uint32_t StatusColour(int s)
{
    static const uint32_t c[] = { 0xFF6890F0, 0xFF78C850, 0xFFC83C3C, 0xFFB8B8D0, 0xFFB8B8D0, 0xFFF08030 };
    return s >= 0 && s <= Released ? c[s] : 0xFFB8B8D0;
}

struct Entry
{
    int map = -1, sec = -1, species = 0, level = 0, status = Seen;
    bool shiny = false, dead = false;
    uint32_t pid = 0; // of the one caught
};
struct Grave
{
    uint32_t pid = 0;
    int species = 0, level = 0, map = -1;
    bool shiny = false, buried = false;
    bool fallen = true; // false: a catch that was not allowed, not a death
    std::string name, why;
};

// What the tracker needs of the running game each frame.
struct Env
{
    HudParty::Ram ram;
    std::vector<uint8_t*> mem; // writable main RAM: one console, or both of --lockstep's
    uint32_t mask = 0x3FFFFF;
    bool battle = false;       // the battle application runs
    bool idle = false;         // the overworld alone runs (no menu, battle or other screen)
    int map = -1;              // the map the player is on (-1 not known)
    uint32_t frame = 0;
};

constexpr uint32_t kFieldSystem = 0x021D4158;
constexpr int kGraveBox = 17;

class Tracker
{
public:
    int rule = Off;
    int type[2] = { -1, -1 }; // the types of a Monolocke / Duallocke (-1 none chosen)
    bool runOver = false;
    int alive = -1;           // living Pokemon in the party, as last counted (-1 not known)
    std::vector<Entry> ledger;
    std::vector<Grave> grave;
    std::vector<std::pair<std::string, std::string>> toasts; // text, icon: for main.cpp to show
    bool BagBlocked() const { return rule == Hardlocke && inBattle; }
    bool On() const { return rule != Off; }
    std::function<int(int map)> sec; // the area of a map

    void NewRun()
    {
        ledger.clear();
        grave.clear();
        runOver = false;
        inBattle = pendingEnd = false;
        Save();
    }

    // ---- the file
    std::string path;
    void Load(const std::string& p)
    {
        path = p;
        std::ifstream f(p);
        std::string line;
        while (std::getline(f, line))
        {
            std::vector<std::string> v;
            size_t a = 0;
            for (size_t b; (b = line.find('\t', a)) != std::string::npos; a = b + 1) v.push_back(line.substr(a, b - a));
            v.push_back(line.substr(a));
            auto n = [&](size_t i) { return i < v.size() ? atoi(v[i].c_str()) : 0; };
            if (v[0] == "rule") rule = std::clamp(n(1), 0, (int)RuleCount - 1);
            else if (v[0] == "types") type[0] = n(1), type[1] = n(2);
            else if (v[0] == "over") runOver = n(1);
            else if (v[0] == "e" && v.size() >= 9)
            {
                Entry e;
                e.map = n(1), e.sec = n(2), e.species = n(3), e.level = n(4), e.status = std::clamp(n(5), 0, (int)Released);
                e.shiny = n(6), e.dead = n(7), e.pid = (uint32_t)strtoul(v[8].c_str(), nullptr, 10);
                ledger.push_back(e);
            }
            else if (v[0] == "g" && v.size() >= 9)
            {
                Grave g;
                g.pid = (uint32_t)strtoul(v[1].c_str(), nullptr, 10);
                g.species = n(2), g.level = n(3), g.map = n(4), g.shiny = n(5), g.buried = n(6), g.name = v[7], g.why = v[8], g.fallen = v.size() < 10 || n(9);
                grave.push_back(g);
            }
        }
    }
    void Save() const
    {
        if (path.empty()) return;
        std::ofstream f(path, std::ios::trunc);
        f << "rule\t" << rule << "\ntypes\t" << type[0] << "\t" << type[1] << "\nover\t" << runOver << "\n";
        for (const Entry& e : ledger)
            f << "e\t" << e.map << "\t" << e.sec << "\t" << e.species << "\t" << e.level << "\t" << e.status << "\t" << e.shiny << "\t" << e.dead << "\t" << e.pid << "\n";
        for (const Grave& g : grave)
            f << "g\t" << g.pid << "\t" << g.species << "\t" << g.level << "\t" << g.map << "\t" << g.shiny << "\t" << g.buried << "\t" << g.name << "\t" << g.why << "\n";
    }

    // ---- rules
    bool TypeOk(int species) const
    {
        if ((rule != Monolocke && rule != Duallocke) || type[0] < 0) return true;
        int t[2];
        if (!Party_SpeciesTypes(species, t)) return true;
        for (int k = 0; k < 2; k++)
            if (t[k] == type[0] || (rule == Duallocke && type[1] >= 0 && t[k] == type[1])) return true;
        return false;
    }
    int Fallen() const
    {
        int n = 0;
        for (const Grave& g : grave) n += g.fallen;
        return n;
    }
    int CaughtCount() const
    {
        int n = 0;
        for (const Entry& e : ledger) n += e.status == Caught;
        return n;
    }

    // ---- once per frame
    void Update(const Env& e)
    {
        if (!On())
        {
            inBattle = pendingEnd = false;
            return;
        }
        if (e.map >= 0) lastMap = e.map;
        if (e.battle)
        {
            Battle b;
            if (ReadBattle(e.ram, b))
            {
                if (!inBattle) StartBattle(e, b);
                if (inBattle)
                {
                    lastBattle = e.frame;
                    for (const BMon& m : b.mine)
                        if (m.hp == 0 && !m.egg && Counts(b)) Die(m, b);
                }
            }
        }
        else if (inBattle && e.frame > lastBattle + 20)
        {
            inBattle = false;
            pendingEnd = true;
        }
        if (!e.idle || e.frame < lastBattle + 60 || e.battle) return;
        if (pendingEnd) EndBattle(e);
        if (e.frame % 30 == 0) Tidy(e);
    }

private:
    bool inBattle = false, pendingEnd = false, illegal = false;
    int cur = -1; // the ledger row of this fight, -1 none
    int lastMap = -1;
    uint32_t lastBattle = 0;
    std::set<uint32_t> before;
    uint32_t foeType = 0;

    struct BMon
    {
        uint32_t pid = 0, otid = 0;
        int species = 0, level = 0, hp = 0;
        bool shiny = false, egg = false;
        std::string name;
    };
    struct Battle
    {
        uint32_t type = 0;
        BMon foe;
        std::vector<BMon> mine;
    };
    static bool Shiny(uint32_t pid, uint32_t otid)
    {
        const uint32_t x = (otid >> 16) ^ (otid & 0xFFFF) ^ (pid >> 16) ^ (pid & 0xFFFF);
        return x < 8;
    }
    // The battle from main RAM: see HudBattle::Read for where things are. The Pokemon in battle
    // (BattleMon, 0xC0 bytes each at BattleContext +0x2D40): species 0, moves 0xC, level 0x34,
    // nickname 0x36, HP 0x4C, personality 0x68, trainer id 0x74; the egg flag is bit 30 of the
    // word at 0x14. Even battler types (OpponentData +0x195) are the player's side.
    static bool ReadBattle(const HudParty::Ram& r, Battle& b)
    {
        const uint32_t fs = r.R32(kFieldSystem), pm = r.R32(fs), app = r.R32(pm + 4);
        if (!r.Ptr(fs) || !r.Ptr(pm) || !r.Ptr(app) || r.R32(app + 0xC) != 12) return false;
        const uint32_t bs = r.R32(app + 0x1C), ctx = r.R32(bs + 0x30), count = r.R32(bs + 0x44);
        if (!r.Ptr(bs) || !r.Ptr(ctx) || count < 2 || count > 4) return false;
        b = Battle();
        b.type = r.R32(bs + 0x2C);
        bool foe = false;
        for (uint32_t i = 0; i < count; i++)
        {
            const uint32_t od = r.R32(bs + 0x34 + 4 * i), a = ctx + 0x2D40 + 0xC0 * i;
            if (!r.Ptr(od)) return false;
            BMon m;
            m.species = (int)r.R16(a);
            m.level = (int)r.R8(a + 0x34);
            m.hp = (int)r.R32(a + 0x4C);
            m.pid = r.R32(a + 0x68);
            m.otid = r.R32(a + 0x74);
            m.egg = r.R32(a + 0x14) >> 30 & 1;
            m.shiny = Shiny(m.pid, m.otid);
            uint16_t nick[11];
            for (int k = 0; k < 11; k++) nick[k] = (uint16_t)r.R16(a + 0x36 + 2 * k);
            m.name = NdsText(nick, 11);
            if (m.species < 1 || m.species > 493 || m.level < 1 || m.level > 100 || m.hp < 0) continue;
            if (r.R8(od + 0x195) & 1)
            {
                if (!foe) b.foe = m, foe = true;
            }
            else
                b.mine.push_back(m);
        }
        return foe && !b.mine.empty();
    }
    // Plain wild battles (a roaming Pokemon aside): the trainer, link, Safari, Pal Park,
    // Contest and other bits are all left out.
    static bool Wild(const Battle& b) { return (b.type & ~2u) == 0; }
    // Battles whose deaths count: not link, tutorial, Pal Park, Safari, Frontier, Contest.
    static bool Counts(const Battle& b) { return !(b.type & (4u | 1u << 5 | 1u << 7 | 1u << 9 | 1u << 10 | 1u << 12 | 1u << 31)); }

    // ---- the save
    uint32_t Block(const HudParty::Ram& r, uint32_t id, uint32_t size = 0) const
    {
        const uint32_t fs = r.R32(kFieldSystem), save = r.R32(fs + 0x0C);
        const uint32_t h = save + 0x23014 + 16 * id;
        if (!r.Ptr(fs) || !r.Ptr(save) || r.R32(h) != id || (size && r.R32(h + 4) != size)) return 0;
        return save + 0x10 + r.R32(h + 8);
    }
    uint32_t PartyAt(const HudParty::Ram& r) const
    {
        const uint32_t p = Block(r, 2);
        return r.Ptr(p) && r.R32(p) == 6 && r.R32(p + 4) >= 1 && r.R32(p + 4) <= 6 ? p : 0;
    }
    uint32_t StorageAt(const HudParty::Ram& r) const
    {
        const uint32_t s = Block(r, 41);
        return r.Ok(s, 0x122EC) && !(s & 3) ? s : 0;
    }
    std::set<uint32_t> Pids(const HudParty::Ram& r) const
    {
        std::set<uint32_t> out;
        if (const uint32_t p = PartyAt(r))
            for (uint32_t i = 0; i < r.R32(p + 4); i++) out.insert(r.R32(p + 8 + 236 * i));
        if (const uint32_t s = StorageAt(r))
            for (uint32_t b = 0; b < 18; b++)
                for (uint32_t i = 0; i < 30; i++)
                {
                    const uint32_t a = s + 0x1000 * b + 136 * i;
                    if (r.R32(a) || r.R16(a + 6)) out.insert(r.R32(a));
                }
        return out;
    }

    // ---- the fight
    void Say(const std::string& text, const char* icon = "icons/icon_star") { toasts.push_back({ text, icon }); }
    std::string Place(int map) const { return map >= 0 ? Party_PlaceName(map) : "this area"; }

    void StartBattle(const Env& e, const Battle& b)
    {
        if (pendingEnd) EndBattle(e);
        inBattle = true;
        pendingEnd = false;
        illegal = false;
        cur = -1;
        foeType = b.type;
        lastBattle = e.frame;
        before = Pids(e.ram);
        if (!Wild(b) || lastMap < 0 || !sec) return;
        const int s = sec(lastMap), sp = b.foe.species;
        const std::string name = Party_SpeciesName(sp), place = Place(lastMap);
        Entry en;
        en.map = lastMap, en.sec = s, en.species = sp, en.level = b.foe.level, en.shiny = b.foe.shiny;
        int open = -1;
        bool used = false;
        for (size_t i = 0; i < ledger.size(); i++)
            if (ledger[i].sec == s)
            {
                if (ledger[i].status == Duplicate || ledger[i].status == WrongType) open = (int)i;
                else used |= !ledger[i].shiny; // (a shiny's row does not use the area up)
            }
        // The rows of an area still open are overwritten by the next encounter there.
        auto put = [&](int status) {
            en.status = status;
            if (open >= 0) ledger[open] = en, cur = open;
            else ledger.push_back(en), cur = (int)ledger.size() - 1;
        };
        if (!TypeOk(sp))
        {
            put(WrongType);
            Say(name + " is the wrong type: it does not count, and a catch is not allowed", "icons/icon_fight");
        }
        else if (b.foe.shiny)
        {
            // The shiny clause: always catchable, and it does not use up the area.
            if (used) open = -1;
            put(Seen);
            Say("A shiny " + name + "! It can always be caught", "icons/icon_star");
        }
        else if (used)
        {
            illegal = true;
            Say(place + " has had its Pok\xC3\xA9mon: a catch here does not count", "icons/icon_fight");
        }
        else if (SpeciesCaught(sp))
        {
            put(Duplicate);
            Say(name + " is a duplicate: it does not count. " + place + " stays open", "icons/icon_star");
        }
        else
        {
            put(Seen);
            Say("First encounter in " + place + ": " + name + ". Catch it or lose the chance", "icons/icon_star");
        }
        Save();
    }
    bool SpeciesCaught(int species) const
    {
        for (const Entry& en : ledger)
            if (en.species == species && en.status == Caught) return true;
        return false;
    }

    void Die(const BMon& m, const Battle& b)
    {
        for (const Grave& g : grave)
            if (g.pid == m.pid) return;
        Grave g;
        g.pid = m.pid, g.species = m.species, g.level = m.level, g.map = lastMap, g.shiny = m.shiny;
        g.name = m.name.empty() ? Party_SpeciesName(m.species) : m.name;
        g.why = "Fainted against " + Party_SpeciesName(b.foe.species);
        grave.push_back(g);
        for (Entry& en : ledger)
            if (en.pid == m.pid && m.pid) en.dead = true;
        Say(g.name + " has fallen", "icons/icon_exit");
        Save();
    }

    void EndBattle(const Env& e)
    {
        pendingEnd = false;
        const std::set<uint32_t> now = Pids(e.ram);
        uint32_t got = 0;
        for (uint32_t p : now)
            if (!before.count(p)) got = p;
        if (!got)
        {
            if (cur >= 0 && ledger[cur].status == Seen)
            {
                ledger[cur].status = Failed;
                if (!ledger[cur].shiny) Say(Party_SpeciesName(ledger[cur].species) + " got away: " + Place(ledger[cur].map) + " is used up", "icons/icon_exit");
            }
            cur = -1;
            illegal = false;
            Save();
            return;
        }
        if (cur >= 0)
        {
            Entry& en = ledger[cur];
            if (en.status == WrongType) illegal = true, en.status = Released;
            else
            {
                en.status = Caught;
                en.pid = got;
                Say(Party_SpeciesName(en.species) + " caught: " + Place(en.map) + " is done", "icons/icon_star");
            }
        }
        if (illegal)
        {
            // Not allowed: into the Graveyard box, so that it does not count (and is not lost).
            Grave g;
            g.pid = got, g.map = lastMap, g.fallen = false, g.why = "Caught against the rules";
            Loc l;
            if (Find(e, got, l))
            {
                PartyMonInfo(e, l, g);
                const bool ok = Bury(e, l);
                g.buried = true;
                grave.push_back(g);
                Say(g.name + " was not a legal catch: " + (ok ? "sent to the Graveyard box" : "released"), "icons/icon_exit");
            }
        }
        cur = -1;
        illegal = false;
        Save();
    }

    // ---- moving Pokemon
    struct Loc
    {
        int party = -1, box = -1, slot = -1;
        uint32_t addr = 0;
    };
    bool Find(const Env& e, uint32_t pid, Loc& l) const
    {
        if (const uint32_t p = PartyAt(e.ram))
            for (uint32_t i = 0; i < e.ram.R32(p + 4); i++)
                if (e.ram.R32(p + 8 + 236 * i) == pid)
                {
                    l.party = (int)i;
                    l.addr = p + 8 + 236 * i;
                    return true;
                }
        if (const uint32_t s = StorageAt(e.ram))
            for (uint32_t b = 0; b < 18; b++)
                for (uint32_t i = 0; i < 30; i++)
                {
                    const uint32_t a = s + 0x1000 * b + 136 * i;
                    if (e.ram.R32(a) == pid && (pid || e.ram.R16(a + 6)))
                    {
                        l.box = (int)b, l.slot = (int)i, l.addr = a;
                        return true;
                    }
                }
        return false;
    }
    void PartyMonInfo(const Env& e, const Loc& l, Grave& g) const
    {
        PartyMon m;
        if (Party_ReadMon(l.addr, m, l.party < 0))
        {
            g.species = m.species, g.level = m.level, g.name = m.name;
            g.shiny = Shiny(g.pid, m.otId);
        }
    }
    static void W(const Env& e, uint32_t addr, const uint8_t* src, size_t n)
    {
        for (uint8_t* mem : e.mem) memcpy(mem + (addr & e.mask), src, n);
    }
    // The 136 bytes a box keeps of the Pokemon at `l`, encrypted as a box has them (the game
    // decrypts a Pokemon in place while it reads it and says so in the flags at +4).
    static std::vector<uint8_t> BoxBytes(const Env& e, uint32_t addr)
    {
        std::vector<uint8_t> m(136);
        for (int i = 0; i < 136; i++) m[i] = (uint8_t)e.ram.R8(addr + i);
        uint32_t flags = RomGet16(&m[4]);
        const uint32_t check = RomGet16(&m[6]);
        if (flags & 2)
        {
            uint32_t seed = check;
            for (int w = 0; w < 64; w++)
            {
                seed = seed * 0x41C64E6D + 0x6073;
                const uint32_t v = RomGet16(&m[8 + 2 * w]) ^ (seed >> 16);
                m[8 + 2 * w] = (uint8_t)v, m[9 + 2 * w] = (uint8_t)(v >> 8);
            }
            flags &= ~2u;
            m[4] = (uint8_t)flags, m[5] = (uint8_t)(flags >> 8);
        }
        return m;
    }
    static const uint16_t* GraveName()
    {
        static uint16_t n[10];
        const char* s = "Graveyard";
        for (int i = 0; i < 9; i++) n[i] = (uint16_t)(i == 0 ? 0x012B + (s[i] - 'A') : 0x0145 + (s[i] - 'a'));
        n[9] = 0xFFFF;
        return n;
    }
    // Takes the Pokemon at `l` out of the party or its box and puts it in the Graveyard box
    // (the last), naming the box on the first use. False when the box is full or missing: the
    // Pokemon is then only released.
    bool Bury(const Env& e, const Loc& l) const
    {
        const uint32_t s = StorageAt(e.ram);
        uint32_t to = 0;
        if (s)
            for (uint32_t i = 0; i < 30 && !to; i++)
            {
                const uint32_t a = s + 0x1000 * kGraveBox + 136 * i;
                if (!e.ram.R32(a) && !e.ram.R16(a + 6)) to = a;
            }
        if (to)
        {
            const std::vector<uint8_t> m = BoxBytes(e, l.addr);
            W(e, to, m.data(), 136);
            uint8_t name[40] = {};
            const uint16_t* gn = GraveName();
            for (int i = 0; i < 20; i++)
            {
                const uint16_t c = i < 10 ? gn[i] : 0xFFFF;
                name[2 * i] = (uint8_t)c, name[2 * i + 1] = (uint8_t)(c >> 8);
            }
            W(e, s + 0x12008 + 40 * kGraveBox, name, 40);
        }
        if (l.party >= 0)
        {
            const uint32_t p = PartyAt(e.ram), n = e.ram.R32(p + 4);
            std::vector<uint8_t> all(236 * 6, 0);
            for (uint32_t i = 0; i < 236 * 6; i++) all[i] = (uint8_t)e.ram.R8(p + 8 + i);
            memmove(&all[236 * l.party], &all[236 * (l.party + 1)], 236 * (n - 1 - l.party));
            memset(&all[236 * (n - 1)], 0, 236);
            W(e, p + 8, all.data(), 236 * 6);
            const uint8_t cnt[4] = { (uint8_t)(n - 1), 0, 0, 0 };
            W(e, p + 4, cnt, 4);
        }
        else
        {
            const uint8_t zero[136] = {};
            W(e, l.addr, zero, 136);
        }
        if (to)
        {
            PartyMon chk;
            fprintf(stderr, "[nuzlocke] to the Graveyard box: %s
", Party_ReadMon(to, chk, true) ? (Party_SpeciesName(chk.species) + " Lv " + std::to_string(chk.level)).c_str() : "unreadable");
        }
        else
            fprintf(stderr, "[nuzlocke] no room in the Graveyard box (or no PC found): released
");
        return to != 0;
    }
    bool IsDead(uint32_t pid) const
    {
        for (const Grave& g : grave)
            if (g.pid == pid && g.fallen) return true;
        return false;
    }

    // The overworld is quiet: bury the dead, and find those that fainted outside a fight.
    void Tidy(const Env& e)
    {
        const uint32_t p = PartyAt(e.ram);
        if (!p) return;
        const uint32_t n = e.ram.R32(p + 4);
        int alive = 0;
        for (uint32_t i = 0; i < n; i++)
        {
            const uint32_t a = p + 8 + 236 * i, pid = e.ram.R32(a);
            PartyMon m;
            if (!Party_ReadMon(a, m)) { alive++; continue; }
            if (m.hp == 0 && !m.egg && !IsDead(pid))
            {
                Grave g;
                g.pid = pid, g.species = m.species, g.level = m.level, g.map = lastMap, g.shiny = Shiny(pid, m.otId);
                g.name = m.name, g.why = "Fainted";
                grave.push_back(g);
                Say(g.name + " has fallen", "icons/icon_exit");
                Save();
            }
            alive += !IsDead(pid);
        }
        this->alive = alive;
        if (alive == 0)
        {
            if (!runOver) runOver = true, Say("Run over: every Pok\xC3\xA9mon has fallen", "icons/icon_exit"), Save();
            return; // the last ones stay in the party (a party cannot be empty)
        }
        for (uint32_t i = 0; i < n; i++)
        {
            const uint32_t pid = e.ram.R32(p + 8 + 236 * i);
            if (!IsDead(pid)) continue;
            Loc l;
            l.party = (int)i, l.addr = p + 8 + 236 * i;
            for (Grave& g : grave)
                if (g.pid == pid && !g.buried)
                {
                    Bury(e, l);
                    g.buried = true;
                    Say(g.name + " was laid to rest in the Graveyard box", "icons/icon_exit");
                    Save();
                    return; // one at a time: the party has moved
                }
        }
    }
};

// ---- the ledger screen
struct View
{
    int page = 0;     // 0 ledger, 1 graveyard
    int scroll = 0;   // first row shown
    bool confirmNew = false;
};

inline void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Tracker& t, const View& v, const HudOnline::IconFn& icon,
    const std::vector<HudHint>& hints)
{
    using namespace HudColour;
    DrawBackdrop(c, sp, "backgrounds/bg_dusk", 0xFFFFFFFF);
    const float wd = ui.W / ui.s;
    HudText(c, ui, f, 100, 50, 64, "NUZLOCKE", White, 0, true);
    const std::string rule = Nuz::RuleName(t.rule);
    HudText(c, ui, f, wd - 100, 70, 40, rule, White, 2, true);

    // Left: the run.
    const float lx = 100, lw = 520, ly = 150;
    HudOnline::TitledCard(c, ui, sp, f, lx, ly, lw, 66 + 16 + 5 * 60 + 16 + 150, "THE RUN");
    float ry = ly + 66 + 16;
    const std::pair<std::string, std::string> rows[] = {
        { "Lives", t.alive < 0 ? "?" : std::to_string(t.alive) }, { "Fallen", std::to_string(t.Fallen()) },
        { "Caught", std::to_string(t.CaughtCount()) }, { "Areas", std::to_string(t.ledger.size()) },
        { "State", t.runOver ? "Run over" : "Going" } };
    for (const auto& r : rows)
    {
        HudScreens::StatRow(c, ui, sp, f, lx + 23, ry, lw - 46, r.first, r.second, r.first == "Lives");
        ry += 60;
    }
    const std::vector<std::string> about = HudWrap(ui, f, 32, Nuz::RuleAbout(t.rule), lw - 60);
    for (size_t i = 0; i < about.size() && i < 4; i++) HudText(c, ui, f, lx + 30, ry + 16 + 36.0f * i, 28, about[i], Muted);
    if ((t.rule == Monolocke || t.rule == Duallocke) && t.type[0] >= 0)
    {
        std::string ty = HudParty::TypeName(t.type[0]);
        if (t.rule == Duallocke && t.type[1] >= 0) ty += " and " + std::string(HudParty::TypeName(t.type[1]));
        HudText(c, ui, f, lx + 30, ly + 66 + 16 + 5 * 60 + 130, 32, ty, Ink, 0, true);
    }

    // Right: the ledger or the graveyard.
    const float rx = 680, rw = wd - rx - 100, ty0 = 150;
    HudPanel(c, ui, sp, v.page == 0 ? "panels/button_yellow" : "panels/button_white", rx, ty0, 240, 56);
    HudText(c, ui, f, rx + 120, ty0 + 10, 32, "LEDGER", Ink, 1);
    HudPanel(c, ui, sp, v.page == 1 ? "panels/button_yellow" : "panels/button_white", rx + 260, ty0, 280, 56);
    HudText(c, ui, f, rx + 400, ty0 + 10, 32, "GRAVEYARD", Ink, 1);
    const float cy = ty0 + 76, rowH = 70;
    const int visible = 11;
    HudPanel(c, ui, sp, "panels/card", rx, cy, rw, visible * rowH + 30);
    const int total = v.page == 0 ? (int)t.ledger.size() : (int)t.grave.size();
    if (!total)
        HudText(c, ui, f, rx + rw / 2, cy + 200, 32, v.page == 0 ? "No wild Pok\xC3\xA9mon met yet." : "Nobody has fallen. Yet.", Muted, 1);
    for (int i = 0; i < visible && v.scroll + i < total; i++)
    {
        const int k = v.scroll + i;
        const float y = cy + 15 + i * rowH;
        HudPanel(c, ui, sp, "panels/row", rx + 20, y, rw - 40, rowH - 8);
        if (v.page == 0)
        {
            const Entry& en = t.ledger[k];
            if (icon) HudPixelArt(c, ui, icon(en.species), rx + 26, y - 8, 64, 64);
            HudText(c, ui, f, rx + 100, y + 12, 32, Party_PlaceName(en.map), Ink);
            HudText(c, ui, f, rx + rw * 0.46f, y + 12, 32, Party_SpeciesName(en.species) + (en.shiny ? " *" : ""), Muted);
            const std::string st = en.dead ? "Dead" : StatusName(en.status);
            const float pw = HudTextWidth(ui, f, 28, st) + 40;
            HudPanel(c, ui, sp, "panels/button_white", rx + rw - 40 - pw, y + 6, pw, 44, en.dead ? 0xFFC8A0A0 : StatusColour(en.status));
            HudText(c, ui, f, rx + rw - 40 - pw / 2, y + 12, 28, st, en.dead ? Danger : Ink, 1);
        }
        else
        {
            const Grave& g = t.grave[k];
            if (icon) HudPixelArt(c, ui, icon(g.species), rx + 26, y - 8, 64, 64);
            HudText(c, ui, f, rx + 100, y + 12, 32, g.name + "  Lv " + std::to_string(g.level), Ink);
            HudText(c, ui, f, rx + rw * 0.46f, y + 12, 28, g.why + (g.map >= 0 ? ", " + Party_PlaceName(g.map) : ""), Muted);
        }
    }
    if (total > visible) HudText(c, ui, f, rx + rw - 30, cy + visible * rowH + 4, 28, std::to_string(v.scroll + 1) + "-" + std::to_string(std::min(total, v.scroll + visible)) + " / " + std::to_string(total), Muted, 2);
    if (v.confirmNew) HudText(c, ui, f, rx, cy + visible * rowH + 50, 32, "Start a new run? Press the same button again. Everything above is cleared.", Danger);
    DrawHintBar(c, ui, sp, f, hints);
}
} // namespace Nuz
