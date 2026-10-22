// Signing in with Discord through the account server (see accounts.h).
//
// The game has no TLS library, and account servers should use https, so a request is made
// by what the system has: WinHTTP on Windows, the curl program elsewhere. Each request runs
// on its own thread; Accounts_Process (the main thread) starts them and reads their answers.
#include "accounts.h"

#include <SDL.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace
{
struct Reply
{
    int status = 0; // 0: could not connect
    std::string body;
};

#ifdef _WIN32
Reply Http(const std::string& method, const std::string& url, const std::string& body)
{
    Reply out;
    const std::wstring w(url.begin(), url.end());
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[1024] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1023;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) return out;
    HINTERNET s = WinHttpOpen(L"JohtoOnline", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!s) return out;
    WinHttpSetTimeouts(s, 10000, 10000, 10000, 10000);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    const std::wstring wm(method.begin(), method.end());
    HINTERNET r = c ? WinHttpOpenRequest(c, wm.c_str(), path, nullptr, nullptr, nullptr,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r && WinHttpSendRequest(r, L"Content-Type: application/json\r\n", (DWORD)-1L, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)
        && WinHttpReceiveResponse(r, nullptr))
    {
        DWORD code = 0, len = sizeof code;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &len, nullptr);
        out.status = (int)code;
        DWORD n = 0;
        char buf[4096];
        while (out.body.size() < (1 << 20) && WinHttpReadData(r, buf, sizeof buf, &n) && n) out.body.append(buf, n);
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return out;
}
#elif defined(__ANDROID__)
Reply Http(const std::string&, const std::string&, const std::string&) { return {}; } // no curl there
#else
bool Plain(const std::string& s, const char* extra)
{
    for (char ch : s)
        if (!isalnum((unsigned char)ch) && !strchr(extra, ch)) return false;
    return true;
}

Reply Http(const std::string& method, const std::string& url, const std::string& body)
{
    Reply out;
    // The address and body go into a command line: only characters that need no quoting.
    if (!Plain(url, ":/._~?=&%-") || !Plain(body.empty() ? "" : body, "{}\":,_- ")) return out;
    std::string cmd = "curl -sS -m 10 -w '\\n%{http_code}' -X " + method + " -H 'Content-Type: application/json'";
    if (!body.empty()) cmd += " --data '" + body + "'";
    cmd += " '" + url + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t n;
    while (out.body.size() < (1 << 20) && (n = fread(buf, 1, sizeof buf, p)) > 0) out.body.append(buf, n);
    pclose(p);
    const size_t nl = out.body.rfind('\n');
    if (nl != std::string::npos)
    {
        out.status = atoi(out.body.c_str() + nl + 1);
        out.body.resize(nl);
    }
    return out;
}
#endif

// The string value of "key" in a flat JSON object ("" if absent), \uXXXX escapes decoded.
std::string Field(const std::string& json, const std::string& key)
{
    size_t at = json.find("\"" + key + "\"");
    if (at == std::string::npos) return "";
    at = json.find('"', json.find(':', at));
    if (at == std::string::npos) return "";
    std::string out;
    for (size_t i = at + 1; i < json.size() && json[i] != '"'; i++)
    {
        if (json[i] != '\\' || i + 1 >= json.size()) { out += json[i]; continue; }
        const char e = json[++i];
        if (e == 'u' && i + 4 < json.size())
        {
            unsigned cp = (unsigned)strtoul(json.substr(i + 1, 4).c_str(), nullptr, 16);
            i += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && json.compare(i + 1, 2, "\\u") == 0)
            {
                const unsigned lo = (unsigned)strtoul(json.substr(i + 3, 4).c_str(), nullptr, 16);
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i += 6;
            }
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) out += (char)(0xC0 | cp >> 6), out += (char)(0x80 | (cp & 63));
            else if (cp < 0x10000) out += (char)(0xE0 | cp >> 12), out += (char)(0x80 | (cp >> 6 & 63)), out += (char)(0x80 | (cp & 63));
            else out += (char)(0xF0 | cp >> 18), out += (char)(0x80 | (cp >> 12 & 63)), out += (char)(0x80 | (cp >> 6 & 63)), out += (char)(0x80 | (cp & 63));
        }
        else out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
    }
    return out;
}

