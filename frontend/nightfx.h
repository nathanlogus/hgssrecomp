#pragma once
// Night effects over the widescreen world: a glow around lit windows and lamps, warm
// light pooling around them, fireflies over grass and woods, and deeper night colours
// with a vignette. They are worked out at a quarter of the picture's size, as two layers
// the compositor blends over the 3D world (graphics card: multiply, then add; the CPU
// compositor bakes them in with NightFxBake), so their cost does not grow with the
// picture. Text boxes and menus are drawn over them and stay as they are.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// How much of the night effects to show at this time of the game's clock: all of them
// from 20:00 to 4:00, coming in through the evening and going at dawn.
inline float NightStrength(int hour, int minute)
{
    const float t = hour + minute / 60.0f;
    if (t >= 20 || t < 4) return 1;
    if (t >= 17) return 0.25f + 0.75f * (t - 17) / 3;
    if (t < 6) return 1 - (t - 4) / 2;
    return 0;
}

struct NightFly
{
    float u, v;       // place in the wide picture, 0..1
    float brightness; // 0..1
};

struct NightLight
{
    float u, v;   // the lamp's foot in the wide picture, 0..1
    float ru, rv; // half the width and height of the oval its light makes on the ground
    float hu, hv; // its head
};

class NightFx
{
public:
    std::vector<uint32_t> Add, Mul; // ARGB layers of W x H
    int W = 0, H = 0;

