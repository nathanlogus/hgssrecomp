#pragma once
// Parties: up to four games meet through a relay server (tools/party_server.py) and use
// the Union Room, link battles and trades with each other as if their consoles were side
// by side, and see who else is playing (the online HUD). Each game keeps its own save.
// See party.cpp.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace melonDS { class NDS; }

enum class PartyLink { Off, Connecting, Online, Lost };

// What this player is doing, sent to the others.
enum PartyState { PS_FIELD = 0, PS_BATTLE = 1, PS_MENU = 2, PS_LINK = 3 };

// Where a player's trainer is on the overworld, sent about ten times a second.
struct PartyWalker
{
    bool shown = false;    // on the overworld and visible
    int matrix = -1;       // the map matrix (0 is the whole outdoors) and the map
    int map = -1;
    int32_t x = 0, y = 0, z = 0; // world position (fx32: a tile is 16 << 12)
    int facing = 1;        // 0 north, 1 south, 2 west, 3 east
    int sprite = 0;        // the game's sprite id (the boy, the girl, on a bike...)
    uint32_t at = 0;       // when it was heard (Ms clock)
    int emote = -1;        // the emote shown (EmoteNames index) and since when
    uint32_t emoteAt = 0;
    std::string say;       // the last words (chat or a quick line) and since when
    uint32_t sayAt = 0;
};

// A battle as its player sees it, sent to the others each turn (for spectators and the
// link battle's turn timer).
struct PartyBattle
{
    bool link = false;     // a link battle (else against the game)
    bool choosing = false; // the player is choosing a command
    int turn = 0;
    int species = 0, hp = 0;       // the player's Pokemon in battle, HP in percent
    int foeSpecies = 0, foeHp = 0; // the other side's
    uint32_t at = 0;               // when it was heard (Ms clock), 0 never
    uint32_t choosingAt = 0;       // since when they have been choosing
};

struct PartyPeer
{
    bool present = false;
    std::string name;
    int colour = 0;        // 0 blue, 1 green, 2 orange, 3 red
    int partner = 0;       // the Pokemon shown with them (species, 0 none)
    bool host = false;
    int state = PS_FIELD;
    int map = -1;
    int badges = 0;
    int rtt = -1;          // their round trip to the server, ms
    uint32_t sinceMs = 0;  // when they joined (Ms clock)
    std::string version;   // their game version ("" not sent: an older game)
    int versionCmp = 0;    // -1 theirs is older than this game's, 1 newer, 0 the same or not known
    std::string uid;       // their player id (empty: not sent, an older game)
    int look = -1;         // their look: a member of the ROM's overworld sprites (-1: not known)
    PartyWalker walk;
    PartyBattle battle;
};

// What happened in the party, for the HUD's toasts.
struct PartyEvent
{
    enum Kind { Joined, Left, Caught, Badge, Reconnecting, Reconnected, Failed, Invite, Friend, SessionInvite, Version, Removed } kind;
    int player = -1;
    int arg = 0;           // Version: -1 older, 1 newer; Caught: species; Badge: badges now; Invite: PartyInvite; Friend: PartyFriend
    std::string text;      // Failed: why; SessionInvite: the session's code
    std::string from, name; // SessionInvite: the friend's player id and name
};

// This player's trainer card, from their save in memory.
struct PartyCard
{
    bool known = false;
    uint32_t id = 0, money = 0;
    int gender = 0;            // 0 boy, 1 girl
    int johto = 0, kanto = 0;  // badges, one bit each
    int hours = 0, minutes = 0;
    int dex = -1;              // Pokemon caught (Party_SaveCard only)
    std::vector<int> party;    // the party's species (Party_SaveCard only)
};
PartyCard Party_MyCard();
PartyCard Party_SaveCard();  // the same from the save file (known is false without a save)