std::string Server, Token, Name, Code, Message, DeviceCode, LoginUrl;
AccountState State = AccountState::SignedOut;
unsigned Version = 0;
bool Changed = false;
uint32_t NextPoll = 0, Deadline = 0;
int Interval = 3000;

// The request in flight, if any: its reply is read by Accounts_Process.
struct Pending
{
    std::atomic<bool> done{false};
    Reply reply;
};
std::shared_ptr<Pending> Flight;

void Launch(const std::string& method, const std::string& path, const std::string& body)
{
    auto f = Flight = std::make_shared<Pending>();
    const std::string url = Server + path;
    std::thread([f, method, url, body] { f->reply = Http(method, url, body); f->done = true; }).detach();
}

void Set(AccountState s, const std::string& message = "")
{
    State = s;
    Message = message;
    Version++;
}

void Fail(const std::string& why)
{
    fprintf(stderr, "account: %s\n", why.c_str());
    Set(AccountState::Failed, why);
}
}

void Accounts_SetServer(const std::string& url)
{
    Server = url;
    while (!Server.empty() && Server.back() == '/') Server.pop_back();
    if (!Server.empty() && Server.find("://") == std::string::npos) Server = "https://" + Server;
}

void Accounts_Restore(const std::string& token, const std::string& name)
{
    Token = token;
    Name = name;
    State = Token.empty() ? AccountState::SignedOut : AccountState::SignedIn;
    Version++;
}

void Accounts_StartSignIn()
{
    if (Server.empty()) return Fail("Set the account server first.");
    if (State == AccountState::Starting || State == AccountState::Waiting) return;
    Set(AccountState::Starting);
    Launch("POST", "/device/start", "");
}

void Accounts_SignOut()
{
    Flight.reset();
    Token.clear();
    Name.clear();
    Changed = true;
    Set(AccountState::SignedOut);
}

void Accounts_Process()
{
    const uint32_t now = SDL_GetTicks();
    if (State == AccountState::Waiting && !Flight && now >= NextPoll)
    {
        if ((int32_t)(now - Deadline) > 0) return Fail("The sign-in took too long. Try again.");
        Launch("POST", "/device/poll", "{\"device_code\": \"" + DeviceCode + "\"}");
    }
    if (!Flight || !Flight->done) return;
    const Reply r = Flight->reply;
    Flight.reset();
    NextPoll = now + Interval;
    if (State == AccountState::Starting)
    {
        DeviceCode = Field(r.body, "device_code");
        Code = Field(r.body, "user_code");
        LoginUrl = Field(r.body, "login_url");
        if (r.status != 200 || DeviceCode.empty() || Code.empty() || LoginUrl.empty())
            return Fail(r.status ? "The account server did not answer as expected." : "Cannot reach the account server.");
        Interval = 3000;
        Deadline = now + 600000;
        Set(AccountState::Waiting);
        if (SDL_OpenURL(LoginUrl.c_str()) != 0) fprintf(stderr, "account: could not open the browser; go to %s\n", LoginUrl.c_str());
    }
    else if (State == AccountState::Waiting)
    {
        const std::string st = Field(r.body, "status");
        if (st == "ok" && !Field(r.body, "token").empty())
        {
            Token = Field(r.body, "token");
            Name = Field(r.body, "name");
            Changed = true;
            Set(AccountState::SignedIn);
        }
        else if (st == "denied") Fail(Field(r.body, "message").empty() ? "The sign-in was refused." : Field(r.body, "message"));
        else if (st == "expired") Fail("The sign-in ran out of time. Try again.");
        // pending, or no answer: try again at the next interval
    }
}

AccountState Accounts_State() { return State; }
std::string Accounts_Code() { return Code; }
std::string Accounts_Message() { return Message; }
std::string Accounts_Name() { return Name; }
std::string Accounts_Token() { return Token; }
unsigned Accounts_Version() { return Version; }
bool Accounts_TakeChanged()
{
    const bool c = Changed;
    Changed = false;
    return c;
}
