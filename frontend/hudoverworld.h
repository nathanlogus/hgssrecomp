#pragma once
// The overworld HUD: shown while the player walks about freely, faded out for anything else
// (messages, menus, battles, cutscenes, fades). Top left the party: icons, HP bars, status;
// top right a round map of the area around the player drawn from the game's own map data
// (each tile's permissions: grass, water, walls, doors), the place and the game's clock under
// it, a few badges beside it (Repel steps left, an EXP. Share held) and the other players'
// faces down the right edge; along the bottom quick buttons for the Bag, the Pokemon, the
// menu and the friends. Everything is read from the game's memory and the ROM; nothing is
// written to the game's memory. The Bag and Pokemon buttons open the game's own screens by
// pressing its buttons for the player (X, then the cursor to the entry, then A).
//
// The HUD is drawn into a canvas of its own and sent to the screen in a few small pieces
// (the corners and the bottom row) with the fade as the texture's alpha, so it costs little
// more than what it draws. Only main.cpp includes this, after SDL.
#include <algorithm>
#include <cmath>
#include <vector>
#include "hudonline.h"
#include "hudparty.h"
#include "hudworld.h"

namespace HudOverworld
{
using HudParty::Ram;

// The DS buttons, as the key mask's bits (KEYINPUT, then X and Y).
constexpr int kKeyA = 0, kKeyRight = 4, kKeyLeft = 5, kKeyUp = 6, kKeyDown = 7, kKeyX = 10;

// The quick buttons along the bottom, and the map (for a click on it).
enum Quick { Q_BAG, Q_PARTY, Q_MENU, Q_FRIENDS, Q_COUNT, Q_MAP = Q_COUNT };

// Another player on this map: where they are (fx32 world units) and who.
struct Other
{
    const PartyPeer* peer = nullptr;
    double x = 0, z = 0;
};

// What main.cpp tells the HUD each frame.
struct Input
{
    bool show = false;          // the player is free on the overworld with nothing over it
    float top = 32;             // design y of the right column's top (below on-screen controls)
    float partyTop = 32;        // design y of the party strip (below the session badge)
    float bottomLift = 0;       // design pixels to keep free at the bottom (on-screen controls)
    int hour = -1, minute = 0;  // the game's clock
    std::string place;          // the place's name
    std::vector<Other> others;  // other players on this map
    std::vector<const PartyPeer*> faces; // other players in the session (chips down the right edge)
    std::string glyphs[Q_COUNT + 1]; // the controller button of each quick button and of the zoom ("": none)
};

class Hud
{
public:
    void SetRom(const uint8_t* rom, size_t size)
    {
        Rom = rom;
        RomSize = size;
        Land = NdsNarcFiles(rom, size, "a/0/6/5");
        Maps.clear();
    }

    // Shown enough to take buttons and clicks.
    bool Shown() const { return Alpha > 0.5f && Target; }
    bool Visible() const { return Alpha > 0; }
    // Design width kept at the top right (for the corner pills to sit left of the map).
    float RightReserve() const { return Visible() ? MapD + 24 : 0; }
    // Design y under the party strip (for the toasts).
    float PartyBottom() const { return Visible() && PartyShown ? PartyY + StripH + 16 : 0; }

    // The quick button (or the map, Q_MAP) at output pixel x, y, or -1.
    int HitTest(int x, int y) const
    {
        if (!Shown()) return -1;
        for (int i = 0; i <= Q_COUNT; i++)
            if (x >= Hits[i].x0 && x < Hits[i].x1 && y >= Hits[i].y0 && y < Hits[i].y1) return i;
        return -1;
    }

    // The map's zoom: 0 the widest, 2 the closest.
    void ZoomStep(int d) { Zoom = std::clamp(Zoom + d, 0, 2); }
    void ZoomCycle() { Zoom = (Zoom + 1) % 3; }

    // Opens the Bag or the Pokemon through the game's own menu (see Keys).
    void Open(int quick, uint32_t frame)
    {
        Want = quick == Q_BAG ? 2 : quick == Q_PARTY ? 1 : -1;
        Since = frame;
        Next = frame;
        PressedX = false;
        HeldLeft = 0;
    }
    bool Opening() const { return Want >= 0; }

