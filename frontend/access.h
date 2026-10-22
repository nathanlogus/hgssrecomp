#pragma once
// Comfort and accessibility options: colour vision filters, high contrast, a limit on
// flashes, less motion, bigger text, the separate volumes, battle extras and the low-power
// preset. The settings live in AccessCfg (read and written by the frontend's Settings).
// The picture filters work on the finished picture (game, widescreen world and HUD together),
// so the window and the screenshots of the headless tests are filtered by the same code.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// Read by the drawing code: the HUD's text is drawn this much larger (hud.h RomFont), and the
// HUD's own slides and the fireflies are left out.
inline float g_textBoost = 1.0f;
inline bool g_reduceMotion = false;

struct AccessCfg
{
    int colour = 0;         // 0 off, 1 protanopia, 2 deuteranopia, 3 tritanopia, 4 monochrome
    int strength = 100;     // percent
    bool contrast = false;  // high contrast
    bool flashLimit = false; // a sudden change of brightness is spread over several pictures
    bool lessMotion = false;
    int textSize = 0;       // 0 normal, 1 large, 2 larger
    bool foePercent = false; // the foe's HP as a percentage in a battle
    int beep = 100;         // low health beep volume, percent (0 off)
    int volMusic = 100, volSfx = 100, volCry = 100; // percent
    int limit = 0;          // 0: a picture for each of the game's frames, 1: every second one
    bool potato = false;    // low-power preset on
    std::string potatoPrev; // what it changed, to put back
    int wheel = 1;          // shortcut wheel on Select held

    static constexpr int TextSizes = 3;
    static float TextBoost(int s) { return s == 1 ? 1.2f : s == 2 ? 1.4f : 1.0f; }
    void Apply() const
    {
        g_textBoost = TextBoost(textSize);
        g_reduceMotion = lessMotion;
    }

    // True for a key this struct owns.
    bool Load(const std::string& k, const std::string& v)
    {
        auto num = [&](int lo, int hi) { return std::clamp(atoi(v.c_str()), lo, hi); };
        if (k == "colour_filter") colour = num(0, 4);
        else if (k == "colour_strength") strength = num(0, 100);
        else if (k == "high_contrast") contrast = v == "1";
        else if (k == "flash_limit") flashLimit = v == "1";
        else if (k == "less_motion") lessMotion = v == "1";
        else if (k == "text_size") textSize = num(0, TextSizes - 1);
        else if (k == "foe_percent") foePercent = v == "1";
        else if (k == "low_hp_beep") beep = num(0, 100);
        else if (k == "volume_music") volMusic = num(0, 100);
        else if (k == "volume_sfx") volSfx = num(0, 100);
        else if (k == "volume_cries") volCry = num(0, 100);
        else if (k == "picture_limit") limit = num(0, 1);
        else if (k == "low_power") potato = v == "1";
        else if (k == "low_power_prev") potatoPrev = v;
        else if (k == "shortcut_wheel") wheel = num(0, 1);
        else return false;
        return true;
    }
    void Save(std::ostream& f) const
    {
        f << "colour_filter=" << colour << "\ncolour_strength=" << strength << "\nhigh_contrast=" << contrast
          << "\nflash_limit=" << flashLimit << "\nless_motion=" << lessMotion << "\ntext_size=" << textSize
          << "\nfoe_percent=" << foePercent << "\nlow_hp_beep=" << beep << "\nvolume_music=" << volMusic
          << "\nvolume_sfx=" << volSfx << "\nvolume_cries=" << volCry << "\npicture_limit=" << limit
          << "\nlow_power=" << potato << "\nlow_power_prev=" << potatoPrev << "\nshortcut_wheel=" << wheel << "\n";
    }
};

class AccessFilter
{
public:
    static bool Active(const AccessCfg& c) { return c.colour || c.contrast || c.flashLimit; }

    // Filters a finished ARGB picture in place. Call it for every picture shown (the flash
    // limit follows one picture to the next).
    void Apply(const AccessCfg& c, uint32_t* px, int w, int h)
    {
        if (w <= 0 || h <= 0) return;
        if (c.flashLimit) Limit(px, w, h);
        if (c.colour) Colour(c, px, (size_t)w * h);
        if (c.contrast) Contrast(px, (size_t)w * h);
    }

