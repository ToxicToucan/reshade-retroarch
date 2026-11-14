// Implementation of downsample.h: the grid for the Downsample mode.

#include "downsample.h"

#include <algorithm>
#include <cmath>

PixelGrid downsample_grid(int frame_w, int frame_h, const DownsampleSettings &s)
{
    PixelGrid g;
    if (frame_w <= 0 || frame_h <= 0)
        return g;
    // The rectangle left after cropping, in whole frame pixels.
    const auto crop = [](float percent, int extent) {
        return int(std::lround(double(std::clamp(percent, 0.0f, kDownsampleMaxCrop)) * extent / 100.0));
    };
    const int left = crop(s.crop_left, frame_w), right = crop(s.crop_right, frame_w);
    const int top = crop(s.crop_top, frame_h), bottom = crop(s.crop_bottom, frame_h);
    g.rect_x = left;
    g.rect_y = top;
    g.rect_w = std::max(1, frame_w - left - right);
    g.rect_h = std::max(1, frame_h - top - bottom);

    const int tw = std::clamp(s.target_w, kDownsampleMinSize, kDownsampleMaxWidth);
    const int th = std::clamp(s.target_h, kDownsampleMinSize, kDownsampleMaxHeight);
    g.native_h = th;
    g.native_w = tw;
    if (!s.stretch)
    {
        // The target is for a 4:3 picture; a picture of another shape keeps the same pixel
        // width and so gets proportionally more or fewer columns.
        const double aspect = double(g.rect_w) / double(g.rect_h);
        g.native_w = std::clamp(int(std::lround(tw * aspect * 3.0 / 4.0)), kDownsampleMinSize, kDownsampleMaxWidth);
    }
    g.valid = true;
    g.average = true;
    g.bounded = true;
    g.match = 1.0f;
    return g;
}

float crop_for_4_3(int frame_w, int frame_h)
{
    if (frame_w <= 0 || frame_h <= 0 || frame_w * 3 <= frame_h * 4)
        return 0.0f;
    const double picture_w = frame_h * 4.0 / 3.0;
    return float((frame_w - picture_w) / 2.0 * 100.0 / frame_w);
}
