#pragma once
// The shortcut wheel: hold Select (Back) for a ring of shortcuts, point at one with the left
// stick, let go to choose it. A tap of Select stays the game's own Select. Which shortcuts
// there are, what each does and whether it can be used now is up to main.cpp; this holds the
// ring's state and draws it.
#include <cmath>
#include <functional>
#include "hudmenu.h"

class ShortcutWheel
{
public:
    enum { Map, Lines, Emotes, Friends, Bag, Party, Menu, Count };
    struct Entry
    {
        const char* label;
        const char* disc; // menu disc sprite ("menu/menu_map")
    };
    static const Entry& Get(int i)
    {
        static const Entry e[Count] = { { "Town Map", "menu/menu_map" }, { "Quick lines", "menu/menu_communicate" },
            { "Emotes", "menu/menu_help" }, { "Friends", "menu/menu_friends" }, { "Bag", "menu/menu_bag" },
            { "Pok\xC3\xA9mon", "menu/menu_party" }, { "Menu", "menu/menu_options" } };
        return e[i];
    }

    // A hook for the quick lines (the together part of the game): called when "Quick lines" is
    // chosen. While it is empty the shortcut is greyed out.
    static std::function<void()>& QuickLinesHook()
    {
        static std::function<void()> hook;
        return hook;
    }

    static constexpr int HoldFrames = 10; // Select held this long opens the ring

    bool open = false;
    int hover = -1;
    bool usable[Count] = {};

    enum Result { None = -1, Tap = -2 };

    // Once a frame. `held`: Select is down; (sx, sy): the left stick. Returns the shortcut
    // chosen as Select is let go, Tap for a short press, else None.
    int Update(bool held, float sx, float sy)
    {
        int result = None;
        if (held)
        {
            frames++;
            if (frames >= HoldFrames) open = true;
            if (open)
            {
                if (sx * sx + sy * sy > 0.35f * 0.35f)
                {
                    // Sector 0 straight up, clockwise.
                    float a = std::atan2(sx, -sy);
                    if (a < 0) a += 6.2831853f;
                    hover = (int)(a / (6.2831853f / Count) + 0.5f) % Count;
                }
                else hover = -1;
            }
        }
        else
        {
            if (open) result = hover >= 0 && usable[hover] ? hover : None;
            else if (frames > 0) result = Tap;
            frames = 0;
            open = false;
            hover = -1;
        }
        return result;
    }

    // The ring over the picture, centred at design position (cx, cy).
    void Draw(Canvas& c, const Ui& ui, Sprites& sp, const RomFont& f, float cx, float cy) const
    {
        FillBlend(c, 0, 0, (float)c.w, (float)c.h, 0x90000000);
        const float r = 270, d = 150;
        for (int i = 0; i < Count; i++)
        {
            const float a = i * 6.2831853f / Count;
            const float x = cx + std::sin(a) * r, y = cy - std::cos(a) * r;
            const bool on = i == hover;
            const float k = on ? 1.25f : 1.0f;
            const std::string disc = Get(i).disc;
            HudSprite(c, ui, sp, on ? disc + "_selected" : disc, x - d * k / 2, y - d * k / 2, d * k, d * k, usable[i] ? 0xFFFFFFFF : 0x70FFFFFF);
            HudText(c, ui, f, x, y + d * k / 2 + 6, 32, Get(i).label, usable[i] ? HudColour::White : 0xFF9AA1AE, 1, on);
        }
        const std::string mid = hover >= 0 ? Get(hover).label : "Shortcuts";
        HudText(c, ui, f, cx, cy - 24, 48, mid, HudColour::White, 1, true);
        if (hover >= 0 && !usable[hover])
            HudText(c, ui, f, cx, cy + 36, 32, "Not now", HudColour::Muted, 1);
    }

private:
    int frames = 0;
};