// Invites from one player to another, and their answers.
enum PartyInvite { PI_BATTLE = 1, PI_TRADE = 2, PI_SESSION = 8 /* (a friend's, by the server: never sent as V) */, PI_YES = 0x10, PI_NO = 0x20, PI_BUSY = 0x40 }; // answers: PI_YES | PI_BATTLE...

// Friend requests to a player of the session, and their answers.
enum PartyFriend { PF_ASK = 1, PF_YES = 2, PF_NO = 3 };
void Party_SendFriend(int player, int kind);

// This player's stable id (16 letters, kept in the settings): friends know each other by it.
void Party_SetId(const std::string& id);
// This player's friends (ids): the server tells which of them are online, while they have
// this player as a friend too.
void Party_SetFriends(const std::vector<std::string>& ids);
struct PartyFriendOnline
{
    int count = 0, max = 0; // players in their session and how many it allows
    std::string code;       // its code, to join
};
const std::map<std::string, PartyFriendOnline>& Party_FriendsOnline();
// Invites a friend (online, by id) into this player's session.
void Party_InviteFriend(const std::string& id);
// This player's battle, every frame while in one (sent when it changes, and every 2 s).
void Party_SetBattle(const PartyBattle& b);

// Who this player is to the others: a name (up to 10 characters) and a colour (0-3, or
// -1 for one by player number).
void Party_SetProfile(const std::string& name, int colour);