    // While opening the Bag or the Pokemon: the buttons for the game instead of the player's.
    // X opens the menu; then the cursor goes to the entry (StartMenuTaskData's actions, as
    // HudWorld::ReadXMenu reads them) and A opens it. Each button is held two frames and let go
    // for three, so the game sees every press.
    void Keys(const Ram& r, uint32_t& keys, uint32_t frame, bool idle)
    {
        if (Want < 0) return;
        if (frame - Since > 300)
        {
            fprintf(stderr, "[hud] the game's menu did not open\n");
            Want = -1;
            return;
        }
        keys = 0;
        if (HeldLeft > 0)
        {
            keys = Held;
            if (--HeldLeft == 0 && Held == 1u << kKeyA) Want = -1;
            return;
        }
        if (frame < Next) return;
        auto press = [&](int key) {
            Held = keys = 1u << key;
            HeldLeft = 1; // this frame and the next
            Next = frame + 5;
        };
        HudWorld::XMenu xm;
        if (HudWorld::ReadXMenu(r, xm))
        {
            const auto it = std::find(xm.actions.begin(), xm.actions.end(), Want);
            if (it == xm.actions.end())
            {
                Want = -1; // not in the menu yet (early in the game)
                return;
            }
            const int at = (int)(it - xm.actions.begin());
            press(xm.cursor == at ? kKeyA : at > xm.cursor ? kKeyDown : kKeyUp);
        }
        else if (!PressedX && idle)
        {
            press(kKeyX);
            PressedX = true;
        }
    }

    // Reads the game and draws the HUD into its own canvas (Layer), the output's size.
    void Update(const Ram& r, const Input& in, uint32_t frame, int outW, int outH, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art)
    {
        Target = in.show;
        Alpha = std::clamp(Alpha + (Target ? 1.0f / 10 : -1.0f / 5), 0.0f, 1.0f);
        Layer.Resize(outW, outH);
        for (const Box& b : Boxes)
            for (int y = b.y0; y < b.y1; y++) std::fill(Layer.px.begin() + (size_t)y * Layer.w + b.x0, Layer.px.begin() + (size_t)y * Layer.w + b.x1, 0);
        Boxes.clear();
        for (Box& h : Hits) h = Box();
        Uploaded = false;
        if (!Visible()) return;
        // What is drawn stays as it was while the HUD fades out (the game has moved on).
        if (Target) Read(r, frame);
        const float W = ui.W / ui.s, H = ui.H / ui.s;
        Region([&] { DrawParty(ui, sp, f, art, in.partyTop); });
        Region([&] { DrawRight(r, in, ui, sp, f, art, W); });
        Region([&] { DrawButtons(in, ui, sp, f, W, H); });
    }

    // Sends the HUD to the screen over the game's picture (SDL renderer of main.cpp).
    void Present(SDL_Renderer* rend, int outW, int outH)
    {
        if (!Visible() || Boxes.empty()) return;
        int tw = 0, th = 0;
        if (Tex) SDL_QueryTexture(Tex, nullptr, nullptr, &tw, &th);
        if (!Tex || tw != outW || th != outH)
        {
            if (Tex) SDL_DestroyTexture(Tex);
            Tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, outW, outH);
            SDL_SetTextureBlendMode(Tex, SDL_BLENDMODE_BLEND);
            Uploaded = false;
        }
        for (const Box& b : Boxes)
        {
            const SDL_Rect rc = { b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0 };
            if (!Uploaded) SDL_UpdateTexture(Tex, &rc, Layer.px.data() + (size_t)b.y0 * Layer.w + b.x0, Layer.w * 4);
            SDL_SetTextureAlphaMod(Tex, (Uint8)std::lround(Alpha * 255));
            SDL_RenderCopy(rend, Tex, &rc, &rc);
        }
        Uploaded = true; // the pictures between the game's frames show the same
    }

