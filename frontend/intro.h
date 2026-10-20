#pragma once
// The title screen's video: assets/animations/intro.mpg (MPEG-1, without sound; the README
// says how to make one from another video), played full-screen in a loop behind the logo
// before the start screen. Decoded with pl_mpeg (vendored, MIT) as time goes by. The video
// is the player's own and never part of the project.
#include <cmath>
#include <cstdint>
#include <vector>
#include "hud.h"
#include "thirdparty/pl_mpeg.h"

class IntroVideo
{
public:
    // Where the video leaves room for the logo, as fractions of its picture (the glow on the
    // left, above its own "Press A"): the logo's centre and width.
    static constexpr float LogoX = 0.195f, LogoY = 0.47f, LogoW = 0.33f;
    // The first second fades in from white; the loop starts after it.
    static constexpr double LoopFrom = 1.0;

    ~IntroVideo()
    {
        if (plm) plm_destroy(plm);
    }

    bool Load(std::vector<uint8_t> file)
    {
        if (file.empty()) return false;
        data = std::move(file);
        plm = plm_create_with_memory(data.data(), data.size(), 0);
        if (!plm || plm_get_width(plm) <= 0 || plm_get_height(plm) <= 0)
        {
            if (plm) plm_destroy(plm);
            plm = nullptr;
            return false;
        }
        plm_set_audio_enabled(plm, 0);
        frame.w = plm_get_width(plm);
        frame.h = plm_get_height(plm);
        frame.px.assign((size_t)frame.w * frame.h, 0xFF000000);
        duration = plm_get_duration(plm);
        return true;
    }
    bool Ok() const { return plm != nullptr; }

    // The picture `seconds` after the video started (looping), filling the canvas: cropped to
    // its shape and scaled to the nearest pixel (a smoothed scale of a whole screen each frame
    // would cost a good part of a frame).
    void Draw(Canvas& c, double seconds)
    {
        if (!plm || c.w <= 0 || c.h <= 0) return;
        const double t = seconds < duration || duration <= LoopFrom ? seconds : LoopFrom + std::fmod(seconds - duration, duration - LoopFrom);
        if (t + 0.5 < shownTime)
        {
            // Looped: back to the start of the loop.
            plm_seek(plm, t, 0);
            shownTime = -1;
        }
        while (shownTime < t)
        {
            plm_frame_t* f = plm_decode_video(plm);
            if (!f)
            {
                plm_seek(plm, LoopFrom, 0);
                shownTime = -1;
                break;
            }
            shownTime = f->time;
            plm_frame_to_bgra(f, (uint8_t*)frame.px.data(), frame.w * 4);
        }
        Cover(c.w, c.h);
        if (cols.size() != (size_t)c.w || rowsMap.size() != (size_t)c.h) return;
        for (int y = 0; y < c.h; y++)
        {
            const uint32_t* src = &frame.px[(size_t)rowsMap[y] * frame.w];
            uint32_t* dst = &c.px[(size_t)y * c.w];
            for (int x = 0; x < c.w; x++) dst[x] = 0xFF000000 | src[cols[x]];
        }
        c.Mark(0, 0, c.w, c.h);
    }

    // The logo's place on a canvas of this size, in canvas pixels.
    void LogoPlace(int w, int h, float& cx, float& cy, float& width) const
    {
        const float k = std::max((float)w / frame.w, (float)h / frame.h);
        const float ox = (w - frame.w * k) / 2, oy = (h - frame.h * k) / 2;
        cx = ox + LogoX * frame.w * k;
        cy = oy + LogoY * frame.h * k;
        width = LogoW * frame.w * k;
    }

private:
    std::vector<uint8_t> data;
    plm_t* plm = nullptr;
    Image frame;
    double duration = 0, shownTime = -1;
    std::vector<int> cols, rowsMap;

    // Which picture pixel each canvas column and row shows (covering the canvas).
    void Cover(int w, int h)
    {
        if (cols.size() == (size_t)w && rowsMap.size() == (size_t)h) return;
        const float k = std::max((float)w / frame.w, (float)h / frame.h);
        const float ox = (w - frame.w * k) / 2, oy = (h - frame.h * k) / 2;
        cols.resize(w);
        rowsMap.resize(h);
        for (int x = 0; x < w; x++) cols[x] = std::clamp((int)((x + 0.5f - ox) / k), 0, frame.w - 1);
        for (int y = 0; y < h; y++) rowsMap[y] = std::clamp((int)((y + 0.5f - oy) / k), 0, frame.h - 1);
    }
};
