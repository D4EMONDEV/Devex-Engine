#include <devex/platform/ExportTools.hpp>

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Process.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <mutex>
#include <system_error>
#include <utility>

namespace devex::platform {

namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
constexpr std::string_view programSuffix = ".exe";
#else
constexpr std::string_view programSuffix = "";
#endif

std::mutex chosenMutex;
ExportToolPaths chosen;

// The attributes of [export_tools], by the paths they hold.
constexpr std::array<std::pair<const char*, fs::path ExportToolPaths::*>, 4> attributes{{
    {"android_sdk", &ExportToolPaths::androidSdk},
    {"android_ndk", &ExportToolPaths::androidNdk},
    {"java", &ExportToolPaths::java},
    {"emsdk", &ExportToolPaths::emsdk},
}};

[[nodiscard]] fs::path environmentPath(const char* name)
{
    return core::pathFromUtf8(environmentVariable(name));
}

[[nodiscard]] bool isDirectory(const fs::path& path)
{
    std::error_code error;
    return !path.empty() && fs::is_directory(path, error);
}

[[nodiscard]] bool isFile(const fs::path& path)
{
    std::error_code error;
    return !path.empty() && fs::is_regular_file(path, error);
}

[[nodiscard]] bool isAndroidSdk(const fs::path& path)
{
    return isDirectory(path / "platform-tools") || isDirectory(path / "platforms");
}

[[nodiscard]] bool isAndroidNdk(const fs::path& path)
{
    return isDirectory(path / "build" / "cmake");
}

[[nodiscard]] fs::path home()
{
#ifdef _WIN32
    return environmentPath("USERPROFILE");
#else
    return environmentPath("HOME");
#endif
}

} // namespace

void setExportToolPaths(ExportToolPaths paths)
{
    const std::scoped_lock lock(chosenMutex);
    chosen = std::move(paths);
}

ExportToolPaths exportToolPaths()
{
    const std::scoped_lock lock(chosenMutex);
    return chosen;
}

ExportToolPaths readExportToolPaths(const serialization::TextSection& section)
{
    ExportToolPaths paths;
    for (const auto& [name, member] : attributes)
    {
        if (const std::string* const value = serialization::stringAttribute(section, name))
        {
            paths.*member = core::pathFromUtf8(*value);
        }
    }
    return paths;
}

serialization::TextSection writeExportToolPaths(const ExportToolPaths& paths)
{
    serialization::TextSection section;
    section.type = "export_tools";
    for (const auto& [name, member] : attributes)
    {
        if (!(paths.*member).empty())
        {
            section.attributes.push_back({name, serialization::TextValue(core::toUtf8(paths.*member))});
        }
    }
    return section;
}

void loadExportToolPaths()
{
    const core::Result<fs::path> editor = userDataLocation("Devex", "Editor");
    if (!editor)
    {
        return;
    }
    const core::Result<std::string> text = core::readTextFile(*editor / "editor.dvx");
    if (!text)
    {
        return;
    }
    const core::Result<serialization::TextDocument> document = serialization::parseText(*text);
    if (!document)
    {
        return;
    }
    for (const serialization::TextSection& section : document->sections)
    {
        if (section.type == "export_tools")
        {
            setExportToolPaths(readExportToolPaths(section));
        }
    }
}

fs::path findAndroidSdk()
{
    if (const fs::path given = exportToolPaths().androidSdk; !given.empty())
    {
        return isAndroidSdk(given) ? given : fs::path{};
    }
    const fs::path user = home();
    for (const fs::path& candidate : {environmentPath("ANDROID_HOME"), environmentPath("ANDROID_SDK_ROOT"),
#ifdef _WIN32
                                      environmentPath("LOCALAPPDATA") / "Android" / "Sdk",
#else
                                      user / "Android" / "Sdk", user / "Library" / "Android" / "sdk",
#endif
         })
    {
        if (isAndroidSdk(candidate))
        {
            return candidate;
        }
    }
    return {};
}

fs::path findAndroidNdk(const fs::path& sdk)
{
    if (const fs::path given = exportToolPaths().androidNdk; !given.empty())
    {
        return isAndroidNdk(given) ? given : fs::path{};
    }
    for (const fs::path& candidate : {environmentPath("ANDROID_NDK_HOME"), environmentPath("ANDROID_NDK_ROOT")})
    {
        if (isAndroidNdk(candidate))
        {
            return candidate;
        }
    }
    std::vector<fs::path> versions;
    std::error_code error;
    if (!sdk.empty())
    {
        for (const fs::directory_entry& entry : fs::directory_iterator(sdk / "ndk", error))
        {
            if (isAndroidNdk(entry.path()))
            {
                versions.push_back(entry.path());
            }
        }
    }
    std::ranges::sort(versions);
    return versions.empty() ? fs::path{} : versions.back();
}

fs::path findJava()
{
    const std::string java = std::format("java{}", programSuffix);
    if (const fs::path given = exportToolPaths().java; !given.empty())
    {
        if (isFile(given / "bin" / java))
        {
            return given / "bin" / java;
        }
        return isFile(given) ? given : fs::path{};
    }
    std::vector<fs::path> homes{environmentPath("JAVA_HOME")};
#ifdef _WIN32
    homes.push_back(environmentPath("ProgramFiles") / "Android" / "Android Studio" / "jbr");
#else
    homes.push_back(home() / "android-studio" / "jbr");
    homes.emplace_back("/opt/android-studio/jbr");
    homes.emplace_back("/Applications/Android Studio.app/Contents/jbr/Contents/Home");
#endif
    for (const fs::path& jdk : homes)
    {
        if (isFile(jdk / "bin" / java))
        {
            return jdk / "bin" / java;
        }
    }
    return java;
}

std::vector<fs::path> emsdkCandidates()
{
    if (fs::path given = exportToolPaths().emsdk; !given.empty())
    {
        return {std::move(given)};
    }
#ifdef _WIN32
    return {environmentPath("EMSDK"), home() / "emsdk", "C:/emsdk"};
#else
    return {environmentPath("EMSDK"), home() / "emsdk", "/opt/emsdk"};
#endif
}

std::optional<std::string> emscriptenVersion(const fs::path& emsdk)
{
    const fs::path emscripten = emsdk / "upstream" / "emscripten";
    if (emsdk.empty() || !isFile(emscripten / "cmake" / "Modules" / "Platform" / "Emscripten.cmake"))
    {
        return std::nullopt;
    }
    // "7.0.0", with its quotes.
    std::string version = core::readTextFile(emscripten / "emscripten-version.txt").value_or(std::string{});
    std::erase_if(version, [](char c) { return c == '"' || std::isspace(static_cast<unsigned char>(c)) != 0; });
    return version;
}

} // namespace devex::platform
