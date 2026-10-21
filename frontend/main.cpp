// Johto Online (ssrecomp) frontend: boots the game on the melonDS core with an SDL2 window,
// or headless with scripted input, screenshots and state hashes for testing.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include "sampler.h"
#include <vector>
#include <tuple>
#include <deque>

#ifdef __APPLE__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#if defined(__linux__) // (Android too)
#include <unistd.h>
#endif
#ifndef SSR_NO_PNG
#include <png.h>
#endif
#ifdef __ANDROID__
#include <atomic>
#include <jni.h>
#endif

#include "NDS.h"
#include "NDSCart.h"
#include "Args.h"
#include "GPU.h"
#include "GPU2D_Soft.h"
#include "SPU.h"
#include "RTC.h"
#include "Savestate.h"
#include "host.h"
#include "layout.h"
#include "ui.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "hud.h"
#include "hudmenu.h"
#include "hudtoast.h"
#include "hudscreens.h"
#include "hudbattle.h"
#include "hudbattleui.h"
#include "hudbattlebag.h"
#include "hudbattleparty.h"
#include "hudbattledex.h"
#include "hudbag.h"
#include "hudparty.h"
#include "hudtrade.h"
#include "huddex.h"
#include "huddexinfo.h"
#include "hudgear.h"
#include "hudoptions.h"
#include "hudnaming.h"
#include "hudpcbox.h"
#include "hudworld.h"
#include "hudshop.h"
#include "hudfrontier.h"
#include "hudminigames.h"
#define STB_IMAGE_IMPLEMENTATION
#include "autosave.h"
#define DR_MP3_IMPLEMENTATION
#include "songs.h"
#include "gamesfx.h"
#define PL_MPEG_IMPLEMENTATION
#include "intro.h"
#include "merged.h"
#include "gpucompose.h"
#include "battlewide.h"
#include "voidfill.h"
#include "party.h"
#include "worldblocks.h"
#include "recomp_rt.h"
#include "recomp_api.h"
#include "GPU3D_Wide.h"

#ifdef __ANDROID__
// The app holds the player's own game, so it cannot update itself as the setup window does:
// at start it asks GitHub for the newest release, and if that is newer than this build
// (SSR_VERSION, from CHANGELOG.md) the game says so, to rebuild it on the PC.
namespace UpdateCheck {
static std::atomic<bool> g_done{false};
static std::string g_newer; // set before g_done

// The body of an HTTPS GET through Java (the NDK has no TLS of its own); empty on failure.
static std::string Get(JNIEnv* env, const char* address)
{
    std::string body;
    auto failed = [&]() {
        if (!env->ExceptionCheck()) return false;
        env->ExceptionClear();
        return true;
    };
    jclass urlClass = env->FindClass("java/net/URL");
    jclass connClass = env->FindClass("java/net/URLConnection");
    jclass streamClass = env->FindClass("java/io/InputStream");
    if (failed() || !urlClass || !connClass || !streamClass) return body;
    jobject url = env->NewObject(urlClass, env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V"), env->NewStringUTF(address));
    if (failed()) return body;
    jobject conn = env->CallObjectMethod(url, env->GetMethodID(urlClass, "openConnection", "()Ljava/net/URLConnection;"));
    if (failed() || !conn) return body;
    env->CallVoidMethod(conn, env->GetMethodID(connClass, "setConnectTimeout", "(I)V"), 10000);
    env->CallVoidMethod(conn, env->GetMethodID(connClass, "setReadTimeout", "(I)V"), 10000);
    env->CallVoidMethod(conn, env->GetMethodID(connClass, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V"),
        env->NewStringUTF("Accept"), env->NewStringUTF("application/vnd.github+json"));
    if (failed()) return body;
    jobject in = env->CallObjectMethod(conn, env->GetMethodID(connClass, "getInputStream", "()Ljava/io/InputStream;"));
    if (failed() || !in) return body;
    jmethodID read = env->GetMethodID(streamClass, "read", "([BII)I");
    jbyteArray buf = env->NewByteArray(4096);
    while (body.size() < (1 << 20))
    {
        jint n = env->CallIntMethod(in, read, buf, 0, 4096);
        if (failed()) { body.clear(); break; }
        if (n < 0) break;
        const size_t at = body.size();
        body.resize(at + n);
        env->GetByteArrayRegion(buf, 0, n, (jbyte*)&body[at]);
    }
    env->CallVoidMethod(in, env->GetMethodID(streamClass, "close", "()V"));
    failed();
    return body;
}

static std::vector<int> Key(const std::string& v)
{
    std::vector<int> key;
    std::stringstream ss(v);
    std::string part;
    while (std::getline(ss, part, '.'))
    {
        if (part.empty() || part.find_first_not_of("0123456789") != std::string::npos) return {};
        key.push_back(atoi(part.c_str()));
    }
    return key;
}

static void Start()
{
    const std::string current = SSR_VERSION;
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JavaVM* vm = nullptr;
    if (current.empty() || !env || env->GetJavaVM(&vm) != JNI_OK) return;
    std::thread([vm, current]() {
        JNIEnv* env = nullptr;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        const std::string body = Get(env, "https://api.github.com/repos/nathanlogus/hgssrecomp/releases/latest");
        vm->DetachCurrentThread();
        // "tag_name": "v1.2.3"
        std::string latest;
        size_t at = body.find("\"tag_name\"");
        if (at != std::string::npos && (at = body.find('"', body.find(':', at))) != std::string::npos)
        {
            const size_t end = body.find('"', at + 1);
            if (end != std::string::npos) latest = body.substr(at + 1, end - at - 1);
        }
        if (!latest.empty() && latest[0] == 'v') latest.erase(0, 1);
        const std::vector<int> a = Key(latest), b = Key(current);
        if (latest.empty() || a.empty())
            fprintf(stderr, "update check: no answer from GitHub\n");
        else
        {
            fprintf(stderr, "update check: this is %s, the newest release is %s\n", current.c_str(), latest.c_str());
            if (!b.empty() && a > b) g_newer = latest;
        }
        g_done = true;
    }).detach();
}
}
#endif

// With --log-sections and a shot directory, a screenshot is taken around every change of merged scene.
static std::set<uint32_t> g_sceneShots;

// Defined in the 2D renderer: layers not to draw, per engine (see GPU2D_Soft.cpp).
extern unsigned rc_hide_layers[2];

using namespace melonDS;

namespace Host
{
bool StopRequested = false;
int LogLevel = 2;

static std::vector<uint8_t> SaveData;
static bool SaveIsDirty = false;

void SaveDirty(const uint8_t* data, uint32_t len)
{
    SaveData.assign(data, data + len);
    SaveIsDirty = true;
}
}

enum
{
    KEY_A, KEY_B, KEY_SELECT, KEY_START, KEY_RIGHT, KEY_LEFT, KEY_UP, KEY_DOWN, KEY_R, KEY_L, KEY_X, KEY_Y,
};

struct ScriptEvent
{
    uint32_t start, end;
    uint32_t keys = 0;
    bool touch = false;
    int tx = 0, ty = 0;
    // Virtual controller input, for testing the controller paths without hardware.
    uint32_t padButtons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
};

// Controller state in SDL's button order, filled from a real pad or from a test script.
enum
{
    PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, PAD_GUIDE, PAD_START, PAD_LS, PAD_RS, PAD_LB, PAD_RB,
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_COUNT
};

struct PadState
{
    bool btn[PAD_COUNT] = {};
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
};

#include "touchpad.h"

static int PadButtonFromName(const std::string& n)
{
    static const char* names[PAD_COUNT] = { "a", "b", "x", "y", "back", "guide", "start", "ls", "rs", "lb", "rb",
        "up", "down", "left", "right" };
    for (int i = 0; i < PAD_COUNT; i++)
        if (n == names[i]) return i;
    return -1;
}

static int KeyFromName(const std::string& n)
{
    static const char* names[] = { "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L", "X", "Y" };
    for (int i = 0; i < 12; i++)
        if (n == names[i]) return i;
    return -1;
}

// Script lines: "<first frame> <last frame> KEY [KEY...]" or "<first> <last> touch <x> <y>"
static std::vector<ScriptEvent> LoadScript(const std::string& path)
{
    std::vector<ScriptEvent> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        ScriptEvent ev;
        std::string tok;
        if (!(ss >> ev.start >> ev.end)) continue;
        while (ss >> tok)
        {
            if (tok == "touch")
            {
                ev.touch = true;
                ss >> ev.tx >> ev.ty;
            }
            else if (tok.rfind("pad:", 0) == 0)
            {
                // pad:a, pad:guide, ... for buttons; pad:rt, pad:lt, pad:rx+, pad:ry-, pad:lx+ ... for axes
                std::string n = tok.substr(4);
                int b = PadButtonFromName(n);
                if (b >= 0) ev.padButtons |= 1u << b;
                else if (n == "rt") ev.rt = 1;
                else if (n == "lt") ev.lt = 1;
                else if (n.size() == 3 && (n[2] == '+' || n[2] == '-'))
                {
                    float v = n[2] == '+' ? 1.0f : -1.0f;
                    if (n.compare(0, 2, "lx") == 0) ev.lx = v;
                    else if (n.compare(0, 2, "ly") == 0) ev.ly = v;
                    else if (n.compare(0, 2, "rx") == 0) ev.rx = v;
                    else if (n.compare(0, 2, "ry") == 0) ev.ry = v;
                    else fprintf(stderr, "script: unknown pad input '%s'\n", tok.c_str());
                }
                else fprintf(stderr, "script: unknown pad input '%s'\n", tok.c_str());
            }
            else
            {
                int k = KeyFromName(tok);
                if (k < 0) fprintf(stderr, "script: unknown key '%s'\n", tok.c_str());
                else ev.keys |= 1u << k;
            }
        }
        out.push_back(ev);
    }
    return out;
}

static uint32_t TopChanges = 0;
static int AudioPeak = 0;
static double AudioSumSq = 0;
static uint64_t AudioSamples = 0;

static uint64_t Fnv1a(const void* data, size_t len, uint64_t h = 0xcbf29ce484222325ull)
{
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++)
    {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

static bool WritePNG(const std::string& path, const uint32_t* pixels, int w, int h)
{
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    for (int i = 0; i < w * h; i++)
    {
        rgb[i * 3 + 0] = (pixels[i] >> 16) & 0xFF;
        rgb[i * 3 + 1] = (pixels[i] >> 8) & 0xFF;
        rgb[i * 3 + 2] = pixels[i] & 0xFF;
    }
#ifdef SSR_NO_PNG
    (void)path;
    return false;
#else
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    img.width = w;
    img.height = h;
    img.format = PNG_FORMAT_RGB;
    return png_image_write_to_file(&img, path.c_str(), 0, rgb.data(), 0, nullptr) != 0;
#endif
}

static std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::vector<uint8_t> data;
#ifdef __ANDROID__
    // Relative paths are files packed in the APK (the ROM); SDL reads both kinds.
    if (SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb"))
    {
        Sint64 len = SDL_RWsize(rw);
        if (len > 0)
        {
            data.resize((size_t)len);
            if (SDL_RWread(rw, data.data(), 1, (size_t)len) != (size_t)len) data.clear();
        }
        SDL_RWclose(rw);
    }
    return data;
#endif
    // Through a UTF-8 path: fopen takes the Windows code page and misses names such as
    // "Pokémon" there.
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    if (!f) return data;
    const std::streamsize len = f.tellg();
    if (len <= 0) return data;
    data.resize((size_t)len);
    f.seekg(0);
    if (!f.read((char*)data.data(), len)) data.clear();
    return data;
}

// The HUD's sprite folder (assets/ui): next to the source tree's build folder on PCs, in
// the APK's assets on Android.
// The folder above it (assets/) holds the logo and the player's own files: songs, fonts,
// buttons, the title video and the autosave GIF. On Android that is the APK's assets
// themselves (tools/android/apk.py), as "ui/../x" is not resolved there.
static std::string g_uiDir, g_userDir;
static std::vector<uint8_t> ReadUiFile(const std::string& name) { return ReadFile(g_uiDir + name); }
// The player's own songs (songs.h): assets/songs, beside the HUD's folder.
static std::vector<uint8_t> ReadSongFile(const std::string& name) { return ReadFile(g_userDir + "songs/" + name); }
namespace melonDS { extern u16 ssr_SpuHearMute; } // SPU.cpp: channels left out of what is heard
static void FindUiDir()
{
#ifdef __ANDROID__
    g_uiDir = "ui/";
    g_userDir = "";
#else
    namespace fs = std::filesystem;
    char* base = SDL_GetBasePath();
    const fs::path exe = base ? fs::u8path(base) : fs::current_path();
    SDL_free(base);
    for (const fs::path& d : { exe / ".." / "assets" / "ui", exe / "assets" / "ui", fs::current_path() / "assets" / "ui" })
    {
        std::error_code ec;
        if (fs::exists(d / "manifest.json", ec))
        {
            g_uiDir = (d / "").u8string();
            break;
        }
    }
    g_userDir = g_uiDir + "../";
#endif
}

static void FlushSave(const std::string& path)
{
    if (!Host::SaveIsDirty || path.empty()) return;
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    bool ok = fwrite(Host::SaveData.data(), 1, Host::SaveData.size(), f) == Host::SaveData.size();
    ok = fclose(f) == 0 && ok;
    if (!ok) return;
    // std::filesystem::rename replaces an existing file on Windows too (plain rename()
    // fails there when the target exists, which kept every save after the first).
    std::error_code err;
    std::filesystem::rename(tmp, path, err);
    if (err)
    {
        fprintf(stderr, "cannot write save %s: %s\n", path.c_str(), err.message().c_str());
        return;
    }
    Host::SaveIsDirty = false;
}

struct Options
{
    std::string rom;
    std::string save;
    std::string script;
    std::string shotDir;
    std::set<uint32_t> shotAt;
    uint32_t shotEvery = 0;
    uint32_t hashEvery = 0;
    std::string nav;                     // test aid: walk to these tiles, "x,z;x,z;...", holding the script meanwhile
    bool navStopOnBattle = false;        // end --nav when a battle starts, so the script can play it
    uint32_t stopAt = 0;                 // real frame at which to quit, whatever the script clock says
    uint32_t navAfter = 0;               // script frame at which --nav starts
    uint32_t talkAfter = 0;              // test aid: from this script frame, walk to the nearest person and talk to them
    std::vector<std::pair<int, std::string>> navMapIds; // grids of single interior maps
    std::string navMap;                  // permission grid from tools/navmap.py for --nav
    uint32_t posEvery = 0;               // test aid: print map and player tile every N frames
    uint32_t frames = 0;
    bool headless = false;
    bool noSave = false;
    bool recomp = true;
    bool lockstep = false;
    bool fullscreen = false;
    bool swapped = false;
    bool logSections = false;
    bool noWide = false;                 // turn the widescreen 3D renderer off
    bool benchCompose = false;           // headless: compose the merged picture every frame, to time it
    bool autoFocus = true;
    bool layoutGiven = false;
    bool audioStats = false;
    int interp60 = -1;                   // 3D frame interpolation: -1 = from settings (off when headless)
    bool battleMash = false;             // test aid: tap A through battles, pausing the script meanwhile
    bool countChanges = false;           // report how many frames changed the top screen
    bool shotLayers = false;             // also write a per-pixel layer map with each screenshot
    std::set<uint32_t> traceFuncsAt;     // frames at which to list the functions that just ran
    std::string symbols = "gen/symbols.txt";
    std::string config;          // settings file
    std::string stateLoad;       // snapshot to resume from
    std::string stateSave;       // snapshot to write
    uint32_t stateSaveAt = 0;    // frame at which to write it
    int composeW = 0, composeH = 0; // screenshots use the single-display layout at this size
    int scale = 3;
    std::string layout = "merged";
    std::string party, partyCode = "union"; // relay server and party to join
};

static void Usage()
{
    fprintf(stderr,
        "usage: johto-online <rom.nds> [options]\n"
        "  --save <file>        save file (default: <rom>.sav)\n"
        "  --no-save            never write the save file\n"
        "  --layout <name>      merged | hybrid | side | vertical | top | bottom (default merged)\n"
        "  --config <file>      settings file (default: johto-online.cfg beside the save)\n"
        "  --scale <n>          window scale (default 3)\n"
        "  --fullscreen         start in fullscreen (F11 toggles)\n"
        "  --headless           no window or audio; run as fast as possible\n"
        "  --frames <n>         stop after n frames\n"
        "  --input <file>       scripted input\n"
        "  --swap               start with the bottom screen as the large one (hybrid)\n"
        "  --no-auto-focus      keep the top screen large instead of choosing automatically\n"
        "  --compose <WxH>      screenshots show the chosen layout at this size instead of\n"
        "                       the two raw screens stacked\n"
        "  --state-save <file> --state-at <frame>   write a snapshot at that frame\n"
        "  --state-load <file>  resume from a snapshot (frame numbering continues)\n"
        "  --shot-dir <dir>     where screenshots go\n"
        "  --shot-at <a,b,c>    screenshot these frames\n"
        "  --shot-every <n>     screenshot every n frames\n"
        "  --hash-every <n>     print a state hash every n frames\n"
        "  --log-sections       print which overlays run code and which merged scene is shown, on change\n"
        "  --interp60 / --no-interp60   force the 60 fps 3D interpolation on or off\n"
        "  --battle-mash        while a battle is running, tap A and hold the input script (and the\n"
        "                       frame used by --state-at) still, so scripts can walk through grass\n"
        "  --count-changes      report on how many frames the top screen image changed\n"
        "  --audio-stats        print the peak and RMS level of the generated audio at exit\n"
        "  --shot-layers        with each screenshot, also write layers_<frame>.png colouring every\n"
        "                       pixel by the 2D layer it came from\n"

        "  --trace-funcs <a,b>  at these frames, write the names of all game functions entered in\n"
        "                       the last two frames to funcs_<frame>.txt in the screenshot directory\n"
        "  --symbols <file>     function list for --trace-funcs (default gen/symbols.txt)\n"
        "  --interp             run the ARM9 on the interpreter instead of recompiled code\n"
        "  --lockstep           run interpreter and recompiled code side by side and compare\n"
        "                       every ARM9 instruction (headless only)\n"
        "  --party <host[:port]> join a party on a relay server (tools/party_server.py): the\n"
        "                       games in it share local wireless (Union Room, link battles, trades)\n"
        "  --party-code <name>  which party on the server (default union)\n"
        "  --log <0-3>          core log level (0 = everything)\n");
}

static bool ParseArgs(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--save") o.save = next();
        else if (a == "--no-save") o.noSave = true;
        else if (a == "--layout") { o.layout = next(); o.layoutGiven = true; }
        else if (a == "--config") o.config = next();
        else if (a == "--audio-stats") o.audioStats = true;
        else if (a == "--shot-layers") o.shotLayers = true;
        else if (a == "--count-changes") o.countChanges = true;
        else if (a == "--battle-mash") o.battleMash = true;
        else if (a == "--interp60") o.interp60 = 1;
        else if (a == "--no-interp60") o.interp60 = 0;

        else if (a == "--symbols") o.symbols = next();
        else if (a == "--trace-funcs")
        {
            std::istringstream ss(next());
            std::string tok;
            while (std::getline(ss, tok, ',')) o.traceFuncsAt.insert(strtoul(tok.c_str(), nullptr, 10));
        }
        else if (a == "--scale") o.scale = atoi(next().c_str());
        else if (a == "--headless") o.headless = true;
        else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--swap") o.swapped = true;
        else if (a == "--log-sections") o.logSections = true;
        else if (a == "--no-wide") o.noWide = true;
        else if (a == "--bench-compose") o.benchCompose = true;
        else if (a == "--no-auto-focus") o.autoFocus = false;
        else if (a == "--state-save") o.stateSave = next();
        else if (a == "--state-at") o.stateSaveAt = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--state-load") o.stateLoad = next();
        else if (a == "--compose") sscanf(next().c_str(), "%dx%d", &o.composeW, &o.composeH);
        else if (a == "--interp") o.recomp = false;
        else if (a == "--party") o.party = next();
        else if (a == "--party-code") o.partyCode = next();
        else if (a == "--lockstep") { o.lockstep = true; o.headless = true; o.noSave = true; }
        else if (a == "--frames") o.frames = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--input") o.script = next();
        else if (a == "--shot-dir") o.shotDir = next();
        else if (a == "--shot-every") o.shotEvery = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--nav") o.nav = next();
        else if (a == "--nav-stop-on-battle") o.navStopOnBattle = true;
        else if (a == "--stop-at") o.stopAt = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--nav-after") o.navAfter = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--talk-after") o.talkAfter = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--nav-map") o.navMap = next();
        else if (a == "--nav-map-id")
        {
            std::string v = next();
            size_t eq = v.find('=');
            if (eq != std::string::npos) o.navMapIds.push_back({ atoi(v.c_str()), v.substr(eq + 1) });
        }
        else if (a == "--pos-every") o.posEvery = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--hash-every") o.hashEvery = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--log") Host::LogLevel = atoi(next().c_str());
        else if (a == "--shot-at")
        {
            std::istringstream ss(next());
            std::string tok;
            while (std::getline(ss, tok, ',')) o.shotAt.insert(strtoul(tok.c_str(), nullptr, 10));
        }
        else if (a[0] == '-') return false;
        else o.rom = a;
    }
    return !o.rom.empty();
}

// The DS clock. The game lights the overworld by the time of day (dark blue at night,
// orange in the evening), so it follows the computer's clock as on a real DS; the tests
// use a fixed noon so their pictures do not depend on when they run.
// SSR_CLOCK="YYYY-MM-DD HH:MM" overrides it.
static void SetClock(NDS& nds, bool fixed)
{
    int y = 2026, mo = 1, d = 1, h = 12, mi = 0, sec = 0;
    if (const char* c = getenv("SSR_CLOCK"))
        sscanf(c, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi);
    else if (!fixed)
    {
        time_t now = time(nullptr);
        struct tm t = *localtime(&now);
        y = t.tm_year + 1900; mo = t.tm_mon + 1; d = t.tm_mday;
        h = t.tm_hour; mi = t.tm_min; sec = t.tm_sec;
    }
    nds.RTC.SetDateTime(y, mo, d, h, mi, sec);
}

static std::unique_ptr<NDS> MakeConsole(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& sav, bool fixedClock)
{
    NDSCart::NDSCartArgs cartArgs;
    if (!sav.empty())
    {
        cartArgs.SRAM = std::make_unique<u8[]>(sav.size());
        memcpy(cartArgs.SRAM.get(), sav.data(), sav.size());
        cartArgs.SRAMLength = (u32)sav.size();
    }
    auto cart = NDSCart::ParseROM(rom.data(), (u32)rom.size(), nullptr, std::move(cartArgs));
    if (!cart)
    {
        fprintf(stderr, "ROM rejected by the core\n");
        return nullptr;
    }
    NDSArgs args;
    args.JIT = std::nullopt;
    auto nds = std::make_unique<NDS>(std::move(args));
    nds->SetNDSCart(std::move(cart));
    nds->Reset();
    nds->SetupDirectBoot("game.nds");
    SetClock(*nds, fixedClock);
    nds->Start();
    return nds;
}

struct TraceState
{
    u32 r[16];
    u32 cpsr;
    s32 cycles;
    u64 ts;
};

static std::vector<TraceState> TraceLog;
static size_t TracePos = 0;
static uint32_t TraceFrame = 0;
static bool TraceFailed = false;
static u32 TracePrevR15 = 0, TracePrevCPSR = 0;

static void TraceCapture(ARMv5* cpu, TraceState& st)
{
    memcpy(st.r, cpu->R, sizeof(st.r));
    st.cpsr = cpu->CPSR;
    st.cycles = cpu->Cycles;
    st.ts = cpu->NDS.ARM9Timestamp;
}

static void TraceRecord(ARMv5* cpu, void*)
{
    TraceLog.emplace_back();
    TraceCapture(cpu, TraceLog.back());
}

static void TraceDump(const char* tag, const TraceState& st)
{
    fprintf(stderr, "  %s:", tag);
    for (int i = 0; i < 16; i++) fprintf(stderr, " r%d=%08x", i, st.r[i]);
    fprintf(stderr, " cpsr=%08x cycles=%d ts=%llu\n", st.cpsr, st.cycles, (unsigned long long)st.ts);
}

static void TraceCompare(ARMv5* cpu, void*)
{
    if (TraceFailed) return;
    TraceState now;
    memset(&now, 0, sizeof(now));
    TraceCapture(cpu, now);
    bool bad = TracePos >= TraceLog.size();
    if (!bad)
    {
        const TraceState& want = TraceLog[TracePos];
        bad = memcmp(want.r, now.r, sizeof(now.r)) != 0 || want.cpsr != now.cpsr ||
            want.cycles != now.cycles || want.ts != now.ts;
    }
    if (bad)
    {
        TraceFailed = true;
        bool thumb = (TracePrevCPSR & 0x20) != 0;
        fprintf(stderr, "LOCKSTEP MISMATCH frame %u instruction #%zu at %08x (%s) opcode %08x\n",
            TraceFrame, TracePos, TracePrevR15 - (thumb ? 2 : 4), thumb ? "thumb" : "arm", cpu->CurInstr);
        if (TracePos < TraceLog.size()) TraceDump("interp", TraceLog[TracePos]);
        else fprintf(stderr, "  interp: ran fewer instructions this frame (%zu)\n", TraceLog.size());
        TraceDump("recomp", now);
    }
    TracePos++;
    TracePrevR15 = cpu->R[15];
    TracePrevCPSR = cpu->CPSR;
}

// Decides which DS screen should be the large one on a single display.
//
// Two signals: a screen showing one flat colour has nothing to look at, and some
// applications (recognised by their per-frame function having just run) are driven
// from the bottom screen. A manual swap overrides the decision until it next changes.
struct AutoFocus
{
    int mode = 0;              // 0 automatic, 1 top screen large, 2 bottom screen large
    bool wantBottom = false;   // current automatic decision
    bool manualFlip = false;
    int pending = 0;           // frames the opposite decision has been stable
    std::vector<const RcApp*> bottomApps;

    void Init()
    {
        // Applications whose controls and content are on the bottom screen.
        static const char* names[] = {
            "Bag", "PartyMenu", "NamingScreen", "Pokegear", "GearConfigure", "GearMap", "GearPhone", "GearRadio",
            "TownMap", "PCBox", "EasyChat", "VoltorbFlip", "Pokedex", "ScratchOffCards", "AlphPuzzle",
            "TrainerCard", "TrainerCardSignature", "BugContestSwapMon", "WirelessTradeSelectMon", "OptionsMenu",
            "MainMenu", "CheckSave", "DeleteSave",
        };
        for (unsigned i = 0; i < rc_num_apps; i++)
            for (const char* n : names)
                if (!strcmp(rc_apps[i].name, n)) bottomApps.push_back(&rc_apps[i]);
    }

    static bool Blank(const u32* fb)
    {
        u32 c = fb[0] & 0xFFFFFF;
        for (int i = 1; i < 256 * 192; i++)
            if ((fb[i] & 0xFFFFFF) != c) return false;
        return true;
    }

    // appBottom: one of bottomApps is starting, running or ending (its probes, which see
    // the start and end too, unlike the check below).
    void Update(const u32* top, const u32* bot, bool fieldNow = false, bool appBottom = false)
    {
        bool topBlank = Blank(top), botBlank = Blank(bot);
        bool want = wantBottom;
        int hold = 12;
        if (topBlank && botBlank)
            ; // fade or load: keep whatever is showing
        else if (appBottom && !fieldNow)
        {
            // Straight to the bottom screen, even while it is still dark and the other one
            // is not: an application loading draws one screen before the other, and
            // following that showed the top screen first and then switched.
            want = true;
            hold = 1;
        }
        else if (fieldNow && !topBlank)
        {
            // Back in the overworld: the application that was open is over, even though
            // its code ran a few frames ago.
            want = false;
            hold = 1;
        }
        else if (topBlank)
        {
            // A top screen of one colour is also what a flash or fade looks like for a
            // moment, so this has to last before the bottom screen is shown instead.
            want = true;
            hold = 45;
        }
        else if (botBlank)
            want = false;
        else
        {
            want = false;
            for (const RcApp* app : bottomApps)
                if (rc_ran_recently(app->section, app->execAddr, 6)) want = true;
        }
        if (want == wantBottom)
        {
            pending = 0;
            return;
        }
        // Require the new state to hold briefly so transitions do not flicker.
        if (++pending >= hold)
        {
            wantBottom = want;
            manualFlip = false;
            pending = 0;
        }
    }

    bool Swapped() const
    {
        bool base = mode == 0 ? wantBottom : mode == 2;
        return base != manualFlip;
    }
};

// Software version of what the window shows, for screenshots.
static void Compose(const Layout& layout, int w, int h, const u32* top, const u32* bot, std::vector<uint32_t>& out)
{
    out.assign((size_t)w * h, 0);
    SDL_Rect rects[2];
    LayoutRects(layout, w, h, rects);
    int first = LayoutDrawFirst(layout);
    for (int n = 0; n < 2; n++)
    {
        int i = n == 0 ? first : 1 - first;
        const SDL_Rect& r = rects[i];
        const u32* src = i == 0 ? top : bot;
        if (r.w <= 0 || r.h <= 0) continue;
        for (int y = 0; y < r.h; y++)
        {
            int dy = r.y + y;
            if (dy < 0 || dy >= h) continue;
            const u32* row = src + (y * 192 / r.h) * 256;
            for (int x = 0; x < r.w; x++)
            {
                int dx = r.x + x;
                if (dx >= 0 && dx < w) out[(size_t)dy * w + dx] = row[x * 256 / r.w];
            }
        }
    }
}

static uint32_t KeyboardKeys()
{
    const Uint8* st = SDL_GetKeyboardState(nullptr);
    uint32_t k = 0;
    if (st[SDL_SCANCODE_X]) k |= 1 << KEY_A;
    if (st[SDL_SCANCODE_Z]) k |= 1 << KEY_B;
    if (st[SDL_SCANCODE_S]) k |= 1 << KEY_X;
    if (st[SDL_SCANCODE_A]) k |= 1 << KEY_Y;
    if (st[SDL_SCANCODE_Q]) k |= 1 << KEY_L;
    if (st[SDL_SCANCODE_W]) k |= 1 << KEY_R;
    if (st[SDL_SCANCODE_RETURN]) k |= 1 << KEY_START;
    if (st[SDL_SCANCODE_RSHIFT] || st[SDL_SCANCODE_BACKSPACE]) k |= 1 << KEY_SELECT;
    if (st[SDL_SCANCODE_UP]) k |= 1 << KEY_UP;
    if (st[SDL_SCANCODE_DOWN]) k |= 1 << KEY_DOWN;
    if (st[SDL_SCANCODE_LEFT]) k |= 1 << KEY_LEFT;
    if (st[SDL_SCANCODE_RIGHT]) k |= 1 << KEY_RIGHT;
    return k;
}

// The controllers in use: all of them at once, as which one is first is not to be relied on
// (Windows often lists a virtual or idle one first: Steam's, DS4Windows', a wheel's). Those
// SDL knows the layout of are game controllers; any other joystick (a generic USB pad) is
// read with the usual layout: the first buttons as A B X Y, L R, Select Start, the hat and
// the first two axes as the D-pad and the left stick.
struct Pads
{
    std::vector<SDL_GameController*> controllers;
    std::vector<SDL_Joystick*> joysticks;
    // Per joystick, whether its first two axes rest in the middle (1), not (2) or not known
    // yet (0, until half a second after it was opened, when its state has surely arrived):
    // some keyboards, mice and wheels report themselves as joysticks with an axis resting at
    // an end, which would walk the player on its own. And when it was opened.
    std::vector<int> axesUsable;
    std::vector<Uint32> openedAt;

    bool Has(SDL_JoystickID id) const
    {
        for (SDL_GameController* c : controllers)
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)) == id) return true;
        for (SDL_Joystick* j : joysticks)
            if (SDL_JoystickInstanceID(j) == id) return true;
        return false;
    }
    bool Empty() const { return controllers.empty() && joysticks.empty(); }

    // Opens the device at `index` unless already open; its name, or null.
    const char* Open(int index)
    {
        if (getenv("SSR_NO_PAD") || Has(SDL_JoystickGetDeviceInstanceID(index))) return nullptr; // tests must not hear a real controller
        if (SDL_IsGameController(index))
        {
            SDL_GameController* c = SDL_GameControllerOpen(index);
            if (!c)
            {
                fprintf(stderr, "controller %d could not be opened: %s\n", index, SDL_GetError());
                return nullptr;
            }
            controllers.push_back(c);
            const char* name = SDL_GameControllerName(c);
            fprintf(stderr, "controller: %s\n", name ? name : "?");
            return name ? name : "";
        }
#ifdef __ANDROID__
        // Android reports some of a device's own keys as a joystick, and using one hides the
        // on-screen controls.
        return nullptr;
#endif
        SDL_Joystick* j = SDL_JoystickOpen(index);
        if (!j)
        {
            fprintf(stderr, "joystick %d could not be opened: %s\n", index, SDL_GetError());
            return nullptr;
        }
        // Not one without buttons (an accelerometer, a wheel's pedals alone).
        if (SDL_JoystickNumButtons(j) < 2)
        {
            SDL_JoystickClose(j);
            return nullptr;
        }
        joysticks.push_back(j);
        axesUsable.push_back(SDL_JoystickNumAxes(j) >= 2 ? 0 : 2);
        openedAt.push_back(SDL_GetTicks());
        const char* name = SDL_JoystickName(j);
        fprintf(stderr, "joystick without a known layout: %s (%d buttons, %d axes, %d hats)\n", name ? name : "?",
            SDL_JoystickNumButtons(j), SDL_JoystickNumAxes(j), SDL_JoystickNumHats(j));
        return name ? name : "";
    }

    // Closes the device with instance `id`; whether it was open.
    bool Close(SDL_JoystickID id)
    {
        for (size_t i = 0; i < controllers.size(); i++)
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[i])) == id)
            {
                SDL_GameControllerClose(controllers[i]);
                controllers.erase(controllers.begin() + i);
                return true;
            }
        for (size_t i = 0; i < joysticks.size(); i++)
            if (SDL_JoystickInstanceID(joysticks[i]) == id)
            {
                SDL_JoystickClose(joysticks[i]);
                joysticks.erase(joysticks.begin() + i);
                axesUsable.erase(axesUsable.begin() + i);
                openedAt.erase(openedAt.begin() + i);
                return true;
            }
        return false;
    }
};

static PadState ReadPad(Pads& pads)
{
    PadState st;
    // Each button pressed on any of them; each stick as far as on the one pushed furthest.
    auto merge = [](float& to, float v) { if (fabsf(v) > fabsf(to)) to = v; };
    for (SDL_GameController* pad : pads.controllers)
    {
        for (int i = 0; i < PAD_COUNT; i++)
            st.btn[i] |= SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)i) != 0;
        auto axis = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(pad, a) / 32767.0f; };
        merge(st.lx, axis(SDL_CONTROLLER_AXIS_LEFTX));
        merge(st.ly, axis(SDL_CONTROLLER_AXIS_LEFTY));
        merge(st.rx, axis(SDL_CONTROLLER_AXIS_RIGHTX));
        merge(st.ry, axis(SDL_CONTROLLER_AXIS_RIGHTY));
        merge(st.lt, axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT));
        merge(st.rt, axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
    }
    for (size_t k = 0; k < pads.joysticks.size(); k++)
    {
        SDL_Joystick* j = pads.joysticks[k];
        static const int order[] = { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB, PAD_BACK, PAD_START };
        const int n = std::min(SDL_JoystickNumButtons(j), (int)(sizeof(order) / sizeof(order[0])));
        for (int i = 0; i < n; i++)
            st.btn[order[i]] |= SDL_JoystickGetButton(j, i) != 0;
        if (SDL_JoystickNumHats(j) > 0)
        {
            const Uint8 h = SDL_JoystickGetHat(j, 0);
            st.btn[PAD_UP] |= (h & SDL_HAT_UP) != 0;
            st.btn[PAD_DOWN] |= (h & SDL_HAT_DOWN) != 0;
            st.btn[PAD_LEFT] |= (h & SDL_HAT_LEFT) != 0;
            st.btn[PAD_RIGHT] |= (h & SDL_HAT_RIGHT) != 0;
        }
        int& usable = pads.axesUsable[k];
        if (usable == 0 && SDL_GetTicks() - pads.openedAt[k] >= 500)
            usable = (abs(SDL_JoystickGetAxis(j, 0)) < 8000 && abs(SDL_JoystickGetAxis(j, 1)) < 8000) ? 1 : 2;
        if (usable == 1)
        {
            merge(st.lx, SDL_JoystickGetAxis(j, 0) / 32767.0f);
            merge(st.ly, SDL_JoystickGetAxis(j, 1) / 32767.0f);
        }
    }
    return st;
}

// The DS buttons that can be given another controller button, in the Controls tab's order.
static const int RemapKeys[] = { KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_START, KEY_SELECT };
static const int RemapCount = (int)(sizeof(RemapKeys) / sizeof(RemapKeys[0]));

// The controller button for a DS button (RemapKeys[i]): the one chosen in the Controls tab,
// or else by the label printed on it ("A" is A on any pad) or by position (the DS diamond:
// right = A, bottom = B).
static int PadButtonFor(int i, bool byLabel, const int* map)
{
    if (map && map[i] >= 0) return map[i];
    static const int labels[] = { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB, PAD_START, PAD_BACK };
    static const int positions[] = { PAD_B, PAD_A, PAD_Y, PAD_X, PAD_LB, PAD_RB, PAD_START, PAD_BACK };
    return byLabel ? labels[i] : positions[i];
}

// DS buttons from the controller.
static uint32_t PadKeys(const PadState& p, bool byLabel, const int* map)
{
    uint32_t k = 0;
    for (int i = 0; i < RemapCount; i++)
        if (p.btn[PadButtonFor(i, byLabel, map)]) k |= 1 << RemapKeys[i];
    if (p.btn[PAD_UP] || p.ly < -0.5f) k |= 1 << KEY_UP;
    if (p.btn[PAD_DOWN] || p.ly > 0.5f) k |= 1 << KEY_DOWN;
    if (p.btn[PAD_LEFT] || p.lx < -0.5f) k |= 1 << KEY_LEFT;
    if (p.btn[PAD_RIGHT] || p.lx > 0.5f) k |= 1 << KEY_RIGHT;
    return k;
}

// The HUD sprite showing a controller button.
static std::string PadGlyph(int b)
{
    switch (b)
    {
    case PAD_A: return "buttons/button_a";
    case PAD_B: return "buttons/button_b";
    case PAD_X: return "buttons/button_x";
    case PAD_Y: return "buttons/button_y";
    case PAD_LB: return "buttons/button_l";
    case PAD_RB: return "buttons/button_r";
    case PAD_START: return "buttons/button_plus";
    case PAD_BACK: return "buttons/button_minus";
    case PAD_LS: return "buttons/stick_l";
    case PAD_RS: return "buttons/stick_r";
    case PAD_UP: return "buttons/button_dpad_up";
    case PAD_DOWN: return "buttons/button_dpad_down";
    case PAD_LEFT: return "buttons/button_dpad_left";
    case PAD_RIGHT: return "buttons/button_dpad_right";
    default: return "buttons/button_dpad";
    }
}

// Shapes the single-screen view can be held to, with bars beside it on a wider window.
// 0 is the window's own shape.
static const struct { const char* name; float ratio; } AspectRatios[] = {
    { "FILL WINDOW", 0 }, { "4:3", 4 / 3.0f }, { "16:10", 16 / 10.0f }, { "16:9", 16 / 9.0f }, { "21:9", 64 / 27.0f },
};
static const int AspectCount = (int)(sizeof(AspectRatios) / sizeof(AspectRatios[0]));
// World zoom steps (percent): above 100 the overworld is drawn larger, below it more of it is seen.
static const int ZoomSteps[] = { 60, 70, 80, 90, 100, 115, 130, 150 };
static const int ZoomCount = (int)(sizeof(ZoomSteps) / sizeof(ZoomSteps[0]));
// Window size steps: the height in pixels, the width following the aspect ratio.
static const int WindowHeights[] = { 720, 900, 1080, 1440 };
static const int WindowHeightCount = (int)(sizeof(WindowHeights) / sizeof(WindowHeights[0]));

// Settings that persist between runs.
struct Settings
{
    std::string layout = "merged";
    int focusMode = 0;
    bool fullscreen = false;
    bool smooth = false;
    bool wide = true;   // widescreen 3D overworld
    int wideLines = 720; // height the widescreen 3D is drawn at, at most
    bool byLabel = true;
    int padMap[8] = { -1, -1, -1, -1, -1, -1, -1, -1 }; // controller button per DS button (RemapKeys); -1 by label/position
    bool fps60 = true;
    bool fpsUnlimited = false; // more pictures than the game makes: as many as there is time for
    bool vsync = false;        // each picture shown at the display's refresh (no tearing)
    // Right stick as a touch cursor, right trigger as the press, and the stick clicks that
    // switch screens. Off unless asked for: brushing the stick put the bottom screen up.
    bool padTouch = false;
    bool showFps = false; // frames shown per second, in a corner
    int aspect = 0;       // index into AspectRatios
    int zoom = 100;       // world zoom, percent (one of ZoomSteps)
    bool voidFill = true; // haze instead of black where the world reaches past the loaded map
    bool nightFx = true;  // glow, light pools, fireflies and night colours in the evening and at night
    bool redrawn = true;
    bool newSongs = true;
    bool menuSounds = true; // the game's own cursor and button sounds in the frontend's menus (gamesfx.h)
    std::string textFont = "new"; // Text: a font family of the player's (assets/fonts) by name, "new" (the first), or "game"
    int blur = 0;         // focus blur towards the edges of the world: 0 off, 1 light, 2 strong
    bool farMap = true;   // draw the map blocks around the loaded ones into the widescreen world
    int windowH = 0;      // window height chosen in the menu; 0 = from --scale
    // Playing online (party.cpp): the relay server, and who this player is to the others.
    std::string onlineServer;
    std::string onlineName; // empty: the trainer's name
    std::string friends;    // friends: "id:name:colour:last seen (Unix time)," ... (older: without the id)
    std::string playerId;   // this player's id for friends (16 letters, made once)
    int onlineColour = -1;  // -1: by player number
    int hostMax = 4;        // players a hosted session allows
    bool autoHost = true;   // host a session by itself once the player is in the world
    int onlinePartner = 0;  // the Pokemon shown with this player (species, 0 none)
    bool showPing = true;
    bool onlineToasts = true;
    bool startScreen = true; // the frontend's own start screen before the game
    bool autosave = true;    // resume points (autosave.h): kept in the background, never a freeze
    bool autosaveGame = false; // also the game's own save at calm moments (it stops the game while it writes)

    void Load(const std::string& path)
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line))
        {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            // "view" replaced the older "layout" key when the merged view became the default.
            if (k == "view") layout = v;
            else if (k == "large_screen") focusMode = v == "top" ? 1 : v == "bottom" ? 2 : 0;
            else if (k == "fullscreen") fullscreen = v == "1";
            else if (k == "smooth") smooth = v == "1";
            else if (k == "wide") wide = v == "1";
            else if (k == "wide_lines") wideLines = std::clamp(atoi(v.c_str()), 192, 2160);
            else if (k == "buttons") byLabel = v != "positions";
            else if (k == "online_server") onlineServer = v;
            else if (k == "online_name") onlineName = v.substr(0, 10);
            else if (k == "online_colour") onlineColour = std::clamp(atoi(v.c_str()), -1, 3);
            else if (k == "host_max") hostMax = std::clamp(atoi(v.c_str()), 2, 4);
            else if (k == "auto_host") autoHost = v == "1";
            else if (k == "friends") friends = v;
            else if (k == "player_id") playerId = v.size() == 16 ? v : "";
            else if (k == "online_partner") onlinePartner = std::clamp(atoi(v.c_str()), 0, 493);
            else if (k == "show_ping") showPing = v == "1";
            else if (k == "online_toasts") onlineToasts = v == "1";
            else if (k == "start_screen") startScreen = v == "1";
            else if (k == "autosave") autosave = v == "1";
            else if (k == "autosave_game") autosaveGame = v == "1";
            else if (k == "pad_map")
            {
                std::istringstream ss(v);
                for (int i = 0; i < 8 && ss; i++)
                {
                    int b = -1;
                    ss >> b;
                    padMap[i] = b >= 0 && b < PAD_COUNT ? b : -1;
                }
            }
            else if (k == "fps60") fps60 = v == "1";
            else if (k == "fps_unlimited") fpsUnlimited = v == "1";
            else if (k == "vsync") vsync = v == "1";
            else if (k == "pad_touch") padTouch = v == "1";
            else if (k == "show_fps") showFps = v == "1";
            else if (k == "void_fill") voidFill = v == "1";
            else if (k == "night_fx") nightFx = v == "1";
            else if (k == "redrawn_screens") redrawn = v == "1";
            else if (k == "music") newSongs = v != "original";
            else if (k == "menu_sounds") menuSounds = v == "1";
            else if (k == "text_font") textFont = v;
            else if (k == "blur") blur = std::clamp(atoi(v.c_str()), 0, 2);
            else if (k == "far_map") farMap = v == "1";
            else if (k == "zoom") zoom = std::clamp(atoi(v.c_str()), ZoomSteps[0], ZoomSteps[ZoomCount - 1]);
            else if (k == "window_height") windowH = std::clamp(atoi(v.c_str()), 0, 4320);
            else if (k == "aspect")
                for (int i = 1; i < AspectCount; i++)
                    if (v == AspectRatios[i].name) aspect = i;
        }
    }

    void Save(const std::string& path) const
    {
        std::ofstream f(path);
        f << "view=" << layout << "\n";
        f << "large_screen=" << (focusMode == 1 ? "top" : focusMode == 2 ? "bottom" : "auto") << "\n";
        f << "fullscreen=" << (fullscreen ? 1 : 0) << "\n";
        f << "smooth=" << (smooth ? 1 : 0) << "\n";
        f << "wide=" << (wide ? 1 : 0) << "\n";
        f << "wide_lines=" << wideLines << "\n";
        f << "buttons=" << (byLabel ? "labels" : "positions") << "\n";
        f << "online_server=" << onlineServer << "\n";
        f << "online_name=" << onlineName << "\n";
        f << "online_colour=" << onlineColour << "\n";
        f << "host_max=" << hostMax << "\n";
        f << "auto_host=" << (autoHost ? 1 : 0) << "\n";
        f << "online_partner=" << onlinePartner << "\n";
        f << "friends=" << friends << "\n";
        f << "player_id=" << playerId << "\n";
        f << "show_ping=" << (showPing ? 1 : 0) << "\n";
        f << "online_toasts=" << (onlineToasts ? 1 : 0) << "\n";
        f << "start_screen=" << (startScreen ? 1 : 0) << "\n";
        f << "autosave=" << (autosave ? 1 : 0) << "\n";
        f << "autosave_game=" << (autosaveGame ? 1 : 0) << "\n";
        f << "pad_map=";
        for (int i = 0; i < 8; i++) f << padMap[i] << (i < 7 ? " " : "\n");
        f << "fps60=" << (fps60 ? 1 : 0) << "\n";
        f << "fps_unlimited=" << (fpsUnlimited ? 1 : 0) << "\n";
        f << "vsync=" << (vsync ? 1 : 0) << "\n";
        f << "pad_touch=" << (padTouch ? 1 : 0) << "\n";
        f << "show_fps=" << (showFps ? 1 : 0) << "\n";
        f << "aspect=" << (aspect ? AspectRatios[aspect].name : "fill") << "\n";
        f << "zoom=" << zoom << "\n";
        f << "void_fill=" << (voidFill ? 1 : 0) << "\n";
        f << "night_fx=" << (nightFx ? 1 : 0) << "\n";
        f << "redrawn_screens=" << (redrawn ? 1 : 0) << "\n";
        f << "music=" << (newSongs ? "new" : "original") << "\n";
        f << "menu_sounds=" << (menuSounds ? 1 : 0) << "\n";
        f << "text_font=" << textFont << "\n";
        f << "blur=" << blur << "\n";
        f << "far_map=" << (farMap ? 1 : 0) << "\n";
        f << "window_height=" << windowH << "\n";
    }
};

static const char* LayoutName(LayoutMode m)
{
    switch (m)
    {
    case LayoutMode::Merged: return "merged";
    case LayoutMode::Hybrid: return "hybrid";
    case LayoutMode::Side: return "side";
    case LayoutMode::Vertical: return "vertical";
    case LayoutMode::Top: return "top";
    default: return "bottom";
    }
}

// Draws the overlay canvas over an ARGB image of the same size.
static void BlendCanvas(const Canvas& c, std::vector<uint32_t>& img)
{
    for (size_t i = 0; i < img.size() && i < c.px.size(); i++)
    {
        uint32_t o = c.px[i];
        uint32_t a = o >> 24;
        if (!a) continue;
        if (a == 255)
        {
            img[i] = o;
            continue;
        }
        uint32_t d = img[i];
        uint32_t r = (((o >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * (255 - a)) / 255;
        uint32_t g = (((o >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * (255 - a)) / 255;
        uint32_t b2 = ((o & 0xFF) * a + (d & 0xFF) * (255 - a)) / 255;
        img[i] = 0xFF000000 | (r << 16) | (g << 8) | b2;
    }
}

static bool WriteSnapshot(NDS& nds, const std::string& path, uint32_t frame)
{
    Savestate st;
    if (!nds.DoSavestate(&st) || st.Error) return false;
    st.Finish();
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(&frame, 4, 1, f);
    fwrite(st.Buffer(), 1, st.Length(), f);
    fclose(f);
    return true;
}

// Snapshot file: 4-byte frame number, then the core's savestate.
static bool ReadSnapshot(NDS& nds, const std::string& path, uint32_t* frame)
{
    std::vector<uint8_t> blob = ReadFile(path);
    if (blob.size() <= 4) return false;
    Savestate st(blob.data() + 4, (u32)blob.size() - 4, false);
    if (st.Error || !nds.DoSavestate(&st)) return false;
    if (frame) memcpy(frame, blob.data(), 4);
    rc_reset();
    return true;
}

#ifdef _WIN32
// From kernel32, declared here rather than including windows.h, whose macros clash.
extern "C" __declspec(dllimport) unsigned long __stdcall GetConsoleProcessList(unsigned long*, unsigned long);
extern "C" __declspec(dllimport) int __stdcall FreeConsole(void);
#endif

int main(int argc, char** argv)
{
    Options opt;
#ifdef __ANDROID__
    // Started from the app icon: the ROM is packed in the APK, the save and the settings
    // go in the app's own storage.
    std::vector<std::string> androidArgs;
    std::vector<char*> androidArgv;
    if (argc < 2)
    {
        const char* dir = SDL_AndroidGetInternalStoragePath();
        // Files the emulator core keeps by relative name go there too.
        if (dir) chdir(dir);
        // Messages go to log.txt (Android drops them otherwise), with the frame timing,
        // for bug reports: in the app's folder on the shared storage
        // (Android/data/io.github.clickingmaster.ssrecomp/files), which a file manager or
        // a PC over USB can open, or else beside the save.
        const char* ext = SDL_AndroidGetExternalStoragePath();
        if (!ext || !freopen((std::string(ext) + "/log.txt").c_str(), "w", stderr))
            freopen("log.txt", "w", stderr);
        setvbuf(stderr, nullptr, _IOLBF, 0);
        setenv("SSR_PERF", "1", 0);
        androidArgs = { "ssrecomp", "rom.nds", "--save", std::string(dir ? dir : ".") + "/soulsilver.sav", "--fullscreen" };
        for (auto& a : androidArgs) androidArgv.push_back(a.data());
        argc = (int)androidArgv.size();
        argv = androidArgv.data();
    }
#else
    // Double-clicked (johto-online.exe in build/): play like the setup program's Play button,
    // with the ROM it recorded, or else the first .nds in the folder above build/.
    std::vector<std::string> clickArgs;
    std::vector<char*> clickArgv;
    if (argc < 2)
    {
        namespace fs = std::filesystem;
        char* base = SDL_GetBasePath();
        fs::path here = base ? fs::u8path(base).parent_path().parent_path() : fs::current_path();
        SDL_free(base);
        std::string rom, save;
        std::ifstream note(here / (fs::exists(here / "johto-online.rom") ? "johto-online.rom" : "ssrecomp.rom"));
        if (std::getline(note, rom) && !rom.empty())
        {
            // The setup program writes it relative to its folder in portable mode.
            rom = (here / fs::u8path(rom)).u8string();
            // The save is kept in ROMS/ when the player keeps it there with the ROMs.
            save = ((fs::exists(here / "ROMS" / "soulsilver.sav") ? here / "ROMS" : here) / "soulsilver.sav").u8string();
        }
        else
        {
            rom.clear();
            std::error_code ec;
            for (auto& e : fs::directory_iterator(here, ec))
                if (e.path().extension() == ".nds") { rom = e.path().u8string(); break; }
        }
        if (!rom.empty())
        {
            fs::current_path(here);
            // There is no terminal to read the messages in: they go to game.log, for bug reports.
            freopen("game.log", "w", stderr);
#ifdef _WIN32
            // Windows opened a console window just for us (we are its only process):
            // close it, nothing is written there. Started from a terminal, keep it.
            unsigned long procs[2];
            if (GetConsoleProcessList(procs, 2) == 1) FreeConsole();
#endif
            clickArgs = { "ssrecomp", rom };
            if (!save.empty()) clickArgs.insert(clickArgs.end(), { "--save", save });
            for (auto& a : clickArgs) clickArgv.push_back(a.data());
            argc = (int)clickArgv.size();
            argv = clickArgv.data();
        }
    }
#endif
    if (!ParseArgs(argc, argv, opt))
    {
        Usage();
        return 2;
    }
    if (opt.save.empty()) opt.save = opt.rom + ".sav";

    std::vector<uint8_t> rom = ReadFile(opt.rom);
    if (rom.empty())
    {
        fprintf(stderr, "cannot read ROM: %s\n", opt.rom.c_str());
        return 1;
    }

    // The Wi-Fi Plaza as a map of the overworld (plazamap.h): data only, before the
    // consoles (both of --lockstep's) are made.
    const bool plazaMap = PlazaMap::Patch(rom);

    std::vector<uint8_t> sav = ReadFile(opt.save);
    fprintf(stderr, "ROM %s: %zu bytes; save %s: %zu bytes\n", opt.rom.c_str(), rom.size(), opt.save.c_str(), sav.size());
    if (!opt.party.empty() && opt.lockstep)
    {
        fprintf(stderr, "--party cannot be used with --lockstep\n");
        return 2;
    }
    auto nds = MakeConsole(rom, sav, opt.headless);
    if (!nds) return 1;
    Party_Attach(*nds);
    Party_SetRom(rom.data(), rom.size());
    RomArt romArt;
    romArt.SetRom(rom.data(), rom.size());
    HudBattle::Moves battleMoves;
    battleMoves.SetRom(rom.data(), rom.size());
    HudBattle::Hud battleHud;
    const HudOnline::IconFn monIcon = [&](int species) { return romArt.PokemonIcon(species); };
    Party_SetSave(sav.data(), sav.size());
    nds->ARM9.RecompEnable = opt.recomp;

    // Lockstep: a second console on the plain interpreter runs each frame first and
    // records the CPU state after every instruction; the recompiled console must match.
    std::unique_ptr<NDS> ref;
    if (opt.lockstep)
    {
        ref = MakeConsole(rom, sav, true);
        if (!ref) return 1;
        nds->ARM9.RecompEnable = true;
        rc_single = true;
        ref->ARM9.TraceHook = TraceRecord;
        nds->ARM9.TraceHook = TraceCompare;
    }

    std::vector<ScriptEvent> script;
    if (!opt.script.empty()) script = LoadScript(opt.script);

    // Function tracing: a probe on every known function.
    std::vector<std::pair<int, std::string>> traceProbes;
    if (!opt.traceFuncsAt.empty())
    {
        std::ifstream f(opt.symbols);
        std::string sec, name;
        unsigned addr, size;
        while (f >> sec >> std::hex >> addr >> size >> name)
        {
            int p = rc_add_probe(sec.c_str(), addr);
            if (p >= 0) traceProbes.emplace_back(p, sec + ":" + name);
        }
        fprintf(stderr, "[trace] %zu function probes\n", traceProbes.size());
    }

    uint32_t frame = 0;
    if (!opt.stateLoad.empty())
    {
        for (NDS* n : { ref.get(), nds.get() })
        {
            if (n && !ReadSnapshot(*n, opt.stateLoad, &frame))
            {
                fprintf(stderr, "cannot load snapshot: %s\n", opt.stateLoad.c_str());
                return 1;
            }
        }
    }
    // A snapshot carries the clock of when it was made; play goes on at the current time.
    if (!opt.headless || getenv("SSR_CLOCK")) SetClock(*nds, opt.headless);

    const size_t slash = opt.save.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? "." : opt.save.substr(0, slash);
    // Headless runs use the default settings unless a settings file is named (tests of
    // the zoom or the void fill name one).
    const bool configGiven = !opt.config.empty();
    if (opt.config.empty())
    {
        opt.config = dir + "/johto-online.cfg";
        // Settings from before the rename (ssrecomp.cfg) are moved over once.
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::u8path(opt.config), ec))
            std::filesystem::rename(std::filesystem::u8path(dir + "/ssrecomp.cfg"), std::filesystem::u8path(opt.config), ec);
    }
    const std::string quickState = opt.save + ".state";

    Settings cfg;
    if (!opt.headless || configGiven) cfg.Load(opt.config);
    // Online: who this player is (the trainer's name unless another was chosen), and a
    // party to join from the command line (the server is remembered for the menu).
    auto trainerName = [&]() {
        // The trainer's name in the save (general block, PlayerProfile at 0x64).
        for (size_t base : { (size_t)0, (size_t)0x40000 })
            if (base + 0x74 <= sav.size() && sav[base + 0x64] && sav[base + 0x64] != 0xFF)
            {
                uint16_t codes[8];
                memcpy(codes, &sav[base + 0x64], 16);
                const std::string n = NdsText(codes, 8);
                if (!n.empty()) return n;
            }
        return std::string("Trainer");
    };
    auto onlineName = [&]() { return cfg.onlineName.empty() ? trainerName() : cfg.onlineName; };
    if (cfg.playerId.empty())
    {
        std::random_device rd;
        for (int i = 0; i < 16; i++) cfg.playerId += "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"[rd() % 32];
    }
    Party_SetId(cfg.playerId);
    Party_SetProfile(onlineName(), cfg.onlineColour);
    Party_SetPartner(cfg.onlinePartner);
    if (!opt.party.empty())
    {
        if (!opt.headless || configGiven) cfg.onlineServer = opt.party;
        Party_Join(opt.party, opt.partyCode);
    }
    if (opt.layoutGiven || opt.headless) cfg.layout = opt.layout;
    if (opt.fullscreen) cfg.fullscreen = true;
    if (!opt.autoFocus) cfg.focusMode = 1;
    if (opt.headless) cfg.fps60 = false; // tests compare images with the plain renderer unless asked
    if (opt.interp60 >= 0) cfg.fps60 = opt.interp60 == 1;
    nds->GPU.GPU3D.InterpolationEnabled = cfg.fps60 && !opt.lockstep;
    // The DS-resolution 3D picture is drawn on its own thread, alongside the CPU emulation.
    if (!getenv("SSR_NO_RENDER_THREAD"))
        static_cast<melonDS::SoftRenderer&>(nds->GPU.GetRenderer3D()).SetThreaded(true, nds->GPU);

    // SSR_PERF=1 prints where the time of a frame goes, averaged over 300 frames.
    const bool perfOn = getenv("SSR_PERF") != nullptr;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* tex[2] = { nullptr, nullptr };
    SDL_Texture* overlayTex = nullptr;
    SDL_Texture* tagTex = nullptr; // tagCanvas
    SDL_AudioDeviceID audio = 0;
    Pads pads;
    Layout layout = LayoutFromName(cfg.layout);
    AutoFocus focus;
    focus.mode = cfg.focusMode;
    focus.manualFlip = opt.swapped;
    focus.Init();
    layout.swapped = focus.Swapped();

    auto applySmooth = [&]() {
        for (int i = 0; i < 2; i++)
            if (tex[i]) SDL_SetTextureScaleMode(tex[i], cfg.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    };
    // VSync: presenting a picture waits for the display's refresh. With Frame rate Unlimited
    // that paces the pictures between the game's frames to the display; the game's own frames
    // keep their clock (a present waits at most one refresh).
    auto applyVSync = [&]() {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        if (renderer && SDL_RenderSetVSync(renderer, cfg.vsync ? 1 : 0) != 0)
            fprintf(stderr, "VSync %s: %s\n", cfg.vsync ? "on" : "off", SDL_GetError());
#endif
    };

    if (!opt.headless)
    {
#ifdef __ANDROID__
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
        // Fingers are handled one by one (on-screen controls and the touch screen).
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        // Tilting the phone must not move the player.
        SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
        // Controllers come through Android's own input, which knows every gamepad. With SDL's
        // HIDAPI drivers on, a pad one of them claims (a Backbone One, a PlayStation pad) is
        // left out of Android's list, and is lost if SDL cannot open it over USB itself.
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
#endif
        // The controller keeps working while another window has the focus (on Windows SDL
        // otherwise ignores it then, as when the game was started from the setup window).
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
        {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        int w, h;
        LayoutNativeSize(layout, w, h);
        // The single-screen view opens wide (16:9 unless a shape is chosen), so that the
        // widescreen world is seen without resizing the window first.
        float ratio = (float)w / h;
        if (layout.mode == LayoutMode::Merged) ratio = cfg.aspect ? AspectRatios[cfg.aspect].ratio : 16 / 9.0f;
        const int winH = cfg.windowH > 0 ? cfg.windowH : h * opt.scale;
        const int winW = (int)(winH * ratio + 0.5f);
        window = SDL_CreateWindow("Johto Online", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            winW, winH,
            SDL_WINDOW_RESIZABLE |
#ifdef __APPLE__
            SDL_WINDOW_ALLOW_HIGHDPI |
#endif
            (cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        // The OpenGL ES renderer uploads the picture several times faster than SDL's default
        // (desktop OpenGL) on the machines this was measured on. SDL_RENDER_DRIVER overrides.
        // Not on Windows: there OpenGL ES can come with a framebuffer that converts to sRGB,
        // which washes the colours out (seen on AMD), and SDL's default is Direct3D anyway.
#if defined(__APPLE__)
        if (!getenv("SDL_RENDER_DRIVER")) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
#elif !defined(_WIN32)
        if (!getenv("SDL_RENDER_DRIVER")) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
#endif
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer)
        {
            fprintf(stderr, "SDL accelerated renderer failed: %s; trying fallback\n", SDL_GetError());
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
            renderer = SDL_CreateRenderer(window, -1, 0);
        }
        if (!window || !renderer)
        {
            fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
            return 1;
        }
        {
            // In every log: bug reports about the picture need to know this.
            SDL_RendererInfo info;
            if (SDL_GetRendererInfo(renderer, &info) == 0)
                fprintf(stderr, "SDL renderer %s, video driver %s\n", info.name, SDL_GetCurrentVideoDriver());
        }
        for (int i = 0; i < 2; i++)
            tex[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);
        applySmooth();
        applyVSync();
        SDL_ShowCursor(cfg.fullscreen ? SDL_DISABLE : SDL_ENABLE);

        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = 48000;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        audio = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (audio) SDL_PauseAudioDevice(audio, 0);

        // For bug reports about controllers: what SDL sees, and what may hide a device from it
        // (Steam sets these variables for the games it starts; a container may lack udev).
        SDL_version sdlv;
        SDL_GetVersion(&sdlv);
        fprintf(stderr, "SDL %d.%d.%d, %d joystick device(s)\n", sdlv.major, sdlv.minor, sdlv.patch, SDL_NumJoysticks());
#ifdef __linux__
        if (access("/run/.containerenv", F_OK) == 0 || access("/.dockerenv", F_OK) == 0)
            fprintf(stderr, "running in a container\n");
#endif
        for (const char* var : {"SteamGameId", "SDL_GAMECONTROLLER_IGNORE_DEVICES", "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT",
                                "SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "SDL_JOYSTICK_DISABLE_UDEV", "SDL_GAMECONTROLLERCONFIG"})
            if (const char* v = getenv(var))
                fprintf(stderr, "  %s=%.200s\n", var, v);
        for (int i = 0; i < SDL_NumJoysticks(); i++)
        {
            const char* name = SDL_JoystickNameForIndex(i);
            fprintf(stderr, "  device %d: %s, %04x:%04x, %s\n", i, name ? name : "?", SDL_JoystickGetDeviceVendor(i),
                SDL_JoystickGetDeviceProduct(i), SDL_IsGameController(i) ? "known layout" : "no known layout");
            pads.Open(i);
        }
    }

    // Functions whose activity selects the merged scene.
    auto focusProbe = [&](const char* name) {
        for (unsigned i = 0; i < rc_num_focus_syms; i++)
            if (!strcmp(rc_focus_syms[i].name, name))
                return rc_add_probe(rc_focus_syms[i].section, rc_focus_syms[i].execAddr);
        return -1;
    };
    const int probeStartMenu = focusProbe("Task_StartMenu");
    const int probeStartMenuWait = focusProbe("Task_StartMenu_WaitApp");
    const int probeBattleInput = focusProbe("BattleInput_CheckTouch");
    const int probeSave[] = { focusProbe("sub_0203D264"), focusProbe("sub_0203D288"), focusProbe("sub_0203D2CC"),
        focusProbe("sub_0203D2EC"), focusProbe("sub_0203D304"), focusProbe("TouchSaveApp_HandleSaveConfirmation") };
    const int probeChoice[] = { focusProbe("ov27_0225C434") };
    const int probeMart = focusProbe("ov31_0225D7A0");
    const int probeTouch[] = { focusProbe("TouchscreenHitbox_FindRectAtTouchHeld"), focusProbe("TouchscreenHitbox_FindRectAtTouchNew"),
        focusProbe("TouchscreenHitbox_FindHitboxAtTouchNew"), focusProbe("TouchscreenHitbox_FindHitboxAtTouchHeld"),
        focusProbe("TouchscreenHitbox_TouchHeldIsIn"), focusProbe("TouchscreenHitbox_TouchNewIsIn") };
    const int probeFieldMap = focusProbe("FieldMap_Main");
    // Writing the save: for the seconds this takes the game sits in its flash write loop
    // and the code of the screen it shows (the save question, the overworld) runs only now
    // and then, so nothing on screen changes but the probes go quiet.
    const int probeSaveWrite[] = { focusProbe("HandleWriteSaveAsync_NormalData"), focusProbe("HandleWriteSaveAsync_PCBoxes") };
    auto probeActive = [&](int probe) {
        unsigned last = rc_probe_last_frame(probe);
        return last && last + 3 >= rc_current_frame();
    };
    // The battle asked for touch-screen input within the last few frames (it stops for a
    // moment between two menus).
    auto battleInputRecent = [&]() {
        unsigned last = rc_probe_last_frame(probeBattleInput);
        return last && last + 20 >= rc_current_frame();
    };
    auto saveWriting = [&]() {
        for (int probe : probeSaveWrite)
        {
            unsigned last = rc_probe_last_frame(probe);
            if (last && last + 8 >= rc_current_frame()) return true;
        }
        return false;
    };
    // Whether the overworld was running when the save write began: it counts as running
    // until the write ends, so the widescreen picture does not drop out meanwhile.
    bool fieldHeldBySave = false;
    // The overworld's main function is not entered for stretches of about ten frames while
    // some scripts run (a nurse bowing, for one), so it gets a longer memory than the rest:
    // otherwise the widescreen picture and the menu layout drop out for those frames.
    // It also counts as running while the game itself says so: its map is set up and nothing
    // has taken its place (FieldSystem.runningFieldMap, a field process, no application over
    // it). That covers the longer stretches without FieldMap_Main, such as the fade-in after
    // Continue or a battle, which otherwise showed the plain DS picture and then jumped to the
    // zoomed wide one. The field overlay must be running as well, so a FieldSystem left behind
    // after a return to the title screen does not count.
    auto fieldRunning = [&]() {
        auto rd = [&](u32 a) -> u32 { return (a >> 24) == 0x02 ? *(u32*)&nds->MainRAM[a & nds->MainRAMMask & ~3u] : 0; };
        const u32 fs = rd(0x021D4158), pm = rd(fs);
        if (!pm || !rd(pm) || rd(pm + 4) || !rd(fs + 0x6C)) return false;
        for (u32 i = 0; i < rc_num_sections; i++)
            if (!strcmp(rc_sections[i].name, "field"))
                return rc_section_last_frame[i] && rc_section_last_frame[i] + 3 >= rc_current_frame();
        return false;
    };
    auto fieldActive = [&]() {
        unsigned last = rc_probe_last_frame(probeFieldMap);
        return (last && last + 16 >= rc_current_frame()) || fieldHeldBySave || fieldRunning();
    };
    // Coming back from the Bag, the Pokegear and so on, the game fades the overworld in with
    // the X menu already drawn, a little before the menu's task runs again. The strip it
    // puts along the bottom of the top screen gives that away; while this counts down, the
    // menu layout is used although the task's probe is still silent.
    int menuReturn = 0;
    // Frames in a row in which the battle's touch screen showed something that is neither
    // of the menus with a layout of their own nor the idle backdrop.
    int battleUnknown = 0;
    // The frame until which the battle strip stays up while its buttons are not recognised:
    // between the commands and the moves the game slides one set out and the other in, and
    // the strip would otherwise vanish for those frames, uncovering the message box.
    uint32_t battleStripUntil = 0;
    // The button chosen in a battle strip (see the input handling before the frame runs).
    int battleSel = 0;
    // The strip `battleSel` belongs to (its scene's name); on a new strip it starts again at 0.
    std::string battleStripKind;
    // Whether the HUD's sprites loaded: the redrawn battle menus (hudbattle.h) need them.
    bool battleHudOk = false;
    // Per application, the overlay that holds its code when no other application's code is
    // there (-1 otherwise), and per section the frames on which it has run since it started
    // running (see where it is counted).
    std::vector<int> appOwnSection(rc_num_apps, -1);
    std::vector<unsigned> sectionStreak(rc_num_sections, 0);
    for (unsigned i = 0; i < rc_num_apps; i++)
    {
        if (!strcmp(rc_apps[i].section, "main") || !strcmp(rc_apps[i].section, "field")) continue;
        unsigned sharing = 0;
        for (unsigned j = 0; j < rc_num_apps; j++)
            if (!strcmp(rc_apps[j].section, rc_apps[i].section)) sharing++;
        if (sharing != 1) continue;
        for (u32 sec = 0; sec < rc_num_sections; sec++)
            if (!strcmp(rc_sections[sec].name, rc_apps[i].section)) appOwnSection[i] = (int)sec;
    }
    // Probes per game application, to know which one is running: on its per-frame function
    // and on the functions that start and end it, which run every frame while it loads and
    // fades in and while it fades out. Its layout then stays from the first frame to the
    // last instead of other layouts showing in between.
    std::vector<std::pair<const char*, int>> appProbes;
    std::vector<std::pair<const char*, int>> appMainProbes; // the per-frame functions alone
    for (unsigned i = 0; i < rc_num_apps; i++)
    {
        appProbes.emplace_back(rc_apps[i].name, rc_add_probe(rc_apps[i].section, rc_apps[i].execAddr));
        appMainProbes.push_back(appProbes.back());
        if (rc_apps[i].initAddr)
            appProbes.emplace_back(rc_apps[i].name, rc_add_probe(rc_apps[i].initSection, rc_apps[i].initAddr));
        if (rc_apps[i].exitAddr)
            appProbes.emplace_back(rc_apps[i].name, rc_add_probe(rc_apps[i].exitSection, rc_apps[i].exitAddr));
    }
    // The application's per-frame function alone, without its start and end: what the test
    // options that wait for a battle (--battle-mash, --nav-stop-on-battle) go by.
    auto appRunning = [&](const char* name) {
        for (auto& ap : appMainProbes)
            if (!strcmp(ap.first, name))
            {
                unsigned last = rc_probe_last_frame(ap.second);
                if (last && last + 6 >= rc_current_frame()) return true;
            }
        return false;
    };
    auto appActive = [&](const char* name) {
        for (auto& ap : appProbes)
            if (!strcmp(ap.first, name))
            {
                unsigned last = rc_probe_last_frame(ap.second);
                if (last && last + 6 >= rc_current_frame()) return true;
            }
        // Opened from the start menu, an application is prepared by the field with code of
        // its own overlay for a while before its start function runs: that counts too, when
        // the overlay is that application's alone and has kept running for a while
        // (the start menu calls into some overlays once in passing).
        const unsigned waitLast = rc_probe_last_frame(probeStartMenuWait);
        if (waitLast && waitLast + 3 >= rc_current_frame())
            for (unsigned i = 0; i < rc_num_apps; i++)
                if (!strcmp(rc_apps[i].name, name) && appOwnSection[i] >= 0 && sectionStreak[appOwnSection[i]] >= 8)
                    return true;
        return false;
    };

    auto sectionActive = [&](const char* name) {
        for (u32 i = 0; i < rc_num_sections; i++)
            if (!strcmp(rc_sections[i].name, name))
                return rc_section_last_frame[i] && rc_section_last_frame[i] + 3 >= rc_current_frame();
        return false;
    };

    // Whether an application driven from the bottom screen is starting, running or ending
    // (worked out each frame before the scene is chosen).
    bool appBottomNow = false;
    // On the Wi-Fi Plaza (plazamap.h), whose room only the widescreen renderer draws: it is
    // used there at any width and with the widescreen setting off.
    bool onPlaza = false;
    // Chooses what the single merged view shows this frame.
    auto pickScene = [&](bool cursorInUse, float viewWidth, const ScreenImage* screens) {
        Scene sc;
        auto only = [&](const char* name, int screen) {
            sc.name = name;
            sc.items = { FullScreen(screen) };
        };
        auto startMenu = [&]() {
            only("start-menu", 0);
            // On a wide view the panel stands at the right edge, mostly beside the overworld.
            const float menuScale = viewWidth >= 300 ? 0.8f : 0.75f;
            const float menuX = viewWidth >= 300 ? 128 + viewWidth / 2 - 161 * menuScale - 2 : 131;
            sc.items.push_back({ 1, 4, 0, 161, 192, menuX, (192 - 192 * menuScale) / 2, menuScale, 0, 255, false });
            sc.items.back().border = !AutoFocus::Blank(screens[1].pixels);
        };

        // The safety net below must not flash up for the few frames in which the field map is
        // not running during a door transition, so its condition has to hold for a while.
        static int spreadFrames = 0;
        if (std::any_of(std::begin(probeTouch), std::end(probeTouch), probeActive) && !fieldActive() &&
            !appActive("Battle") && !appActive("TitleScreen") && !appActive("IntroMovie") && !appActive("Credits"))
            spreadFrames++;
        else
            spreadFrames = 0;
        if (focus.manualFlip)
            only("manual", focus.wantBottom ? 0 : 1);
        else if (appActive("PokemonSummary"))
        {
            // The page's card is one tall card split across the screens; its two halves stand
            // side by side after the column with the page's name and the Pokemon's name,
            // picture and item, on the page's colour. The page tabs and the party grid are
            // left out; the D-pad does both. The game frames the card on its right only (it
            // runs off the screen on the left), so the halves are cut from inside that frame
            // and each gets an even frame of its own, with what is drawn on it centred (the
            // moves list, for one, stands left of the middle).
            const uint32_t page = screens[0].pixels[100 * 256 + 1] & 0xFFFFFF;
            const uint32_t frame = 0x414141;
            Element side = Crop(0, 152, 8, 104, 184), upper = Crop(0, 0, 22, 147, 170), lower = Crop(1, 0, 0, 147, 160);
            // The lower half keeps the EXP bar, which overlaps the frame's bottom edge: the
            // edge's shading and its rounded corner are left out around it.
            lower.keys[0] = screens[1].pixels[158 * 256 + 1] & 0xFFFFFF;
            lower.keyRing = 2;
            lower.cutX = 143;
            lower.cutY = 154;
            const float pad = 4, gap = 6, margin = 6, h = 189;
            const int cardW = upper.sw;
            const float w = side.sw + 2 * (cardW + 2 * pad) + 2 * gap;
            const float scale = std::min((192 - 2 * margin) / h, (viewWidth - 2 * margin) / w);
            float x = 128 - w * scale / 2;
            const float y = 96 - h * scale / 2;
            sc.name = "summary";
            {
                // The name and item boxes run off the screen at the right, and the item box
                // off its bottom too: they are closed with mirrored copies of their left and
                // top edges, and the column's shaded strip at the right is painted over
                // beside them, as is the header's shaded end.
                const uint32_t* px = screens[0].pixels;
                const uint32_t column = px[100 * 256 + 154] & 0xFFFFFF;
                Element back = FillRect(x, y, side.sw, (int)h, column);
                back.scale = scale;
                back.border = true;
                back.borderColor = frame;
                back.borderRadius = 3;
                sc.items.push_back(back);
                auto at = [&](Element e, int sx, int sy) {
                    e.scale = scale;
                    e.dx = x + (sx - 152) * scale;
                    e.dy = y + (sy - 8) * scale;
                    sc.items.push_back(e);
                };
                at(side, 152, 8);
                at(FillRect(0, 0, 10, 16, px[15 * 256 + 153] & 0xFFFFFF), 246, 8);
                auto inBox = [&](int sy) {
                    for (int sx = 156; sx <= 158; sx++)
                        if ((px[sy * 256 + sx] & 0xFFFFFF) == frame) return true;
                    return false;
                };
                for (int sy = 26; sy < 192;)
                {
                    int end = sy;
                    while (end < 192 && inBox(end) == inBox(sy)) end++;
                    if (!inBox(sy)) at(FillRect(0, 0, 4, end - sy, column), 246, sy);
                    sy = end;
                }
                Element right = Crop(0, 152, 26, 6, 166);
                right.flipX = true;
                at(right, 250, 26);
                Element bottom = Crop(0, 152, 156, 98, 2);
                bottom.flipY = true;
                at(bottom, 152, 192);
                Element corner = Crop(0, 152, 156, 6, 2);
                corner.flipX = corner.flipY = true;
                at(corner, 250, 192);
            }
            x += (side.sw + gap) * scale;
            for (Element* e : { &upper, &lower })
            {
                Element back = FillRect(x, y, cardW + 2 * (int)pad, (int)h, page);
                back.scale = scale;
                back.border = true;
                back.borderColor = frame;
                back.borderRadius = 3;
                sc.items.push_back(back);
                // What is drawn on the card comes in blocks, one above the other with the page's
                // colour between them; each block is centred across the panel, and all of them
                // together top to bottom.
                const uint32_t* px = screens[e->screen].pixels;
                const uint32_t edge = e->keys[0];
                auto drawn = [&](int sx, int sy) {
                    const uint32_t c = px[sy * 256 + sx] & 0xFFFFFF;
                    return c != page && !(sy >= e->sy + e->sh - e->keyRing && c == edge) &&
                        (sx < e->cutX || sy < e->cutY);
                };
                struct Block { int y0, y1, x0, x1; };
                std::vector<Block> blocks;
                int blank = 2;
                for (int sy = e->sy; sy < e->sy + e->sh; sy++)
                {
                    int r0 = cardW, r1 = -1;
                    for (int sx = 0; sx < cardW; sx++)
                        if (drawn(sx, sy))
                        {
                            r0 = std::min(r0, sx);
                            r1 = sx;
                        }
                    if (r1 < 0)
                    {
                        blank++;
                        continue;
                    }
                    if (blank >= 2 || blocks.empty())
                        blocks.push_back({ sy, sy, r0, r1 });
                    Block& bl = blocks.back();
                    bl.y1 = sy;
                    bl.x0 = std::min(bl.x0, r0);
                    bl.x1 = std::max(bl.x1, r1);
                    blank = 0;
                }
                if (!blocks.empty())
                {
                    const float top = y + (pad + (h - 2 * pad - (blocks.back().y1 + 1 - blocks[0].y0)) / 2.0f) * scale;
                    for (const Block& bl : blocks)
                    {
                        Element piece = *e;
                        piece.sx = bl.x0;
                        piece.sw = bl.x1 - bl.x0 + 1;
                        piece.sy = bl.y0;
                        piece.sh = bl.y1 - bl.y0 + 1;
                        piece.scale = scale;
                        piece.dx = x + (pad + (cardW - piece.sw) / 2.0f) * scale;
                        piece.dy = top + (bl.y0 - blocks[0].y0) * scale;
                        sc.items.push_back(piece);
                        // A box the game runs off the screen or into the card's frame (the
                        // ability, the number of ribbons) gets sides of its own.
                        if (bl.x0 == 0 || bl.x1 == cardW - 1)
                            for (float bx : { piece.dx - 2 * scale, piece.dx + piece.sw * scale })
                            {
                                Element side = FillRect(bx, piece.dy, 2, piece.sh, frame);
                                side.scale = scale;
                                sc.items.push_back(side);
                            }
                    }
                }
                x += (cardW + 2 * pad + gap) * scale;
            }
            sc.bgScreen = 0;
            sc.bgX = 1;
            sc.bgY = 100;
        }
        else if (appActive("Bag"))
            // Just the bag screen; the description box on the other screen is left out.
            sc = Stack("bag", { Crop(1, 0, 0, 256, 192) }, false, viewWidth);
        else if (appActive("PCBox"))
            // The selected Pokemon's info card from the other screen beside the box.
        {
            sc = SideBySide("pc-box", Crop(0, 0, 34, 216, 156), Crop(1, 0, 0, 256, 192), viewWidth);
            sc.bgScreen = 0;
            sc.bgX = 236;
            sc.bgY = 100;
        }
        else if (appActive("Pokedex"))
        {
            // Every Pokedex page fills both screens, so they are joined into one page: as one
            // tall picture for the closed cover (red at both left edges), otherwise as a spread
            // with the entry on the left and the list or controls on the right.
            auto red = [&](int screen) {
                uint32_t c = screens[screen].pixels[100 * 256 + 10];
                return ((c >> 16) & 0xFF) > 180 && ((c >> 8) & 0xFF) < 100 && (c & 0xFF) < 100;
            };
            bool cover = red(0) && red(1);
            uint32_t title = screens[0].pixels[8 * 256 + 200];
            auto light = [](uint32_t c) { return ((c >> 16) & 0xFF) > 235 && ((c >> 8) & 0xFF) > 235 && (c & 0xFF) > 235; };
            // The list page has a red title bar and, once the cover has slid away, bare paper
            // between the picture and the description.
            int bare = 0;
            for (int y = 127; y < 131; y++)
                for (int x = 4; x < 96; x++) bare += light(screens[0].pixels[y * 256 + x]);
            bool listPage = ((title >> 16) & 0xFF) > 200 && ((title >> 8) & 0xFF) < 90 && (title & 0xFF) < 90 && bare > 280;
            // The detail pages have a coloured title bar on the top screen and a blue tab bar
            // at the foot of the bottom screen.
            uint32_t tab = screens[1].pixels[176 * 256 + 63];
            bool tabPage = !light(title) && (tab & 0xFF) > 150 && ((tab >> 16) & 0xFF) < 120 && !listPage &&
                (((title >> 16) & 0xFF) < 200 || ((title >> 8) & 0xFF) > 90);
            auto put = [&](Element e, float x, float y, float scale) {
                e.dx = x;
                e.dy = y;
                e.scale = scale;
                sc.items.push_back(e);
            };
            const float W = viewWidth, L = 128 - W / 2;
            if (cover || W < 300)
                sc = Stack("pokedex", { Crop(0, 0, 0, 256, 192), Crop(1, 0, 0, 256, 192) }, !cover, viewWidth);
            else if (listPage)
            {
                // The list as one page: the pieces of the entry on the top screen are laid out
                // again around the number grid. A title bar runs across the top; on the left
                // are the name, the picture and the type and size card; on the right the
                // description sits above the grid and its buttons.
                sc.name = "pokedex-list";
                sc.items.clear();
                // The page is drawn on plain paper: the two most common colours in an empty
                // strip of the top screen are the paper and its ruling, and the ruling is
                // left out of the rearranged pieces so that they do not show as patches.
                uint32_t paper = 0;
                std::vector<std::pair<int, uint32_t>> ruling;
                {
                    // Sampled from the empty area between the picture and the description.
                    std::map<uint32_t, int> count;
                    for (int y = 113; y < 131; y++)
                        for (int x = 4; x < 96; x++) count[screens[0].pixels[y * 256 + x] & 0xFFFFFF]++;
                    int best = 0;
                    for (auto& c : count)
                        if (c.second > best)
                        {
                            best = c.second;
                            paper = c.first;
                        }
                    uint32_t line = paper;
                    int lineCount = 0;
                    for (auto& c : count)
                        if (c.first != paper && c.second > lineCount)
                        {
                            lineCount = c.second;
                            line = c.first;
                        }
                    // The ruling also has lighter greys where lines cross; those are between
                    // the line colour and the paper, and losing one inside a picture would
                    // hardly show.
                    count.clear();
                    for (int y = 24; y < 126; y++)
                        for (int x = 4; x < 100; x++) count[screens[0].pixels[y * 256 + x] & 0xFFFFFF]++;
                    for (auto& c : count)
                    {
                        uint32_t r = (c.first >> 16) & 0xFF, g = (c.first >> 8) & 0xFF, bl = c.first & 0xFF;
                        if (r == g && g == bl && r > (line & 0xFF) && r < (paper & 0xFF)) ruling.push_back({ -c.second, c.first });
                    }
                    std::sort(ruling.begin(), ruling.end());
                    if (ruling.size() > 3) ruling.resize(3);
                    if (line != paper) ruling.push_back({ 0, line });
                    sc.bgScreen = 0;
                    for (int i = 127 * 256 + 4; i < 131 * 256; i++)
                        if ((screens[0].pixels[i] & 0xFFFFFF) == paper)
                        {
                            sc.bgX = i % 256;
                            sc.bgY = i / 256;
                            break;
                        }
                }
                auto putKeyed = [&](Element e, float x, float y, float scale) {
                    for (size_t k = 0; k < 4 && k < ruling.size(); k++) e.keys[k] = ruling[k].second;
                    put(e, x, y, scale);
                };
                const float t = 0.77f, barH = 17 * t;
                for (float x = L; x < L + W; x += 100 * t) put(Crop(0, 140, 0, 100, 17), x, 0, t);
                put(Crop(0, 0, 0, 80, 17), L, 0, t);
                const float leftW = W * 0.43f, rightW = W - leftW, rx = L + leftW;
                const float nameW = 156 * t;
                putKeyed(Crop(0, 100, 21, 156, 39), L + (leftW - nameW) / 2, barH + 3, t);
                const float statsY = 192 - 64 * t - 2;
                putKeyed(Crop(0, 100, 62, 156, 64), L + (leftW - nameW) / 2, statsY, t);
                const float picTop = barH + 3 + 39 * t, picS = std::min((statsY - picTop) / 102, leftW / 96);
                putKeyed(Crop(0, 4, 24, 96, 102), L + (leftW - 96 * picS) / 2, picTop + (statsY - picTop - 102 * picS) / 2, picS);
                const float descS = std::min(t, (rightW - 4) / 248), descH = 56 * descS;
                putKeyed(Crop(0, 4, 132, 248, 56), rx + (rightW - 248 * descS) / 2, barH + 2, descS);
                const float gridTop = barH + 2 + descH + 1, gridS = std::min((192 - gridTop) / 192, rightW / 256);
                put(Crop(1, 0, 0, 256, 192), rx + (rightW - 256 * gridS) / 2, 192 - 192 * gridS, gridS);
            }
            else if (!tabPage)
            {
                // Search and anything else: both screens as the two columns of a page.
                sc = Stack("pokedex", { Crop(0, 0, 0, 256, 192), Crop(1, 0, 0, 256, 192) }, true, viewWidth);
                if (light(screens[0].pixels[2 * 256 + 250]))
                {
                    sc.bgScreen = 0;
                    sc.bgX = 250;
                    sc.bgY = 2;
                }
            }
            else
            {
                // The other pages (area, size, forms): the title bar of the top screen and the
                // tab bar of the bottom screen frame the whole view, with the two bodies as
                // the columns of one page between them.
                sc.name = "pokedex-page";
                sc.items.clear();
                for (int i = 24 * 256; i < 192 * 256; i++)
                {
                    uint32_t c = screens[0].pixels[i];
                    if (((c >> 16) & 0xFF) > 235 && ((c >> 8) & 0xFF) > 235 && (c & 0xFF) > 235)
                    {
                        sc.bgScreen = 0;
                        sc.bgX = i % 256;
                        sc.bgY = i / 256;
                        break;
                    }
                }
                const float headH = 18, footH = 28, room = 192 - headH - footH;
                const float b = std::min(W / 512, room / 170);
                put(Crop(0, 0, 21, 256, 169), L + (W / 2 - 256 * b) / 2, headH + (room - 169 * b) / 2, b);
                put(Crop(1, 0, 0, 256, 162), 128 + (W / 2 - 256 * b) / 2, headH + (room - 162 * b) / 2, b);
                for (float x = L + 200; x < L + W; x += 40) put(Crop(0, 100, 0, 40, 18), x, 0, 1);
                put(Crop(0, 0, 0, 220, 18), L, 0, 1);
                for (float x = L; x < L + W; x += 6) put(Crop(1, 61, 162, 6, 28), x, 192 - footH, 1);
                put(Crop(1, 0, 162, 256, 28), 0, 192 - footH, 1);
            }
        }
        else if (appActive("GearMap") || appActive("TownMap"))
        {
            // The location's name and description from the other screen beside the map.
            sc = SideBySide("map", Crop(0, 0, 64, 256, 128), Crop(1, 0, 0, 256, 192), viewWidth);
            sc.bgScreen = 0;
            sc.bgX = 128;
            sc.bgY = 58;
        }
        else if (appActive("GearPhone") && screens[1].flags &&
            LayerClass(screens[1].flags[170 * 256 + 30]) == LAYER_OBJ)
            // A call in progress (the contact list is inactive and a sprite panel covers its
            // tab bar): the caller's words are in a box on the top screen.
            only("call", 0);
        else if (appActive("GearPhone"))
            // The clock and call text from the other screen above the contact list.
            sc = Stack("phone", { Crop(0, 0, 16, 256, 58), Crop(1, 0, 0, 256, 192) }, false, viewWidth);
        else if (appActive("ChooseStarter"))
        {
            // The Pokemon preview and the professor's text, with the Poke Balls from the
            // other screen inset in a corner.
            only("starter", 0);
            Element balls = Crop(1, 0, 0, 256, 150);
            if (viewWidth >= 300)
            {
                // Wide view: the preview moves to the left edge and the tray of Poke Balls
                // stands in the space that frees up on the right.
                const float L = 128 - viewWidth / 2, R = 128 + viewWidth / 2;
                sc.items[0].dx = L;
                balls.scale = std::min(0.62f, (viewWidth - 256 + 36) / 256);
                balls.dx = R - 256 * balls.scale;
                balls.dy = 72 - 75 * balls.scale;
            }
            else
            {
                balls.dx = 3;
                balls.dy = 3;
                balls.scale = 0.42f;
            }
            balls.border = viewWidth >= 300 && !AutoFocus::Blank(screens[1].pixels);
            sc.items.push_back(balls);
        }
        else if (appActive("NamingScreen"))
            only("naming", 1);
        else if (appActive("OakSpeech") && AutoFocus::Blank(screens[0].pixels) && !AutoFocus::Blank(screens[1].pixels))
            // Pages of the intro that use the bottom screen alone.
            only("bottom", 1);
        else if (appActive("OakSpeech"))
        {
            // The professor on the top screen; choices from the bottom screen (its backdrop art
            // dropped) are placed where they hide the least text.
            only("intro", 0);
            Element el;
            const uint8_t drop = LAYER_BG3 | LAYER_BACKDROP;
            if (viewWidth >= 300)
            {
                // Wide view: the professor's screen moves to the left edge and its backdrop,
                // which is the same all the way across, is continued to the right edge. The
                // choices from the bottom screen (buttons, the boy and the girl) stand in
                // that extra space instead of on top of the professor.
                const float L = 128 - viewWidth / 2, R = 128 + viewWidth / 2, free = viewWidth - 256;
                // Everything on the bottom screen that is not its plain background colour is a
                // choice: buttons, or the cards of the boy and the girl. The cards and the red
                // frame around the selected one are part of the background layer, so the
                // pieces are found and cut out by colour rather than by layer.
                const uint32_t plain = screens[1].pixels[96 * 256 + 2] & 0xFFFFFF;
                // A faint Poke Ball is sometimes drawn into that background in nearly the
                // same blue; it counts as background too.
                auto nearPlain = [&](uint32_t c) {
                    return std::abs((int)((c >> 16) & 0xFF) - (int)((plain >> 16) & 0xFF)) <= 56 &&
                        std::abs((int)((c >> 8) & 0xFF) - (int)((plain >> 8) & 0xFF)) <= 56 &&
                        std::abs((int)(c & 0xFF) - (int)(plain & 0xFF)) <= 56;
                };
                int bx0 = 256, by0 = 192, bx1 = -1, by1 = -1;
                for (int y = 8; y < 184; y++)
                    for (int x = 0; x < 256; x++)
                        if (!nearPlain(screens[1].pixels[y * 256 + x] & 0xFFFFFF))
                        {
                            bx0 = std::min(bx0, x);
                            bx1 = std::max(bx1, x);
                            by0 = std::min(by0, y);
                            by1 = std::max(by1, y);
                        }
                // Alone in the lower right corner there is only a "touch to continue" hint.
                const bool found = bx1 >= bx0 && bx1 - bx0 < 250 && !(bx0 >= 144 && by0 >= 128);
                // The box around the choices only ever grows while they are up: a button that
                // flashes when pressed must not make the whole group jump about.
                static int cx0, cy0, cx1, cy1, cedge;
                static unsigned choiceSeen = 0;
                const unsigned now = rc_current_frame();
                const bool fresh = !choiceSeen || now - choiceSeen > 40;
                if (found)
                {
                    int edge = 0;
                    if (screens[0].flags)
                        for (int y = 8; y < 140; y++)
                            for (int x = 255; x > edge; x--)
                                if (!(LayerClass(screens[0].flags[y * 256 + x]) & drop))
                                {
                                    edge = x;
                                    break;
                                }
                    cx0 = fresh ? bx0 : std::min(cx0, bx0);
                    cy0 = fresh ? by0 : std::min(cy0, by0);
                    cx1 = fresh ? bx1 : std::max(cx1, bx1);
                    cy1 = fresh ? by1 : std::max(cy1, by1);
                    cedge = fresh ? edge : std::max(cedge, edge);
                    choiceSeen = now;
                }
                const bool showing = choiceSeen && now - choiceSeen <= 40;
                // While he only talks, his screen is in the middle. For a choice it slides to
                // the left edge and the choices stand in the space that frees up beside him,
                // instead of on top of him. His backdrop is the same all the way across, so it
                // is continued to both edges of the view.
                static float slide = 0;
                static unsigned slideFrame = 0;
                if (now != slideFrame)
                {
                    slide += std::clamp((showing ? 1.0f : 0.0f) - slide, -1.0f / 14, 1.0f / 14);
                    slideFrame = now;
                }
                const float ease = slide * slide * (3 - 2 * slide);
                const float picX = L * ease;
                sc.items[0].dx = picX;
                for (float x = picX - 2; x > L - 2; x -= 2)
                {
                    Element strip = Crop(0, 0, 0, 2, 192);
                    strip.dx = x;
                    strip.crisp = true;
                    sc.items.push_back(strip);
                }
                for (float x = picX + 256; x < R; x += 2)
                {
                    Element strip = Crop(0, 0, 0, 2, 192);
                    strip.dx = x;
                    strip.crisp = true;
                    sc.items.push_back(strip);
                }
                if (showing)
                {
                    int x0 = std::max(0, cx0 - 2), y0 = std::max(0, cy0 - 2);
                    el = Crop(1, x0, y0, std::min(255, cx1 + 2) - x0 + 1, std::min(191, cy1 + 2) - y0 + 1);
                    el.keys[0] = plain;
                    el.keyTolerance = 56;
                    // They may reach over the right of the professor's screen as far as
                    // there is nothing but backdrop there (above the message box).
                    float room = R - 3 - (L + cedge + 6);
                    if (room < free) room = free;
                    el.scale = std::min({ 1.25f, room / el.sw, 136.0f / el.sh });
                    float w = el.sw * el.scale, h = el.sh * el.scale;
                    el.dx = R - 3 - room + (room - w) / 2;
                    el.dy = 4 + (136 - h) / 2;
                    el.alpha = (uint8_t)(255 * ease);
                    sc.items.push_back(el);
                }
            }
            else if (SmartOverlay(screens, 1, 160, 144, drop, 180, 120, LAYER_BG0, 255, el) &&
                SmartOverlay(screens, 1, 200, 152, drop, 180, 120, LAYER_BG0, 255, el))
                // The lower right corner only ever holds a "touch to continue" hint.
                sc.items.push_back(el);
        }
        else if (std::any_of(std::begin(probeSave), std::end(probeSave), probeActive) &&
                 // Closing, the X menu's save step runs on for a few frames after the save
                 // screen is gone and the menu is back.
                 (sectionActive("touch_save_app") || !probeActive(probeStartMenu)))
            only("save", 1);
        else if (probeActive(probeMart))
            // Poke Mart: the item's description from the other screen above the list of goods.
        {
            sc = Stack("mart", { Crop(0, 0, 140, 256, 52), Crop(1, 0, 0, 256, 192) }, false, viewWidth);
            sc.bgScreen = 1;
            sc.bgX = 6;
            sc.bgY = 100;
        }
        else if (std::any_of(std::begin(probeChoice), std::end(probeChoice), probeActive))
        {
            // A question asked in the overworld: the answer buttons are on the bottom screen.
            // They are drawn, without that screen's background, at the right above the message box.
            only("choice", 0);
            Element el;
            if (SmartOverlay(screens, 1, 256, 192, LAYER_BG2 | LAYER_BG3 | LAYER_BACKDROP, 168, 136, 0, 255, el))
            {
                // Keep the piece the same while the question is up, growing it if needed, so
                // that a button flashing does not move or resize the group.
                static Element held;
                static unsigned heldAt = 0;
                const unsigned now = rc_current_frame();
                if (heldAt && now - heldAt <= 8)
                {
                    int x0 = std::min(held.sx, el.sx), y0 = std::min(held.sy, el.sy);
                    int x1 = std::max(held.sx + held.sw, el.sx + el.sw), y1 = std::max(held.sy + held.sh, el.sy + el.sh);
                    el.sx = x0;
                    el.sy = y0;
                    el.sw = x1 - x0;
                    el.sh = y1 - y0;
                    el.scale = std::min({ 1.0f, 168.0f / el.sw, 136.0f / el.sh });
                }
                held = el;
                heldAt = now;
                el.dx = (viewWidth >= 300 ? 128 + viewWidth / 2 - 3 : 252) - el.sw * el.scale;
                el.dy = 142 - el.sh * el.scale;
                sc.items.push_back(el);
            }
        }
        else if (menuReturn > 0)
            startMenu();
        else if (probeActive(probeStartMenuWait) && !appBottomNow)
        {
            // An application chosen in the start menu is loading (the field prepares it for a
            // while before it starts): the menu while it fades, then the bottom screen,
            // where these applications appear, instead of the top screen and both.
            if (!AutoFocus::Blank(screens[1].pixels))
                startMenu();
            else
                only("bottom", 1);
        }
        else if (focus.wantBottom || appBottomNow)
            only("bottom", 1);
        else if (sectionActive("OVY_8"))
            // In-battle bag and party screens (overlay 8) fill the bottom screen.
            only("battle-submenu", 1);
        else if (probeActive(probeBattleInput) ||
            // While the commands slide out and the moves in, the game stops reading the touch
            // screen for a few frames; the strip stays up meanwhile (see battleStripUntil).
            (frame < battleStripUntil && sectionActive("OVY_12") && !AutoFocus::Blank(screens[1].pixels)) ||
            // (With the redrawn menus, also while the game's memory says a menu is up: see menuUp.)
            (cfg.redrawn && battleHudOk && battleInputRecent() && sectionActive("OVY_12") && [&] {
                HudBattle::State s;
                return HudBattle::Read(nds->MainRAM, nds->MainRAMMask, s) && s.kind != HudBattle::Idle;
            }()))
        {
            // Battle, choosing a command or a move. On a wide view the buttons are taken off
            // the touch screen one by one and laid out as a strip along the bottom, in place
            // of the message box (which only says "What will X do?" at that point): nothing
            // of the scene is covered. Which menu is up is read from the colours of its
            // buttons; while they slide in or out neither is recognised and no buttons are
            // shown. Anything else the battle asks on the touch screen (a target, yes or no)
            // has no strip of its own and is shown whole, as a card at the lower left.
            only("battle", 0);
            sc.edgeFill = true;
            const uint32_t* b = screens[1].pixels;
            auto rgb = [&](int x, int y, int& r, int& g, int& bl) {
                uint32_t c = b[y * 256 + x];
                r = (c >> 16) & 0xFF; g = (c >> 8) & 0xFF; bl = c & 0xFF;
            };
            auto blue = [&](int x, int y) { int r, g, bl; rgb(x, y, r, g, bl); return bl > 150 && r < 110 && g > 90 && g < 200 && bl > r + 60; };
            auto red = [&](int x, int y) { int r, g, bl; rgb(x, y, r, g, bl); return r > 180 && g < 120 && bl < 120; };
            auto orange = [&](int x, int y) { int r, g, bl; rgb(x, y, r, g, bl); return r > 180 && g > 110 && g < 200 && bl < 110; };
            // With the redrawn menus the target (double battles) and the questions with two
            // answers get the strip as well, going by the game's memory (hudbattle.h draws
            // them). The target's CANCEL bar would otherwise pass for the moves' buttons.
            // While the buttons of one menu slide out and the next one's in (the moves and the
            // target of a double battle), neither picture is recognised, but the game's memory
            // still says a menu is up: the strip stays rather than the view jumping for those
            // frames. (The menu's number stays after the last answer while the turn plays out,
            // so only as long as the game was asking for input a moment ago.)
            HudBattle::State pickState;
            const bool redrawnRead = cfg.redrawn && battleHudOk && viewWidth >= 300 && battleInputRecent() &&
                HudBattle::Read(nds->MainRAM, nds->MainRAMMask, pickState);
            const bool pick = redrawnRead && (pickState.kind == HudBattle::Target || pickState.kind == HudBattle::Choice);
            const bool menuUp = redrawnRead && pickState.kind != HudBattle::Idle;
            const bool cmd = !pick && red(128, 60) && orange(40, 160) && blue(128, 166);
            const bool moves = !pick && !cmd && blue(40, 166) && blue(128, 166) && blue(216, 166);
            const bool idle = AutoFocus::Blank(screens[1].pixels) || (!blue(128, 166) && !red(128, 60));
            battleUnknown = (cmd || moves || idle || pick) ? 0 : battleUnknown + 1;
            // Only after the commands (FIGHT): after a move is chosen the battle goes on and
            // its messages must not be covered.
            if (cmd) battleStripUntil = frame + 20;
            else if (moves || idle || pick) battleStripUntil = 0;
            const float L = 128 - viewWidth / 2;
            auto put = [&](int sx, int sy, int sw, int sh, float x, float y, float scale) {
                sc.items.push_back({ 1, sx, sy, sw, sh, x, y, scale, 0, 255, false });
            };
            if (viewWidth >= 300 && (cmd || moves || pick || menuUp || frame < battleStripUntil))
            {
                // (Neither: the strip without buttons, while the next ones slide in.)
                sc.name = pick ? "battle-pick" : cmd ? "battle-cmd" : moves ? "battle-moves" : "battle-strip";
                // The strip: the first line of the message box ("What will X do?") as a
                // label at the top, the buttons under it.
                sc.items.push_back(FillRect(L, 137, (int)viewWidth + 1, 55, 0x20242C));
                sc.items.push_back(FillRect(L, 137, (int)viewWidth + 1, 1, 0x5A6270));
                {
                    // (Only the text part: the right end holds the battle's own little indicator boxes.)
                    Element label = { 0, 10, 149, 190, 18, L + 4.5f, 139.5f, 0.78f, 0, 255, false };
                    label.border = true;
                    label.borderColor = 0x101218;
                    label.borderWidth = 6;
                    label.borderRadius = 1.5f;
                    sc.items.push_back(label);
                }
                const size_t firstButton = sc.items.size();
                if (cmd)
                {
                    // FIGHT, BAG, POKeMON, RUN: the coloured inside of each button with its
                    // label, all cut to the same height and drawn at one scale, each with an
                    // outline of its own. (The buttons' own frames differ in size, and RUN's
                    // runs off the bottom of the touch screen, so they are not used.)
                    struct Cut { int sx, sy, sw; };
                    static const Cut cuts[4] = { { 68, 67, 120 }, { 6, 152, 66 }, { 182, 151, 68 }, { 94, 158, 68 } };
                    const int ch = 34, total = 120 + 66 + 68 + 68;
                    const float gap = 5;
                    const float sb = std::min(33.0f / ch, (viewWidth - 8 - 3 * gap) / total);
                    float x = 128 - (total * sb + 3 * gap) / 2;
                    for (const Cut& c : cuts)
                    {
                        put(c.sx, c.sy, c.sw, ch, x, 156.5f + (33 - ch * sb) / 2, sb);
                        x += c.sw * sb + gap;
                    }
                }
                else if (moves)
                {
                    // The four moves in a row, each its whole button with its frame; the touch
                    // screen's background around the rounded corners is left out so the button
                    // stands on the strip. There is no
                    // room for the CANCEL bar at a readable size; B cancels.
                    const int mw = 126, mh = 54;
                    const float gap = 5;
                    const float sm = std::min(33.0f / mh, (viewWidth - 8 - 3 * gap) / (4 * mw));
                    float x = 128 - (4 * mw * sm + 3 * gap) / 2;
                    const float y = 156.5f + (33 - mh * sm) / 2;
                    static const int slotX[4] = { 1, 129, 1, 129 }, slotY[4] = { 25, 25, 89, 89 };
                    for (int i = 0; i < 4; i++)
                    {
                        put(slotX[i], slotY[i], mw, mh, x, y, sm);
                        // The background is its own layer (BG2), so it is simply left out. The
                        // frame's rounded corners are shaded with the button's tan against that
                        // background; on the dark strip those pixels read as stray dots, so the
                        // tan is also left out along the rim (it is the main colour inside).
                        Element& el = sc.items.back();
                        el.drop = LAYER_BG2;
                        el.keys[0] = 0xCBBAA2;
                        el.keyRing = 5;
                        x += mw * sm + gap;
                    }
                }
                // The chosen button is outlined. (The game's own cursor is not used by the
                // strip; the keys that would bring up its red corners do not reach the game.)
                for (size_t i = firstButton; i < sc.items.size(); i++)
                {
                    Element& el = sc.items[i];
                    // Move buttons have frames of their own; only the chosen one is outlined.
                    el.border = cmd;
                    el.borderColor = 0x101218;
                    el.borderWidth = 6;
                    if ((int)(i - firstButton) == battleSel)
                    {
                        el.border = true;
                        // Around the rounded move buttons the outline is rounded to match.
                        if (moves) el.borderRadius = 4.0f;
                        el.borderColor = 0xF04040;
                        el.borderWidth = 16;
                    }
                }
            }
            else if (viewWidth < 300 || battleUnknown >= 20)
            {
                sc.name = "battle-menu";
                float scale = std::min((126 - L) / 256, 98.0f / 192);
                sc.items.push_back({ 1, 0, 0, 256, 192, L + 1, 144 - 192 * scale, scale, 0, 255, false });
                sc.items.back().border = !AutoFocus::Blank(screens[1].pixels);
            }
        }
        else if (probeActive(probeStartMenu) && fieldActive())
            startMenu();
        else if (spreadFrames >= 40)
            // Safety net for screens without a layout of their own: something is waiting for a
            // touch, so both screens are shown joined as one spread rather than hiding either.
            sc = Stack("spread", { Crop(0, 0, 0, 256, 192), Crop(1, 0, 0, 256, 192) }, true, viewWidth);
        else if (cursorInUse)
            // Nothing on screen maps to the touchscreen, so show it while the cursor is out.
            only("bottom", 1);
        else
        {
            only("top", 0);
            // (Not while the overworld runs: after a battle some of the battle overlay's code
            // still runs now and then, and each time the view jumped to the battle layout,
            // and the world zoom to 100%, for a few frames.)
            if (sectionActive("OVY_12") && !fieldActive())
            {
                // A battle outside its menus: messages and animations.
                sc.name = "battle";
                sc.edgeFill = true;
            }
        }
        // The overworld is drawn across the whole wide view by the widescreen 3D renderer.
        sc.wide = (viewWidth >= 300 || onPlaza) && (fieldActive() || menuReturn > 0) && !sc.items.empty() && sc.items[0].screen == 0 &&
            sc.items[0].sw == 256 && sc.items[0].sh == 192 && sc.items[0].dx == 0 && sc.items[0].scale == 1.0f;
        return sc;
    };
    Scene scene;
    scene.items = { FullScreen(0) };
    std::vector<uint32_t> mergedImage;
    SDL_Texture* mergedTex = nullptr;
    GpuCompositor* gpuCompose = nullptr;
    BattleWide battleWide;
    // The battle's HP boxes, message box and level-up stats redrawn (hudbattleui.h), while
    // the wide battle picture is built without the game's boxes (battleUiWide).
    HudBattleUi::Reader battleUiReader;
    HudBattleUi::State battleUi;
    HudBattleUi::Hud battleUiHud;
    bool battleUiWide = false;
    VoidFill voidFill;
    // The map blocks around the loaded ones, drawn into the widescreen world when a frame's
    // 3D is flushed (inside the emulated frame, with that frame's camera).
    WorldBlocks worldBlocks;
    NightFx nightFx;
    FocusBlur focusBlur;
    std::vector<NightFly> fireflies;
    std::vector<NightLight> lamps;
    worldBlocks.SetRom(rom);
    battleUiHud.SetRom(rom.data(), rom.size());
    worldBlocks.Plaza = plazaMap;
    // The HUD: its sprites and the game's own font.
    Sprites sprites;
    RomFont font;
    FindUiDir();
    const bool spritesOk = sprites.Load(ReadUiFile);
    battleHudOk = spritesOk;
    const bool fontOk = font.Load(rom.data(), rom.size());
    fprintf(stderr, "[hud] sprites %s, font %s\n", spritesOk ? g_uiDir.c_str() : "missing (plain menu)", fontOk ? "from the ROM" : "missing");
    // The player's own fonts (.otf or .ttf files in assets/fonts, as families: "Calamity-Regular"
    // and "Calamity-Bold" are one, its bold used for bold text) and button pictures
    // (assets/buttons, the white Nintendo Switch "Tips" icons: icon_key_a_00.png and its
    // shadow icon_key_ds_a_ds_00.png, ...): used when they are there, never shipped.
    struct FontFamily
    {
        std::string name;
        std::unique_ptr<VectorFont> regular, bold;
    };
    std::vector<FontFamily> fontFamilies;
    {
        std::vector<std::filesystem::path> files;
#ifdef __ANDROID__
        // An APK's assets cannot be listed: apk.py writes the names of the player's files.
        const std::vector<uint8_t> index = ReadFile("user-files.txt");
        std::string line;
        for (const uint8_t ch : index)
        {
            if (ch != '\n')
            {
                line += (char)ch;
                continue;
            }
            if (line.compare(0, 6, "fonts/") == 0) files.push_back(std::filesystem::u8path(line));
            line.clear();
        }
#else
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(g_userDir + "fonts"), ec))
            files.push_back(e.path());
#endif
        files.erase(std::remove_if(files.begin(), files.end(), [](const std::filesystem::path& p) {
            std::string ext = p.extension().u8string();
            for (char& ch : ext) ch = (char)tolower((unsigned char)ch);
            return ext != ".otf" && ext != ".ttf";
        }), files.end());
        std::sort(files.begin(), files.end());
        for (const auto& path : files)
        {
            // "Calamity-Bold" -> family "Calamity", bold; "rodin" -> "rodin".
            std::string stem = path.stem().u8string(), lower = stem;
            for (char& ch : lower) ch = (char)tolower((unsigned char)ch);
            bool bold = false;
            for (const char* suffix : { "-bold", "_bold", " bold", "-regular", "_regular", " regular" })
                if (lower.size() > strlen(suffix) && lower.compare(lower.size() - strlen(suffix), strlen(suffix), suffix) == 0)
                {
                    bold = strstr(suffix, "bold") != nullptr;
                    stem.resize(stem.size() - strlen(suffix));
                    break;
                }
            auto font = std::make_unique<VectorFont>();
            if (!font->Load(ReadFile(path.u8string()))) continue;
            auto it = std::find_if(fontFamilies.begin(), fontFamilies.end(), [&](const FontFamily& f) { return f.name == stem; });
            if (it == fontFamilies.end())
            {
                fontFamilies.push_back({ stem, nullptr, nullptr });
                it = fontFamilies.end() - 1;
            }
            (bold ? it->bold : it->regular) = std::move(font);
            fprintf(stderr, "[hud] font %s\n", path.filename().u8string().c_str());
        }
        // A family with only a bold file uses it for everything.
        for (FontFamily& f : fontFamilies)
            if (!f.regular) f.regular = std::move(f.bold);
    }
    // The family in use (cfg.textFont), or null for the game's letters.
    auto textFamily = [&]() -> const FontFamily* {
        if (cfg.textFont == "game" || fontFamilies.empty()) return nullptr;
        for (const FontFamily& f : fontFamilies)
            if (f.name == cfg.textFont) return &f;
        return &fontFamilies[0];
    };
    auto applyFont = [&]() {
        const FontFamily* f = textFamily();
        font.UseVector(f ? f->regular.get() : nullptr, f ? f->bold.get() : nullptr);
    };
    applyFont();
    std::vector<std::pair<std::string, Image>> buttonPics;
    {
        static const char* names[][2] = { { "buttons/button_a", "a" }, { "buttons/button_b", "b" }, { "buttons/button_x", "x" },
            { "buttons/button_y", "y" }, { "buttons/button_l", "l" }, { "buttons/button_r", "r" }, { "buttons/button_zl", "zl" },
            { "buttons/button_zr", "zr" }, { "buttons/button_plus", "plus" }, { "buttons/button_minus", "minus" },
            { "buttons/button_dpad_up", "up" }, { "buttons/button_dpad_down", "down" }, { "buttons/button_dpad_left", "left" },
            { "buttons/button_dpad_right", "right" }, { "buttons/stick_l", "stick_l" }, { "buttons/stick_r", "stick_r" } };
        const std::string dir = g_userDir + "buttons/";
        for (const auto& n : names)
        {
            Image glyph, shadow;
            const std::vector<uint8_t> g = ReadFile(dir + "icon_key_" + n[1] + "_00.png"), d = ReadFile(dir + "icon_key_ds_" + n[1] + "_ds_00.png");
            if (g.empty() || !DecodePng(g.data(), g.size(), glyph)) continue;
            if (d.empty() || !DecodePng(d.data(), d.size(), shadow)) shadow = Image();
            // White glyphs for dark screens: drawn in the HUD's ink over their shadow, softened,
            // so that they read on its white bars.
            Image pic;
            pic.w = std::max(glyph.w, shadow.w);
            pic.h = std::max(glyph.h, shadow.h);
            pic.px.assign((size_t)pic.w * pic.h, 0);
            // `invert`: light parts in the HUD's ink, dark parts (the letters) white.
            auto put = [&](const Image& src, uint32_t rgb, float alpha, bool invert) {
                const int ox = (pic.w - src.w) / 2, oy = (pic.h - src.h) / 2;
                for (int y = 0; y < src.h; y++)
                    for (int x = 0; x < src.w; x++)
                    {
                        const uint32_t p = src.px[(size_t)y * src.w + x], a = (uint32_t)((p >> 24) * alpha);
                        if (!a) continue;
                        uint32_t c = rgb;
                        if (invert)
                        {
                            const float l = ((p >> 16 & 0xFF) * 0.3f + (p >> 8 & 0xFF) * 0.59f + (p & 0xFF) * 0.11f) / 255;
                            auto mix = [&](int sh) { return (uint32_t)std::lround(255 * (1 - l) + ((rgb >> sh) & 0xFF) * l) << sh; };
                            c = mix(16) | mix(8) | mix(0);
                        }
                        uint32_t& o = pic.px[(size_t)(y + oy) * pic.w + x + ox];
                        o = BlendOver(o, a << 24 | c);
                    }
            };
            if (!shadow.px.empty()) put(shadow, 0x000000, 0.25f, false);
            put(glyph, HudColour::Ink & 0xFFFFFF, 1.0f, true);
            buttonPics.push_back({ n[0], pic });
        }
        // "Any direction" (buttons/button_dpad): the four direction buttons at half size, in a
        // cross, as the set has no picture of the whole pad.
        auto pic = [&](const char* name) -> const Image* {
            for (const auto& b : buttonPics)
                if (b.first == name) return &b.second;
            return nullptr;
        };
        const Image *up = pic("buttons/button_dpad_up"), *down = pic("buttons/button_dpad_down"),
                    *left = pic("buttons/button_dpad_left"), *right = pic("buttons/button_dpad_right");
        if (up && down && left && right && up->w == down->w && up->w == left->w && up->w == right->w &&
            up->h == down->h && up->h == left->h && up->h == right->h)
        {
            Image cross;
            cross.w = up->w;
            cross.h = up->h;
            cross.px.assign((size_t)cross.w * cross.h, 0);
            const struct { const Image* img; int x, y; } parts[] = {
                { up, cross.w / 4, 0 }, { down, cross.w / 4, cross.h / 2 }, { left, 0, cross.h / 4 }, { right, cross.w / 2, cross.h / 4 } };
            for (const auto& p : parts)
                for (int y = 0; y < p.img->h / 2; y++)
                    for (int x = 0; x < p.img->w / 2; x++)
                    {
                        // The average of the 2 x 2 pixels, premultiplied so that edges stay clean.
                        float a = 0, r = 0, g = 0, b = 0;
                        for (int k = 0; k < 4; k++)
                        {
                            const uint32_t s = p.img->px[(size_t)(2 * y + k / 2) * p.img->w + 2 * x + k % 2];
                            const float sa = (s >> 24) / 4.0f;
                            a += sa, r += (s >> 16 & 0xFF) * sa, g += (s >> 8 & 0xFF) * sa, b += (s & 0xFF) * sa;
                        }
                        if (a < 0.5f) continue;
                        uint32_t& o = cross.px[(size_t)(p.y + y) * cross.w + p.x + x];
                        o = BlendOver(o, (uint32_t)(a + 0.5f) << 24 | (uint32_t)(r / a) << 16 | (uint32_t)(g / a) << 8 | (uint32_t)(b / a));
                    }
            buttonPics.push_back({ "buttons/button_dpad", cross });
        }
        if (!buttonPics.empty()) fprintf(stderr, "[hud] %zu button pictures from %s\n", buttonPics.size(), dir.c_str());
    }
    for (const auto& b : buttonPics) sprites.Replace(b.first, b.second);
    SongPlayer songs;
    if (songs.Load(ReadSongFile)) fprintf(stderr, "[songs] a list of songs in %ssongs\n", g_userDir.c_str());
    GameSfx sfx;
    sfx.SetRom(rom.data(), rom.size());
    auto uiSound = [&](GameSfx::Sound s) {
        if (cfg.menuSounds) sfx.Play(s);
    };
    // A menu's input with its sounds: the cursor sound when the row or tab changes or a value
    // is changed, the choose sound when a row is used.
    auto menuUpdate = [&](Menu& m, const MenuInput& in, int& dir) {
        const int tab = m.tab, sel = m.selected;
        const int hit = m.Update(in, dir);
        if (hit >= 0) uiSound(dir ? GameSfx::Cursor : GameSfx::Confirm);
        else if (m.tab != tab || m.selected != sel) uiSound(GameSfx::Cursor);
        return hit;
    };
    // (Tests: SSR_SFX_DUMP=<folder> writes the rendered sounds there as .wav files.)
    if (const char* dir = getenv("SSR_SFX_DUMP"))
        for (int i = 0; i < GameSfx::SoundCount; i++)
        {
            const std::vector<int16_t>& pcm = sfx.Get((GameSfx::Sound)i);
            const std::string path = std::string(dir) + "/sfx" + std::to_string(GameSfx::Seqs[i]) + ".wav";
            if (FILE* f = fopen(path.c_str(), "wb"))
            {
                const uint32_t bytes = (uint32_t)pcm.size() * 2;
                const uint32_t h[] = { 0x46464952, 36 + bytes, 0x45564157, 0x20746D66, 16, 0x00020001, 48000, 48000 * 4, 0x00100004, 0x61746164, bytes };
                fwrite(h, 4, 11, f);
                fwrite(pcm.data(), 2, pcm.size(), f);
                fclose(f);
            }
        }
    // The logo on the start screen: assets/logo.png (beside the HUD's folder), if it is there.
    Image logo;
    {
        const std::vector<uint8_t> f = ReadFile(g_userDir + "logo.png");
        if (f.empty() || !DecodePng(f.data(), f.size(), logo)) logo = Image();
    }
    // The title screen's video (intro.h), if the player has put one in assets/animations.
    IntroVideo intro;
    if (intro.Load(ReadFile(g_userDir + "animations/intro.mpg"))) fprintf(stderr, "[hud] title video\n");
    // Other players' trainers near this one (online), moved smoothly towards where they
    // were last heard and drawn into the widescreen world as the far map's people are.
    struct RemoteTrainer
    {
        bool on = false;
        double x = 0, y = 0, z = 0;
        int step = 0;
    };
    RemoteTrainer remotes[4];
    std::vector<WorldBlocks::Trainer> remoteDraw;
    // At each flush of the game's 3D: its camera and where the trainers were (0-3 the
    // others, 4 this player), kept for the last few flushes. What is drawn over the picture
    // uses those of the flush the picture shows (or halfway between two for an in-between
    // picture), not the newest, so it stays on the world while it moves.
    struct FlushView
    {
        uint64_t flush = 0;
        WorldBlocks::Camera camera;
        bool on[5] = {};
        double pos[5][3] = {};
    };
    FlushView flushViews[4];
    FlushView shownView; // for the picture on the screen
    bool emoteWheel = false;
    melonDS::Wide.Extra = [&](std::vector<u32>& commands) {
        if (cfg.farMap) worldBlocks.Build(*nds, commands);
        worldBlocks.EmitPlaza(*nds, commands);
        worldBlocks.EmitTrainers(*nds, remoteDraw, commands);
        FlushView& fv = flushViews[melonDS::Wide.FlushCount & 3];
        fv = FlushView();
        fv.flush = melonDS::Wide.FlushCount;
        fv.camera = worldBlocks.ReadCamera(*nds, melonDS::Wide.ViewWidth, melonDS::Wide.Zoom);
        for (int i = 0; i < 4; i++)
            if ((fv.on[i] = remotes[i].on))
            {
                fv.pos[i][0] = remotes[i].x;
                fv.pos[i][1] = remotes[i].y;
                fv.pos[i][2] = remotes[i].z;
            }
        // This player's trainer (sFieldSysPtr -> playerAvatar -> map object's position).
        auto rd = [&](uint32_t a) -> uint32_t { return (a >> 24) == 0x02 ? *(uint32_t*)&nds->MainRAM[a & nds->MainRAMMask & ~3u] : 0; };
        const uint32_t obj = rd(rd(rd(0x021D4158) + 0x40) + 0x30);
        if ((fv.on[4] = obj != 0))
            for (int k = 0; k < 3; k++) fv.pos[4][k] = (int32_t)rd(obj + 0x70 + 4 * k);
    };
    // The view of the picture now on the screen (see FlushView); none when there is no
    // widescreen picture, and the overlays then use the game's camera as it is.
    auto updateShownView = [&]() {
        const FlushView& cur = flushViews[melonDS::Wide.FrontFlush & 3];
        const FlushView& prev = flushViews[(melonDS::Wide.FrontFlush - 1) & 3];
        shownView = FlushView();
        if (!melonDS::Wide.Owner || cur.flush != melonDS::Wide.FrontFlush || !cur.camera.valid)
        {
            worldBlocks.Shown = WorldBlocks::Camera();
            return;
        }
        shownView = cur;
        const double t = melonDS::Wide.FrontT;
        if (t < 1 && prev.flush + 1 == cur.flush && prev.camera.valid)
        {
            shownView.camera = WorldBlocks::Lerp(prev.camera, cur.camera, t);
            for (int i = 0; i < 5; i++)
                if (cur.on[i] && prev.on[i])
                    for (int k = 0; k < 3; k++) shownView.pos[i][k] = prev.pos[i][k] + (cur.pos[i][k] - prev.pos[i][k]) * t;
        }
        worldBlocks.Shown = shownView.camera;
    };
    const bool cpuCompose = getenv("SSR_CPU_COMPOSE") != nullptr;
    int mergedTexW = 0, mergedTexH = 0;

    // Test navigator (--nav): walks the player to a list of tiles, learning which moves
    // are blocked as it goes and routing around them.
    struct NavState
    {
        std::vector<std::pair<int, int>> way;
        std::vector<int> push;
        int pushDir = -1;
        size_t idx = 0;
        std::set<std::tuple<int, int, int>> blocked;
        int dir = -1, lastDir = -1, fromX = -1, fromZ = -1, map = -1;
        uint32_t since = 0, started = 0;
        bool active = false;
        bool outdoor = true; // the permission grid only describes the overworld matrix
    } navState;
    {
        std::istringstream ss(opt.nav);
        std::string item;
        while (std::getline(ss, item, ';'))
        {
            int x, z;
            char push = 0;
            // "x,z" or "x,z,U|D|L|R": on arriving, keep walking that way until the map changes (a door).
            int n = sscanf(item.c_str(), "%d,%d,%c", &x, &z, &push);
            if (n >= 2)
            {
                navState.way.push_back({ x, z });
                const char* dirs = "UDLR";
                const char* p = n == 3 ? strchr(dirs, push) : nullptr;
                navState.push.push_back(p ? (int)(p - dirs) : -1);
            }
        }
        navState.active = !navState.way.empty();
    }
    auto readRam = [&](uint32_t a) -> uint32_t {
        if (a < 0x02000000 || a >= 0x02400000) return 0;
        uint32_t v;
        memcpy(&v, nds->MainRAM + (a & nds->MainRAMMask), 4);
        return v;
    };
    // The game's save screen (TouchSaveApp, src/touch_save_app.c): its data, found in memory
    // by its field system pointer (+0x1C) while its functions run, and kept while it checks
    // out. +0x0C its step (and +0x10 the step after a message), +0x48 its yes/no prompt.
    uint32_t saveAppAt = 0;
    auto findSaveApp = [&]() -> uint32_t {
        const uint32_t fs = readRam(0x021D4158);
        auto looks = [&](uint32_t a) {
            return fs && readRam(a + 0x1C) == fs && readRam(a + 0x0C) < 16 && readRam(a + 0x10) < 16 &&
                (readRam(a + 0x04) >> 24) == 0x02 && (readRam(a + 0x08) >> 24) == 0x02 && (readRam(a + 0x44) >> 24) == 0x02;
        };
        if (saveAppAt && looks(saveAppAt)) return saveAppAt;
        saveAppAt = 0;
        if (!std::any_of(std::begin(probeSave), std::end(probeSave), probeActive)) return 0;
        for (uint32_t a = 0x02100000; a < 0x02400000; a += 4)
            if (looks(a)) return saveAppAt = a;
        return 0;
    };
    // The player walking about with nothing of the game open: no X menu, text box or event
    // (the player's map object paused, flag 0x40, while someone talks or a script runs).
    auto fieldIdle = [&]() {
        const uint32_t fs = readRam(0x021D4158), obj = readRam(readRam(fs + 0x40) + 0x30);
        return fieldActive() && !probeActive(probeStartMenu) && fs && obj && !(readRam(fs + 0xD0) >> 16 & 0x40) && !(readRam(obj) & 0x40);
    };
    // sFieldSysPtr -> location (map id) and playerAvatar -> mapObject -> current tile.
    auto playerPos = [&](int& map, int& x, int& z) {
        uint32_t fs = readRam(0x021D4158), loc = readRam(fs + 0x20), obj = readRam(readRam(fs + 0x40) + 0x30);
        map = (int)readRam(loc);
        x = (int)readRam(obj + 0x64);
        z = (int)readRam(obj + 0x6C);
        return obj != 0;
    };
    // Movement permissions of the overworld, if given: lets the navigator plan around
    // walls and one-way ledges instead of discovering them.
    struct NavGrid { int w = 0, h = 0; std::vector<uint16_t> cells; };
    auto loadNavGrid = [](const std::string& path) {
        NavGrid g;
        std::ifstream f(path, std::ios::binary);
        uint16_t dims[2] = {};
        f.read((char*)dims, 4);
        g.w = dims[0];
        g.h = dims[1];
        g.cells.resize((size_t)g.w * g.h);
        f.read((char*)g.cells.data(), g.cells.size() * 2);
        return g;
    };
    NavGrid navOverworld;
    std::map<int, NavGrid> navInterior;
    if (!opt.navMap.empty()) navOverworld = loadNavGrid(opt.navMap);
    for (auto& e : opt.navMapIds) navInterior[e.first] = loadNavGrid(e.second);
    static const int navDX[4] = { 0, 0, -1, 1 }, navDZ[4] = { -1, 1, 0, 0 };
    // Where a step in direction d from (x, z) ends up; false if it cannot be made.
    auto navMove = [&](int x, int z, int d, int& ox, int& oz) -> bool {
        if (navState.blocked.count({ x, z, d })) return false;
        ox = x + navDX[d];
        oz = z + navDZ[d];
        const NavGrid* grid = &navOverworld;
        if (!navState.outdoor)
        {
            auto it = navInterior.find(navState.map);
            grid = it == navInterior.end() ? nullptr : &it->second;
        }
        if (!grid || grid->cells.empty() || navState.map < 0) return true;
        auto perm = [&](int px, int pz) -> uint16_t {
            return px < 0 || pz < 0 || px >= grid->w || pz >= grid->h ? 0x8000 : grid->cells[(size_t)pz * grid->w + px];
        };
        if (navState.idx < navState.way.size() && navState.way[navState.idx] == std::make_pair(ox, oz)) return true; // doors
        uint16_t p = perm(ox, oz);
        int b = p & 0xFF;
        // Ladders, stairs and door mats warp: only step on one when it is the waypoint.
        if ((b >= 0x3C && b <= 0x3F) || (b >= 0x5E && b <= 0x6F)) return false;
        if (b >= 0x38 && b <= 0x3B)
        {
            // Ledge: 0x38 jumps right, 0x39 left, 0x3A up, 0x3B down, landing on the far side.
            static const int ledgeDir[4] = { 3, 2, 0, 1 };
            if (ledgeDir[b - 0x38] != d) return false;
            ox += navDX[d];
            oz += navDZ[d];
            return !(perm(ox, oz) & 0x8000);
        }
        return !(p & 0x8000);
    };
    // Next direction (0 up, 1 down, 2 left, 3 right) on a shortest path.
    auto navStep = [&](int x, int z, int tx, int tz) -> int {
        const int R = 64;
        if (x < 0 || z < 0 || x > 4096 || z > 4096) return -1; // not a position (mid-warp)
        int x0 = std::min(x, tx) - R, z0 = std::min(z, tz) - R;
        int w = std::abs(x - tx) + 2 * R + 1, h = std::abs(z - tz) + 2 * R + 1;
        std::vector<int8_t> first((size_t)w * h, -1);
        std::deque<std::pair<int, int>> q;
        q.push_back({ x, z });
        first[(size_t)(z - z0) * w + (x - x0)] = 4;
        while (!q.empty())
        {
            auto [cx, cz] = q.front();
            q.pop_front();
            int8_t f = first[(size_t)(cz - z0) * w + (cx - x0)];
            if (cx == tx && cz == tz) return f;
            for (int d = 0; d < 4; d++)
            {
                int nx, nz;
                if (!navMove(cx, cz, d, nx, nz)) continue;
                if (nx < x0 || nz < z0 || nx >= x0 + w || nz >= z0 + h) continue;
                int8_t& slot = first[(size_t)(nz - z0) * w + (nx - x0)];
                if (slot != -1) continue;
                slot = f == 4 ? (int8_t)d : f;
                q.push_back({ nx, nz });
            }
        }
        return -1;
    };
    uint32_t scriptClock = frame; // equals frame unless --battle-mash holds it during battles

    Canvas canvas;
    // In a window, the names and pointers over the world (drawWorldTags) on a layer of their
    // own under the rest of the HUD, so the pictures drawn between the game's frames can
    // draw them again where the world then is.
    Canvas tagCanvas;
    Ui ui;
    Menu menu;
    enum
    {
        M_RESUME, M_LAYOUT, M_LARGE, M_FPS60, M_FULLSCREEN, M_SMOOTH, M_ASPECT, M_WINDOWSIZE, M_WIDE, M_ZOOM, M_FARMAP, M_VOIDFILL, M_NIGHTFX, M_BLUR, M_WIDELINES, M_SHOWFPS, M_BUTTONS, M_PADTOUCH, M_FAST, M_SAVESTATE, M_LOADSTATE, M_RESET, M_QUIT,
        M_REMAP, // M_REMAP + i: the controller button for RemapKeys[i]
        M_REMAP_RESET = M_REMAP + 8,
        M_HOST, M_JOIN, M_HOSTMAX, M_SESSION, M_LEAVE, M_SERVER, M_SHOWPING, M_TOASTS, M_NICK, M_COLOUR, M_STARTSCREEN, M_PARTNER,
        M_FRIENDS, M_ACCEPT, M_DECLINE, M_REDRAWN, M_FPSMAX, M_MUSIC, M_AUTOHOST, M_AUTOSAVEGAME, M_TEXTFONT, M_AUTOSAVE, M_MENUSOUNDS, M_VSYNC,
        M_FRIEND, // M_FRIEND + i: player i (Session tab) or friend i (Friends tab)
    };
    // The pause menu's tabs.
    enum { T_RESUME, T_GAME, T_DISPLAY, T_CONTROLS, T_ONLINE, T_PROFILE, T_STATES };
    menu.tabs = { { "RESUME", "menu/menu_resume" }, { "GAME", "menu/menu_party" }, { "DISPLAY", "menu/menu_options" },
        { "CONTROLS", "menu/menu_controls" }, { "ONLINE", "menu/menu_online" }, { "PROFILE", "menu/menu_profile" },
        { "STATES", "menu/menu_states" } };
    menu.tab = T_DISPLAY;
    // Screens of the online HUD over (or instead of) the pause menu.
    enum { SCR_NONE, SCR_SESSION, SCR_SUMMARY, SCR_EDIT, SCR_FRIENDS, SCR_INVITE };
    int screen = SCR_NONE;
    Menu sessionMenu;
    sessionMenu.open = true;
    // The friends panel: the players here now, the friends (kept in the settings by player
    // id: name, colour and when last seen) and friend requests. Its rows, as drawn.
    struct Friend
    {
        std::string id, name; // (no id: one remembered by an older version, by name only)
        int colour = 0;
        long long seen = 0;
    };
    std::map<std::string, Friend> friends; // by id (or name)
    {
        std::istringstream ss(cfg.friends);
        std::string e;
        while (std::getline(ss, e, ','))
        {
            std::vector<std::string> v;
            std::istringstream es(e);
            for (std::string part; std::getline(es, part, ':');) v.push_back(part);
            if (v.size() == 4 && v[0].size() == 16 && !v[1].empty()) friends[v[0]] = { v[0], v[1], atoi(v[2].c_str()) & 3, atoll(v[3].c_str()) };
            else if (v.size() == 3 && !v[0].empty()) friends[v[0]] = { "", v[0], atoi(v[1].c_str()) & 3, atoll(v[2].c_str()) };
        }
    }
    auto saveFriends = [&]() {
        cfg.friends.clear();
        std::vector<std::string> ids;
        for (const auto& [key, fr] : friends)
        {
            cfg.friends += (fr.id.empty() ? "" : fr.id + ":") + fr.name + ":" + std::to_string(fr.colour) + ":" + std::to_string(fr.seen) + ",";
            if (!fr.id.empty()) ids.push_back(fr.id);
        }
        Party_SetFriends(ids);
    };
    saveFriends();
    // A player of the session as a friend (both sides said yes), or seen again.
    auto addFriend = [&](const PartyPeer& p) {
        std::string name = p.name;
        name.erase(std::remove_if(name.begin(), name.end(), [](char ch) { return ch == ':' || ch == ','; }), name.end());
        if (p.uid.size() != 16 || name.empty()) return;
        // A friend remembered by name only (older versions) is the same one now with an id.
        auto old = friends.find(name);
        if (old != friends.end() && old->second.id.empty()) friends.erase(old);
        if (!friends.count(p.uid)) fprintf(stderr, "[party] %s is now a friend\n", name.c_str());
        friends[p.uid] = { p.uid, name, p.colour, (long long)time(nullptr) };
        saveFriends();
    };
    Menu friendsMenu;
    friendsMenu.open = true;
    friendsMenu.tabs = { { "Session", "" }, { "Friends", "" }, { "Requests", "" } };
    std::vector<HudOnline::FriendRow> friendRows;
    // What each row's button does, changed with left and right: the session's players
    // (battle, trade, add as a friend), the friends online (invite, join), requests (yes, no).
    constexpr int PF_ADD = 0x100, PF_WATCH = 0x200;
    int inviteChoice[4] = { PI_BATTLE, PI_BATTLE, PI_BATTLE, PI_BATTLE };
    std::map<std::string, int> friendChoice; // id -> 0 invite, 1 join
    std::vector<std::string> friendRowIds;   // the Friends tab's rows' ids
    struct FriendAsk
    {
        int player;
        std::string uid;
        bool no = false; // the button shows "Decline"
    };
    std::vector<FriendAsk> requests;
    int watching = -1; // the player whose battle is watched (spectator card), or -1
    PartyBattle myBattle; // this player's battle, as told to the others (at 0: none)
    // An invite being answered: from whom, what, since when (a friend's invite to their
    // session: from no player here, inviteWho and inviteCode).
    Menu inviteMenu;
    inviteMenu.open = true;
    int inviteFrom = -1, inviteKind = 0;
    PartyPeer inviteWho;
    std::string inviteCode;
    uint32_t inviteAt = 0;
    // The Union Room hand-off: an accepted battle or trade invite takes both players there and
    // matches them (see where it runs, after the controller is read).
    enum { H_OFF, H_SAVE, H_WARP, H_WARPING, H_ROOM, H_WALK, H_MENU, H_ANSWER };
    struct Handoff
    {
        int phase = H_OFF;
        int peer = -1, kind = 0;       // the other player, PI_BATTLE or PI_TRADE
        std::string name;              // theirs
        bool asker = false;            // sent the invite: walks to the other and asks
        uint32_t start = 0, since = 0; // frames: the hand-off's start, this phase's
        bool saveAsked = false, saveSeen = false; // a save has been asked for, has begun
        bool answered = false;         // the question has been answered
    } hand;
    uint32_t handTapAt = 0, handKey = 0; // a press of the hand-off's, held three frames
    bool unionWarped = false; // in the Union Room by the hand-off: Party_LeftUnionRoom once out
    int partyMap = -1; // this player's map, as last given to the party
    Image trainerArt;  // this player's trainer, for the trainer card
    // The frontend's own start screen, before the game runs (not for tests or a party or
    // snapshot from the command line).
    bool startOpen = spritesOk && ((cfg.startScreen && !opt.headless && opt.party.empty() && opt.stateLoad.empty()) || getenv("SSR_START_SCREEN"));
    int startSel = 0;
    // Hosting on this computer (Host when playing, the server being localhost): its party
    // server is started now, so that it is listening by the time the player is in the world.
    if (cfg.autoHost && Party_IsLocalServer(cfg.onlineServer) && opt.party.empty() && !opt.headless && !opt.lockstep)
        if (!Party_StartLocalServer(g_uiDir + "../../tools/party_server.py"))
            fprintf(stderr, "[party] no Python to start the local party server\n");
    // Before the start screen: the title, the video with the logo, until A (or + or Enter).
    bool titleOpen = startOpen && intro.Ok();
    const uint32_t titleSince = SDL_GetTicks();
    enum { ST_CONTINUE, ST_NEW, ST_ONLINE, ST_SETTINGS, ST_QUIT };
    const bool haveSave = Party_SaveCard().known;
    std::vector<int> startActions = { ST_NEW, ST_ONLINE, ST_SETTINGS, ST_QUIT };
    if (haveSave) startActions.insert(startActions.begin(), ST_CONTINUE);
    // Starting the game: its intro, title screen and own Continue / New game menu are
    // passed by pressing A (fast, behind a loading picture) until the player stands in
    // the world, or the new game's first scene starts. Not in tests, which script the boot.
    enum { BOOT_NONE, BOOT_CONTINUE, BOOT_NEW };
    const bool bootSkipOk = (!opt.headless && opt.stateLoad.empty() && opt.script.empty()) || getenv("SSR_BOOT_SKIP");
    int bootMode = !startOpen && bootSkipOk ? (haveSave ? BOOT_CONTINUE : BOOT_NEW) : BOOT_NONE;
    uint32_t bootFrames = 0;
    bool bootSawMenu = false;
    uint32_t bootMenuFrames = 0;
    // Autosave (autosave.h). Not in tests unless asked for (SSR_AUTOSAVE=1), nor with
    // --lockstep, whose two consoles must get the same buttons.
    const bool autosaveOk = !ref && (!opt.headless || getenv("SSR_AUTOSAVE"));
    Autosave::Resume resume;
    resume.path = opt.save + ".resume";
    Autosave::GameSave gameSave;
    Autosave::Indicator saveIndicator;
    saveIndicator.Load(ReadFile(g_userDir + "autosave.gif"));
    bool played = false;           // the overworld has run since the game started or was reset
    uint32_t resumeAt = frame;     // when the last resume point was made
    uint32_t resumeShownUntil = 0; // its indicator
    bool resumeDue = false;        // one is wanted at the next calm moment (a save was written)
    static const uint32_t resumeEvery = getenv("SSR_AUTOSAVE_RESUME") ? (uint32_t)atoi(getenv("SSR_AUTOSAVE_RESUME")) : 60 * 60 * 3;
    // A resume point CONTINUE can start from: one holding the save there is now.
    Autosave::ResumeHeader resumeInfo;
    const bool haveResume = autosaveOk && !opt.noSave && resume.Find(*nds, resumeInfo) >= 0;
    auto tryResume = [&]() {
        bool broken = false;
        if (!haveResume || !cfg.autosave || !resume.Load(*nds, broken))
        {
            if (broken)
            {
                // Refused halfway through: the game starts again as without one.
                fprintf(stderr, "[autosave] the resume point could not be loaded\n");
                nds->Reset();
                nds->SetupDirectBoot("game.nds");
                nds->Start();
                rc_reset();
            }
            return false;
        }
        rc_reset();
        if (!opt.headless || getenv("SSR_CLOCK")) SetClock(*nds, opt.headless);
        fprintf(stderr, "[autosave] resumed from %s\n", resume.path.c_str());
        return true;
    };
    // The loading picture stays a few frames after a resume, until the widescreen world is drawn.
    uint32_t resumeCoverUntil = 0;
    if (bootMode == BOOT_CONTINUE && tryResume())
    {
        bootMode = BOOT_NONE;
        resumeCoverUntil = frame + 10;
    }
    // Typing a text: the nickname, the server's address or a party's code.
    enum { E_NAME, E_SERVER, E_CODE };
    struct TextEdit
    {
        int purpose = E_NAME;
        std::string text, charset;
        size_t max = 10;
        int cursor = 0;
    } edit;
    static const std::string CodeLetters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no O/0 or I/1
    // The DS button waiting for a controller button in the Controls tab (-1 none), and since when.
    int remapping = -1;
    uint32_t remapSince = 0;
    bool fastLock = false;
    // World detail choices: the most lines the widescreen 3D is drawn at (192 is the DS's own).
    static const int detailLines[] = { 192, 288, 384, 540, 720, 1080, 1440, 2160 };
    const int detailCount = (int)(sizeof(detailLines) / sizeof(detailLines[0]));
    auto detailIndex = [&]() {
        int i = 0;
        while (i + 1 < detailCount && detailLines[i + 1] <= cfg.wideLines) i++;
        return i;
    };
    auto buildMenu = [&]() {
        auto onOff = [](bool v) { return std::string(v ? "On" : "Off"); };
        static const char* large[] = { "Auto", "Top", "Bottom" };
        static const char* layoutNames[] = { "Merged", "Hybrid", "Side by side", "Vertical", "Top only", "Bottom only" };
        static const char* dsNames[] = { "A", "B", "X", "Y", "L", "R", "START", "SELECT" };
        std::string aspect = AspectRatios[cfg.aspect].name;
        if (aspect == "FILL WINDOW") aspect = "Fill window";
        menu.items.clear();
        auto add = [&](int tab, int id, const std::string& label, const std::string& value, const std::string& desc) {
            MenuItem it;
            it.tab = tab;
            it.id = id;
            it.label = label;
            it.value = value;
            it.desc = desc;
            menu.items.push_back(it);
            return &menu.items.back();
        };
        add(T_RESUME, M_RESUME, "Back to the game", "", "Close this menu and carry on playing.");

        add(T_GAME, M_FPS60, "60 fps", onOff(cfg.fps60), "Draws a picture between each two of the game's, so the world moves at 60 frames a second.");
        add(T_GAME, M_MUSIC, "Music", !songs.Any() ? "Original" : cfg.newSongs ? "New songs" : "Original",
            songs.Any() ? "New songs: your own music files (assets/songs, listed in songs.txt) in place of the game's tunes. Sound effects stay."
                        : "Put music files and a songs.txt in assets/songs to hear your own songs in place of the game's tunes.")->disabled = !songs.Any();
        add(T_GAME, M_MENUSOUNDS, "Menu sounds", onOff(cfg.menuSounds), "The game's own cursor and button sounds in these menus.");
        add(T_GAME, M_FPSMAX, "Frame rate", cfg.fpsUnlimited ? "Unlimited" : "60",
            "Unlimited: as many pictures of the world as your computer can draw, for screens faster than 60 Hz. The game runs at its normal speed either way.");
        add(T_GAME, M_FAST, "Fast forward", onOff(fastLock), "Runs the game faster until you turn it off again.");
        add(T_GAME, M_AUTOSAVE, "Autosave", onOff(cfg.autosave),
            "Keeps your place every few minutes and when you quit, in the background, for Continue. The game never stops for it.");
        add(T_GAME, M_AUTOSAVEGAME, "Also save the game", onOff(cfg.autosaveGame),
            "Also saves the game itself in Pok\xC3\xA9mon Centers and after a badge. The game stops for a few seconds while it writes.")->disabled = !cfg.autosave;
        add(T_GAME, M_STARTSCREEN, "Start screen", onOff(cfg.startScreen), "Shows the start screen (play, online, settings) when the game starts.");
        add(T_GAME, M_RESET, "Reset game", "", "Starts the game again from its title screen. Anything not saved is lost.")->danger = true;
        add(T_GAME, M_QUIT, "Quit", "", "Closes the game. The in-game save is kept; play since your last save is not.")->danger = true;

        add(T_DISPLAY, M_LAYOUT, "Screen layout", layoutNames[(int)layout.mode % 6],
            layout.mode == LayoutMode::Merged ? "Merged: both DS screens in one picture. The world behind this menu is the live preview."
                                              : "How the two DS screens are arranged in the window.");
        add(T_DISPLAY, M_LARGE, "Large screen", large[focus.mode], "Which DS screen is shown large: chosen for you, or always the top or the bottom one.");
        add(T_DISPLAY, M_FULLSCREEN, "Fullscreen", onOff(cfg.fullscreen), "Fills the whole screen. F11 does the same.");
        add(T_DISPLAY, M_SMOOTH, "Smooth scaling", onOff(cfg.smooth), "Smooths the pixels when the picture is enlarged.");
        add(T_DISPLAY, M_VSYNC, "VSync", onOff(cfg.vsync),
            "Shows each picture when the screen refreshes, so it never tears. With Frame rate Unlimited, as many pictures as the screen shows.");
        add(T_DISPLAY, M_ASPECT, "Aspect ratio", aspect, "The shape of the picture; on a wider screen there are bars beside it.");
        add(T_DISPLAY, M_WINDOWSIZE, "Window size", cfg.windowH ? std::to_string(cfg.windowH) + "p" : "Default", "The height of the window.");
        add(T_DISPLAY, M_WIDE, "Widescreen world", onOff(cfg.wide), "Shows more of the overworld to the left and right instead of bars.");
        MenuItem* zoom = add(T_DISPLAY, M_ZOOM, "World zoom", std::to_string(cfg.zoom) + "%",
            "Below 100% the camera pulls back and you see more of the world; above it the world is drawn larger.");
        zoom->bar = (cfg.zoom - ZoomSteps[0]) / (float)(ZoomSteps[ZoomCount - 1] - ZoomSteps[0]);
        add(T_DISPLAY, M_FARMAP, "Far map", onOff(cfg.farMap), "Draws the map beyond the part the game has loaded, so a wide or zoomed-out view has no gaps.");
        add(T_DISPLAY, M_VOIDFILL, "Void fill", onOff(cfg.voidFill), "Fills any ground not loaded yet with a soft haze instead of black.");
        add(T_DISPLAY, M_NIGHTFX, "Night effects", onOff(cfg.nightFx), "In the evening and at night: night colours, lamp light, glowing windows and fireflies.");
        add(T_DISPLAY, M_TEXTFONT, "Text", textFamily() ? textFamily()->name : "Game font",
            !fontFamilies.empty() ? "The letters of this menu and the HUD: one of the fonts in assets/fonts, or the game's own."
                                  : "Put a font file (.otf or .ttf) in assets/fonts to use it for the menus and the HUD.")->disabled = fontFamilies.empty();
        add(T_DISPLAY, M_REDRAWN, "New screens", onOff(cfg.redrawn), "The game's own screens drawn anew, in this menu's style: the save screen, the trainer card, the bag, the party and summary, the Pok\xC3\xA9" "dex, the Pok\xC3\xA9gear, the PC's boxes, the battle commands, the trade screen, buying and selling in shops, the Battle Factory's rentals, the Battle Hall's board, Voltorb Flip, the Ruins of Alphâs puzzles, what people say, phone calls, the fly map and the Options.");
        add(T_DISPLAY, M_BLUR, "Blur", cfg.blur == 2 ? "Strong" : cfg.blur == 1 ? "Light" : "Off", "Blurs the far edges of the world a little, like a camera.");
        add(T_DISPLAY, M_WIDELINES, "World detail", std::to_string(detailLines[detailIndex()]), "How many lines the widescreen world is drawn with. Lower is faster.");
        add(T_DISPLAY, M_SHOWFPS, "FPS counter", onOff(cfg.showFps), "Shows how many frames a second are drawn, in the top right corner.");

        for (int i = 0; i < RemapCount; i++)
        {
            MenuItem* it = add(T_CONTROLS, M_REMAP + i, dsNames[i], "",
                std::string("The controller button for the DS's ") + dsNames[i] + ". Press A to choose another, Y to go back to the usual one.");
            if (remapping == i) it->value = "Press a button...";
            else it->glyph = PadGlyph(PadButtonFor(i, cfg.byLabel, cfg.padMap));
        }
        add(T_CONTROLS, M_BUTTONS, "Face buttons", cfg.byLabel ? "By label" : "By position",
            "By label: the button marked A is the DS's A. By position: the DS's layout, so the right-hand button is A.");
        add(T_CONTROLS, M_PADTOUCH, "Stick touch cursor", onOff(cfg.padTouch), "Moves a cursor on the touch screen with the right stick; click the stick to touch.");
        add(T_CONTROLS, M_REMAP_RESET, "Usual buttons", "", "Gives every DS button its usual controller button again.");

        const PartyLink link = Party_Link();
        const std::string server = cfg.onlineServer.empty() ? "Not set" : cfg.onlineServer;
        if (link == PartyLink::Off)
        {
            add(T_ONLINE, M_HOST, "Host a session", "",
                "Starts a session for up to " + std::to_string(cfg.hostMax) + " players, with a code to give to the others.");
            add(T_ONLINE, M_JOIN, "Join with a code", "", "Joins a friend's session: type the code they give you (SSR- and four letters).");
            add(T_ONLINE, M_HOSTMAX, "Players", std::to_string(cfg.hostMax), "How many players a session you host allows, you included.");
            add(T_ONLINE, M_AUTOHOST, "Host when playing", onOff(cfg.autoHost),
                "Starts a session by itself once you are in the game, so others can join with its code at any time.");
        }
        else
        {
            add(T_ONLINE, M_SESSION, "Session", Party_Code().empty() ? "Joining..." : Party_Code(),
                "Who is here, and the code to give to anyone who wants to join.");
            add(T_ONLINE, M_LEAVE, "Leave session", "", "Leaves the session and plays on alone. Your game is not affected.")->danger = true;
        }
        add(T_ONLINE, M_FRIENDS, "Friends", friends.empty() ? "" : std::to_string(friends.size()),
            "The players here now (a battle, a trade, watching their battle, being friends), your friends online and friend requests.");
        add(T_ONLINE, M_SERVER, "Server", server,
            "The address of the party server everyone uses (it runs tools/party_server.py), e.g. 203.0.113.5 or play.example.org:7373.");
        add(T_ONLINE, M_SHOWPING, "Show ping", onOff(cfg.showPing), "Shows the time a message takes to the server and back, beside the session badge.");
        add(T_ONLINE, M_TOASTS, "Activity toasts", onOff(cfg.onlineToasts),
            "Messages when someone joins, leaves, catches a Pok\xC3\xA9mon or earns a badge. They wait until you are not busy.");
        add(T_PROFILE, M_NICK, "Nickname", onlineName(), "The name the other players see. Up to 10 letters; your trainer's name unless you change it.");
        add(T_PROFILE, M_PARTNER, "Partner", cfg.onlinePartner ? Party_SpeciesName(cfg.onlinePartner) : "None",
            "The Pok\xC3\xA9mon shown with you on your card and to the others: one from your party.");
        add(T_PROFILE, M_COLOUR, "Colour", cfg.onlineColour < 0 ? "Automatic" : HudOnline::ColourName(cfg.onlineColour),
            "Your colour: your chip, and your name in messages. Automatic gives each player in a session a different one.");

        add(T_STATES, M_SAVESTATE, "Save state", "", "Saves this exact moment to come back to with Load state (F5).");
        add(T_STATES, M_LOADSTATE, "Load state", "", "Goes back to the moment saved with Save state (F8).");
    };
    buildMenu();
    auto buildSession = [&]() {
        sessionMenu.items.clear();
        const bool host = Party_Me() >= 0 && Party_Peers()[Party_Me()].host;
        MenuItem players;
        players.id = M_HOSTMAX;
        players.label = "Max players";
        players.value = std::to_string(Party_Max());
        players.disabled = !host;
        sessionMenu.items.push_back(players);
        MenuItem leave;
        leave.id = M_LEAVE;
        leave.label = "Leave session";
        leave.danger = true;
        sessionMenu.items.push_back(leave);
        if (sessionMenu.selected >= (int)sessionMenu.items.size()) sessionMenu.selected = 0;
    };
    // The buttons a player of the session can be given: watching their battle while they
    // are in one, else a battle, a trade, and asking them to be friends (not yet friends).
    auto sessionChoices = [&](int i) {
        const PartyPeer& p = Party_Peers()[i];
        if (p.state == PS_BATTLE) return std::vector<int> { PF_WATCH };
        std::vector<int> c = { PI_BATTLE, PI_TRADE };
        if (!p.uid.empty() && !friends.count(p.uid)) c.push_back(PF_ADD);
        return c;
    };
    auto buildFriends = [&]() {
        friendsMenu.items.clear();
        friendRows.clear();
        friendRowIds.clear();
        const PartyPeer* peers = Party_Peers();
        // Requests from players who have left go with them.
        requests.erase(std::remove_if(requests.begin(), requests.end(),
            [&](const FriendAsk& q) { return !peers[q.player].present || peers[q.player].uid != q.uid; }), requests.end());
        const long long now = (long long)time(nullptr);
        const bool on = Party_Link() == PartyLink::Online;
        auto addRow = [&](const HudOnline::FriendRow& r, int id) {
            friendRows.push_back(r);
            MenuItem it;
            it.id = id;
            it.tab = friendsMenu.tab;
            it.value = r.action;
            it.disabled = r.action.empty();
            friendsMenu.items.push_back(it);
        };
        if (friendsMenu.tab == 0)
            for (int i = 0; i < 4; i++)
            {
                if (!peers[i].present || i == Party_Me() || Party_Link() == PartyLink::Off) continue;
                const PartyPeer& p = peers[i];
                HudOnline::FriendRow r;
                r.who = p;
                const std::string place = Party_PlaceName(p.walk.map >= 0 ? p.walk.map : p.map);
                r.info = (place.empty() ? "" : place + " \xC2\xB7 ") + std::to_string(p.badges) + (p.badges == 1 ? " badge" : " badges") +
                    (p.state == PS_BATTLE ? " \xC2\xB7 in a battle" : p.rtt >= 0 ? " \xC2\xB7 " + std::to_string(p.rtt) + " ms" : "");
                const std::vector<int> c = sessionChoices(i);
                if (std::find(c.begin(), c.end(), inviteChoice[i]) == c.end()) inviteChoice[i] = c[0];
                r.action = inviteChoice[i] == PF_WATCH ? (watching == i ? "Stop watching" : "Watch") : inviteChoice[i] == PF_ADD ? "Add friend"
                    : inviteChoice[i] == PI_TRADE ? "Trade" : "Battle";
                addRow(r, M_FRIEND + i);
            }
        else if (friendsMenu.tab == 1)
        {
            // Online first, then the most recently seen.
            const auto& online = Party_FriendsOnline();
            std::vector<Friend> list;
            for (const auto& fr : friends) list.push_back(fr.second);
            auto isOn = [&](const Friend& f) { return on && !f.id.empty() && online.count(f.id); };
            std::sort(list.begin(), list.end(), [&](const Friend& a, const Friend& b) {
                return isOn(a) != isOn(b) ? isOn(a) : a.seen > b.seen;
            });
            for (const Friend& fr : list)
            {
                HudOnline::FriendRow r;
                r.who.name = fr.name;
                r.who.colour = fr.colour;
                const long long days = (now - fr.seen) / 86400;
                r.info = days <= 0 ? "Last seen today" : days == 1 ? "Last seen yesterday" : "Last seen " + std::to_string(days) + " days ago";
                if (fr.id.empty()) r.info += " \xC2\xB7 add them again to see when they are online";
                r.dim = true;
                if (isOn(fr))
                {
                    const PartyFriendOnline& o = online.at(fr.id);
                    r.dim = false;
                    if (o.code == Party_Code()) r.info = "In your session";
                    else
                    {
                        const bool canJoin = o.count > 1 && o.count < o.max;
                        r.info = o.count <= 1 ? "Online \xC2\xB7 on their own" : "Online \xC2\xB7 in a session of " + std::to_string(o.count);
                        r.action = canJoin && friendChoice[fr.id] == 1 ? "Join" : "Invite";
                    }
                }
                friendRowIds.push_back(fr.id);
                addRow(r, M_FRIEND + (int)friendRowIds.size() - 1);
            }
        }
        else
            for (size_t k = 0; k < requests.size(); k++)
            {
                const FriendAsk& q = requests[k];
                HudOnline::FriendRow r;
                r.who = peers[q.player];
                r.info = "Wants to be friends";
                r.action = q.no ? "Decline" : "Accept";
                addRow(r, M_FRIEND + (int)k);
            }
        friendsMenu.tabs[2].name = requests.empty() ? "Requests" : "Requests (" + std::to_string(requests.size()) + ")";
        if (friendsMenu.selected >= (int)friendsMenu.items.size()) friendsMenu.selected = 0;
    };
    auto startEdit = [&](int purpose) {
        edit = TextEdit();
        edit.purpose = purpose;
        if (purpose == E_NAME)
        {
            edit.text = onlineName();
            edit.charset = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-.'";
            edit.max = 10;
        }
        else if (purpose == E_SERVER)
        {
            edit.text = cfg.onlineServer;
            edit.charset = "abcdefghijklmnopqrstuvwxyz0123456789.-:";
            edit.max = 40;
        }
        else
        {
            edit.charset = CodeLetters;
            edit.max = 4;
        }
        edit.cursor = std::min((int)edit.text.size(), (int)edit.max - 1);
        screen = SCR_EDIT;
    };

    std::string toast;
    uint32_t toastUntil = 0;
    int shownFps = 0;
    uint32_t presented = 0, fpsSince = 0;
    ToastStack toasts;
    HudOnline::SessionStats sessionStats;
    PartyLink shownLink = PartyLink::Off;
    // A message of the frontend's own, shown at once (with the plain menu: the last one).
    auto say = [&](const std::string& msg, const std::string& icon = "", uint32_t frames = 150) {
        toast = msg;
        toastUntil = frame + frames;
        Toast t;
        t.text = msg;
        t.lead = icon;
        t.frames = frames;
        toasts.Push(t);
    };
    auto startHand = [&](int peer, int kind, bool asker) {
        hand = Handoff();
        hand.phase = H_SAVE;
        hand.peer = peer;
        hand.kind = kind;
        hand.name = Party_Peers()[peer].name;
        hand.asker = asker;
        hand.start = hand.since = frame;
        fprintf(stderr, "[handoff] frame %u: to the Union Room with player %d (%s), %s\n", frame, peer + 1,
            kind == PI_TRADE ? "trade" : "battle", asker ? "asking" : "answering");
    };
    auto answerInvite = [&](bool yes) {
        if (inviteKind == PI_SESSION)
        {
            // A friend's session: joined at once (this one is left).
            if (yes) Party_Join(cfg.onlineServer, inviteCode), say("Joining " + inviteWho.name + "'s session...", "icons/icon_wifi");
            screen = SCR_NONE;
            inviteCode.clear();
            return;
        }
        Party_SendInvite(inviteFrom, (yes ? PI_YES : PI_NO) | inviteKind);
        if (yes)
        {
            say("Going to the Union Room with " + Party_Peers()[inviteFrom].name + "\xE2\x80\xA6", "icons/icon_wifi", 240);
            startHand(inviteFrom, inviteKind, false);
        }
        screen = SCR_NONE;
        inviteFrom = -1;
    };
#ifdef __ANDROID__
    bool updateSaid = false;
    UpdateCheck::Start();
#endif

    std::vector<uint32_t> shot;
    std::vector<int16_t> audioBuf(48000 * 2);
    bool running = true;
    bool mouseDown = false;
    int mouseX = 0, mouseY = 0;
    // On-screen controls (touch screens): shown while no controller is connected.
    TouchPad touchPad;
    bool touchPadUsed = false, touchMenuHeld = false, prevTouchMenu = false;
#ifdef __ANDROID__
    touchPadUsed = true;
#endif
    // Touch cursor for devices without a touchscreen: moved with the right stick or IJKL,
    // pressed with the right trigger or Space.
    float cursorX = 128, cursorY = 96;
    uint32_t cursorShownUntil = 0;
    PadState prevPad;
    MenuInput heldNav;          // navigation directions held last tick, for edge detection
    bool prevComboMenu = false;
    bool swallowMenuButtons = false; // keeps the buttons that closed the menu from reaching the game
    const double frameTime = 1.0 / 59.8261;
    uint64_t perfFreq = opt.headless ? 1 : SDL_GetPerformanceFrequency();
    uint64_t nextTick = opt.headless ? 0 : SDL_GetPerformanceCounter();

    Sampler::Start();
    double perfAcc[6] = {};
    double perfWorst = 0;
    int perfN = 0;
    auto perfNow = []() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    double perfT = perfNow(), perfFrameStart = perfT;
    auto perfMark = [&](int phase) {
        if (!perfOn) return;
        double t = perfNow();
        perfAcc[phase] += t - perfT;
        perfT = t;
    };

    while (running && !Host::StopRequested)
    {
        if (perfOn)
        {
            double t = perfNow();
            if (t - perfFrameStart > perfWorst) perfWorst = t - perfFrameStart;
            perfFrameStart = perfT = t;
            if (++perfN == 300)
            {
                fprintf(stderr, "[perf] input %.2f  emulate %.2f  scene %.2f  compose %.2f  present %.2f  wait %.2f ms; worst frame %.1f ms\n",
                    perfAcc[0] / perfN, perfAcc[1] / perfN, perfAcc[2] / perfN, perfAcc[3] / perfN, perfAcc[4] / perfN,
                    perfAcc[5] / perfN, perfWorst);
                for (double& a : perfAcc) a = 0;
                perfN = 0;
                perfWorst = 0;
            }
        }
        uint32_t keys = 0;
        bool touching = false;
        int touchX = 0, touchY = 0;
        MenuInput nav;              // edge-triggered menu input for this tick
        bool toggleMenu = false;
        bool kbFast = false;
        float curDX = 0, curDY = 0;
        bool curPress = false;
        PadState ps;

        int outW = opt.composeW > 0 ? opt.composeW : 1280, outH = opt.composeH > 0 ? opt.composeH : 720;

        if (!opt.headless)
        {
            SDL_Event ev;
            touchPad.NextFrame();
            while (SDL_PollEvent(&ev))
            {
                if (ev.type == SDL_QUIT) running = false;
                else if (ev.type == SDL_APP_WILLENTERBACKGROUND || ev.type == SDL_APP_TERMINATING)
                {
                    // A phone may close the app at any time once it is in the background.
                    if (!opt.noSave) FlushSave(opt.save);
                    if (autosaveOk && cfg.autosave && played && !opt.noSave)
                    {
                        int m = -1, x, z;
                        if (!playerPos(m, x, z)) m = -1;
                        resume.Write(*nds, m, Party_PlaceName(m));
                        resume.Finish();
                    }
                    cfg.Save(opt.config);
                }
                else if (touchPadUsed && (ev.type == SDL_FINGERDOWN || ev.type == SDL_FINGERMOTION || ev.type == SDL_FINGERUP))
                {
                    int ow = 0, oh = 0;
                    SDL_GetRendererOutputSize(renderer, &ow, &oh);
                    touchPad.Layout(ow, oh);
                    float fx = ev.tfinger.x * ow, fy = ev.tfinger.y * oh;
                    if (ev.type == SDL_FINGERDOWN)
                    {
                        // A finger on the screen brings the controls back (that finger only
                        // touches the picture, see Down).
                        touchPad.Down(ev.tfinger.fingerId, fx, fy);
                        touchPad.visible = true;
                    }
                    else if (ev.type == SDL_FINGERMOTION) touchPad.Move(ev.tfinger.fingerId, fx, fy);
                    else touchPad.Up(ev.tfinger.fingerId);
                }
                else if (ev.type == SDL_KEYDOWN && !ev.key.repeat)
                {
                    SDL_Keycode k = ev.key.keysym.sym;
                    const bool uiOpen = menu.open || screen != SCR_NONE || startOpen;
                    const bool typing = screen == SCR_EDIT;
                    if (k == SDLK_ESCAPE || k == SDLK_AC_BACK)
                    {
                        if (uiOpen) nav.back = true;
                        else toggleMenu = true;
                    }
                    else if (uiOpen)
                    {
                        if (k == SDLK_UP) nav.up = true;
                        else if (k == SDLK_DOWN) nav.down = true;
                        else if (k == SDLK_LEFT) nav.left = true;
                        else if (k == SDLK_RIGHT) nav.right = true;
                        else if (k == SDLK_RETURN || (!typing && k == SDLK_x)) nav.accept = true;
                        else if (typing && k == SDLK_BACKSPACE && edit.cursor > 0)
                        {
                            edit.text.erase(--edit.cursor, 1);
                        }
                        else if (!typing && k == SDLK_z) nav.back = true;
                        else if (!typing && k == SDLK_q) nav.tabPrev = true;
                        else if (!typing && k == SDLK_w) nav.tabNext = true;
                    }
                    else if (k == SDLK_TAB) layout = LayoutNext(layout);
                    else if (k == SDLK_F1) focus.manualFlip = !focus.manualFlip;
                    else if (k == SDLK_F2) focus.mode = (focus.mode + 1) % 3;
                    else if (k == SDLK_F5) say(WriteSnapshot(*nds, quickState, frame) ? "State saved" : "The state could not be saved", "icons/icon_save");
                    else if (k == SDLK_F8)
                    {
                        say(ReadSnapshot(*nds, quickState, nullptr) ? "State loaded" : "No state to load", "icons/icon_layers");
                        SetClock(*nds, false);
                    }
                    else if (k == SDLK_F11
#ifdef __APPLE__
                        || (k == SDLK_f && (ev.key.keysym.mod & KMOD_GUI) && (ev.key.keysym.mod & KMOD_CTRL))
#endif
                    )
                    {
                        cfg.fullscreen = !cfg.fullscreen;
                        SDL_SetWindowFullscreen(window, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        SDL_ShowCursor(cfg.fullscreen ? SDL_DISABLE : SDL_ENABLE);
                    }
                }
                else if (ev.type == SDL_TEXTINPUT && screen == SCR_EDIT)
                {
                    // Typed letters, where the text takes them.
                    for (const char* t = ev.text.text; *t; t++)
                    {
                        char ch = *t;
                        if (edit.purpose == E_CODE && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
                        if (edit.charset.find(ch) == std::string::npos || edit.text.size() >= edit.max) continue;
                        edit.text.insert(edit.text.begin() + edit.cursor, ch);
                        edit.cursor = std::min(edit.cursor + 1, (int)edit.max - 1);
                    }
                }
                else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                    mouseDown = false;
                else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) mouseDown = true;
                else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) mouseDown = false;
                // (Every joystick is announced by SDL_JOYDEVICEADDED, game controllers also
                // by SDL_CONTROLLERDEVICEADDED; Open takes each once.)
                else if (ev.type == SDL_JOYDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEADDED)
                {
                    if (pads.Open(ev.jdevice.which)) say("Controller connected", "icons/icon_gamepad");
                }
                else if (ev.type == SDL_JOYDEVICEREMOVED && pads.Close(ev.jdevice.which))
                {
                    // Pause rather than leave the game running with nobody at the controls.
                    if (pads.Empty() && !menu.open) toggleMenu = true;
                    say("Controller disconnected", "icons/icon_gamepad");
                }
            }
            SDL_GetMouseState(&mouseX, &mouseY);
            SDL_GetRendererOutputSize(renderer, &outW, &outH);
            // SDL mouse positions are window points; layout, cursor and touch use output pixels.
            // Refresh each frame for resize and movement between Retina/external displays.
            int windowW = 0, windowH = 0;
            SDL_GetWindowSize(window, &windowW, &windowH);
            if (outW <= 0 || outH <= 0 || windowW <= 0 || windowH <= 0 ||
                (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED))
            {
                SDL_Delay(16);
                continue;
            }
            mouseX = int(double(mouseX) * outW / windowW);
            mouseY = int(double(mouseY) * outH / windowH);
            ps = ReadPad(pads);
            if (touchPadUsed)
            {
                touchPad.Layout(outW, outH);
                // Using a controller hides them: a connected one is not proof enough, as
                // Android reports some built-in keys as one.
                bool padUsed = fabsf(ps.lx) > 0.5f || fabsf(ps.ly) > 0.5f;
                for (int i = 0; i < PAD_COUNT; i++) padUsed |= ps.btn[i];
                if (padUsed) touchPad.visible = false;
                // A finger on the picture touches the game's touch screen, like the mouse.
                float fx, fy;
                mouseDown = touchPad.ScreenFinger(fx, fy);
                if (mouseDown)
                {
                    mouseX = (int)fx;
                    mouseY = (int)fy;
                }
                // The on-screen buttons go by their labels: the one marked A is the DS's A.
                bool held[PAD_COUNT] = {};
                touchPad.Apply(held, touchMenuHeld);
                if (!cfg.byLabel)
                {
                    std::swap(held[PAD_A], held[PAD_B]);
                    std::swap(held[PAD_X], held[PAD_Y]);
                }
                for (int i = 0; i < PAD_COUNT; i++) ps.btn[i] |= held[i];
                if (touchMenuHeld && !prevTouchMenu) toggleMenu = true;
                prevTouchMenu = touchMenuHeld;
            }

            const Uint8* kb = SDL_GetKeyboardState(nullptr);
            if (!menu.open)
            {
                keys |= KeyboardKeys();
                if (kb[SDL_SCANCODE_J]) curDX -= 1;
                if (kb[SDL_SCANCODE_L]) curDX += 1;
                if (kb[SDL_SCANCODE_I]) curDY -= 1;
                if (kb[SDL_SCANCODE_K]) curDY += 1;
                if (kb[SDL_SCANCODE_SPACE]) curPress = true;
                if (kb[SDL_SCANCODE_LSHIFT]) kbFast = true;
            }
        }

        const bool mashing = opt.battleMash && appRunning("Battle");
        if (mashing && (frame / 12) % 2 == 0)
        {
            // A fights; if that wandered into the in-battle party or bag screen (e.g. by answering
            // Yes to "switch Pokemon?"), B backs out and declines the question that led there.
            // If B does not leave it (the lead fainted), move to the next Pokemon and send it out.
            static uint32_t submenuSeen = 0, submenuSince = 0;
            if (sectionActive("OVY_8"))
            {
                if (!submenuSince || frame - submenuSeen > 24) submenuSince = frame;
                submenuSeen = frame;
            }
            if (submenuSeen == frame && frame - submenuSince > 600)
            {
                static const int cycle[4] = { KEY_B, KEY_RIGHT, KEY_A, KEY_A };
                keys |= 1 << cycle[(frame / 24) % 4];
            }
            else if (submenuSeen && frame - submenuSeen < 240)
                keys |= 1 << KEY_B;
            else if ((frame / 24) % 2 == 0)
                keys |= 1 << KEY_A;
            else
            {
                // Every other press is a touch on one of the left-hand moves (FIGHT in the command
                // menu), so that the first move running out of PP does not stall the battle.
                touching = true;
                touchX = 60;
                touchY = (frame / 48) % 2 ? 115 : 55;
            }
        }
        bool navigating = false;
        if (navState.active && opt.navStopOnBattle && navState.started && appRunning("Battle"))
        {
            printf("nav: battle started at frame %u, stopping\n", frame);
            navState.active = false;
        }
        if (navState.active && !mashing && scriptClock >= opt.navAfter)
        {
            int map, x, z;
            if (playerPos(map, x, z))
            {
                navigating = true;
                if (!navState.started) navState.started = frame;
                if (navState.map != -1 && map != navState.map)
                {
                    // Went through a door: that waypoint is done and the grid is a new one.
                    printf("nav: map %d -> %d at frame %u\n", navState.map, map, frame);
                    if (navState.idx < navState.way.size() &&
                        std::abs(navState.way[navState.idx].first - navState.fromX) +
                        std::abs(navState.way[navState.idx].second - navState.fromZ) <= 2)
                        navState.idx++;
                    navState.blocked.clear();
                    navState.dir = -1;
                    navState.pushDir = -1;
                }
                navState.map = map;
                navState.outdoor = x >= 96 || z >= 96; // interiors have their own small grids
                while (navState.idx < navState.way.size() && navState.way[navState.idx] == std::make_pair(x, z))
                {
                    printf("nav: reached %d,%d at frame %u\n", x, z, frame);
                    navState.pushDir = navState.push[navState.idx];
                    navState.idx++;
                    navState.dir = -1;
                }
                if (navState.pushDir >= 0)
                {
                    static const int pushKeys[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
                    keys |= 1 << pushKeys[navState.pushDir];
                }
                else if (navState.idx >= navState.way.size() || frame - navState.started > 150000)
                {
                    printf("nav: %s at frame %u, map %d x %d z %d\n",
                        navState.idx >= navState.way.size() ? "done" : "gave up", frame, map, x, z);
                    navState.active = false;
                    navigating = false;
                }
                else
                {
                    if (navState.dir >= 0 && (x != navState.fromX || z != navState.fromZ))
                        navState.dir = -1; // moved
                    else if (navState.dir >= 0 && frame - navState.since > 60)
                    {
                        navState.blocked.insert({ x, z, navState.dir });
                        navState.dir = -1;
                    }
                    if (navState.dir < 0)
                    {
                        auto [tx, tz] = navState.way[navState.idx];
                        navState.dir = navStep(x, z, tx, tz);
                        if (navState.dir < 0)
                        {
                            navState.blocked.clear();
                            navState.dir = navStep(x, z, tx, tz);
                        }
                        navState.fromX = x;
                        navState.fromZ = z;
                        navState.since = frame;
                    }
                    static const int navKeys[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
                    if (navState.dir >= 0)
                    {
                        keys |= 1 << navKeys[navState.dir];
                        navState.lastDir = navState.dir;
                    }
                    else if (navState.lastDir >= 0)
                        keys |= 1 << navKeys[navState.lastDir]; // side doors open by walking on into them
                    // With no route (e.g. mid-warp, the target is on the next map) just wait.
                    // Dismiss anything that stops the walk (signs, people talking).
                    if (frame - navState.since > 24 && (frame / 6) % 2 == 0) keys |= 1 << KEY_B;
                }
            }
        }
        // Talk (--talk-after): walk up to the nearest other person on the map (people move,
        // so the target is chosen afresh each frame), face them and press A; the script then
        // answers the conversation. A conversation pauses the player's own movement (map
        // object flag 0x40); if that has not happened within a second, the person stepped
        // away as A was pressed, so the walk starts again.
        static bool talkDone = false;
        static uint32_t talkAt = 0, talkSince = 0;
        if (opt.talkAfter && !talkDone && scriptClock >= opt.talkAfter)
        {
            if (!talkSince) talkSince = frame;
            if (frame - talkSince > 3600)
            {
                talkDone = true; // nobody to talk to: let the script and the frame count go on
                printf("talk: gave up at frame %u\n", frame);
            }
        }
        if (opt.talkAfter && talkAt && !talkDone)
        {
            if (frame - talkAt < 5) keys |= 1 << KEY_A; // held a few frames, as a tap: the field reads input every other frame
            const uint32_t me = readRam(readRam(readRam(0x021D4158) + 0x40) + 0x30);
            if (readRam(me) & 0x40)
            {
                talkDone = true;
                printf("talk: in conversation (frame %u)\n", frame);
            }
            else if (frame - talkAt >= 60)
                talkAt = 0;
        }
        if (opt.talkAfter && !talkDone && !talkAt && !navigating && scriptClock >= opt.talkAfter)
        {
            int map, x, z;
            const uint32_t fs = readRam(0x021D4158), mgr = readRam(fs + 0x3C), me = readRam(readRam(fs + 0x40) + 0x30);
            const uint32_t count = readRam(mgr + 4), objs = readRam(mgr + 0x124);
            int best = 1 << 30, tx = 0, tz = 0;
            if (playerPos(map, x, z) && objs && count < 256)
                for (uint32_t i = 0; i < count; i++)
                {
                    const uint32_t o = objs + i * 0x12C;
                    if (o == me || (readRam(o) & 0x201) != 1) continue; // not the player; active and not hidden
                    const int ox = (int)readRam(o + 0x64), oz = (int)readRam(o + 0x6C);
                    const int d = std::abs(ox - x) + std::abs(oz - z);
                    if (d < best) { best = d; tx = ox; tz = oz; }
                }
            if (best < (1 << 30))
            {
                navigating = true;
                static const int talkKeys[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
                if (best == 1)
                {
                    const int dir = tz < z ? 0 : tz > z ? 1 : tx < x ? 2 : 3;
                    // currentFacing: 0 up, 1 down, 2 left, 3 right, as the directions here.
                    if ((int)readRam(me + 0x28) != dir) { if (frame % 8 < 2) keys |= 1 << talkKeys[dir]; } // two frames: the field reads every other one
                    else
                    {
                        keys |= 1 << KEY_A;
                        talkAt = frame;
                    }
                }
                else
                {
                    const int dir = navStep(x, z, tx, tz);
                    if (dir >= 0) keys |= 1 << talkKeys[dir];
                }
            }
        }
        const bool holdScript = mashing || navigating;
        for (const ScriptEvent& ev : script)
        {
            if (holdScript) break;
            if (scriptClock < ev.start || scriptClock > ev.end) continue;
            keys |= ev.keys;
            if (ev.touch)
            {
                touching = true;
                touchX = ev.tx;
                touchY = ev.ty;
            }
            for (int i = 0; i < PAD_COUNT; i++)
                if (ev.padButtons & (1u << i)) ps.btn[i] = true;
            if (ev.lx != 0) ps.lx = ev.lx;
            if (ev.ly != 0) ps.ly = ev.ly;
            if (ev.rx != 0) ps.rx = ev.rx;
            if (ev.ry != 0) ps.ry = ev.ry;
            if (ev.lt != 0) ps.lt = ev.lt;
            if (ev.rt != 0) ps.rt = ev.rt;
        }

        // Controller: the pause menu opens with Guide, or with Start and Select together.
        auto pressed = [&](int b) { return ps.btn[b] && !prevPad.btn[b]; };
        bool comboMenu = ps.btn[PAD_START] && ps.btn[PAD_BACK];
        if (pressed(PAD_GUIDE) || (comboMenu && !prevComboMenu)) toggleMenu = true;
        // + (Start) opens and closes it too on the overworld, when nothing of the game is open
        // (the game does not use Start there); anywhere else it stays the game's Start.
        if (pressed(PAD_START) && !ps.btn[PAD_BACK] && !startOpen && remapping < 0 && screen == SCR_NONE && (menu.open || fieldIdle()))
            toggleMenu = true;
        prevComboMenu = comboMenu;

        MenuInput heldNow;
        heldNow.up = ps.btn[PAD_UP] || ps.ly < -0.5f;
        heldNow.down = ps.btn[PAD_DOWN] || ps.ly > 0.5f;
        heldNow.left = ps.btn[PAD_LEFT] || ps.lx < -0.5f;
        heldNow.right = ps.btn[PAD_RIGHT] || ps.lx > 0.5f;
        if (menu.open || screen != SCR_NONE || startOpen)
        {
            nav.up |= heldNow.up && !heldNav.up;
            nav.down |= heldNow.down && !heldNav.down;
            nav.left |= heldNow.left && !heldNav.left;
            nav.right |= heldNow.right && !heldNav.right;
            nav.accept |= pressed(cfg.byLabel ? PAD_A : PAD_B);
            nav.back |= pressed(cfg.byLabel ? PAD_B : PAD_A);
            nav.tabPrev |= pressed(PAD_LB);
            nav.tabNext |= pressed(PAD_RB);
        }
        heldNav = heldNow;

        // The online screens and the start screen take the input before the pause menu.
        auto finishEdit = [&]() {
            const std::string t = edit.text;
            if (edit.purpose == E_NAME)
            {
                std::string name = t;
                while (!name.empty() && name.back() == ' ') name.pop_back();
                cfg.onlineName = name;
                Party_SetProfile(onlineName(), cfg.onlineColour);
            }
            else if (edit.purpose == E_SERVER)
                cfg.onlineServer = t;
            else if (t.size() == 4)
            {
                Party_Join(cfg.onlineServer, "SSR-" + t);
                say("Joining SSR-" + t + "...", "icons/icon_wifi");
            }
            screen = SCR_NONE;
            buildMenu();
        };
        if (screen == SCR_EDIT && !toggleMenu)
        {
            const size_t cs = edit.charset.size();
            auto at = [&]() -> char { return edit.cursor < (int)edit.text.size() ? edit.text[edit.cursor] : 0; };
            if (nav.up || nav.down)
            {
                // The next or previous letter at the cursor (after the text: a new one, from A).
                if (at())
                {
                    size_t i = edit.charset.find(at());
                    if (i == std::string::npos) i = 0;
                    edit.text[edit.cursor] = edit.charset[(i + (nav.up ? 1 : cs - 1)) % cs];
                }
                else if (edit.text.size() < edit.max)
                    edit.text += edit.charset[nav.up ? (edit.charset[0] == ' ' ? 1 : 0) : cs - 1];
                uiSound(GameSfx::Cursor);
            }
            if (nav.right && edit.cursor < (int)std::min(edit.text.size(), edit.max - 1)) edit.cursor++, uiSound(GameSfx::Cursor);
            if (nav.left && edit.cursor > 0) edit.cursor--, uiSound(GameSfx::Cursor);
            if (pressed(cfg.byLabel ? PAD_X : PAD_Y) && at()) edit.text.erase(edit.cursor, 1), uiSound(GameSfx::Cancel);
            if (nav.accept && (edit.purpose != E_CODE || edit.text.size() == 4)) finishEdit(), uiSound(GameSfx::Confirm);
            else if (nav.back) screen = SCR_NONE, uiSound(GameSfx::Cancel);
        }
        else if (screen == SCR_INVITE && inviteKind != PI_SESSION && (Party_Link() != PartyLink::Online || inviteFrom < 0 || !Party_Peers()[inviteFrom].present))
            screen = SCR_NONE; // the session or the player has gone
        else if (screen == SCR_INVITE && Party_Now() - inviteAt > 20000)
            answerInvite(false); // no answer in time
        else if (screen == SCR_INVITE && !toggleMenu)
        {
            int dir = 0;
            const int hit = menuUpdate(inviteMenu, nav, dir);
            if (hit >= 0 && dir == 0) answerInvite(hit == M_ACCEPT);
            else if (nav.back) answerInvite(false), uiSound(GameSfx::Cancel);
        }
        else if (screen == SCR_FRIENDS && !toggleMenu)
        {
            buildFriends();
            int dir = 0;
            const int hit = menuUpdate(friendsMenu, nav, dir);
            if (nav.tabPrev || nav.tabNext) friendsMenu.selected = 0;
            const int who = hit - M_FRIEND;
            if (friendsMenu.tab == 0 && who >= 0 && who < 4)
            {
                const std::vector<int> c = sessionChoices(who);
                const int at = (int)(std::find(c.begin(), c.end(), inviteChoice[who]) - c.begin());
                if (dir) inviteChoice[who] = c[(at + (dir > 0 ? 1 : (int)c.size() - 1)) % c.size()];
                else if (inviteChoice[who] == PF_WATCH)
                {
                    // Back to the game, where the card is shown.
                    watching = watching == who ? -1 : who;
                    screen = SCR_NONE;
                    menu.open = false;
                    swallowMenuButtons = true;
                }
                else if (inviteChoice[who] == PF_ADD)
                {
                    Party_SendFriend(who, PF_ASK);
                    say("Asked " + Party_Peers()[who].name + " to be friends", "icons/icon_wifi");
                }
                else
                {
                    Party_SendInvite(who, inviteChoice[who]);
                    say("Asked " + Party_Peers()[who].name + (inviteChoice[who] == PI_TRADE ? " for a trade" : " for a battle"), "icons/icon_wifi");
                    screen = SCR_NONE;
                }
            }
            else if (friendsMenu.tab == 1 && who >= 0 && who < (int)friendRowIds.size())
            {
                const std::string id = friendRowIds[who];
                const auto& online = Party_FriendsOnline();
                if (dir) friendChoice[id] ^= 1;
                else if (online.count(id) && friendChoice[id] == 1 && online.at(id).count < online.at(id).max)
                {
                    // Into their session (this one is left).
                    Party_Join(cfg.onlineServer, online.at(id).code);
                    say("Joining " + friends[id].name + "'s session...", "icons/icon_wifi");
                    screen = SCR_NONE;
                }
                else if (online.count(id))
                {
                    Party_InviteFriend(id);
                    say("Invited " + friends[id].name + " to your session", "icons/icon_wifi");
                }
            }
            else if (friendsMenu.tab == 2 && who >= 0 && who < (int)requests.size())
            {
                FriendAsk& q = requests[who];
                if (dir) q.no = !q.no;
                else
                {
                    const PartyPeer& p = Party_Peers()[q.player];
                    Party_SendFriend(q.player, q.no ? PF_NO : PF_YES);
                    if (!q.no && p.present && p.uid == q.uid) addFriend(p);
                    requests.erase(requests.begin() + who);
                }
            }
            if (nav.back) screen = SCR_NONE, uiSound(GameSfx::Cancel);
        }
        else if (screen == SCR_SESSION && Party_Link() == PartyLink::Off)
            screen = SCR_NONE; // the session has ended
        else if (screen == SCR_SESSION && !toggleMenu)
        {
            buildSession();
            int dir = 0;
            const int hit = menuUpdate(sessionMenu, nav, dir);
            if (hit == M_HOSTMAX && dir) Party_SetMax(std::clamp(Party_Max() + dir, 2, 4));
            else if (hit == M_LEAVE && dir == 0) screen = SCR_SUMMARY;
            else if (nav.back) screen = SCR_NONE, uiSound(GameSfx::Cancel);
        }
        else if (screen == SCR_SUMMARY && !toggleMenu)
        {
            if (nav.accept)
            {
                uiSound(GameSfx::Confirm);
                Party_Leave();
                say("You left the session", "icons/icon_exit");
                screen = SCR_NONE;
                buildMenu();
            }
            else if (nav.back) screen = SCR_NONE, uiSound(GameSfx::Cancel);
        }
        else if (startOpen && titleOpen && !menu.open && !toggleMenu)
        {
            if (nav.accept || nav.back || pressed(PAD_START) || pressed(PAD_A)) titleOpen = false, uiSound(GameSfx::Confirm);
        }
        else if (startOpen && !menu.open && !toggleMenu)
        {
            const int n = (int)startActions.size();
            if (nav.left) startSel = (startSel + n - 1) % n, uiSound(GameSfx::Cursor);
            if (nav.right) startSel = (startSel + 1) % n, uiSound(GameSfx::Cursor);
            if (nav.accept || pressed(PAD_START))
            {
                uiSound(GameSfx::Confirm);
                const int act = startActions[startSel];
                if (act == ST_CONTINUE || act == ST_NEW)
                {
                    startOpen = false;
                    swallowMenuButtons = true;
                    if (bootSkipOk) bootMode = act == ST_CONTINUE ? BOOT_CONTINUE : BOOT_NEW;
                    if (act == ST_CONTINUE && tryResume())
                    {
                        bootMode = BOOT_NONE;
                        resumeCoverUntil = frame + 10;
                    }
                }
                else if (act == ST_QUIT) running = false;
                else
                {
                    menu.open = true;
                    menu.tab = act == ST_ONLINE ? T_ONLINE : T_DISPLAY;
                    menu.selected = 0;
                    buildMenu();
                }
            }
        }
        else if (toggleMenu && screen != SCR_NONE)
            screen = SCR_NONE, uiSound(GameSfx::Cancel);
        else if (toggleMenu && startOpen && !menu.open)
            ; // the start screen stays until Play
        else if (toggleMenu)
        {
            menu.open = !menu.open;
            if (menu.open)
            {
                menu.selected = 0;
                buildMenu();
                uiSound(GameSfx::Confirm);
            }
            else swallowMenuButtons = true, uiSound(GameSfx::Cancel);
        }
        else if (menu.open && remapping >= 0)
        {
            // Waiting for the controller button for a DS button: the next one pressed, unless
            // Escape is pressed or 5 seconds go by.
            int got = -1;
            for (int b = 0; b < PAD_COUNT && got < 0; b++)
                if (b != PAD_GUIDE && pressed(b)) got = b;
            if (got >= 0)
            {
                // A button another DS button had swaps places with this one's.
                const int old = PadButtonFor(remapping, cfg.byLabel, cfg.padMap);
                for (int i = 0; i < RemapCount; i++)
                    if (i != remapping && PadButtonFor(i, cfg.byLabel, cfg.padMap) == got) cfg.padMap[i] = old;
                cfg.padMap[remapping] = got;
            }
            if (got >= 0 || nav.back || frame - remapSince > 300)
            {
                uiSound(got >= 0 ? GameSfx::Confirm : GameSfx::Cancel);
                remapping = -1;
                swallowMenuButtons = true;
                buildMenu();
            }
        }
        else if (menu.open)
        {
            if (nav.back)
            {
                menu.open = false;
                swallowMenuButtons = true;
                uiSound(GameSfx::Cancel);
            }
            // Y on a button row: back to the usual controller button.
            if (const MenuItem* sel = menu.Selected(); sel && sel->id >= M_REMAP && sel->id < M_REMAP + RemapCount && pressed(PAD_Y))
            {
                cfg.padMap[sel->id - M_REMAP] = -1;
                buildMenu();
            }
            int dir = 0;
            int hit = menuUpdate(menu, nav, dir);
            if (hit >= M_REMAP && hit < M_REMAP + RemapCount && dir == 0)
            {
                remapping = hit - M_REMAP;
                remapSince = frame;
            }
            switch (hit)
            {
            case M_RESUME:
                menu.open = false;
                swallowMenuButtons = true;
                break;
            case M_LAYOUT:
                layout.mode = (LayoutMode)(((int)layout.mode + (dir < 0 ? 5 : 1)) % 6);
                break;
            case M_LARGE:
                focus.mode = (focus.mode + (dir < 0 ? 2 : 1)) % 3;
                focus.manualFlip = false;
                break;
            case M_FPSMAX:
                cfg.fpsUnlimited = !cfg.fpsUnlimited;
                break;
            case M_FPS60:
                cfg.fps60 = !cfg.fps60;
                nds->GPU.GPU3D.InterpolationEnabled = cfg.fps60;
                break;
            case M_FULLSCREEN:
                cfg.fullscreen = !cfg.fullscreen;
                if (window)
                {
                    SDL_SetWindowFullscreen(window, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_ShowCursor(cfg.fullscreen ? SDL_DISABLE : SDL_ENABLE);
                }
                break;
            case M_SMOOTH:
                cfg.smooth = !cfg.smooth;
                applySmooth();
                break;
            case M_VSYNC:
                cfg.vsync = !cfg.vsync;
                applyVSync();
                break;
            case M_ASPECT:
                cfg.aspect = (cfg.aspect + (dir < 0 ? AspectCount - 1 : 1)) % AspectCount;
                // In a window (not maximised) the window takes the new shape, keeping its height.
                if (window && cfg.aspect && !(SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED)))
                {
                    int ww, wh;
                    SDL_GetWindowSize(window, &ww, &wh);
                    SDL_SetWindowSize(window, (int)(wh * AspectRatios[cfg.aspect].ratio + 0.5f), wh);
                }
                break;
            case M_WINDOWSIZE:
            {
                // The next preset up or down, wrapping round; heights taller than the screen are skipped.
                SDL_Rect usable = { 0, 0, 0, 0 };
                if (window) SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(window), &usable);
                int fit = WindowHeightCount;
                while (fit > 1 && usable.h > 0 && WindowHeights[fit - 1] > usable.h) fit--;
                int i = 0;
                while (i < fit && WindowHeights[i] != cfg.windowH) i++;
                if (i == fit) i = dir < 0 ? 0 : fit - 1; // not a preset yet: start from an end
                i = (i + (dir < 0 ? fit - 1 : 1)) % fit;
                cfg.windowH = WindowHeights[i];
                if (window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN))
                {
                    const float ratio = cfg.aspect ? AspectRatios[cfg.aspect].ratio : 16 / 9.0f;
                    SDL_RestoreWindow(window);
                    SDL_SetWindowSize(window, (int)(cfg.windowH * ratio + 0.5f), cfg.windowH);
                    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                }
                break;
            }
            case M_ZOOM:
            {
                int i = 0;
                while (i + 1 < ZoomCount && ZoomSteps[i] < cfg.zoom) i++;
                cfg.zoom = ZoomSteps[(i + (dir < 0 ? ZoomCount - 1 : 1)) % ZoomCount];
                break;
            }
            case M_FARMAP:
                cfg.farMap = !cfg.farMap;
                break;
            case M_BLUR:
                cfg.blur = (cfg.blur + (dir < 0 ? 2 : 1)) % 3;
                break;
            case M_NIGHTFX:
                cfg.nightFx = !cfg.nightFx;
                break;
            case M_TEXTFONT:
            {
                // The fonts in turn, then the game's letters.
                const FontFamily* f = textFamily();
                int i = -1;
                for (size_t k = 0; k < fontFamilies.size(); k++)
                    if (f == &fontFamilies[k]) i = (int)k;
                const int n = (int)fontFamilies.size() + 1;
                i = ((i < 0 ? n - 1 : i) + (dir < 0 ? n - 1 : 1)) % n;
                cfg.textFont = i == n - 1 ? "game" : fontFamilies[i].name;
                applyFont();
                break;
            }
            case M_MUSIC:
                cfg.newSongs = !cfg.newSongs;
                break;
            case M_MENUSOUNDS:
                cfg.menuSounds = !cfg.menuSounds;
                break;
            case M_REDRAWN:
                cfg.redrawn = !cfg.redrawn;
                break;
            case M_VOIDFILL:
                cfg.voidFill = !cfg.voidFill;
                break;
            case M_WIDE:
                cfg.wide = !cfg.wide;
                break;
            case M_WIDELINES:
                cfg.wideLines = detailLines[(detailIndex() + (dir < 0 ? detailCount - 1 : 1)) % detailCount];
                break;
            case M_SHOWFPS:
                cfg.showFps = !cfg.showFps;
                break;
            case M_BUTTONS:
                cfg.byLabel = !cfg.byLabel;
                break;
            case M_PADTOUCH:
                cfg.padTouch = !cfg.padTouch;
                break;
            case M_AUTOHOST:
                cfg.autoHost = !cfg.autoHost;
                break;
            case M_HOST:
                if (cfg.onlineServer.empty()) startEdit(E_SERVER);
                else
                {
                    Party_Host(cfg.onlineServer, cfg.hostMax);
                    say("Starting a session...", "icons/icon_wifi");
                    screen = SCR_SESSION;
                }
                break;
            case M_JOIN:
                startEdit(cfg.onlineServer.empty() ? E_SERVER : E_CODE);
                break;
            case M_HOSTMAX:
                cfg.hostMax = std::clamp(cfg.hostMax + (dir < 0 ? -1 : 1), 2, 4);
                break;
            case M_SESSION:
                screen = SCR_SESSION;
                break;
            case M_FRIENDS:
                friendsMenu.tab = !requests.empty() ? 2 : Party_Link() == PartyLink::Off && !friends.empty() ? 1 : 0;
                friendsMenu.selected = 0;
                // Online for the friends' presence: a session of this player's own.
                if (Party_Link() == PartyLink::Off && !cfg.onlineServer.empty()) Party_Host(cfg.onlineServer, cfg.hostMax);
                screen = SCR_FRIENDS;
                break;
            case M_LEAVE:
                screen = SCR_SUMMARY;
                break;
            case M_SERVER:
                startEdit(E_SERVER);
                break;
            case M_SHOWPING:
                cfg.showPing = !cfg.showPing;
                break;
            case M_TOASTS:
                cfg.onlineToasts = !cfg.onlineToasts;
                break;
            case M_NICK:
                startEdit(E_NAME);
                break;
            case M_COLOUR:
                cfg.onlineColour = (cfg.onlineColour + 1 + (dir < 0 ? 4 : 1)) % 5 - 1; // Automatic, then the four colours
                Party_SetProfile(onlineName(), cfg.onlineColour);
                break;
            case M_PARTNER:
            {
                // None, then the party's species in order.
                std::vector<int> choices = Party_MySpecies();
                choices.insert(choices.begin(), 0);
                auto it = std::find(choices.begin(), choices.end(), cfg.onlinePartner);
                int i = it == choices.end() ? 0 : (int)(it - choices.begin());
                i = (i + (dir < 0 ? (int)choices.size() - 1 : 1)) % (int)choices.size();
                cfg.onlinePartner = choices[i];
                Party_SetPartner(cfg.onlinePartner);
                break;
            }
            case M_STARTSCREEN:
                cfg.startScreen = !cfg.startScreen;
                break;
            case M_AUTOSAVE:
                cfg.autosave = !cfg.autosave;
                break;
            case M_AUTOSAVEGAME:
                cfg.autosaveGame = !cfg.autosaveGame;
                break;
            case M_REMAP_RESET:
                for (int& b : cfg.padMap) b = -1;
                say("The usual buttons are back", "icons/icon_gamepad");
                break;
            case M_FAST:
                fastLock = !fastLock;
                break;
            case M_SAVESTATE:
                say(WriteSnapshot(*nds, quickState, frame) ? "State saved" : "The state could not be saved", "icons/icon_save");
                menu.open = false;
                swallowMenuButtons = true;
                break;
            case M_LOADSTATE:
                say(ReadSnapshot(*nds, quickState, nullptr) ? "State loaded" : "No state to load", "icons/icon_layers");
                SetClock(*nds, false);
                menu.open = false;
                swallowMenuButtons = true;
                break;
            case M_RESET:
                played = false; // (no resume point at the title screen)
                nds->Reset();
                nds->SetupDirectBoot("game.nds");
                nds->Start();
                rc_reset();
                say("Game reset", "icons/icon_refresh");
                menu.open = false;
                swallowMenuButtons = true;
                break;
            case M_QUIT:
                running = false;
                break;
            default:
                break;
            }
            if (hit >= 0) buildMenu();
        }

        bool anyHeld = false;
        for (int i = 0; i < PAD_COUNT; i++) anyHeld |= ps.btn[i];
        if (swallowMenuButtons && !anyHeld) swallowMenuButtons = false;

        // With the pause menu, a screen of the HUD or the start screen open the game keeps
        // running, with no buttons or touches for it: its music plays on (the intro's and
        // the title screen's behind the start screen) and changes to the view show at once.
        const bool uiOpen = menu.open || screen != SCR_NONE || startOpen;
        int speed = 1;
        if (!uiOpen && !swallowMenuButtons)
        {
            keys |= PadKeys(ps, cfg.byLabel, cfg.padMap);
            if (cfg.padTouch)
            {
                if (pressed(PAD_RS)) focus.manualFlip = !focus.manualFlip;
                if (pressed(PAD_LS)) layout = LayoutNext(layout);
                if (fabsf(ps.rx) > 0.2f) curDX += ps.rx;
                if (fabsf(ps.ry) > 0.2f) curDY += ps.ry;
                if (ps.rt > 0.3f) curPress = true;
            }
        }
        // The emote wheel: online on the overworld, hold R and press the D-pad, which then
        // goes to the wheel instead of the game.
        {
            static uint32_t wheelPrev = 0;
            const uint32_t dpad = keys & ((1u << KEY_UP) | (1u << KEY_LEFT) | (1u << KEY_RIGHT) | (1u << KEY_DOWN));
            emoteWheel = !uiOpen && Party_Link() == PartyLink::Online && fieldActive() && (keys & (1u << KEY_R));
            if (emoteWheel)
            {
                const uint32_t down = dpad & ~wheelPrev;
                if (down & (1u << KEY_UP)) Party_SendEmote(0);
                else if (down & (1u << KEY_LEFT)) Party_SendEmote(1);
                else if (down & (1u << KEY_RIGHT)) Party_SendEmote(2);
                else if (down & (1u << KEY_DOWN)) Party_SendEmote(3);
                keys &= ~dpad;
            }
            wheelPrev = emoteWheel ? dpad : 0;
        }
        // The Union Room hand-off (an accepted battle or trade invite): first a save, as at a
        // Pokemon Center's counter (Autosave::GameSave, below), then the counter's own warp
        // (Party_GoToUnionRoom) once nothing runs on the overworld. There the one who invited
        // walks to the other and picks Battle or Trade from the game's menu, and the other's
        // game says yes to that question only. The player's B there stops it on both sides
        // (as a "no" to the other); the other leaving, their "no" or a long wait calls it off.
        {
            int hmap = -1, hx = 0, hz = 0;
            if (!playerPos(hmap, hx, hz)) hmap = -1;
            const uint32_t player = keys;
            if (hand.phase != H_OFF)
            {
                auto go = [&](int phase) {
                    fprintf(stderr, "[handoff] frame %u: phase %d -> %d (map %d)\n", frame, hand.phase, phase, hmap);
                    hand.phase = phase;
                    hand.since = frame;
                };
                auto callOff = [&](const std::string& why, bool tell) {
                    fprintf(stderr, "[handoff] frame %u: called off in phase %d%s%s\n", frame, hand.phase, why.empty() ? "" : ": ", why.c_str());
                    if (tell) Party_SendInvite(hand.peer, PI_NO | hand.kind);
                    if (!why.empty()) say(why, "icons/icon_wifi", 240);
                    hand.phase = H_OFF;
                };
                // A press, held three frames (the overworld reads the buttons every other one),
                // at most one every 14 frames.
                auto tap = [&](int key) {
                    if (frame - handTapAt < 14) return false;
                    handTapAt = frame;
                    handKey = 1u << key;
                    return true;
                };
                // The overworld's script (its task's environment, include/script.h): the native
                // function its first context waits in, and VAR_SPECIAL_RESULT.
                const uint32_t fs = readRam(0x021D4158), task = fs ? readRam(fs + 0x10) : 0, env = task ? readRam(task + 0x0C) : 0;
                const bool script = env && readRam(env) == 222271; // Unk80_10_C_MAGIC
                const uint32_t ctx = script ? readRam(env + 0x38) : 0, waits = ctx ? readRam(ctx + 4) & ~1u : 0;
                const uint32_t result = script ? readRam(env + 0xA4) & 0xFFFF : 0;
                const bool back = player & (1u << KEY_B);
                const PartyPeer& who = Party_Peers()[hand.peer];
                if (getenv("SSR_HANDOFF_DEBUG") && frame % 30 == 0)
                    fprintf(stderr, "[handoff] frame %u phase %d map %d at %d,%d task %08X script %d waits %08X result %u; them map %d\n",
                        frame, hand.phase, hmap, hx, hz, task, script, waits, result, who.map);
                if (Party_Link() != PartyLink::Online || !who.present)
                    callOff(hmap == 2 ? "" : "Not going to the Union Room: " + hand.name + " left", false);
                else switch (hand.phase)
                {
                case H_SAVE:
                    // As at the counter, a save first: a link that breaks restarts the game from it.
                    if (hmap == 2) go(H_ROOM);
                    // None in the Wi-Fi Plaza: the game has no menu there (a "Mystery Zone");
                    // a broken link restarts from the receptionist's save on the way in.
                    else if (hmap == PlazaMap::MapId) go(H_WARP);
                    else if (frame - hand.start > 60 * 60) callOff("Not going to the Union Room: the game could not be saved", true);
                    else if (gameSave.Busy()) hand.saveSeen = true;
                    else if (hand.saveSeen && gameSave.wrote) go(H_WARP);
                    else if (!hand.saveAsked || hand.saveSeen)
                    {
                        gameSave.Want(frame, true); // (again after a press cut it short)
                        hand.saveAsked = true;
                        hand.saveSeen = false;
                    }
                    break;
                case H_WARP:
                    if (hmap == 2) go(H_ROOM);
                    else if (frame - hand.start > 60 * 90) callOff("Not going to the Union Room: the game is busy", true);
                    else if (fieldIdle() && !uiOpen && !player && !gameSave.Busy() && !task && Party_GoToUnionRoom()) go(H_WARPING);
                    break;
                case H_WARPING:
                    keys = 0;
                    if (hmap == 2)
                    {
                        unionWarped = true;
                        go(H_ROOM);
                    }
                    else if (frame - hand.since > 30 && fieldIdle() && !task)
                        go(H_WARP); // it did not run (the overworld started something first): again
                    break;
                case H_ROOM:
                    // Both there.
                    if (hmap != 2) callOff("", false);
                    else if (who.map == 2)
                        go(hand.asker ? H_WALK : H_ANSWER);
                    else if (frame - hand.since > 60 * 120) callOff(hand.name + " did not come to the Union Room", true);
                    break;
                case H_WALK:
                {
                    // To the other's trainer, facing them, then A; once the game has stopped the
                    // player for the conversation, on. The Union Room shows each other player as
                    // the map object of their slot (id 1-10, sub_0205AF78); which slot is whose is
                    // not known here, so this goes ahead only when there is one other player there.
                    const uint32_t mgr = readRam(fs + 0x3C), me = readRam(readRam(fs + 0x40) + 0x30);
                    if (hmap != 2 || back || frame - hand.since > 60 * 40) { callOff("", back); break; }
                    if (script || (readRam(me) & 0x40)) { go(H_MENU); break; }
                    const uint32_t count = readRam(mgr + 4), objs = readRam(mgr + 0x124);
                    int seen = 0, tx = 0, tz = 0;
                    for (uint32_t i = 0; objs && count < 256 && i < count; i++)
                    {
                        const uint32_t o = objs + i * 0x12C, id = readRam(o + 8);
                        if (o == me || id < 1 || id > 10 || (readRam(o) & 0x201) != 1) continue; // active and not hidden
                        tx = (int)readRam(o + 0x64);
                        tz = (int)readRam(o + 0x6C);
                        seen++;
                    }
                    // (On arrival every slot shows for a moment.)
                    if (seen > 1 && frame - hand.since > 180) { callOff("Talk to " + hand.name + " in the Union Room", false); break; }
                    if (seen != 1) break;
                    static const int dirKeys[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
                    if (std::abs(tx - hx) + std::abs(tz - hz) == 1)
                    {
                        const int dir = tz < hz ? 0 : tz > hz ? 1 : tx < hx ? 2 : 3;
                        // currentFacing: 0 up, 1 down, 2 left, 3 right, as the directions here.
                        tap((int)readRam(me + 0x28) != dir ? dirKeys[dir] : KEY_A);
                    }
                    else if (const int dir = navStep(hx, hz, tx, tz); dir >= 0)
                        keys |= 1u << dirKeys[dir];
                    break;
                }
                case H_MENU:
                {
                    // "Is there something that you wanted to do?" (scr_seq_UNION_003: ScrCmd_064's
                    // menu, waited for by ScrCmd_585's native function): Greet, Draw, Battle, Trade...
                    // The cursor (the menu's ListMenu2D, +0xB8, its selectedIndex) to Battle or
                    // Trade, then A.
                    if (hmap != 2 || back || frame - hand.since > 60 * 30) { callOff("", back); break; }
                    if (waits != 0x02041900) break;
                    const uint32_t menu = readRam(env + 0x10), list = menu ? readRam(menu + 0xB8) : 0;
                    if (!list) break;
                    const int cur = (int)(readRam(list + 0x15) & 0xFF), want = hand.kind == PI_TRADE ? 3 : 2;
                    if (cur != want) tap(cur < want ? KEY_DOWN : KEY_UP);
                    else if (tap(KEY_A))
                    {
                        fprintf(stderr, "[handoff] frame %u: asked for a %s\n", frame, want == 3 ? "trade" : "battle");
                        hand.phase = H_OFF;
                    }
                    break;
                }
                case H_ANSWER:
                    // The other's question (scr_seq_UNION_004: what they ask for in VAR_SPECIAL_RESULT,
                    // 2 a battle and 3 a trade, its message, waited on in ov01_021EF348 for A, then
                    // GetMenuChoice's yes/no): yes, to this one only.
                    if (hmap != 2 || back || frame - hand.since > 60 * 120) { callOff("", back); break; }
                    if (result == (hand.kind == PI_TRADE ? 3u : 2u) && (waits == 0x021EF348 || waits == 0x020477C0))
                        hand.answered |= tap(KEY_A) && waits == 0x020477C0;
                    else if (hand.answered)
                    {
                        fprintf(stderr, "[handoff] frame %u: said yes\n", frame);
                        hand.phase = H_OFF;
                    }
                    break;
                }
            }
            if (frame - handTapAt < 3) keys |= handKey;
            // Back out of a Union Room visit of the hand-off's.
            if (unionWarped && hmap >= 0 && hmap != 2 && fieldIdle())
            {
                Party_LeftUnionRoom();
                unionWarped = false;
                fprintf(stderr, "[handoff] frame %u: back from the Union Room (map %d)\n", frame, hmap);
            }
            // Test aid: SSR_INVITE=<frame>,<player 0-3>,<1 battle, 2 trade> sends an invite then.
            static const char* inviteEnv = getenv("SSR_INVITE");
            unsigned invF, invP, invK;
            if (inviteEnv && sscanf(inviteEnv, "%u,%u,%u", &invF, &invP, &invK) == 3 && frame == invF) Party_SendInvite((int)invP, (int)invK);
        }
        // Autosave (autosave.h): the game's own save at calm moments, and resume points.
        {
            int amap = -1, ax = 0, az = 0;
            if (!playerPos(amap, ax, az)) amap = -1;
            const bool writingNow = saveWriting();
            static bool wasWriting = false;
            if (wasWriting && !writingNow)
            {
                // Any save, the player's own too: the next autosave waits, a resume point follows.
                gameSave.Saved(frame);
                resumeDue = true;
            }
            wasWriting = writingNow;
            if (bootMode == BOOT_NONE && !startOpen && fieldActive()) played = true;
            const bool on = autosaveOk && cfg.autosave && bootMode == BOOT_NONE && !startOpen;
            // Test aid: SSR_AUTOSAVE_AT=<frame> asks for a save then.
            static const char* saveAt = getenv("SSR_AUTOSAVE_AT");
            if (saveAt && frame == (uint32_t)atoi(saveAt)) gameSave.Want(frame, true);
            if ((on && cfg.autosaveGame) || gameSave.Busy() || saveAt || hand.phase == H_SAVE)
            {
                Autosave::GameSave::In in;
                in.frame = frame;
                in.idle = fieldIdle() && !uiOpen;
                in.startMenu = probeActive(probeStartMenu);
                in.saveApp = findSaveApp() != 0;
                in.writing = writingNow;
                in.player = keys;
                in.map = amap;
                in.x = ax;
                in.z = az;
                in.badges = Party_MyBadges();
                in.read = readRam;
                gameSave.Step(in, keys, KEY_A, KEY_B, KEY_X, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT);
                // The writing itself fast: it took most of the 17 seconds (songs keep their pace).
                if (gameSave.Busy() && writingNow) speed = 8;
            }
            if (on && !opt.noSave && played && !gameSave.Busy() && !uiOpen && fieldIdle() && (resumeDue || frame - resumeAt >= resumeEvery))
            {
                // The save file first, so that the two agree if the game is not quit normally.
                FlushSave(opt.save);
                if (resume.Write(*nds, amap, Party_PlaceName(amap))) resumeShownUntil = frame + 90;
                resumeAt = frame;
                resumeDue = false;
            }
        }
        // The save screen redrawn (HudScreens::SaveScreen): -1 not shown, else its phase. The
        // answer is chosen on the card and then given to the game's yes/no prompt as presses
        // read off the prompt: A to leave touch mode, up to Yes, then A; or B for No.
        int savePhase = -1;
        static int saveSel = 0, saveTarget = -1, saveGap = 0, saveLastPhase = -1, saveHold = 0;
        static uint32_t saveHoldKey = 0;
        static uint32_t saveKeysBefore = 0;
        // (An autosave answers it the same way: Yes, and Yes to writing over the save there is.)
        if (((cfg.redrawn && spritesOk && !uiOpen) || gameSave.Busy()) && bootMode == BOOT_NONE)
            if (const uint32_t app = findSaveApp())
            {
                const int st = (int)readRam(app + 0x0C), next = st == 2 ? (int)readRam(app + 0x10) : st;
                // (8 and 9: another adventure's save, a failed save: the game's own messages.)
                savePhase = next <= 4 ? 0 : next <= 7 ? 1 : next <= 9 ? -1 : next <= 12 ? 2 : 3;
                if (savePhase != saveLastPhase)
                {
                    // The game then asks whether to write over the save file there is (every
                    // time there is one): already answered by choosing Save.
                    saveTarget = savePhase == 1 && saveLastPhase == 0 && saveTarget == 0 ? 0 : -1;
                    saveSel = 0;
                }
                saveLastPhase = savePhase;
                if (gameSave.Busy() && (savePhase == 0 || savePhase == 1)) saveTarget = 0;
                const uint32_t newKeys = keys & ~saveKeysBefore;
                saveKeysBefore = keys;
                if (savePhase >= 0)
                {
                    if ((savePhase == 0 || savePhase == 1) && saveTarget < 0)
                    {
                        // (Only the cursor's sound: the answer is given to the game's own
                        // yes/no prompt, which makes its own sound for it.)
                        if (newKeys & ((1u << KEY_UP) | (1u << KEY_DOWN) | (1u << KEY_LEFT) | (1u << KEY_RIGHT))) saveSel ^= 1, uiSound(GameSfx::Cursor);
                        if (newKeys & (1u << KEY_A)) saveTarget = saveSel;
                        else if (newKeys & (1u << KEY_B)) saveTarget = 1;
                    }
                    keys = 0;
                    touching = false;
                    const uint32_t prompt = readRam(app + 0x48);
                    if (saveHold > 0)
                    {
                        // Held a few frames, as a tap: the game reads the buttons every other frame.
                        keys |= saveHoldKey;
                        saveHold--;
                    }
                    else if (st == 2 && (savePhase >= 1 || saveTarget == 0) && --saveGap <= 0)
                    {
                        // Its messages printed at once once Save is chosen, as A does by hand.
                        saveHoldKey = 1u << KEY_A;
                        keys |= saveHoldKey;
                        saveHold = 2;
                        saveGap = 4;
                    }
                    else if (saveTarget >= 0 && (st == 4 || st == 7) && (prompt >> 24) == 0x02 && --saveGap <= 0)
                    {
                        const uint32_t w = readRam(prompt + 0x74); // touch mode (bit 0), cursor, result (3: waiting)
                        if (((w >> 16) & 0xF) == 3)
                        {
                            saveHoldKey = (w & 1) ? 1u << KEY_A : saveTarget == 1 ? 1u << KEY_B
                                : ((w >> 8) & 0xFF) != 0 ? 1u << KEY_UP : 1u << KEY_A;
                            keys |= saveHoldKey;
                            saveHold = 3;
                            saveGap = 8; // then let go: the game acts on new presses
                        }
                    }
                }
            }
        if (savePhase < 0) saveLastPhase = -1;
        if (!uiOpen && (fastLock || kbFast || ps.lt > 0.5f)) speed = 4;
        if (bootMode != BOOT_NONE)
        {
            // Past the intro (see bootMode): the player's own presses wait meanwhile.
            keys = 0;
            speed = 8;
            bootFrames++;
            const bool mainMenu = appRunning("MainMenu");
            const bool pulse = bootFrames % 12 < 3;
            if (mainMenu)
            {
                // Continue is the first choice, New game the next one when there is a save.
                bootSawMenu = true;
                const uint32_t m = ++bootMenuFrames;
                const bool down = bootMode == BOOT_NEW && haveSave;
                if (down && m >= 30 && m < 34) keys |= 1u << KEY_DOWN;
                else if (m >= (down ? 50u : 30u) && pulse) keys |= 1u << KEY_A;
            }
            else if (!bootSawMenu && pulse)
                keys |= 1u << KEY_A; // the intro and the title screen
            // After the menu: no more presses, only waiting for the world (or the new game).
            if ((bootSawMenu && !mainMenu && (bootMode == BOOT_NEW || fieldActive())) || bootFrames > 60 * 120)
            {
                // The loading picture stays a little longer: while the world fades in, the game's
                // own menu screen showed for a moment, as if it had been opened.
                if (bootMode == BOOT_CONTINUE) resumeCoverUntil = frame + 45;
                bootMode = BOOT_NONE;
                speed = 1;
            }
        }
        prevPad = ps;

        SDL_Rect rects[2];
        LayoutRects(layout, outW, outH, rects);
        const bool merged = layout.mode == LayoutMode::Merged;
        // The single-screen view is drawn in mergedW columns from mergedX: the whole output, or
        // the chosen shape centred with bars beside it. Headless pictures keep their own size.
        int mergedW = outW, mergedX = 0;
        if (!opt.headless && cfg.aspect && outW > outH * AspectRatios[cfg.aspect].ratio)
        {
            mergedW = (int)(outH * AspectRatios[cfg.aspect].ratio + 0.5f);
            mergedX = (outW - mergedW) / 2;
        }
        MergedView view = MergedMakeView(mergedW, outH);
        view.originX += mergedX;
        // Widescreen 3D: on while the overworld is running in the merged view on a wide
        // output. It is drawn at the size of the picture, up to 1440 lines.
        static int wideFrames = 0;
        {
            int ww = opt.headless && opt.composeW ? opt.composeW : mergedW, wh = opt.headless && opt.composeH ? opt.composeH : outH;
            // Drawing it costs time in proportion to its size; cfg.wideLines caps its height
            // and the result is enlarged to the picture.
            if (wh > cfg.wideLines)
            {
                ww = ww * cfg.wideLines / wh;
                wh = cfg.wideLines;
            }
            onPlaza = worldBlocks.OnPlaza(*nds);
            // (On the Plaza in the layouts with both screens too, as the DS's own view: the
            // top screen is then made from it, see plazaTop.)
            const bool wantWide = ((merged && cfg.wide && view.width >= 300) || onPlaza) && !opt.noWide && !opt.lockstep &&
                (fieldActive() || menuReturn > 0 || scene.edgeFill);
            if (!merged)
            {
                ww = 256;
                wh = 192;
            }
            melonDS::Wide.Owner = wantWide ? &nds->GPU.GPU3D : nullptr;
            melonDS::Wide.Between = cfg.fpsUnlimited && !opt.headless && merged;
            melonDS::Wide.ViewWidth = merged ? view.width : 256;
            melonDS::Wide.Zoom = merged && (fieldActive() || menuReturn > 0) && !scene.edgeFill ? cfg.zoom / 100.0f : 1;
            melonDS::Wide.Width = ww;
            melonDS::Wide.Height = wh;
            wideFrames = wantWide ? wideFrames + 1 : 0;
        }
        // Filled in after the frame has run: the renderer hands over its picture then.
        WideImage wideImage;
        // Set while a picture between the game's frames is composed (Frame rate: Unlimited):
        // the blur and the night effects of the frame's own picture are used again then, as
        // building them takes most of the time there is (they are soft enough not to show it).
        bool composingBetween = false;
        auto takeWideImage = [&]() {
            wideImage = WideImage();
            battleUiWide = false;
            // A battle on a wide view: the screen widened (battlewide.h) instead of the bars.
            if (scene.edgeFill && cfg.wide && !opt.noWide)
            {
                WideImage world;
                if (wideFrames > 3 && !melonDS::Wide.Front.empty())
                {
                    world.pixels = melonDS::Wide.Front.data();
                    world.w = melonDS::Wide.FrontW;
                    world.h = melonDS::Wide.FrontH;
                }
                const float vw = view.width;
                const int tu = (nds->PowerControl9 & (1 << 15)) ? 0 : 1;
                const ScreenImage topImage = { nds->GPU.Framebuffer[nds->GPU.FrontBuffer][0].get(),
                    static_cast<GPU2D::SoftRenderer&>(nds->GPU.GetRenderer2D()).LayerFlags[tu] };
                // With the new screens the HP boxes are drawn anew (hudbattleui.h).
                battleWide.HideBoxes = cfg.redrawn && spritesOk && merged && HudBattleUi::Battle({ nds->MainRAM, nds->MainRAMMask }) != 0;
                battleUiWide = false;
                if (vw >= 300 && battleWide.Build(*nds, topImage, (int)std::ceil((vw - 256) / 2), &world, vw, wideImage))
                {
                    battleUiWide = battleWide.HideBoxes;
                    scene.wide = true;
                    return;
                }
            }
            else
                battleWide.Reset();
            if (wideFrames > 3 && !melonDS::Wide.Front.empty())
            {
                wideImage.pixels = melonDS::Wide.Front.data();
                wideImage.w = melonDS::Wide.FrontW;
                wideImage.h = melonDS::Wide.FrontH;
                if (cfg.voidFill)
                {
                    // The DS's own view: 256 of the view's base pixels across, all 192 down,
                    // both scaled by the zoom.
                    const float zoom = melonDS::Wide.Zoom;
                    wideImage.pixels = voidFill.Apply(wideImage.pixels, wideImage.w, wideImage.h,
                        std::min(1.0f, 256 * zoom / std::max(256.0f, melonDS::Wide.ViewWidth)), std::min(1.0f, zoom),
                        melonDS::Wide.FrontSerial);
                }
                if (cfg.blur)
                {
                    if (!composingBetween) focusBlur.Build(wideImage.pixels, wideImage.w, wideImage.h, cfg.blur);
                    wideImage.blur = &focusBlur;
                }
                if (cfg.nightFx && worldBlocks.Outdoors(*nds))
                {
                    // Evening and night by the game's clock (outdoors: interiors look the same at any hour).
                    int year, month, day, hour, minute, second;
                    nds->RTC.GetDateTime(year, month, day, hour, minute, second);
                    const float strength = NightStrength(hour, minute);
                    if (strength > 0)
                    {
                        if (!composingBetween)
                        {
                            fireflies.clear();
                            worldBlocks.Fireflies(*nds, frame / 60.0f, melonDS::Wide.ViewWidth, melonDS::Wide.Zoom, fireflies);
                            lamps.clear();
                            worldBlocks.Lights(*nds, melonDS::Wide.ViewWidth, melonDS::Wide.Zoom, lamps);
                            nightFx.Build(wideImage.pixels, wideImage.w, wideImage.h, strength, fireflies, lamps);
                        }
                        wideImage.fx = &nightFx;
                    }
                }
                // Fades: the game darkens or whitens its screen with the master brightness
                // register, or with a brightness effect on the 3D layer.
                int topUnit = (nds->PowerControl9 & (1 << 15)) ? 0 : 1;
                const auto& unit = topUnit == 0 ? nds->GPU.GPU2D_A : nds->GPU.GPU2D_B;
                int mode = (unit.MasterBrightness >> 14) & 3, f = std::min(16, unit.MasterBrightness & 0x1F);
                if ((mode != 1 && mode != 2) || !f)
                {
                    int effect = (unit.BlendCnt >> 6) & 3;
                    if ((unit.BlendCnt & 1) && effect >= 2)
                    {
                        mode = effect == 2 ? 1 : 2;
                        f = std::min<int>(16, unit.EVY);
                    }
                    else
                        mode = 0;
                }
                wideImage.fadeMode = mode;
                wideImage.fade = f;
            }
            if (opt.logSections)
            {
                // Where the widescreen picture comes and goes, and why.
                static int last = -1;
                int now = (wideImage.pixels ? 1 : 0) | (scene.wide ? 2 : 0) | (melonDS::Wide.Owner ? 4 : 0);
                if (now != last)
                    fprintf(stderr, "[wide] frame %u: picture %d, scene %s wide %d, renderer on %d, fade %d/%d\n", frame,
                        now & 1, scene.name, (now >> 1) & 1, (now >> 2) & 1, wideImage.fadeMode, wideImage.fade);
                last = now;
            }
        };
        if (!uiOpen)
        {
            if (!opt.headless && mouseDown)
            {
                if (merged)
                {
                    float bx = (mouseX - view.originX) / view.unit, by = (mouseY - view.originY) / view.unit;
                    if (MergedTouch(scene, bx, by, touchX, touchY)) touching = true;
                }
                else if (LayoutTouch(rects[1], mouseX, mouseY, touchX, touchY)) touching = true;
            }
            if (curDX != 0 || curDY != 0 || curPress)
            {
                // Faster with a fuller stick deflection, for both precision and reach.
                float mag = sqrtf(curDX * curDX + curDY * curDY);
                float step = 1.0f + 3.5f * mag;
                cursorX += curDX * step;
                cursorY += curDY * step;
                // In the merged view the cursor can also reach pieces drawn beside the 4:3 area.
                float margin = merged ? (view.width - 256) / 2 : 0;
                if (cursorX < -margin) cursorX = -margin;
                if (cursorX > 255 + margin) cursorX = 255 + margin;
                if (cursorY < 0) cursorY = 0;
                if (cursorY > 191) cursorY = 191;
                cursorShownUntil = frame + 180;
            }
            if (curPress && !touching)
            {
                if (!merged)
                {
                    touching = true;
                    touchX = (int)cursorX;
                    touchY = (int)cursorY;
                }
                else if (MergedTouch(scene, cursorX, cursorY, touchX, touchY)) touching = true;
            }
        }

        // Battle strips: left and right walk along the buttons in the order they are shown,
        // and A presses the chosen one by touching it on the touch screen. The game's own
        // cursor moves in the touch screen's arrangement (FIGHT above the other three),
        // which does not match a row, so the arrows and A do not reach it here.
        {
            std::string& stripKind = battleStripKind;
            static uint32_t prevStripKeys = 0;
            static int stripTouch = 0, stripTouchX = 0, stripTouchY = 0;
            const bool cmd = merged && !strcmp(scene.name, "battle-cmd"), moves = merged && !strcmp(scene.name, "battle-moves");
            // The redrawn target and two-answer menus (only shown with the redrawn menus).
            const bool pick = merged && !strcmp(scene.name, "battle-pick");
            // With the redrawn menus (hudbattle.h) the discs are in another order and the moves
            // and targets in a grid; what the game shows is read from its memory.
            HudBattle::State bs;
            const bool redrawnBattle = cfg.redrawn && spritesOk && (cmd || moves || pick) && HudBattle::Read(nds->MainRAM, nds->MainRAMMask, bs) &&
                (pick ? bs.kind == HudBattle::Target || bs.kind == HudBattle::Choice : !bs.idle && bs.moves == moves);
            const bool target = pick && redrawnBattle && bs.kind == HudBattle::Target, choice = pick && redrawnBattle && bs.kind == HudBattle::Choice;
            if (!uiOpen && (cmd || moves || target || choice))
            {
                if (stripKind != scene.name)
                {
                    stripKind = scene.name;
                    battleSel = 0;
                    // A target: the first that can be aimed at, foes first.
                    if (target)
                        for (int i : { 3, 1, 0, 2 })
                            if (bs.hit[i]) { battleSel = i; break; }
                    prevStripKeys = keys; // a key still held from before does not count
                }
                const uint32_t edge = keys & ~prevStripKeys;
                prevStripKeys = keys;
                static const int cmdX[4] = { 128, 39, 216, 128 }, cmdY[4] = { 84, 168, 168, 172 };
                static const int movX[4] = { 64, 192, 64, 192 }, movY[4] = { 52, 52, 116, 116 };
                // The target menu's buttons (as numbered in HudBattle::TargetGrid) and the two answers.
                static const int tgtX[4] = { 60, 196, 196, 60 }, tgtY[4] = { 116, 44, 116, 44 };
                static const int ansX[2] = { 128, 128 }, ansY[2] = { 68, 140 };
                // Empty move slots are skipped: theirs is a plain grey where a move's name is white.
                const u32* screenImagesPrev = nds->GPU.Framebuffer[nds->GPU.FrontBuffer][1].get();
                auto usable = [&](int i) {
                    if (cmd || choice) return true;
                    if (target) return bs.hit[i];
                    if (redrawnBattle) return bs.move[i] != 0;
                    uint32_t c = screenImagesPrev[(movY[i] - 14) * 256 + movX[i]];
                    return ((c >> 16) & 0xFF) > 200 && ((c >> 8) & 0xFF) > 200 && (c & 0xFF) > 200;
                };
                if (redrawnBattle && !cmd)
                {
                    // The grids of two by two (moves, targets) and the two answers, one above
                    // the other: each arrow goes to the neighbour on its side, if there is one.
                    // (A move's buttons are numbered row by row, so the neighbours differ in
                    // bit 0 across and bit 1 up and down; a target's go by TargetGrid.)
                    int row = 0, col = 0;
                    if (target)
                    {
                        for (int r = 0; r < 2; r++)
                            for (int c = 0; c < 2; c++)
                                if (HudBattle::TargetGrid[r][c] == battleSel) row = r, col = c;
                    }
                    else if (choice)
                        row = battleSel & 1;
                    else
                        row = battleSel >> 1, col = battleSel & 1;
                    int r2 = row, c2 = col;
                    if (edge & (1u << KEY_UP | 1u << KEY_DOWN)) r2 = (edge & 1u << KEY_UP) ? 0 : 1;
                    if (!choice && (edge & (1u << KEY_LEFT | 1u << KEY_RIGHT))) c2 = (edge & 1u << KEY_LEFT) ? 0 : 1;
                    const int to = target ? HudBattle::TargetGrid[r2][c2] : choice ? r2 : r2 * 2 + c2;
                    // (A move hitting all its targets at once has nothing to choose.)
                    if (to != battleSel && usable(to) && !(target && bs.multi)) battleSel = to;
                }
                else
                {
                    // The buttons in the order they are shown, as indices of the touch screen's.
                    static const int sameOrder[4] = { 0, 1, 2, 3 };
                    const int* order = redrawnBattle && cmd ? HudBattle::CmdOrder : sameOrder;
                    const int pos = int(std::find(order, order + 4, battleSel) - order);
                    const uint32_t next = 1u << KEY_RIGHT, prev = 1u << KEY_LEFT;
                    for (int dir : { (edge & next) ? 1 : 0, (edge & prev) ? -1 : 0 })
                        if (dir)
                            for (int p = pos + dir; p >= 0 && p < 4; p += dir)
                                if (usable(order[p])) { battleSel = order[p]; break; }
                }
                if (opt.logSections && edge) fprintf(stderr, "[strip] frame %u: keys %03x, chosen %d\n", frame, edge, battleSel);
                if (edge & (1 << KEY_A))
                {
                    stripTouch = 4;
                    const int* xs = cmd ? cmdX : target ? tgtX : choice ? ansX : movX;
                    const int* ys = cmd ? cmdY : target ? tgtY : choice ? ansY : movY;
                    stripTouchX = xs[battleSel];
                    stripTouchY = ys[battleSel];
                }
                // B in the move list and the target menu touches CANCEL. Given to the game as
                // a key, the first press only brought up its own cursor. On the command row it
                // touches CANCEL when the game shows one there (a double battle's second
                // Pokémon: back to the first); in a question the lower answer, as the game's B.
                if ((moves || target || (cmd && redrawnBattle && bs.cancel) || choice) && (edge & (1 << KEY_B)))
                {
                    stripTouch = 4;
                    stripTouchX = 128;
                    stripTouchY = choice ? ansY[1] : 172;
                }
                keys &= ~((1u << KEY_LEFT) | (1u << KEY_RIGHT) | (1u << KEY_UP) | (1u << KEY_DOWN) | (1u << KEY_A) | (1u << KEY_B));
            }
            else
                stripKind.clear();
            if (!uiOpen && stripTouch > 0)
            {
                touching = true;
                touchX = stripTouchX;
                touchY = stripTouchY;
                stripTouch--;
            }
        }

        // The Ruins of Alph's puzzles, played with the buttons (hudminigames.h): they become
        // the touches the game expects.
        static HudMinigames::AlphPad alphPad;
        static HudMinigames::Alph alphState;
        static bool alphRead = false;
        alphRead = false;
        if (!uiOpen && appRunning("AlphPuzzle"))
            for (unsigned i = 0; i < rc_num_apps; i++)
                if (!strcmp(rc_apps[i].name, "AlphPuzzle"))
                {
                    static uint32_t alphAt = 0;
                    const HudParty::Ram ram{ nds->MainRAM, nds->MainRAMMask + 1 };
                    const uint32_t man = HudParty::FindApp(ram, alphAt, rc_apps[i].initAddr, rc_apps[i].execAddr, rc_apps[i].exitAddr,
                        [](uint32_t) { return true; });
                    if (man && HudMinigames::ReadAlph(ram, man, alphState))
                    {
                        alphRead = true;
                        int tx, ty;
                        if (HudMinigames::AlphInput(alphState, alphPad, keys, tx, ty)) touching = true, touchX = tx, touchY = ty;
                    }
                    break;
                }
        if (!alphRead) alphPad = HudMinigames::AlphPad();

        if (uiOpen && bootMode == BOOT_NONE && !gameSave.Busy())
        {
            // The buttons are the menu's (scripted test input and the keyboard too); an autosave
            // goes on behind the menu.
            keys = 0;
            touching = false;
        }
        else if (gameSave.Busy())
            touching = false; // (its buttons are already in keys)
        // Behind the title and start screens the game boots on its own: its logos and opening
        // movie are passed with A, silently, so that its title music (or the song in its
        // place) starts at once. Never on the title screen itself, where A would go on.
        bool silentBoot = false;
        // Only on the way to the title (or back through the opening, which the title screen
        // plays again after a while): the title screen's own frames sometimes do not count as
        // it running, and passing those quickly made the speed flicker and the songs drag.
        static bool reachedTitle = false;
        if (appRunning("TitleScreen") || appRunning("MainMenu") || fieldActive()) reachedTitle = true;
        if (startOpen && bootMode == BOOT_NONE && (!reachedTitle || appRunning("IntroMovie")) && !appRunning("TitleScreen") &&
            !appRunning("MainMenu") && !fieldActive())
        {
            static uint32_t pulse = 0;
            if (++pulse % 12 < 3) keys |= 1u << KEY_A;
            silentBoot = true;
            speed = 8; // the logos before the opening movie take seconds and cannot be skipped
        }
        if (ref)
        {
            TraceLog.clear();
            TracePos = 0;
            TraceFrame = frame;
            ref->SetKeyMask(~keys & 0xFFF);
            if (touching) ref->TouchScreen((u16)touchX, (u16)touchY);
            else ref->ReleaseScreen();
            ref->RunFrame();
            TracePrevR15 = nds->ARM9.R[15];
            TracePrevCPSR = nds->ARM9.CPSR;
        }

        perfMark(0);
        int got = 0;
        {
            nds->SetKeyMask(~keys & 0xFFF);
            if (touching) nds->TouchScreen((u16)touchX, (u16)touchY);
            else nds->ReleaseScreen();

            for (int i = 0; i < speed; i++)
            {
                rc_frame++;
                nds->RunFrame();
                int avail = nds->SPU.GetOutputSize();
                if (avail > (int)audioBuf.size() / 2) avail = (int)audioBuf.size() / 2;
                int n = nds->SPU.ReadOutput(audioBuf.data(), avail);
                if (i == 0) got = n; // when fast-forwarding only the first frame's audio is played
            }
        }
        perfMark(1);
        updateShownView();
        {
            int pmap = -1, px = 0, pz = 0;
            if (!playerPos(pmap, px, pz)) pmap = -1;
            const int pstate = uiOpen ? PS_MENU : sectionActive("OVY_12") && !fieldActive() ? PS_BATTLE
                : pmap == 2 || pmap == 4 || pmap == 5 ? PS_LINK : PS_FIELD;
            Party_Process(pstate, pmap);
            // This player's battle, for the others' spectator cards and the turn timer.
            {
                PartyBattle b;
                if (pstate == PS_BATTLE && HudBattle::ReadLive(nds->MainRAM, nds->MainRAMMask, b))
                {
                    b.at = Party_Now();
                    b.choosingAt = b.choosing && (!myBattle.choosing || myBattle.turn != b.turn) ? b.at : myBattle.choosingAt;
                    Party_SetBattle(b);
                }
                myBattle = b;
            }
            // The others' trainers: on this map, or anywhere outdoors (the outdoors is one map
            // matrix, whose places all players share).
            {
                const PartyPeer* peers = Party_Peers();
                const int me = Party_Me();
                const PartyWalker mine = me >= 0 ? peers[me].walk : PartyWalker();
                remoteDraw.clear();
                for (int i = 0; i < 4; i++)
                {
                    RemoteTrainer& r = remotes[i];
                    const PartyWalker& w = peers[i].walk;
                    const bool here = Party_Link() != PartyLink::Off && i != me && peers[i].present && w.shown && mine.shown &&
                        w.matrix == mine.matrix && (mine.matrix == 0 || w.map == mine.map);
                    if (!here)
                    {
                        r.on = false;
                        continue;
                    }
                    const double dx = w.x - r.x, dy = w.y - r.y, dz = w.z - r.z;
                    const double gap = std::max(std::fabs(dx), std::fabs(dz));
                    if (!r.on || gap > 4 * 0x10000)
                    {
                        // Just seen, or jumped (a door, flying): no walk there.
                        r.x = w.x;
                        r.y = w.y;
                        r.z = w.z;
                        r.on = true;
                    }
                    else
                    {
                        // A quarter of the way each frame: messages come every 100 ms or so.
                        r.x += dx / 4;
                        r.y += dy / 4;
                        r.z += dz / 4;
                    }
                    // Walking: standing, a step, standing, the other step, 4 frames each.
                    r.step = gap > 0x800 ? r.step + 1 : 0;
                    remoteDraw.push_back({ w.sprite, w.facing, r.step ? (r.step / 4 + 1) % 4 : 0, (int32_t)r.x, (int32_t)r.y, (int32_t)r.z });
                }
            }
            if (frame % 60 == 0) romArt.SetRam(nds->MainRAM, nds->MainRAMMask + 1);
            // A new session: its numbers start again. Battles started in the Colosseum or the
            // Union Room and trades while online are this session's link battles and trades.
            if (Party_Link() == PartyLink::Online && shownLink != PartyLink::Online && shownLink != PartyLink::Lost)
                sessionStats = HudOnline::SessionStats();
            shownLink = Party_Link();
            {
                static bool wasBattle = false, wasTrade = false;
                const bool battle = appRunning("Battle"), trade = appRunning("TradeSequence");
                if (Party_Link() == PartyLink::Online)
                {
                    if (battle && !wasBattle && (pmap == 2 || pmap == 4 || pmap == 5)) sessionStats.linkBattles++;
                    if (trade && !wasTrade) sessionStats.trades++;
                }
                wasBattle = battle;
                wasTrade = trade;
                if (Party_Me() >= 0) sessionStats.caught[Party_Me()] = Party_CaughtThisSession();
            }
            partyMap = pmap;
            // Hosting by itself (Host when playing): once, when the player is first in the world
            // and not already in a party; the code is then shown so it can be shared.
            static bool autoHosted = false;
            if (!autoHosted && cfg.autoHost && !startOpen && bootMode == BOOT_NONE && fieldActive() && Party_Link() == PartyLink::Off &&
                !cfg.onlineServer.empty() && opt.party.empty() && (!opt.headless || getenv("SSR_AUTO_HOST")) && !opt.lockstep)
            {
                autoHosted = true;
                Party_Host(cfg.onlineServer, cfg.hostMax);
            }
            static std::string announcedCode;
            if (Party_Link() == PartyLink::Online && Party_Me() >= 0 && Party_Peers()[Party_Me()].host && !Party_Code().empty() &&
                Party_Code() != announcedCode)
            {
                announcedCode = Party_Code();
                // On this computer's own server, friends need its address on the network too.
                const std::string lan = Party_IsLocalServer(cfg.onlineServer) ? Party_LanAddress() : "";
                say("Your session is " + announcedCode + (lan.empty() ? ": share the code to play together"
                    : ": friends join with it on server " + lan), "icons/icon_wifi", 480);
            }
            if (trainerArt.px.empty() && frame % 60 == 0 && fieldActive())
                worldBlocks.SpriteImage(*nds, Party_MyCard().gender ? 97 : 0, 1, trainerArt); // the boy or the girl, facing south
            // Friends in the session: seen now (every few seconds while together), with their
            // name and colour as they are now.
            if (Party_Link() == PartyLink::Online && frame % 300 == 0)
                for (int i = 0; i < 4; i++)
                {
                    const PartyPeer& p = Party_Peers()[i];
                    if (p.present && i != Party_Me() && friends.count(p.uid)) addFriend(p);
                }
            PartyEvent pe;
            while (Party_PopEvent(pe))
            {
                const PartyPeer* peers = Party_Peers();
                Toast t;
                t.waits = true;
                if (pe.player >= 0 && pe.player < 4 && pe.kind != PartyEvent::Left)
                {
                    const PartyPeer& who = peers[pe.player];
                    t.name = who.name;
                    t.nameColour = HudOnline::TextColourOf(who.colour);
                    t.chip = HudOnline::Initials(who.name);
                    t.chipRing = HudOnline::RingOf(who.colour);
                }
                switch (pe.kind)
                {
                case PartyEvent::Joined:
                    t.text = " joined the session";
                    t.trail = "emotes/emote_hi";
                    break;
                case PartyEvent::Left:
                    t.name = pe.text;
                    t.nameColour = HudOnline::TextColourOf(pe.arg);
                    t.chip = HudOnline::Initials(pe.text);
                    t.chipRing = "people/chip_ring_grey";
                    t.text = " left the session";
                    break;
                case PartyEvent::Caught:
                    t.text = " caught " + std::string(strchr("AEIOU", Party_SpeciesName(pe.arg)[0]) ? "an " : "a ") + Party_SpeciesName(pe.arg);
                    if (const Image* ic = romArt.PokemonIcon(pe.arg)) t.trailArt = *ic;
                    else t.trail = "emotes/emote_sparkle";
                    sessionStats.caught[pe.player & 3]++;
                    sessionStats.lastCaught[pe.player & 3] = pe.arg;
                    break;
                case PartyEvent::Badge:
                    t.chip.clear();
                    t.lead = "people/crown_gold";
                    t.text = " earned a badge (" + std::to_string(pe.arg) + " now)";
                    break;
                case PartyEvent::Reconnected:
                    t = Toast();
                    t.text = "Back online";
                    t.lead = "icons/icon_wifi";
                    break;
                case PartyEvent::Failed:
                    t = Toast();
                    t.text = pe.text;
                    t.lead = "icons/icon_wifi";
                    t.frames = 300;
                    break;
                case PartyEvent::Friend:
                    if (pe.arg == PF_ASK)
                    {
                        // Kept for the Requests tab (once per player).
                        if (std::none_of(requests.begin(), requests.end(), [&](const FriendAsk& q) { return q.player == pe.player; }))
                            requests.push_back({ pe.player, peers[pe.player].uid });
                        t.text = " wants to be friends: see Friends";
                        t.trail = "emotes/emote_hi";
                        break;
                    }
                    if (pe.arg == PF_YES) addFriend(peers[pe.player]);
                    t.waits = false;
                    t.text = pe.arg == PF_YES ? " is now your friend" : " said no to being friends";
                    t.trail = pe.arg == PF_YES ? "emotes/emote_thanks" : "emotes/emote_sweat";
                    break;
                case PartyEvent::SessionInvite:
                {
                    // From a friend in another session: their card, unless this player is busy.
                    auto fr = friends.find(pe.from);
                    t = Toast();
                    t.name = fr != friends.end() ? fr->second.name : pe.name;
                    const int colour = fr != friends.end() ? fr->second.colour : 0;
                    t.nameColour = HudOnline::TextColourOf(colour);
                    t.chip = HudOnline::Initials(t.name);
                    t.chipRing = HudOnline::RingOf(colour);
                    if (screen != SCR_NONE || menu.open || startOpen || pstate != PS_FIELD)
                    {
                        t.text = " invited you to their session: see Friends";
                        t.frames = 300;
                        break;
                    }
                    inviteWho = PartyPeer();
                    inviteWho.name = t.name;
                    inviteWho.colour = colour;
                    inviteCode = pe.text;
                    inviteFrom = -1;
                    inviteKind = PI_SESSION;
                    inviteAt = Party_Now();
                    inviteMenu.items.clear();
                    MenuItem yes, no;
                    yes.id = M_ACCEPT;
                    yes.label = "Join";
                    no.id = M_DECLINE;
                    no.label = "Not now";
                    no.danger = true;
                    inviteMenu.items = { yes, no };
                    inviteMenu.selected = 0;
                    screen = SCR_INVITE;
                    continue;
                }
                case PartyEvent::Invite:
                    if (pe.arg == PI_BATTLE || pe.arg == PI_TRADE)
                    {
                        // Asked: the card takes the focus, unless this player is busy.
                        if (screen != SCR_NONE || menu.open || startOpen || pstate != PS_FIELD)
                            Party_SendInvite(pe.player, PI_BUSY | pe.arg);
                        else
                        {
                            inviteFrom = pe.player;
                            inviteKind = pe.arg;
                            inviteAt = Party_Now();
                            inviteMenu.items.clear();
                            MenuItem yes, no;
                            yes.id = M_ACCEPT;
                            yes.label = pe.arg == PI_TRADE ? "Trade" : "Battle";
                            no.id = M_DECLINE;
                            no.label = "Not now";
                            no.danger = true;
                            inviteMenu.items = { yes, no };
                            inviteMenu.selected = 0;
                            screen = SCR_INVITE;
                        }
                        continue;
                    }
                    t.waits = false;
                    t.frames = 300;
                    if (pe.arg & PI_YES)
                    {
                        t.text = " said yes: going to the Union Room";
                        t.trail = "emotes/emote_thanks";
                        startHand(pe.player, pe.arg & (PI_BATTLE | PI_TRADE), true);
                    }
                    else
                    {
                        if (hand.phase != H_OFF && pe.player == hand.peer && (pe.arg & hand.kind))
                        {
                            fprintf(stderr, "[handoff] frame %u: called off by the other player\n", frame);
                            hand.phase = H_OFF;
                        }
                        t.text = (pe.arg & PI_BUSY) ? " is busy right now" : " can't right now";
                        t.trail = "emotes/emote_sweat";
                    }
                    break;
                default:
                    continue;
                }
                if (pe.kind == PartyEvent::Joined || pe.kind == PartyEvent::Left || pe.kind == PartyEvent::Caught || pe.kind == PartyEvent::Badge)
                    if (!cfg.onlineToasts) continue;
                toasts.Push(t);
            }
        }
        frame++;
        if (!holdScript) scriptClock++;

        if (opt.traceFuncsAt.count(frame))
        {
            char name[64];
            snprintf(name, sizeof(name), "/funcs_%06u.txt", frame);
            std::ofstream out((opt.shotDir.empty() ? std::string(".") : opt.shotDir) + name);
            unsigned now = rc_current_frame();
            for (auto& tp : traceProbes)
                if (rc_probe_last_frame(tp.first) + 1 >= now) out << tp.second << "\n";
        }

        if (opt.logSections)
        {
            static std::string lastActive;
            std::string active;
            for (u32 i = 0; i < rc_num_sections; i++)
                if (rc_section_last_frame[i] == rc_frame && strcmp(rc_sections[i].name, "main") && strcmp(rc_sections[i].name, "ITCM") &&
                    strcmp(rc_sections[i].name, "bios9"))
                    active += std::string(" ") + rc_sections[i].name;
            if (active != lastActive)
            {
                fprintf(stderr, "[sections] frame %u:%s\n", frame, active.c_str());
                lastActive = active;
            }
        }

        if (ref)
        {
            if (!TraceFailed && TracePos != TraceLog.size())
            {
                fprintf(stderr, "LOCKSTEP MISMATCH frame %u: recomp ran %zu instructions, interpreter %zu\n",
                    frame - 1, TracePos, TraceLog.size());
                TraceFailed = true;
            }
            if (TraceFailed) break;
            if (frame % 300 == 0)
                fprintf(stderr, "[lockstep] %u frames identical (%zu instructions in the last one)\n", frame, TraceLog.size());
        }

        const u32* top = nds->GPU.Framebuffer[nds->GPU.FrontBuffer][0].get();
        const u32* bot = nds->GPU.Framebuffer[nds->GPU.FrontBuffer][1].get();
        // The two screens with their per-pixel layer flags, for the merged view.
        auto& renderer2d = static_cast<GPU2D::SoftRenderer&>(nds->GPU.GetRenderer2D());
        const int topUnit = (nds->PowerControl9 & (1 << 15)) ? 0 : 1;
        const ScreenImage screenImages[2] = {
            { top, renderer2d.LayerFlags[topUnit] },
            { bot, renderer2d.LayerFlags[1 - topUnit] },
        };
        const bool fieldNow = rc_probe_last_frame(probeFieldMap) + 1 >= rc_current_frame() && rc_probe_last_frame(probeFieldMap);
        for (u32 sec = 0; sec < rc_num_sections; sec++)
        {
            // (A section that misses a frame or two still counts as running; only a gap of
            // more than three frames starts the count again.)
            const unsigned last = rc_section_last_frame[sec];
            if (!last || last + 3 < rc_current_frame()) sectionStreak[sec] = 0;
            else if (last == rc_current_frame()) sectionStreak[sec]++;
        }
        bool appBottom = false;
        for (const RcApp* app : focus.bottomApps)
            if (appActive(app->name)) appBottom = true;
        appBottomNow = appBottom;
        if (top && bot) focus.Update(top, bot, fieldNow, appBottom);
        layout.swapped = focus.Swapped();
        // While the touch cursor is in use the bottom screen has to be big enough to aim at.
        if (!merged && focus.mode == 0 && frame < cursorShownUntil) layout.swapped = true;
        LayoutRects(layout, outW, outH, rects);
        {
            // A new layout has to be asked for a few frames in a row before it replaces the
            // one on screen, so that the frames of a transition in which the game is between
            // two states do not flash a third layout. The battle panel stays longer: the
            // game stops polling it while its buttons animate between menus.
            const bool writing = saveWriting();
            fieldHeldBySave = writing && fieldActive();
            Scene picked = pickScene(frame < cursorShownUntil, view.width, screenImages);
            // The Wi-Fi Plaza's room is only in the widescreen world: the game's own picture
            // shows its backdrop around the player there, which must not cover the room.
            if (picked.wide && onPlaza) picked.items[0].drop |= LAYER_BACKDROP;
            // An application's own layout stays while it fades out: the game is still ending it
            // (and its picture is still what is on the screens) when only a general layout
            // would be chosen, until the overworld runs again or a moment has passed.
            {
                static Scene lastApp;
                static uint32_t lastAppFrame = 0;
                static const char* const appScenes[] = { "bag", "summary", "phone", "pc-box", "map", "pokedex",
                    "pokedex-list", "pokedex-page", "mart", "naming" };
                const bool isApp = std::any_of(std::begin(appScenes), std::end(appScenes),
                    [&](const char* n) { return !strcmp(picked.name, n); });
                const bool generic = !strcmp(picked.name, "top") || !strcmp(picked.name, "bottom") || !strcmp(picked.name, "spread");
                if (isApp)
                {
                    lastApp = picked;
                    lastAppFrame = frame;
                }
                else if (generic && lastAppFrame && frame - lastAppFrame < 90 && !fieldNow && !appBottomNow)
                    // (Not when another application has started, e.g. the party after a summary.)
                    picked = lastApp;
                else if (!generic)
                    lastAppFrame = 0;
            }
            // With the X menu open the game covers the bottom of the overworld with a strip
            // that points at the other screen. It means nothing here, so the layers it is
            // drawn on are left out and the overworld shows through.
            {
                int topUnit = (nds->PowerControl9 & (1 << 15)) ? 0 : 1;
                // The strip of the X menu on the top screen: 2D across the full width of its
                // lower rows, with the 3D world above it.
                const uint8_t* tf = screenImages[0].flags;
                auto is2D = [&](int x, int y) { uint8_t c = LayerClass(tf[y * 256 + x]); return c != LAYER_3D && c != LAYER_BACKDROP; };
                // Only while the overworld's code is what is running: a battle's opening has
                // the same shape on screen.
                const bool strip = tf && sectionActive("field") && !sectionActive("OVY_12") &&
                    is2D(0, 152) && is2D(255, 152) && is2D(0, 189) && is2D(255, 189) && LayerClass(tf[170 * 256 + 128]) == LAYER_OBJ &&
                    LayerClass(tf[60 * 256 + 128]) == LAYER_3D && LayerClass(tf[100 * 256 + 20]) == LAYER_3D;
                if (probeActive(probeStartMenu) && fieldActive()) menuReturn = 0;
                else if (strip && merged) menuReturn = 30;
                else if (menuReturn > 0) menuReturn--;
                // (Also all through an autosave, whose menu task goes quiet while the save is written.)
                bool menuOpen = merged && ((probeActive(probeStartMenu) && fieldActive()) || menuReturn > 0 || gameSave.Covering(frame));
                rc_hide_layers[topUnit] = menuOpen ? 0x18 : 0;
                rc_hide_layers[1 - topUnit] = 0;
            }
            // The start menu, the layouts of the applications opened from it, and the plain
            // bottom screen (the party).
            auto menuScene = [](const char* n) {
                static const char* const names[] = { "start-menu", "bottom", "bag", "summary", "phone", "pc-box", "map",
                    "pokedex", "pokedex-list", "pokedex-page", "mart", "naming" };
                return std::any_of(std::begin(names), std::end(names), [&](const char* m) { return !strcmp(n, m); });
            };
            static std::string shownName, pendingName;
            static int pending = 0, heldForBlank = 0;
            // Whether every screen a layout shows is a single colour (the widescreen world
            // counts as something to show).
            auto sceneBlank = [&](const Scene& s) {
                if (s.wide) return false;
                bool any = false;
                for (const Element& it : s.items)
                {
                    if (it.screen < 0) continue;
                    if (!AutoFocus::Blank(it.screen == 0 ? top : bot)) return false;
                    any = true;
                }
                return any;
            };
            if (shownName == picked.name) heldForBlank = 0;
            if (writing && !shownName.empty())
                // The layout on screen stays until the save is written (see probeSaveWrite).
                pending = 0;
            else if (shownName.empty() || shownName == picked.name ||
                // Between menus, with both screens black nothing on screen changes, so the new
                // layout can take over at once and is in place when the picture fades in.
                // (Not around battles: there the timing of the layouts decides which buttons
                // are on screen.)
                (AutoFocus::Blank(top) && AutoFocus::Blank(bot) && !sectionActive("OVY_12") && !sectionActive("OVY_8") &&
                 (menuScene(picked.name) || menuScene(shownName.c_str()))))
            {
                scene = picked;
                shownName = picked.name;
                pending = 0;
            }
            else if (sceneBlank(picked) && !sceneBlank(scene) && heldForBlank < 90)
            {
                // The new layout would show only screens that are still one plain colour (an
                // application fading in on the bottom screen while the world is still on top,
                // the overworld fading in after a battle): the switch would blink the view to
                // black and back, so the old layout stays until there is something to show.
                heldForBlank++;
                pending = 0;
            }
            else
            {
                if (pendingName != picked.name) pending = 0;
                pendingName = picked.name;
                int need = shownName == "bottom" && fieldNow ? 1 :
                    shownName == "battle-menu" && (pendingName == "top" || pendingName == "battle") ? 40 :
                    (shownName == "starter" || shownName == "intro") && pendingName == "top" ? 16 : 4;
                if (++pending >= need)
                {
                    scene = picked;
                    shownName = picked.name;
                    pending = 0;
                }
            }
        }
        {
            // While an autosave runs the overworld's layout stays, with the pill over it: the X
            // menu and the save screen it goes through are not shown.
            static Scene beforeSave;
            if (!gameSave.Covering(frame)) beforeSave = scene;
            else scene = beforeSave;
        }
        if (opt.logSections)
        {
            static std::string lastScene;
            if (lastScene != scene.name)
            {
                lastScene = scene.name;
                fprintf(stderr, "[scene] frame %u: %s\n", frame, lastScene.c_str());
                g_sceneShots.insert(frame);
                g_sceneShots.insert(frame + 2);
                g_sceneShots.insert(frame + 20);
            }
        }

        {
            // The game's music now (sSoundWork.currentSeqNo), a jingle on (its wait after a
            // fanfare), and whether its music channels sound (SOUNDxCNT busy).
            const uint32_t soundWork = 0x02111958;
            const int seq = (int)(readRam(soundWork + 0xBEBFC) & 0xFFFF);
            const bool jingle = (readRam(soundWork + 0xBEC00) >> 16 & 0xFFFF) != 0;
            bool busy = false, effects = false;
            for (int i = 0; i < 16; i++)
                if (nds->SPU.Read32(0x04000400 + 16 * i) >> 31)
                {
                    busy |= (SongPlayer::MusicChannels >> i & 1) != 0;
                    effects |= (SongPlayer::EffectChannels >> i & 1) != 0;
                }
            songs.Update(cfg.newSongs && !opt.lockstep, seq, busy, effects, jingle, frame);
            melonDS::ssr_SpuHearMute = silentBoot ? 0xFFFF : songs.Mute();
            // Faster than normal (fast forward, a boot passed behind the loading picture), only
            // the first emulated frame's sound is kept, which is far shorter than the time that
            // goes by; songs go on at their own pace, so the sound sent is as long as that time
            // (the game's part padded with silence). Otherwise they stuttered and slowed down.
            // (The time since the last frame shown, whatever its speed: a first fast frame after
            // normal ones takes as long as the others.)
            static uint64_t soundAt = 0;
            const uint64_t at = SDL_GetPerformanceCounter();
            if (speed > 1 && got > 0 && !opt.headless && soundAt)
            {
                const int room = std::min<int>(24000, (int)audioBuf.size() / 2);
                const int n = std::clamp((int)((at - soundAt) * 48000 / SDL_GetPerformanceFrequency()), got, room);
                std::fill(audioBuf.begin() + got * 2, audioBuf.begin() + n * 2, (int16_t)0);
                got = n;
            }
            soundAt = at;
            if (got > 0) songs.Mix(audioBuf.data(), got);
            if (got > 0) sfx.Mix(audioBuf.data(), got);
            // (Tests: SSR_AUDIO_DUMP=<file> writes what is heard there, 48 kHz stereo 16-bit.)
            static FILE* audioDump = getenv("SSR_AUDIO_DUMP") ? fopen(getenv("SSR_AUDIO_DUMP"), "wb") : nullptr;
            if (audioDump && got > 0) fwrite(audioBuf.data(), 4, got, audioDump);
        }
        if (audio && got > 0)
        {
            // Drop audio rather than let latency grow if the queue backs up.
            if (SDL_GetQueuedAudioSize(audio) < 48000 * 4 / 4)
                SDL_QueueAudio(audio, audioBuf.data(), got * 4);
        }
        if (opt.audioStats && got > 0)
        {
            for (int i = 0; i < got * 2; i++)
            {
                int v = abs(audioBuf[i]);
                if (v > AudioPeak) AudioPeak = v;
                AudioSumSq += (double)audioBuf[i] * audioBuf[i];
            }
            AudioSamples += (uint64_t)got * 2;
        }

        if (opt.countChanges && top)
        {
            static uint64_t lastTop = 0;
            uint64_t h = Fnv1a(top, 256 * 192 * 4);
            if (h != lastTop) TopChanges++;
            lastTop = h;
        }

        if (opt.posEvery && frame % opt.posEvery == 0)
        {
            // sFieldSysPtr -> location (map id) and playerAvatar -> mapObject -> current tile.
            auto rd = [&](uint32_t a) -> uint32_t {
                if (a < 0x02000000 || a >= 0x02400000) return 0;
                uint32_t v;
                memcpy(&v, nds->MainRAM + (a & nds->MainRAMMask), 4);
                return v;
            };
            uint32_t fs = rd(0x021D4158), loc = rd(fs + 0x20), obj = rd(rd(fs + 0x40) + 0x30);
            printf("pos %u map %u x %u z %u\n", frame, rd(loc), rd(obj + 0x64), rd(obj + 0x6C));
        }
        // Development aid: SSR_DUMP_RAM=<frame> writes main RAM to ram_<frame>.bin in the shot folder.
        if (const char* dump = getenv("SSR_DUMP_RAM"); dump && (uint32_t)atoi(dump) == frame)
        {
            const std::string path = (opt.shotDir.empty() ? std::string(".") : opt.shotDir) + "/ram_" + std::to_string(frame) + ".bin";
            if (FILE* f = fopen(path.c_str(), "wb"))
            {
                fwrite(nds->MainRAM, 1, nds->MainRAMMask + 1, f);
                fclose(f);
            }
        }
        if (opt.hashEvery && frame % opt.hashEvery == 0)
        {
            uint64_t fb = Fnv1a(top, 256 * 192 * 4);
            fb = Fnv1a(bot, 256 * 192 * 4, fb);
            uint64_t ram = Fnv1a(nds->MainRAM, nds->MainRAMMask + 1);
            printf("frame %u fb %016llx ram %016llx pc9 %08x pc7 %08x\n", frame,
                (unsigned long long)fb, (unsigned long long)ram, nds->ARM9.R[15], nds->ARM7.R[15]);
        }

        // Overlay: touch cursor, status message, pause menu.
        bool overlayUsed = false;
        canvas.Resize(outW, outH);
        canvas.Clear();
        const SDL_Rect cursorRect = merged
            ? SDL_Rect { (int)view.originX, (int)view.originY, (int)(256 * view.unit), (int)(192 * view.unit) }
            : rects[1];
        if (frame < cursorShownUntil && cursorRect.w > 0 && !uiOpen)
        {
            int cx = cursorRect.x + (int)(cursorX * cursorRect.w / 256);
            int cy = cursorRect.y + (int)(cursorY * cursorRect.h / 192);
            int arm = cursorRect.w / 40 + 3;
            int t = outH / 360 + 1;
            canvas.Fill(cx - arm - 1, cy - t - 1, arm * 2 + 3, t * 2 + 3, 0xFF000000);
            canvas.Fill(cx - t - 1, cy - arm - 1, t * 2 + 3, arm * 2 + 3, 0xFF000000);
            canvas.Fill(cx - arm, cy - t, arm * 2 + 1, t * 2 + 1, curPress ? 0xFFFFD75E : 0xFFFFFFFF);
            canvas.Fill(cx - t, cy - arm, t * 2 + 1, arm * 2 + 1, curPress ? 0xFFFFD75E : 0xFFFFFFFF);
            overlayUsed = true;
        }
        // The top right corner, below the on-screen R button when those controls are shown
        // (their top row ends at 12% of the height).
        int cornerY = touchPadUsed && touchPad.visible ? outH * 13 / 100 : 0;
        // (With the HUD's sprites: pills, in design units below the corner.)
        const bool hudFree = !menu.open && screen == SCR_NONE && !startOpen;
        const bool online = spritesOk && Party_Link() != PartyLink::Off && hudFree;
        const bool trayShown = online && view.width >= 300 && fieldActive();
        // Without the widescreen world (turned off, or a narrow view) the others' trainers are
        // drawn over the game's own picture of the overworld, when it is shown whole and
        // unscaled in the middle of the view: their place by the game's camera, their frame
        // from the game's sprites.
        const bool dsWorld = !melonDS::Wide.Owner && merged && fieldActive() && !scene.items.empty() && scene.items[0].screen == 0 &&
            scene.items[0].sw == 256 && scene.items[0].sh == 192 && scene.items[0].dx == 0 && scene.items[0].dy == 0 && scene.items[0].scale == 1.0f;
        auto dsPlace = [&](double x, double y, double z, float& px, float& py) {
            float u, v;
            if (!worldBlocks.PlaceInView(*nds, 256, 1, x, y, z, u, v)) return false;
            px = view.originX + u * 256 * view.unit;
            py = view.originY + v * 192 * view.unit;
            return true;
        };
        if (dsWorld && Party_Link() != PartyLink::Off)
            for (const WorldBlocks::Trainer& t : remoteDraw)
            {
                // A person is 32 world units square, its feet a little towards the camera.
                float lx, ly, rx, ry;
                Image img;
                if (!dsPlace((t.x - 0x10000) / 4096.0, t.y / 4096.0, (t.z + 0x6000) / 4096.0, lx, ly) ||
                    !dsPlace((t.x + 0x10000) / 4096.0, t.y / 4096.0, (t.z + 0x6000) / 4096.0, rx, ry) ||
                    !worldBlocks.SpriteImage(*nds, (uint16_t)t.sprite, t.facing, img, t.frame))
                    continue;
                const float size = rx - lx;
                Blit(canvas, img, 0, 0, (float)img.w, (float)img.h, lx, ly - size, size, size, false);
                overlayUsed = true;
            }
        // Names over the trainers, emotes, the emote wheel, and pointers to the others when
        // they are off the screen; drawn into `cv` by the view of the picture shown.
        auto drawWorldTags = [&](Canvas& cv) {
            if (!online || !(trayShown || dsWorld) || !(melonDS::Wide.Owner || dsWorld)) return;
            const float W = ui.W / ui.s, H = ui.H / ui.s;
            const PartyPeer* peers = Party_Peers();
            const uint32_t now = Party_Now();
            const int me = Party_Me();
            auto emoteOf = [&](const PartyPeer& p) { return p.walk.emote >= 0 && now - p.walk.emoteAt < 3000 ? p.walk.emote : -1; };
            // Above the head: a person is 32 world units tall, drawn a little towards the camera.
            auto headAt = [&](double x, double y, double z, float& u, float& v) {
                if (dsWorld)
                {
                    if (!dsPlace(x / 4096, y / 4096 + 30, (z + 0x6000) / 4096, u, v)) return false;
                    u /= ui.W;
                    v /= ui.H;
                    return true;
                }
                return worldBlocks.PlaceInView(*nds, melonDS::Wide.ViewWidth, melonDS::Wide.Zoom, x / 4096, y / 4096 + 30,
                    (z + 0x6000) / 4096, u, v);
            };
            float u, v;
            auto posOf = [&](int i, double out[3]) {
                const bool fromShown = shownView.camera.valid && shownView.on[i];
                for (int k = 0; k < 3; k++)
                    out[k] = fromShown ? shownView.pos[i][k] : i == 4 ? (k == 0 ? peers[me].walk.x : k == 1 ? peers[me].walk.y : peers[me].walk.z)
                        : (k == 0 ? remotes[i].x : k == 1 ? remotes[i].y : remotes[i].z);
            };
            double mine[3] = {};
            if (me >= 0) posOf(4, mine);
            for (int i = 0; i < 4; i++)
            {
                if (i == me || !remotes[i].on) continue;
                double at[3];
                posOf(i, at);
                const PartyPeer& p = peers[i];
                const bool seen = headAt(at[0], at[1], at[2], u, v);
                if (seen && u > 0.03f && u < 0.97f && v > 0.08f && v < 0.97f)
                    HudOnline::NamePill(cv, ui, sprites, font, u * W, v * H, p, emoteOf(p));
                else if (me >= 0)
                {
                    // On the edge of the screen, the way they are from this player (the camera
                    // looks north, so the world's x and z are the screen's across and down).
                    const float ang = (float)std::atan2(at[2] - mine[2], at[0] - mine[0]);
                    const float ca = std::cos(ang), sa = std::sin(ang);
                    const float k = std::min((W / 2 - 80) / std::max(std::fabs(ca), 1e-3f), (H / 2 - 150) / std::max(std::fabs(sa), 1e-3f));
                    HudOnline::OffscreenPointer(cv, ui, sprites, font, W / 2 + ca * k, H / 2 + sa * k, ang, p,
                        p.walk.map != peers[me].walk.map ? Party_PlaceName(p.walk.map) : "", W);
                }
            }
            if (me >= 0 && peers[me].walk.shown && headAt(mine[0], mine[1], mine[2], u, v))
            {
                if (emoteWheel) HudOnline::EmoteWheel(cv, ui, sprites, u * W, v * H - 40);
                else HudOnline::NamePill(cv, ui, sprites, font, u * W, v * H, peers[me], emoteOf(peers[me]));
            }
        };
        tagCanvas.Resize(outW, outH);
        tagCanvas.Clear();
        drawWorldTags(opt.headless ? canvas : tagCanvas);
        if (online && Party_Me() >= 0 && appRunning("Battle") && (partyMap == 2 || partyMap == 4 || partyMap == 5))
        {
            // A link battle: who against whom (the others in the same room, in a battle too).
            const PartyPeer* peers = Party_Peers();
            std::vector<PartyPeer> them;
            for (int i = 0; i < 4; i++)
                if (i != Party_Me() && peers[i].present && peers[i].state == PS_BATTLE && peers[i].map == partyMap) them.push_back(peers[i]);
            // The turn timer: how long this player has been choosing, else how long the other
            // side has (only shown: nothing is chosen for anyone).
            auto mmss = [](uint32_t ms) {
                char b[16];
                snprintf(b, sizeof b, "%u:%02u", ms / 60000, ms / 1000 % 60);
                return std::string(b);
            };
            std::string note;
            const uint32_t now = Party_Now();
            if (myBattle.at && myBattle.choosing) note = "Your move \xC2\xB7 " + mmss(now - myBattle.choosingAt);
            else
                for (const PartyPeer& p : them)
                    if (p.battle.at && now - p.battle.at < 5000 && p.battle.choosing)
                        note = "Waiting for " + p.name + " \xC2\xB7 " + mmss(now - p.battle.choosingAt);
            HudOnline::VsBanner(canvas, ui, sprites, font, peers[Party_Me()], them, note);
        }
        if (online)
        {
            HudOnline::SessionBadge(canvas, ui, sprites, font, 32, 32 + cornerY / ui.s, cfg.showPing);
            if (trayShown) HudOnline::ChipTray(canvas, ui, sprites, font, ui.W / ui.s - 32, 32 + cornerY / ui.s);
            // Watching another player's battle (Friends, Session: Watch): until it has ended.
            if (watching >= 0)
            {
                const PartyPeer* peers = Party_Peers();
                const PartyPeer& w = peers[watching];
                const bool live = w.present && w.battle.at && Party_Now() - w.battle.at < 5000;
                if (!w.present || (!live && w.state != PS_BATTLE)) watching = -1;
                else if (live)
                {
                    // A link battle's other side: the other player in a link battle now.
                    std::string foe;
                    for (int i = 0; i < 4; i++)
                        if (i != watching && peers[i].present && peers[i].state == PS_BATTLE && w.battle.link &&
                            (i == Party_Me() ? myBattle.link : peers[i].battle.link))
                            foe = peers[i].name;
                    HudOnline::SpectatorCard(canvas, ui, sprites, font, 32, 112 + cornerY / ui.s, w, foe,
                        [&](int species) { return romArt.PokemonIcon(species); });
                }
            }
            if (Party_Link() == PartyLink::Lost)
            {
                std::string host;
                for (int i = 0; i < 4; i++)
                    if (Party_Peers()[i].present && Party_Peers()[i].host) host = Party_Peers()[i].name;
                HudOnline::LostCard(canvas, ui, sprites, font, host);
            }
            overlayUsed = true;
        }
        float pillY = 32 + cornerY / ui.s + (trayShown ? 116 : 0);
        if (cfg.showFps && !opt.headless && spritesOk)
        {
            pillY = DrawCornerPill(canvas, ui, sprites, font, ui.W / ui.s - 32, pillY, std::to_string(shownFps) + " fps");
            overlayUsed = true;
        }
        else if (cfg.showFps && !opt.headless)
        {
            int sc = outH / 240 < 1 ? 1 : outH / 240;
            const std::string text = std::to_string(shownFps) + " FPS";
            const int tw = Canvas::TextWidth(text, sc) + 8 * sc;
            canvas.Fill(outW - tw, cornerY, tw, 13 * sc, 0xC0000000);
            canvas.Text(outW - tw + 4 * sc, cornerY + 3 * sc, text, sc, 0xFFFFFFFF);
            cornerY += 13 * sc;
            overlayUsed = true;
        }
        if (spritesOk && (gameSave.Busy() || frame < resumeShownUntil))
        {
            // Saving the game; a resume point made (the animation alone).
            pillY = saveIndicator.Draw(canvas, ui, sprites, font, ui.W / ui.s - 32, pillY, gameSave.Busy() ? "Saving..." : "", frame * 1000 / 60);
            overlayUsed = true;
        }
        if (speed > 1 && spritesOk)
        {
            DrawCornerPill(canvas, ui, sprites, font, ui.W / ui.s - 32, pillY, ">> Fast forward");
            overlayUsed = true;
        }
        else if (speed > 1)
        {
            int sc = outH / 240 < 1 ? 1 : outH / 240;
            canvas.Text(outW - Canvas::TextWidth(">>", sc) - 4 * sc, cornerY + 4 * sc, ">>", sc, 0xFFFFFFFF);
            overlayUsed = true;
        }
#ifdef __ANDROID__
        if (!updateSaid && UpdateCheck::g_done)
        {
            updateSaid = true;
            if (!UpdateCheck::g_newer.empty())
            {
                say("Version " + UpdateCheck::g_newer + " is out: rebuild the app on your PC", "icons/icon_info", 600);
            }
        }
#endif
        if (spritesOk)
        {
            // Toasts about other players wait while the game wants attention: a battle, a
            // text box (FieldSystem.textbox_open) or a menu.
            const u32 fs = readRam(0x021D4158);
            const bool textbox = fs && (readRam(fs + 0xD0) >> 16 & 0x40);
            toasts.Update(frame, !hudFree || !fieldActive() || textbox);
            if (toasts.Any() && hudFree)
            {
                // Below the on-screen controls' top row when they are shown; two at most on a
                // narrow view.
                const bool controls = touchPadUsed && touchPad.visible;
                const float ty = (controls ? outH * 13 / 100 / ui.s : 32) + (online ? 76 : 0);
                toasts.Draw(canvas, ui, sprites, font, frame, 32, ty, view.width >= 300 ? 4 : 2);
                overlayUsed = true;
            }
        }
        else if (frame < toastUntil)
        {
            int sc = outH / 240 < 1 ? 1 : outH / 240;
            const int tw = Canvas::TextWidth(toast, sc) + 8 * sc;
            // With on-screen controls, centred below their top row (L, MENU and R end at
            // 12% of the height) instead of under the L button.
            const bool controls = touchPadUsed && touchPad.visible;
            const int tx = controls ? (outW - tw) / 2 : 0, ty = controls ? outH * 13 / 100 : 0;
            canvas.Fill(tx, ty, tw, 13 * sc, 0xC0000000);
            canvas.Text(tx + 4 * sc, ty + 3 * sc, toast, sc, 0xFFFFFFFF);
            overlayUsed = true;
        }
        ui.Set(canvas.w, canvas.h);
        sprites.HiRes = ui.s > 1.01f;
        // The battle's commands and moves redrawn over the strip under the field (hudbattle.h);
        // also while one set slides out and the other in, as long as the game's memory says
        // which is up. Otherwise the strip stays as it is.
        bool battleHudDrawn = false;
        if (spritesOk && cfg.redrawn && merged && hudFree &&
            (!strcmp(scene.name, "battle-cmd") || !strcmp(scene.name, "battle-moves") || !strcmp(scene.name, "battle-strip") ||
                !strcmp(scene.name, "battle-pick")))
        {
            HudBattle::State bs;
            // (Between menus, idle, only while the strip is kept up for the slide.)
            if (HudBattle::Read(nds->MainRAM, nds->MainRAMMask, bs) && (!bs.idle || !strcmp(scene.name, "battle-strip")))
            {
                // The chosen button once the input has taken this strip over (it starts at 0).
                const bool pickKind = bs.kind == HudBattle::Target || bs.kind == HudBattle::Choice;
                const bool agree = !bs.idle && !strcmp(scene.name, pickKind ? "battle-pick" : bs.moves ? "battle-moves" : "battle-cmd");
                const int sel = !agree ? -1 : battleStripKind == scene.name ? battleSel : 0;
                const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                std::vector<HudHint> hints;
                if (!bs.idle) hints = { { { "buttons/button_dpad" }, "Choose" }, { { accept }, "Confirm" } };
                if (bs.moves || bs.kind == HudBattle::Target || (bs.kind == HudBattle::Commands && bs.cancel)) hints.push_back({ { back }, "Back" });
                battleHud.Draw(canvas, ui, sprites, font, battleMoves, romArt, bs, sel, hints, (view.originY + 137 * view.unit) / ui.s);
                overlayUsed = true;
                battleHudDrawn = true;
            }
        }
        // The HP boxes, the message box and the level-up stats redrawn (hudbattleui.h), where
        // the battle's wide picture was built without the game's boxes.
        // (Read every frame: the HP bars' animation is followed from one frame to the next.)
        const int tu = (nds->PowerControl9 & (1 << 15)) ? 0 : 1;
        const auto& unit = tu == 0 ? nds->GPU.GPU2D_A : nds->GPU.GPU2D_B;
        HudBattleUi::Vram battleVram;
        for (int b = 0; b < 4; b++) battleVram.bgCnt[b] = unit.BGCnt[b];
        battleVram.charOffset = tu == 0 ? ((unit.DispCnt >> 24) & 7) * 0x10000 : 0;
        battleVram.read8 = [&](uint32_t a) { return tu == 0 ? nds->GPU.ReadVRAM_ABG<u8>(a) : nds->GPU.ReadVRAM_BBG<u8>(a); };
        if (cfg.redrawn && spritesOk && battleUiReader.Read(nds->MainRAM, nds->MainRAMMask, battleVram, battleUi) && battleUiWide && merged &&
            hudFree && !strncmp(scene.name, "battle", 6))
        {
            // The game's fade, as it applies to the message box's layer (BG1): the master
            // brightness, or a brightness effect on the layer; or as the box looks.
            HudBattleUi::Fade fade;
            fade.mode = (unit.MasterBrightness >> 14) & 3;
            fade.f = std::min(16, unit.MasterBrightness & 0x1F);
            if (fade.mode != 1 && fade.mode != 2) fade.mode = 0;
            if (!fade.mode && (unit.BlendCnt & 2) && ((unit.BlendCnt >> 6) & 3) >= 2)
            {
                fade.mode = ((unit.BlendCnt >> 6) & 3) == 2 ? 1 : 2;
                fade.f = std::min<int>(16, unit.EVY);
            }
            auto& soft = static_cast<GPU2D::SoftRenderer&>(nds->GPU.GetRenderer2D());
            const uint32_t* topPixels = nds->GPU.Framebuffer[nds->GPU.FrontBuffer][0].get();
            const bool msgShown = HudBattleUi::MessageShown(battleUi.msg, soft.LayerFlags[tu]);
            if (msgShown) HudBattleUi::FadeFromScreen(battleUi.msg, topPixels, fade);
            // The HP boxes as the game's own look (hidden, faded).
            HudBattleUi::Look(battleUi, topPixels, soft.LayerFlags[tu], soft.ObjPrio[tu]);
            // (The band of the touch screen's menus has the message box's place then.)
            const bool band = !battleHudDrawn && msgShown;
            const float half = view.width / 2 * view.unit;
            battleUiHud.Draw(canvas, ui, sprites, font, battleUi, view.originX + 128 * view.unit - half, view.originX + 128 * view.unit + half,
                view.originY, view.unit, (view.originY + 137 * view.unit) / ui.s, band, fade);
            overlayUsed = true;
        }
        // The bag of a battle redrawn (hudbattlebag.h), in place of the touch screen's.
        static HudBattleBag::Reader battleBagReader;
        static HudBag::Texts battleTexts(rom.data(), rom.size());
        if (spritesOk && cfg.redrawn && merged && hudFree && !strcmp(scene.name, "battle-submenu"))
        {
            const auto& bottom = tu == 0 ? nds->GPU.GPU2D_B : nds->GPU.GPU2D_A;
            HudBattleUi::Vram& v = battleBagReader.vram;
            for (int b = 0; b < 4; b++) v.bgCnt[b] = bottom.BGCnt[b];
            v.charOffset = tu == 1 ? ((bottom.DispCnt >> 24) & 7) * 0x10000 : 0;
            v.read8 = [&](uint32_t a) { return tu == 1 ? nds->GPU.ReadVRAM_ABG<u8>(a) : nds->GPU.ReadVRAM_BBG<u8>(a); };
            battleBagReader.words = &battleUiHud.Words();
            HudBattleBag::State st;
            if (battleBagReader.Read(nds->MainRAM, nds->MainRAMMask, frame, st))
            {
                const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Choose" }, { { accept }, st.screen == 2 ? "Use" : "Select" },
                    { { back }, st.screen == 0 ? "Close bag" : "Back" } };
                HudBattleBag::Draw(canvas, ui, sprites, font, romArt, battleTexts, st, hints);
                overlayUsed = true;
            }
            else
            {
                // The party of a battle (hudbattleparty.h), drawn as the party and summary
                // outside battles.
                static HudBattleParty::Reader battlePartyReader;
                battlePartyReader.vram = v;
                battlePartyReader.words = &battleUiHud.Words();
                HudBattleParty::State ps;
                if (battlePartyReader.Read(nds->MainRAM, nds->MainRAMMask, frame, ps))
                {
                    const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                    const HudParty::MonIconFn icon = [&](const PartyMon& m) { return romArt.PokemonIcon(m.species, m.form, m.egg); };
                    if (ps.screen == 2)
                    {
                        HudParty::SummaryScreen sv;
                        sv.mon = ps.list.mons[ps.chosen];
                        sv.page = 1;
                        sv.slot = ps.chosen;
                        sv.count = ps.list.count;
                        std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Choose" }, { { accept }, "Select" }, { { back }, "Back" } };
                        HudParty::DrawSummary(canvas, ui, sprites, font, sv, icon, hints);
                    }
                    else
                    {
                        std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Choose" }, { { accept }, "Select" }, { { back }, "Back" } };
                        if (!ps.list.message.empty()) hints = { { { accept }, "Next" } };
                        HudParty::DrawParty(canvas, ui, sprites, font, ps.list, icon, hints);
                    }
                    overlayUsed = true;
                }
                battlePartyReader.vram.read8 = nullptr;
            }
            v.read8 = nullptr;
        }
        // The Pokédex page after a catch redrawn (hudbattledex.h): the wild Pokémon in battle.
        if (spritesOk && cfg.redrawn && merged && hudFree && (!strcmp(scene.name, "top") || !strcmp(scene.name, "battle")))
        {
            const HudBattleUi::Mem mem{ nds->MainRAM, nds->MainRAMMask };
            const uint32_t bs = HudBattleUi::Battle(mem);
            const int species = bs ? (int)mem.r16(mem.r32(bs + 0x30) + 0x2D40 + 0xC0) : 0;
            float bright = 1;
            if (species >= 1 && species <= 493 && HudBattleDex::Shown(nds->GPU.Framebuffer[nds->GPU.FrontBuffer][0].get(), bright))
            {
                const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B);
                HudBattleDex::Draw(canvas, ui, sprites, font, romArt, rom.data(), rom.size(), species, bright, { { { accept }, "Next" } });
                overlayUsed = true;
            }
        }
        // The overworld's message box redrawn (hudworld.h), with a script's yes/no question,
        // while the single-screen view shows the overworld and nothing but the box is on the
        // game's BG3. The game's box (BG3) and the question's buttons from the touch screen are
        // left out of the picture then; the box for two frames after (its tiles go a frame later).
        {
            static int worldBoxHold = 0;
            static const bool worldTested = getenv("SSR_WORLDUI_SELFTEST") &&
                fprintf(stderr, "[worldui] self-test %s\n", HudWorld::SelfTest() ? "passed" : "FAILED");
            (void)worldTested;
            // The game's fade of the top screen (master brightness, or a brightness effect of
            // its layers, as the widescreen world takes it) over a redrawn panel at (x, y, w, h).
            auto worldFade = [&](float x, float y, float w, float h) {
                const auto& tu = (nds->PowerControl9 & (1 << 15)) ? nds->GPU.GPU2D_A : nds->GPU.GPU2D_B;
                int mode = (tu.MasterBrightness >> 14) & 3, fade = std::min(16, tu.MasterBrightness & 0x1F);
                if (mode != 1 && mode != 2) fade = 0;
                if (const int effect = (tu.BlendCnt >> 6) & 3; (tu.BlendCnt & 1) && effect >= 2)
                    if (const int evy = std::min<int>(16, tu.EVY); evy > fade) mode = effect == 2 ? 1 : 2, fade = evy;
                if (fade)
                    FillBlend(canvas, ui(x - 16), ui(y - 16), ui(w + 32), ui(h + 32), (uint32_t)(fade * 255 / 16) << 24 | (mode == 1 ? 0xFFFFFF : 0));
            };
            const bool fieldView = (!strcmp(scene.name, "top") || !strcmp(scene.name, "choice")) && !scene.items.empty() && scene.items[0].screen == 0;
            if (worldBoxHold > 0 && fieldView) scene.items[0].drop &= ~LAYER_BG3;
            const HudParty::Ram ram{ nds->MainRAM, nds->MainRAMMask + 1 };
            const bool may = spritesOk && cfg.redrawn && merged && hudFree && fieldView;
            // A sign slides in and out: drawn as low as the game's is. It slides away with its
            // last page (the game lets go of it first): that page, while it is on the screen.
            const int top = may ? HudWorld::BoxTop(screenImages[0].flags) : -1;
            int slide = std::max(0, top - HudWorld::kBoxY - 1);
            HudWorld::Dialogue dlg;
            bool shown = top >= 0 && HudWorld::ReadDialogue(ram, dlg) && (dlg.sign >= 0 || !slide);
            static HudWorld::Dialogue worldLast;
            static uint32_t worldLastAt = 0;
            // A script lets go of its box and leaves the last page on the screen (HoldMsg, before
            // a list of choices): that page, while the game's box shows exactly what it did.
            static uint64_t worldBoxPicture = 0;
            const uint64_t picture = top >= 0 ? HudWorld::BoxPicture(screenImages[0]) : 0;
            if (shown) worldLast = dlg, worldLastAt = frame, worldBoxPicture = dlg.more ? 0 : picture;
            else if (top >= 0 && slide && worldLast.sign >= 0 && frame - worldLastAt < 12)
            {
                dlg = worldLast;
                dlg.more = false;
                shown = true;
            }
            else if (top >= 0 && !slide && picture && picture == worldBoxPicture && worldLast.sign < 0)
            {
                dlg = worldLast;
                shown = true;
            }
            else
                worldBoxPicture = 0;
            // The place's name sliding down at the top, when nothing else is on the game's BG3.
            std::string placeName;
            int placeSlide = 0;
            const bool placeShown = may && !shown && HudWorld::OnlyTheName(screenImages[0].flags) &&
                HudWorld::ReadMapName(ram, placeName, placeSlide);
            if (placeShown)
            {
                const float u = view.unit / ui.s, x0 = (mergedX + view.originX) / ui.s, y0 = view.originY / ui.s;
                const float px = x0 + 6 * u, py = y0 + (6 - placeSlide) * u - placeSlide * 1.5f;
                HudWorld::DrawMapName(canvas, ui, sprites, font, placeName, px, py);
                worldFade(px, py, HudTextWidth(ui, font, 48, placeName) + 150, 84);
                overlayUsed = true;
            }
            worldBoxHold = shown || placeShown ? 3 : std::max(0, worldBoxHold - 1);
            if (worldBoxHold > 0 && fieldView) scene.items[0].drop |= LAYER_BG3;
            if (shown)
            {
                HudWorld::Choice choice;
                if (HudWorld::ReadChoice(ram, choice)) scene.items.resize(1);
                const float u = view.unit / ui.s, x0 = (mergedX + view.originX) / ui.s, y0 = view.originY / ui.s;
                HudWorld::Draw(canvas, ui, sprites, font, dlg, x0 + HudWorld::kBoxX * u, y0 + (HudWorld::kBoxY + slide) * u, HudWorld::kBoxW * u,
                    HudWorld::kBoxH * u - 6, frame, &choice);
                worldFade(x0 + HudWorld::kBoxX * u, y0 + (HudWorld::kBoxY + slide) * u, HudWorld::kBoxW * u, HudWorld::kBoxH * u - 6);
                overlayUsed = true;
            }
            // The game's X menu, drawn where its touch-screen panel stands, which is left out;
            // as it was last while the game closes it or opens what was chosen (its panel stays
            // up for a few frames).
            static HudWorld::XMenu xmLast;
            static uint32_t xmAt = 0;
            HudWorld::XMenu xm;
            const bool xmView = spritesOk && cfg.redrawn && merged && hudFree && !strcmp(scene.name, "start-menu") && scene.items.size() == 2;
            if (xmView && HudWorld::ReadXMenu(ram, xm)) xmLast = xm, xmAt = frame;
            else if (xmView && xmAt && frame - xmAt <= 20)
                xm = xmLast;
            if (xm.cursor >= 0)
            {
                const Element& el = scene.items[1];
                const float u = view.unit / ui.s, x = (mergedX + view.originX) / ui.s + el.dx * u, y = view.originY / ui.s + el.dy * u;
                const float w = el.sw * el.scale * u, h = el.sh * el.scale * u;
                HudWorld::DrawXMenu(canvas, ui, sprites, font, xm, trainerName(), x, y, w, h);
                worldFade(x, y, w, h); // (the screen fading to what was chosen)
                scene.items.resize(1);
                overlayUsed = true;
            }
        }
        if (menu.open && spritesOk)
        {
            // The buttons as they are labelled on the controller (see PadButtonFor).
            const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
            std::vector<HudHint> hints = { { { "buttons/button_l", "buttons/button_r" }, "Tab" } };
            const MenuItem* sel = menu.Selected();
            if (remapping >= 0)
                hints = { { {}, "Press the button to use" } };
            else if (sel && sel->id >= M_REMAP && sel->id < M_REMAP + RemapCount)
            {
                hints.push_back({ { accept }, "Remap" });
                hints.push_back({ { PadGlyph(PAD_Y) }, "Usual" });
            }
            else if (sel && (!sel->value.empty() || !sel->glyph.empty()) && !sel->disabled)
                hints.push_back({ { "buttons/button_dpad" }, "Change" });
            else if (sel && !sel->disabled)
                hints.push_back({ { accept }, "Select" });
            if (remapping < 0) hints.push_back({ { back }, "Back" });
            // The menu drawn again only when something on it changes (drawing its panels takes
            // a good part of a frame): otherwise the last drawing is put back.
            std::string key = std::to_string(menu.tab) + "|" + std::to_string(menu.selected) + "|" + std::to_string(canvas.w) + "x" +
                std::to_string(canvas.h) + "|" + std::to_string(remapping) + "|" + std::to_string(Party_DexCount()) + "|" +
                std::to_string(Party_MyBadges()) + "|" + std::to_string(sessionStats.linkBattles) + "|" + std::to_string(sessionStats.trades);
            for (const MenuItem& it : menu.items)
                key += "|" + it.label + "=" + it.value + it.glyph + (it.disabled ? "!" : "") + std::to_string((int)(it.bar * 1000));
            for (const HudHint& h : hints)
                for (const std::string& g : h.glyphs) key += "|" + g + h.label;
            static std::string menuKey;
            static std::vector<uint32_t> menuPicture;
            if (key == menuKey && menuPicture.size() == canvas.px.size())
            {
                std::copy(menuPicture.begin(), menuPicture.end(), canvas.px.begin());
                canvas.Mark(0, 0, canvas.w, canvas.h);
            }
            else if (menu.tab == T_PROFILE)
            {
                HudRect side;
                DrawPauseMenu(canvas, ui, sprites, font, menu, hints, 560, &side);
                PartyPeer me;
                me.name = onlineName();
                me.colour = cfg.onlineColour >= 0 ? cfg.onlineColour : std::max(0, Party_Me());
                me.partner = cfg.onlinePartner;
                HudOnline::TrainerCardPreview(canvas, ui, sprites, font, side, me, Party_DexCount(), Party_MyBadges(), sessionStats, monIcon);
            }
            else
                DrawPauseMenu(canvas, ui, sprites, font, menu, hints);
            if (key != menuKey)
            {
                menuKey = key;
                menuPicture = canvas.px;
            }
            overlayUsed = true;
        }
        else if (menu.open)
        {
            menu.Draw(canvas, "POKEMON SOULSILVER", cfg.byLabel ? "A: SELECT   B: BACK" : "RIGHT: SELECT   BOTTOM: BACK");
            overlayUsed = true;
        }
        if (savePhase >= 0 && !gameSave.Busy())
        {
            int year, month, day, hour, minute, second;
            nds->RTC.GetDateTime(year, month, day, hour, minute, second);
            char clock[16];
            snprintf(clock, sizeof clock, "%d:%02d", hour, minute);
            const std::string place = Party_PlaceName(partyMap);
            std::string session;
            if (Party_Link() == PartyLink::Online)
            {
                const uint32_t secs = Party_SessionSeconds();
                char t[16];
                snprintf(t, sizeof t, "%u:%02u", secs / 3600, secs / 60 % 60);
                session = std::to_string(Party_Count()) + (Party_Count() == 1 ? " player \xC2\xB7 " : " players \xC2\xB7 ") + t;
            }
            const std::vector<int> party = Party_MySpecies();
            const int partnerSpecies = cfg.onlinePartner ? cfg.onlinePartner : party.empty() ? 0 : party[0];
            const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
            std::vector<HudHint> hints;
            if (savePhase == 0) hints = { { { "buttons/button_dpad" }, "Choose" }, { { accept }, "Confirm" }, { { back }, "Back" } };
            HudScreens::SaveScreen(canvas, ui, sprites, font, trainerName(), Party_MyCard(), Party_DexCount(),
                trainerArt.px.empty() ? nullptr : &trainerArt, romArt.PokemonIcon(partnerSpecies), savePhase, saveSel, session,
                (place.empty() ? "" : place + " \xC2\xB7 ") + clock, hints);
            overlayUsed = true;
        }
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && appRunning("TrainerCard"))
        {
            PartyPeer me;
            me.name = onlineName();
            me.colour = cfg.onlineColour >= 0 ? cfg.onlineColour : std::max(0, Party_Me());
            const PartyCard card = Party_MyCard();
            if (card.known)
            {
                HudScreens::TrainerCard(canvas, ui, sprites, font, me, card, Party_DexCount(), sessionStats, Party_Link() != PartyLink::Off,
                    trainerArt.px.empty() ? nullptr : &trainerArt, cfg.onlinePartner ? romArt.PokemonIcon(cfg.onlinePartner) : nullptr,
                    { { { PadGlyph(cfg.byLabel ? PAD_B : PAD_A) }, "Back" } });
                overlayUsed = true;
            }
        }
        // The bag redrawn (HudBag), read from the game's memory each frame, with the item's
        // menu, the quantity to trash and the messages about it. A frame ends at times while
        // the game is halfway through changing what is read (a Pokemon decrypted, a menu
        // being built): for up to three frames that do not check out, the last picture stays
        // rather than the game's own flashing up (the party and summary below too).
        static uint32_t bagManager = 0, bagActions = 0, bagGood = 0;
        static bool bagShown = false;
        static HudBag::State bagLast;
        static HudBag::Texts bagTexts(rom.data(), rom.size());
        bool bagNow = false;
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && appRunning("Bag"))
            for (unsigned i = 0; i < rc_num_apps; i++)
                if (!strcmp(rc_apps[i].name, "Bag"))
                {
                    HudBag::State bs;
                    if (HudBag::FindManager(readRam, rc_apps[i].initAddr, rc_apps[i].execAddr, rc_apps[i].exitAddr, bagManager) &&
                        HudBag::ReadState(readRam, bagManager, bs, bagActions))
                    {
                        bagNow = HudBag::Shown(bs, bagShown);
                        if (bagNow) bagLast = bs, bagGood = frame;
                    }
                    else if (bagShown && frame - bagGood <= 3)
                        bagNow = true, bs = bagLast;
                    if (bagNow)
                    {
                        const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                        std::vector<HudHint> hints = { { { PadGlyph(PAD_LB), PadGlyph(PAD_RB) }, "Pocket" }, { { "buttons/button_dpad" }, "Choose" },
                            { { accept }, bs.giving ? "Give" : bs.selling ? "Sell" : "Select" }, { { back }, "Close bag" } };
                        if (bs.menu) hints = { { { "buttons/button_dpad" }, "Choose" }, { { accept }, "Select" }, { { back }, "Cancel" } };
                        if (bs.quantity) hints = { { { "buttons/button_dpad" }, "How many" }, { { accept }, "Trash" }, { { back }, "Cancel" } };
                        if (!bs.message.empty()) hints = { { { accept }, "Next" } };
                        if (bs.yesNo >= 0) hints = { { { "buttons/button_dpad" }, "Choose" }, { { accept }, "Select" }, { { back }, "No" } };
                        if (bs.picking) hints = { { { "buttons/button_dpad" }, "How many" }, { { accept }, "Sell" }, { { back }, "Cancel" } };
                        if (bs.selling) bs.money = HudShop::SellingMoney(readRam);
                        HudBag::Draw(canvas, ui, sprites, font, romArt, bagTexts, bs, hints);
                        overlayUsed = true;
                    }
                    break;
                }
        bagShown = bagNow;
        // Buying at a Poké Mart redrawn (hudshop.h) from the moment the shop opens until it
        // closes; the clerk's questions around it stay the game's own. For up to two frames
        // that do not check out (the shop half way through closing) the last picture stays.
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen)
        {
            static uint32_t shopYesNo = 0, shopGood = 0;
            static HudShop::State shopLast;
            HudShop::State ss;
            bool shopNow = false;
            if (HudShop::Read(readRam, ss, shopYesNo) && HudShop::Shown(ss))
                shopNow = true, shopLast = ss, shopGood = frame;
            else if (shopGood && frame - shopGood <= 2)
                shopNow = true, ss = shopLast;
            if (shopNow)
            {
                const std::string a = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), b = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Choose" }, { { a }, ss.cursor == 8 ? "Leave" : "Select" }, { { b }, "Leave" } };
                if (ss.step == 5 || ss.step == 7) hints = { { { "buttons/button_dpad" }, "How many" }, { { a }, "Buy" }, { { b }, "Cancel" } };
                else if (ss.yesNo >= 0) hints = { { { "buttons/button_dpad" }, "Choose" }, { { a }, "Select" }, { { b }, "No" } };
                else if (ss.step >= 10 && ss.step <= 15) hints = { { { a }, "Next" } };
                HudShop::Draw(canvas, ui, sprites, font, romArt, bagTexts, ss, hints);
                overlayUsed = true;
            }
        }
        // The Battle Factory's rental screen redrawn (hudfrontier.h), while its overlay runs.
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && sectionActive("OVY_81"))
        {
            static uint32_t factoryAt = 0;
            HudFrontier::Factory fv;
            if (HudFrontier::ReadFactory({ nds->MainRAM, nds->MainRAMMask + 1 }, factoryAt, fv))
            {
                const std::string a = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), b = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Choose" }, { { a }, "Select" } };
                if (fv.menu || fv.step == 4) hints.push_back({ { b }, "Back" });
                else if (fv.numChosen) hints.push_back({ { b }, "Undo" });
                HudFrontier::DrawFactory(canvas, ui, sprites, font, fv,
                    [&](const PartyMon& m) { return romArt.PokemonIcon(m.species, m.form, m.egg); }, hints);
                overlayUsed = true;
            }
        }
        // The Battle Hall's type board redrawn (hudfrontier.h), while its overlay runs.
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && sectionActive("OVY_82"))
        {
            static uint32_t hallAt = 0;
            HudFrontier::Hall hv;
            if (HudFrontier::ReadHall({ nds->MainRAM, nds->MainRAMMask + 1 }, hallAt, hv))
            {
                const std::string a = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), b = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Choose" }, { { a }, "Select" } };
                if (hv.yesNo >= 0) hints.push_back({ { b }, "No" });
                HudFrontier::DrawHall(canvas, ui, sprites, font, hv,
                    [&](const PartyMon& m) { return romArt.PokemonIcon(m.species, m.form, m.egg); }, hints);
                overlayUsed = true;
            }
        }
        // Voltorb Flip redrawn (hudminigames.h); the game keeps its own controls.
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && appRunning("VoltorbFlip"))
            for (unsigned i = 0; i < rc_num_apps; i++)
                if (!strcmp(rc_apps[i].name, "VoltorbFlip"))
                {
                    static uint32_t vfAt = 0;
                    const HudParty::Ram ram{ nds->MainRAM, nds->MainRAMMask + 1 };
                    HudMinigames::Voltorb v;
                    const uint32_t man = HudParty::FindApp(ram, vfAt, rc_apps[i].initAddr, rc_apps[i].execAddr, rc_apps[i].exitAddr,
                        [](uint32_t) { return true; });
                    if (man && HudMinigames::ReadVoltorb(ram, man, v))
                    {
                        const std::string a = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), b = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                        const std::string x = PadGlyph(cfg.byLabel ? PAD_X : PAD_Y);
                        std::vector<HudHint> hints;
                        if (!v.menu.empty() || v.yesNo >= 0) hints = { { { "buttons/button_dpad" }, "Choose" }, { { a }, "Select" }, { { b }, "Back" } };
                        else if (v.memoFocused) hints = { { { "buttons/button_dpad" }, "Mark" }, { { a }, "Set / clear" }, { { x }, "Close memo" }, { { b }, "Back to the board" } };
                        else if (v.workflow == HudMinigames::WF_BOARD || v.workflow == HudMinigames::WF_MEMO_TOUCH)
                            hints = { { { "buttons/button_dpad" }, "Move" }, { { a }, v.memoOpen ? "Mark" : "Flip" }, { { x }, v.memoOpen ? "Close memo" : "Memo" }, { { b }, "Quit" } };
                        // (A Voltorb's message passes by itself; a number's waits for a button.)
                        else if (v.message.more || (v.workflow == HudMinigames::WF_FLIP && v.messageShown && v.task == 4 && v.type[v.focus] != 4))
                            hints = { { { a }, "Next" } };
                        HudMinigames::DrawVoltorb(canvas, ui, sprites, font, romArt, v, hints);
                        overlayUsed = true;
                    }
                    break;
                }
        // The Ruins of Alph's puzzles (hudminigames.h): redrawn around the game's board, or
        // without the new screens the cursor over the game's own picture of it.
        if (alphRead && screen == SCR_NONE && !menu.open && !startOpen && alphState.step >= 1 && alphState.step <= 6 && alphState.step != 5)
        {
            const std::string a = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), b = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
            const std::string x = PadGlyph(cfg.byLabel ? PAD_X : PAD_Y);
            const bool carrying = alphPad.phase == 2 || alphPad.phase == 3;
            std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Move" }, { { a }, carrying ? "Put down" : "Pick up" } };
            if (!carrying) hints.push_back({ { x }, "Turn" });
            hints.push_back({ { b }, carrying ? "Put back" : "Quit" });
            if (alphState.step == 6) hints.clear();
            if (spritesOk && cfg.redrawn)
            {
                HudMinigames::DrawAlph(canvas, ui, sprites, font, alphState, alphPad, bot, hints);
                overlayUsed = true;
            }
            else if (merged && alphState.step <= 4)
                for (const Element& el : scene.items)
                    if (el.screen == 1)
                    {
                        // The place under the cursor, where the merged view shows the touch screen.
                        const float s = el.scale * view.unit;
                        const float px = view.originX + (el.dx + (32 + 32 * alphPad.cx - el.sx) * el.scale) * view.unit;
                        const float py = view.originY + (el.dy + (32 * alphPad.cy - el.sy) * el.scale) * view.unit;
                        const int t = std::max(2, (int)(2 * s));
                        const uint32_t col = carrying ? 0xFFFFC23A : 0xFFFFFFFF;
                        canvas.Fill((int)px, (int)py, (int)(32 * s), t, col);
                        canvas.Fill((int)px, (int)(py + 32 * s) - t, (int)(32 * s), t, col);
                        canvas.Fill((int)px, (int)py, t, (int)(32 * s), col);
                        canvas.Fill((int)(px + 32 * s) - t, (int)py, t, (int)(32 * s), col);
                        overlayUsed = true;
                        break;
                    }
        }
        // The Pokégear redrawn (hudgear.h): its phone and map tabs, read from the game's
        // memory each frame; calls, menus and the other tabs are the game's own picture.
        static uint32_t gearManager = 0;
        static bool gearShown = false;
        static HudGear::Book gearBook(rom.data(), rom.size());
        bool gearNow = false;
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && appRunning("Pokegear"))
        {
            auto app = [](const char* name) -> const RcApp* {
                for (unsigned i = 0; i < rc_num_apps; i++)
                    if (!strcmp(rc_apps[i].name, name)) return &rc_apps[i];
                return nullptr;
            };
            HudGear::State gs;
            int step = -1;
            bool between = false;
            // The last picture stays while the Pokégear changes tabs (about 40 frames), so
            // the game's own does not flash up between them.
            static HudGear::State gearLast;
            static int gearHeld = 0;
            if (HudGear::ReadState(readRam, app, bagTexts, gearManager, gs, step, between) && HudGear::Shown(gs, step, gearShown))
            {
                gearNow = true;
                gearLast = gs;
                gearHeld = 0;
            }
            else if (between && gearShown && ++gearHeld < 90)
            {
                gearNow = true;
                gs = gearLast;
            }
            if (gearNow)
            {
                HudGear::Env env;
                env.me.name = onlineName();
                env.me.colour = cfg.onlineColour >= 0 ? cfg.onlineColour : std::max(0, Party_Me());
                env.me.map = partyMap;
                env.rd = readRam;
                int year, month, day, hour, minute, second;
                nds->RTC.GetDateTime(year, month, day, hour, minute, second);
                // Sakamoto's day of the week (0 Sunday), as the DS clock counts it.
                static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
                if (year < 100) year += 2000;
                const int y = year - (month < 3);
                env.weekday = (y + y / 4 - y / 100 + y / 400 + t[(month + 11) % 12] + day) % 7;
                const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Move" }, { { accept }, "Select" }, { { back }, gs.inTabs ? "Close" : "Back" } };
                if (!gs.menu.items.empty()) hints = { { { "buttons/button_dpad" }, "Choose" }, { { accept }, "Select" }, { { back }, "Cancel" } };
                else if (gs.call.on) hints = gs.call.waiting ? std::vector<HudHint>{ { { accept }, "Next" } } : std::vector<HudHint>{};
                else if (gs.app == HudGear::APP_RADIO && !gs.inTabs)
                    hints = { { { "buttons/button_dpad" }, "Tune" }, { { accept }, "Next station" }, { { back }, "Back" } };
                else if (gs.moving >= 0) hints = { { { "buttons/button_dpad" }, "Move" }, { { accept }, "Put here" }, { { back }, "Cancel" } };
                HudGear::Draw(canvas, ui, sprites, font, bagTexts, gearBook, gs, env, hints);
                overlayUsed = true;
            }
        }
        gearShown = gearNow;
        // Choosing where to fly, and the Town Map (hudgear.h too): the same map.
        static uint32_t flyManager = 0;
        static bool flyShown = false;
        bool flyNow = false;
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && appRunning("TownMap"))
        {
            auto app = [](const char* name) -> const RcApp* {
                for (unsigned i = 0; i < rc_num_apps; i++)
                    if (!strcmp(rc_apps[i].name, name)) return &rc_apps[i];
                return nullptr;
            };
            HudGear::State fs;
            int step = -1;
            if (HudGear::ReadFly(readRam, app, flyManager, fs, step) && HudGear::Shown(fs, step, flyShown))
            {
                flyNow = true;
                HudGear::Env env;
                env.me.name = onlineName();
                env.me.colour = cfg.onlineColour >= 0 ? cfg.onlineColour : std::max(0, Party_Me());
                env.me.map = partyMap;
                env.rd = readRam;
                const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Move" } };
                if (fs.mode == 1) hints.push_back({ { accept }, "Fly here" });
                hints.push_back({ { back }, "Close" });
                if (!fs.menu.items.empty()) hints = { { { "buttons/button_dpad" }, "Choose" }, { { accept }, "Select" }, { { back }, "Cancel" } };
                HudGear::Draw(canvas, ui, sprites, font, bagTexts, gearBook, fs, env, hints);
                overlayUsed = true;
            }
        }
        flyShown = flyNow;
        // The game's Options menu redrawn (hudoptions.h); while it leaves (step 3, 4) only if
        // it was shown before.
        static bool optionsShown = false;
        bool optionsNow = false;
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && appRunning("OptionsMenu"))
            for (unsigned i = 0; i < rc_num_apps; i++)
                if (!strcmp(rc_apps[i].name, "OptionsMenu"))
                {
                    static uint32_t optionsAt = 0;
                    HudOptions::State os;
                    if (HudOptions::ReadState(readRam, rc_apps[i].initAddr, rc_apps[i].execAddr, rc_apps[i].exitAddr, optionsAt, bagTexts, os) &&
                        (os.step <= 2 || optionsShown))
                    {
                        optionsNow = true;
                        const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                        HudOptions::Draw(canvas, ui, sprites, font, os,
                            os.row == 6 ? std::vector<HudHint>{ { { "buttons/button_dpad" }, "Choose" }, { { accept }, "Select" }, { { back }, "Quit" } }
                                        : std::vector<HudHint>{ { { "buttons/button_dpad" }, "Change" }, { { back }, "Quit" } });
                        overlayUsed = true;
                    }
                    break;
                }
        optionsShown = optionsNow;
        // The naming screen's keyboard redrawn (hudnaming.h): while typing (steps 1, 2), and
        // while it fades out only if it was shown before.
        static bool namingShown = false;
        bool namingNow = false;
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && appRunning("NamingScreen"))
            for (unsigned i = 0; i < rc_num_apps; i++)
                if (!strcmp(rc_apps[i].name, "NamingScreen"))
                {
                    static uint32_t namingAt = 0;
                    HudNaming::State ns;
                    if (HudNaming::ReadState(readRam, rc_apps[i].initAddr, rc_apps[i].execAddr, rc_apps[i].exitAddr, namingAt, ns) &&
                        (ns.step == 1 || ns.step == 2 || (ns.step == 3 && namingShown)))
                    {
                        namingNow = true;
                        const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                        HudNaming::Draw(canvas, ui, sprites, font, ns.type == 1 ? romArt.PokemonIcon(ns.species, ns.form) : nullptr, ns,
                            { { { "buttons/button_dpad" }, "Move" }, { { accept }, "Type" }, { { back }, "Delete" },
                                { { PadGlyph(PAD_BACK) }, "Next page" }, { { PadGlyph(PAD_START) }, "To OK" } });
                        overlayUsed = true;
                    }
                    break;
                }
        namingShown = namingNow;
        // The Union Room's trade screen redrawn (hudtrade.h), mirroring the game's own; for
        // up to three frames that do not check out the last picture stays, as with the bag.
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && appRunning("WirelessTradeSelectMon"))
            for (unsigned i = 0; i < rc_num_apps; i++)
                if (!strcmp(rc_apps[i].name, "WirelessTradeSelectMon"))
                {
                    static uint32_t tradeAt = 0, tradeGood = 0;
                    static HudTrade::View tradeView;
                    HudTrade::View tv;
                    if (HudTrade::Read({ nds->MainRAM, nds->MainRAMMask + 1 }, tradeAt, rc_apps[i].initAddr, rc_apps[i].execAddr, rc_apps[i].exitAddr, tv))
                        tradeView = tv, tradeGood = frame;
                    if (frame - tradeGood > 3) break;
                    const HudTrade::View& v = tradeView;
                    const std::string a = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), b = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                    std::vector<HudHint> hints;
                    if (v.step == HudTrade::View::Browse) hints = { { { "buttons/button_dpad" }, "Choose" }, { { a }, "Select" }, { { b }, "Cancel" } };
                    else if (v.step == HudTrade::View::Menu || v.yesNo >= 0) hints = { { { "buttons/button_dpad" }, "Choose" }, { { a }, "Select" }, { { b }, "Back" } };
                    else if (v.step == HudTrade::View::Notice) hints = { { { a }, "OK" } };
                    // The other trainer's colour, when they are one of the party's players.
                    int theirs = 1;
                    for (int p = 0; p < 4; p++)
                        if (p != Party_Me() && Party_Peers()[p].present && Party_Peers()[p].name == v.partner) theirs = Party_Peers()[p].colour;
                    HudTrade::Draw(canvas, ui, sprites, font, v, cfg.onlineColour >= 0 ? cfg.onlineColour : std::max(0, Party_Me()), theirs,
                        [&](const PartyMon& m) { return romArt.PokemonIcon(m.species, m.form, m.egg); }, hints);
                    overlayUsed = true;
                    break;
                }
        // The party menu and the summary redrawn (hudparty.h), mirroring the game's own
        // applications, which keep the input; on any step they do not draw, the game shows.
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen)
        {
            static uint32_t partyAt = 0, summaryAt = 0, partyGood = 0, summaryGood = 0;
            const HudParty::Ram ram{ nds->MainRAM, nds->MainRAMMask + 1 };
            auto app = [](const char* name) -> const RcApp* {
                for (unsigned i = 0; i < rc_num_apps; i++)
                    if (!strcmp(rc_apps[i].name, name)) return &rc_apps[i];
                return nullptr;
            };
            const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
            static HudParty::PartyScreen partyView;
            static HudParty::SummaryScreen summaryView;
            const HudParty::MonIconFn partyIcon = [&](const PartyMon& m) { return romArt.PokemonIcon(m.species, m.form, m.egg); };
            const RcApp *pa = app("PartyMenu"), *sa = app("PokemonSummary");
            HudParty::PartyScreen pv;
            HudParty::SummaryScreen sv;
            if (pa && appRunning("PartyMenu") && HudParty::ReadPartyScreen(ram, partyAt, pa->initAddr, pa->execAddr, pa->exitAddr, pv))
                partyView = pv, partyGood = frame;
            if (sa && appRunning("PokemonSummary") && HudParty::ReadSummaryScreen(ram, summaryAt, sa->initAddr, sa->execAddr, sa->exitAddr, sv))
                summaryView = sv, summaryGood = frame;
            if (pa && appRunning("PartyMenu") && frame - partyGood <= 3)
            {
                std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Choose" },
                    { { accept }, partyView.items.empty() && partyView.context == 0 ? "Options" : "Select" }, { { back }, "Back" } };
                if (!partyView.message.empty()) hints = { { { accept }, "Next" } };
                if (partyView.yesNo >= 0) hints = { { { "buttons/button_dpad" }, "Choose" }, { { accept }, "Select" }, { { back }, "No" } };
                HudParty::DrawParty(canvas, ui, sprites, font, partyView, partyIcon, hints);
                overlayUsed = true;
            }
            else if (sa && appRunning("PokemonSummary") && frame - summaryGood <= 3)
            {
                std::vector<HudHint> hints; // an egg has one page
                if (!summaryView.mon.egg) hints.push_back({ { "buttons/button_dpad_left", "buttons/button_dpad_right" }, "Page" });
                if (summaryView.count > 1) hints.push_back({ { "buttons/button_dpad_up", "buttons/button_dpad_down" }, "Next Pok\xC3\xA9mon" });
                hints.push_back({ { back }, "Back" });
                HudParty::DrawSummary(canvas, ui, sprites, font, summaryView, partyIcon, hints);
                overlayUsed = true;
            }
        }
        // The Pokedex redrawn (huddex.h): its front page and its list, mirrored from the game's
        // application; its details and search are the game's own picture.
        static int dexShown = 0; // what was drawn the frame before: 0 nothing, 1 the front page, 2 the list, 3 the details
        int dexNow = 0;
        if (spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen && appRunning("Pokedex"))
            for (unsigned i = 0; i < rc_num_apps; i++)
                if (!strcmp(rc_apps[i].name, "Pokedex"))
                {
                    static uint32_t dexAt = 0;
                    HudDex::View dv;
                    // The details (huddexinfo.h), drawn as 3; while they fade or flash a
                    // button, the page drawn last stays.
                    static HudDexInfo::View infoLast;
                    HudDexInfo::View iv;
                    const int info = HudDexInfo::Read({ nds->MainRAM, nds->MainRAMMask + 1 }, dexAt, rc_apps[i].initAddr, rc_apps[i].execAddr,
                        rc_apps[i].exitAddr, HudDex::JohtoNumbers(rom.data(), rom.size()), iv);
                    if (info == 1 || (info < 0 && dexShown == 3))
                    {
                        if (info == 1) infoLast = iv;
                        const HudDexInfo::View& v = infoLast;
                        const std::string a = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), b = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                        std::vector<HudHint> hints = { { { "buttons/button_dpad_left", "buttons/button_dpad_right" }, "Page" } };
                        if (v.page == HudDexInfo::Area && !v.places.empty()) hints.push_back({ { "buttons/button_dpad_up", "buttons/button_dpad_down" }, "Place" });
                        if (v.page == HudDexInfo::Area) hints.push_back({ { a }, "Time of day" });
                        if (v.page == HudDexInfo::Forms && v.forms.size() > 1)
                        {
                            hints.push_back({ { "buttons/button_dpad_up", "buttons/button_dpad_down" }, "Choose" });
                            hints.push_back({ { a }, "Compare" });
                        }
                        if (v.page == HudDexInfo::Back) hints.push_back({ { a }, "Back to the list" });
                        hints.push_back({ { b }, "Back" });
                        HudDexInfo::Draw(canvas, ui, sprites, font, romArt, rom.data(), rom.size(), v, trainerArt.px.empty() ? nullptr : &trainerArt,
                            Party_MyCard().gender, hints);
                        dexNow = 3;
                        overlayUsed = true;
                    }
                    else if (HudDex::Read({ nds->MainRAM, nds->MainRAMMask + 1 }, dexAt, rc_apps[i].initAddr, rc_apps[i].execAddr, rc_apps[i].exitAddr,
                            HudDex::JohtoNumbers(rom.data(), rom.size()), dexShown == 3 ? 0 : dexShown, dv))
                    {
                        const std::string a = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), b = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
                        const std::string x = PadGlyph(cfg.byLabel ? PAD_X : PAD_Y), y = PadGlyph(cfg.byLabel ? PAD_Y : PAD_X);
                        if (dv.list)
                        {
                            std::vector<HudHint> hints = { { { PadGlyph(PAD_LB), PadGlyph(PAD_RB) }, "Page" }, { { "buttons/button_dpad" }, "Choose" } };
                            if (dv.places[dv.pos].species) hints.push_back({ { a }, "Details" });
                            hints.push_back({ { x }, "Cry" });
                            hints.push_back({ { y }, "Search" });
                            hints.push_back({ { b }, "Close" });
                            HudDex::DrawList(canvas, ui, sprites, font, romArt, rom.data(), rom.size(), dv, hints);
                        }
                        else
                        {
                            std::vector<HudHint> hints;
                            if (dv.canSwitch) hints.push_back({ { "buttons/button_dpad_left", "buttons/button_dpad_right" }, "Pok\xC3\xA9" "dex" });
                            hints.push_back({ { a }, "Open" });
                            hints.push_back({ { y }, "Search" });
                            hints.push_back({ { b }, "Close" });
                            HudDex::DrawFront(canvas, ui, sprites, font, romArt, dv, hints);
                        }
                        dexNow = dv.list ? 2 : 1;
                        overlayUsed = true;
                    }
                    break;
                }
        dexShown = dexNow;
        // The PC's Pokemon storage redrawn (hudpcbox.h), mirroring the game's application;
        // its messages, questions and the screens it does not draw are the game's own. Its
        // short animations are drawn when it was drawn just before (some frames skip this).
        static uint32_t pcShownAt = 0, pcLiveAt = 0, pcTabsAt = 0;
        static HudPcBox::Screen pc;
        static float pcSlide = -1; // how far the game's panels have slid (0-1), or -1
        static uint32_t pcSlideTo = 0;
        static int pcGlideFrom = -1;
        static float pcGlide = -1;
        static HudPcBox::Screen pcScrollFrom;
        static int pcScrollDir = 0;
        static uint32_t pcScrollAt = 0, pcScrollStart = 0;
        const bool pcMay = spritesOk && cfg.redrawn && screen == SCR_NONE && !menu.open && !startOpen;
        const bool pcHold = pcMay && pcShownAt && pcShownAt == frame - 1;
        bool pcNow = false;
        if (pcMay && appRunning("PCBox"))
        {
            for (unsigned i = 0; i < rc_num_apps; i++)
                if (!strcmp(rc_apps[i].name, "PCBox"))
                {
                    static uint32_t pcAt = 0;
                    HudPcBox::Screen now;
                    const bool read = HudPcBox::Read({ nds->MainRAM, nds->MainRAMMask + 1 }, pcAt, rc_apps[i].initAddr,
                        rc_apps[i].execAddr, rc_apps[i].exitAddr, now);
                    // Coming back from the boxes' tabs (the game's picture) counts as shown before.
                    // The fade in (step 2 after loading at 11, or at 15 back from a summary; step 2 is
                    // the fade out too) is drawn at once rather than the game's picture fading in.
                    static bool pcOpening = false;
                    if (read) pcOpening = now.step == 11 || now.step == 15 || (pcOpening && now.step == 2);
                    static uint32_t pcMsgAt = 0;
                    if (read && (now.step == 6 || now.step == 7)) pcMsgAt = frame;
                    pcNow = read && (HudPcBox::Shown(now, (pcShownAt && frame - pcShownAt <= 3) || (pcTabsAt && frame - pcTabsAt <= 3),
                                         pcMsgAt ? frame - pcMsgAt : ~0u) ||
                                        (pcOpening && now.step == 2 && !HudPcBox::PastTabs(now)));
                    if (read && HudPcBox::PastTabs(now)) pcTabsAt = frame;
                    // A button that changes the screen (hudpcbox.h SlideOf) starts the game's
                    // slide on the next step 5, still on the old screen's d-pad table; it ends
                    // when the table changes. Leaving the party moving goes back where it began.
                    static uint32_t pcSlideGrid = 0, pcMovedFrom = HudPcBox::kGridMove, pcLastGrid = 0;
                    static int pcSlideLen = 0;
                    static uint32_t pcSlideAt = 0;
                    pcSlide = -1;
                    if (read)
                    {
                        uint32_t to;
                        int len;
                        if (HudPcBox::SlideOf(now.step, to, len))
                            pcSlideGrid = now.grid, pcSlideTo = to ? to : pcMovedFrom, pcSlideLen = len, pcSlideAt = 0;
                        else if (now.step == 5 && pcSlideGrid && now.grid == pcSlideGrid)
                        {
                            if (!pcSlideAt) pcSlideAt = frame;
                            pcSlide = std::min(1.0f, (float)(frame - pcSlideAt) / pcSlideLen);
                        }
                        else pcSlideGrid = 0;
                        if (now.grid == HudPcBox::kGridMoveParty && pcLastGrid != now.grid && pcLastGrid) pcMovedFrom = pcLastGrid;
                        pcLastGrid = now.grid;
                    }
                    // Withdraw sends the Pokemon to the party once the party's panel is out (step 5
                    // going on to 85): it glides there over the 9 frames the game takes.
                    static uint32_t pcGlideAt = 0;
                    if (read && now.step == 5 && now.next == 85)
                    {
                        if (!pcGlideAt) pcGlideAt = frame, pcGlideFrom = now.chosen;
                        pcGlide = (frame - pcGlideAt) / 9.0f;
                    }
                    else if (read) pcGlideAt = 0, pcGlide = -1;
                    // Left or right on the box's name: the game scrolls the old box out and the new
                    // one in over the 24 frames of the step 5 that follows.
                    if (pcNow && pcLiveAt && frame - pcLiveAt <= 5 && now.box != pc.box && now.mode == pc.mode)
                    {
                        const int d = (now.box - pc.box + 18) % 18;
                        pcScrollDir = d == 1 ? 1 : d == 17 ? -1 : 0;
                        pcScrollFrom = pc, pcScrollAt = frame, pcScrollStart = 0;
                    }
                    if (pcScrollDir && read && now.step == 5 && !pcScrollStart) pcScrollStart = frame;
                    if (pcNow) pc = now, pcLiveAt = frame;
                    // A Pokemon being put down is copied over a frame: the storage reads
                    // half-written for that frame and the last screen stands in for it.
                    else if (!read && pcHold && frame - pcLiveAt <= 2) pcNow = true;
                    break;
                }
        }
        // While the game is busy for a few frames (loading the menu after A on a Pokemon) the
        // application's per-frame function does not run and its picture stands still: the last
        // redrawn screen stands in for it rather than the game's own flashing up.
        else if (pcHold && frame - pcLiveAt <= 30)
            pcNow = true;
        // How far the new box still is from its place: still until the step 5 begins (or a few
        // frames without one), then steadily, as the game moves it.
        float pcScroll = 0;
        if (pcScrollDir)
        {
            const float p = pcScrollStart ? (frame - pcScrollStart) / 24.0f : frame - pcScrollAt > 6 ? 1.0f : 0.0f;
            if (p >= 1) pcScrollDir = 0;
            else pcScroll = pcScrollDir * (1 - p);
        }
        if (pcNow)
        {
            auto ds = [&](int k) { return PadGlyph(PadButtonFor(k, cfg.byLabel, cfg.padMap)); };
            std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Choose" }, { { ds(0) }, pc.held >= 0 ? "Put down" : "Select" },
                { { ds(1) }, "Back" } };
            if (pc.yesNo >= 0) hints = { { { "buttons/button_dpad" }, "Choose" }, { { ds(0) }, "Select" }, { { ds(1) }, "No" } };
            else if (!pc.message.empty()) hints = { { { ds(0) }, "Next" } };
            const HudOnline::IconFn itemIcon = [&](int item) { return romArt.ItemIcon(item); };
            HudPcBox::Draw(canvas, ui, sprites, font, pc, monIcon, itemIcon, trainerName(), Party_MyCard().id, hints,
                pcSlide, pcSlideTo, pcGlideFrom, pcGlide, &pcScrollFrom, pcScroll);
            overlayUsed = true;
            pcShownAt = frame;
        }
        if (spritesOk && (bootMode != BOOT_NONE || frame < resumeCoverUntil) && !menu.open && screen == SCR_NONE)
        {
            HudOnline::LoadingScreen(canvas, ui, sprites, font, logo.px.empty() ? nullptr : &logo, "POK\xC3\xA9MON",
                bootMode != BOOT_NEW ? "Loading your adventure" : "Starting a new adventure", frame);
            overlayUsed = true;
        }
        if (spritesOk && (screen != SCR_NONE || (startOpen && !menu.open)))
        {
            const std::string accept = PadGlyph(cfg.byLabel ? PAD_A : PAD_B), back = PadGlyph(cfg.byLabel ? PAD_B : PAD_A);
            if (screen == SCR_SESSION)
            {
                buildSession();
                HudOnline::SessionScreen(canvas, ui, sprites, font, sessionMenu,
                    { { { "buttons/button_dpad" }, "Change" }, { { accept }, "Select" }, { { back }, "Back" } }, monIcon);
            }
            else if (screen == SCR_FRIENDS)
            {
                buildFriends();
                std::vector<HudHint> hints = { { { "buttons/button_l", "buttons/button_r" }, "Tab" } };
                if (!friendRows.empty())
                {
                    hints.push_back({ { "buttons/button_dpad" }, friendsMenu.tab == 0 ? "Battle, trade or friend" : friendsMenu.tab == 1 ? "Invite or join" : "Accept or decline" });
                    hints.push_back({ { accept }, friendsMenu.tab == 2 ? "Answer" : "Choose" });
                }
                hints.push_back({ { back }, "Close" });
                HudOnline::FriendsPanel(canvas, ui, sprites, font, friendsMenu, friendRows, hints);
            }
            else if (screen == SCR_INVITE && (inviteFrom >= 0 || inviteKind == PI_SESSION))
            {
                const float left = 20 - (Party_Now() - inviteAt) / 1000.0f;
                HudOnline::InviteCard(canvas, ui, sprites, font, inviteMenu, inviteKind == PI_SESSION ? inviteWho : Party_Peers()[inviteFrom], inviteKind,
                    left / 20, left,
                    { { { accept }, "Choose" }, { { back }, "Not now" } });
            }
            else if (screen == SCR_SUMMARY)
                HudOnline::Summary(canvas, ui, sprites, font, { { { accept }, "Leave" }, { { back }, "Stay" } }, sessionStats, monIcon);
            else if (screen == SCR_EDIT)
            {
                const std::vector<HudHint> hints = { { { "buttons/button_dpad" }, "Letters" }, { { PadGlyph(cfg.byLabel ? PAD_X : PAD_Y) }, "Delete" },
                    { { accept }, edit.purpose == E_CODE ? "Join" : "Done" }, { { back }, "Cancel" } };
                if (edit.purpose == E_CODE)
                {
                    FillBlend(canvas, 0, 0, (float)canvas.w, (float)canvas.h, 0x80000000);
                    HudOnline::CodeEntry(canvas, ui, sprites, font, edit.text, edit.cursor, "Join with a code",
                        "Up and down change the letter, or type it. No O/0 or I/1.", ui.W / ui.s / 2 - 320, 330);
                    DrawHintBar(canvas, ui, sprites, font, hints);
                }
                else
                    HudOnline::TextEntry(canvas, ui, sprites, font, edit.purpose == E_NAME ? "Nickname" : "Party server", edit.text, edit.cursor,
                        edit.purpose == E_NAME ? "Up and down change the letter, or type it." : "Its address, and :port if not 7373. Up and down change the letter, or type it.",
                        hints);
            }
            else
            {
                if (titleOpen)
                {
                    intro.Draw(canvas, (SDL_GetTicks() - titleSince) / 1000.0);
                    float lx, ly, lw;
                    intro.LogoPlace(canvas.w, canvas.h, lx, ly, lw);
                    if (!logo.px.empty())
                    {
                        const float lh = lw * logo.h / logo.w;
                        // Faded in with the video's own fade from white (its first second).
                        const float fade = std::clamp(((SDL_GetTicks() - titleSince) / 1000.0f - 0.2f) / 0.9f, 0.0f, 1.0f);
                        BlitShadow(canvas, logo, lx - lw / 2, ly - lh / 2, lw, lh, (uint32_t)(0x33 * fade) << 24 | 0x64646F);
                        Blit(canvas, logo, 0, 0, (float)logo.w, (float)logo.h, lx - lw / 2, ly - lh / 2, lw, lh, true,
                            (uint32_t)(255 * fade) << 24 | 0xFFFFFF);
                    }
                }
                else
                {
                std::vector<HudOnline::StartItem> items;
                for (int act : startActions)
                    items.push_back(act == ST_CONTINUE ? HudOnline::StartItem { "CONTINUE", "menu/menu_resume" }
                        : act == ST_NEW ? HudOnline::StartItem { "NEW GAME", "menu/menu_states" }
                        : act == ST_ONLINE ? HudOnline::StartItem { "ONLINE", "menu/menu_online" }
                        : act == ST_SETTINGS ? HudOnline::StartItem { "SETTINGS", "menu/menu_options" }
                        : HudOnline::StartItem { "QUIT", "menu/menu_quit" });
                // The save: the trainer (boy or girl) and the first Pokemon, from the ROM.
                static Image saveTrainer;
                static bool saveArt = false;
                const PartyCard saveCard = Party_SaveCard();
                if (!saveArt && saveCard.known)
                {
                    saveArt = true;
                    worldBlocks.RomSpriteImage(saveCard.gender ? "heroine" : "hero", 1, saveTrainer);
                }
                auto drawSave = [&](float y) {
                    if (!saveCard.known) return;
                    const Image* partner = romArt.PokemonIcon(cfg.onlinePartner ? cfg.onlinePartner : saveCard.party.empty() ? 0 : saveCard.party[0]);
                    HudOnline::SaveCard(canvas, ui, sprites, font, ui.W / ui.s / 2 - 700, y, 1400, onlineName(), saveCard,
                        saveTrainer.px.empty() ? nullptr : &saveTrainer, partner,
                        haveResume && cfg.autosave ? Autosave::ResumeLine(resumeInfo.place, resumeInfo.time) : "");
                };
                const PartyLink link = Party_Link();
                std::string footer = onlineName() + (link == PartyLink::Online ? " \xC2\xB7 online, " + Party_Code() : link == PartyLink::Connecting ? " \xC2\xB7 joining..." : "");
                // HeartGold's game codes are IPK*, SoulSilver's IPG*.
                const bool heartGold = rom.size() > 0x10 && rom[0x0C] == 'I' && rom[0x0D] == 'P' && rom[0x0E] == 'K';
                HudOnline::StartScreen(canvas, ui, sprites, font, items, startSel, logo.px.empty() ? nullptr : &logo,
                    heartGold ? "POK\xC3\xA9MON HEARTGOLD" : "POK\xC3\xA9MON SOULSILVER", "",
                    { { { accept }, "Select" } }, footer, drawSave);
                }
            }
            overlayUsed = true;
        }

        bool wantShot = !opt.shotDir.empty() &&
            (opt.shotAt.count(frame) || g_sceneShots.count(frame) || (opt.shotEvery && frame % opt.shotEvery == 0));
        if (wantShot && opt.shotLayers)
        {
            // BG0 red, BG1 green, BG2 blue, BG3 yellow, backdrop black, 3D magenta, sprites white.
            auto& r2d = static_cast<GPU2D::SoftRenderer&>(nds->GPU.GetRenderer2D());
            int topUnit = (nds->PowerControl9 & (1 << 15)) ? 0 : 1;
            std::vector<uint32_t> img(256 * 384);
            for (int scr = 0; scr < 2; scr++)
            {
                const u8* flags = r2d.LayerFlags[scr == 0 ? topUnit : 1 - topUnit];
                for (int i = 0; i < 256 * 192; i++)
                {
                    u8 f = flags[i];
                    uint32_t c = 0x000000;
                    switch (LayerClass(f))
                    {
                    case LAYER_OBJ: c = 0xFFFFFF; break;
                    case LAYER_3D: c = 0xFF00FF; break;
                    case LAYER_BG0: c = 0xFF0000; break;
                    case LAYER_BG1: c = 0x00C000; break;
                    case LAYER_BG2: c = 0x0000FF; break;
                    case LAYER_BG3: c = 0xFFFF00; break;
                    default: break;
                    }
                    img[scr * 256 * 192 + i] = c;
                }
            }
            for (int scr = 0; scr < 2; scr++)
            {
                const u8* flags = r2d.LayerFlags[scr == 0 ? topUnit : 1 - topUnit];
                int hist[256] = {};
                for (int i = 0; i < 256 * 192; i++) hist[flags[i]]++;
                fprintf(stderr, "[layers] frame %u %s:", frame, scr == 0 ? "top" : "bottom");
                for (int v = 0; v < 256; v++)
                    if (hist[v]) fprintf(stderr, " %02x=%d", v, hist[v]);
                fprintf(stderr, "\n");
            }
            char lname[64];
            snprintf(lname, sizeof(lname), "/layers_%06u.png", frame);
            WritePNG(opt.shotDir + lname, img.data(), 256, 384);
        }
        // The Wi-Fi Plaza's room is drawn only by the widescreen renderer: in the layouts with
        // both screens the top screen shown is its picture (the DS's own view) with the game's
        // 2D over it, put together as the single-screen view does it.
        static std::vector<uint32_t> plazaTop;
        if (!merged && onPlaza && melonDS::Wide.Owner && top)
        {
            takeWideImage();
            if (wideImage.pixels)
            {
                Scene plaza;
                plaza.wide = true;
                plaza.items = { FullScreen(0) };
                plaza.items[0].drop |= LAYER_BACKDROP;
                MergedCompose(plaza, screenImages, 256, 192, plazaTop, false, &wideImage);
                top = plazaTop.data();
            }
        }
        if (wantShot && top && bot)
        {
            char name[64];
            snprintf(name, sizeof(name), "/frame_%06u.png", frame);
            if (opt.composeW > 0 && opt.composeH > 0)
            {
                if (merged) { takeWideImage(); MergedCompose(scene, screenImages, opt.composeW, opt.composeH, shot, cfg.smooth, &wideImage); }
                else Compose(layout, opt.composeW, opt.composeH, top, bot, shot);
                BlendCanvas(canvas, shot);
                WritePNG(opt.shotDir + name, shot.data(), opt.composeW, opt.composeH);
            }
            else
            {
                shot.resize(256 * 384);
                memcpy(shot.data(), top, 256 * 192 * 4);
                memcpy(shot.data() + 256 * 192, bot, 256 * 192 * 4);
                WritePNG(opt.shotDir + name, shot.data(), 256, 384);
            }
        }

        perfMark(2);
        if (opt.headless && opt.benchCompose && merged && top && bot && opt.composeW)
        {
            takeWideImage();
            MergedCompose(scene, screenImages, opt.composeW, opt.composeH, mergedImage, cfg.smooth, &wideImage);
            perfMark(3);
        }

        if (!opt.headless && top && bot)
        {
            // Puts the picture on the screen; `extra`: again, with the world drawn at a later
            // moment between the game's frames (Frame rate: Unlimited).
            auto drawFrame = [&](bool extra) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
                if (merged)
                {
                    // The graphics card puts the pieces together (gpucompose.h). SSR_CPU_COMPOSE=1
                    // uses the compositor of the headless tests instead, for comparing the two.
                    takeWideImage();
                    if (!cpuCompose)
                    {
                        if (!gpuCompose) gpuCompose = new GpuCompositor(renderer);
                        const SDL_Rect area = { mergedX, 0, mergedW, outH };
                        SDL_RenderSetViewport(renderer, &area);
                        gpuCompose->Draw(scene, screenImages, mergedW, outH, cfg.smooth, &wideImage);
                        SDL_RenderSetViewport(renderer, nullptr);
                        perfMark(3);
                    }
                    else
                    {
                        int cw = mergedW, ch = outH;
                        if (cfg.smooth && outH > 768)
                        {
                            cw = mergedW * 768 / outH;
                            ch = 768;
                        }
                        MergedCompose(scene, screenImages, cw, ch, mergedImage, cfg.smooth, &wideImage);
                        perfMark(3);
                        if (!mergedTex || mergedTexW != cw || mergedTexH != ch)
                        {
                            if (mergedTex) SDL_DestroyTexture(mergedTex);
                            mergedTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, cw, ch);
                            mergedTexW = cw;
                            mergedTexH = ch;
                        }
                        SDL_SetTextureScaleMode(mergedTex, cfg.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
                        SDL_UpdateTexture(mergedTex, nullptr, mergedImage.data(), cw * 4);
                        const SDL_Rect area = { mergedX, 0, mergedW, outH };
                        SDL_RenderCopy(renderer, mergedTex, nullptr, &area);
                    }
                    // The bars beside a view held to a narrower shape (clearing ignores the area).
                    if (mergedX > 0)
                    {
                        const SDL_Rect bars[2] = { { 0, 0, mergedX, outH }, { mergedX + mergedW, 0, outW - mergedX - mergedW, outH } };
                        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        SDL_RenderFillRects(renderer, bars, 2);
                    }
                }
                else
                {
                    SDL_UpdateTexture(tex[0], nullptr, top, 256 * 4);
                    SDL_UpdateTexture(tex[1], nullptr, bot, 256 * 4);
                    int first = LayoutDrawFirst(layout);
                    for (int n = 0; n < 2; n++)
                    {
                        int i = n == 0 ? first : 1 - first;
                        if (rects[i].w > 0) SDL_RenderCopy(renderer, tex[i], nullptr, &rects[i]);
                    }
                }
                // A canvas over the picture: only the part drawn into is sent and shown.
                auto showCanvas = [&](const Canvas& cv, SDL_Texture*& t) {
                    if (!cv.Dirty()) return;
                    int tw = 0, th = 0;
                    if (t) SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                    if (!t || tw != outW || th != outH)
                    {
                        if (t) SDL_DestroyTexture(t);
                        t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, outW, outH);
                        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                    }
                    const SDL_Rect dr = { cv.x0d, cv.y0d, cv.x1d - cv.x0d, cv.y1d - cv.y0d };
                    SDL_UpdateTexture(t, &dr, cv.px.data() + (size_t)dr.y * outW + dr.x, outW * 4);
                    SDL_RenderCopy(renderer, t, &dr, &dr);
                };
                showCanvas(tagCanvas, tagTex);
                if (overlayUsed) showCanvas(canvas, overlayTex);
                // In a window, screenshots are read back from the renderer: exactly what is shown.
                // (The pictures between the game's frames as _01, _02 and so on.)
                static int between = 0;
                between = extra ? between + 1 : 0;
                if (wantShot)
                {
                    std::vector<uint32_t> back((size_t)outW * outH);
                    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, back.data(), outW * 4) == 0)
                    {
                        char name[64];
                        if (extra) snprintf(name, sizeof(name), "/window_%06u_%02d.png", frame, between);
                        else snprintf(name, sizeof(name), "/window_%06u.png", frame);
                        WritePNG(opt.shotDir + name, back.data(), outW, outH);
                    }
                }
                if (touchPadUsed && touchPad.visible)
                {
                    // Lit by what the fingers hold (by label, as drawn).
                    bool held[PAD_COUNT] = {};
                    bool menuHeld = false;
                    touchPad.Apply(held, menuHeld);
                    touchPad.Draw(renderer, held, menuHeld);
                }
                SDL_RenderPresent(renderer);
            };
            drawFrame(false);
            perfMark(4);
            // Frames shown in the last whole second, for the FPS counter.
            presented++;
            const uint32_t ms = SDL_GetTicks();
            if (ms - fpsSince >= 1000)
            {
                shownFps = (int)(presented * 1000 / (ms - fpsSince));
                static const bool fpsLog = getenv("SSR_FPS_LOG") != nullptr;
                if (fpsLog) fprintf(stderr, "[fps] %d pictures a second, game frame %u\n", shownFps, frame);
                presented = 0;
                fpsSince = ms;
            }

            nextTick += (uint64_t)(frameTime * perfFreq);
            uint64_t now = SDL_GetPerformanceCounter();
            // For tests: SSR_BETWEEN_SHOTS=n draws n of them on frames with a screenshot,
            // however long that takes (saving one leaves no time for any).
            static const int forceShots = getenv("SSR_BETWEEN_SHOTS") ? atoi(getenv("SSR_BETWEEN_SHOTS")) : 0;
            int forced = wantShot ? forceShots : 0;
            if (cfg.fpsUnlimited && merged && melonDS::Wide.Owner && (now < nextTick || forced))
            {
                // Frame rate Unlimited: until the game's next frame is due, the world is drawn
                // again at later moments between this picture and the next and shown, as often
                // as there is time for. The game itself keeps to its own pace.
                const uint64_t from = now, span = (uint64_t)(frameTime * perfFreq);
                // What one costs (an average, kept from frame to frame, that drifts down while
                // none fits so that a slow one does not stop them for good) and a millisecond to
                // spare: the game's next frame must not start late.
                static uint64_t cost = 0;
                const uint64_t spare = perfFreq / 1000;
                for (;;)
                {
                    const uint64_t t0 = SDL_GetPerformanceCounter();
                    float at;
                    if (forced > 0)
                        at = (float)(forceShots - forced-- + 1) / (forceShots + 1); // evenly spaced
                    else if (t0 + cost + spare >= nextTick)
                    {
                        cost -= cost / 32;
                        break;
                    }
                    else
                        at = (float)((double)(t0 + cost - from) / span); // where it is when it is on the screen
                    if (!melonDS::WideDrawBetween(nds->GPU, at)) break;
                    updateShownView();
                    tagCanvas.Clear();
                    drawWorldTags(tagCanvas);
                    composingBetween = true;
                    drawFrame(true);
                    composingBetween = false;
                    presented++;
                    const uint64_t took = SDL_GetPerformanceCounter() - t0;
                    cost = cost ? (cost * 3 + took) / 4 : took;
                }
                now = SDL_GetPerformanceCounter();
            }
            if (now < nextTick)
            {
                // Sleep for most of the wait and spin for the last part of a millisecond:
                // frames then start within a few microseconds of an even 16.7 ms apart.
                double left = (double)(nextTick - now) / perfFreq;
                if (left > 0.0008) std::this_thread::sleep_for(std::chrono::duration<double>(left - 0.0006));
                while (SDL_GetPerformanceCounter() < nextTick) {}
            }
            else if (now - nextTick > perfFreq / 4) nextTick = now;
            perfMark(5);
        }

        if (!opt.stateSave.empty() && scriptClock == opt.stateSaveAt && !holdScript)
        {
            if (!WriteSnapshot(*nds, opt.stateSave, scriptClock)) fprintf(stderr, "snapshot failed\n");
        }

        if (!opt.noSave && frame % 120 == 0) FlushSave(opt.save);
        if (opt.frames && scriptClock >= opt.frames) running = false;
        if (opt.stopAt && frame >= opt.stopAt)
        {
            if (!opt.stateSave.empty() && !WriteSnapshot(*nds, opt.stateSave, frame)) fprintf(stderr, "snapshot failed\n");
            running = false;
        }
    }

    Sampler::Stop();
    // The 3D render thread must be finished before anything it draws from is torn down.
    static_cast<melonDS::SoftRenderer&>(nds->GPU.GetRenderer3D()).SetThreaded(false, nds->GPU);
    melonDS::Wide.Owner = nullptr;
    melonDS::Wide.Extra = nullptr;
    if (!opt.headless)
    {
        cfg.layout = LayoutName(layout.mode);
        cfg.focusMode = focus.mode;
        cfg.Save(opt.config);
    }
    if (nds->GPU.GPU3D.InterpolationEnabled)
        fprintf(stderr, "[60fps] %u in-between frames drawn in %u frames\n", nds->GPU.GPU3D.InterpolatedFrames, frame);
    if (opt.countChanges) fprintf(stderr, "[changes] top screen changed on %u of %u frames\n", TopChanges, frame);
    if (opt.audioStats && AudioSamples)
        fprintf(stderr, "[audio] %llu samples, peak %d, rms %.1f\n", (unsigned long long)AudioSamples, AudioPeak,
            sqrt(AudioSumSq / AudioSamples));
    if (!opt.noSave) FlushSave(opt.save);
    if (autosaveOk && cfg.autosave && played && !opt.noSave)
    {
        // Where the player is, for CONTINUE (autosave.h): whatever is going on, a battle too.
        int m = -1, x, z;
        if (!playerPos(m, x, z)) m = -1;
        if (resume.Write(*nds, m, Party_PlaceName(m))) fprintf(stderr, "[autosave] resume point written\n");
        resume.Finish();
    }
    Party_Leave();
    Party_StopLocalServer();
    if (!opt.headless) SDL_Quit();
    if (nds->ARM9.RecompEnable) rc_print_stats();
    if (opt.lockstep)
    {
        if (TraceFailed) return 1;
        fprintf(stderr, "[lockstep] PASS: %u frames, every ARM9 instruction matched the interpreter\n", frame);
    }
    return 0;
}