    // The same into a canvas (headless screenshots).
    void Composite(Canvas& c) const
    {
        if (!Visible() || c.w != Layer.w || c.h != Layer.h) return;
        const uint32_t a = (uint32_t)std::lround(Alpha * 255);
        for (const Box& b : Boxes)
        {
            c.Mark(b.x0, b.y0, b.x1, b.y1);
            for (int y = b.y0; y < b.y1; y++)
                for (int x = b.x0; x < b.x1; x++)
                {
                    const uint32_t s = Layer.px[(size_t)y * Layer.w + x];
                    if (s >> 24) c.px[(size_t)y * c.w + x] = BlendOver(c.px[(size_t)y * c.w + x], ((s >> 24) * a / 255) << 24 | (s & 0xFFFFFF));
                }
        }
    }

private:
    struct Box
    {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };
    const uint8_t* Rom = nullptr;
    size_t RomSize = 0;
    std::vector<std::pair<uint32_t, uint32_t>> Land; // the map blocks' files (a/0/6/5)
    Canvas Layer;
    std::vector<Box> Boxes; // the parts of Layer drawn into
    Box Hits[Q_COUNT + 1];  // the quick buttons and the map, output pixels
    SDL_Texture* Tex = nullptr;
    bool Uploaded = false, Target = false;
    float Alpha = 0;
    int Zoom = 1;
    // Opening the Bag or the Pokemon (Keys): the menu's action wanted, -1 none.
    int Want = -1, HeldLeft = 0;
    uint32_t Since = 0, Next = 0, Held = 0;
    bool PressedX = false;

    // The party and the save's numbers, read every few frames.
    int Count = 0, Repel = 0;
    PartyMon Mons[6];
    bool PartyShown = false;
    uint32_t ReadAt = 0;
    float PartyY = 32;
    static constexpr float StripH = 104, MapD = 232;

    // The player: map matrix, tile position (fractional), facing.
    bool HaveMe = false;
    double MeX = 0, MeZ = 0;
    int Facing = 1;

    // Maps as pictures, a pixel a tile, by their matrix (the last few).
    struct MapPic
    {
        uint64_t key = 0;
        Image img;
    };
    std::vector<MapPic> Maps;
    const Image* MapNow = nullptr;

    template <class F> void Region(F draw)
    {
        Layer.x0d = Layer.w;
        Layer.y0d = Layer.h;
        Layer.x1d = Layer.y1d = 0;
        draw();
        if (Layer.Dirty()) Boxes.push_back({ Layer.x0d, Layer.y0d, Layer.x1d, Layer.y1d });
    }

    void Read(const Ram& r, uint32_t frame)
    {
        const uint32_t fs = r.R32(HudWorld::kFieldSystem), obj = r.R32(r.R32(fs + 0x40) + 0x30);
        HaveMe = r.Ptr(fs) && r.Ptr(obj);
        if (HaveMe)
        {
            // MapObject: the position (fx32, +0x70 x, +0x78 z; a tile is 16 units), facing +0x28.
            MeX = (int32_t)r.R32(obj + 0x70) / 65536.0;
            MeZ = (int32_t)r.R32(obj + 0x78) / 65536.0;
            Facing = (int)(r.R32(obj + 0x28) & 3);
        }
        MapNow = HaveMe ? MapImage(r, fs) : nullptr;
        if (frame - ReadAt < 20 && frame >= ReadAt && PartyShown) return;
        ReadAt = frame;
        // The save data (FieldSystem +0x0C, SaveData in include/save.h): its blocks' headers
        // at +0x23014 (id, size, offset, 16 bytes each), their data from +0x10.
        const uint32_t save = r.R32(fs + 0x0C);
        auto block = [&](uint32_t id, uint32_t size) -> uint32_t {
            const uint32_t h = save + 0x23014 + 16 * id;
            if (!r.Ptr(save) || r.R32(h) != id || (size && r.R32(h + 4) != size)) return 0;
            return save + 0x10 + r.R32(h + 8);
        };
        PartyShown = HudParty::ReadParty(r, block(2, 0), Count, Mons);
        // RoamerSaveData (block 21, 0x68 bytes): the Repel's steps left at +0x65.
        const uint32_t roam = block(21, 0x68);
        Repel = roam ? (int)r.R8(roam + 0x65) : 0;
    }

