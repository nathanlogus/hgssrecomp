#pragma once
// The game's own menu sounds for the frontend's menus (Menu sounds): the cursor, choose and
// cancel sounds of the game's menus, read from the player's ROM (its sound archive,
// data/sound/gs_sound_data.sdat) and played through a small renderer of the archive's
// sequences, so nothing of the game is shipped. Each sound is rendered once, when first
// played, into 48 kHz stereo, and mixed into the frontend's audio output like the songs.
//
// The renderer follows the DS sound library (NitroSDK SND) as far as these short sequences
// need: notes with a length, rests, tempo, volume, expression, pan, pitch bend, program
// changes, several tracks, and the instruments' sampled waves (PCM8, PCM16, IMA-ADPCM) with
// their attack/decay/sustain/release envelopes, updated at the sound library's own rate.
// shortcut: no PSG/noise instruments, modulation, portamento, sweep or variables; none of the
// sounds used here needs them, add them if another sound is wanted.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "ndsrom.h"

class GameSfx
{
public:
    // The game's sound effects (pret's include/constants/sndseq.h). Its menus play
    // SEQ_SE_DP_SELECT as the cursor moves and as a value changes (options, bag, yes/no),
    // SEQ_SE_DP_BUTTON9 when a yes/no prompt is answered, and SEQ_SE_GS_GEARCANCEL when a
    // menu is left with B (start menu, options, bag).
    enum Sound { Cursor, Confirm, Cancel, SoundCount };
    static constexpr int Seqs[SoundCount] = { 1500, 1508, 2368 };

    void SetRom(const uint8_t* rom, size_t size)
    {
        uint32_t off, len;
        if (NdsRomFile(rom, size, "data/sound/gs_sound_data.sdat", off, len) && len > 0x40 && !memcmp(rom + off, "SDAT", 4))
        {
            sdat = rom + off;
            sdatSize = len;
        }
        else fprintf(stderr, "[sfx] no sound archive in the ROM\n");
    }

    // A new sound takes the place of the one playing, as the game's sound-effect player does.
    void Play(Sound s)
    {
        if (!sdat) return;
        const std::vector<int16_t>& p = Get(s);
        if (p.empty()) return;
        cur = s;
        pos = 0;
    }

    // The sound rendered (empty if it cannot be); for tests too.
    const std::vector<int16_t>& Get(Sound s)
    {
        if (!rendered[s])
        {
            rendered[s] = true;
            pcm[s] = Render(Seqs[s]);
            fprintf(stderr, "[sfx] sound %d: %zu ms\n", Seqs[s], pcm[s].size() / 96);
        }
        return pcm[s];
    }

    float userGain = 1.0f; // Settings: Sound effects volume

    // Adds what plays to `frames` stereo frames at 48 kHz.
    void Mix(int16_t* out, int frames)
    {
        if (cur < 0) return;
        const std::vector<int16_t>& p = pcm[cur];
        for (int i = 0; i < frames * 2 && pos < p.size(); i++, pos++)
            out[i] = (int16_t)std::clamp(out[i] + (int)(p[pos] * userGain), -32768, 32767);
        if (pos >= p.size()) cur = -1;
    }

private:
    const uint8_t* sdat = nullptr;
    size_t sdatSize = 0;
    std::vector<int16_t> pcm[SoundCount];
    bool rendered[SoundCount] = {};
    int cur = -1;
    size_t pos = 0;

    // Level of the output next to the game's own sound (melonDS's mix of the DS's channels):
    // measured on the three sounds, rendered here and played by the game (the options' cursor,
    // the save prompt's Yes, and B in the options), which agree to within 10%.
    static constexpr float Gain = 1.04f;

    uint32_t U32(size_t o) const { return o + 4 <= sdatSize ? RomGet32(sdat + o) : 0; }
    uint32_t U16(size_t o) const { return o + 2 <= sdatSize ? RomGet16(sdat + o) : 0; }

