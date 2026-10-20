// Checks frontend/hud.h: PNG decoding, blending, 9-slice drawing and (with a ROM) the
// game's font. Writes hud_test.ppm to look at.
// Build and run: c++ -std=c++17 -O2 -Ifrontend tests/hud_test.cpp -lz -o hud_test && ./hud_test [rom.nds]
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include "hud.h"

static std::vector<uint8_t> ReadAsset(const std::string& name)
{
    std::ifstream f("assets/ui/" + name, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

int main(int argc, char** argv)
{
    // Blending: half-transparent white over nothing stays half transparent; over opaque
    // black it becomes opaque grey.
    assert(BlendOver(0, 0x80FFFFFF) == 0x80FFFFFF);
    const uint32_t grey = BlendOver(0xFF000000, 0x80FFFFFF);
    assert(grey >> 24 == 0xFF && (grey & 0xFF) >= 0x7F && (grey & 0xFF) <= 0x81);

    Sprites sprites;
    assert(sprites.Load(ReadAsset));
    const Sprite* card = sprites.Get("panels/card");
    assert(card && card->nine && card->inset[0] == 36 && card->img.w > 0);
    const Sprite* rowSel = sprites.Get("panels/row_selected");
    const Sprite* cursor = sprites.Get("cursor/cursor");
    const Sprite* btn = sprites.Get("buttons/button_a");
    assert(rowSel && cursor && btn);
    assert(!sprites.Get("panels/no_such_sprite"));

    Canvas c;
    c.Resize(1280, 720);
    Ui ui;
    ui.Set(c.w, c.h);
    FillBlend(c, 0, 0, (float)c.w, (float)c.h, 0xFF9AD86A);
    DrawSprite(c, card, ui(100 - 12), ui(100 - 10), ui(900 + 24), ui(500 + 24), ui.s);
    DrawSprite(c, rowSel, ui(140 - 12), ui(140 - 10), ui(820 + 24), ui(60 + 24), ui.s);
    DrawSprite(c, cursor, ui(150), ui(146), ui(44), ui(48), ui.s);
    DrawSprite(c, btn, ui(1100), ui(980), ui(48), ui(48), ui.s);
    // The card's middle is white; the canvas outside it untouched green.
    assert((c.px[(size_t)ui(400) * c.w + (int)ui(500)] & 0xFFFFFF) == 0xFFFFFF);
    assert(c.px[5 * c.w + 5] == 0xFF9AD86A);

    RomFont font;
    if (argc > 1)
    {
        std::ifstream f(argv[1], std::ios::binary);
        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), {});
        assert(font.Load(rom.data(), rom.size()));
        assert(font.Width("Screen layout", 1) > 40 && font.Width("Screen layout", 1) < 100);
        assert(font.Width("Bold", 1, true) > font.Width("Bold", 1));
    }
    font.Draw(c, (int)ui(200), (int)ui(150), "Screen layout  Pok\xC3\xA9mon \xE2\x99\x82", ui.Font(32), 0xFF3B3F4A, 0xFFD0D4DC);
    font.Draw(c, (int)ui(200), (int)ui(300), "Display", ui.Font(64), 0xFF3B3F4A, 0xFFD0D4DC, true);

    std::ofstream out("hud_test.ppm", std::ios::binary);
    out << "P6\n" << c.w << " " << c.h << "\n255\n";
    for (uint32_t p : c.px)
    {
        const char rgb[3] = { (char)(p >> 16), (char)(p >> 8), (char)p };
        out.write(rgb, 3);
    }
    printf("hud test passed%s\n", font.Ok() ? " (with the ROM's font)" : "");
}
