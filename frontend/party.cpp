// Parties: games far apart use the Union Room, link battles and trades together.
//
// On a real DS these run over local wireless, where the consoles answer each other within
// microseconds; passing the radio traffic over the internet stalls the game (measured: it
// stops connecting at about 30 ms of round trip). So instead the game's own wireless layer
// (the "WirelessManager" of the Gen 4 games, in pret/pokeplatinum's names; asm/unk_02032844.s
// in pokeheartgold) is taken over: when the game calls one of its functions, this file does
// the work over a TCP connection to the relay server (tools/party_server.py) and returns.
// The layer above it, the game's communication system, allows several messages in flight
// and simply waits for late ones, as it does over Nintendo Wi-Fi, so lag slows exchanges
// down instead of breaking them.
//
// The game's WirelessManager struct stays the record of the state (connected, player
// number, callbacks); this file reads and writes its fields. Callbacks into the game
// (data received, a player joined, a parent found) are made at the start of the game's
// per-frame communication update, one after another: each returns to an address hooked
// for the purpose (Pad), and the next one starts there.
//
// Addresses are those of HeartGold and SoulSilver (US), which share this code; it is
// checked before anything is hooked.
//
// Around the link, the online HUD: who is in the party (name, colour, host), what they are
// doing, the round trip to the server, and events (a Pokemon caught, a badge earned),
// found by watching this player's save data in memory. Joining happens in the background:
// the game goes on while the connection is made, and a lost connection is retried for 30 s.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "NDS.h"
#include "ndsrom.h"
#include "party.h"

using namespace melonDS;

// melonDS/src/ARM.cpp
namespace melonDS
{
extern bool (*ssr_HleHook)(ARMv5* cpu, u32 addr);
extern u32 ssr_HleFirst, ssr_HleLast;
}

