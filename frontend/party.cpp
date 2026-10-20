// Parties: the DS's local wireless traffic of up to four games, passed through a relay
// server (tools/party_server.py). This follows melonDS's LAN mode (melonDS/src/net/LAN.cpp)
// step by step, with plain UDP instead of ENet so no new library is needed, and the
// relay instead of direct connections so players behind home routers can meet.
//
// The console waits for replies while it runs: on real hardware they come back within
// microseconds, so every round trip to another player holds the game up. The [party]
// lines on stderr show how long the game waited, which is what lag costs.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

#include "Platform.h"
#include "net/MPInterface.h"
#include "party.h"

using melonDS::MPPacketHeader;
using melonDS::u8;
using melonDS::u16;
using melonDS::u32;
using melonDS::u64;

namespace
{
#ifdef _WIN32
using Socket = SOCKET;
const Socket NoSocket = INVALID_SOCKET;
#else
using Socket = int;
const Socket NoSocket = -1;
#endif

constexpr u32 FrameMagic = 0x4946494E;

Socket Sock = NoSocket;
int Me = -1;
u16 Connected = 0; // players whose wireless is on, one bit each; kept by the server
int LastHost = -1;
int Timeout = 25;  // ms to wait for another console, as in melonDS's LAN mode

struct Received
{
    std::vector<u8> data;
    u32 time;
};
std::deque<Received> Queue;

u32 LastKeepAlive = 0, StatsStart = 0, WaitedMs = 0, Waits = 0, Missed = 0;

u32 Ms()
{
    using namespace std::chrono;
    return (u32)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void Control(char kind, const std::string& text = "")
{
    std::string m = "SSR";
    m += kind;
    m += text;
    send(Sock, m.data(), (int)m.size(), 0);
}

u32 FrameType(const Received& r)
{
    MPPacketHeader h;
    memcpy(&h, r.data.data(), sizeof h);
    return h.Type & 0xFFFF;
}

// Reads one datagram, waiting up to `ms`. 1: a wireless frame was queued; 0: something
// else was read (a message from the server, or junk); -1: nothing arrived.
int Poll(int ms)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(Sock, &set);
    timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    if (select((int)Sock + 1, &set, nullptr, nullptr, &tv) <= 0) return -1;
    u8 buf[4096];
    const int n = recv(Sock, (char*)buf, sizeof buf, 0);
    if (n >= 6 && !memcmp(buf, "SSRM", 4))
    {
        memcpy(&Connected, buf + 4, 2);
        return 0;
    }
    if (n < (int)sizeof(MPPacketHeader)) return 0;
    MPPacketHeader h;
    memcpy(&h, buf, sizeof h);
    if (h.Magic != FrameMagic || h.SenderID > 15 || (int)h.SenderID == Me || h.Length > n - sizeof h) return 0;
    Queue.push_back({ std::vector<u8>(buf, buf + n), Ms() });
    return 1;
}

// LAN::ProcessLAN. type 0: take in what has arrived; 1: the core looks for a plain
// frame; 2: the core waits for a frame from another console.
void Pump(int type)
{
    const u32 start = Ms();
    while (!Queue.empty())
    {
        // The core takes frames within a frame's time; older ones are stale.
        if (start - Queue.front().time > 16)
        {
            Queue.pop_front();
            continue;
        }
        if (type == 2) return;
        if (type == 1)
        {
            if (FrameType(Queue.front()) == 0) return;
            Queue.pop_front();
        }
        break;
    }
    for (;;)
    {
        const int left = type == 2 ? Timeout - (int)(Ms() - start) : 0;
        const int r = left < 0 ? -1 : Poll(left);
        if (r != 0) break;
    }
    if (type == 2)
    {
        WaitedMs += Ms() - start;
        Waits++;
        if (Queue.empty()) Missed++;
    }
}

int SendFrame(u32 type, u8* data, int len, u64 timestamp)
{
    if (Sock == NoSocket || len > 2048) return 0;
    std::vector<u8> buf(sizeof(MPPacketHeader) + len);
    const MPPacketHeader h = { FrameMagic, (u32)Me, type, (u32)len, timestamp };
    memcpy(buf.data(), &h, sizeof h);
    if (len) memcpy(buf.data() + sizeof h, data, len);
    send(Sock, (const char*)buf.data(), (int)buf.size(), 0);
    return len;
}

int RecvFrame(u8* data, bool block, u64* timestamp)
{
    if (Sock == NoSocket) return 0;
    Pump(block ? 2 : 1);
    if (Queue.empty()) return 0;
    const Received r = std::move(Queue.front());
    Queue.pop_front();
    MPPacketHeader h;
    memcpy(&h, r.data.data(), sizeof h);
    const u32 len = h.Length > 2048 ? 2048 : h.Length;
    if (len)
    {
        memcpy(data, r.data.data() + sizeof h, len);
        if (h.Type == 1) LastHost = (int)h.SenderID;
    }
    if (timestamp) *timestamp = h.Timestamp;
    return (int)len;
}
}

