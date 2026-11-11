#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Uuid.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace devex::scene
{
class Scene;
}

namespace devex::tools::detail
{

struct RemoteInspectionTarget
{
    // Player numbers start at one. Zero means the editor is showing its own scene.
    std::uint32_t player = 0;
    core::Uuid entity;
};

struct RemoteEntity
{
    core::Uuid id;
    std::string name;
    std::uint32_t depth = 0;
};

struct RemoteProperty
{
    std::string name;
    std::string value;
};

struct RemoteComponent
{
    std::string name;
    std::vector<RemoteProperty> properties;
};

struct RemoteSceneSnapshot
{
    // Preorder, in the same sibling order as the running scene.
    std::vector<RemoteEntity> entities;
    core::Uuid selection;
    std::optional<std::string> selectedName;
    std::vector<RemoteComponent> components;
    bool treeLimited = false;
    bool propertiesLimited = false;
};

struct RemoteInspectionView
{
    // The PID in each player slot, or zero while it is unavailable.
    std::array<std::uint32_t, 4> processes{};
    std::uint32_t player = 0;
    std::shared_ptr<const RemoteSceneSnapshot> snapshot;
    bool unavailable = false;
};

} // namespace devex::tools::detail

namespace devex::runtime::detail
{

inline constexpr auto remoteInspectionFile = "inspection.dvx";

// The pipe carries only the request and its acknowledgement. The snapshot is bounded and
// replaced atomically in this player's existing session directory.
[[nodiscard]] core::Result<void> writeRemoteInspection(const std::filesystem::path& path,
                                                       const scene::Scene& scene,
                                                       std::int64_t serial, core::Uuid selected);
[[nodiscard]] core::Result<tools::detail::RemoteSceneSnapshot>
readRemoteInspection(const std::filesystem::path& path, std::int64_t serial, core::Uuid selected);

} // namespace devex::runtime::detail