    // The matrix (rows of R, G, B in thousandths) the colour filter multiplies each pixel with.
    static void Matrix(int mode, int strength, int m[9])
    {
        // How a person with the condition sees the colours (Machado et al. 2009, full severity).
        static const float sim[3][9] = {
            { 0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f, 1.051998f },
            { 0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f, 0.968881f },
            { 1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f, 0.303900f } };
        // Where the colour that is lost is shown instead (Fidaner's daltonisation).
        static const float shift[3][9] = {
            { 0, 0, 0, 0.7f, 1, 0, 0.7f, 0, 1 }, { 0, 0, 0, 0.7f, 1, 0, 0.7f, 0, 1 }, { 1, 0, 0.7f, 0, 1, 0.7f, 0, 0, 0 } };
        float t[9];
        if (mode == 4)
            for (int r = 0; r < 3; r++) { t[r * 3] = 0.299f; t[r * 3 + 1] = 0.587f; t[r * 3 + 2] = 0.114f; }
        else
        {
            const float* s = sim[mode - 1];
            const float* a = shift[mode - 1];
            // t = I + A (I - S)
            float d[9];
            for (int i = 0; i < 9; i++) d[i] = (i % 4 == 0 ? 1.0f : 0.0f) - s[i];
            for (int r = 0; r < 3; r++)
                for (int col = 0; col < 3; col++)
                {
                    float v = r == col ? 1.0f : 0.0f;
                    for (int k = 0; k < 3; k++) v += a[r * 3 + k] * d[k * 3 + col];
                    t[r * 3 + col] = v;
                }
        }
        const float k = std::clamp(strength, 0, 100) / 100.0f;
        for (int i = 0; i < 9; i++) m[i] = (int)std::lround(((i % 4 == 0 ? 1.0f : 0.0f) + k * (t[i] - (i % 4 == 0 ? 1.0f : 0.0f))) * 1000);
    }

private:
    float prevLuma = -1;

    static float Luma(uint32_t p) { return (0.299f * ((p >> 16) & 0xFF) + 0.587f * ((p >> 8) & 0xFF) + 0.114f * (p & 0xFF)) / 255.0f; }

    // At most this much of a change in the picture's average brightness from one picture to
    // the next: a bigger change is shown as a mix of the picture with the last brightness.
    void Limit(uint32_t* px, int w, int h)
    {
        double sum = 0;
        int n = 0;
        for (int y = 0; y < h; y += 8)
            for (int x = 0; x < w; x += 8, n++) sum += Luma(px[(size_t)y * w + x]);
        const float m = n ? (float)(sum / n) : 0.0f;
        if (prevLuma < 0) prevLuma = m;
        const float step = 0.12f;
        const float target = std::clamp(m, prevLuma - step, prevLuma + step);
        if (target != m)
        {
            const float k = (target - prevLuma) / (m - prevLuma); // how much of the picture is kept
            const int grey = (int)(prevLuma * 255), ki = (int)(k * 256);
            for (size_t i = 0, e = (size_t)w * h; i < e; i++)
            {
                const uint32_t p = px[i];
                const int r = (((p >> 16) & 0xFF) * ki + grey * (256 - ki)) >> 8, g = (((p >> 8) & 0xFF) * ki + grey * (256 - ki)) >> 8,
                          b = ((p & 0xFF) * ki + grey * (256 - ki)) >> 8;
                px[i] = 0xFF000000 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
            }
        }
        prevLuma = target;
    }

    static void Colour(const AccessCfg& c, uint32_t* px, size_t n)
    {
        int m[9];
        Matrix(c.colour, c.strength, m);
        for (size_t i = 0; i < n; i++)
        {
            const uint32_t p = px[i];
            const int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            const int nr = std::clamp((m[0] * r + m[1] * g + m[2] * b) / 1000, 0, 255);
            const int ng = std::clamp((m[3] * r + m[4] * g + m[5] * b) / 1000, 0, 255);
            const int nb = std::clamp((m[6] * r + m[7] * g + m[8] * b) / 1000, 0, 255);
            px[i] = (p & 0xFF000000) | (uint32_t)nr << 16 | (uint32_t)ng << 8 | (uint32_t)nb;
        }
    }

    // Darks darker and lights lighter: the difference between neighbouring colours grows.
    static void Contrast(uint32_t* px, size_t n)
    {
        static uint8_t lut[256];
        static bool made = false;
        if (!made)
        {
            for (int v = 0; v < 256; v++) lut[v] = (uint8_t)std::clamp((int)std::lround((v - 128) * 1.6f + 128), 0, 255);
            made = true;
        }
        for (size_t i = 0; i < n; i++)
        {
            const uint32_t p = px[i];
            px[i] = (p & 0xFF000000) | (uint32_t)lut[(p >> 16) & 0xFF] << 16 | (uint32_t)lut[(p >> 8) & 0xFF] << 8 | lut[p & 0xFF];
        }
    }
};
