#pragma once
// The Downsample mode: for games whose picture has no native pixel grid to find, such as
// 3D drawn at the full window resolution, or pixel art upscaled smoothly. Instead of
// looking for the game's original pixels, the add-on averages the frame down to a small
// picture of a size the user chooses (for example 320x240) and runs the preset on that.
// Each small pixel is the area average of the frame pixels it covers (capture.slang with
// rra_average), so edges of a 3D picture come out smoothed as by supersampling, not
// jagged as by taking one pixel per cell (the Fixed mode).
//
// This file only works out the grid (which rectangle of the frame, at what small size);
// the averaging itself runs on the GPU in the capture preset. Plain arithmetic, shared by
// the add-on and tools/render_png.

#include "grid_detect.h"

// The user's Downsample settings, stored in ReShade.ini by the add-on (addon.cpp).
struct DownsampleSettings
{
    // Size of the small picture for a game picture with the shape of a 4:3 screen, the
    // shape the usual targets (320x240, 256x224, ...) were made for.
    int target_w = 320, target_h = 240;
    // false: keep the game picture's shape: the small picture has target_h rows, and as
    // many columns as a picture of that shape gets at the target's pixel width (a 16:9
    // picture at 320x240 becomes 427x240). true: always exactly target_w x target_h, the
    // picture squeezed or stretched to fit.
    bool stretch = false;
    // Parts of the frame left out at each edge, in percent of the frame's width (left,
    // right) or height (top, bottom), for games with black bars or a border drawn into
    // the frame. The picture is averaged from the rest and written back over the rest only;
    // the cropped edges are left untouched. Percentages, so they still fit after the window
    // is resized.
    float crop_left = 0, crop_top = 0, crop_right = 0, crop_bottom = 0;
};

// Limits of the settings, which load and the overlay keep them within.
constexpr int kDownsampleMinSize = 16;
constexpr int kDownsampleMaxWidth = 7680, kDownsampleMaxHeight = 4320;
constexpr float kDownsampleMaxCrop = 45.0f; // percent per edge, so at least 10% is left

// Returns the grid the Downsample mode uses on a frame of frame_w x frame_h pixels: the
// rectangle left after cropping (at least 1 pixel each way), its small size per
// `settings` (each side at least kDownsampleMinSize and at most the maximum above), and
// `average` set. Out-of-range settings are clamped. Returns a grid with valid == false
// for an empty frame.
PixelGrid downsample_grid(int frame_w, int frame_h, const DownsampleSettings &settings);

// Left and right crop, in percent of the frame's width, that leave a 4:3 picture in the
// middle of a frame_w x frame_h frame (for example 12.5 each for 16:9). 0 for a frame
// that is 4:3 or narrower.
float crop_for_4_3(int frame_w, int frame_h);
