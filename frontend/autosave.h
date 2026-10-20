#pragma once
// Autosave (the Game tab's "Autosave" setting), in two parts:
//
// Resume points: every few minutes on the overworld, after each save the game writes and
// when the game is quit, a snapshot of the whole console is written beside the save file
// (<save>.resume, the one before kept as <save>.resume.bak). CONTINUE on the start screen
// starts from it instead of booting the game. Each one records a hash of the save memory
// it holds, and it is only used while that equals the save memory the console has now:
// loading it then writes the same save back, so an in-game save made after it (or a save
// file swapped in) can never be rolled back. Otherwise the game boots as without one.
//
// Game saves: the game's own save, run at calm moments (entering a Pokemon Center, after
// a badge) through its X menu with injected presses: X, the D-pad to SAVE (read off the
// menu's cursor and button list in memory), A; the yes/no prompts are answered by the
// save screen code in main.cpp, then B closes the menu. The player's input is held back
// meanwhile and only a small "Saving..." pill shows.
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include "NDS.h"
#include "Savestate.h"
#include "hud.h"
#include "hudmenu.h"

// stb_image v2.30 (public domain), github.com/nothings/stb at 013ac3beddff3dbffafd5177e7972067cd2b5083,
// for the GIF only.
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "thirdparty/stb_image.h"

namespace Autosave
{

inline uint64_t Hash(const uint8_t* d, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 0x100000001b3ull;
    return h;
}

// What a resume point file starts with; the core's savestate follows.
struct ResumeHeader
{
    char magic[8] = { 'S', 'S', 'R', 'R', 'E', 'S', '0', '2' };
    uint64_t saveHash = 0; // of the save memory inside the snapshot
    int64_t time = 0;      // Unix time it was made
    int32_t map = -1;      // where the player was
    uint32_t saveLen = 0;
    char place[48] = {};   // its name, for the start screen
};

class Resume
{
public:
    std::string path; // <save>.resume

    ~Resume() { Finish(); }

    // Snapshots the console now and writes the file on a thread (the snapshot itself takes a
    // few milliseconds; the file, some megabytes, would hold up a frame).
    bool Write(melonDS::NDS& nds, int map, const std::string& place)
    {
        Finish();
        melonDS::Savestate st;
        if (!nds.DoSavestate(&st) || st.Error) return false;
        st.Finish();
        ResumeHeader h;
        h.saveLen = nds.GetNDSSaveLength();
        h.saveHash = Hash(nds.GetNDSSave(), h.saveLen);
        h.time = (int64_t)::time(nullptr);
        h.map = map;
        snprintf(h.place, sizeof h.place, "%s", place.c_str());
        std::vector<uint8_t> blob(sizeof h + st.Length());
        memcpy(blob.data(), &h, sizeof h);
        memcpy(blob.data() + sizeof h, st.Buffer(), st.Length());
        writer = std::thread([this, blob = std::move(blob)]() {
            namespace fs = std::filesystem;
            const fs::path to = fs::u8path(path), tmp = fs::u8path(path + ".tmp"), bak = fs::u8path(path + ".bak");
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f.write((const char*)blob.data(), (std::streamsize)blob.size()) || (f.close(), !f))
                {
                    fprintf(stderr, "[autosave] cannot write %s\n", path.c_str());
                    return;
                }
            }
            std::error_code ec;
            if (fs::exists(to, ec)) fs::rename(to, bak, ec);
            fs::rename(tmp, to, ec);
            if (ec) fprintf(stderr, "[autosave] cannot write %s: %s\n", path.c_str(), ec.message().c_str());
        });
        return true;
    }

    // Waits for a file still being written.
    void Finish()
    {
        if (writer.joinable()) writer.join();
    }

    // The newest resume point that holds the save memory the console has now: its header,
    // and which file (0 the latest, 1 the backup); -1 if there is none.
    int Find(const melonDS::NDS& nds, ResumeHeader& out)
    {
        Finish();
        const uint32_t len = nds.GetNDSSaveLength();
        const uint64_t hash = Hash(nds.GetNDSSave(), len);
        for (int i = 0; i < 2; i++)
        {
            std::ifstream f(std::filesystem::u8path(i ? path + ".bak" : path), std::ios::binary);
            ResumeHeader h;
            if (f.read((char*)&h, sizeof h) && !memcmp(h.magic, ResumeHeader().magic, 8) && h.saveLen == len && h.saveHash == hash)
            {
                out = h;
                return i;
            }
        }
        return -1;
    }

    // Loads the newest resume point that fits (see Find). False if there is none, or if the
    // core refused it; `broken` then says the console was left half loaded and must be reset.
    bool Load(melonDS::NDS& nds, bool& broken)
    {
        broken = false;
        ResumeHeader h;
        const int which = Find(nds, h);
        if (which < 0) return false;
        std::ifstream f(std::filesystem::u8path(which ? path + ".bak" : path), std::ios::binary | std::ios::ate);
        const std::streamsize size = f ? (std::streamsize)f.tellg() : 0;
        if (size <= (std::streamsize)sizeof h) return false;
        std::vector<uint8_t> blob((size_t)size);
        f.seekg(0);
        if (!f.read((char*)blob.data(), size)) return false;
        melonDS::Savestate st(blob.data() + sizeof h, (melonDS::u32)(blob.size() - sizeof h), false);
        if (st.Error) return false;
        if (!nds.DoSavestate(&st))
        {
            broken = true;
            return false;
        }
        return true;
    }

