#pragma once
// The "World" options: changes to the game's DATA, made to the ROM image before the consoles
// are created (like plazamap.h), so --lockstep's two consoles run on the same data. Same
// sizes only (the game keeps the ROM's file table in memory from boot) and no code: only
// tables the game reads (encounters, trainer parties, personal data, learnsets, scripts).
//   - Randomizer: wild Pokemon, trainer parties, gifts and eggs, optionally the moves each
//     species learns and its abilities. One seed decides everything (the same seed on the
//     same game gives the same world), so it can be shared. A species is replaced by one
//     of a similar total of base stats, and never by a legendary unless it was one.
//   - More Pokemon: the base forms of late-game and post-League species in a few rare
//     places of earlier routes' encounter tables, at the level of the route.
//   - Skip the tutorials (see SkipTutorials): scripts only.
//   - Move Tutor (see MoveTutor): the receptionist at the left counter of every Pokemon Center
//     teaches moves a Pokemon forgot and gives nicknames.
// Nothing here is read from or written to anything but the player's own ROM at run time.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "ndsrom.h"
#include "plazamap.h" // EncodeText

namespace WorldPatch
{
struct Options
{
    bool randomize = false;
    uint32_t seed = 0;
    bool wild = true, trainers = true, gifts = true; // what the randomizer changes
    bool moves = false, abilities = false;
    bool more = false;
    int starters = 0;          // 0 Johto (the game's), 1 Kanto, 2 Hoenn, 3 Sinnoh, 4 chosen below, 5 by the seed
    int chosen[3] = { 1, 4, 7 }; // species of starters == 4
    // (A "partner" starter with perfect IVs is not possible with data alone: the game rolls the
    // IVs in its code when it makes the Pokemon in Elm's lab, and a script has no command for them.)
    int resolved[3] = { 152, 155, 158 }; // what Patch decided the three balls hold
    bool skipTutorials = false;
    bool moveTutor = false;    // a Move Tutor and Name Rater in every Pokemon Center
    int dayLength = 0;         // 0 real clock, 1 a day in an hour, 2 a day in 20 minutes

    bool StartersChanged() const { return starters || (randomize && gifts); }
    bool AnyData() const { return randomize || more || skipTutorials || starters || moveTutor; }

    // "key=value,key=value" (the config's world_* entries and SSR_WORLD use the same keys).
    void Set(const std::string& k, const std::string& v)
    {
        const int n = atoi(v.c_str());
        const bool b = v == "1" || v == "true" || v == "on";
        if (k == "rand") randomize = b;
        else if (k == "seed") seed = (uint32_t)strtoul(v.c_str(), nullptr, 10) % 1000000u;
        else if (k == "wild") wild = b;
        else if (k == "trainers") trainers = b;
        else if (k == "gifts") gifts = b;
        else if (k == "moves") moves = b;
        else if (k == "abilities") abilities = b;
        else if (k == "more") more = b;
        else if (k == "starters") starters = std::clamp(n, 0, 5);
        else if (k == "s1") chosen[0] = std::clamp(n, 1, 493);
        else if (k == "s2") chosen[1] = std::clamp(n, 1, 493);
        else if (k == "s3") chosen[2] = std::clamp(n, 1, 493);
        else if (k == "skip") skipTutorials = b;
        else if (k == "tutor") moveTutor = b;
        else if (k == "day") dayLength = std::clamp(n, 0, 2);
    }
    void Parse(const std::string& s)
    {
        size_t p = 0;
        while (p < s.size())
        {
            size_t e = s.find(',', p);
            if (e == std::string::npos) e = s.size();
            const std::string kv = s.substr(p, e - p);
            const size_t eq = kv.find('=');
            if (eq != std::string::npos) Set(kv.substr(0, eq), kv.substr(eq + 1));
            p = e + 1;
        }
    }
};

// The first three species of each region's starter set, by name for the menu.
constexpr int RegionStarters[4][3] = { { 152, 155, 158 }, { 1, 4, 7 }, { 252, 255, 258 }, { 387, 390, 393 } };

inline uint32_t Mix(uint32_t x)
{
    x += 0x9E3779B9u;
    x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
    x = (x ^ (x >> 13)) * 0xC2B2AE35u;
    return x ^ (x >> 16);
}

// What the game says about the species, read from the ROM.
struct Dex
{
    static constexpr int N = 494;
    int bst[N] = {};
    bool evolved[N] = {}; // something evolves into it
    bool valid = false;
    std::vector<std::pair<uint32_t, uint32_t>> personal, learn, moveData;

