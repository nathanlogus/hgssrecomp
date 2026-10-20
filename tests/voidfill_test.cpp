// Checks frontend/voidfill.h on made-up pictures.
// Build and run: c++ -std=c++17 -O2 -Ifrontend tests/voidfill_test.cpp -o voidfill_test && ./voidfill_test
#include <cassert>
#include <cstdio>
#include "voidfill.h"

int main()
{
    const int w = 320, h = 180;
    std::vector<uint32_t> img((size_t)w * h);
    // Drawn ground on the left two thirds (green, brighter towards the bottom), nothing
    // (top byte 0) on the right third, as when the view reaches past the loaded map.
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            img[(size_t)y * w + x] = x < 2 * w / 3 ? 0xFF000000 | (40u << 16) | ((100u + y / 2) << 8) | 40u : 0;

    VoidFill vf;
    // The DS's own view in the middle 30% is fully drawn: the void is filled.
    const uint32_t* out = vf.Apply(img.data(), w, h, 0.3f, 0.3f, 1);
    assert(out != img.data());
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const uint32_t c = out[(size_t)y * w + x];
            assert(c >> 24);                                   // nothing left empty
            if (x < 2 * w / 3) assert(c == img[(size_t)y * w + x]); // drawn pixels untouched
        }
    // Just past the edge the haze continues the ground's colour, dimmed.
    const uint32_t edge = img[(size_t)90 * w + 2 * w / 3 - 1], haze = out[(size_t)90 * w + 2 * w / 3 + 2];
    const int eg = (edge >> 8) & 0xFF, hg = (haze >> 8) & 0xFF;
    assert(hg > eg * 0.5 && hg < eg * 0.7);
    // Far out it keeps the gradient of the rows next to it (top darker than bottom).
    assert(((out[(size_t)5 * w + w - 1] >> 8) & 0xFF) < ((out[(size_t)(h - 5) * w + w - 1] >> 8) & 0xFF));

    // Empty pixels inside the DS's view (the black around a room) are the game's own: untouched.
    assert(vf.Apply(img.data(), w, h, 1.0f, 1.0f, 2) == img.data());
    // Same picture again (same serial): the same answer without working it out again.
    assert(vf.Apply(img.data(), w, h, 1.0f, 1.0f, 2) == img.data());
    // Nothing drawn at all (black between maps): untouched.
    std::vector<uint32_t> black((size_t)w * h, 0);
    assert(vf.Apply(black.data(), w, h, 0.5f, 0.5f, 3) == black.data());

    // A picture to look at: writes voidfill_test.ppm.
    out = vf.Apply(img.data(), w, h, 0.3f, 0.3f, 4);
    if (FILE* f = fopen("voidfill_test.ppm", "wb"))
    {
        fprintf(f, "P6 %d %d 255\n", w, h);
        for (size_t i = 0; i < (size_t)w * h; i++)
        {
            const unsigned char rgb[3] = { (unsigned char)(out[i] >> 16), (unsigned char)(out[i] >> 8), (unsigned char)out[i] };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
    }
    printf("voidfill: all checks passed\n");
    return 0;
}