    // A record of the INFO block (kind 0: sequences, 2: banks, 3: wave archives), as an
    // offset in the archive, or 0.
    size_t Info(int kind, int i) const
    {
        const size_t info = U32(0x18), table = info + U32(info + 8 + 4 * kind);
        if ((uint32_t)i >= U32(table)) return 0;
        const uint32_t o = U32(table + 4 + 4 * i);
        return o ? info + o : 0;
    }
    // A file of the archive, by its id.
    std::pair<const uint8_t*, size_t> File(uint32_t id) const
    {
        const size_t fat = U32(0x20), e = fat + 12 + 16 * (size_t)id;
        const uint32_t o = U32(e), n = U32(e + 4);
        if (id >= U32(fat + 8) || (uint64_t)o + n > sdatSize) return { nullptr, 0 };
        return { sdat + o, n };
    }

    // The sound library's volume curve, in tenths of a decibel: (v/127)^2.
    static int DbSquare(int v) { return v <= 0 ? -723 : std::max(-722, (int)std::lround(400 * std::log10(v / 127.0))); }

    struct Wave
    {
        std::vector<float> s;
        int timer = 0; // the sound hardware's timer for it at its own pitch
        size_t loopStart = 0;
        bool loop = false;
    };
    struct Instrument
    {
        const Wave* wave = nullptr;
        int root = 60, attack = 127, decay = 127, sustain = 127, release = 127, pan = 64;
    };