int Party_Join(const std::string& server, const std::string& code)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
#endif
    const size_t colon = server.rfind(':');
    const std::string host = colon == std::string::npos ? server : server.substr(0, colon);
    const std::string port = colon == std::string::npos ? "7373" : server.substr(colon + 1);
    addrinfo hints = {}, *res = nullptr;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
    {
        fprintf(stderr, "[party] cannot find the server %s\n", server.c_str());
        return -1;
    }
    Sock = socket(res->ai_family, SOCK_DGRAM, IPPROTO_UDP);
    const bool ok = Sock != NoSocket && connect(Sock, res->ai_addr, (int)res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!ok)
    {
        fprintf(stderr, "[party] cannot open a connection to %s\n", server.c_str());
        Party_Leave();
        return -1;
    }
    if (const char* t = getenv("SSR_PARTY_TIMEOUT")) Timeout = atoi(t);

    // The server answers with our player number ("SSRI" n) or that the party is full.
    for (int attempt = 0; attempt < 6 && Me < 0; attempt++)
    {
        Control('J', code);
        const u32 start = Ms();
        while (Me < 0 && Ms() - start < 500)
        {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(Sock, &set);
            timeval tv = { 0, 100000 };
            if (select((int)Sock + 1, &set, nullptr, nullptr, &tv) <= 0) continue;
            char buf[16];
            const int n = recv(Sock, buf, sizeof buf, 0);
            if (n >= 5 && !memcmp(buf, "SSRI", 4)) Me = buf[4];
            else if (n >= 4 && !memcmp(buf, "SSRF", 4))
            {
                fprintf(stderr, "[party] the party \"%s\" is full\n", code.c_str());
                Party_Leave();
                return -1;
            }
        }
    }
    if (Me < 0)
    {
        fprintf(stderr, "[party] no answer from %s\n", server.c_str());
        Party_Leave();
        return -1;
    }
    fprintf(stderr, "[party] joined \"%s\" on %s as player %d (waits up to %d ms for replies)\n",
        code.c_str(), server.c_str(), Me + 1, Timeout);
    StatsStart = LastKeepAlive = Ms();
    return Me;
}

void Party_Process()
{
    if (Sock == NoSocket) return;
    Pump(0);
    const u32 now = Ms();
    if (now - LastKeepAlive >= 1000)
    {
        Control('K');
        LastKeepAlive = now;
    }
    if (now - StatsStart >= 5000)
    {
        if (Waits)
            fprintf(stderr, "[party] players on wireless %x: waited %u ms in 5 s for other consoles (%u waits, %u with no answer)\n",
                Connected, WaitedMs, Waits, Missed);
        StatsStart = now;
        WaitedMs = Waits = Missed = 0;
    }
}

void Party_Leave()
{
    if (Sock == NoSocket) return;
    if (Me >= 0) Control('L');
#ifdef _WIN32
    closesocket(Sock);
#else
    close(Sock);
#endif
    Sock = NoSocket;
    Me = -1;
}

// melonDS's multiplayer interface, as LAN.cpp implements it.
namespace melonDS::Platform
{

void MP_Begin(void*)
{
    if (Sock == NoSocket) return;
    Connected |= 1 << Me;
    LastHost = -1;
    Control('B');
}

void MP_End(void*)
{
    if (Sock == NoSocket) return;
    Connected &= ~(1 << Me);
    Control('E');
}

int MP_SendPacket(u8* data, int len, u64 timestamp, void*) { return SendFrame(0, data, len, timestamp); }
int MP_RecvPacket(u8* data, u64* timestamp, void*) { return RecvFrame(data, false, timestamp); }
int MP_SendCmd(u8* data, int len, u64 timestamp, void*) { return SendFrame(1, data, len, timestamp); }
int MP_SendReply(u8* data, int len, u64 timestamp, u16 aid, void*) { return SendFrame(2 | (aid << 16), data, len, timestamp); }
int MP_SendAck(u8* data, int len, u64 timestamp, void*) { return SendFrame(3, data, len, timestamp); }

int MP_RecvHostPacket(u8* data, u64* timestamp, void*)
{
    if (LastHost != -1 && !(Connected & (1 << LastHost))) return -1; // the host has left
    return RecvFrame(data, true, timestamp);
}

u16 MP_RecvReplies(u8* data, u64 timestamp, u16 aidmask, void*)
{
    if (Sock == NoSocket) return 0;
    u16 ret = 0, seen = 1 << Me;
    if ((seen & Connected) == Connected) return 0;
    for (;;)
    {
        Pump(2);
        if (Queue.empty()) return ret; // no more replies in time
        const Received r = std::move(Queue.front());
        Queue.pop_front();
        MPPacketHeader h;
        memcpy(&h, r.data.data(), sizeof h);
        if ((h.Type & 0xFFFF) != 2 || h.Timestamp < timestamp - 32) continue;
        if (h.Length)
        {
            const u32 aid = h.Type >> 16, len = h.Length > 1024 ? 1024 : h.Length;
            if (aid < 1 || aid > 15) continue;
            memcpy(&data[(aid - 1) * 1024], r.data.data() + sizeof h, len);
            ret |= 1 << aid;
        }
        seen |= 1 << h.SenderID;
        if ((seen & Connected) == Connected || (ret & aidmask) == aidmask) return ret;
    }
}

}