private:
    std::thread writer;
};

// "Resume: Route 30 · 5 min ago" for the start screen's save card.
inline std::string ResumeLine(const std::string& place, int64_t when)
{
    const int64_t ago = std::max<int64_t>(0, (int64_t)time(nullptr) - when);
    std::string age = ago < 60 ? "just now" : ago < 3600 ? std::to_string(ago / 60) + " min ago"
        : ago < 86400 ? std::to_string(ago / 3600) + (ago < 7200 ? " hour ago" : " hours ago")
        : std::to_string(ago / 86400) + (ago < 172800 ? " day ago" : " days ago");
    return "Resume: " + (place.empty() ? "" : place + " \xC2\xB7 ") + age;
}

// Pokemon Centers' ground floors (sPokemonCenterMaps, src/map_header.c).
inline bool IsPokemonCenter(int map)
{
    static const int maps[] = { 501, 475, 428, 434, 358, 393, 482, 508, 407, 69, 158, 166, 236, 185, 226, 81, 246, 293, 514, 511,
        466, 169, 528, 534 };
    return std::find(std::begin(maps), std::end(maps), map) != std::end(maps);
}

// The game's save, driven through its X menu. Step runs every frame before the buttons go
// to the game; while Busy() the player's buttons are replaced by its own.
class GameSave
{
public:
    enum { IDLE, OPEN, NAV, CHOOSE, SAVING, CLOSE };
    int step = IDLE;
    bool wrote = false; // the last run reached the save write

    GameSave() { memset(moves, -1, sizeof moves); }

    bool Busy() const { return step != IDLE; }
    // Busy, or just done: the picture stays the overworld's a moment longer while the menu fades.
    bool Covering(uint32_t frame) const { return Busy() || (doneAt && frame - doneAt < 30); }

    // What the frontend knows this frame.
    struct In
    {
        uint32_t frame = 0;
        bool idle = false;      // on the overworld with nothing open (fieldIdle in main.cpp)
        bool startMenu = false; // the X menu's task is running
        bool saveApp = false;   // the save screen is open
        bool writing = false;   // the save is being written
        uint32_t player = 0;    // the player's own buttons this frame
        int map = -1, x = 0, z = 0, badges = 0;
        std::function<uint32_t(uint32_t)> read; // main RAM, 0 outside it
    };

