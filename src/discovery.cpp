// Implementation of discovery.h: the folders searched for presets, and the search.

#include "discovery.h"
#include "utf8.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace
{
// True if `p` is an existing folder (false on any error, e.g. no access).
bool is_dir(const fs::path &p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

// Returns the text value `value` of registry key `key` under `root`, or an empty string
// if it does not exist.
std::wstring reg_string(HKEY root, const wchar_t *key, const wchar_t *value)
{
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return {};
    return buf;
}

// Returns the folders Steam installs games into ("libraries"): Steam's own folder (from
// the registry) and those listed in its steamapps/libraryfolders.vdf. Empty if Steam is not
// installed.
std::vector<fs::path> steam_libraries()
{
    std::vector<fs::path> libs;
    const std::wstring steam = reg_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty())
        return libs;
    libs.push_back(fs::path(steam));
    std::ifstream vdf(fs::path(steam) / L"steamapps" / L"libraryfolders.vdf");
    std::stringstream text;
    text << vdf.rdbuf();
    const std::string s = text.str();
    static const std::regex path_re("\"path\"\\s+\"([^\"]+)\"");
    for (auto it = std::sregex_iterator(s.begin(), s.end(), path_re); it != std::sregex_iterator(); ++it)
    {
        std::string p = (*it)[1].str();
        // VDF escapes backslashes.
        for (size_t i = p.find("\\\\"); i != std::string::npos; i = p.find("\\\\", i + 1))
            p.erase(i, 1);
        libs.push_back(path_from_utf8(p));
    }
    return libs;
}
}

namespace
{
// Returns environment variable `name`, or an empty path if it is not set.
fs::path env_path(const wchar_t *name)
{
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? fs::path(buf) : fs::path();
}

// A name for a ShaderPaths folder: the folder's name, and its parent's when the name alone
// says little ("shaders" in a RetroArch folder becomes "RetroArch/shaders").
std::string user_root_name(const fs::path &dir)
{
    fs::path p = dir;
    if (!p.has_filename())
        p = p.parent_path(); // "C:\x\" has an empty filename
    std::string name = utf8_from_path(p.filename());
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if ((lower == "shaders" || lower == "shaders_slang" || lower == "slang-shaders") && p.has_parent_path() &&
        p.parent_path() != p)
        name = utf8_from_path(p.parent_path().filename()) + "/" + name;
    return name.empty() ? utf8_from_path(dir) : name;
}

// Folder names never searched: RetroArch's folders for its older shader formats, which hold
// thousands of files but no .slangp, and version control folders.
bool skip_folder(const fs::path &dir)
{
    std::wstring n = dir.filename().wstring();
    std::transform(n.begin(), n.end(), n.begin(), ::towlower);
    return n == L"shaders_cg" || n == L"shaders_glsl" || n == L".git";
}
} // namespace

std::vector<ShaderRoot> find_shader_roots(const fs::path &addon_dir, const fs::path &exe_dir,
                                          const std::vector<fs::path> &extra, bool auto_find)
{
    std::vector<ShaderRoot> roots;
    // Adds `dir` under `name` if it exists and is not the same folder as one already added.
    auto add = [&](const std::string &name, const fs::path &dir, bool user) {
        if (!is_dir(dir))
            return;
        std::error_code ec;
        const fs::path canon = fs::weakly_canonical(dir, ec);
        for (const ShaderRoot &r : roots)
            if (fs::equivalent(r.dir, canon, ec))
                return;
        roots.push_back({name, ec ? dir : canon, user});
    };

    for (const fs::path &p : extra)
        add(user_root_name(p), p, true);

    add("retroarch-shaders", addon_dir / L"retroarch-shaders", false);
    add("retroarch-shaders", exe_dir / L"retroarch-shaders", false);
    if (!auto_find)
        return roots;

    // RetroArch keeps its shaders in a "shaders" folder (slang ones in shaders\shaders_slang),
    // wherever it is installed. A folder already added (e.g. through ShaderPaths) is skipped.
    const fs::path shaders = L"shaders";
    add("RetroArch", fs::path(L"C:\\RetroArch-Win64") / shaders, false);
    add("RetroArch", fs::path(L"C:\\RetroArch") / shaders, false);
    if (const fs::path appdata = env_path(L"APPDATA"); !appdata.empty())
        add("RetroArch", appdata / L"RetroArch" / shaders, false);
    if (const fs::path home = env_path(L"USERPROFILE"); !home.empty())
        add("RetroArch (EmuDeck)", home / L"EmuDeck" / L"EmulationStation-DE" / L"Emulators" / L"RetroArch" / shaders,
            false);
    for (const fs::path &lib : steam_libraries())
        add("RetroArch (Steam)", lib / L"steamapps" / L"common" / L"RetroArch" / shaders, false);
    return roots;
}

