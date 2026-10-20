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
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
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

// A player's wireless address: the firmware's, with the player number added (main.cpp).
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
                fprintf(stderr, "[party] lost the connection to the server\n");
                Party_Leave();
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

void Queue(u32 fn, u32 a0 = 0, u32 a1 = 0, u32 a2 = 0, u32 a3 = 0, std::function<void()> prepare = nullptr)
{
    if (fn) Calls.push_back({ fn, { a0, a1, a2, a3 }, std::move(prepare) });
}

void Connect(int parent)
{
    ParentPlayer = parent;
    SetState(StateBusy);
    Send('C', { (u8)parent, (u8)CommType() });
    if (Log) fprintf(stderr, "[party] asking player %d to join (link %u)\n", parent + 1, CommType());
}

void Message(char kind, const u8* p, size_t n)
{
    switch (kind)
    {
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
    for (;;)
    {
        u8 buf[4096];
        const int n = recv(Sock, (char*)buf, sizeof buf, 0);
        if (n > 0)
        {
            In.insert(In.end(), buf, buf + n);
            continue;
        }
#ifdef _WIN32
        const bool wouldBlock = n < 0 && WSAGetLastError() == WSAEWOULDBLOCK;
#else
        const bool wouldBlock = n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
#endif
        if (!wouldBlock)
        {
            fprintf(stderr, "[party] the server closed the connection\n");
            Party_Leave();
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

int Party_Join(const std::string& server, const std::string& code)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
#endif
    Log = getenv("SSR_PARTY_LOG") != nullptr;
    const size_t colon = server.rfind(':');
    const std::string host = colon == std::string::npos ? server : server.substr(0, colon);
    const std::string port = colon == std::string::npos ? "7373" : server.substr(colon + 1);
    addrinfo hints = {}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
    {
        fprintf(stderr, "[party] cannot find the server %s\n", server.c_str());
        return -1;
    }
    Sock = socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP);
    const bool ok = Sock != NoSocket && connect(Sock, res->ai_addr, (int)res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!ok)
    {
        fprintf(stderr, "[party] cannot connect to %s\n", server.c_str());
        Party_Leave();
        return -1;
    }
    int one = 1;
    setsockopt(Sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);

    // The server answers with our player number ("I" n) or that the party is full ("F").
    Send('J', std::vector<u8>(code.begin(), code.end()));
    for (u32 start = Ms(); Me < 0 && Ms() - start < 5000;)
    {
        u8 buf[8];
        fd_set set;
        FD_ZERO(&set);
        FD_SET(Sock, &set);
        timeval tv = { 0, 100000 };
        if (select((int)Sock + 1, &set, nullptr, nullptr, &tv) <= 0) continue;
        const int n = recv(Sock, (char*)buf, 4, 0);
        if (n >= 3 && buf[2] == 'F')
        {
            fprintf(stderr, "[party] the party \"%s\" is full\n", code.c_str());
            Party_Leave();
            return -1;
        }
        if (n == 4 && buf[2] == 'I') Me = buf[3];
        else break;
    }
    if (Me < 0)
    {
        fprintf(stderr, "[party] no answer from %s\n", server.c_str());
        Party_Leave();
        return -1;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(Sock, FIONBIO, &nb);
#else
    fcntl(Sock, F_SETFL, fcntl(Sock, F_GETFL) | O_NONBLOCK);
#endif
    fprintf(stderr, "[party] joined \"%s\" on %s as player %d\n", code.c_str(), server.c_str(), Me + 1);
    return Me;
}

void Party_Attach(NDS& nds)
{
    Nds = &nds;
}

void Party_Process()
{
    if (Sock == NoSocket) return;
    TryHook();
}

void Party_Leave()
{
    if (Sock == NoSocket) return;
    if (Me >= 0) Leave();
#ifdef _WIN32
    closesocket(Sock);
#else
    close(Sock);
#endif
    Sock = NoSocket;
    Me = -1;
    ssr_HleHook = nullptr;
    Hooked = false;
}
