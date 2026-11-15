#pragma once
// Finding RetroArch shader presets (.slangp files) on the computer, for the list in the
// add-on's window: which folders to search (find_shader_roots), every preset in them
// (scan_presets), and the folder tree the window shows them in (build_preset_tree). The
// add-on runs the first two on a background thread (see Scan in addon.cpp).

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

// A folder searched for presets.
struct ShaderRoot
{
    std::string name;          // shown in the window, e.g. "RetroArch (Steam)"
    std::filesystem::path dir; // the folder
    bool user = false;         // from the ShaderPaths setting (the user added it), not found automatically
};

// One preset found in a ShaderRoot.
struct PresetEntry
{
    std::string label;          // "<root name>/<path inside the root>", used for sorting and filtering
    std::filesystem::path path; // the .slangp file
    size_t root = 0;            // index of its ShaderRoot in the roots passed to scan_presets
    std::string rel;            // path inside the root, UTF-8 with '/' separators
};

// Returns the folders to search that exist on this computer, each once, in this order:
//  1. `extra` folders from the ShaderPaths setting (the user's own choice comes first),
//  2. "retroarch-shaders" next to the add-on (drop shader packs here),
//  3. the same next to the game executable, if that is elsewhere,
//  4. if `auto_find`: the "shaders" folder of a RetroArch installation (default install
//     folders, %APPDATA%, EmuDeck, any Steam library). The whole "shaders" folder is
//     searched, not just "shaders_slang", so presets saved next to it are found too.
std::vector<ShaderRoot> find_shader_roots(const std::filesystem::path &addon_dir,
                                          const std::filesystem::path &exe_dir,
                                          const std::vector<std::filesystem::path> &extra, bool auto_find);

// Returns every .slangp file under the roots (subfolders included), sorted by label.
// Folders that cannot be opened are skipped and counted in `skipped`; linked folders
// (symbolic links, junctions) are followed, but no folder is visited twice. RetroArch's
// "shaders_cg" and "shaders_glsl" folders hold no .slangp files and are not searched.
// Stops early, returning what it has found so far, when `cancel` becomes true.
std::vector<PresetEntry> scan_presets(const std::vector<ShaderRoot> &roots, const std::atomic<bool> &cancel,
                                      int &skipped);

// One folder in the tree the window shows: its subfolders and the presets directly in it
// (indexes into the preset list), both sorted by name, ignoring case.
struct PresetFolder
{
    std::string name;
    std::vector<PresetFolder> folders;
    std::vector<size_t> files;
};

// Builds one tree per root (in the order of `roots`) from `presets` as scan_presets returns
// them. A root's tree is named after the root; a root with no presets gets an empty tree.
std::vector<PresetFolder> build_preset_tree(const std::vector<ShaderRoot> &roots,
                                            const std::vector<PresetEntry> &presets);

// The file name of a preset without the ".slangp" extension (any case), e.g.
// "crt/crt-royale.slangp" gives "crt-royale".
std::string preset_display_name(const std::string &rel);
