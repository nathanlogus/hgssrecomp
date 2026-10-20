// ssrecomp: widescreen, full-resolution rendering of the 3D scene.
//
// The DS draws its 3D scene 256x192 pixels large through a 4:3 view. For a wide display
// the frame's polygons are submitted a second time with the horizontal clip coordinate
// squeezed, so that what lies left and right of the original view survives clipping, and
// are then drawn by a separate rasteriser at the size of the output. The emulated state
// is not touched: the game sees exactly the polygon counts and results it would see
// without any of this.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <vector>
#include "types.h"

namespace melonDS
{
class GPU;
class GPU3D;

struct WideState
{
    GPU3D* Owner = nullptr;   // the console whose scene is drawn wide; nullptr turns it off
    int Width = 0, Height = 0; // size to draw at, set by the frontend
    float ViewWidth = 256;    // width of the view in DS pixels (256 = no extra width)
    float Zoom = 1;           // above 1 the world is drawn larger (less of it), below 1 smaller

    // Extra geometry for the wide picture only, asked for when a frame is flushed: the
    // frontend appends geometry commands to the list (command byte, then its parameters,
    // as GPU3D::RunCapturedCommands reads them). They are run through the vertex pipeline
    // on top of the game's state, which is put back afterwards, and their polygons are
    // added to the frame's recorded ones; the game and the DS's own picture never see them.
    std::function<void(std::vector<u32>& commands)> Extra;
    bool Capturing = false;   // set while Extra's commands run
    // Textures the game drew this frame with generated texture coordinates (scrolling
    // water): texture parameters without the generation bits -> the full parameters and
    // the texture matrix, so that Extra can animate the same textures the same way.
    std::map<u32, std::array<s32, 17>> TexAnim;

    // The image that belongs to the frame the 2D renderer has just output: XRGB, with
    // the top byte 0 where no polygon was drawn.
    std::vector<u32> Front;
    int FrontW = 0, FrontH = 0;
    u64 FrontSerial = 0;      // changes whenever Front does
};
extern WideState Wide;
}
