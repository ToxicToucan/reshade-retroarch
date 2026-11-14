#version 450

// Capture pass for Direct3D 9: the same job as capture.slang (see there), written for
// Shader Model 3, which has no integer arithmetic and no exact texel fetch. The cell
// centre is computed in floating point and sampled with point filtering at the centre of
// that frame pixel. Float division can give a frame pixel one away from the exact integer
// result near a cell edge; that pixel is still inside the same cell (cells are at least 2
// pixels wide), so for a nearest-neighbour or sharp stretch the colour is the same.
//
// The area average (rra_average = 1, the Downsample mode) is exact like capture.slang's
// while a cell spans at most 16 frame pixels on an axis (320x240 from frames up to about
// 4800x3600). On an axis where it spans more, it takes 16 point samples spread evenly over
// the cell instead of every pixel, which is close but not exact.

#pragma parameter rra_rect_x "Game picture: left edge in the frame (pixels)" 0.0 0.0 16384.0 1.0
#pragma parameter rra_rect_y "Game picture: top edge in the frame (pixels)" 0.0 0.0 16384.0 1.0
#pragma parameter rra_rect_w "Game picture: width in the frame (pixels)" 1.0 1.0 16384.0 1.0
#pragma parameter rra_rect_h "Game picture: height in the frame (pixels)" 1.0 1.0 16384.0 1.0
#pragma parameter rra_native_w "Game picture: native width" 1.0 1.0 16384.0 1.0
#pragma parameter rra_native_h "Game picture: native height" 1.0 1.0 16384.0 1.0
#pragma parameter rra_average "Game picture: average each cell (1) or take its centre (0)" 0.0 0.0 1.0 1.0

layout(push_constant) uniform Push
{
    vec4 SourceSize;
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
    const vec2 cell = floor(vTexCoord * params.OutputSize.xy);
    const vec2 origin = vec2(params.rra_rect_x, params.rra_rect_y);
    const vec2 size = vec2(params.rra_rect_w, params.rra_rect_h);
    const vec2 native = vec2(params.rra_native_w, params.rra_native_h);
    if (params.rra_average < 0.5)
    {
        // Frame pixel at the centre of that cell, then the centre of that pixel.
        const vec2 p = origin + floor((2.0 * cell + 1.0) * size / (2.0 * native));
        FragColor = vec4(textureLod(Source, (p + 0.5) * params.SourceSize.zw, 0.0).rgb, 1.0);
        return;
    }
    // Area average. The cell's edges in frame pixels: [lo, hi) on each axis; it touches
    // `count` pixels from `first` on.
    const vec2 lo = origin + cell * size / native;
    const vec2 hi = origin + (cell + 1.0) * size / native;
    const vec2 first = floor(lo);
    const vec2 count = ceil(hi) - first;
    // Up to 16 pixels: each one, weighted by its part inside the cell. More: 16 equal
    // parts of the cell, each read from the pixel at its centre.
    const vec2 n = min(count, vec2(16.0));
    const vec2 step = (hi - lo) / 16.0;
    vec3 sum = vec3(0.0);
    for (int j = 0; j < 16; ++j)
    {
        if (float(j) >= n.y)
            break;
        float y = first.y + float(j);
        float wy = min(y + 1.0, hi.y) - max(y, lo.y);
        if (count.y > 16.0)
        {
            y = floor(lo.y + (float(j) + 0.5) * step.y);
            wy = step.y;
        }
        vec3 row = vec3(0.0);
        for (int i = 0; i < 16; ++i)
        {
            if (float(i) >= n.x)
                break;
            float x = first.x + float(i);
            float wx = min(x + 1.0, hi.x) - max(x, lo.x);
            if (count.x > 16.0)
            {
                x = floor(lo.x + (float(i) + 0.5) * step.x);
                wx = step.x;
            }
            row += wx * textureLod(Source, (vec2(x, y) + 0.5) * params.SourceSize.zw, 0.0).rgb;
        }
        sum += wy * row;
    }
    FragColor = vec4(sum / ((hi.x - lo.x) * (hi.y - lo.y)), 1.0);
}
