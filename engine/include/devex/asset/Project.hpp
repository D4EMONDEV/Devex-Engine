#pragma once

#include <devex/core/Error.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace devex::asset {

inline constexpr std::string_view projectExtension = ".dvxproj";
inline constexpr std::string_view resourceScheme = "res://";

// A game project: a .dvxproj file whose directory holds the assets/ folder, the code/ folder of its
// game module when it has one, and the .devex/ cache of imported data and builds, which is never
// versioned.
struct Project
{
    std::string name;
    // Absolute directory of the project file.
    std::filesystem::path root;
    // Absolute path of the .dvxproj file.
    std::filesystem::path file;
    // The res:// path of the scene the player opens; empty for the first scene of the project.
    std::string startupScene;

    [[nodiscard]] std::filesystem::path assetsDirectory() const
    {
        return root / "assets";
    }

    [[nodiscard]] std::filesystem::path cacheDirectory() const
    {
        return root / ".devex";
    }

    // The sources of the game module, built with CMake.
    [[nodiscard]] std::filesystem::path codeDirectory() const
    {
        return root / "code";
    }

    // "res://assets/models/crate.glb" for a file inside the project, empty otherwise.
    [[nodiscard]] std::string resourcePath(const std::filesystem::path& path) const;
    // The absolute path of a res:// path, or nothing when the text is not one.
    [[nodiscard]] std::optional<std::filesystem::path> absolutePath(std::string_view resource) const;
};

// Reads "[project format=1 name="My game" startup_scene="res://assets/scenes/Main.dvxscene"]" from
// the .dvxproj file.
[[nodiscard]] core::Result<Project> loadProject(const std::filesystem::path& projectFile);

// Writes the project's settings to its .dvxproj file.
[[nodiscard]] core::Result<void> saveProject(const Project& project);

// Writes a .dvxproj file into the directory and creates its assets/ folder.
[[nodiscard]] core::Result<Project> createProject(const std::filesystem::path& directory,
                                                  std::string_view name);

} // namespace devex::asset
