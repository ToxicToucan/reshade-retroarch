// Implementation of capture.h: writes the built-in capture and output shaders and presets
// to disk, and sets their parameters.

#include "capture.h"
#include "capture_slang.h"
#include "chain.h"
#include "grid_detect.h"
#include "utf8.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>

namespace fs = std::filesystem;

namespace
{
// Writes `text` to `path` unless the file already holds exactly that text. Writes to a
// temporary name first and renames, so another process reading the file never reads
// half of it. Returns false if the file cannot be written.
bool write_if_different(const fs::path &path, const std::string &text)
{
    {
        std::ifstream in(path, std::ios::binary);
        if (in && std::string(std::istreambuf_iterator<char>(in), {}) == text)
            return true;
    }
    const fs::path tmp = path.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        if (!out)
            return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec)
    {
        fs::remove(tmp, ec);
        // Another process may have written the same file in the meantime.
        std::ifstream in(path, std::ios::binary);
        return in && std::string(std::istreambuf_iterator<char>(in), {}) == text;
    }
    return true;
}
}

// Writes the shader `slang` as `shader_name` and the preset `slangp` as `preset_name` into
// a folder named `prefix` plus a hash of their content, under the user's temporary
// folder, and returns the preset's path (UTF-8). The hash means an older version's files
// are never reused. Returns an empty string and sets `error` (naming `what`) on failure.
static std::string write_preset(const wchar_t *prefix, const wchar_t *shader_name, const std::string &slang,
                                const wchar_t *preset_name, const std::string &slangp, const char *what,
                                std::string &error)
{
    wchar_t tmp[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tmp) == 0)
    {
        error = std::string("no temporary folder for the ") + what;
        return {};
    }
    const size_t hash = std::hash<std::string>()(slang + slangp);
    const fs::path dir = fs::path(tmp) / (prefix + std::to_wstring(hash));
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!write_if_different(dir / shader_name, slang) || !write_if_different(dir / preset_name, slangp))
    {
        error = std::string("could not write the ") + what + " to " + utf8_from_path(dir);
        return {};
    }
    return utf8_from_path(dir / preset_name);
}

std::string capture_preset_path(bool shader_model_3, std::string &error)
{
    static std::mutex mutex;
    static std::string cached[2];
    std::lock_guard<std::mutex> lock(mutex);
    if (!cached[shader_model_3].empty())
        return cached[shader_model_3];

    const std::string slang = shader_model_3 ? kCaptureSm3Slang : kCaptureSlang;
    // The Shader Model 3 preset is the same preset pointing at the other shader file.
    std::string slangp = kCaptureSlangp;
    if (shader_model_3)
        slangp.replace(slangp.find("\"capture.slang\""), 15, "\"capture_sm3.slang\"");
    cached[shader_model_3] =
        write_preset(L"reshade-retroarch-capture-", shader_model_3 ? L"capture_sm3.slang" : L"capture.slang", slang,
                     L"capture.slangp", slangp, "capture shader", error);
    return cached[shader_model_3];
}

std::string output_preset_path(std::string &error)
{
    static std::mutex mutex;
    static std::string cached;
    std::lock_guard<std::mutex> lock(mutex);
    if (cached.empty())
        cached = write_preset(L"reshade-retroarch-output-", L"output.slang", kOutputSlang, L"output.slangp",
                              kOutputSlangp, "HDR output shader", error);
    return cached;
}

bool set_capture_params(ShaderChain &capture, const PixelGrid &grid, std::string &error)
{
    const std::pair<const char *, int> values[] = {
        {"rra_rect_x", grid.rect_x},     {"rra_rect_y", grid.rect_y},     {"rra_rect_w", grid.rect_w},
        {"rra_rect_h", grid.rect_h},     {"rra_native_w", grid.native_w}, {"rra_native_h", grid.native_h},
        {"rra_average", grid.average ? 1 : 0}};
    for (const auto &[name, value] : values)
        if (!capture.set_param(name, float(value), error))
            return false;
    return true;
}

bool set_capture_colour(ShaderChain &capture, FrameColour colour, const HdrSettings &hdr, std::string &error)
{
    return capture.set_param("rra_input", float(int(colour)), error) &&
           capture.set_param("rra_paper_white", hdr.paper_white, error);
}

bool set_output_params(ShaderChain &output, FrameColour colour, const HdrSettings &hdr, std::string &error)
{
    return output.set_param("rra_output", float(int(colour)), error) &&
           output.set_param("rra_paper_white", hdr.paper_white, error) &&
           output.set_param("rra_peak", hdr.peak, error) &&
           output.set_param("rra_expand_gamut", hdr.expand_gamut ? 1.0f : 0.0f, error);
}
