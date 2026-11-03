#pragma once

#include <devex/asset/Project.hpp>

namespace devex::asset
{

inline constexpr std::string_view addonManifest = "addon.dvxaddon";

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
};

[[nodiscard]] DEVEX_API bool validAddonId(std::string_view id) noexcept;
[[nodiscard]] DEVEX_API core::Result<AddonInfo> readAddon(const std::filesystem::path& directory);
// Invalid installed manifests are returned with an error, so the editor can explain them.
[[nodiscard]] DEVEX_API std::vector<AddonInfo> installedAddons(const Project& project);
[[nodiscard]] DEVEX_API bool addonEnabled(const Project& project, std::string_view id);
// Existing destinations are never replaced. A failed install rolls back the files it created.
[[nodiscard]] DEVEX_API core::Result<AddonInfo> installAddon(const Project& project,
                                                             const std::filesystem::path& source);

} // namespace devex::asset