    // The map the player is on, a pixel for each tile coloured by its permissions; matrices
    // as worldblocks.h reads them (FieldSystem +0x30: width, height, ..., the blocks' files).
    const Image* MapImage(const Ram& r, uint32_t fs)
    {
        const uint32_t matrix = r.R32(fs + 0x30);
        if (!r.Ptr(matrix) || Land.empty()) return nullptr;
        const int w = (int)r.R8(matrix), h = (int)r.R8(matrix + 1);
        if (w < 1 || h < 1 || w * h > 799) return nullptr;
        const uint32_t models = (matrix + 4 + 2 + 799 * 2 + 799 + 1) & ~1u;
        uint64_t key = 1469598103934665603ull ^ (uint64_t)(w << 8 | h);
        for (int i = 0; i < w * h; i++) key = (key ^ r.R16(models + 2 * i)) * 1099511628211ull;
        for (MapPic& m : Maps)
            if (m.key == key) return &m.img;
        if (Maps.size() >= 6) Maps.erase(Maps.begin());
        Maps.push_back({ key, Image() });
        Image& img = Maps.back().img;
        img.w = w * 32;
        img.h = h * 32;
        img.px.assign((size_t)img.w * img.h, 0);
        for (int cell = 0; cell < w * h; cell++)
        {
            const uint16_t id = (uint16_t)r.R16(models + 2 * cell);
            if (id >= Land.size()) continue;
            // A block's file: the permissions' size, ...; a header of its own (0x1234) at 16,
            // then a u16 per tile: the behaviour, and the wall bit (0x8000).
            const uint8_t* d = Rom + Land[id].first;
            const uint32_t size = Land[id].second;
            if (size < 20) continue;
            const uint32_t perm = RomGet32(d);
            uint32_t p = 16;
            if (RomGet16(d + p) == 0x1234) p += 4 + RomGet16(d + p + 2);
            if (perm < 2048 || (uint64_t)p + 2048 > size) continue;
            const int bx = cell % w * 32, bz = cell / w * 32;
            for (int t = 0; t < 1024; t++)
                img.px[(size_t)(bz + t / 32) * img.w + bx + t % 32] = TileColour(d[p + 2 * t], d[p + 2 * t + 1]);
        }
        return &img;
    }

    // Behaviours (include/constants/metatile_behavior.h): water that can be surfed, tall
    // grass, ledges, doors and other ways out.
    static uint32_t TileColour(uint8_t b, uint8_t hi)
    {
        if ((b >= 16 && b <= 21) || b == 25) return 0xFF5CA8EE;
        if (b == 2 || b == 3) return 0xFF6DBE4E;
        if (b >= 56 && b <= 59) return 0xFFC08A4E;
        if ((b >= 60 && b <= 62) || b == 94 || b == 95 || (b >= 98 && b <= 101) || b == 103 || b == 105 || (b >= 108 && b <= 111))
            return 0xFFF7A23A;
        if (hi & 0x80) return 0xFF7F9C88;
        return 0xFFF3ECD6;
    }

    void DrawParty(const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, float y)
    {
        PartyY = y;
        if (!PartyShown || Count < 1) return;
        const float slot = 80, x = 32, w = 12 + Count * slot + 4;
        HudPanel(Layer, ui, sp, "panels/card_glass", x, y, w, StripH);
        for (int i = 0; i < Count; i++)
        {
            const PartyMon& m = Mons[i];
            const float sx = x + 12 + i * slot;
            const bool fainted = !m.egg && m.hp == 0;
            if (const Image* icon = art.PokemonIcon(m.species, m.form, m.egg))
                Blit(Layer, *icon, 0, 0, 32, 32, ui(sx), ui(y + 2), ui(64), ui(64), false, fainted ? 0x70FFFFFF : 0xFFFFFFFF);
            if (m.egg) continue;
            const int most = std::max(1, m.stats[0]);
            const float bx = sx + 4, by = y + 72, bw = 60;
            HudParty::FillRound(Layer, ui, bx, by, bw, 12, 6, 0xFF1C2A44);
            if (m.hp > 0)
            {
                const uint32_t col = m.hp * 2 > most ? 0xFF5BD15B : m.hp * 5 > most ? 0xFFFFD23A : 0xFFF0505A;
                HudParty::FillRound(Layer, ui, bx + 2, by + 2, std::max(8.0f, (bw - 4) * std::min(1.0f, (float)m.hp / most)), 8, 4, col);
            }
            std::string s;
            uint32_t col;
            if (HudParty::StatusOf(m, s, col))
            {
                const float pw = HudTextWidth(ui, f, 16, s) + 10;
                HudParty::FillRound(Layer, ui, sx + 64 - pw + 2, y + 50, pw, 20, 10, col);
                HudText(Layer, ui, f, sx + 64 - pw / 2 + 2, y + 52, 16, s, HudColour::White, 1);
            }
        }
    }