    static Wave DecodeWave(const uint8_t* w, size_t n)
    {
        Wave out;
        if (n < 12) return out;
        const int type = w[0];
        out.loop = w[1] != 0;
        const uint32_t timer = RomGet16(w + 4), loopWords = RomGet16(w + 6), restWords = RomGet32(w + 8);
        out.timer = timer ? (int)timer : 512;
        const uint8_t* d = w + 12;
        const size_t bytes = std::min<size_t>(n - 12, (size_t)(loopWords + restWords) * 4);
        if (type == 0)
        {
            for (size_t i = 0; i < bytes; i++) out.s.push_back((int8_t)d[i] / 128.0f);
            out.loopStart = loopWords * 4;
        }
        else if (type == 1)
        {
            for (size_t i = 0; i + 1 < bytes; i += 2) out.s.push_back((int16_t)RomGet16(d + i) / 32768.0f);
            out.loopStart = loopWords * 2;
        }
        else if (type == 2 && bytes >= 4)
        {
            static const int index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
            static const uint16_t steps[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
                41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337,
                371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
                2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
                11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
            int sample = (int16_t)RomGet16(d), idx = std::clamp((int)RomGet16(d + 2), 0, 88);
            for (size_t i = 8; i < bytes * 2; i++)
            {
                const int nib = d[i / 2] >> (i & 1) * 4 & 15;
                const int step = steps[idx];
                int diff = step >> 3;
                if (nib & 1) diff += step >> 2;
                if (nib & 2) diff += step >> 1;
                if (nib & 4) diff += step;
                sample = std::clamp(nib & 8 ? sample - diff : sample + diff, -32767, 32767);
                idx = std::clamp(idx + index[nib & 7], 0, 88);
                out.s.push_back(sample / 32768.0f);
            }
            out.loopStart = loopWords ? (loopWords - 1) * 8 : 0;
        }
        if (out.loopStart >= out.s.size()) out.loop = false;
        return out;
    }

    std::vector<int16_t> Render(int seqNo) const
    {
        std::vector<int16_t> out;
        const size_t si = Info(0, seqNo);
        if (!si) return out;
        const uint32_t seqFile = U16(si), bankNo = U16(si + 4);
        // The sequence's own volume, in proportion (not squared like the others): measured on
        // three sounds of volumes 54, 82 and 110 against the game's playing of them.
        const int seqDb = DbSquare(U16(si + 6) & 0xFF) / 2;
        const size_t bi = Info(2, bankNo);
        if (!bi) return out;
        const auto [seq, seqSize] = File(seqFile);
        const auto [bank, bankSize] = File(U16(bi));
        if (!seq || !bank || seqSize < 0x1C || bankSize < 0x3C) return out;

        // The bank's wave archives, decoded when an instrument needs one of their waves.
        std::vector<std::pair<const uint8_t*, size_t>> archives(4);
        for (int a = 0; a < 4; a++)
            if (const uint32_t w = U16(bi + 4 + 2 * a); w != 0xFFFF)
                if (const size_t wi = Info(3, w)) archives[a] = File(U32(wi) & 0xFFFFFF);
        std::vector<std::pair<uint32_t, Wave>> waves;
        waves.reserve(16);
        auto wave = [&](int swav, int swar) -> const Wave* {
            const uint32_t key = swar << 16 | swav;
            for (auto& w : waves)
                if (w.first == key) return &w.second;
            if (swar > 3 || !archives[swar].first) return nullptr;
            const auto [a, n] = archives[swar];
            if (n < 0x3C || (uint32_t)swav >= RomGet32(a + 0x38) || 0x3C + 4 * (size_t)swav + 4 > n) return nullptr;
            const uint32_t o = RomGet32(a + 0x3C + 4 * swav);
            const uint32_t end = (uint32_t)swav + 1 < RomGet32(a + 0x38) ? RomGet32(a + 0x40 + 4 * swav) : (uint32_t)n;
            if (o >= n || end > n || end <= o) return nullptr;
            if (waves.size() == waves.capacity()) return nullptr; // (the channels keep pointers to them)
            waves.push_back({ key, DecodeWave(a + o, end - o) });
            return &waves.back().second;
        };
        // The instrument of a program for a key: a single one, a drum set or a key split.
        auto instrument = [&](int prog, int key, Instrument& inst) {
            if ((uint32_t)prog >= RomGet32(bank + 0x38) || 0x3C + 4 * (size_t)prog + 4 > bankSize) return false;
            const uint8_t type = bank[0x3C + 4 * prog];
            size_t at = RomGet16(bank + 0x3C + 4 * prog + 1);
            if (type == 16)
            {
                if (at + 2 > bankSize || key < bank[at] || key > bank[at + 1]) return false;
                at += 2 + 12 * (key - bank[at]) + 2;
            }
            else if (type == 17)
            {
                int r = 0;
                while (r < 8 && at + r < bankSize && bank[at + r] && key > bank[at + r]) r++;
                if (r == 8 || at + r >= bankSize || !bank[at + r]) return false;
                at += 8 + 12 * r + 2;
            }
            if (type >= 16 && (at < 2 || at + 10 > bankSize || RomGet16(bank + at - 2) != 1)) return false;
            if ((type < 16 && type != 1) || at + 10 > bankSize) return false; // PSG and noise: not made here
            const uint8_t* p = bank + at;
            inst.wave = wave(RomGet16(p), RomGet16(p + 2));
            inst.root = p[4];
            inst.attack = p[5];
            inst.decay = p[6];
            inst.sustain = p[7];
            inst.release = p[8];
            inst.pan = p[9];
            return inst.wave != nullptr;
        };

        struct Track
        {
            bool on = false;
            uint32_t pc = 0, wait = 0;
            int prog = 0, vol = 127, expr = 127, pan = 0, transpose = 0, bend = 0, range = 2; // (pan: -64..63)
            int attack = -1, decay = -1, sustain = -1, release = -1;
            bool noteWait = true;
            uint32_t stack[3] = {};
            int depth = 0;
        };
        struct Channel
        {
            const Wave* wave = nullptr;
            int track = 0, key = 60, root = 60, velocity = 127, pan = 0;
            int length = 0; // ticks left before the release (counted down by the sequence)
            int stage = 0;  // 0 attack, 1 decay, 2 sustain, 3 release, 4 done
            int amp = -92544, attackRate = 0, decayRate = 0, sustain = 0, releaseRate = 0;
            double pos = 0;
            int trackDb = 0, trackPan = 0;
            float gainL = 0, gainR = 0; // this update's
        };
        auto fall = [](int v) { return v == 127 ? 0xFFFF : v == 126 ? 0x3C00 : v < 50 ? v * 2 + 1 : 0x1E00 / (126 - v); };

        Track tracks[16];
        std::vector<Channel> chans;
        tracks[0].on = true;
        tracks[0].pc = RomGet32(seq + 0x18);
        const uint32_t dataStart = tracks[0].pc;
        int tempo = 120, tempoCounter = 240;
        auto byte = [&](Track& t) -> uint32_t { return t.pc < seqSize ? seq[t.pc++] : 0xFF; };
        auto varLen = [&](Track& t) {
            uint32_t v = 0, b;
            do { b = byte(t); v = v << 7 | (b & 0x7F); } while ((b & 0x80) && t.pc < seqSize);
            return v;
        };
        auto u24 = [&](Track& t) { uint32_t v = byte(t); v |= byte(t) << 8; return v | byte(t) << 16; };

        // One tick of a track: its notes' lengths counted down, then its commands run until
        // it waits.
        auto step = [&](int ti) {
            Track& t = tracks[ti];
            for (Channel& c : chans)
                if (c.track == ti && c.stage < 3 && c.length > 0 && --c.length == 0) c.stage = 3;
            if (t.wait > 0 && --t.wait > 0) return;
            for (int guard = 0; t.on && t.wait == 0 && guard < 1000; guard++)
            {
                const uint32_t cmd = byte(t);
                if (cmd < 0x80)
                {
                    const int velocity = (int)byte(t);
                    const int len = (int)varLen(t);
                    const int key = std::clamp((int)cmd + t.transpose, 0, 127);
                    Instrument inst;
                    if (instrument(t.prog, key, inst) && chans.size() < 32)
                    {
                        Channel c;
                        c.wave = inst.wave;
                        c.track = ti;
                        c.key = key;
                        c.root = inst.root;
                        c.velocity = velocity;
                        c.pan = inst.pan - 64;
                        c.length = len;
                        const int a = t.attack >= 0 ? t.attack : inst.attack;
                        static const uint8_t fast[] = { 0x00, 0x01, 0x05, 0x0E, 0x1A, 0x26, 0x33, 0x3F, 0x49, 0x54,
                                                        0x5C, 0x64, 0x6D, 0x74, 0x7B, 0x7F, 0x84, 0x89, 0x8F };
                        c.attackRate = a >= 0x6D ? fast[127 - a] : 255 - a;
                        c.decayRate = fall(t.decay >= 0 ? t.decay : inst.decay);
                        const int s = t.sustain >= 0 ? t.sustain : inst.sustain;
                        c.sustain = s == 127 ? 0 : DbSquare(s) * 128;
                        c.releaseRate = fall(t.release >= 0 ? t.release : inst.release);
                        chans.push_back(c);
                    }
                    if (t.noteWait) t.wait = len;
                    continue;
                }
                switch (cmd)
                {
                case 0x80: t.wait = varLen(t); break;
                case 0x81: t.prog = (int)varLen(t); break;
                case 0x93:
                {
                    const uint32_t n = byte(t) & 15, at = u24(t);
                    tracks[n] = Track();
                    tracks[n].on = true;
                    tracks[n].pc = dataStart + at;
                    break;
                }
                case 0x94: t.pc = dataStart + u24(t); break;
                case 0x95:
                {
                    const uint32_t at = u24(t);
                    if (t.depth < 3) t.stack[t.depth++] = t.pc;
                    t.pc = dataStart + at;
                    break;
                }
                case 0xFD:
                    if (t.depth > 0) t.pc = t.stack[--t.depth];
                    break;
                case 0xFE: byte(t); byte(t); break;
                case 0xFF: t.on = false; break;
                case 0xC0: t.pan = (int)byte(t) - 64; break;
                case 0xC1: t.vol = (int)byte(t); break;
                case 0xC3: t.transpose = (int8_t)byte(t); break;
                case 0xC4: t.bend = (int8_t)byte(t); break;
                case 0xC5: t.range = (int)byte(t); break;
                case 0xC7: t.noteWait = byte(t) != 0; break;
                case 0xD0: t.attack = (int)byte(t); break;
                case 0xD1: t.decay = (int)byte(t); break;
                case 0xD2: t.sustain = (int)byte(t); break;
                case 0xD3: t.release = (int)byte(t); break;
                case 0xD5: t.expr = (int)byte(t); break;
                case 0xE1: tempo = (int)byte(t); tempo |= (int)byte(t) << 8; break;
                case 0xFC: break; // loop end: these sounds do not loop
                default:
                    if (cmd >= 0xB0 && cmd <= 0xBD) { byte(t); byte(t); byte(t); }      // variables
                    else if (cmd >= 0xC0 && cmd <= 0xD7) byte(t);                       // the others with a byte
                    else if (cmd >= 0xE0 && cmd <= 0xE3) { byte(t); byte(t); }          // and with two
                    else t.on = false; // not understood (0xA0-0xA2 and the like): stop the track
                }
            }
        };

        // The sound library's update: 64 * 2728 cycles of the ARM7 (33.513982 MHz), 5.2 ms.
        const double perUpdate = 48000.0 * 64 * 2728 / 33513982.0;
        double owed = 0;
        for (int update = 0; update < 192 * 4; update++)
        {
            // The sequence: as many ticks as the tempo gives (240 per update at tempo 240).
            while (tempoCounter >= 240)
            {
                tempoCounter -= 240;
                for (int ti = 0; ti < 16; ti++)
                    if (tracks[ti].on) step(ti);
            }
            tempoCounter += tempo;
            // The channels: envelope, volume and pan for this update.
            bool any = false;
            for (int ti = 0; ti < 16; ti++) any |= tracks[ti].on;
            for (Channel& c : chans)
            {
                if (c.stage == 0)
                {
                    c.amp = -((-c.amp) * c.attackRate / 255);
                    if (c.amp == 0) c.stage = 1;
                }
                else if (c.stage == 1)
                {
                    c.amp -= c.decayRate;
                    if (c.amp <= c.sustain) { c.amp = c.sustain; c.stage = 2; }
                }
                else if (c.stage == 3)
                {
                    c.amp -= c.releaseRate;
                    if (c.amp <= -92544) c.stage = 4;
                }
                // Its track's volume and pan, until the note is let go: then it fades as it was
                // (measured: the game's fades of the track's volume at the end of the cursor
                // sound are not heard on the notes already let go).
                const Track& t = tracks[c.track];
                if (c.stage < 3)
                {
                    c.trackDb = DbSquare(t.vol) + DbSquare(t.expr);
                    c.trackPan = t.pan;
                }
                const int db = std::max(-723, DbSquare(c.velocity) + (c.amp >> 7) + c.trackDb + seqDb);
                const float v = c.stage == 4 ? 0.0f : (float)std::pow(10.0, db / 200.0);
                const int pan = std::clamp(64 + c.pan + c.trackPan, 0, 127);
                const float l = v * (128 - pan) / 128.0f, r = v * pan / 128.0f;
                any |= c.stage != 4;
                c.gainL = l;
                c.gainR = r;
            }
            owed += perUpdate;
            const int n = (int)owed;
            owed -= n;
            const size_t base = out.size();
            out.resize(base + (size_t)n * 2, 0);
            std::vector<float> mix((size_t)n * 2, 0.0f);
            for (Channel& c : chans)
            {
                if (c.stage == 4 || !c.wave || c.wave->s.empty()) continue;
                const Track& t = tracks[c.track];
                // The pitch in 64ths of a semitone, made into the hardware's whole timer value
                // as the sound library does (SND_CalcTimer): notes of a pitch played over one
                // another then keep the game's phases to one another, which is heard.
                const int pitch = (c.key - c.root) * 64 + (t.bend * t.range >> 1);
                const int timer = std::clamp((int)(c.wave->timer * std::pow(2.0, -pitch / 768.0)), 16, 0xFFFF);
                const double inc = 16756991.0 / timer / 48000.0;
                const std::vector<float>& s = c.wave->s;
                for (int i = 0; i < n; i++)
                {
                    if (c.pos >= s.size())
                    {
                        if (!c.wave->loop) { c.stage = 4; break; }
                        c.pos -= (double)(s.size() - c.wave->loopStart);
                    }
                    const size_t a = (size_t)c.pos;
                    const float f = (float)(c.pos - a);
                    const float b = a + 1 < s.size() ? s[a + 1] : c.wave->loop ? s[c.wave->loopStart] : 0.0f;
                    const float x = s[a] * (1 - f) + b * f;
                    mix[i * 2] += x * c.gainL;
                    mix[i * 2 + 1] += x * c.gainR;
                    c.pos += inc;
                }
            }
            for (size_t i = 0; i < mix.size(); i++)
                out[base + i] = (int16_t)std::clamp((int)std::lround(mix[i] * Gain * 32767), -32768, 32767);
            if (!any) break;
        }
        // The silent end left off.
        size_t end = out.size();
        while (end >= 2 && std::abs(out[end - 1]) < 2 && std::abs(out[end - 2]) < 2) end -= 2;
        out.resize(end);
        return out;
    }
};
