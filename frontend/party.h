#pragma once
// Parties: up to four games meet through a relay server (tools/party_server.py) and use
// the Union Room, link battles and trades with each other as if their consoles were side
// by side. Each game keeps its own save. See party.cpp.
#include <string>

namespace melonDS { class NDS; }

// Joins the party `code` on `server` ("host" or "host:port"). Returns the player number
// (0-3), which also gives this console its own wireless address, or -1 on failure.
int Party_Join(const std::string& server, const std::string& code);
// Takes over the game's wireless code on this console (after Party_Join).
void Party_Attach(melonDS::NDS& nds);
// Called once per frame: network traffic and statistics.
void Party_Process();
void Party_Leave();