// The party server on this computer (tools/party_server.py at `script`), started for hosting
// when the server is "localhost": in the background, without a window, until StopLocalServer
// (or the game's end). False if no Python could start it.
bool Party_StartLocalServer(const std::string& script);
void Party_StopLocalServer();
// Whether `server` names this computer ("localhost", "127.0.0.1", "::1", with or without :port).
bool Party_IsLocalServer(const std::string& server);
// This computer's address on the local network (e.g. 192.168.1.20), for friends nearby; empty
// if there is none.
std::string Party_LanAddress();

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
// The code as the screens show it: SSR-**** while hidden (for streamers), else the code.
std::string Party_CodeShown();
void Party_HideCode(bool hide);
int Party_RttMs();               // round trip to the server, ms (-1 not known yet)
float Party_LostSeconds();       // how long the connection has been lost
const PartyPeer* Party_Peers();  // 4 entries
int Party_Count();
bool Party_PopEvent(PartyEvent& e);
// For this player's own numbers: badges and Pokemon caught since joining.
int Party_MyBadges();
int Party_CaughtThisSession();
uint32_t Party_SessionSeconds();
int Party_DexCount();                // Pokemon caught in the Pokedex (-1 not known yet)
std::vector<int> Party_MySpecies();  // the species in this player's party
void Party_SetPartner(int species);  // the Pokemon shown with this player
// This player's look (a member of the overworld sprite archive, -1 none), sent with where
// their trainer is: the others draw them walking with it and show its face in their chips.
void Party_SetLook(int member);
void Party_SendEmote(int emote);     // shows an emote over this player's trainer to the others
void Party_SendInvite(int player, int invite); // a PartyInvite to one player
uint32_t Party_Now();                // the clock of PartyWalker::at (ms)
// Takes this player to the Union Room as a Pokemon Center's counter does (the way back is
// where they stand), at the game's next communication update. Only while online and with
// nothing running on the overworld; false if it cannot.
bool Party_GoToUnionRoom();
// Fly to a friend's town by the game's own Fly (with its cry and flight): `map` is the town's
// map, which must be a fly point this player has been to. Party_FlyWhy says in a sentence why
// not (empty if it can be done); Party_FlyTo starts it at the game's next communication update
// (only with nothing running on the overworld). Party_IsFlyPoint: a town, not a route, building or cave.
bool Party_IsFlyPoint(int map);
std::string Party_FlyWhy(int map);
bool Party_FlyTo(int map);
int Party_FlyMon();  // the party slot of a Pokemon that knows Fly, or -1
// Back from a Union Room visit started by Party_GoToUnionRoom: clears what it set.
void Party_LeftUnionRoom();
// Practice battles: remembers this player's party as the game holds it in memory, and puts it
// back (HP, PP, status, experience as they were) once the battle is over, as long as the same
// Pokemon are in it (a trade is a trade). Both only touch memory, never the save file.
void Party_PracticeBegin();
bool Party_PracticeEnd();  // true if it put anything back
// A map's place name ("Route 30"), from the ROM's text.
std::string Party_PlaceName(int map);
// The name of a species (from the ROM's text), e.g. "Hoothoot".
std::string Party_SpeciesName(int species);
// A Pokemon of the party as the game holds it in memory, decoded, with what the ROM says
// about its species and moves (for the redrawn party and summary screens).
struct PartyMon
{
    int species = 0, form = 0, item = 0, ability = 0, nature = 0;
    int johto = 0;                     // the number in the Johto Pokedex (0: not in it)
    int gender = 0;                    // 0 male, 1 female, 2 neither
    bool egg = false;
    std::string name, otName;          // the nickname (or the species' name), the original trainer
    uint32_t otId = 0, exp = 0, expLevel = 0, expNext = 0; // experience, and what this level and the next start at (0 at level 100)
    int level = 0, hp = 0;
    int stats[6] = {};                 // HP (most), Attack, Defense, Speed, Sp. Atk, Sp. Def
    uint32_t status = 0;               // sleep turns (bits 0-2), poison, burn, freeze, paralysis, bad poison
    int moves[4] = {}, pp[4] = {}, ppMax[4] = {}, moveType[4] = {};
    int types[2] = { -1, -1 };         // -1: not known (forms with types of their own)
    int metPlace = 0, metLevel = 0;    // metPlace: a place of text bank 279
    int metMonth = 0, metDay = 0;      // the date met (0: not known)
    int ribbons = 0;
    int friendship = 0;                // for an egg, the cycles of steps left before it hatches
    uint32_t pid = 0;                  // the personality value
    int ivs[6] = {}, evs[6] = {};      // in the order of stats[]
    bool shiny = false;                // (trainer ID ^ secret ID ^ both halves of the personality) < 8, as the game works it out
    int hiddenType = 0, hiddenPower = 0; // what Hidden Power would be: the type (the game's numbering) and its power
};
// The Pokemon (236 bytes, the party's format) at `addr` in main RAM; false unless its
// checksum and numbers check out. `boxed`: the 136 bytes a PC box keeps, without the
// party's part (no HP or stats; the level is worked out from the experience, as the game does).
bool Party_ReadMon(uint32_t addr, PartyMon& mon, bool boxed = false);
// Line `index` of the ROM's text bank `bank` (moves 750, abilities 720, items 222,
// natures 34, types 735, places 279).
std::string Party_Text(int bank, int index);
// The rom, for names (set once).
void Party_SetRom(const uint8_t* rom, size_t size);
// This player's save file (as loaded at start): finds their save data in memory.
void Party_SetSave(const uint8_t* sav, size_t size);

// Playing together. The online protocol's revision (raise it when what players send each other
// changes in a way older games cannot follow): sent to the others, who compare it with theirs
// (PartyPeer::versionCmp, and a Version event). Games before revision 2 send none.
constexpr int PartyProtocol = 2;
// The host closes the session to new players (and opens it again); everyone sees whether it is.
bool Party_Locked();
void Party_SetLocked(bool lock);
// The host removes a player from the session (the server enforces that only the host can).
void Party_Remove(int player);
// Chat: typed words (up to 60 letters), or quick line `Party_SendChat("2", true)` by its number.
// Both show over the player for a few seconds (PartyWalker::say) and go into the log.
struct PartyChatLine { int player; std::string name, text; uint32_t at; int colour; };
void Party_SendChat(const std::string& text, bool quick = false);
const std::deque<PartyChatLine>& Party_Chat();
int Party_QuickLines();
std::string Party_QuickLine(int i);  // HI!, THIS WAY!, WAIT!, THANKS!, BATTLE?, TRADE?
