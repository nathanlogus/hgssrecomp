#pragma once
// Focus blur over the widescreen world ("tilt-shift", as in Link's Awakening): a round
// area around the player stays sharp and the world softens towards the edges, which
// makes it look like a model. A quarter-size blurred copy of the world, opaque towards
// the edges and clear in the middle, is drawn over it (graphics card: alpha blending;
// the CPU compositor bakes it in with FocusBlurBake). The camera looks at the player, so
// the sharp area is centred on the picture. Text boxes and menus stay sharp.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

class FocusBlur
{
public:
    std::vector<uint32_t> Layer; // ARGB, W x H: blurred world, alpha = how much of it shows
    int W = 0, H = 0;

    // level 1: light (a large sharp area, soft edges), 2: strong.
    void Build(const uint32_t* px, int pw, int ph, int level)
    {
        const int cell = std::max(2, pw / 320);
        W = (pw + cell - 1) / cell;
        H = (ph + cell - 1) / cell;
        const size_t n = (size_t)W * H;
        const int step = std::max(1, cell / 2);
        rgb.assign(n * 3, 0.0f);
        for (int cy = 0; cy < H; cy++)
            for (int cx = 0; cx < W; cx++)
            {
                float r = 0, g = 0, b = 0;
                int count = 0;
                for (int y = cy * cell; y < std::min(ph, cy * cell + cell); y += step)
                    for (int x = cx * cell; x < std::min(pw, cx * cell + cell); x += step)
                    {
                        const uint32_t c = px[(size_t)y * pw + x];
                        count++;
                        if (!(c >> 24)) continue; // nothing drawn: black
                        r += (c >> 16) & 0xFF;
                        g += (c >> 8) & 0xFF;
                        b += c & 0xFF;
                    }
                float* o = &rgb[((size_t)cy * W + cx) * 3];
                if (count) { o[0] = r / count; o[1] = g / count; o[2] = b / count; }
            }
        // The quarter size is already soft; light adds a little, strong more.
        for (int k = 0; k < level; k++) Blur(1);

        // Sharp inside a circle (in units of the picture's height), fully blurred at the
        // far edge of the ramp.
        const float sharp = level >= 2 ? 0.28f : 0.45f, ramp = level >= 2 ? 0.42f : 0.5f;
        Layer.resize(n);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                const float dx = ((x + 0.5f) / W - 0.5f) * W / H, dy = (y + 0.5f) / H - 0.5f;
                const float t = std::clamp((std::sqrt(dx * dx + dy * dy) - sharp) / ramp, 0.0f, 1.0f);
                const uint32_t a = (uint32_t)(255 * t * t * (3 - 2 * t));
                const float* c = &rgb[((size_t)y * W + x) * 3];
                Layer[(size_t)y * W + x] = (a << 24) | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | (uint32_t)c[2];
            }
    }

private:
    std::vector<float> rgb, tmp;

    // Box blur of radius r on the three channels, along the rows and then the columns.
    void Blur(int r)
    {
        tmp.resize(rgb.size());
        const float scale = 1.0f / (2 * r + 1);
        for (int pass = 0; pass < 2; pass++)
        {
            const int len = pass ? H : W, lines = pass ? W : H;
            const size_t stride = pass ? (size_t)W * 3 : 3, lineStep = pass ? 3 : (size_t)W * 3;
            for (int line = 0; line < lines; line++)
                for (int c = 0; c < 3; c++)
                {
                    const float* in = rgb.data() + line * lineStep + c;
                    float* out = tmp.data() + line * lineStep + c;
                    float sum = in[0] * (r + 1);
                    for (int i = 1; i <= r; i++) sum += in[std::min(i, len - 1) * stride];
                    for (int i = 0; i < len; i++)
                    {
                        out[i * stride] = sum * scale;
                        sum += in[std::min(len - 1, i + r + 1) * stride] - in[std::max(0, i - r) * stride];
                    }
                }
            rgb.swap(tmp);
        }
    }
};

// The CPU compositor's version of the blending: the layer stretched smoothly over the
// picture, mixed in by its alpha.
inline void FocusBlurBake(const uint32_t* px, int pw, int ph, const FocusBlur& fb, std::vector<uint32_t>& out)
{
    out.resize((size_t)pw * ph);
    for (int y = 0; y < ph; y++)
    {
        const float fy = std::clamp((y + 0.5f) / ph * fb.H - 0.5f, 0.0f, (float)(fb.H - 1));
        const int y0 = (int)fy, y1 = std::min(fb.H - 1, y0 + 1);
        const float ay = fy - y0;
        for (int x = 0; x < pw; x++)
        {
            const float fx = std::clamp((x + 0.5f) / pw * fb.W - 0.5f, 0.0f, (float)(fb.W - 1));
            const int x0 = (int)fx, x1 = std::min(fb.W - 1, x0 + 1);
            const float ax = fx - x0;
            float v[4];
            for (int k = 0; k < 4; k++)
            {
                const int sh = 24 - 8 * k;
                auto ch = [&](int xx, int yy) { return (float)((fb.Layer[(size_t)yy * fb.W + xx] >> sh) & 0xFF); };
                v[k] = (ch(x0, y0) * (1 - ax) + ch(x1, y0) * ax) * (1 - ay) + (ch(x0, y1) * (1 - ax) + ch(x1, y1) * ax) * ay;
            }
            const uint32_t c = px[(size_t)y * pw + x];
            const float a = v[0] / 255;
            uint32_t o = 0xFF000000;
            for (int k = 0; k < 3; k++)
            {
                const int sh = 16 - 8 * k;
                const float base = (c >> 24) ? (float)((c >> sh) & 0xFF) : 0.0f;
                o |= (uint32_t)(base + (v[k + 1] - base) * a) << sh;
            }
            out[(size_t)y * pw + x] = o;
        }
    }
}