    // Watches for the moments to save and runs a save; `keys` is what goes to the game.
    void Step(const In& in, uint32_t& keys, int keyA, int keyB, int keyX, int keyUp, int keyDown, int keyLeft, int keyRight)
    {
        // Moments: a Pokemon Center entered, a badge earned (both wait for calm).
        if (in.map != lastMap && in.map >= 0)
        {
            if (lastMap >= 0 && IsPokemonCenter(in.map)) Want(in.frame, false);
            lastMap = in.map;
        }
        // (Only once the count has held for a while: it reads 0 until the save is found in memory.)
        if (in.badges != lastBadges)
        {
            if (in.badges == lastBadges + 1 && badgesHeld >= 600) Want(in.frame, true);
            lastBadges = in.badges;
            badgesHeld = 0;
        }
        else
            badgesHeld++;
        // Calm: on the overworld, nothing pressed, standing on the same tile, for a second.
        if (in.idle && !in.player && in.x == stillX && in.z == stillZ && in.map == lastMap) still++;
        else still = 0;
        stillX = in.x;
        stillZ = in.z;

        if (step == IDLE)
        {
            // (Longer after the player cut one short: they are busy.)
            if (wanted && still >= (attempts ? 300u : 60u))
            {
                wanted = false;
                attempts++;
                Go(OPEN, in.frame);
                wrote = false;
                lastDir = -1;
                fprintf(stderr, "[autosave] frame %u: saving the game on map %d\n", in.frame, in.map);
            }
            else
                return;
        }
        // The player's presses cancel it until the save is chosen; then it runs to the end.
        if (in.player && (step == OPEN || step == NAV || step == CHOOSE))
        {
            fprintf(stderr, "[autosave] frame %u: stopped by a button press\n", in.frame);
            Go(in.startMenu ? CLOSE : IDLE, in.frame);
            wanted = attempts < 3; // tried again at a calmer moment
            if (step == IDLE) return;
        }
        keys = 0;
        if (hold > 0)
        {
            keys = holdKey;
            hold--;
            return;
        }
        if (gap > 0)
        {
            gap--;
            return;
        }
        const uint32_t since = in.frame - stepAt;
        auto tap = [&](int key, int wait) {
            holdKey = 1u << key;
            keys = holdKey;
            hold = 2; // held three frames: the game reads the buttons every other frame
            gap = wait;
        };
        const uint32_t fs = in.read(0x021D4158);
        // The X menu's data (StartMenuTaskData, include/start_menu.h) is the environment of
        // the field's task manager (FieldSystem +0x10, its +0x0C) while the menu runs.
        const uint32_t menu = in.startMenu && fs ? in.read(in.read(fs + 0x10) + 0x0C) : 0;
        const uint32_t count = menu ? in.read(menu + 0x2C) : 0, state = menu ? in.read(menu + 0x24) >> 16 : 0;
        auto action = [&](uint32_t i) { return (int)(in.read(menu + 0x3A + i) & 0xFF); }; // selectionToAction
        const bool menuOk = menu && count >= 1 && count <= 10 && state < 21;
        switch (step)
        {
        case OPEN:
            if (menuOk && state == 3) // START_MENU_STATE_HANDLE_INPUT
                Go(NAV, in.frame);
            else if (!in.startMenu && !in.idle && since > 4)
                Stop(in, "the overworld is busy");
            else if (since > 150)
                Stop(in, "the menu did not open");
            else if (!in.startMenu && since % 60 == 0)
                tap(keyX, 20);
            break;
        case NAV:
        {
            if (!menuOk)
            {
                Stop(in, "the menu went away");
                break;
            }
            int target = -1;
            uint64_t buttons = count;
            for (uint32_t i = 0; i < count; i++)
            {
                if (action(i) == 4) target = (int)i; // START_MENU_ACTION_SAVE
                buttons = buttons * 16 + action(i);
            }
            if (buttons != movesFor)
            {
                // Other buttons (the Pokedex got, the Bug-Catching Contest's menu): other moves.
                memset(moves, -1, sizeof moves);
                movesFor = buttons;
            }
            if (target < 0)
            {
                Stop(in, "saving is not possible here");
                break;
            }
            // The cursor: FieldSystem.unkD3, the button in the order of the list. Where each
            // direction takes it depends on the buttons there are (the menu's own code, in an
            // overlay not decompiled), so the moves are learned as they are made and the
            // shortest known way to SAVE is taken, trying the directions not known yet.
            const int cur = (int)(in.read(fs + 0xD0) >> 24) & 15;
            if (lastDir >= 0) moves[lastCur][lastDir] = (int8_t)cur;
            lastDir = -1;
            if (cur == target && state == 3)
            {
                tap(keyA, 30);
                Go(CHOOSE, in.frame);
                break;
            }
            const int dir = Route(cur, target);
            if (since > 600 || tries > 24 || dir < 0)
            {
                memset(moves, -1, sizeof moves); // (in case a press went unseen: learned again)
                Stop(in, "the cursor did not reach SAVE");
                break;
            }
            tries++;
            lastCur = cur;
            lastDir = dir;
            const int keys4[4] = { keyDown, keyRight, keyUp, keyLeft };
            tap(keys4[dir], 12);
            break;
        }
        case CHOOSE:
            if (in.saveApp) Go(SAVING, in.frame);
            else if (since > 180) Stop(in, "the save screen did not open");
            break;
        case SAVING:
            // The save screen's own code (main.cpp) answers its questions; this waits for it to
            // close, back to the menu (or straight to the overworld).
            if (in.writing) wrote = true;
            if (!in.saveApp) Go(CLOSE, in.frame);
            else if (since > 60 * 90) Stop(in, "the save took too long");
            break;
        case CLOSE:
            // Closed for half a second: after a save the game takes its menu down and puts it
            // straight back up.
            if (!in.startMenu && in.idle)
            {
                if (++tries >= 30)
                {
                    fprintf(stderr, "[autosave] frame %u: %s\n", in.frame, wrote ? "saved" : "menu closed");
                    Go(IDLE, in.frame);
                }
            }
            else
            {
                tries = 0;
                if (since > 400) Stop(in, "the menu did not close");
                else if (menuOk && state == 3) tap(keyB, 30);
            }
            break;
        }
    }

