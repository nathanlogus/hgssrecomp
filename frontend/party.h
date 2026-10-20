#pragma once
// Parties: up to four games meet through a relay server (tools/party_server.py) and use
// the Union Room, link battles and trades with each other as if their consoles were side
// by side, and see who else is playing (the online HUD). Each game keeps its own save.
// See party.cpp.
#include <cstddef>
#include <cstdint>
#include <string>

namespace melonDS { class NDS; }

enum class PartyLink { Off, Connecting, Online, Lost };

// What this player is doing, sent to the others.
enum PartyState { PS_FIELD = 0, PS_BATTLE = 1, PS_MENU = 2, PS_LINK = 3 };

struct PartyPeer
{
    bool present = false;
    std::string name;
    int colour = 0;        // 0 blue, 1 green, 2 orange, 3 red
    bool host = false;
    int state = PS_FIELD;
    int map = -1;
    int badges = 0;
    int rtt = -1;          // their round trip to the server, ms
    uint32_t sinceMs = 0;  // when they joined (Ms clock)
};

// What happened in the party, for the HUD's toasts.
struct PartyEvent
{
    enum Kind { Joined, Left, Caught, Badge, Reconnecting, Reconnected, Failed } kind;
    int player = -1;
    int arg = 0;           // Caught: species; Badge: badges now
    std::string text;      // Failed: why
};

// Who this player is to the others: a name (up to 10 characters) and a colour (0-3, or
// -1 for one by player number).
void Party_SetProfile(const std::string& name, int colour);

// Joins the party `code` (a code like SSR-7K2Q, or any name) on `server` ("host" or
// "host:port"), or with `host` makes a new party for up to `maxPlayers` and gets its code.
// Returns at once; Party_Link() says how it goes. False if the server cannot be found.
bool Party_Join(const std::string& server, const std::string& code);
bool Party_Host(const std::string& server, int maxPlayers);
// Takes over the game's wireless code on this console once in a party.
void Party_Attach(melonDS::NDS& nds);
// Once per frame: network traffic, pings, what this player is doing.
void Party_Process(int state, int map);
void Party_Leave();
// The host changes how many players the party allows (2-4).
void Party_SetMax(int maxPlayers);

PartyLink Party_Link();
int Party_Me();                  // this player's number (0-3), or -1
int Party_Max();                 // players the party allows
std::string Party_Code();        // the party's code or name
int Party_RttMs();               // round trip to the server, ms (-1 not known yet)
float Party_LostSeconds();       // how long the connection has been lost
const PartyPeer* Party_Peers();  // 4 entries
int Party_Count();
bool Party_PopEvent(PartyEvent& e);
// For this player's own numbers: badges and Pokemon caught since joining.
int Party_MyBadges();
int Party_CaughtThisSession();
uint32_t Party_SessionSeconds();
// The name of a species (from the ROM's text), e.g. "Hoothoot".
std::string Party_SpeciesName(int species);
// The rom, for names (set once).
void Party_SetRom(const uint8_t* rom, size_t size);
// This player's save file (as loaded at start): finds their save data in memory.
void Party_SetSave(const uint8_t* sav, size_t size);