    static bool Legendary(int s)
    {
        return s == 144 || s == 145 || s == 146 || s == 150 || s == 151 || (s >= 243 && s <= 245) || (s >= 249 && s <= 251) ||
            (s >= 377 && s <= 386) || (s >= 480 && s <= 493);
    }
    // Species a randomizer never makes: Unown has forms the data would have to name.
    static bool Banned(int s) { return s == 201; }

    bool Load(const uint8_t* r, size_t n)
    {
        personal = NdsNarcFiles(r, n, "a/0/0/2");
        learn = NdsNarcFiles(r, n, "a/0/3/3");
        moveData = NdsNarcFiles(r, n, "a/0/1/1");
        const auto evo = NdsNarcFiles(r, n, "a/0/3/4");
        if (personal.size() < N || learn.size() < N || evo.size() < N || moveData.size() < 468) return false;
        for (int s = 1; s < N; s++)
        {
            if (personal[s].second < 44 || evo[s].second < 42) return false;
            for (int i = 0; i < 6; i++) bst[s] += r[personal[s].first + i];
            for (int e = 0; e < 7; e++)
            {
                const uint8_t* p = r + evo[s].first + e * 6;
                const int t = (int)RomGet16(p + 4);
                if (RomGet16(p) && t > 0 && t < N) evolved[t] = true;
            }
        }
        valid = true;
        return true;
    }