    // Asks for a save at the next calm moment; `now` skips the wait since the last one.
    void Want(uint32_t frame, bool now)
    {
        if (now || !lastSave || frame - lastSave >= MinGap)
        {
            wanted = true;
            attempts = 0;
        }
    }

    // Called when a save is done (the game's or the player's own), for the wait between two.
    void Saved(uint32_t frame) { lastSave = frame ? frame : 1; }

    static constexpr uint32_t MinGap = 60 * 60 * 10; // at most one every ten minutes, but for badges

private:
    // Where each direction (down, right, up, left) took the cursor from each button; -1 not
    // tried yet. Kept from one save to the next.
    int8_t moves[16][4];
    uint64_t movesFor = 0; // the buttons they are for
    int lastCur = 0, lastDir = -1;
    bool wanted = false;
    int lastMap = -1, lastBadges = -1, stillX = -1, stillZ = -1, tries = 0, attempts = 0;
    uint32_t still = 0, badgesHeld = 0, doneAt = 0, stepAt = 0, lastSave = 0, holdKey = 0;
    int hold = 0, gap = 0;

    // The first direction on the shortest known way from `from` to `to`, or else towards the
    // nearest button with a direction not tried yet; -1 if there is nothing left to try.
    int Route(int from, int to) const
    {
        int first[16], queue[16], n = 0;
        for (int& f : first) f = -2;
        first[from] = -1;
        queue[n++] = from;
        int untried = -1;
        for (int q = 0; q < n; q++)
        {
            const int at = queue[q];
            if (at == to) return first[at];
            for (int d = 0; d < 4; d++)
            {
                const int next = moves[at][d];
                if (next < 0)
                {
                    if (untried < 0) untried = first[at] < 0 ? d : first[at];
                    continue;
                }
                if (first[next] != -2) continue;
                first[next] = first[at] < 0 ? d : first[at];
                queue[n++] = next;
            }
        }
        return untried;
    }

