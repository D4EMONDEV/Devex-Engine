#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/asset/MeshData.hpp>
#include <devex/asset/NavMeshData.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace devex::asset {
struct TerrainMaps;
}

namespace devex::scene {
class Scene;
struct NavMeshSurface;
}

// Navigation: the meshes agents walk, baked with Recast from the scenery, and the agents that walk
// them with Detour.
namespace devex::navigation {

// Triangles in world space.
struct DEVEX_API NavGeometry
{
    std::vector<math::Vec3> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] std::size_t triangleCount() const noexcept
    {
        return indices.size() / 3;
    }
};

// The mesh of an asset, for mesh colliders; null when it cannot be loaded.
using MeshSource = std::function<const asset::MeshData*(asset::AssetId mesh)>;

// The current maps of a terrain, including edits not saved yet. They must remain valid during bake.
using TerrainSource = std::function<const asset::TerrainMaps*(asset::AssetId terrain)>;

// What agents walk on and around: the colliders of the scene that stay put, as triangles in world
// space. Triggers, the colliders of dynamic and kinematic bodies and characters are left out. Boxes,
// cylinders and meshes are exact; spheres and capsules are rounded with a few sides.
[[nodiscard]] DEVEX_API NavGeometry collectGeometry(const scene::Scene& scene, const MeshSource& meshes);

[[nodiscard]] DEVEX_API asset::NavMeshBuildSettings buildSettingsOf(const scene::NavMeshSurface& surface) noexcept;

// Cuts the scenery into voxels, finds where agents of the settings fit, and keeps the layers of
// each tile, compressed, for the navigation mesh to be built from when it loads.
[[nodiscard]] DEVEX_API core::Result<asset::NavMeshData> bakeNavMesh(const NavGeometry& geometry,
                                                                     const asset::NavMeshBuildSettings& settings);

// Bakes static colliders and collidable terrains together. Terrain samples are rasterized in strips
// over each tile, with Jolt's triangle diagonal and holes, without expanding the whole height map.
// Terrains follow their world position and size; their rotation and scale are ignored, as in physics.
// Update the scene's world transforms before baking.
[[nodiscard]] DEVEX_API core::Result<asset::NavMeshData> bakeNavMesh(const scene::Scene& scene,
                                                                     const MeshSource& meshes,
                                                                     const TerrainSource& terrains,
                                                                     const asset::NavMeshBuildSettings& settings);

// The edges of the detailed navigation mesh as pairs of points following the ground, those along
// its borders apart from those inside, for the editor to draw.
struct DEVEX_API NavMeshLines
{
    std::vector<math::Vec3> borders;
    std::vector<math::Vec3> inner;
    std::size_t polygons = 0;
};
[[nodiscard]] DEVEX_API core::Result<NavMeshLines> navMeshLines(const asset::NavMeshData& navMesh);

} // namespace devex::navigation
