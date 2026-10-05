#include "ScriptSettings.hpp"

#include <devex/core/Path.hpp>
#include <devex/platform/Process.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <system_error>

namespace devex::tools::detail {
namespace {

std::string lower(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Known executables only, never shell associations or arbitrary programs found in these folders.
#ifdef _WIN32
constexpr std::array<std::string_view, 7> executableNames{
    "rider64.exe", "rider.exe", "clion64.exe", "clion.exe", "devenv.exe", "Code.exe", "Code - Insiders.exe"};
#else
constexpr std::array<std::string_view, 6> executableNames{
    "rider", "rider.sh", "clion", "clion.sh", "code", "code-insiders"};
#endif

void installation(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& root)
{
    for (const auto name : executableNames)
    {
        const auto executable = core::pathFromUtf8(name);
        candidates.push_back(root / executable);
        candidates.push_back(root / "bin" / executable);
    }
    candidates.push_back(root / "Common7/IDE/devenv.exe");
    candidates.push_back(root / "Contents/MacOS/rider");
    candidates.push_back(root / "Contents/MacOS/clion");
    candidates.push_back(root / "Contents/MacOS/Electron");
}

template<class Visitor>
void directories(const std::filesystem::path& root, Visitor visit)
{
    if (root.empty())
    {
        return;
    }
    std::error_code error;
    for (std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error))
    {
        if (it->is_directory(error) && !it->is_symlink(error))
        {
            visit(it->path());
        }
        error.clear();
    }
}

void jetBrains(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& root, bool toolbox = false)
{
    directories(root, [&](const auto& product) {
        const std::string name = lower(core::toUtf8(product.filename()));
        if (!name.starts_with("rider") && !name.starts_with("clion"))
        {
            return;
        }
        installation(candidates, product);
        if (toolbox)
        {
            // Older Toolbox installations use product/channel/version. Do not traverse IDE contents.
            directories(product, [&](const auto& channel) {
                installation(candidates, channel);
                directories(channel, [&](const auto& version) { installation(candidates, version); });
            });
        }
    });
}

} // namespace

ExternalEditor externalEditorKind(const std::filesystem::path& executable)
{
    const std::string name = lower(core::toUtf8(executable.filename()));
    if (name == "rider64.exe" || name == "rider.exe" || name == "rider" || name == "rider.sh")
    {
        return ExternalEditor::Rider;
    }
    if (name == "clion64.exe" || name == "clion.exe" || name == "clion" || name == "clion.sh")
    {
        return ExternalEditor::CLion;
    }
    if (name == "devenv.exe")
    {
        return ExternalEditor::VisualStudio;
    }
    if (name == "code.exe" || name == "code - insiders.exe" || name == "code" || name == "code-insiders" ||
        (name == "electron" && lower(core::toUtf8(executable)).find("visual studio code") != std::string::npos))
    {
        return ExternalEditor::VSCode;
    }
    return ExternalEditor::Unknown;
}

bool sameEditorPath(const std::filesystem::path& left, const std::filesystem::path& right)
{
#ifdef _WIN32
    const bool sameSpelling = lower(core::toUtf8(left.lexically_normal())) == lower(core::toUtf8(right.lexically_normal()));
#else
    const bool sameSpelling = left.lexically_normal() == right.lexically_normal();
#endif
    if (sameSpelling)
    {
        return true;
    }
    // Discovery returns canonical paths, but settings and TEMP can use short Windows names
    // (RUNNER~1 on CI) or filesystem links to the same installation.
    std::error_code error;
    return std::filesystem::equivalent(left, right, error) && !error;
}

std::vector<InstalledScriptEditor> findScriptEditors(std::span<const std::filesystem::path> candidates)
{
    std::vector<InstalledScriptEditor> found;
    for (const auto& candidate : candidates)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(candidate, error))
        {
            continue;
        }
        const auto path = std::filesystem::weakly_canonical(candidate, error);
        if (error || std::ranges::any_of(found, [&](const auto& editor) {
            return sameEditorPath(editor.executable, path) ||
                (externalEditorKind(path) == externalEditorKind(editor.executable) &&
                 sameEditorPath(editor.executable.parent_path(), path.parent_path()));
        }))
        {
            continue;
        }
        std::string name;
        switch (externalEditorKind(path))
        {
        case ExternalEditor::Rider: name = "JetBrains Rider"; break;
        case ExternalEditor::CLion: name = "JetBrains CLion"; break;
        case ExternalEditor::VisualStudio:
            name = "Visual Studio " + core::toUtf8(path.parent_path().parent_path().parent_path().parent_path().filename()) +
                   " " + core::toUtf8(path.parent_path().parent_path().parent_path().filename());
            break;
        case ExternalEditor::VSCode:
            name = lower(core::toUtf8(path)).find("insiders") != std::string::npos ? "Visual Studio Code Insiders" : "Visual Studio Code";
            break;
        default: continue;
        }
        found.push_back({std::move(name), path});
    }
    // Different versions remain selectable. The full path is shown beneath the selection.
    std::ranges::sort(found, [](const auto& left, const auto& right) {
        return left.name != right.name ? left.name < right.name : left.executable < right.executable;
    });
    for (std::size_t i = 0; i < found.size();)
    {
        std::size_t end = i + 1;
        while (end < found.size() && found[end].name == found[i].name) { ++end; }
        if (end - i > 1)
        {
            for (std::size_t at = i; at < end; ++at)
            {
                found[at].name += " (" + std::to_string(at - i + 1) + ")";
            }
        }
        i = end;
    }
    return found;
}