    void Go(int s, uint32_t frame)
    {
        if (s == IDLE && step != IDLE) doneAt = frame;
        step = s;
        stepAt = frame;
        tries = 0;
    }
    // Gives up: closes the menu if it is open (CLOSE gives up in turn after a while).
    void Stop(const In& in, const char* why)
    {
        fprintf(stderr, "[autosave] frame %u: not saved, %s\n", in.frame, why);
        Go(step != CLOSE && in.startMenu ? CLOSE : IDLE, in.frame);
    }
};

// The animation in the pill: assets/autosave.gif if the player put one there (not part of
// the project), else a ring of dots.
class Indicator
{
public:
    void Load(const std::vector<uint8_t>& gif)
    {
        if (gif.empty()) return;
        int* delays = nullptr;
        int w = 0, h = 0, n = 0, comp = 0;
        stbi_uc* px = stbi_load_gif_from_memory(gif.data(), (int)gif.size(), &delays, &w, &h, &n, &comp, 4);
        if (!px)
        {
            fprintf(stderr, "[autosave] cannot read assets/autosave.gif\n");
            return;
        }
        for (int i = 0; i < n; i++)
        {
            Image img;
            img.w = w;
            img.h = h;
            img.px.resize((size_t)w * h);
            const stbi_uc* s = px + (size_t)i * w * h * 4;
            for (size_t p = 0; p < img.px.size(); p++, s += 4) img.px[p] = (uint32_t)s[3] << 24 | s[0] << 16 | s[1] << 8 | s[2];
            frames.push_back(std::move(img));
            const int d = delays && delays[i] > 0 ? delays[i] : 100;
            total += d;
            ends.push_back(total);
        }
        stbi_image_free(px);
        free(delays);
    }

    // A pill right-aligned at `right` with its top at y: the animation, and `text` if any.
    // Returns the y below it.
    float Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float right, float y, const std::string& text, uint32_t ms)
    {
        const float h = 64, icon = 48;
        const float iw = frames.empty() ? icon : icon * frames[0].w / std::max(1, frames[0].h);
        const float w = 16 + iw + (text.empty() ? 16 : 12 + HudTextWidth(ui, f, 32, text) + 24);
        HudPanel(c, ui, sp, "panels/toast", right - w, y, w, h);
        const float ix = right - w + 16, iy = y + (h - icon) / 2;
        if (!frames.empty())
        {
            const uint32_t t = ms % (uint32_t)total;
            size_t i = 0;
            while (i + 1 < ends.size() && (int)t >= ends[i]) i++;
            Blit(c, frames[i], 0, 0, (float)frames[i].w, (float)frames[i].h, ui(ix), ui(iy), ui(iw), ui(icon));
        }
        else
        {
            // Eight dots round a circle, the brightest going round once a second.
            const int lead = (int)(ms / 125 % 8);
            for (int k = 0; k < 8; k++)
            {
                const float a = k * 3.14159265f / 4, d = 11;
                const int age = (lead - k + 8) % 8;
                const uint32_t alpha = (uint32_t)(255 - age * 28);
                HudSprite(c, ui, sp, "people/status_online", ix + icon / 2 + std::sin(a) * 16 - d / 2, iy + icon / 2 - std::cos(a) * 16 - d / 2,
                    d, d, alpha << 24 | 0xFFFFFF);
            }
        }
        if (!text.empty()) HudText(c, ui, f, ix + iw + 12, y + (h - 32) / 2, 32, text, HudColour::Ink);
        return y + h + 16;
    }

private:
    std::vector<Image> frames;
    std::vector<int> ends; // when each frame ends, in milliseconds from the first
    int total = 0;
};

} // namespace Autosave