namespace
{
#ifdef _WIN32
using Socket = SOCKET;
const Socket NoSocket = INVALID_SOCKET;
#else
using Socket = int;
const Socket NoSocket = -1;
#endif

// The game's code (HeartGold/SoulSilver US).
constexpr u32 WmGlobal = 0x021D4124;    // +4: pointer to the WirelessManager
constexpr u32 FieldSystemPtr = 0x021D4158; // sFieldSysPtr
constexpr u32 MapHeaders = 0x020F6BE0;     // sMapHeaders: 24 bytes per map
constexpr u32 CommGlobal = 0x021D4150;  // pointer; +0x4E: the kind of link (CommManager_GetCommType)
constexpr u32 Pad = 0x02032874;         // WirelessManager_SetParentParameter: only ConnectServer calls it
constexpr u32 ConnectClientAuto = 0x02032B84, ConnectClientScan = 0x02032C1C, StartMeasureChannel = 0x020332C0,
    ConnectServer = 0x02033668, ConnectClient = 0x0203373C, SendMessage = 0x02033800, Reset = 0x02033830,
    Finalize = 0x02033858, End = 0x020338D0, SetGameInfo = 0x020339B4, SetEntry = 0x02033A0C,
    CommUpdate = 0x02036144; // called once per frame from the main loop

// WirelessManager fields.
constexpr u32 ParentGameInfo = 0x0, ParentGameInfoLen = 0x4, ParentGgid = 0x8;
constexpr u32 RecvBuffer = 0x1020; // the MP receive buffer: unused, as MP never starts
constexpr u32 BssDesc = 0x1220, ScanBssid = 0x12E8;
constexpr u32 ScanCallback = 0x1300, AutoConnect = 0x130E, State = 0x1310, ConnectionType = 0x1314,
    RecvFunc = 0x1318, GgidScanCallback = 0x1320, DisconnectCallback = 0x1324, ConnectCallback = 0x1328,
    Aid = 0x132C, Connected = 0x132E, ErrorCode = 0x1330, MaxConnections = 0x1334, PauseClient = 0x1335,
    MeasureChannel = 0x133C, MeasureBusy = 0x133E, LeastUsedChannels = 0x1340, PauseConnection = 0x1342,
    PauseSystem = 0x1343, EntrySet = 0x1344, BeaconsSent = 0x1345;
enum { StateStop, StateIdle, StateScan, StateBusy, StateConnected, StateCheckChannel = 7, StateError = 9 };
constexpr u32 ErrDisconnected = 20;
constexpr u8 AttrEntry = 1, AttrMb = 2, AttrKs = 4;

// The first instruction of each hooked function, to recognise the game.
const std::pair<u32, u32> Expected[] = {
    { Pad, 0x2003B508 }, { ConnectClientAuto, 0x1C17B5F8 }, { ConnectClientScan, 0x1C05B570 },
    { StartMeasureChannel, 0xB082B538 }, { ConnectServer, 0x1C05B5F8 }, { ConnectClient, 0xB081B578 },
    { SendMessage, 0x1C05B5F8 }, { Reset, 0x4807B508 }, { Finalize, 0x481AB508 }, { End, 0x2003B508 },
    { SetGameInfo, 0xB082B570 }, { SetEntry, 0x4A0AB510 }, { CommUpdate, 0xF002B510 },
};

Socket Sock = NoSocket;
int Me = -1;
bool Log = false;
u32 Frame = 0; // communication updates so far, for the log
NDS* Nds = nullptr;
bool Hooked = false;
std::vector<u8> In; // received bytes not yet made into messages

// The online side (see the top of this file).
PartyLink Link = PartyLink::Off;
std::string Server, Code, JoinCode; // the server, the party's code or name, and what to join
bool Hosting = false;
int HostMax = 4, MaxPlayers = 4;
int Dial = 0;                       // 1 connecting, 2 waiting for the player number
u32 DialSince = 0, LastHeard = 0, LastPing = 0, LastStatus = 0, LastScan = 0, LostAt = 0, LastTry = 0, SessionStart = 0;
int Rtt = -1;
PartyPeer Peers[4];
bool PendingJoin[4] = {};
std::deque<PartyEvent> Events;
std::string MyName = "Trainer";
int MyPartner = 0; // species shown with this player (0 none)
int MyLook = -1;   // this player's look (Party_SetLook)
int MyColour = 0, MyState = 0, MyMap = -1, SentState = -1, SentMap = -2;
// Friends: this player's id, their friends' ids, and which of those the server last said
// were online.
std::string MyId;
std::vector<std::string> FriendIds;
std::map<std::string, PartyFriendOnline> FriendsOn;
u32 LastPresence = 0;
std::vector<u8> SentBattle; // the last T message, and when
u32 LastBattle = 0;
const uint8_t* RomData = nullptr;
size_t RomSize = 0;
// This player's save data in memory: their trainer name and ID find it; the Pokedex is
// at the same distance from them as in the save file.
uint8_t TrainerName[16] = {};
u32 TrainerId = 0, DexDelta = 0;
bool HaveTrainer = false;
u32 ProfileAt = 0;
PartyCard SaveCard; // from the save file
std::vector<int> SpeciesOfParty(const uint8_t* general)
{
    // The party in the save data: at 0x90 of the general block (PlayerProfile is at 0x64):
    // the most, the count, then 236 bytes each, whose first 136 are a BoxPokemon: its
    // personality value, a checksum and four 32-byte blocks, shuffled by the personality
    // and encrypted with the checksum as the seed. The species is the first word of block A.
    std::vector<int> out;
    const uint8_t* ram = general;
    const u32 party = 0x90;
    const u32 count = std::min<u32>(RomGet32(ram + party + 4), 6);
    static const char* orders[24] = { "ABCD", "ABDC", "ACBD", "ACDB", "ADBC", "ADCB", "BACD", "BADC", "BCAD", "BCDA", "BDAC", "BDCA",
        "CABD", "CADB", "CBAD", "CBDA", "CDAB", "CDBA", "DABC", "DACB", "DBAC", "DBCA", "DCAB", "DCBA" };
    for (u32 i = 0; i < count; i++)
    {
        const uint8_t* m = ram + party + 8 + i * 236;
        const u32 pid = RomGet32(m), check = RomGet16(m + 6);
        const int posA = (int)(strchr(orders[((pid >> 13) & 31) % 24], 'A') - orders[((pid >> 13) & 31) % 24]);
        u32 seed = check;
        int species = 0;
        for (int w = 0; w <= posA * 16; w++)
        {
            seed = seed * 0x41C64E6D + 0x6073;
            if (w == posA * 16) species = (RomGet16(m + 8 + 2 * w) ^ (seed >> 16)) & 0xFFFF;
        }
        if (species > 0 && species <= 493) out.push_back(species);
    }
    return out;
}
uint8_t DexCaught[64];
bool DexKnown = false;
int Badges = -1, CaughtSession = 0;
void Lost(const char* why);
void Fail(const std::string& why);

// Parents seen in the party: player -> beacon (ggid, tgid, attribute, max entry, game info).
struct Beacon
{
    u32 ggid;
    u16 tgid;
    u8 attr, maxEntry;
    std::vector<u8> info;
};
std::map<int, Beacon> Beacons;
Beacon MyBeacon;
bool Serving = false;        // we are a parent
int ParentPlayer = -1;       // as a child: the parent's player number
std::map<int, int> AidOfPlayer, PlayerOfAid; // as a parent: our children
bool Scanning = false;
u32 ScanClock = 0;
int MeasureWait = 0, IdleWait = 0, EntryWait = 0;
u32 BeaconClock = 0;

// Calls into the game, made one after another at the start of the communication update.
struct Call
{
    u32 fn;
    u32 args[4];
    std::function<void()> prepare; // fills a buffer the call reads, just before it
    std::function<bool(u32* args)> ready; // just before it: false skips it (else it may set the arguments)
};
std::deque<Call> Calls;
bool InChain = false, SkipUpdate = false;
u32 Saved[6]; // r0-r3, lr and the entry (with the Thumb bit) of the interrupted update

u32 Ms()
{
    using namespace std::chrono;
    return (u32)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Game memory: main RAM and the data TCM (stack buffers).
u8* Ptr(u32 a)
{
    ARMv5& cpu = Nds->ARM9;
    if ((a & cpu.DTCMMask) == cpu.DTCMBase) return &cpu.DTCM[a & (DTCMPhysicalSize - 1)];
    if ((a >> 24) == 0x02) return &Nds->MainRAM[a & Nds->MainRAMMask];
    return nullptr;
}
u32 R8(u32 a) { u8* p = Ptr(a); return p ? *p : 0; }
u32 R16(u32 a) { u8* p = Ptr(a & ~1u); return p ? (u32)(p[0] | p[1] << 8) : 0; }
u32 R32(u32 a) { u8* p = Ptr(a & ~3u); return p ? (u32)(p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24) : 0; }
void W8(u32 a, u32 v) { if (u8* p = Ptr(a)) *p = (u8)v; }
void W16(u32 a, u32 v) { if (u8* p = Ptr(a & ~1u)) { p[0] = (u8)v; p[1] = (u8)(v >> 8); } }
void W32(u32 a, u32 v) { if (u8* p = Ptr(a & ~3u)) for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (8 * i)); }
std::vector<u8> ReadBytes(u32 a, u32 n)
{
    std::vector<u8> v(n);
    for (u32 i = 0; i < n; i++) v[i] = (u8)R8(a + i);
    return v;
}
void WriteBytes(u32 a, const std::vector<u8>& v)
{
    for (size_t i = 0; i < v.size(); i++) W8(a + (u32)i, v[i]);
}

u32 Wm() { return R32(WmGlobal + 4); }
u32 Field(u32 off, int size) { const u32 a = Wm() + off; return size == 1 ? R8(a) : size == 2 ? R16(a) : R32(a); }
void SetField(u32 off, int size, u32 v)
{
    const u32 a = Wm() + off;
    if (size == 1) W8(a, v); else if (size == 2) W16(a, v); else W32(a, v);
}
void SetState(u32 s) { SetField(State, 4, s); }
u32 CommType()
{
    const u32 p = R32(CommGlobal);
    return p ? R8(p + 0x4E) : 1;
}

// The wireless address the game is shown for a player: made up from the player number, as
// a scan result names a parent by its address. (The consoles' own addresses stay as they
// are: the game resets the clock-related parts of a save that it finds on another console.)
void MacOf(int player, u8* mac)
{
    const u8 base[6] = { 0x00, 0x09, 0xBF, 0x11, 0x22, 0x33 };
    memcpy(mac, base, 6);
    mac[5] = (u8)(mac[5] + player * 0x10);
}
int PlayerOfMac(const u8* mac)
{
    for (int p = 0; p < 4; p++)
    {
        u8 m[6];
        MacOf(p, m);
        if (!memcmp(m, mac, 6)) return p;
    }
    return -1;
}

// Messages to and from the server: u16 length, then the kind and the contents.
void Send(char kind, const std::vector<u8>& body = {})
{
    if (Sock == NoSocket) return;
    std::vector<u8> m(3 + body.size());
    const u16 len = (u16)(1 + body.size());
    m[0] = (u8)len;
    m[1] = (u8)(len >> 8);
    m[2] = (u8)kind;
    if (!body.empty()) memcpy(&m[3], body.data(), body.size());
    size_t sent = 0;
    while (sent < m.size())
    {
        const int n = send(Sock, (const char*)m.data() + sent, (int)(m.size() - sent), 0);
        if (n <= 0)
        {
            // Non-blocking: wait for room rather than drop part of a message.
            fd_set set;
            FD_ZERO(&set);
            FD_SET(Sock, &set);
            timeval tv = { 1, 0 };
            if (select((int)Sock + 1, nullptr, &set, nullptr, &tv) <= 0)
            {
                Lost("lost the connection to the server");
                return;
            }
            continue;
        }
        sent += n;
    }
}

void SendBeacon()
{
    std::vector<u8> b = { (u8)MyBeacon.ggid, (u8)(MyBeacon.ggid >> 8), (u8)(MyBeacon.ggid >> 16), (u8)(MyBeacon.ggid >> 24),
        (u8)MyBeacon.tgid, (u8)(MyBeacon.tgid >> 8), MyBeacon.attr, MyBeacon.maxEntry };
    b.insert(b.end(), MyBeacon.info.begin(), MyBeacon.info.end());
    Send('B', b);
}

void Leave()
{
    if (Serving || ParentPlayer >= 0) Send('L');
    if (Serving) Send('b');
    Serving = false;
    ParentPlayer = -1;
    AidOfPlayer.clear();
    PlayerOfAid.clear();
    Scanning = false;
}

// A parent's beacon as the WMBssDesc the scan callbacks get (NitroSDK layout, 0xC0 bytes).
std::vector<u8> BssDescOf(int player, const Beacon& b)
{
    std::vector<u8> d(0xC0, 0);
    d[0] = 0xC0 / 2;
    MacOf(player, &d[4]);
    d[10] = 32;
    d[0x36] = 1;                                 // channel
    d[0x32] = 200;                               // beacon period
    const u16 infoLen = (u16)(16 + b.info.size());
    d[0x3C] = (u8)infoLen;
    d[0x3D] = (u8)(infoLen >> 8);
    u8* g = &d[0x40];                            // WMGameInfo
    g[0] = 1;                                    // magic number
    g[2] = 1;                                    // version
    for (int i = 0; i < 4; i++) g[4 + i] = (u8)(b.ggid >> (8 * i));
    g[8] = (u8)b.tgid;
    g[9] = (u8)(b.tgid >> 8);
    g[10] = (u8)b.info.size();
    g[11] = b.attr;
    g[12] = 192;                                 // parent and child data sizes
    g[14] = 38;
    memcpy(g + 16, b.info.data(), std::min<size_t>(b.info.size(), 112));
    return d;
}

Call& Queue(u32 fn, u32 a0 = 0, u32 a1 = 0, u32 a2 = 0, u32 a3 = 0, std::function<void()> prepare = nullptr)
{
    static Call none;
    if (!fn) return none = Call();
    Calls.push_back({ fn, { a0, a1, a2, a3 }, std::move(prepare) });
    return Calls.back();
}

void Connect(int parent)
{
    ParentPlayer = parent;
    SetState(StateBusy);
    Send('C', { (u8)parent, (u8)CommType() });
    if (Log) fprintf(stderr, "[party] asking player %d to join (link %u)\n", parent + 1, CommType());
}

void SendHello()
{
    // No colour chosen: one by player number, so that everyone differs.
    std::vector<u8> b = { (u8)(MyColour >= 0 ? MyColour : Me & 3), (u8)MyPartner, (u8)(MyPartner >> 8) };
    b.insert(b.end(), MyName.begin(), MyName.end());
    Send('N', b);
    if (MyId.size() == 16) Send('U', std::vector<u8>(MyId.begin(), MyId.end()));
    LastPresence = 0; // and the friends again, at once
}

void Message(char kind, const u8* p, size_t n)
{
    switch (kind)
    {
    case 'I': // our player number
        if (n >= 1 && p[0] < 4 && Dial == 2)
        {
            Me = p[0];
            Dial = 0;
            for (PartyPeer& q : Peers) q = PartyPeer();
            Peers[Me].present = true;
            Peers[Me].sinceMs = Ms();
            if (Link == PartyLink::Lost) Events.push_back({ PartyEvent::Reconnected, Me });
            else
            {
                SessionStart = Ms();
                CaughtSession = 0;
            }
            Link = PartyLink::Online;
            LastHeard = LastPing = Ms();
            Rtt = -1;
            SentState = -1;
            SendHello();
            // Still the parent of a link the game started before the connection was lost.
            if (Serving) SendBeacon();
            fprintf(stderr, "[party] in the party as player %d\n", Me + 1);
        }
        break;
    case 'F':
        Fail(Hosting ? "The server could not make a party" : "That party is full, or there is no party with that code");
        break;
    case 'K':
        Code.assign((const char*)p, n);
        break;
    case 'M':
        if (n >= 1) MaxPlayers = p[0];
        break;
    case 'j': // a player joined (their name follows)
        if (n >= 1 && p[0] < 4)
        {
            Peers[p[0]] = PartyPeer();
            Peers[p[0]].present = true;
            Peers[p[0]].sinceMs = Ms();
            PendingJoin[p[0]] = true;
        }
        break;
    case 'l': // a player left
        if (n >= 1 && p[0] < 4)
        {
            if (Peers[p[0]].present && p[0] != Me)
            {
                PartyEvent e = { PartyEvent::Left, p[0] };
                e.text = Peers[p[0]].name;
                e.arg = Peers[p[0]].colour;
                Events.push_back(e);
            }
            Peers[p[0]] = PartyPeer();
            PendingJoin[p[0]] = false;
        }
        break;
    case 'N': // who a player is: player, flags, colour, partner (u16), name
        if (n >= 5 && p[0] < 4)
        {
            PartyPeer& q = Peers[p[0]];
            if (!q.present) q.sinceMs = Ms();
            q.present = true;
            q.host = p[1] & 1;
            q.colour = p[2] & 3;
            q.partner = p[3] | p[4] << 8;
            q.name.assign((const char*)p + 5, std::min<size_t>(n - 5, 20));
            if (PendingJoin[p[0]])
            {
                PendingJoin[p[0]] = false;
                Events.push_back({ PartyEvent::Joined, p[0] });
            }
        }
        break;
    case 'S': // what a player is doing: player, state, map (u16), badges, round trip (u16)
        if (n >= 5 && p[0] < 4)
        {
            PartyPeer& q = Peers[p[0]];
            q.state = p[1];
            q.map = p[2] | p[3] << 8;
            q.badges = p[4];
            if (n >= 7) q.rtt = p[5] | p[6] << 8;
        }
        break;
    case 'O': // where a player's trainer is: player, flags, matrix, map (u16), x, y, z (s32), facing, sprite (u16), look + 1 (u16)
        if (n >= 20 && p[0] < 4 && p[0] != Me)
        {
            if (n >= 23) Peers[p[0]].look = (int)RomGet16(p + 21) - 1; // (checked against the ROM's looks before use)
            PartyWalker& w = Peers[p[0]].walk;
            w.shown = p[1] & 1;
            w.matrix = RomGet16(p + 2);
            w.map = RomGet16(p + 4);
            w.x = (int32_t)RomGet32(p + 6);
            w.y = (int32_t)RomGet32(p + 10);
            w.z = (int32_t)RomGet32(p + 14);
            w.facing = p[18] & 3;
            w.sprite = n >= 21 ? RomGet16(p + 19) : 0;
            w.at = Ms();
        }
        break;
    case 'U': // a player's id: player, id (16)
        if (n >= 17 && p[0] < 4) Peers[p[0]].uid.assign((const char*)p + 1, 16);
        break;
    case 'Z': // a friend request or its answer: player, PartyFriend
        if (n >= 2 && p[0] < 4 && Peers[p[0]].present && !Peers[p[0]].uid.empty())
            Events.push_back({ PartyEvent::Friend, p[0], p[1] });
        break;
    case 'T': // a player's battle: player, flags (1 choosing, 2 link), turn, species (u16), HP %, the other side's
        if (n >= 9 && p[0] < 4 && p[0] != Me)
        {
            PartyBattle& b = Peers[p[0]].battle;
            const bool choosing = p[1] & 1;
            if (choosing && (!b.choosing || b.turn != p[2])) b.choosingAt = Ms();
            b.choosing = choosing;
            b.link = p[1] & 2;
            b.turn = p[2];
            b.species = RomGet16(p + 3);
            b.hp = p[5];
            b.foeSpecies = RomGet16(p + 6);
            b.foeHp = p[8];
            b.at = Ms();
        }
        break;
    case 'y': // the friends online: (id, players, most, code length, code)...
    {
        FriendsOn.clear();
        size_t at = 0;
        while (at + 19 <= n && at + 19 + p[at + 18] <= n)
        {
            PartyFriendOnline& f = FriendsOn[std::string((const char*)p + at, 16)];
            f.count = p[at + 16];
            f.max = p[at + 17];
            f.code.assign((const char*)p + at + 19, p[at + 18]);
            at += 19 + p[at + 18];
        }
        break;
    }
    case 'g': // a friend's invite to their session: id, code length, code, name
        if (n >= 17 && 17 + (size_t)p[16] <= n)
        {
            PartyEvent e = { PartyEvent::SessionInvite, -1 };
            e.from.assign((const char*)p, 16);
            e.text.assign((const char*)p + 17, p[16]);
            e.name.assign((const char*)p + 17 + p[16], std::min<size_t>(n - 17 - p[16], 20));
            Events.push_back(e);
        }
        break;
    case 'V': // an invite or its answer: player, PartyInvite
        if (n >= 2 && p[0] < 4 && Peers[p[0]].present)
            Events.push_back({ PartyEvent::Invite, p[0], p[1] });
        break;
    case 'W': // an emote: player, emote
        if (n >= 2 && p[0] < 4)
        {
            Peers[p[0]].walk.emote = p[1];
            Peers[p[0]].walk.emoteAt = Ms();
        }
        break;
    case 'E': // an event: player, kind, argument (u16)
        if (n >= 4 && p[0] < 4 && (p[1] == 1 || p[1] == 2))
            Events.push_back({ p[1] == 1 ? PartyEvent::Caught : PartyEvent::Badge, p[0], p[2] | p[3] << 8 });
        break;
    case 'q': // the answer to a ping
        if (n >= 4)
        {
            const int r = (int)(Ms() - RomGet32(p));
            if (r >= 0 && r < 60000) Rtt = Rtt < 0 ? r : (Rtt * 3 + r) / 4;
        }
        break;
    case 'B': // a parent's beacon: player, ggid, tgid, attribute, max entry, game info
        if (n >= 9)
        {
            Beacon b;
            b.ggid = p[1] | p[2] << 8 | p[3] << 16 | (u32)p[4] << 24;
            b.tgid = (u16)(p[5] | p[6] << 8);
            b.attr = p[7];
            b.maxEntry = p[8];
            b.info.assign(p + 9, p + n);
            Beacons[p[0]] = b;
        }
        break;
    case 'b':
        if (n >= 1) Beacons.erase(p[0]);
        break;
    case 'C': // a child asks to join us: player, link type
        if (n >= 2)
        {
            const int child = p[0];
            const u16 connected = (u16)Field(Connected, 2);
            int count = 0;
            for (int i = 0; i < 16; i++) count += (connected >> i) & 1;
            int aid = 1;
            while (aid < 16 && (connected & (1 << aid))) aid++;
            // The checks of WirelessManager_FinishStartServer.
            if (!Serving || Field(PauseSystem, 1) || Field(PauseConnection, 1) || count >= (int)Field(MaxConnections, 1)
                || p[1] != CommType() || aid >= 16 || AidOfPlayer.count(child))
            {
                Send('R', { (u8)child });
                if (Log) fprintf(stderr, "[party] turned player %d away\n", child + 1);
                break;
            }
            SetField(Connected, 2, connected | (1 << aid));
            AidOfPlayer[child] = aid;
            PlayerOfAid[aid] = child;
            Send('A', { (u8)child, (u8)aid });
            Queue(Field(ConnectCallback, 4), aid);
            if (Log) fprintf(stderr, "[party] player %d joined us as %d\n", child + 1, aid);
        }
        break;
    case 'A': // the parent took us: our number
        if (n >= 1 && ParentPlayer >= 0)
        {
            if (Field(PauseClient, 1))
            {
                SetField(ErrorCode, 4, ErrDisconnected);
                SetState(StateError);
                Leave();
                break;
            }
            SetField(Aid, 2, p[0]);
            SetState(StateConnected);
            if (Log) fprintf(stderr, "[party] joined player %d as %d\n", ParentPlayer + 1, p[0]);
        }
        break;
    case 'R': // the parent turned us away
    case 'P': // the parent has gone
        if (ParentPlayer >= 0)
        {
            SetField(ErrorCode, 4, ErrDisconnected);
            SetState(StateError);
            ParentPlayer = -1;
            if (Log) fprintf(stderr, "[party] %s\n", kind == 'R' ? "the parent turned us away" : "the parent left");
        }
        break;
    case 'X': // a child has gone: player
        if (n >= 1 && AidOfPlayer.count(p[0]))
        {
            const int aid = AidOfPlayer[p[0]];
            AidOfPlayer.erase(p[0]);
            PlayerOfAid.erase(aid);
            SetField(Connected, 2, Field(Connected, 2) & ~(1u << aid));
            Queue(Field(DisconnectCallback, 4), aid);
            Queue(Field(RecvFunc, 4), aid, 0, 0);
            if (Log) fprintf(stderr, "[party] child %d left\n", aid);
        }
        break;
    case 'D': // data: from player, port, contents
        if (n >= 2)
        {
            int aid = 0;
            if (Serving)
            {
                auto it = AidOfPlayer.find(p[0]);
                if (it == AidOfPlayer.end()) break;
                aid = it->second;
            }
            else if (p[0] != ParentPlayer)
                break;
            std::vector<u8> data(p + 2, p + n); // (one port, 14, carries everything)
            static int shown = 0;
            if (Log && shown++ < 6)
                fprintf(stderr, "[party] %u: data from %d (%zu bytes): %02X %02X %02X %02X %02X %02X\n", Frame, aid, data.size(),
                    data[0], data[1], data[2], data[3], data[4], data[5]);
            const u32 buf = Wm() + RecvBuffer;
            Queue(Field(RecvFunc, 4), aid, buf, (u32)data.size(), 0, [buf, data] { WriteBytes(buf, data); });
        }
        break;
    }
}

void Receive()
{
    if (Sock == NoSocket || Dial == 1) return;
    for (;;)
    {
        u8 buf[4096];
        const int n = recv(Sock, (char*)buf, sizeof buf, 0);
        if (n > 0)
        {
            In.insert(In.end(), buf, buf + n);
            LastHeard = Ms();
            continue;
        }
#ifdef _WIN32
        const bool wouldBlock = n < 0 && WSAGetLastError() == WSAEWOULDBLOCK;
#else
        const bool wouldBlock = n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
#endif
        if (!wouldBlock)
        {
            Lost("the server closed the connection");
            return;
        }
        break;
    }
    size_t at = 0;
    while (In.size() - at >= 2)
    {
        const size_t len = In[at] | In[at + 1] << 8;
        if (In.size() - at - 2 < len) break;
        if (len) Message((char)In[at + 2], &In[at + 3], len - 1);
        at += 2 + len;
    }
    In.erase(In.begin(), In.begin() + at);
}

// Once per frame, before the game's communication update.
void Update()
{
    Frame++;
    Receive();
    const u32 state = Field(State, 4);

    if (Scanning && state != StateScan)
    {
        // WirelessManager_StopScan: the scan ends and the manager is idle again.
        Scanning = false;
        SetField(AutoConnect, 2, 0);
        SetState(StateIdle);
    }
    else if (Scanning && ++ScanClock % 10 == 0)
    {
        // WirelessManager_FinishStartScan, for every parent in the party.
        u8 want[6];
        for (int i = 0; i < 6; i++) want[i] = (u8)R8(Wm() + ScanBssid + i);
        const bool any = !memcmp(want, "\xFF\xFF\xFF\xFF\xFF\xFF", 6);
        const u32 myGgid = Field(ParentGgid, 4);
        for (const auto& [player, b] : Beacons)
        {
            u8 mac[6];
            MacOf(player, mac);
            if (!any && memcmp(mac, want, 6)) continue;
            const std::vector<u8> desc = BssDescOf(player, b);
            const u32 bss = Wm() + BssDesc;
            auto fill = [bss, desc] { WriteBytes(bss, desc); };
            if (b.info.size() >= 8) Queue(Field(GgidScanCallback, 4), b.ggid, b.info[4], 0, 0, fill);
            if (b.ggid != myGgid || (b.attr & (AttrEntry | AttrMb)) != AttrEntry) continue;
            Queue(Field(ScanCallback, 4), bss, 0, 0, 0, fill);
            if (Field(AutoConnect, 2))
            {
                WriteBytes(bss, desc);
                Scanning = false;
                SetState(StateIdle);
                Connect(player);
                break;
            }
        }
    }

    if (Serving && ++BeaconClock % 6 == 0 && Field(BeaconsSent, 1) < 255)
        SetField(BeaconsSent, 1, Field(BeaconsSent, 1) + 1);
    if (MeasureWait && --MeasureWait == 0)
    {
        // Every channel equally quiet: channel 1.
        SetField(LeastUsedChannels, 2, 1);
        SetField(MeasureBusy, 2, 0);
        SetState(StateCheckChannel);
    }
    if (IdleWait && --IdleWait == 0) SetState(StateIdle);
    if (EntryWait && --EntryWait == 0) SetField(EntrySet, 1, 1);
}

// Starts the next queued call, or goes back into the interrupted communication update.
void NextCall(ARMv5* cpu)
{
    while (!Calls.empty() && !Calls.front().fn) Calls.pop_front();
    if (Calls.empty())
    {
        InChain = false;
        SkipUpdate = true;
        for (int i = 0; i < 4; i++) cpu->R[i] = Saved[i];
        cpu->R[14] = Saved[4];
        cpu->JumpTo(Saved[5]);
        return;
    }
    Call c = std::move(Calls.front());
    Calls.pop_front();
    if (c.ready && !c.ready(c.args))
    {
        NextCall(cpu);
        return;
    }
    if (Log && c.fn != Field(RecvFunc, 4))
        fprintf(stderr, "[party] %u: calling %08X(%X, %X)\n", Frame, c.fn, c.args[0], c.args[1]);
    if (c.prepare) c.prepare();
    for (int i = 0; i < 4; i++) cpu->R[i] = c.args[i];
    cpu->R[14] = Pad | 1;
    cpu->JumpTo(c.fn);
}

void Return(ARMv5* cpu, u32 value)
{
    cpu->R[0] = value;
    cpu->JumpTo(cpu->R[14]);
}

bool Hook(ARMv5* cpu, u32 addr)
{
    u32* r = cpu->R;
    bool hooked = false;
    for (const auto& e : Expected) hooked |= e.first == addr;
    if (!hooked) return false;
    if (Log && addr != Pad && addr != CommUpdate)
        fprintf(stderr, "[party] %u: %08X(%X, %X, %X, %X) from %08X, state %u\n", Frame, addr, r[0], r[1], r[2], r[3], r[14],
            Field(State, 4));
    switch (addr)
    {
    case Pad:
        if (!InChain) return false;
        NextCall(cpu);
        return true;

    case CommUpdate:
        if (SkipUpdate || InChain)
        {
            SkipUpdate = false;
            return false;
        }
        Update();
        if (Calls.empty()) return false;
        InChain = true;
        for (int i = 0; i < 4; i++) Saved[i] = r[i];
        Saved[4] = r[14];
        Saved[5] = CommUpdate | ((cpu->CPSR & 0x20) ? 1 : 0);
        NextCall(cpu);
        return true;

    case ConnectClientAuto:
    case ConnectClientScan:
    {
        // (connectionType or scan callback, wanted MAC address, channel)
        const bool autoConnect = addr == ConnectClientAuto;
        SetState(StateScan);
        if (autoConnect) SetField(ConnectionType, 4, r[0]);
        SetField(ScanCallback, 4, autoConnect ? 0 : r[0]);
        SetField(AutoConnect, 2, autoConnect);
        for (int i = 0; i < 6; i++) W8(Wm() + ScanBssid + i, R8(r[1] + i));
        Scanning = true;
        ScanClock = 9;
        Return(cpu, 1);
        return true;
    }

    case StartMeasureChannel:
        SetField(MeasureChannel, 2, 0);
        SetField(MeasureBusy, 2, 101);
        SetState(StateBusy);
        MeasureWait = 2;
        Return(cpu, 1);
        return true;

    case ConnectServer:
    {
        // (connectionType, tgid, channel, maxEntry, [beaconPeriod, entryFlag])
        const u32 entry = R32(r[13] + 4);
        SetField(ConnectionType, 4, r[0]);
        SetField(Aid, 2, 0);
        SetField(Connected, 2, 1);
        MyBeacon.ggid = Field(ParentGgid, 4);
        MyBeacon.tgid = (u16)r[1];
        MyBeacon.maxEntry = (u8)r[3];
        MyBeacon.attr = (entry ? AttrEntry : 0) | (r[0] == 2 ? AttrKs : 0);
        MyBeacon.info = ReadBytes(Field(ParentGameInfo, 4), std::min<u32>(Field(ParentGameInfoLen, 2), 112));
        Serving = true;
        BeaconClock = 0;
        SendBeacon();
        SetState(StateConnected);
        if (Log) fprintf(stderr, "[party] serving: ggid %08X, link %u, up to %u\n", MyBeacon.ggid, CommType(), r[3]);
        Return(cpu, 1);
        return true;
    }

    case ConnectClient:
    {
        // (connectionType, WMBssDesc*)
        const std::vector<u8> desc = ReadBytes(r[1], 0xC0);
        WriteBytes(Wm() + BssDesc, desc);
        SetField(ConnectionType, 4, r[0]);
        const int parent = PlayerOfMac(&desc[4]);
        if (parent < 0 || parent == Me)
        {
            SetField(ErrorCode, 4, ErrDisconnected);
            SetState(StateError);
        }
        else
            Connect(parent);
        Return(cpu, 1);
        return true;
    }

    case SendMessage:
    {
        // (message, size, port, callback): WirelessManager_SendMessage
        if (Field(Aid, 2) == 0 && !(Field(Connected, 2) & 0xFE))
        {
            Return(cpu, 0);
            return true;
        }
        std::vector<u8> body = ReadBytes(r[0], std::min<u32>(r[1], 512));
        body.insert(body.begin(), (u8)r[2]);
        Send('D', body);
        Queue(r[3], 1);
        Return(cpu, 1);
        return true;
    }

    case Finalize:
    case Reset:
        if (addr == Finalize && Field(State, 4) == StateIdle)
        {
            Return(cpu, 0);
            return true;
        }
        Leave();
        SetState(StateBusy);
        IdleWait = 2;
        Return(cpu, 0);
        return true;

    case End:
        Leave();
        return false; // the game's own WM_End follows

    case SetGameInfo:
        // (buffer, size, ggid, tgid)
        if (Serving && Field(State, 4) == StateConnected)
        {
            MyBeacon.info = ReadBytes(r[0], std::min<u32>(r[1], 112));
            MyBeacon.ggid = r[2];
            MyBeacon.tgid = (u16)r[3];
            MyBeacon.attr |= AttrEntry;
            SendBeacon();
        }
        Return(cpu, 0);
        return true;

    case SetEntry:
        SetField(EntrySet, 1, 0);
        if (Serving && Field(State, 4) == StateConnected)
        {
            MyBeacon.attr = (MyBeacon.attr & ~AttrEntry) | (r[0] ? AttrEntry : 0);
            SendBeacon();
            EntryWait = 1;
            Return(cpu, 1);
        }
        else
            Return(cpu, 0);
        return true;
    }
    return false;
}

void CloseSocket()
{
    if (Sock == NoSocket) return;
#ifdef _WIN32
    closesocket(Sock);
#else
    close(Sock);
#endif
    Sock = NoSocket;
    In.clear();
    Dial = 0;
}

// The game's link partners are gone with the connection: as when they leave.
void DropLink()
{
    if (!Hooked || !Nds) return;
    if (ParentPlayer >= 0)
    {
        SetField(ErrorCode, 4, ErrDisconnected);
        SetState(StateError);
        ParentPlayer = -1;
    }
    for (const auto& [player, aid] : AidOfPlayer)
    {
        SetField(Connected, 2, Field(Connected, 2) & ~(1u << aid));
        Queue(Field(DisconnectCallback, 4), aid);
        Queue(Field(RecvFunc, 4), aid, 0, 0);
    }
    AidOfPlayer.clear();
    PlayerOfAid.clear();
    Beacons.clear();
}

void Lost(const char* why)
{
    CloseSocket();
    DropLink();
    if (Link == PartyLink::Online)
    {
        fprintf(stderr, "[party] %s; trying again\n", why);
        Link = PartyLink::Lost;
        LostAt = LastTry = Ms();
        Events.push_back({ PartyEvent::Reconnecting, Me });
    }
    else if (Link == PartyLink::Connecting)
        Fail(why);
}

void Fail(const std::string& why)
{
    CloseSocket();
    if (Link == PartyLink::Lost) return; // keeps trying until its time is up
    DropLink();
    fprintf(stderr, "[party] %s\n", why.c_str());
    Link = PartyLink::Off;
    Me = -1;
    for (PartyPeer& q : Peers) q = PartyPeer();
    PartyEvent e = { PartyEvent::Failed, -1 };
    e.text = why;
    Events.push_back(e);
}

// Starts connecting to Server; the rest happens in Party_Process.
bool Dialup()
{
#ifdef _WIN32
    static bool started = false;
    WSADATA wsa;
    if (!started && WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    started = true;
#endif
    const size_t colon = Server.rfind(':');
    const std::string host = colon == std::string::npos ? Server : Server.substr(0, colon);
    const std::string port = colon == std::string::npos ? "7373" : Server.substr(colon + 1);
    addrinfo hints = {}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) return false;
    // An IPv4 address when the name has one: older servers listen on IPv4 only, and
    // "localhost" gives ::1 first on Windows.
    const addrinfo* use = res;
    for (const addrinfo* a = res; a; a = a->ai_next)
        if (a->ai_family == AF_INET)
        {
            use = a;
            break;
        }
    Sock = socket(use->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (Sock == NoSocket)
    {
        freeaddrinfo(res);
        return false;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(Sock, FIONBIO, &nb);
#else
    fcntl(Sock, F_SETFL, fcntl(Sock, F_GETFL) | O_NONBLOCK);
#endif
    int one = 1;
    setsockopt(Sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
    connect(Sock, use->ai_addr, (int)use->ai_addrlen); // finishes in the background
    freeaddrinfo(res);
    Dial = 1;
    DialSince = Ms();
    return true;
}

// The connection being made: once connected, asks to join (or host).
void DialStep()
{
    if (Ms() - DialSince > 5000)
    {
        Fail(Dial == 1 ? "Cannot reach the server " + Server : "No answer from the server " + Server);
        return;
    }
    if (Dial != 1) return;
    fd_set w, e;
    FD_ZERO(&w);
    FD_ZERO(&e);
    FD_SET(Sock, &w);
    FD_SET(Sock, &e);
    timeval tv = { 0, 0 };
    if (select((int)Sock + 1, nullptr, &w, &e, &tv) <= 0) return;
    int err = 0;
    socklen_t len = sizeof err;
    getsockopt(Sock, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    if (err || FD_ISSET(Sock, &e))
    {
        Fail("Cannot reach the server " + Server);
        return;
    }
    Dial = 2;
    if (Hosting) Send('H', { (u8)HostMax });
    else Send('J', std::vector<u8>(JoinCode.begin(), JoinCode.end()));
}

// This player's badges and Pokemon caught, from their save data in memory, as events.
void ScanSave()
{
    if (!Nds || !HaveTrainer) return;
    uint8_t* ram = Nds->MainRAM;
    const u32 size = Nds->MainRAMMask + 1;
    auto at = [&](u32 profile) {
        return profile + 20 <= size && profile + DexDelta + 4 + 64 <= size && !memcmp(ram + profile, TrainerName, 16) &&
            RomGet32(ram + profile + 16) == TrainerId && RomGet32(ram + profile + DexDelta) == 0xBEEFCAFE;
    };
    if (!ProfileAt || !at(ProfileAt))
    {
        ProfileAt = 0;
        DexKnown = false;
        for (u32 a = 0; a + 20 <= size; a += 4)
            if (at(a)) { ProfileAt = a; break; }
        if (!ProfileAt) return;
    }
    // Badges: Johto's (+0x1A) and Kanto's (+0x1F), one bit each.
    int b = 0;
    for (int i = 0; i < 8; i++) b += (ram[ProfileAt + 0x1A] >> i & 1) + (ram[ProfileAt + 0x1F] >> i & 1);
    if (Badges >= 0 && b == Badges + 1 && Link == PartyLink::Online) Send('E', { 2, (u8)b, 0 });
    Badges = b;
    const uint8_t* caught = ram + ProfileAt + DexDelta + 4;
    if (DexKnown)
    {
        int fresh = 0;
        for (int i = 0; i < 64; i++) fresh += __builtin_popcount(caught[i] & ~DexCaught[i]);
        // A handful at most: more is another save (a state loaded), not catches.
        if (fresh <= 3)
            for (int i = 0; i < 64 * 8; i++)
                if ((caught[i >> 3] & ~DexCaught[i >> 3]) >> (i & 7) & 1)
                {
                    CaughtSession++;
                    if (Link == PartyLink::Online) Send('E', { 1, (u8)(i + 1), (u8)((i + 1) >> 8) });
                }
    }
    memcpy(DexCaught, caught, 64);
    DexKnown = true;
}

// The game's code is in RAM once it has unpacked itself; hook only if it is the expected code.
void TryHook()
{
    if (Hooked || !Nds) return;
    for (const auto& [addr, word] : Expected)
        if (R32(addr) != word) return;
    ssr_HleHook = Hook;
    ssr_HleFirst = Pad;
    ssr_HleLast = CommUpdate;
    Hooked = true;
    fprintf(stderr, "[party] wireless play goes through the party server\n");
}

}

void Party_SetProfile(const std::string& name, int colour)
{
    MyName = name.empty() ? "Trainer" : name.substr(0, 10);
    MyColour = colour >= 0 ? colour & 3 : -1;
    if (Link == PartyLink::Online) SendHello();
}

bool Party_Join(const std::string& server, const std::string& code)
{
    Party_Leave();
    Server = server;
    JoinCode = code;
    Hosting = false;
    Log = getenv("SSR_PARTY_LOG") != nullptr;
    if (!Dialup())
    {
        Fail("Cannot find the server " + server);
        return false;
    }
    Link = PartyLink::Connecting;
    return true;
}

bool Party_Host(const std::string& server, int maxPlayers)
{
    Party_Leave();
    Server = server;
    Hosting = true;
    HostMax = std::max(2, std::min(4, maxPlayers));
    Log = getenv("SSR_PARTY_LOG") != nullptr;
    if (!Dialup())
    {
        Fail("Cannot find the server " + server);
        return false;
    }
    Link = PartyLink::Connecting;
    return true;
}

void Party_Attach(NDS& nds)
{
    Nds = &nds;
}

int Party_Count();

// This player's trainer, read every frame (for the HUD) and sent to the others when `send`
// (when it changed, and every second anyway):
// sFieldSysPtr -> location (the map) and playerAvatar -> its map object, whose sprite,
// facing and world position the others draw.
u32 LastWalk = 0, LastWalkSent = 0;
std::vector<u8> SentWalk;
void SendWalk(u32 now, int state, bool send)
{
    // In a battle or a menu the others keep seeing the trainer where it was (sent again
    // every second, for anyone who joins meanwhile).
    if (state != PS_FIELD && state != PS_LINK)
    {
        // The look is chosen in the menu: the others see it within the second.
        if (SentWalk.size() == 22)
        {
            SentWalk[20] = (u8)(MyLook + 1);
            SentWalk[21] = (u8)((MyLook + 1) >> 8);
            Peers[Me & 3].look = MyLook;
        }
        if (send && !SentWalk.empty() && now - LastWalkSent >= 1000)
        {
            Send('O', SentWalk);
            LastWalk = LastWalkSent = now;
        }
        return;
    }
    const u32 fs = R32(FieldSystemPtr), loc = R32(fs + 0x20), obj = R32(R32(fs + 0x40) + 0x30);
    const u32 map = R32(loc);
    const bool shown = state == PS_FIELD && fs && obj && map < 540 && !(R32(obj) & 0x200);
    const u32 matrix = shown ? R16(MapHeaders + 24 * map + 4) : 0xFFFF;
    std::vector<u8> b = { (u8)shown, (u8)matrix, (u8)(matrix >> 8), (u8)map, (u8)(map >> 8) };
    for (int k = 0; k < 3; k++)
    {
        const u32 v = shown ? R32(obj + 0x70 + 4 * k) : 0;
        for (int i = 0; i < 4; i++) b.push_back((u8)(v >> (8 * i)));
    }
    const u32 sprite = R32(obj + 0x10);
    b.push_back((u8)(R32(obj + 0x28) & 3));
    b.push_back((u8)sprite);
    b.push_back((u8)(sprite >> 8));
    b.push_back((u8)(MyLook + 1));
    b.push_back((u8)((MyLook + 1) >> 8));
    Peers[Me & 3].look = MyLook;
    PartyWalker& w = Peers[Me & 3].walk;
    w.shown = shown;
    w.matrix = (int)matrix;
    w.map = (int)map;
    w.x = (int32_t)R32(obj + 0x70);
    w.y = (int32_t)R32(obj + 0x74);
    w.z = (int32_t)R32(obj + 0x78);
    w.facing = R32(obj + 0x28) & 3;
    w.sprite = (int)sprite;
    w.at = now;
    if (!send) return;
    LastWalk = now;
    if (b == SentWalk && now - LastWalkSent < 1000) return;
    SentWalk = b;
    LastWalkSent = now;
    Send('O', b);
}

void Party_Process(int state, int map)
{
    MyState = state;
    MyMap = map;
    static PartyLink shownLink = PartyLink::Off;
    static int shownCount = 0;
    static u32 updates = 0;
    updates++;
    if (Log && (Link != shownLink || Party_Count() != shownCount))
        fprintf(stderr, "[party] link %d, %d players, dial %d (update %u)\n", (int)Link, Party_Count(), Dial, updates);
    shownLink = Link;
    shownCount = Party_Count();
    if (Dial) DialStep();
    Receive();
    // (After reading: what was heard is never later than `now`.)
    const u32 now = Ms();
    // This player's save, also offline (for the profile's numbers).
    if (now - LastScan >= 1000)
    {
        ScanSave();
        LastScan = now;
    }
    if (Link == PartyLink::Online && Sock != NoSocket)
    {
        TryHook();
        if (now - LastHeard > 3500)
            Lost("no answer from the server");
        else
        {
            if (now - LastPing >= 1000)
            {
                Send('Q', { (u8)now, (u8)(now >> 8), (u8)(now >> 16), (u8)(now >> 24) });
                LastPing = now;
            }
            if (Sock != NoSocket) SendWalk(now, state, now - LastWalk >= 100);
            // The friends' presence, every few seconds (the list also tells the server who
            // may see this player online).
            if (Sock != NoSocket && MyId.size() == 16 && (!LastPresence || now - LastPresence >= 3000))
            {
                std::vector<u8> ids;
                for (const std::string& id : FriendIds)
                    if (id.size() == 16 && ids.size() < 64 * 16) ids.insert(ids.end(), id.begin(), id.end());
                Send('Y', ids);
                LastPresence = now;
            }
            if (Sock != NoSocket && (now - LastStatus >= 2000 || state != SentState || map != SentMap))
            {
                const int r = std::max(0, std::min(65535, Rtt));
                Send('S', { (u8)state, (u8)map, (u8)(map >> 8), (u8)std::max(0, Badges), (u8)r, (u8)(r >> 8) });
                Peers[Me].state = state;
                Peers[Me].map = map;
                Peers[Me].badges = std::max(0, Badges);
                Peers[Me].rtt = Rtt;
                SentState = state;
                SentMap = map;
                LastStatus = now;
            }
        }
    }
    else if (Link == PartyLink::Lost)
    {
        if (now - LostAt > 30000)
        {
            Link = PartyLink::Connecting; // so that Fail ends it
            Fail("Could not reconnect: playing offline");
        }
        else if (!Dial && now - LastTry >= 3000)
        {
            // Back into the same party, by its code or name.
            LastTry = now;
            if (!Code.empty()) JoinCode = Code;
            Hosting = false;
            Dialup();
        }
    }
}

// The local party server (see party.h).
#ifdef _WIN32
static PROCESS_INFORMATION LocalServer = {};
#else
static pid_t LocalServer = 0;
#endif

bool Party_IsLocalServer(const std::string& server)
{
    std::string host = server;
    if (host.size() > 2 && host.front() == '[') host = host.substr(1, host.find(']') - 1);
    else if (std::count(host.begin(), host.end(), ':') == 1) host = host.substr(0, host.find(':'));
    return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

bool Party_StartLocalServer(const std::string& script)
{
#ifdef _WIN32
    if (LocalServer.hProcess) return true;
    // pythonw has no console window; the py launcher and python are tried after it.
    for (const char* python : { "pythonw", "pyw -3", "python" })
    {
        std::string cmd = std::string(python) + " \"" + script + "\"";
        std::wstring wcmd(cmd.size() * 2 + 1, L'\0');
        wcmd.resize(MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, &wcmd[0], (int)wcmd.size()) - 1);
        STARTUPINFOW si = {};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(pi.hThread);
            pi.hThread = nullptr;
            LocalServer = pi;
            fprintf(stderr, "[party] local party server started (%s)\n", python);
            return true;
        }
    }
    return false;
#elif defined(__ANDROID__)
    // No Python there, and forking an app's process (with the Java VM in it) is unsafe.
    (void)script;
    return false;
#else
    if (LocalServer) return true;
    const pid_t pid = fork();
    if (pid == 0)
    {
        // The child: quiet, then the server (python3, else python).
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execlp("python3", "python3", script.c_str(), (char*)nullptr);
        execlp("python", "python", script.c_str(), (char*)nullptr);
        _exit(127);
    }
    if (pid < 0) return false;
    LocalServer = pid;
    fprintf(stderr, "[party] local party server started\n");
    return true;
#endif
}

void Party_StopLocalServer()
{
#ifdef _WIN32
    if (!LocalServer.hProcess) return;
    TerminateProcess(LocalServer.hProcess, 0);
    CloseHandle(LocalServer.hProcess);
    LocalServer = {};
#else
    if (!LocalServer) return;
    kill(LocalServer, SIGTERM);
    waitpid(LocalServer, nullptr, 0);
    LocalServer = 0;
#endif
}

std::string Party_LanAddress()
{
#ifdef _WIN32
    static bool started = false;
    WSADATA wsa;
    if (!started && WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return "";
    started = true;
#endif
    // The address a connection towards the internet would leave from (nothing is sent: a
    // UDP socket only picks its route on connect).
    const int s = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return "";
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
    std::string out;
    if (connect(s, (sockaddr*)&to, sizeof to) == 0)
    {
        sockaddr_in me = {};
        socklen_t len = sizeof me;
        char buf[64] = {};
        if (getsockname(s, (sockaddr*)&me, &len) == 0 && inet_ntop(AF_INET, &me.sin_addr, buf, sizeof buf)) out = buf;
    }
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return out == "0.0.0.0" ? "" : out;
}

void Party_Leave()
{
    if (Link == PartyLink::Online && Me >= 0) Leave();
    CloseSocket();
    DropLink();
    Link = PartyLink::Off;
    Me = -1;
    Code.clear();
    Rtt = -1;
    FriendsOn.clear();
    for (PartyPeer& q : Peers) q = PartyPeer();
    // The hook stays: the game's wireless code goes on believing in its (now empty) party,
    // as if every other console had been switched off.
}

void Party_SetMax(int maxPlayers)
{
    if (Link == PartyLink::Online) Send('M', { (u8)std::max(2, std::min(4, maxPlayers)) });
}

void Party_SendEmote(int emote)
{
    if (Link != PartyLink::Online || Me < 0) return;
    Send('W', { (u8)emote });
    Peers[Me].walk.emote = emote;
    Peers[Me].walk.emoteAt = Ms();
}

uint32_t Party_Now() { return Ms(); }

PartyCard Party_SaveCard() { return SaveCard; }

PartyCard Party_MyCard()
{
    // PlayerProfile: name, id (+0x10), money (+0x14), gender (+0x18), Johto's badges (+0x1A),
    // Kanto's (+0x1F); then the coins (u16) and the play time (hours u16, minutes, seconds).
    PartyCard c;
    if (!Nds || !ProfileAt || ProfileAt + 0x26 > Nds->MainRAMMask + 1) return c;
    const uint8_t* p = Nds->MainRAM + ProfileAt;
    c.known = true;
    c.id = RomGet32(p + 0x10);
    c.money = RomGet32(p + 0x14);
    c.gender = p[0x18] & 1;
    c.johto = p[0x1A];
    c.kanto = p[0x1F];
    c.hours = RomGet16(p + 0x22);
    c.minutes = p[0x24];
    return c;
}

void Party_SendFriend(int player, int kind)
{
    if (Link == PartyLink::Online && player >= 0 && player < 4 && player != Me) Send('Z', { (u8)player, (u8)kind });
}

void Party_SetId(const std::string& id)
{
    MyId = id;
}

void Party_SetFriends(const std::vector<std::string>& ids)
{
    FriendIds = ids;
    LastPresence = 0;
}

const std::map<std::string, PartyFriendOnline>& Party_FriendsOnline()
{
    return FriendsOn;
}

void Party_InviteFriend(const std::string& id)
{
    if (Link == PartyLink::Online && id.size() == 16) Send('G', std::vector<u8>(id.begin(), id.end()));
}

void Party_SetBattle(const PartyBattle& b)
{
    if (Link != PartyLink::Online || Me < 0) return;
    const std::vector<u8> m = { (u8)((b.choosing ? 1 : 0) | (b.link ? 2 : 0)), (u8)std::min(255, b.turn), (u8)b.species, (u8)(b.species >> 8),
        (u8)b.hp, (u8)b.foeSpecies, (u8)(b.foeSpecies >> 8), (u8)b.foeHp };
    const u32 now = Ms();
    if (m == SentBattle && now - LastBattle < 2000) return;
    SentBattle = m;
    LastBattle = now;
    Send('T', m);
}

void Party_SendInvite(int player, int invite)
{
    if (Link == PartyLink::Online && player >= 0 && player < 4 && player != Me) Send('V', { (u8)player, (u8)invite });
}

// The Union Room hand-off (party.h). A Pokemon Center's counter runs ScrCmd_286, which calls
// sub_02054030 (pokeheartgold field_warp_tasks.c): it records where the player stands as the
// way back, starts the Union Room's wireless and warps there, as a step of the script's
// field task. With no task running (the player idle), a task that only returns TRUE is made
// for it to be a step of; FLAG_UNK_966 is set as the counter sets it, so that a save made in
// the Union Room (a trade saves) continues at the way back.
namespace
{
constexpr u32 CreateTask = 0x020504F0;      // FieldSystem_CreateTask
constexpr u32 UnionRoomWarp = 0x02054030;   // sub_02054030
constexpr u32 ReturnTrue = 0x0203E3A8;      // a function that returns TRUE
constexpr u32 Flag966 = 0x966;

// The byte of a script flag: the save data in memory (FieldSystem +0x0C, its dynamic region
// from +0x10) holds the general block as the save file does, with the flags at 0x10C4.
u32 FlagByte(u32 fs, u32 flag) { const u32 save = R32(fs + 0x0C); return save ? save + 0x10 + 0x10C4 + flag / 8 : 0; }
}

bool Party_GoToUnionRoom()
{
    const std::pair<u32, u32> code[] = { { CreateTask, 0x1C05B570 }, { UnionRoomWarp, 0x1C06B5F8 }, { ReturnTrue, 0x47702001 } };
    for (const auto& [addr, word] : code)
        if (R32(addr) != word) return false;
    const u32 fs = Nds ? R32(FieldSystemPtr) : 0;
    if (!Hooked || !fs || R32(fs + 0x10)) return false;
    // Both just before they run: no task may have started since (a task already there would be lost).
    Queue(CreateTask | 1, fs, ReturnTrue | 1, 0, 0).ready = [fs](u32*) {
        if (R32(FieldSystemPtr) != fs || R32(fs + 0x10)) return false;
        if (const u32 b = FlagByte(fs, Flag966))
        {
            fprintf(stderr, "[party] to the Union Room: flag byte %02X -> %02X\n", R8(b), R8(b) | 1 << (Flag966 % 8));
            W8(b, R8(b) | 1 << (Flag966 % 8));
        }
        W32(fs + 0x1C, 0); // as ScrCmd_815 0 at the counter
        return true;
    };
    Queue(UnionRoomWarp | 1).ready = [fs](u32* args) {
        const u32 task = R32(fs + 0x10);
        if (!task || (R32(task + 4) & ~1u) != ReturnTrue) return false;
        args[0] = task;
        return true;
    };
    return true;
}

void Party_LeftUnionRoom()
{
    // As the counter's welcome back (std_union_room_return) does.
    const u32 fs = Nds ? R32(FieldSystemPtr) : 0;
    if (const u32 b = fs ? FlagByte(fs, Flag966) : 0)
    {
        fprintf(stderr, "[party] back from the Union Room: flag byte %02X -> %02X\n", R8(b), R8(b) & ~(1u << (Flag966 % 8)));
        W8(b, R8(b) & ~(1u << (Flag966 % 8)));
    }
}

std::string Party_PlaceName(int map)
{
    // The map header's place (mapsec, the low byte at 0x12) names it in text bank 279.
    if (map < 0 || map >= 540 || !Nds) return "";
    static std::map<int, std::string> names;
    auto it = names.find(map);
    if (it != names.end()) return it->second;
    if (map == 516) return names[map] = "Wi-Fi Plaza"; // its place is "Mystery Zone" (see plazamap.h)
    return names[map] = NdsMessage(RomData, RomSize, 279, R16(MapHeaders + 24 * map + 0x12) & 0xFF);
}

int Party_MapSec(int map)
{
    if (map < 0 || map >= 540 || !Nds) return -1;
    return (int)(R16(MapHeaders + 24 * map + 0x12) & 0xFF);
}

static const uint8_t* RomMember(const char* narc, int i, uint32_t need);
bool Party_SpeciesTypes(int species, int types[2])
{
    const u8* ps = species > 0 && species <= 493 ? RomMember("a/0/0/2", species, 0x14) : nullptr;
    if (!ps) return false;
    types[0] = ps[6];
    types[1] = ps[7];
    return true;
}

void Party_SetPartner(int species)
{
    MyPartner = species;
    if (Link == PartyLink::Online) SendHello();
}

void Party_SetLook(int member)
{
    MyLook = member;
}

int Party_DexCount()
{
    if (!DexKnown) return -1;
    int n = 0;
    for (int i = 0; i < 64; i++) n += __builtin_popcount(DexCaught[i]);
    return n;
}

std::vector<int> Party_MySpecies()
{
    if (!Nds || !ProfileAt || ProfileAt - 0x64 + 0x90 + 8 + 6 * 236 > Nds->MainRAMMask + 1) return {};
    return SpeciesOfParty(Nds->MainRAM + ProfileAt - 0x64);
}


PartyLink Party_Link() { return Link; }
int Party_Me() { return Me; }
int Party_Max() { return MaxPlayers; }
std::string Party_Code() { return Code; }
int Party_RttMs() { return Rtt; }
float Party_LostSeconds() { return Link == PartyLink::Lost ? (Ms() - LostAt) / 1000.0f : 0; }
const PartyPeer* Party_Peers() { return Peers; }
int Party_Count()
{
    int n = 0;
    for (const PartyPeer& q : Peers) n += q.present;
    return n;
}
bool Party_PopEvent(PartyEvent& e)
{
    if (Events.empty()) return false;
    e = Events.front();
    Events.pop_front();
    return true;
}
int Party_MyBadges() { return std::max(0, Badges); }
int Party_CaughtThisSession() { return CaughtSession; }
uint32_t Party_SessionSeconds() { return Link == PartyLink::Off || !SessionStart ? 0 : (Ms() - SessionStart) / 1000; }

void Party_SetRom(const uint8_t* rom, size_t size)
{
    RomData = rom;
    RomSize = size;
}

std::string Party_SpeciesName(int species)
{
    // Species names are text bank 237; the game writes them in capitals ("HOOTHOOT").
    std::string s = NdsMessage(RomData, RomSize, 237, species);
    bool first = true;
    for (char& ch : s)
    {
        if (ch >= 'A' && ch <= 'Z' && !first) ch = (char)(ch - 'A' + 'a');
        first = !((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'));
    }
    return s.empty() ? "a Pok\xC3\xA9mon" : s;
}

std::string Party_Text(int bank, int index)
{
    static std::map<std::pair<int, int>, std::string> cache;
    auto it = cache.find({ bank, index });
    if (it != cache.end()) return it->second;
    return cache[{ bank, index }] = NdsMessage(RomData, RomSize, bank, index);
}

// Member `i` of a NARC of the ROM, if it has at least `need` bytes.
static const uint8_t* RomMember(const char* narc, int i, uint32_t need)
{
    static std::map<std::string, std::vector<std::pair<uint32_t, uint32_t>>> files;
    auto it = files.find(narc);
    if (it == files.end()) it = files.emplace(narc, NdsNarcFiles(RomData, RomSize, narc)).first;
    if (i < 0 || i >= (int)it->second.size() || it->second[i].second < need) return nullptr;
    return RomData + it->second[i].first;
}

bool Party_ReadMon(uint32_t addr, PartyMon& mon, bool boxed)
{
    // A BoxPokemon (personality, flags, checksum, four 32-byte blocks shuffled by the
    // personality and encrypted with the checksum as the seed), then the party's own part
    // (status, level, HP and stats: 100 bytes encrypted with the personality as the seed).
    // The flags say when a part is decrypted in place, as the game does while it reads one.
    if (!Nds || !RomData || (addr >> 24) != 0x02) return false;
    std::vector<u8> m = ReadBytes(addr, boxed ? 136 : 236);
    const u32 pid = RomGet32(&m[0]), flags = RomGet16(&m[4]), check = RomGet16(&m[6]);
    if (flags & 4) return false; // the game found its checksum wrong: a Bad Egg
    auto crypt = [&](u32 from, u32 words, u32 seed) {
        for (u32 w = 0; w < words; w++)
        {
            seed = seed * 0x41C64E6D + 0x6073;
            const u32 v = RomGet16(&m[from + 2 * w]) ^ (seed >> 16);
            m[from + 2 * w] = (u8)v;
            m[from + 2 * w + 1] = (u8)(v >> 8);
        }
    };
    if (!(flags & 2)) crypt(8, 64, check);
    if (!(flags & 1) && !boxed) crypt(0x88, 50, pid);
    u32 sum = 0;
    for (u32 w = 0; w < 64; w++) sum += RomGet16(&m[8 + 2 * w]);
    if ((sum & 0xFFFF) != check) return false;
    static const char* orders[24] = { "ABCD", "ABDC", "ACBD", "ACDB", "ADBC", "ADCB", "BACD", "BADC", "BCAD", "BCDA", "BDAC", "BDCA",
        "CABD", "CADB", "CBAD", "CBDA", "CDAB", "CDBA", "DABC", "DACB", "DBAC", "DBCA", "DCAB", "DCBA" };
    const char* order = orders[((pid >> 13) & 31) % 24];
    const u8* blk[4];
    for (int k = 0; k < 4; k++) blk[order[k] - 'A'] = &m[8 + 32 * k];
    const u8 *a = blk[0], *b = blk[1], *c = blk[2], *d = blk[3];
    mon = PartyMon();
    mon.species = RomGet16(a);
    if (mon.species <= 0 || mon.species > 493) return false;
    mon.item = RomGet16(a + 2);
    mon.otId = RomGet32(a + 4);
    mon.exp = RomGet32(a + 8);
    mon.ability = a[0x0D];
    mon.friendship = a[0x0C];
    mon.nature = (int)(pid % 25);
    const u32 ivs = RomGet32(b + 0x10);
    mon.egg = ivs >> 30 & 1;
    mon.gender = b[0x18] >> 1 & 3;
    mon.form = b[0x18] >> 3;
    for (int i = 0; i < 4; i++)
    {
        mon.moves[i] = RomGet16(b + 2 * i);
        mon.pp[i] = b[8 + i];
        if (mon.moves[i] <= 0 || mon.moves[i] > 467) { mon.moves[i] = 0; continue; }
        // Move data (a/0/1/1): the type at +4, the PP at +6; each PP Up adds a fifth.
        if (const uint8_t* mv = RomMember("a/0/1/1", mon.moves[i], 8))
        {
            mon.moveType[i] = mv[4];
            mon.ppMax[i] = mv[6] * (5 + (b[0x0C + i] & 3)) / 5;
        }
    }
    auto text = [](const u8* s, int n) {
        uint16_t codes[16];
        for (int i = 0; i < n; i++) codes[i] = (uint16_t)RomGet16(s + 2 * i);
        return NdsText(codes, (size_t)n);
    };
    mon.name = ivs >> 31 ? text(c, 11) : Party_SpeciesName(mon.species);
    mon.otName = text(d, 8);
    // The place met: Diamond and Pearl's field, or for places they lack ("faraway place",
    // 3002) the one Platinum and HeartGold added.
    mon.metPlace = RomGet16(d + 0x18);
    if (mon.metPlace == 3002 && RomGet16(b + 0x1E)) mon.metPlace = RomGet16(b + 0x1E);
    mon.metLevel = d[0x1C] & 0x7F;
    mon.metMonth = d[0x14] <= 12 ? d[0x14] : 0;
    mon.metDay = d[0x15] <= 31 ? d[0x15] : 0;
    mon.ribbons = __builtin_popcount(RomGet32(a + 0x1C)) + __builtin_popcount(RomGet32(b + 0x14)) +
        __builtin_popcount(RomGet32(c + 0x18)) + __builtin_popcount(RomGet32(c + 0x1C));
    if (!boxed)
    {
        const u8* p = &m[0x88];
        mon.status = RomGet32(p);
        mon.level = p[4];
        mon.hp = RomGet16(p + 6);
        for (int i = 0; i < 6; i++) mon.stats[i] = RomGet16(p + 8 + 2 * i);
        if (mon.level < 1 || mon.level > 100 || mon.stats[0] <= 0 || mon.hp > mon.stats[0]) return false;
    }
    // The species' data (a/0/0/2): its types at +6 (forms with types of their own have
    // entries elsewhere: left unknown), its growth rate at +0x13, whose table (a/0/0/3)
    // gives the experience each level starts at.
    if (const uint8_t* jd = RomMember("a/1/3/8", 0, 2 * 494)) mon.johto = RomGet16(jd + 2 * mon.species);
    if (const uint8_t* ps = RomMember("a/0/0/2", mon.species, 0x14))
    {
        // Wormadam, Rotom, Shaymin and Arceus have types of their own in their other forms.
        if (mon.form == 0 || (mon.species != 413 && mon.species != 479 && mon.species != 492 && mon.species != 493))
        {
            mon.types[0] = ps[6];
            mon.types[1] = ps[7];
        }
        if (const uint8_t* g = RomMember("a/0/0/3", ps[0x13], 101 * 4))
        {
            if (boxed)
            {
                mon.level = 1;
                while (mon.level < 100 && RomGet32(g + 4 * (mon.level + 1)) <= mon.exp) mon.level++;
            }
            mon.expLevel = RomGet32(g + 4 * mon.level);
            mon.expNext = mon.level < 100 ? RomGet32(g + 4 * (mon.level + 1)) : 0;
        }
    }
    return mon.level >= 1;
}

void Party_SetSave(const uint8_t* sav, size_t size)
{
    // The general block of the newer of the save's two copies: the trainer's name and ID
    // (PlayerProfile at 0x64), and where the Pokedex (magic 0xBEEFCAFE) is from there.
    const u32 block = 0xF628;
    u32 best = ~0u, bestCount = 0;
    for (u32 base : { 0u, 0x40000u })
    {
        if (base + block > size || RomGet32(sav + base + block - 8) != 0x20060623) continue;
        const u32 count = RomGet32(sav + base + block - 16);
        if (best == ~0u || count > bestCount) { best = base; bestCount = count; }
    }
    HaveTrainer = false;
    if (best == ~0u) return;
    for (u32 o = 0; o + 4 <= block - 16; o += 4)
        if (RomGet32(sav + best + o) == 0xBEEFCAFE)
        {
            memcpy(TrainerName, sav + best + 0x64, 16);
            TrainerId = RomGet32(sav + best + 0x74);
            DexDelta = o - 0x64;
            HaveTrainer = true;
            // The card, read the same way as from memory (see Party_MyCard).
            const uint8_t* p = sav + best + 0x64;
            SaveCard = PartyCard();
            SaveCard.known = true;
            SaveCard.id = RomGet32(p + 0x10);
            SaveCard.money = RomGet32(p + 0x14);
            SaveCard.gender = p[0x18] & 1;
            SaveCard.johto = p[0x1A];
            SaveCard.kanto = p[0x1F];
            SaveCard.hours = RomGet16(p + 0x22);
            SaveCard.minutes = p[0x24];
            SaveCard.dex = 0;
            for (int i = 0; i < 64; i++) SaveCard.dex += __builtin_popcount(sav[best + o + 4 + i]);
            SaveCard.party = SpeciesOfParty(sav + best);
            return;
        }
}