std::vector<InstalledScriptEditor> detectScriptEditors()
{
    std::vector<std::filesystem::path> candidates;
    const auto environmentPath = [](std::string_view name) { return core::pathFromUtf8(platform::environmentVariable(name)); };
#ifdef _WIN32
    const auto local = environmentPath("LOCALAPPDATA");
    if (!local.empty())
    {
        jetBrains(candidates, local / "Programs");
        jetBrains(candidates, local / "JetBrains/Toolbox/apps", true);
        installation(candidates, local / "Programs/Microsoft VS Code");
        installation(candidates, local / "Programs/Microsoft VS Code Insiders");
    }
    for (const auto variable : {"ProgramFiles", "ProgramFiles(x86)"})
    {
        const auto programs = environmentPath(variable);
        if (programs.empty()) { continue; }
        jetBrains(candidates, programs / "JetBrains");
        installation(candidates, programs / "Microsoft VS Code");
        installation(candidates, programs / "Microsoft VS Code Insiders");
        directories(programs / "Microsoft Visual Studio", [&](const auto& version) {
            directories(version, [&](const auto& edition) { installation(candidates, edition); });
        });
    }
    constexpr char separator = ';';
#else
    const auto home = environmentPath("HOME");
    if (!home.empty())
    {
        jetBrains(candidates, home / ".local/share/JetBrains/Toolbox/apps", true);
        jetBrains(candidates, home / "Applications");
        installation(candidates, home / "Applications/Visual Studio Code.app");
    }
    jetBrains(candidates, "/Applications");
    jetBrains(candidates, "/opt");
    installation(candidates, "/Applications/Visual Studio Code.app");
    constexpr char separator = ':';
#endif
    const std::string path = platform::environmentVariable("PATH");
    for (std::size_t start = 0; start < path.size();)
    {
        const auto end = path.find(separator, start);
        std::string entry = path.substr(start, end == std::string::npos ? end : end - start);
        if (entry.size() > 1 && entry.front() == '"' && entry.back() == '"') { entry = entry.substr(1, entry.size() - 2); }
        if (!entry.empty())
        {
            const auto directory = core::pathFromUtf8(entry);
            for (const auto name : executableNames) { candidates.push_back(directory / core::pathFromUtf8(name)); }
            // VS Code's Windows PATH entry points to its bin folder, containing a .cmd launcher.
            candidates.push_back(directory.parent_path() / "Code.exe");
            candidates.push_back(directory.parent_path() / "Code - Insiders.exe");
        }
        if (end == std::string::npos) { break; }
        start = end + 1;
    }
    return findScriptEditors(candidates);
}

std::filesystem::path scriptProjectFile(const std::filesystem::path& file, const std::filesystem::path& project)
{
    const auto category = scriptEditorCategory(file);
    if (!category || *category > 1) { return {}; }
    const auto target = project / "code" / (*category == 0 ? "Game.csproj" : "CMakeLists.txt");
    std::error_code error;
    return std::filesystem::is_regular_file(target, error) ? target : std::filesystem::path{};
}

} // namespace devex::tools::detail