    // The replacement of `orig` for this seed: of a similar total of base stats, a
    // legendary only for a legendary, a species that nothing evolves into when `basic`.
    int Pick(uint32_t seed, uint32_t salt, int orig, bool basic) const
    {
        if (orig < 1 || orig >= N) return orig;
        std::vector<int> cand;
        for (int pct = 12; pct <= 300 && cand.size() < 6; pct += 12)
        {
            cand.clear();
            const int window = std::max(30, bst[orig] * pct / 100);
            for (int s = 1; s < N; s++)
                if (s != orig && !Banned(s) && Legendary(s) == Legendary(orig) && (!basic || !evolved[s]) && std::abs(bst[s] - bst[orig]) <= window)
                    cand.push_back(s);
        }
        if (cand.empty()) return orig;
        return cand[Mix(seed ^ Mix(salt * 7919u + (uint32_t)orig)) % cand.size()];
    }
};

inline void Put16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

// The four moves a species of `species` would have at `level`, from its learnset.
inline void LearnsetMoves(const uint8_t* r, const Dex& dex, int species, int level, uint16_t out[4])
{
    int n = 0;
    uint16_t all[64];
    const uint8_t* p = r + dex.learn[species].first;
    const uint32_t size = dex.learn[species].second;
    for (uint32_t i = 0; i + 2 <= size && n < 64; i += 2)
    {
        const uint32_t v = RomGet16(p + i);
        if (v == 0xFFFF || (int)(v >> 9) > level) break;
        const uint16_t m = (uint16_t)(v & 0x1FF);
        bool dup = false;
        for (int k = 0; k < n; k++) dup |= all[k] == m;
        if (!dup) all[n++] = m;
    }
    for (int k = 0; k < 4; k++) out[k] = n - 4 + k >= 0 ? all[n - 4 + k] : 0;
}

namespace Detail
{
inline void ForEachEncounterSpecies(uint8_t* d, uint32_t size, const std::function<void(uint8_t*)>& fn)
{
    if (size < 196) return;
    for (int i = 0; i < 36; i++) fn(d + 20 + 2 * i);  // grass: morning, day, night
    for (int i = 0; i < 4; i++) fn(d + 92 + 2 * i);   // the Hoenn and Sinnoh sound lists
    for (int i = 0; i < 5 + 2 + 5 + 5 + 5; i++) fn(d + 100 + 4 * i + 2); // surf, rock smash, rods
    for (int i = 0; i < 4; i++) fn(d + 188 + 2 * i);  // swarms
}

// Basic forms of late-game and post-League species (Kanto, Hoenn and Sinnoh ones, and the
// Johto ones met only far on): this table is IDs only, written for this feature.
constexpr int LateSpecies[] = { 246, 147, 371, 374, 443, 447, 328, 333, 304, 280, 287, 403, 408, 410, 361, 363, 355, 359, 115, 127,
    137, 123, 214, 207, 220, 227, 228, 231, 336, 337, 338, 345, 347, 352, 353, 417, 420, 425, 427, 431, 434, 436, 439, 441, 449, 451, 453, 459 };
// The same for the water: surfing and fishing.
constexpr int LateWater[] = { 131, 349, 320, 339, 341, 270, 418, 456, 458, 422, 363, 366, 369, 370, 120, 223 };
}

// The Move Tutor: the receptionist at the left counter of every Pokemon Center (the same one
// everywhere: script 9017 of the Union Room scripts, who only explains the Trainer Card) runs
// a script of ours: pick a Pokemon, then have it remember a move it forgot (the game's own
// Move Relearner commands 466, 467 and 469, as in Blackthorn City) or give it a new nickname
// (the Name Rater's command 173). Data only:
//   - script bank 10300 (the game's script file 734, whose table is empty and which no map
//     uses) gets the script. That file is 4 bytes, so its entry in the archive's table of
//     members is pointed at the room of four dead files, 71 to 74 (the script of a Battle
//     Tower room that no map has). The archive itself does not move or change size.
//   - text bank 444 (28 blank lines, used by that script bank and nothing else) holds the lines.
//   - the receptionists' event objects (script 9017) get script 10300 instead.
namespace MoveTutor
{
constexpr int ScriptBank = 734, TextBank = 444, ReceptionScript = 9017;
constexpr uint16_t Result = 0x800C, Slot = 0x8005;
enum { Hello = 16, Bye = 17, Egg = 3, Teach = 14, None = 10, Rename = 24 }; // lines of text bank 444

struct Line { int index; const char* text; };
// {N}: the Pokemon's nickname, {?}: a Yes/No menu. Each line goes where a blank one at least as
// long was.
constexpr Line Lines[] = {
    { Hello, "Hi! I'm the Move Tutor.\rI teach moves a Pok\xC3\xA9mon forgot\nand give new nicknames.\rWant some help? {?}" },
    { Bye, "Come back any time!" },
    { Egg, "That's an Egg!\nCome back once it has hatched." },
    { Teach, "Should I teach {N}\na move it forgot? {?}" },
    { None, "{N} has no moves\nit can remember." },
    { Rename, "Give {N} a\nnew nickname? {?}" },
};

inline std::vector<uint16_t> Encode(const std::string& s)
{
    std::vector<uint16_t> out;
    for (size_t i = 0; i < s.size();)
    {
        if (!s.compare(i, 3, "{N}")) { for (uint16_t c : { 0xFFFE, 0x0101, 2, 0, 0 }) out.push_back(c); i += 3; }
        else if (!s.compare(i, 3, "{?}")) { for (uint16_t c : { 0xFFFE, 0x0200, 1, 0 }) out.push_back(c); i += 3; }
        else if (!s.compare(i, 2, "\xC3\xA9")) { out.push_back(0x188); i += 2; }
        else
        {
            const auto c = PlazaMap::EncodeText(std::string(1, s[i]).c_str());
            if (c.empty()) return {};
            out.push_back(c[0]);
            i++;
        }
    }
    out.push_back(0xFFFF);
    return out;
}

// The script, from the game's command numbers (macros/script.inc has them).
inline std::vector<uint8_t> Script()
{
    std::vector<uint8_t> b;
    std::vector<std::pair<std::string, size_t>> labels, jumps;
    auto w16 = [&](uint32_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); };
    auto cmd = [&](uint16_t op, std::initializer_list<uint16_t> args = {}) { w16(op); for (uint16_t a : args) w16(a); };
    auto msg = [&](int line) { w16(45); b.push_back((uint8_t)line); };           // NPCMsg
    auto nick = [&]() { w16(199); b.push_back(0); w16(Slot); };                  // BufferPartyMonNick 0, slot
    auto label = [&](const char* l) { labels.push_back({ l, b.size() }); };
    auto jump = [&](int cond, const char* l) {                                   // GoTo, or GoToIf cond
        if (cond < 0) w16(22); else { w16(28); b.push_back((uint8_t)cond); }
        jumps.push_back({ l, b.size() });
        for (int i = 0; i < 4; i++) b.push_back(0);
    };
    enum { Eq = 1, Ne = 5 };
    auto choice = [&]() { cmd(746); cmd(748, { Result }); cmd(747); };           // the Yes/No answer into Result
    auto fade = [&](int in) { cmd(174, { 6, 1, (uint16_t)in, 0 }); cmd(175); };
    auto cmp = [&](uint16_t var, uint16_t v) { cmd(17, { var, v }); };
    auto finish = [&]() { cmd(50); cmd(53); cmd(97); cmd(2); };                  // WaitButton, CloseMsg, ReleaseAll, End

