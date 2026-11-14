#version 450

// Output pass for HDR swap chains: turns the SDR picture the user's preset made into the
// swap chain's HDR format. Written in RetroArch's shader language and run through
// librashader as a one-pass preset (output.slangp), like the capture pass (capture.slang).
// Used only when the game presents in HDR (scRGB or HDR10); SDR games never run it.
//
// Input ("Source"): the user's preset's output, SDR (8 bits, gamma-encoded, BT.709).
// Output: the same picture for the swap chain, the same size:
//  - rra_output 1, scRGB: linear BT.709 values where 1.0 is 80 nits (16-bit float).
//  - rra_output 2, HDR10: BT.2020 values encoded with the PQ curve (SMPTE ST 2084), where
//    1.0 is 10000 nits (10 bits).
// Steps, per pixel:
//  1. Decode the SDR value to linear light with a 2.2 power curve (the display gamma
//     CRT presets are written for).
//  2. Inverse tone map: linear values up to 0.5 are shown at that fraction of paper
//     white (rra_paper_white nits), and brighter ones rise smoothly so that SDR white
//     (1.0) is shown at rra_peak nits. The curve works on luminance (BT.709 weights) and scales the colour
//     by the same factor, so hues do not shift. With L the luminance in units of paper
//     white and M = rra_peak / rra_paper_white: L up to 0.5 is unchanged; above 0.5,
//     with t = L - 0.5, it becomes 0.5 + t / (1 - t / c), which has slope 1 at 0.5 and
//     reaches M at L = 1 (c = 0.5 (M - 0.5) / (M - 1)). When M <= 1 the picture is left
//     as it is. Then everything is multiplied by paper white, in nits.
//  3. Colours: with rra_expand_gamut 0, BT.709 colours are converted exactly to the
//     output's primaries. With 1, the BT.709 values are taken as DCI-P3 (D65) values
//     instead, which makes saturated colours more vivid, as RetroArch's "expand gamut"
//     option does; it stays within what HDR screens can show.
//  4. Encode for the output (scRGB: divide by 80 nits; HDR10: BT.2020 primaries, PQ).
// The values are set by the add-on through these parameters (they are not meant to be
// changed by hand):

#pragma parameter rra_output "Output: 1 scRGB, 2 HDR10" 1.0 1.0 2.0 1.0
#pragma parameter rra_paper_white "Paper white (nits)" 200.0 1.0 10000.0 1.0
#pragma parameter rra_peak "Peak brightness (nits)" 1000.0 1.0 10000.0 1.0
#pragma parameter rra_expand_gamut "Expand gamut" 0.0 0.0 1.0 1.0

layout(push_constant) uniform Push
{
    vec4 OutputSize;
    float rra_output;
    float rra_paper_white;
    float rra_peak;
    float rra_expand_gamut;
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

// Matrices as rows: result = (dot(r0, c), dot(r1, c), dot(r2, c)).
vec3 mul(vec3 r0, vec3 r1, vec3 r2, vec3 c)
{
    return vec3(dot(r0, c), dot(r1, c), dot(r2, c));
}

// Linear BT.709 to linear BT.2020 (ITU-R BT.2087).
vec3 bt709_to_bt2020(vec3 c)
{
    return mul(vec3(0.627404, 0.329283, 0.043313), vec3(0.069097, 0.919540, 0.011362),
               vec3(0.016391, 0.088013, 0.895595), c);
}

// Linear DCI-P3 (D65) to linear BT.2020.
vec3 p3_to_bt2020(vec3 c)
{
    return mul(vec3(0.753833, 0.198597, 0.047570), vec3(0.045744, 0.941777, 0.012479),
               vec3(-0.001210, 0.017602, 0.983609), c);
}

// Linear DCI-P3 (D65) to linear BT.709 (values outside 0..1 are kept: scRGB holds them).
vec3 p3_to_bt709(vec3 c)
{
    return mul(vec3(1.224940, -0.224940, 0.0), vec3(-0.042057, 1.042057, 0.0),
               vec3(-0.019638, -0.078636, 1.098274), c);
}

// PQ (SMPTE ST 2084) encoding of `y`, linear light where 1.0 is 10000 nits.
vec3 pq_encode(vec3 y)
{
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const vec3 p = pow(clamp(y, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * p) / (1.0 + c3 * p), vec3(m2));
}

void main()
{
    const vec3 sdr = texelFetch(Source, ivec2(vTexCoord * params.OutputSize.xy), 0).rgb;
    // 1. Linear light, 1.0 = paper white.
    vec3 c = pow(clamp(sdr, 0.0, 1.0), vec3(2.2));
    // 2. Inverse tone map on luminance.
    const float m = params.rra_peak / params.rra_paper_white;
    const float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    if (m > 1.0 && l > 0.5)
    {
        const float k = 0.5 * (m - 0.5) / (m - 1.0);
        const float t = l - 0.5;
        c *= (0.5 + t / (1.0 - t / k)) / l;
    }
    c *= params.rra_paper_white; // nits
    // 3. and 4.
    const bool expand = params.rra_expand_gamut > 0.5;
    if (params.rra_output < 1.5)
    {
        const vec3 rgb = expand ? p3_to_bt709(c) : c;
        FragColor = vec4(rgb / 80.0, 1.0);
    }
    else
    {
        const vec3 rgb = expand ? p3_to_bt2020(c) : bt709_to_bt2020(c);
        FragColor = vec4(pq_encode(rgb / 10000.0), 1.0);
    }
}
