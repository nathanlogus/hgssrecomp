#pragma once
// Songs in place of the game's music (Music: New songs): MP3 files the player puts in
// assets/songs, listed in assets/songs/songs.txt as "<music number> = <file>" (the numbers
// are the game's sequence numbers, e.g. 1022 for Goldenrod City; # starts a comment). While
// the game plays a piece that has a song, its music channels are left out of what is heard
// (SPU.cpp, ssr_SpuHearMute) and the song is mixed in instead, looping, with short fades
// when it changes. Sound effects, cries and jingles stay the game's own.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "thirdparty/dr_mp3.h"

class SongPlayer
{
public:
    // The game's music players (1: overworld, 7: battles and the title screen) use channels
    // 1-10, 13 and 15; sound effects use 11, 12, 14 and 15, cries 14 and 15 (the sound
    // archive's player table). Music keeps 15 busy nearly all the time, so it is left out
    // too, except while an effect or a cry plays (then 15 may be theirs: they take 11, 12
    // and 14 first).
    static constexpr uint16_t MusicChannels = 0x27FE, SharedChannel = 0x8000, EffectChannels = 0x5800;
    // How loud songs are next to the game's sounds: each is brought to this loudness (RMS,
    // measured on its first 20 seconds), about that of the game's own music.
    static constexpr float Loudness = 0.09f;

    // Reads the list; `read` gives a file of the songs folder ("songs.txt", "x.mp3").
    using Reader = std::vector<uint8_t> (*)(const std::string& name);
    bool Load(Reader r)
    {
        read = r;
        songs.clear();
        const std::vector<uint8_t> f = read("songs.txt");
        const std::string text(f.begin(), f.end());
        size_t at = 0;
        while (at < text.size())
        {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            if (const size_t hash = line.find('#'); hash != std::string::npos) line.resize(hash);
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const int seq = atoi(Trim(line.substr(0, eq)).c_str());
            const std::string file = Trim(line.substr(eq + 1));
            if (seq > 0 && !file.empty()) songs[seq] = file;
        }
        return !songs.empty();
    }
    bool Any() const { return !songs.empty(); }

    // Once per frame: whether songs are on, the game's music now (`seq`), whether its music
    // channels make any sound, and whether a jingle is on (the game's own is heard then).
    void Update(bool on, int seq, bool channelsBusy, bool effects, bool jingle, uint32_t frame)
    {
        effectsOn = effects;
        if (channelsBusy) lastBusy = frame;
        // The game's music silent for a few seconds (a pause in a scene): the song waits too.
        const bool silent = frame - lastBusy > 150;
        auto it = on ? songs.find(seq) : songs.end();
        const std::string want = it == songs.end() || bad.count(it->second) ? "" : it->second;
        if (cur && cur->name != want) cur->fadingOut = true;
        if (cur && cur->fadingOut && cur->gain <= 0) cur.reset();
        // Read and opened in the background: a song's file is several megabytes.
        if (loading.valid() && loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            std::unique_ptr<Song> s = loading.get();
            if (!s) bad[loadingName] = true;
            else if (!cur && s->name == want) cur = std::move(s);
        }
        if (!cur && !want.empty() && !loading.valid())
        {
            loadingName = want;
            Reader r = read;
            loading = std::async(std::launch::async, [r, want]() { return Open(r, want); });
        }
        paused = jingle || silent;
    }

    // The game's channels to leave out of what is heard.
    uint16_t Mute() const { return cur && !cur->fadingOut && !paused ? MusicChannels | (effectsOn ? 0 : SharedChannel) : 0; }

    float userGain = 1.0f; // Settings: Music volume

