// In-game bug report: the last lines of the frontend's messages, the version, the system, the
// settings (without who the player is or where the server is) and a picture of the view, written
// to a folder the player can attach to an issue. Never the save, save states, the ROM or chat.
//
// The messages are copied as they go to stderr: stderr is replaced by a pipe, and a thread
// writes each chunk on to where stderr went before and into a ring of bytes.
#pragma once
#include <SDL.h>
#ifndef SSR_VERSION
#define SSR_VERSION "" // (only some builds define it, from CHANGELOG.md)
#endif
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace BugReport
{
constexpr size_t RingSize = 1 << 18; // 256 KB, far more than 2000 lines of the usual length
inline char Ring[RingSize];
inline std::atomic<size_t> Head{ 0 }; // bytes ever written
inline int OrigFd = -1, PipeW = -1;
inline std::thread Reader;

// What the crash report needs, filled in by the frontend once it knows.
inline std::string Dir;           // the reports folder's parent (beside the save)
inline std::string Config;        // the settings file
inline std::string Renderer;      // SDL renderer and video driver
inline std::string Version;
inline std::vector<std::string> Secrets; // text to blank out of the messages

inline void Put(const char* p, size_t n)
{
    size_t h = Head.load(std::memory_order_relaxed);
    for (size_t i = 0; i < n; i++) Ring[(h + i) % RingSize] = p[i];
    Head.store(h + n, std::memory_order_release);
}

// Begins copying stderr. Safe to skip (a failure leaves stderr as it was).
inline void Start()
{
    if (OrigFd >= 0) return;
    int fds[2];
#ifdef _WIN32
    if (_pipe(fds, 1 << 16, _O_BINARY) != 0) return;
    OrigFd = _dup(2);
    _dup2(fds[1], 2);
    _close(fds[1]);
#define BR_READ _read
#define BR_WRITE _write
#define BR_CLOSE _close
#else
    if (pipe(fds) != 0) return;
    OrigFd = dup(2);
    dup2(fds[1], 2);
    close(fds[1]);
#define BR_READ read
#define BR_WRITE write
#define BR_CLOSE close
#endif
    const int rd = fds[0];
    Reader = std::thread([rd] {
        char buf[4096];
        for (;;)
        {
            int n = (int)BR_READ(rd, buf, sizeof buf);
            if (n <= 0) break;
            Put(buf, (size_t)n);
            if (BR_WRITE(OrigFd, buf, (unsigned)n) < 0) { /* nowhere to write: keep copying */ }
        }
        BR_CLOSE(rd);
    });
}

// Gives stderr back and lets the thread finish what it was sent.
inline void Stop()
{
    if (OrigFd < 0) return;
    fflush(stderr);
#ifdef _WIN32
    _dup2(OrigFd, 2);
#else
    dup2(OrigFd, 2);
#endif
    if (Reader.joinable()) Reader.join();
    BR_CLOSE(OrigFd);
    OrigFd = -1;
}

// The last `maxLines` lines, with the secrets and addresses blanked out.
inline std::string Tail(size_t maxLines = 2000)
{
    const size_t head = Head.load(std::memory_order_acquire);
    const size_t n = head < RingSize ? head : RingSize;
    std::string s(n, '\0');
    for (size_t i = 0; i < n; i++) s[i] = Ring[(head - n + i) % RingSize];
    size_t lines = 0, pos = s.size();
    while (pos > 0 && lines <= maxLines)
        if (s[--pos] == '\n') lines++;
    if (lines > maxLines) s.erase(0, pos + 1);
    return s;
}

inline std::string Redact(std::string s)
{
    for (const auto& sec : Secrets)
        if (sec.size() >= 3)
            for (size_t p = 0; (p = s.find(sec, p)) != std::string::npos; p += 8) s.replace(p, sec.size(), "(hidden)");
    static const std::regex ip("[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}");
    s = std::regex_replace(s, ip, "x.x.x.x");
    // The user's name is in every path under their home folder.
    for (const char* var : { "USERPROFILE", "HOME" })
        if (const char* h = getenv(var))
            if (strlen(h) > 3)
            {
                std::string back = h, fwd = h;
                for (char& c : fwd) if (c == '\\') c = '/';
                for (const std::string& v : { back, fwd })
                    for (size_t p = 0; (p = s.find(v, p)) != std::string::npos;) s.replace(p, v.size(), "~");
            }
    return s;
}

// The settings file, with the lines that say who the player is or where the server is hidden.
inline std::string SettingsText()
{
    std::ifstream f(std::filesystem::u8path(Config));
    std::string line, out;
    while (std::getline(f, line))
    {
        const std::string key = line.substr(0, line.find('='));
        // Only keys that start like these: "id" or "ip" inside a word would hide "wide_lines".
        static const char* hidden[] = { "account_", "online_", "friend", "player_id", "party", "server", "code", "pad_name" };
        bool hide = false;
        for (const char* h : hidden) hide |= key.rfind(h, 0) == 0;
        out += hide ? key + "=(hidden)\n" : line + "\n";
    }
    return out.empty() ? "(no settings file)\n" : out;
}

inline std::string OsText()
{
    std::string s = SDL_GetPlatform();
#ifdef _WIN32
    using Fn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (auto fn = (Fn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion"))
    {
        OSVERSIONINFOW v = { sizeof v };
        if (fn(&v) == 0) s += " " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + "." + std::to_string(v.dwBuildNumber);
    }
#endif
    return s + ", " + std::to_string(SDL_GetCPUCount()) + " cores, " + std::to_string(SDL_GetSystemRAM()) + " MB";
}

struct Info
{
    unsigned frame = 0;
    int map = -1;
    std::string place, app, party; // where, which game screen, the party server's state
    std::string crash;              // set for a crash report
};

inline std::string Stamp()
{
    char b[32];
    std::time_t t = std::time(nullptr);
    std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", std::localtime(&t));
    return b;
}

// Makes the folder (reports/report-<time>) and returns its path; the screenshot is written there
// by the caller to Folder + "/screenshot.png". Call Finish() once it exists.
inline std::string MakeFolder()
{
    std::error_code ec;
    auto p = std::filesystem::u8path(Dir) / "reports" / ("report-" + Stamp());
    std::filesystem::create_directories(p, ec);
    return p.u8string();
}

// Writes report.txt, log.txt and settings.txt; returns the one-line summary.
inline std::string Finish(const std::string& folder, const Info& in)
{
    char sum[512];
    snprintf(sum, sizeof sum, "Johto Online %s | %s | %s | map %d (%s) | screen %s | frame %u%s",
        Version.empty() ? "dev" : Version.c_str(), OsText().c_str(), Renderer.c_str(), in.map, in.place.c_str(),
        in.app.empty() ? "-" : in.app.c_str(), in.frame, in.crash.empty() ? "" : " | CRASH");
    const auto dir = std::filesystem::u8path(folder);
    std::ofstream(dir / "report.txt") << Redact(std::string(sum)) << "\n\nVersion: " << Version << "\nSystem: " << OsText()
        << "\nRenderer: " << Renderer << "\nMap: " << in.map << " " << in.place << "\nGame screen: " << in.app
        << "\nFrame: " << in.frame << "\nParty server: " << in.party << "\n"
        << (in.crash.empty() ? "" : "Crash: " + in.crash + "\n")
        << "\nNot included: your save, save states, the ROM, chat, your name, server address and party codes.\n";
    std::ofstream(dir / "settings.txt") << SettingsText();
    std::ofstream(dir / "log.txt") << Redact(Tail());
    return Redact(sum);
}

// A crash: the same report without the picture, as far as that is safe to do at that point.
inline void WriteCrash(const char* what)
{
    static std::atomic<bool> once{ false };
    if (once.exchange(true) || Dir.empty()) return;
    Info in;
    in.crash = what;
    try { Finish(MakeFolder(), in); } catch (...) {}
}
#ifdef _WIN32
inline LONG WINAPI CrashFilter(EXCEPTION_POINTERS* e)
{
    char b[64];
    snprintf(b, sizeof b, "exception %08lx", e ? e->ExceptionRecord->ExceptionCode : 0UL);
    WriteCrash(b);
    return EXCEPTION_CONTINUE_SEARCH;
}
inline void InstallCrashHandler() { SetUnhandledExceptionFilter(CrashFilter); }
#else
inline void CrashSignal(int sig)
{
    char b[32];
    snprintf(b, sizeof b, "signal %d", sig);
    WriteCrash(b);
    signal(sig, SIG_DFL);
    raise(sig);
}
inline void InstallCrashHandler()
{
    for (int s : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS }) signal(s, CrashSignal);
}
#endif

// A request waits a few frames so that the menu it came from is gone from the picture.
inline int Wait = -1;
inline bool Pending() { return Wait >= 0; }
inline void Request(int frames = 4) { if (Wait < 0) Wait = frames; }
// True on the frame the screenshot should be taken.
inline bool ShotNow() { return Wait >= 0 && Wait-- == 0; }

// Shows the folder in the file manager and puts the summary on the clipboard.
inline void Present(const std::string& folder, const std::string& summary, bool open)
{
    if (!open) return;
    SDL_SetClipboardText(summary.c_str());
    std::string url = "file://" + std::string(folder.size() > 1 && folder[1] == ':' ? "/" : "") + folder;
    for (char& c : url) if (c == '\\') c = '/';
    SDL_OpenURL(url.c_str());
}
} // namespace BugReport
