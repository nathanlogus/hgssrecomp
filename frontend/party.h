#pragma once
// Parties: up to four games share one "local wireless" space through a relay server
// (tools/party_server.py), so the Union Room, link battles and trades work between
// players far apart. Each game keeps its own save. See party.cpp.
#include <string>

// Joins the party `code` on `server` ("host" or "host:port"). Returns the player number
// (0-3), which also gives this console its own wireless address, or -1 on failure.
int Party_Join(const std::string& server, const std::string& code);
// Called once per frame: keeps the place in the party and prints statistics.
void Party_Process();
void Party_Leave();
