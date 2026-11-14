#version 450

// Capture pass: rebuilds a game's native-resolution picture from the frame the game
// presented. Written in RetroArch's shader language (slang) so that librashader
// compiles it for every graphics API; the add-on runs it as a one-pass shader preset
// (capture.slangp) whose output is the native picture, which is then the input of the
// user's RetroArch preset.
//
// Input ("Source"): the whole frame, as the game drew it.
// Output: one pixel per native pixel (the output size is the native resolution).
// The game's picture covers the rectangle rra_rect_* of the frame and has
// rra_native_w x rra_native_h pixels; each native pixel is a cell of the rectangle.
// Output pixel (i, j) is the frame pixel at the centre of cell (i, j):
//     x = rect_x + floor((2i + 1) * rect_w / (2 * native_w)), and likewise for y.
// This is the same integer formula the pixel grid detector uses (cell_centre in
// grid_detect.h), so the capture reads exactly the pixels the detector validated.
//
// With rra_average set to 1 (the Downsample mode, downsample.h), output pixel (i, j) is
// instead the average of every frame pixel that cell (i, j) covers, each weighted by the
// part of its area inside the cell (a box filter). Cell (i, j) spans
//     x from rect_x + i * rect_w / native_w to rect_x + (i + 1) * rect_w / native_w
// (and likewise for y), which need not be whole pixels: a pixel the cell's edge cuts
// through counts with the part inside. So a 3D picture drawn at full resolution is
// reduced the way a supersampled render would be, and its edges come out smooth.
// The values stored in the frame are averaged as they are (not converted to linear
// light first), as RetroArch passes a core's picture to a preset.
//
// The values are set by the add-on through these parameters (they are not meant to
// be changed by hand):

#pragma parameter rra_rect_x "Game picture: left edge in the frame (pixels)" 0.0 0.0 16384.0 1.0
#pragma parameter rra_rect_y "Game picture: top edge in the frame (pixels)" 0.0 0.0 16384.0 1.0
#pragma parameter rra_rect_w "Game picture: width in the frame (pixels)" 1.0 1.0 16384.0 1.0
#pragma parameter rra_rect_h "Game picture: height in the frame (pixels)" 1.0 1.0 16384.0 1.0
#pragma parameter rra_native_w "Game picture: native width" 1.0 1.0 16384.0 1.0
#pragma parameter rra_native_h "Game picture: native height" 1.0 1.0 16384.0 1.0
#pragma parameter rra_average "Game picture: average each cell (1) or take its centre (0)" 0.0 0.0 1.0 1.0

layout(push_constant) uniform Push
{
    vec4 OutputSize;
    float rra_rect_x;
    float rra_rect_y;
    float rra_rect_w;
    float rra_rect_h;
    float rra_native_w;
    float rra_native_h;
    float rra_average;
} params;

layout(std140, set = 0, binding = 0) uniform UBO
{
    mat4 MVP;
} global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

void main()
{
    // Which native pixel this output pixel is.
    const uvec2 cell = uvec2(vTexCoord * params.OutputSize.xy);
    const uvec2 origin = uvec2(params.rra_rect_x, params.rra_rect_y);
    const uvec2 size = uvec2(params.rra_rect_w, params.rra_rect_h);
    const uvec2 native = uvec2(params.rra_native_w, params.rra_native_h);
    if (params.rra_average < 0.5)
    {
        // Exact integer maths: frame pixel at the centre of that cell.
        const uvec2 p = origin + ((2u * cell + 1u) * size) / (2u * native);
        FragColor = vec4(texelFetch(Source, ivec2(p), 0).rgb, 1.0);
        return;
    }
    // Area average. The cell's edges in frame pixels: [lo, hi) on each axis.
    const vec2 lo = vec2(origin) + vec2(cell * size) / vec2(native);
    const vec2 hi = vec2(origin) + vec2((cell + 1u) * size) / vec2(native);
    const ivec2 first = ivec2(floor(lo));
    const ivec2 last = ivec2(ceil(hi)) - 1;
    vec3 sum = vec3(0.0);
    for (int y = first.y; y <= last.y; ++y)
    {
        // Height of frame row y inside the cell.
        const float wy = min(float(y + 1), hi.y) - max(float(y), lo.y);
        vec3 row = vec3(0.0);
        for (int x = first.x; x <= last.x; ++x)
        {
            const float wx = min(float(x + 1), hi.x) - max(float(x), lo.x);
            row += wx * texelFetch(Source, ivec2(x, y), 0).rgb;
        }
        sum += wy * row;
    }
    FragColor = vec4(sum / ((hi.x - lo.x) * (hi.y - lo.y)), 1.0);
}
