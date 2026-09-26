#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/AssetType.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/Error.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devex::asset {

// An asset whose cooked data can be loaded.
struct AssetInfo
{
    AssetId id;
    AssetType type = AssetType::Mesh;
    std::string name;
    // Main asset of the source file that produced it: its own identifier for a main asset.
    AssetId source;
};

// Reads the cooked data of one asset. It may run on any thread while its source lives.
using ArtifactReader = std::function<core::Result<std::vector<std::byte>>()>;

// Where a game loads its assets from: the asset database of its project while it is developed,
// the package of the exported game once it ships (asset::PackageReader).
class AssetSource
{
public:
    virtual ~AssetSource() = default;

    // The settings of the game. The root of an exported game is the folder of its package.
    [[nodiscard]] virtual const Project& project() const noexcept = 0;

    [[nodiscard]] virtual const AssetInfo* find(AssetId id) const = 0;
    // Sorted by name, optionally of one type.
    [[nodiscard]] virtual std::vector<AssetInfo> assets(std::optional<AssetType> type = std::nullopt) const = 0;
    // The main asset of a source file, by res:// path.
    [[nodiscard]] virtual std::optional<AssetId> findByPath(std::string_view resourcePath) const = 0;

    [[nodiscard]] virtual core::Result<std::vector<std::byte>> loadArtifact(AssetId id) const = 0;
    // What loads the artifact on another thread. Asked for on the thread that uses the source, it
    // takes from the source what may change meanwhile. By default it calls loadArtifact, for sources
    // that do not change once opened, such as a package.
    [[nodiscard]] virtual ArtifactReader artifactReader(AssetId id) const
    {
        return [this, id] { return loadArtifact(id); };
    }
    // The text of a scene. In a project, it comes from the source file, which can be newer than
    // its import.
    [[nodiscard]] virtual core::Result<std::string> sceneText(AssetId id) const = 0;

protected:
    AssetSource() = default;
    AssetSource(const AssetSource&) = default;
    AssetSource& operator=(const AssetSource&) = default;
};

} // namespace devex::asset