    // Works out the layers for a wide picture `px` (top byte 0 where nothing was drawn).
    // The night is a light map in Mul: a dim blue ambient, brought back up to warm lamp
    // light around lamps and lit windows, so the ground there shows its own colours
    // (light added on top would look like fog). Add holds only the glow of the lights.
    void Build(const uint32_t* px, int pw, int ph, float strength, const std::vector<NightFly>& flies,
        const std::vector<NightLight>& lamps)
    {
        const int cell = std::max(2, pw / 320);
        W = (pw + cell - 1) / cell;
        H = (ph + cell - 1) / cell;
        const size_t n = (size_t)W * H;
        const int step = std::max(1, cell / 2); // 2 x 2 samples per cell: lights span several cells
        light.assign(n * 3, 0.0f);
        weight.assign(n, 0.0f);

        // Lights: bright, warm pixels (lit windows, lamps, fire), averaged per cell.
        for (int cy = 0; cy < H; cy++)
            for (int cx = 0; cx < W; cx++)
            {
                float r = 0, g = 0, b = 0, wsum = 0;
                int count = 0;
                for (int y = cy * cell; y < std::min(ph, cy * cell + cell); y += step)
                    for (int x = cx * cell; x < std::min(pw, cx * cell + cell); x += step)
                    {
                        const uint32_t c = px[(size_t)y * pw + x];
                        count++;
                        // Most of the picture is not a light: an integer test first.
                        const int ir = (c >> 16) & 0xFF, ig = (c >> 8) & 0xFF, ib = c & 0xFF;
                        if (!(c >> 24) || ir - ib <= 20 || ir * 3 + ig * 6 + ib <= 1400) continue;
                        const float pr = (float)ir, pg = (float)ig, pb = (float)ib;
                        const float lum = pr * 0.3f + pg * 0.6f + pb * 0.1f, warm = pr - pb;
                        const float l = std::clamp((lum - 140) / 80, 0.0f, 1.0f) * std::clamp((warm - 20) / 40, 0.0f, 1.0f);
                        r += pr * l; g += pg * l; b += pb * l; wsum += l;
                    }
                const size_t i = (size_t)cy * W + cx;
                if (count)
                {
                    light[i * 3] = r / count; light[i * 3 + 1] = g / count; light[i * 3 + 2] = b / count;
                    weight[i] = wsum / count;
                }
            }

        // Glow: the lights blurred a little; pools: blurred a lot and shifted down a
        // little, as lamps light the ground below and in front of them.
        glow = light;
        Blur(glow, W, H, 3, 2);
        Blur(glow, W, H, 3, 2);
        const int PW = (W + 1) / 2, PH = (H + 1) / 2;
        pool.assign((size_t)PW * PH * 3, 0.0f);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                for (int k = 0; k < 3; k++)
                    pool[((size_t)(y / 2) * PW + x / 2) * 3 + k] += light[((size_t)y * W + x) * 3 + k] * 0.25f;
        for (int k = 0; k < 3; k++) Blur(pool, PW, PH, 3, 3);
        Blur(weight, W, H, 1, 2);

        if (vignette.size() != n)
        {
            vignette.resize(n);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                {
                    const float ex = (x + 0.5f) / W * 2 - 1, ey = (y + 0.5f) / H * 2 - 1;
                    vignette[(size_t)y * W + x] = 1 - 0.2f * Smooth(0.6f, 1.3f, std::sqrt(ex * ex * 0.8f + ey * ey));
                }
        }

        // Lamp light on the ground: a bright oval around each lamp's foot with a soft
        // edge, and a faint wider halo.
        lamp.assign(n, 0.0f);
        for (const NightLight& l : lamps)
        {
            const float cx = l.u * W - 0.5f, cy = l.v * H - 0.5f;
            const float rx = std::max(1.0f, l.ru * W), ry = std::max(1.0f, l.rv * H);
            for (int y = std::max(0, (int)(cy - ry * 1.6f)); y <= std::min(H - 1, (int)(cy + ry * 1.6f)); y++)
                for (int x = std::max(0, (int)(cx - rx * 1.6f)); x <= std::min(W - 1, (int)(cx + rx * 1.6f)); x++)
                {
                    const float dx = (x - cx) / rx, dy = (y - cy) / ry, d = std::sqrt(dx * dx + dy * dy);
                    const float core = 1 - Smooth(0.25f, 1.0f, d);
                    lamp[(size_t)y * W + x] += core * 0.9f + 0.2f * std::max(0.0f, 1 - d / 1.6f);
                }
        }

        Add.assign(n, 0xFF000000);
        Mul.assign(n, 0xFFFFFFFF);
        const int shift = std::max(1, H / 60);
        const float warm[3] = { 1.0f, 0.84f, 0.58f }; // lamp light, as a share of daylight
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                const size_t i = (size_t)y * W + x;
                const size_t ip = (size_t)(std::max(0, y - shift) / 2) * PW + x / 2;
                float a[3];
                for (int k = 0; k < 3; k++) a[k] = strength * glow[i * 3 + k] * 1.2f;
                a[0] *= 1.1f; a[2] *= 0.8f;
                Add[i] = 0xFF000000 | (Clamp8(a[0]) << 16) | (Clamp8(a[1]) << 8) | Clamp8(a[2]);

                // Night: a dim blue ambient, a little darker towards the edges; the lamps'
                // ovals and the light spilling below lit windows bring it back up to warm
                // light. Lit windows and lamps themselves keep their own colours.
                const float vig = vignette[i];
                const float keep = std::clamp(weight[i] * 6, 0.0f, 1.0f);
                const float ambient[3] = { 0.40f * vig, 0.45f * vig, 0.70f * (0.85f + 0.15f * vig) };
                const float lit = std::min(1.0f, lamp[i]);
                uint32_t mc = 0xFF000000;
                for (int k = 0; k < 3; k++)
                {
                    const float spill = pool[ip * 3 + k] * 3.0f / 255 * (k == 2 ? 0.7f : 1.0f);
                    const float target = std::min(1.0f, ambient[k] + (warm[k] - ambient[k]) * lit + spill);
                    const float v = 1 + (target - 1) * strength * (1 - keep);
                    mc |= Clamp8(v * 255) << (16 - 8 * k);
                }
                Mul[i] = mc;
            }

        // Lamp heads: a small warm glow.
        for (const NightLight& l : lamps)
        {
            const float hx = l.hu * W - 0.5f, hy = l.hv * H - 0.5f, r = std::max(1.2f, l.ru * W * 0.2f);
            for (int y = std::max(0, (int)(hy - 2.5f * r)); y <= std::min(H - 1, (int)(hy + 2.5f * r)); y++)
                for (int x = std::max(0, (int)(hx - 2.5f * r)); x <= std::min(W - 1, (int)(hx + 2.5f * r)); x++)
                {
                    const float d2 = ((x - hx) * (x - hx) + (y - hy) * (y - hy)) / (r * r);
                    const float k = strength * std::exp(-d2);
                    uint32_t& c = Add[(size_t)y * W + x];
                    const uint32_t cr = std::min(255u, ((c >> 16) & 0xFF) + (uint32_t)(120 * k));
                    const uint32_t cg = std::min(255u, ((c >> 8) & 0xFF) + (uint32_t)(100 * k));
                    const uint32_t cb = std::min(255u, (c & 0xFF) + (uint32_t)(60 * k));
                    c = 0xFF000000 | (cr << 16) | (cg << 8) | cb;
                }
        }

