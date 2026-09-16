#pragma once

#include <devex/core/Error.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace devex::asset {

inline constexpr std::string_view projectExtension = ".dvxproj";
inline constexpr std::string_view resourceScheme = "res://";

// A game project: a .dvxproj file whose directory holds the assets/ folder, and the .devex/ cache
// of imported data, which is never versioned.
struct Project
{
    std::string name;
    // Absolute directory of the project file.
    std::filesystem::path root;

    [[nodiscard]] std::filesystem::path assetsDirectory() const
    {
        return root / "assets";
    }

    [[nodiscard]] std::filesystem::path cacheDirectory() const
    {
        return root / ".devex";
    }

    // "res://assets/models/crate.glb" for a file inside the project, empty otherwise.
    [[nodiscard]] std::string resourcePath(const std::filesystem::path& path) const;
    // The absolute path of a res:// path, or nothing when the text is not one.
    [[nodiscard]] std::optional<std::filesystem::path> absolutePath(std::string_view resource) const;
};

// Reads "[project format=1 name="My game"]" from the .dvxproj file.
[[nodiscard]] core::Result<Project> loadProject(const std::filesystem::path& projectFile);

// Writes a .dvxproj file into the directory and creates its assets/ folder.
[[nodiscard]] core::Result<Project> createProject(const std::filesystem::path& directory,
                                                  std::string_view name);

} // namespace devex::asset
