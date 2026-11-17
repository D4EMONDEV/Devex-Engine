#pragma once

#include <devex/core/Export.hpp>
#include <devex/serialization/Text.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace devex::platform {

// The tools that the exports for Android and the web run, as the user chose them in Editor Settings > Export. An empty
// path is looked for: through the environment, then where the tool is usually installed. A chosen path passes before
// all, and is not looked past when it is not the tool.
struct DEVEX_API ExportToolPaths
{
    std::filesystem::path androidSdk;
    std::filesystem::path androidNdk;
    // The folder of a JDK, which holds bin/java, or the java program itself.
    std::filesystem::path java;
    std::filesystem::path emsdk;

    bool operator==(const ExportToolPaths&) const = default;
};

// What every export of this process reads: the editor sets it from its settings.
DEVEX_API void setExportToolPaths(ExportToolPaths paths);
[[nodiscard]] DEVEX_API ExportToolPaths exportToolPaths();
// The section [export_tools] of the settings of the editor, editor.dvx, which leaves out the empty paths.
[[nodiscard]] DEVEX_API ExportToolPaths readExportToolPaths(const serialization::TextSection& section);
[[nodiscard]] DEVEX_API serialization::TextSection writeExportToolPaths(const ExportToolPaths& paths);
// Sets the paths from the settings of the editor of this user, as an export from the command line does.
DEVEX_API void loadExportToolPaths();

// The Android SDK, empty when there is none.
[[nodiscard]] DEVEX_API std::filesystem::path findAndroidSdk();
// The NDK chosen, named by the environment, or the newest of the SDK; empty when there is none.
[[nodiscard]] DEVEX_API std::filesystem::path findAndroidNdk(const std::filesystem::path& sdk);
// The java program of the JDK chosen, of JAVA_HOME or of Android Studio, or else "java", for the system to find; empty
// when the one chosen is not one.
[[nodiscard]] DEVEX_API std::filesystem::path findJava();
// Where an Emscripten SDK is looked for: the one chosen alone, or else EMSDK and where emsdk is usually cloned.
[[nodiscard]] DEVEX_API std::vector<std::filesystem::path> emsdkCandidates();
// The version of the Emscripten of an SDK, "7.0.0", empty when it does not say; none when the folder is no SDK.
[[nodiscard]] DEVEX_API std::optional<std::string> emscriptenVersion(const std::filesystem::path& emsdk);

} // namespace devex::platform