        // Fireflies: small soft dots, yellow-green, added on top.
        for (const NightFly& f : flies)
        {
            const float fx = f.u * W - 0.5f, fy = f.v * H - 0.5f;
            const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
            for (int dy = -1; dy <= 2; dy++)
                for (int dx = -1; dx <= 2; dx++)
                {
                    const int x = x0 + dx, y = y0 + dy;
                    if (x < 0 || y < 0 || x >= W || y >= H) continue;
                    const float dist2 = (x - fx) * (x - fx) + (y - fy) * (y - fy);
                    const float k = f.brightness * strength * std::exp(-dist2 * 1.3f);
                    uint32_t& c = Add[(size_t)y * W + x];
                    const uint32_t r = std::min(255u, ((c >> 16) & 0xFF) + (uint32_t)(200 * k));
                    const uint32_t g = std::min(255u, ((c >> 8) & 0xFF) + (uint32_t)(255 * k));
                    const uint32_t b = std::min(255u, (c & 0xFF) + (uint32_t)(110 * k));
                    c = 0xFF000000 | (r << 16) | (g << 8) | b;
                }
        }
    }

private:
    std::vector<float> light, weight, glow, pool, vignette, lamp;

    static uint32_t Clamp8(float v) { return (uint32_t)std::clamp(v, 0.0f, 255.0f); }
    static float Smooth(float a, float b, float x)
    {
        const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    }

    // Box blur of a w x h grid of `channels` floats per cell, radius r, along the rows
    // and then the columns, with running sums.
    void Blur(std::vector<float>& v, int w, int h, int channels, int r)
    {
        tmp.resize(v.size());
        const float scale = 1.0f / (2 * r + 1);
        for (int pass = 0; pass < 2; pass++)
        {
            const int len = pass ? h : w, lines = pass ? w : h;
            const size_t stride = pass ? (size_t)w * channels : channels; // between cells along a line
            const size_t lineStep = pass ? channels : (size_t)w * channels;
            for (int line = 0; line < lines; line++)
                for (int c = 0; c < channels; c++)
                {
                    const float* in = v.data() + line * lineStep + c;
                    float* out = tmp.data() + line * lineStep + c;
                    float sum = in[0] * (r + 1);
                    for (int i = 1; i <= r; i++) sum += in[std::min(i, len - 1) * stride];
                    for (int i = 0; i < len; i++)
                    {
                        out[i * stride] = sum * scale;
                        sum += in[std::min(len - 1, i + r + 1) * stride] - in[std::max(0, i - r) * stride];
                    }
                }
            v.swap(tmp);
        }
    }
    std::vector<float> tmp;
};

// The CPU compositor's version of the blending the graphics card does: the picture
// multiplied by Mul and then Add added, both stretched smoothly over it.
inline void NightFxBake(const uint32_t* px, int pw, int ph, const NightFx& fx, std::vector<uint32_t>& out)
{
    out.resize((size_t)pw * ph);
    auto sample = [&](const std::vector<uint32_t>& layer, float u, float v, float c[3]) {
        const float x = std::clamp(u * fx.W - 0.5f, 0.0f, (float)(fx.W - 1)), y = std::clamp(v * fx.H - 0.5f, 0.0f, (float)(fx.H - 1));
        const int x0 = (int)x, y0 = (int)y, x1 = std::min(fx.W - 1, x0 + 1), y1 = std::min(fx.H - 1, y0 + 1);
        const float ax = x - x0, ay = y - y0;
        for (int k = 0; k < 3; k++)
        {
            const int sh = 16 - 8 * k;
            auto ch = [&](int xx, int yy) { return (float)((layer[(size_t)yy * fx.W + xx] >> sh) & 0xFF); };
            c[k] = (ch(x0, y0) * (1 - ax) + ch(x1, y0) * ax) * (1 - ay) + (ch(x0, y1) * (1 - ax) + ch(x1, y1) * ax) * ay;
        }
    };
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++)
        {
            const uint32_t c = px[(size_t)y * pw + x];
            float m[3], a[3];
            const float u = (x + 0.5f) / pw, v = (y + 0.5f) / ph;
            sample(fx.Mul, u, v, m);
            sample(fx.Add, u, v, a);
            uint32_t o = 0xFF000000;
            for (int k = 0; k < 3; k++)
            {
                const int sh = 16 - 8 * k;
                const float base = (c >> 24) ? (float)((c >> sh) & 0xFF) : 0.0f;
                o |= (uint32_t)std::min(255.0f, base * m[k] / 255 + a[k]) << sh;
            }
            out[(size_t)y * pw + x] = o;
        }
}