    // Adds the song to `frames` stereo frames at 48 kHz.
    void Mix(int16_t* out, int frames)
    {
        // (SSR_SONGS_TEST_SILENT=1 leaves the song out: for checking that none of the game's
        // music is heard under it.)
        if (!cur || getenv("SSR_SONGS_TEST_SILENT")) return;
        Song& s = *cur;
        const double step = (double)s.rate / 48000.0;
        for (int i = 0; i < frames; i++)
        {
            // Fades: out over 0.75 s when replaced, in over 0.4 s; while paused it waits.
            const float target = s.fadingOut || paused ? 0.0f : 1.0f;
            const float speed = s.fadingOut ? 1.0f / (0.75f * 48000) : 1.0f / (0.4f * 48000);
            s.gain += std::clamp(target - s.gain, -speed, speed);
            if (s.gain <= 0) continue;
            if (!s.Ensure()) return;
            const size_t a = (size_t)s.pos;
            const float t = (float)(s.pos - a);
            for (int c = 0; c < 2; c++)
            {
                const float v = s.buf[a * 2 + c] * (1 - t) + s.buf[(a + 1) * 2 + c] * t;
                const int mixed = out[i * 2 + c] + (int)(v * 32767 * s.gain * s.level * userGain);
                out[i * 2 + c] = (int16_t)std::clamp(mixed, -32768, 32767);
            }
            s.pos += step;
        }
    }

private:
    struct Song
    {
        std::string name;
        std::vector<uint8_t> file;
        drmp3 mp3;
        bool open = false;
        int rate = 44100;
        std::vector<float> buf; // decoded frames as stereo, from buf[0] = the frame at `pos` 0
        double pos = 0;
        float gain = 0;
        float level = 1; // brings it to Loudness
        bool fadingOut = false;
        ~Song()
        {
            if (open) drmp3_uninit(&mp3);
        }
        // At least two frames from `pos` decoded (more decoded as needed, from the start
        // again at the end).
        bool Ensure()
        {
            if ((size_t)(pos + 2) * 2 <= buf.size()) return true;
            const size_t drop = std::min(buf.size() / 2, (size_t)pos);
            buf.erase(buf.begin(), buf.begin() + drop * 2);
            pos -= (double)drop;
            const int ch = std::max<int>(1, (int)mp3.channels);
            std::vector<float> raw((size_t)4096 * ch);
            int ends = 0;
            while ((size_t)(pos + 2) * 2 > buf.size())
            {
                const drmp3_uint64 got = drmp3_read_pcm_frames_f32(&mp3, 4096, raw.data());
                if (!got)
                {
                    if (++ends > 1) return false; // nothing to play at all
                    drmp3_seek_to_pcm_frame(&mp3, 0); // the end: from the start again
                    continue;
                }
                for (drmp3_uint64 f = 0; f < got; f++)
                {
                    buf.push_back(raw[f * ch]);
                    buf.push_back(raw[f * ch + (ch > 1 ? 1 : 0)]);
                }
            }
            return true;
        }
    };
    Reader read = nullptr;
    std::map<int, std::string> songs;
    std::map<std::string, bool> bad; // could not be read or decoded: not tried again
    std::unique_ptr<Song> cur;
    std::future<std::unique_ptr<Song>> loading;
    std::string loadingName;
    uint32_t lastBusy = 0;
    bool paused = false, effectsOn = false;

    static std::string Trim(std::string s)
    {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        return s.substr(i);
    }

    static std::unique_ptr<Song> Open(Reader read, const std::string& name)
    {
        auto s = std::make_unique<Song>();
        s->name = name;
        s->file = read(name);
        if (s->file.empty() || !drmp3_init_memory(&s->mp3, s->file.data(), s->file.size(), nullptr))
        {
            fprintf(stderr, "[songs] cannot open %s\n", name.c_str());
            return nullptr;
        }
        s->open = true;
        s->rate = (int)s->mp3.sampleRate;
        // Its loudness, from the first 20 seconds; then back to the start.
        const int ch = std::max<int>(1, (int)s->mp3.channels);
        std::vector<float> raw((size_t)4096 * ch);
        double sum = 0;
        drmp3_uint64 counted = 0, got;
        while (counted < (drmp3_uint64)s->rate * 20 && (got = drmp3_read_pcm_frames_f32(&s->mp3, 4096, raw.data())) > 0)
        {
            for (size_t i = 0; i < (size_t)got * ch; i++) sum += (double)raw[i] * raw[i];
            counted += got;
        }
        drmp3_seek_to_pcm_frame(&s->mp3, 0);
        const double rms = counted ? std::sqrt(sum / ((double)counted * ch)) : 0;
        s->level = rms > 0.005 ? (float)std::clamp(Loudness / rms, 0.3, 3.0) : 1.0f;
        fprintf(stderr, "[songs] %s (%d Hz, loudness x%.2f)\n", name.c_str(), s->rate, s->level);
        return s;
    }
};
