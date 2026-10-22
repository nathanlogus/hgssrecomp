#pragma once
// Keyboard keys for the DS buttons: a few layouts and keys of the player's own. Keys are
// kept as SDL scancodes (the key's place on the keyboard), so a layout made of the keys beside
// each other stays so on a QWERTZ or AZERTY keyboard; the names shown are those the player's
// own layout prints on them.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <SDL.h>

struct KeyBinds
{
    // The DS buttons in the order of this table (not the order of the DS's key register).
    static constexpr int N = 12;
    static constexpr const char* Names[N] = { "A", "B", "X", "Y", "L", "R", "START", "SELECT", "UP", "DOWN", "LEFT", "RIGHT" };
    static constexpr int A = 0, B = 1, L = 4, R = 5, Up = 8, Down = 9, Left = 10, Right = 11;
    static constexpr int Layouts = 4; // the last, Custom, is what rebinding makes
    static const char* LayoutName(int l)
    {
        static const char* n[Layouts] = { "Classic", "WASD", "Any keyboard", "Custom" };
        return n[l];
    }

    int sc[N][2] = {}; // two keys for each button, 0 for none
    int layout = 0;

    KeyBinds() { Preset(0); }

    void Preset(int l)
    {
        layout = l;
        for (auto& b : sc) b[0] = b[1] = 0;
        auto set = [&](int i, SDL_Scancode a, SDL_Scancode b = SDL_SCANCODE_UNKNOWN) { sc[i][0] = a; sc[i][1] = b; };
        // The Classic layout is the one before there were any: the arrows and X Z S A Q W.
        // WASD and Any keyboard leave the arrows on as well. "Any keyboard" is made of keys
        // that sit in the same place on QWERTY, QWERTZ and AZERTY (no A, Q, W, Y or Z).
        set(Up, SDL_SCANCODE_UP, l == 1 ? SDL_SCANCODE_W : SDL_SCANCODE_UNKNOWN);
        set(Down, SDL_SCANCODE_DOWN, l == 1 ? SDL_SCANCODE_S : SDL_SCANCODE_UNKNOWN);
        set(Left, SDL_SCANCODE_LEFT, l == 1 ? SDL_SCANCODE_A : SDL_SCANCODE_UNKNOWN);
        set(Right, SDL_SCANCODE_RIGHT, l == 1 ? SDL_SCANCODE_D : SDL_SCANCODE_UNKNOWN);
        set(6, SDL_SCANCODE_RETURN);
        set(7, l == 1 || l == 2 ? SDL_SCANCODE_BACKSPACE : SDL_SCANCODE_RSHIFT, l == 1 || l == 2 ? SDL_SCANCODE_RSHIFT : SDL_SCANCODE_BACKSPACE);
        if (l == 1)
        {
            set(0, SDL_SCANCODE_E); set(1, SDL_SCANCODE_Q); set(2, SDL_SCANCODE_R); set(3, SDL_SCANCODE_F);
            set(4, SDL_SCANCODE_Z); set(5, SDL_SCANCODE_C);
        }
        else if (l == 2)
        {
            set(0, SDL_SCANCODE_X); set(1, SDL_SCANCODE_C); set(2, SDL_SCANCODE_V); set(3, SDL_SCANCODE_B);
            set(4, SDL_SCANCODE_D); set(5, SDL_SCANCODE_F);
        }
        else
        {
            set(0, SDL_SCANCODE_X); set(1, SDL_SCANCODE_Z); set(2, SDL_SCANCODE_S); set(3, SDL_SCANCODE_A);
            set(4, SDL_SCANCODE_Q); set(5, SDL_SCANCODE_W);
        }
    }

    // The buttons held now, as bits of the DS's key register: bit[i] for button i of Names.
    uint32_t Held(const Uint8* st, const int (&bit)[N]) const
    {
        uint32_t k = 0;
        for (int i = 0; i < N; i++)
            if ((sc[i][0] && st[sc[i][0]]) || (sc[i][1] && st[sc[i][1]])) k |= 1u << bit[i];
        return k;
    }

    // The button a key is bound to, or -1.
    int Find(int scancode) const
    {
        for (int i = 0; i < N; i++)
            if (scancode && (sc[i][0] == scancode || sc[i][1] == scancode)) return i;
        return -1;
    }

    // Makes `scancode` the only key of button i (another button that had it loses it).
    void Rebind(int i, int scancode)
    {
        for (auto& b : sc)
            for (int& s : b)
                if (s == scancode) s = 0;
        sc[i][0] = scancode;
        sc[i][1] = 0;
        layout = Layouts - 1;
    }

    static std::string KeyName(int scancode)
    {
        if (!scancode) return "";
        std::string n = SDL_GetKeyName(SDL_GetKeyFromScancode((SDL_Scancode)scancode));
        // (Without a keyboard layout, as in the tests: the keys of a US one.)
        if (n.empty() && scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) n = std::string(1, (char)('A' + scancode - SDL_SCANCODE_A));
        if (n.empty() && scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) n = std::string(1, (char)('1' + scancode - SDL_SCANCODE_1));
        if (n.empty())
            switch (scancode)
            {
            case SDL_SCANCODE_RETURN: n = "Return"; break;
            case SDL_SCANCODE_BACKSPACE: n = "Backspace"; break;
            case SDL_SCANCODE_RSHIFT: n = "Right Shift"; break;
            case SDL_SCANCODE_UP: n = "Up"; break;
            case SDL_SCANCODE_DOWN: n = "Down"; break;
            case SDL_SCANCODE_LEFT: n = "Left"; break;
            case SDL_SCANCODE_RIGHT: n = "Right"; break;
            default: n = "Key " + std::to_string(scancode);
            }
        for (char& c : n) c = (char)toupper((unsigned char)c);
        return n;
    }
    // Both keys of a button ("X" or "BACKSPACE / RIGHT SHIFT"), "-" for none.
    std::string Names2(int i) const
    {
        std::string s = KeyName(sc[i][0]);
        if (sc[i][1]) s += (s.empty() ? "" : " / ") + KeyName(sc[i][1]);
        return s.empty() ? "-" : s;
    }

    bool Load(const std::string& k, const std::string& v)
    {
        if (k == "keys_layout")
            layout = std::clamp(atoi(v.c_str()), 0, Layouts - 1);
        else if (k == "keys")
        {
            std::istringstream ss(v);
            for (int i = 0; i < N; i++)
            {
                int a = 0, b = 0;
                ss >> a >> b;
                sc[i][0] = a > 0 && a < SDL_NUM_SCANCODES ? a : 0;
                sc[i][1] = b > 0 && b < SDL_NUM_SCANCODES ? b : 0;
            }
        }
        else return false;
        return true;
    }
    // (Keys are written for every layout, so that a later version with different presets
    // still keeps what the player had.)
    void Save(std::ostream& f) const
    {
        f << "keys_layout=" << layout << "\nkeys=";
        for (int i = 0; i < N; i++) f << sc[i][0] << " " << sc[i][1] << (i + 1 < N ? " " : "\n");
    }
};
