#pragma once

#include <devex/asset/Project.hpp>

#include <optional>
#include <span>
#include <vector>

namespace devex::asset
{

inline constexpr std::string_view addonManifest = "addon.dvxaddon";
// The class createAddon writes a first editor command in, under code/addons/<id>.
inline constexpr std::string_view addonExampleScript = "Commands.cs";

// A local source package. Install copies its content under assets/addons/<id>, and its code under
// code/addons/<id>, where the existing game builders and external IDEs discover it.
struct DEVEX_API AddonInfo
{
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string license;
    std::filesystem::path directory;
    std::string error;
};

// An editor command advertised by the C# assembly. Only installed, enabled addons expose it.
struct DEVEX_API AddonCommand
{
    std::string addon;
    std::string id;
    std::string name;

    bool operator==(const AddonCommand&) const = default;
};

// What a new addon is made from. An empty id comes from the name.
struct DEVEX_API AddonDraft
{
    std::string id;
    std::string name;
    std::string version = "0.1.0";
    std::string author;
    std::string description;
    std::string license;
    // Also writes a C# class with a first editor command.
    bool exampleCommand = true;
};

// What a package would do to a project, read before anything is copied.
struct DEVEX_API AddonPreview
{
    AddonInfo addon;
    // The version this one would replace, when the project has the addon.
    std::optional<std::string> installedVersion;
    // The source files of code/, which compile with the project and run in the editor.
    std::size_t scripts = 0;
    // What stops it from being installed, one line each.
    std::vector<std::string> conflicts;
};

[[nodiscard]] DEVEX_API bool validAddonId(std::string_view id) noexcept;
[[nodiscard]] DEVEX_API core::Result<AddonInfo> readAddon(const std::filesystem::path& directory);
// Invalid installed manifests are returned with an error, so the editor can explain them.
[[nodiscard]] DEVEX_API std::vector<AddonInfo> installedAddons(const Project& project);
[[nodiscard]] DEVEX_API bool addonEnabled(const Project& project, std::string_view id);
// Lowercase letters and digits joined by dashes: "Scene Tools" gives "scene-tools".
[[nodiscard]] DEVEX_API std::string addonIdFromName(std::string_view name);
// What stops a draft from being created, for the window that asks for one.
[[nodiscard]] DEVEX_API std::optional<std::string> addonDraftProblem(const Project& project,
                                                                     const AddonDraft& draft);
// Makes the addon in place, under assets/addons/<id> and code/addons/<id>, to be worked on there.
[[nodiscard]] DEVEX_API core::Result<AddonInfo> createAddon(const Project& project,
                                                            const AddonDraft& draft);
[[nodiscard]] DEVEX_API core::Result<AddonPreview>
previewAddon(const Project& project, const std::filesystem::path& source);
// Existing destinations are never replaced. A failed install rolls back the files it created.
[[nodiscard]] DEVEX_API core::Result<AddonInfo> installAddon(const Project& project,
                                                             const std::filesystem::path& source);
// Replaces the files of an installed addon by those of its package, or leaves them all in place.
[[nodiscard]] DEVEX_API core::Result<AddonInfo> updateAddon(const Project& project,
                                                            const std::filesystem::path& source);
// Writes an installed addon back as a package, in a new folder named after its id under `parent`.
[[nodiscard]] DEVEX_API core::Result<std::filesystem::path>
exportAddon(const Project& project, std::string_view id, const std::filesystem::path& parent);
// The res:// folders of an addon that exist, for the editor to delete as it deletes other files.
[[nodiscard]] DEVEX_API std::vector<std::string> addonResourcePaths(const Project& project,
                                                                    std::string_view id);
// The commands whose attribute names an addon the project does not have: a misspelled id, mostly.
[[nodiscard]] DEVEX_API std::vector<AddonCommand>
commandsWithoutAddon(const Project& project, std::span<const AddonCommand> commands);

} // namespace devex::asset