    void DrawRight(const Ram& r, const Input& in, const Ui& ui, Sprites& sp, const RomFont& f, RomArt& art, float W)
    {
        (void)r;
        const float d = MapD, mx = W - 32 - d, my = in.top;
        DrawMap(in, ui, sp, f, mx, my, d);
        // The place and the clock under the map, right-aligned to it.
        float y = my + d + 12;
        {
            char clock[16] = "";
            if (in.hour >= 0) snprintf(clock, sizeof clock, "%d:%02d", in.hour, in.minute);
            const std::string text = in.place.empty() ? clock : !clock[0] ? in.place : in.place + "  " + clock;
            if (!text.empty())
            {
                const float tw = HudTextWidth(ui, f, 32, text) + 40 + 40, h = 52, x = std::min(W - 32 - tw, mx + d / 2 - tw / 2);
                HudPanel(Layer, ui, sp, "panels/toast", x, y, tw, h);
                HudSprite(Layer, ui, sp, "icons/icon_clock", x + 14, y + 10, 32, 32);
                HudText(Layer, ui, f, x + 54, y + 10, 32, text, HudColour::Ink);
                y += h + 16;
            }
        }
        // Badges beside the map: what the save says about the walk.
        float by = my + 8;
        auto badge = [&](const Image* icon, const std::string& text) {
            const float h = 48, tw = HudTextWidth(ui, f, 32, text) + 20 + 44, x = mx - 16 - tw;
            HudPanel(Layer, ui, sp, "panels/toast", x, by, tw, h);
            if (icon) Blit(Layer, *icon, 0, 0, (float)icon->w, (float)icon->h, ui(x + 8), ui(by + 4), ui(40), ui(40), false);
            HudText(Layer, ui, f, x + 52, by + 8, 32, text, HudColour::Ink);
            by += h + 14;
        };
        if (Repel > 0) badge(art.ItemIcon(79), "Repel " + std::to_string(Repel));
        for (int i = 0; PartyShown && i < Count; i++)
            if (!Mons[i].egg && Mons[i].item == 216)
            {
                badge(art.ItemIcon(216), "EXP. Share");
                break;
            }
        // The other players' faces down the right edge (the session's chips).
        for (const PartyPeer* p : in.faces)
        {
            if (y > ui.H / ui.s - 200) break;
            const float s = 68, x = W - 32 - s;
            HudOnline::Chip(Layer, ui, sp, f, x, y, s, *p);
            if (p->host) HudSprite(Layer, ui, sp, "people/crown_gold", x + s / 2 - 16, y - 12, 32, 30);
            if (p->state == PS_BATTLE) HudSprite(Layer, ui, sp, "emotes/emote_battle", x - 14, y + 30, 36, 36);
            y += s + 10;
        }
    }

