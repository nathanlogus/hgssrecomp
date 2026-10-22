#pragma once
// Toasts: short messages in white pills along the top left (online-HUD design, 07-toasts),
// the newest at the top, at most four at a time. Each can start with a player's chip (their
// initials in a ring of their colour) or a sprite, have the player's name in their colour,
// and end with a sprite. Toasts about other players wait while a battle, a dialogue or a menu
// is on screen and are shown afterwards; the frontend's own messages are shown at once.
#include <deque>
#include "hudmenu.h"

struct Toast
{
    std::string text;             // after the name
    std::string name;             // a player's name, drawn in `nameColour` (may be empty)
    uint32_t nameColour = 0xFF3B3F4A;
    std::string chip;             // a player's initials in a chip ring of `chipRing` ("people/chip_ring_green")
    std::string chipRing;
    Image face;                   // their face (FaceCrop) in the ring instead of the initials, if any
    std::string lead, trail;     // sprites before the text (instead of a chip) and after it
    Image trailArt;               // or a picture from the ROM after it (a Pokemon's icon)
    bool dashed = false;          // reconnecting: a grey pill
    bool waits = false;           // held back while the player is busy (see ToastStack::Update)
    uint32_t frames = 150;        // how long it stays
};

class ToastStack
{
public:
    void Push(const Toast& t) { queue.push_back(t); }

    // Moves waiting toasts on screen: those that wait only while `busy` is false.
    void Update(uint32_t frame, bool busy)
    {
        while (!shown.empty() && frame >= shown.back().until) shown.pop_back();
        for (auto it = queue.begin(); it != queue.end();)
        {
            if (it->waits && busy) { ++it; continue; }
            Shown s;
            s.t = *it;
            s.start = frame;
            s.until = frame + it->frames;
            shown.push_front(s);
            it = queue.erase(it);
        }
    }

    bool Any() const { return !shown.empty(); }

    // Draws up to `max` toasts from design position (x, y) down.
    void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, uint32_t frame, float x, float y, int max) const
    {
        int k = 0;
        for (const Shown& s : shown)
        {
            if (k++ >= max) break;
            // Slides in from the left over 8 frames; fades out over the last 15.
            const float in = g_reduceMotion ? 1.0f : std::min(1.0f, (frame - s.start) / 8.0f);
            const float out = std::min(1.0f, (s.until - frame) / 15.0f);
            const uint32_t a = (uint32_t)(255 * std::min(in, out));
            const uint32_t tint = a << 24 | 0xFFFFFF;
            DrawOne(c, ui, sp, f, s.t, x - (1 - in) * 40, y, tint);
            y += 80;
        }
    }

private:
    struct Shown
    {
        Toast t;
        uint32_t start = 0, until = 0;
    };
    std::deque<Toast> queue;
    std::deque<Shown> shown;

    static uint32_t Fade(uint32_t colour, uint32_t tint) { return Tint(colour, tint | 0xFFFFFF); }

    static void DrawOne(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, const Toast& t, float x, float y, uint32_t tint)
    {
        const float h = 64;
        float w = 8 + 24;
        const bool hasLead = !t.chip.empty() || !t.lead.empty();
        if (hasLead) w += 52 + 12;
        w += HudTextWidth(ui, f, 32, t.name + t.text);
        if (!t.trail.empty() || !t.trailArt.px.empty()) w += 12 + 48;
        HudPanel(c, ui, sp, "panels/toast", x, y, w, h, t.dashed ? Tint(0xFFE6E9EE, tint) : tint);
        float cx = x + 8;
        if (!t.chip.empty())
        {
            HudSprite(c, ui, sp, t.chipRing, cx, y + 6, 52, 52, tint);
            if (!t.face.px.empty()) DrawRingFace(c, t.face, ui(cx), ui(y + 6), ui(52), tint);
            else HudText(c, ui, f, cx + 26, y + 6 + 10, 32, t.chip, Fade(HudColour::Ink, tint), 1);
            cx += 52 + 12;
        }
        else if (!t.lead.empty())
        {
            HudSprite(c, ui, sp, t.lead, cx, y + 8, 52, 48, tint);
            cx += 52 + 12;
        }
        else
            cx += 16;
        const float ty = y + (h - 32) / 2;
        if (!t.name.empty())
        {
            HudText(c, ui, f, cx, ty, 32, t.name, Fade(t.nameColour, tint));
            cx += HudTextWidth(ui, f, 32, t.name);
        }
        HudText(c, ui, f, cx, ty, 32, t.text, Fade(HudColour::Ink, tint));
        cx += HudTextWidth(ui, f, 32, t.text);
        if (!t.trailArt.px.empty())
            Blit(c, t.trailArt, 0, 0, (float)t.trailArt.w, (float)t.trailArt.h, ui(cx + 8), ui(y), ui(64), ui(64), false, tint);
        else if (!t.trail.empty())
            HudSprite(c, ui, sp, t.trail, cx + 12, y + 8, 48, 48, tint);
    }
};

// A pill with a short text in a corner (the FPS counter, fast forward): right-aligned at x.
inline float DrawCornerPill(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float right, float y, const std::string& text)
{
    const float w = HudTextWidth(ui, f, 32, text) + 40, h = 52;
    HudPanel(c, ui, sp, "panels/toast", right - w, y, w, h);
    HudText(c, ui, f, right - w / 2, y + (h - 32) / 2, 32, text, HudColour::Ink, 1);
    return y + h + 16;
}
