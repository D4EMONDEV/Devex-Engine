#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Navigation meshes: where agents walk, baked from the static colliders of a scene by the editor.
namespace devex::asset {

// A navigation mesh baked for a scene: a binary file, the same bytes as its cooked asset.
inline constexpr std::string_view navMeshExtension = ".dvxnavmesh";

// What a navigation mesh is baked for: the size of the agents that walk it, and how finely the
// scene is cut into voxels to find where they fit.
struct DEVEX_API NavMeshBuildSettings
{
    float agentRadius = 0.4f;
    float agentHeight = 1.8f;
    // The highest step an agent climbs.
    float agentMaxClimb = 0.4f;
    // The steepest slope it walks up, in radians.
    float agentMaxSlope = math::radians(45.0f);
    // The size of a voxel, across and up, in meters.
    float cellSize = 0.2f;
    float cellHeight = 0.1f;
    // Voxels along a side of a tile: the mesh is rebuilt tile by tile around moving obstacles.
    std::int32_t tileSize = 48;

    bool operator==(const NavMeshBuildSettings&) const = default;
};

struct DEVEX_API NavMeshData
{
    NavMeshBuildSettings settings;
    // The corner of the grid of tiles, the far corner of what was baked, and how many tiles the
    // grid has along X and Z.
    math::Vec3 origin{0.0f};
    math::Vec3 boundsMax{0.0f};
    std::int32_t tilesX = 0;
    std::int32_t tilesZ = 0;
    // The layers of the tiles, compressed, as Detour's tile cache reads them: the navigation mesh is
    // built from them when it loads, and again around obstacles.
    std::vector<std::vector<std::byte>> layers;
    // How many triangles of scenery the bake read, for the editor.
    std::uint32_t triangles = 0;

    bool operator==(const NavMeshData&) const = default;
};

// Settings that bake, a grid of tiles, and layers that hold data.
[[nodiscard]] DEVEX_API core::Result<void> validate(const NavMeshData& navMesh);

} // namespace devex::asset