    // The round map: the tiles around the player, north up, the player an arrow in the middle,
    // the others their faces (at the edge when they are farther away).
    void DrawMap(const Input& in, const Ui& ui, Sprites& sp, const RomFont& f, float x, float y, float d)
    {
        static const float radii[3] = { 22, 14, 9 }; // tiles from the middle to the edge
        const float R = ui(d / 2), cx = ui(x + d / 2), cy = ui(y + d / 2), tiles = radii[Zoom], k = R / tiles;
        // A soft shadow, then the disc.
        HudParty::FillRound(Layer, ui, x - 2, y + 4, d + 4, d + 4, d / 2 + 2, 0x30101828);
        const int x0 = std::max(0, (int)(cx - R)), y0 = std::max(0, (int)(cy - R)), x1 = std::min(Layer.w, (int)std::ceil(cx + R));
        const int y1 = std::min(Layer.h, (int)std::ceil(cy + R));
        const float rim = std::max(2.0f, ui(5));
        const Image* img = MapNow;
        Layer.Mark(x0, y0, x1, y1);
        for (int py = y0; py < y1; py++)
        {
            const float dy = py + 0.5f - cy;
            for (int px = x0; px < x1; px++)
            {
                const float dx = px + 0.5f - cx, dist = std::sqrt(dx * dx + dy * dy);
                const float cov = std::clamp(R - dist + 0.5f, 0.0f, 1.0f);
                if (cov <= 0) continue;
                uint32_t col = 0xFF1C2A44;
                if (dist > R - rim) col = 0xFFFFFFFF;
                else if (img && HaveMe)
                {
                    const int tx = (int)std::floor(MeX + dx / k), tz = (int)std::floor(MeZ + dy / k);
                    if (tx >= 0 && tz >= 0 && tx < img->w && tz < img->h)
                    {
                        const uint32_t t = img->px[(size_t)tz * img->w + tx];
                        if (t) col = t;
                    }
                }
                uint32_t& o = Layer.px[(size_t)py * Layer.w + px];
                o = BlendOver(o, (uint32_t)(255 * cov) << 24 | (col & 0xFFFFFF));
            }
        }
        const float ccx = x + d / 2, ccy = y + d / 2, kd = d / 2 / tiles; // design units
        // The others: their faces, kept inside the disc.
        for (const Other& o : in.others)
        {
            float ox = (float)((o.x / 65536.0 - MeX) * kd), oz = (float)((o.z / 65536.0 - MeZ) * kd);
            const float len = std::sqrt(ox * ox + oz * oz), most = d / 2 - 26;
            if (len > most) ox *= most / len, oz *= most / len;
            HudOnline::Chip(Layer, ui, sp, f, ccx + ox - 22, ccy + oz - 22, 44, *o.peer);
        }
        // The player: an arrow the way they face (0 north, 1 south, 2 west, 3 east).
        static const float ang[4] = { -1.5707963f, 1.5707963f, 3.1415927f, 0 };
        const float a = ang[Facing & 3], ca = std::cos(a), sa = std::sin(a);
        auto tri = [&](float s, uint32_t col) {
            auto pt = [&](float fx, float fy) { return std::make_pair(ccx + (fx * ca - fy * sa) * s, ccy + (fx * sa + fy * ca) * s); };
            HudParty::FillConvex(Layer, ui, { pt(1.0f, 0), pt(-0.7f, 0.75f), pt(-0.35f, 0), pt(-0.7f, -0.75f) }, col);
        };
        if (HaveMe)
        {
            tri(19, 0xFF1C2A44);
            tri(14, 0xFFFFD75E);
        }
        // North, on the rim.
        HudParty::FillRound(Layer, ui, ccx - 17, y - 10, 34, 34, 17, 0xFF1C2A44);
        HudText(Layer, ui, f, ccx, y - 1, 16, "N", HudColour::White, 1, true);
        // The zoom's button, on the rim at the bottom left.
        if (!in.glyphs[Q_MAP].empty()) HudSprite(Layer, ui, sp, in.glyphs[Q_MAP], x + d * 0.15f - 22, y + d * 0.85f - 22, 44, 44);
        Hits[Q_MAP] = { (int)ui(x), (int)ui(y), (int)ui(x + d), (int)ui(y + d) };
    }

    void DrawButtons(const Input& in, const Ui& ui, Sprites& sp, const RomFont& f, float W, float H)
    {
        static const char* labels[Q_COUNT] = { "BAG", "POK\xC3\xA9MON", "MENU", "FRIENDS" };
        static const char* icons[Q_COUNT] = { "icons/icon_bag", "icons/icon_party", "icons/icon_layers", "icons/icon_wifi" };
        const float h = 56, gap = 14, y = H - 24 - h - in.bottomLift;
        float widths[Q_COUNT], total = -gap;
        for (int i = 0; i < Q_COUNT; i++)
        {
            widths[i] = 20 + (in.glyphs[i].empty() ? 0 : 48) + 40 + 10 + HudTextWidth(ui, f, 32, labels[i]) + 22;
            total += widths[i] + gap;
        }
        float x = W / 2 - total / 2;
        for (int i = 0; i < Q_COUNT; i++)
        {
            HudPanel(Layer, ui, sp, "panels/toast", x, y, widths[i], h);
            float cx = x + 20;
            if (!in.glyphs[i].empty())
            {
                HudSprite(Layer, ui, sp, in.glyphs[i], cx, y + 8, 40, 40);
                cx += 48;
            }
            HudSprite(Layer, ui, sp, icons[i], cx, y + 8, 40, 40);
            HudText(Layer, ui, f, cx + 50, y + 12, 32, labels[i], HudColour::Ink);
            Hits[i] = { (int)ui(x), (int)ui(y), (int)ui(x + widths[i]), (int)ui(y + h) };
            x += widths[i] + gap;
        }
    }
};
}