namespace
{
// Case-insensitive "less than" for names in the tree.
bool name_less(const std::string &a, const std::string &b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return tolower(static_cast<unsigned char>(x)) < tolower(static_cast<unsigned char>(y));
    });
}

// Sorts a folder's subfolders and files by name, then does the same inside each subfolder.
void sort_tree(PresetFolder &f, const std::vector<PresetEntry> &presets)
{
    std::sort(f.folders.begin(), f.folders.end(),
              [](const PresetFolder &a, const PresetFolder &b) { return name_less(a.name, b.name); });
    std::sort(f.files.begin(), f.files.end(), [&](size_t a, size_t b) {
        return name_less(preset_display_name(presets[a].rel), preset_display_name(presets[b].rel));
    });
    for (PresetFolder &sub : f.folders)
        sort_tree(sub, presets);
}
} // namespace

std::string preset_display_name(const std::string &rel)
{
    const size_t slash = rel.find_last_of('/');
    std::string name = slash == std::string::npos ? rel : rel.substr(slash + 1);
    if (name.size() > 7)
    {
        std::string ext = name.substr(name.size() - 7);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".slangp")
            name.resize(name.size() - 7);
    }
    return name;
}

std::vector<PresetFolder> build_preset_tree(const std::vector<ShaderRoot> &roots,
                                            const std::vector<PresetEntry> &presets)
{
    std::vector<PresetFolder> trees(roots.size());
    for (size_t r = 0; r < roots.size(); ++r)
        trees[r].name = roots[r].name;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const PresetEntry &e = presets[i];
        if (e.root >= trees.size())
            continue;
        // Walk (creating as needed) the folders of e.rel, then add the file to the last one.
        PresetFolder *f = &trees[e.root];
        size_t start = 0;
        for (size_t slash = e.rel.find('/'); slash != std::string::npos; slash = e.rel.find('/', start))
        {
            const std::string part = e.rel.substr(start, slash - start);
            start = slash + 1;
            if (part.empty())
                continue;
            auto it = std::find_if(f->folders.begin(), f->folders.end(),
                                   [&](const PresetFolder &c) { return c.name == part; });
            if (it == f->folders.end())
            {
                f->folders.push_back({part, {}, {}});
                it = f->folders.end() - 1;
            }
            f = &*it;
        }
        f->files.push_back(i);
    }
    for (PresetFolder &t : trees)
        sort_tree(t, presets);
    return trees;
}

std::vector<PresetEntry> scan_presets(const std::vector<ShaderRoot> &roots, const std::atomic<bool> &cancel,
                                      int &skipped)
{
    std::vector<PresetEntry> out;
    std::unordered_set<std::wstring> visited; // canonical folders, so links cannot loop
    skipped = 0;
    // True the first time a folder is found (by its real path, ignoring case), false after.
    auto first_visit = [&](const fs::path &dir) {
        std::error_code ec;
        const fs::path canon = fs::canonical(dir, ec);
        std::wstring key = (ec ? dir : canon).wstring();
        std::transform(key.begin(), key.end(), key.begin(), ::towlower);
        return visited.insert(key).second;
    };
    // Walk each root's folder tree with a list of folders still to open (no recursion, so
    // deep trees cannot overflow the stack).
    for (size_t root_index = 0; root_index < roots.size(); ++root_index)
    {
        const ShaderRoot &root = roots[root_index];
        std::vector<fs::path> pending;
        if (first_visit(root.dir))
            pending.push_back(root.dir);
        while (!pending.empty() && !cancel.load(std::memory_order_relaxed))
        {
            const fs::path dir = std::move(pending.back());
            pending.pop_back();
            std::error_code ec;
            fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
            if (ec)
            {
                ++skipped; // e.g. a path too long for the game process: skip it, keep going
                continue;
            }
            for (; it != end && !cancel.load(std::memory_order_relaxed); it.increment(ec))
            {
                if (ec)
                {
                    ++skipped;
                    break;
                }
                const fs::path &path = it->path();
                if (it->is_directory(ec)) // follows links
                {
                    if (!ec && !skip_folder(path) && first_visit(path))
                        pending.push_back(path);
                    continue;
                }
                if (!it->is_regular_file(ec))
                    continue;
                std::wstring e = path.extension().wstring();
                std::transform(e.begin(), e.end(), e.begin(), ::towlower);
                if (e != L".slangp")
                    continue;
                // Lexical: through a link, fs::relative would use the link's target and
                // give "../..".
                const fs::path rel = path.lexically_relative(root.dir);
                const std::string rel_text = utf8_from_path(rel.empty() ? path.filename() : rel, true);
                out.push_back({root.name + "/" + rel_text, path, root_index, rel_text});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const PresetEntry &a, const PresetEntry &b) { return a.label < b.label; });
    return out;
}
