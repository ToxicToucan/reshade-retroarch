#pragma once
// Access to the add-on's own presets, run through librashader like any other preset, so
// the same code works on every graphics API librashader supports:
//  - the capture preset (capture.slangp, shader capture.slang), which rebuilds a game's
//    native, low-resolution picture from the frame the game presented;
//  - the output preset (output.slangp, shader output.slang), which turns the user's
//    preset's SDR picture into an HDR swap chain's format (HDR games only).

#include <string>

class ShaderChain;
struct PixelGrid;

// How the frame's values are encoded, from the swap chain's format and colour space.
enum class FrameColour
{
    sdr = 0,   // gamma-encoded SDR (every 8-bit and 10-bit SDR swap chain)
    scrgb = 1, // scRGB: linear BT.709, 1.0 = 80 nits, 16-bit float
    hdr10 = 2, // HDR10: BT.2020 with the PQ curve, 10 bits
};

// The user's HDR settings, used only for HDR frames (see output.slang).
struct HdrSettings
{
    float paper_white = 200.0f; // nits SDR white of the game's picture maps to and from
    float peak = 1000.0f;       // nits the brightest SDR white of the preset's output is shown at
    bool expand_gamut = true;   // take BT.709 colours as DCI-P3 (more vivid), as RetroArch does by default
};

// Limits of the HDR settings, which load and the overlay keep them within.
constexpr float kHdrMinNits = 80.0f, kHdrMaxNits = 10000.0f;

// Returns the path (UTF-8) of the capture preset, ready for librashader to load: the one
// using capture_sm3.slang (for Direct3D 9, whose Shader Model 3 has no integer maths)
// when `shader_model_3` is set, else the one using capture.slang. The files are built into
// the program; librashader only loads presets from files, so the first call writes them
// into a folder under the user's temporary directory (its name includes a hash of their
// content, so different versions never mix) and later calls return the same path. Returns an empty
// string and sets `error` if the files cannot be written.
std::string capture_preset_path(bool shader_model_3, std::string &error);

// The same for the output preset (output.slangp). There is no Shader Model 3 version:
// Direct3D 9 has no HDR swap chains.
std::string output_preset_path(std::string &error);

// Passes `grid` to `capture` (the capture preset compiled from that path): its rectangle,
// native size and `average` flag become the shader's rra_* parameters. Returns false and
// sets `error` if librashader rejects one.
bool set_capture_params(ShaderChain &capture, const PixelGrid &grid, std::string &error);

// Tells `capture` how the frame is encoded (rra_input) and, for HDR frames, the paper
// white to bring it to SDR with (rra_paper_white). Not for the Shader Model 3 capture,
// which has neither parameter. Returns false and sets `error` if librashader rejects one.
bool set_capture_colour(ShaderChain &capture, FrameColour colour, const HdrSettings &hdr, std::string &error);

// Passes the output format (`colour`, scrgb or hdr10) and `hdr` to `output` (the output
// preset compiled from output_preset_path()). Returns false and sets `error` if
// librashader rejects one.
bool set_output_params(ShaderChain &output, FrameColour colour, const HdrSettings &hdr, std::string &error);