    cmd(73, { 1500 }); cmd(96); cmd(104);                                        // PlaySE, LockAll, FacePlayer
    msg(Hello); choice(); cmp(Result, 0); jump(Ne, "bye");
    cmd(53); fade(0); cmd(349); cmd(351, { Slot }); cmd(150); fade(1);           // pick a Pokemon
    cmp(Slot, 255); jump(Eq, "bye");
    cmd(354, { Slot, Result }); cmp(Result, 0); jump(Eq, "egg");                 // species 0: an Egg
    nick(); msg(Teach); choice(); cmp(Result, 0); jump(Ne, "rename");
    cmd(466, { Result, Slot }); cmp(Result, 0); jump(Eq, "none");                // is there a move to remember?
    cmd(53); fade(0); cmd(467, { Slot }); cmd(469, { Result }); cmd(150); fade(1); jump(-1, "bye");
    label("none"); nick(); msg(None); finish();
    label("rename"); nick(); msg(Rename); choice(); cmp(Result, 0); jump(Ne, "bye");
    cmd(53); cmd(41, { Result, 0 }); fade(0); cmd(173, { Slot, Result }); fade(1); jump(-1, "bye");
    label("egg"); msg(Egg); finish();
    label("bye"); msg(Bye); finish();
    for (const auto& j : jumps)
        for (const auto& l : labels)
            if (l.first == j.first)
            {
                const uint32_t rel = (uint32_t)((int64_t)l.second - (int64_t)(j.second + 4));
                for (int i = 0; i < 4; i++) b[j.second + i] = (uint8_t)(rel >> (8 * i));
            }
    return b;
}

// False (and nothing changed) when the files are not the ones of the US HeartGold/SoulSilver.
inline bool Patch(uint8_t* r, size_t n)
{
    auto fail = [](const char* why) { fprintf(stderr, "[world] Move Tutor not set up: %s\n", why); return false; };
    const auto scripts = NdsNarcFiles(r, n, "a/0/1/2");
    const auto texts = NdsNarcFiles(r, n, "a/0/2/7");
    uint32_t narc, narcSize;
    if (scripts.size() <= (size_t)ScriptBank || texts.size() <= (size_t)TextBank || !NdsRomFile(r, n, "a/0/1/2", narc, narcSize))
        return fail("not the expected files");

    // The dead files 71 to 74, one after the other, and the empty script file (just the end mark).
    const uint32_t room = scripts[71].first;
    uint32_t roomEnd = room;
    for (int i = 0; i < 4; i++)
    {
        if (scripts[71 + i].first != roomEnd || scripts[71 + i].second != (i ? 8u : 520u)) return fail("script files 71 to 74");
        roomEnd += scripts[71 + i].second;
    }
    if (scripts[ScriptBank].second != 4 || RomGet16(r + scripts[ScriptBank].first) != 0xFD13) return fail("script file 734");
    const std::vector<uint8_t> code = Script();
    if (6 + code.size() > roomEnd - room) return fail("script too long");

    // The people to give the script to: events (a/0/3/2) with an object whose script is 9017, who
    // only explains the Trainer Card (the same sprite at the left counter of every Pokemon Center).
    const auto events = NdsNarcFiles(r, n, "a/0/3/2");
    std::vector<uint8_t*> receptionists;
    for (const auto& e : events)
    {
        if (e.second < 8) continue;
        uint8_t* d = r + e.first;
        const uint32_t bgs = RomGet32(d);
        if (bgs > 64 || 4 + 20 * bgs + 4 > e.second) continue;
        const uint32_t objects = RomGet32(d + 4 + 20 * bgs);
        if (objects > 64 || 8 + 20 * bgs + 32 * objects > e.second) continue;
        for (uint32_t i = 0; i < objects; i++)
        {
            uint8_t* o = d + 8 + 20 * bgs + 32 * i; // id, sprite, movement, type, flag, script, facing...
            if (RomGet16(o + 10) == ReceptionScript) receptionists.push_back(o + 10);
        }
    }
    if (receptionists.size() < 20) return fail("receptionists");

    // Text bank 444: blank lines (spaces and the end mark), long enough, each line with its own key.
    uint8_t* bank = r + texts[TextBank].first;
    const uint32_t bankSize = texts[TextBank].second, count = RomGet16(bank), key = RomGet16(bank + 2);
    if (count != 28 || 4 + count * 8 > bankSize) return fail("text bank 444");
    struct Text { uint32_t at, allocKey, off; std::vector<uint16_t> codes; };
    std::vector<Text> lines;
    for (const Line& l : Lines)
    {
        const uint32_t i = (uint32_t)l.index + 1;
        uint32_t allocKey = (765 * i * key) & 0xFFFF;
        allocKey |= allocKey << 16;
        const uint32_t off = RomGet32(bank + 4 + l.index * 8) ^ allocKey, len = RomGet32(bank + 8 + l.index * 8) ^ allocKey;
        std::vector<uint16_t> codes = Encode(l.text);
        if (codes.empty() || (uint64_t)off + 2 * len > bankSize || codes.size() > len) return fail("text lines");
        uint16_t k = (uint16_t)(i * 596947);
        for (uint32_t c = 0; c < len; c++, k = (uint16_t)(k + 18749))
            if ((RomGet16(bank + off + 2 * c) ^ k) != (c + 1 < len ? 0x1DE : 0xFFFF)) return fail("text lines not blank");
        lines.push_back({ 8 + (uint32_t)l.index * 8, allocKey, off, codes });
    }

    // Everything checked: write.
    for (const Text& t : lines)
    {
        const uint32_t i = (t.at - 8) / 8 + 1;
        uint16_t k = (uint16_t)(i * 596947);
        const uint32_t len = (uint32_t)t.codes.size() ^ t.allocKey;
        for (int b = 0; b < 4; b++) bank[t.at + b] = (uint8_t)(len >> (8 * b)); // the line's length, after its offset
        for (size_t c = 0; c < t.codes.size(); c++, k = (uint16_t)(k + 18749))
        {
            const uint32_t v = t.codes[c] ^ k;
            bank[t.off + 2 * c] = (uint8_t)v;
            bank[t.off + 2 * c + 1] = (uint8_t)(v >> 8);
        }
    }
    uint8_t* file = r + room;
    memset(file, 0, roomEnd - room);
    file[0] = 2;       // the table: one script, at byte 6 (4 + 2)
    file[4] = 0x13, file[5] = 0xFD;
    memcpy(file + 6, code.data(), code.size());
    // The archive's table of members (after its 16 bytes of header and 12 of the table's own).
    const uint32_t fatSize = RomGet32(r + narc + 16 + 4), fnt = 16 + fatSize;
    const uint32_t base = narc + fnt + RomGet32(r + narc + fnt + 4) + 8; // where the members' data starts
    uint8_t* member = r + narc + 16 + 12 + 8 * ScriptBank;
    const uint32_t from = room - base, to = roomEnd - base;
    for (int b = 0; b < 4; b++) { member[b] = (uint8_t)(from >> (8 * b)); member[4 + b] = (uint8_t)(to >> (8 * b)); }
    for (uint8_t* script : receptionists) { script[0] = 10300 & 255; script[1] = 10300 >> 8; }
    fprintf(stderr, "[world] Move Tutor: %zu bytes of script in bank 10300, %zu receptionists\n", code.size(), receptionists.size());
    return true;
}
}

