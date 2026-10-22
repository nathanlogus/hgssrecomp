#pragma once
// Signing in with Discord through the account server (server/account_server.py), for
// relays that ask for an account. A device flow, so it works from a controller: the game
// shows a short code and opens the browser, which shows the same code and a Discord button;
// the game polls until the browser side is done. See accounts.cpp.
#include <string>

enum class AccountState { SignedOut, Starting, Waiting, SignedIn, Failed };

// The account server's address, e.g. https://accounts.example.org (empty: no accounts).
void Accounts_SetServer(const std::string& url);
// The saved sign-in (from the settings).
void Accounts_Restore(const std::string& token, const std::string& name);
// Opens the browser to sign in; the code to compare is Accounts_Code(). Returns at once.
void Accounts_StartSignIn();
void Accounts_SignOut();
// Once per frame: asks the server whether the browser side is done.
void Accounts_Process();

AccountState Accounts_State();
std::string Accounts_Code();      // the code to compare, while Waiting
std::string Accounts_Message();   // why it failed
std::string Accounts_Name();      // the account's display name, while SignedIn (or restored)
std::string Accounts_Token();     // what the relay is sent; empty if not signed in
// True once after the token or name changed (sign-in or out): the settings need saving.
bool Accounts_TakeChanged();
// Bumps whenever the state, code or name changes, so the menu knows to redraw.
unsigned Accounts_Version();
