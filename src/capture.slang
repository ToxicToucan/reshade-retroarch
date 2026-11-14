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
// HDR frames (rra_input 1: scRGB, linear BT.709 with 1.0 = 80 nits; rra_input 2: HDR10,
// BT.2020 with the PQ curve) are first turned into the SDR values RetroArch presets are
// written for: linear light divided by rra_paper_white nits (so paper white becomes SDR
// white), converted to BT.709, limited to 0..1 (brighter highlights are clipped) and
// encoded with a 2.2 power curve. The output pass (output.slang) undoes this after the
// preset. rra_input 0 (SDR frames) leaves the values as they are.
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
#pragma parameter rra_input "Frame: 0 SDR, 1 scRGB, 2 HDR10" 0.0 0.0 2.0 1.0
#pragma parameter rra_paper_white "Frame: paper white of an HDR frame (nits)" 200.0 1.0 10000.0 1.0

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
    float rra_input;
    float rra_paper_white;
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

// Linear BT.2020 to linear BT.709 (inverse of ITU-R BT.2087's matrix).
vec3 bt2020_to_bt709(vec3 c)
{
    return vec3(dot(vec3(1.660491, -0.587641, -0.072850), c), dot(vec3(-0.124550, 1.132900, -0.008349), c),
                dot(vec3(-0.018151, -0.100579, 1.118730), c));
}

// PQ (SMPTE ST 2084) decoding: linear light where 1.0 is 10000 nits.
vec3 pq_decode(vec3 e)
{
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const vec3 p = pow(clamp(e, 0.0, 1.0), vec3(1.0 / m2));
    return pow(max(p - c1, 0.0) / (c2 - c3 * p), vec3(1.0 / m1));
}

// The frame pixel at `p`, as SDR (see the top of the file).
vec3 frame_pixel(ivec2 p)
{
    const vec3 v = texelFetch(Source, p, 0).rgb;
    if (params.rra_input < 0.5)
        return v;
    vec3 c; // linear BT.709, 1.0 = paper white
    if (params.rra_input < 1.5)
        c = v * (80.0 / params.rra_paper_white);
    else
        c = bt2020_to_bt709(pq_decode(v) * (10000.0 / params.rra_paper_white));
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
}

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
        FragColor = vec4(frame_pixel(ivec2(p)), 1.0);
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
            row += wx * frame_pixel(ivec2(x, y));
        }
        sum += wy * row;
    }
    FragColor = vec4(sum / ((hi.x - lo.x) * (hi.y - lo.y)), 1.0);
}