inline bool Patch(std::vector<uint8_t>& rom, Options& o)
{
    if (!o.AnyData()) return false;
    uint8_t* r = rom.data();
    const size_t n = rom.size();
    Dex dex;
    if (!dex.Load(r, n))
    {
        fprintf(stderr, "[world] not set up: not the expected files\n");
        return false;
    }
    const uint32_t seed = o.seed;
    // One replacement per species for the whole world (wild, trainers and gifts alike).
    std::vector<int> repl(Dex::N);
    for (int s = 0; s < Dex::N; s++) repl[s] = (o.randomize && s >= 1) ? dex.Pick(seed, 1, s, false) : s;
    int wildChanged = 0, partiesChanged = 0, giftsChanged = 0, added = 0, scriptsChanged = 0;

    // Wild Pokemon.
    const auto enc = NdsNarcFiles(r, n, "a/0/3/7");
    if (enc.empty()) { fprintf(stderr, "[world] no encounter data\n"); return false; }
    for (size_t f = 0; f < enc.size(); f++)
    {
        uint8_t* d = r + enc[f].first;
        if (enc[f].second < 196) continue;
        if (o.randomize && o.wild)
            Detail::ForEachEncounterSpecies(d, enc[f].second, [&](uint8_t* p) {
                const int s = (int)RomGet16(p);
                if (s >= 1 && s < Dex::N && repl[s] != s) { Put16(p, repl[s]); wildChanged++; }
            });
        if (o.more)
        {
            // Rare places only: the 5% grass slots 6 and 7 (a different species in the
            // morning, by day and at night) and the last two places of the water lists.
            const bool grass = d[0] != 0;
            auto late = [&](uint32_t salt) {
                const uint32_t h = Mix(seed ^ Mix((uint32_t)f * 131u + salt));
                return salt >= 100 ? Detail::LateWater[h % (sizeof(Detail::LateWater) / sizeof(int))]
                                   : Detail::LateSpecies[h % (sizeof(Detail::LateSpecies) / sizeof(int))];
            };
            if (grass && d[8 + 6] <= 45 && d[8 + 6] > 0)
                for (int t = 0; t < 3; t++)
                    for (int slot = 6; slot < 8; slot++)
                    {
                        Put16(d + 20 + 24 * t + 2 * slot, late(t * 4 + slot));
                        added++;
                    }
            const int water[3][2] = { { 100, 5 }, { 148, 5 }, { 168, 5 } }; // surf, good rod, super rod
            for (const auto& w : water)
            {
                const int rate = w[0] == 100 ? d[1] : w[0] == 148 ? d[4] : d[5];
                if (!rate) continue;
                for (int slot = 3; slot < 5; slot++)
                {
                    const int s = (int)RomGet16(d + w[0] + 4 * slot + 2);
                    if (s) { Put16(d + w[0] + 4 * slot + 2, late(100 + w[0] + slot)); added++; }
                }
            }
        }
    }

    // Trainers' parties.
    if (o.randomize && o.trainers)
    {
        const auto td = NdsNarcFiles(r, n, "a/0/5/5");
        const auto tp = NdsNarcFiles(r, n, "a/0/5/6");
        for (size_t t = 0; t < td.size() && t < tp.size(); t++)
        {
            if (td[t].second < 8) continue;
            const uint8_t* h = r + td[t].first;
            const int type = h[0], count = h[3];
            const int item = (type & 2) ? 2 : 0, mv = (type & 1) ? 8 : 0, stride = 8 + item + mv;
            if (count > 6 || (uint64_t)stride * count > tp[t].second) continue;
            for (int i = 0; i < count; i++)
            {
                uint8_t* m = r + tp[t].first + stride * i;
                const int s = (int)(RomGet16(m + 4) & 0x3FF), level = (int)RomGet16(m + 2);
                if (s < 1 || s >= Dex::N || repl[s] == s) continue;
                Put16(m + 4, repl[s]); // the form number (the top bits) is 0 for the new species
                m[1] = 0;              // the ability and gender picks were for the old species
                if (mv)
                {
                    uint16_t moves[4];
                    LearnsetMoves(r, dex, repl[s], level, moves);
                    for (int k = 0; k < 4; k++) Put16(m + 6 + item + 2 * k, moves[k]);
                }
                partiesChanged++;
            }
        }
    }

    // Gifts and eggs: scripts' GiveMon (command 137: species, level, item, form, ability,
    // variable) and GiveEgg (138: species, place). Commands sit at any byte offset.
    if (o.randomize && o.gifts)
    {
        const auto sc = NdsNarcFiles(r, n, "a/0/1/2");
        for (size_t f = 0; f < sc.size(); f++)
        {
            uint8_t* d = r + sc[f].first;
            const uint32_t size = sc[f].second;
            for (uint32_t k = 0; k + 14 <= size; k++)
            {
                const uint32_t cmd = RomGet16(d + k), s = RomGet16(d + k + 2);
                if (s < 1 || s >= (uint32_t)Dex::N || Dex::Legendary((int)s)) continue;
                const bool give = cmd == 137 && RomGet16(d + k + 4) >= 1 && RomGet16(d + k + 4) <= 100 && RomGet16(d + k + 6) < 600 &&
                    RomGet16(d + k + 8) < 40 && RomGet16(d + k + 10) <= 123 && RomGet16(d + k + 12) >= 0x4000;
                const bool egg = cmd == 138 && RomGet16(d + k + 4) == 14; // Primo's eggs (the place: Primo)
                if (!give && !egg) continue;
                const int to = egg ? dex.Pick(seed, 2, (int)s, true) : repl[s];
                if (to != (int)s) { Put16(d + k + 2, to); giftsChanged++; }
                k += 5;
            }
        }
    }

    if (o.randomize && (o.moves || o.abilities))
    {
        if (o.abilities)
            for (int s = 1; s < 494; s++)
            {
                uint8_t* p = r + dex.personal[s].first;
                if (s == 292 || s == 493 || s == 351) continue; // Shedinja, Arceus and Castform keep theirs
                static const int bad[] = { 25, 54, 112, 121, 59, 0 }; // Wonder Guard, Truant, Slow Start, Multitype, Forecast
                auto pick = [&](uint32_t salt) {
                    for (uint32_t t = 0;; t++)
                    {
                        const int a = 1 + (int)(Mix(seed ^ Mix(s * 31u + salt + t * 977u)) % 123);
                        if (std::find(std::begin(bad), std::end(bad), a) == std::end(bad)) return a;
                    }
                };
                if (p[22]) p[22] = (uint8_t)pick(5);
                if (p[23]) p[23] = (uint8_t)pick(6);
            }
        if (o.moves)
            for (int s = 1; s < 494; s++)
            {
                uint8_t* p = r + dex.learn[s].first;
                const uint32_t size = dex.learn[s].second;
                const int t1 = r[dex.personal[s].first + 6], t2 = r[dex.personal[s].first + 7];
                for (uint32_t i = 0; i + 2 <= size; i += 2)
                {
                    const uint32_t v = RomGet16(p + i);
                    if (v == 0xFFFF) break;
                    const int old = (int)(v & 0x1FF);
                    if (old < 1 || old >= 468) continue;
                    const uint8_t* om = r + dex.moveData[old].first;
                    const bool attack = om[2] != 1 && om[3] > 0;
                    for (uint32_t t = 0;; t++)
                    {
                        const int m = 1 + (int)(Mix(seed ^ Mix(s * 4099u + i * 13u + 7u + t * 31u)) % 467);
                        const uint8_t* nm = r + dex.moveData[m].first;
                        const bool nAttack = nm[2] != 1 && nm[3] > 0;
                        // Not a one-hit knockout, Struggle, Sketch, Selfdestruct or Explosion.
                        if (m == 12 || m == 32 || m == 90 || m == 329 || m == 165 || m == 166 || m == 120 || m == 153) continue;
                        if (nAttack != attack) continue;
                        if (t < 200 && attack && std::abs((int)nm[3] - (int)om[3]) > 35) continue; // about as strong
                        if (t < 300 && nm[4] != t1 && nm[4] != t2 && nm[4] != 0) continue;         // its type or Normal
                        Put16(p + i, (v & 0xFE00) | (uint32_t)m);
                        break;
                    }
                }
            }
    }

    // The three Poke Balls in Elm's lab. The balls' contents are in the game's code, so a frontend
    // sets them in memory (Poke below); the data side is that the rival's party and the
    // scripts that follow your choice test for the species of the ball (152, 155, 158), so
    // those tests now name the new ones: the rival still takes the one that beats yours.
    if (o.StartersChanged())
    {
        for (int i = 0; i < 3; i++)
        {
            const int orig = 152 + 3 * i;
            if (o.starters >= 1 && o.starters <= 3) o.resolved[i] = RegionStarters[o.starters][i];
            else if (o.starters == 4) o.resolved[i] = o.chosen[i];
            else o.resolved[i] = dex.Pick(seed, 3, orig, true);
        }
        const auto sc = NdsNarcFiles(r, n, "a/0/1/2");
        for (size_t f = 0; f < sc.size(); f++)
        {
            uint8_t* d = r + sc[f].first;
            const uint32_t size = sc[f].second;
            for (uint32_t k = 0; k + 4 <= size; k++)
            {
                // GetStarterChoice (206) variable, then within a few commands: CompareVarToValue (17) variable, species.
                if (RomGet16(d + k) != 206 || RomGet16(d + k + 2) < 0x4000) continue;
                for (uint32_t j = k + 4; j + 6 <= size && j < k + 100; j++)
                    if (RomGet16(d + j) == 17 && RomGet16(d + j + 2) == RomGet16(d + k + 2))
                    {
                        const uint32_t v = RomGet16(d + j + 4);
                        if (v == 152 || v == 155 || v == 158) { Put16(d + j + 4, o.resolved[(v - 152) / 3]); scriptsChanged++; }
                    }
            }
        }
    }
    if (o.skipTutorials)
    {
        // Route 29: the Dude's catching demonstration (CatchingTutorial, 251, after Wait 10): the
        // command does nothing now, and he still gives the five Poke Balls.
        const auto sc = NdsNarcFiles(r, n, "a/0/1/2");
        if (sc.size() > 225)
        {
            uint8_t* d = r + sc[225].first;
            const uint32_t size = sc[225].second;
            for (uint32_t k = 4; k + 2 <= size; k++)
                if (RomGet16(d + k) == 251 && RomGet16(d + k - 4) == 10 && RomGet16(d + k - 2) == 0x800C) { Put16(d + k, 0); scriptsChanged++; }
        }
    }

    if (o.moveTutor && MoveTutor::Patch(r, n)) scriptsChanged++;

    fprintf(stderr, "[world] seed %06u: %d wild, %d trainer Pokemon, %d gifts, %d added to routes, %d scripts\n", seed, wildChanged,
        partiesChanged, giftsChanged, added, scriptsChanged);
    return true;
}

// The three balls' species in memory: the game's code table (main memory) and the copy the
// choosing screen's overlay holds when it is loaded (the same address as other overlays, so
// only where the original three are found). Called before each frame of each console.
inline void Poke(uint8_t* mainRam, uint32_t mask, const Options& o)
{
    if (!o.StartersChanged()) return;
    for (uint32_t a : { 0x02108514u, 0x021E7398u })
    {
        uint8_t* p = mainRam + (a & mask);
        if (RomGet32(p) == 152 && RomGet32(p + 4) == 155 && RomGet32(p + 8) == 158)
        {
            for (int i = 0; i < 3; i++) { uint32_t v = o.resolved[i]; for (int b = 0; b < 4; b++) p[4 * i + b] = (uint8_t)(v >> (8 * b)); }
            static int said[2];
            if (!said[a == 0x021E7398u]++)  fprintf(stderr, "[world] the balls hold %d, %d, %d (at %08X)\n", o.resolved[0], o.resolved[1], o.resolved[2], a);
        }
    }
}
}
