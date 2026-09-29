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

// What agents walk on and around: the colliders of the scene that stay put, as triangles in world
// space. Triggers, the colliders of dynamic and kinematic bodies and characters are left out. Boxes,
// cylinders and meshes are exact; spheres and capsules are rounded with a few sides.
[[nodiscard]] DEVEX_API NavGeometry collectGeometry(const scene::Scene& scene, const MeshSource& meshes);

[[nodiscard]] DEVEX_API asset::NavMeshBuildSettings buildSettingsOf(const scene::NavMeshSurface& surface) noexcept;

// Cuts the scenery into voxels, finds where agents of the settings fit, and keeps the layers of
// each tile, compressed, for the navigation mesh to be built from when it loads.
[[nodiscard]] DEVEX_API core::Result<asset::NavMeshData> bakeNavMesh(const NavGeometry& geometry,
                                                                     const asset::NavMeshBuildSettings& settings);

// The edges of the polygons of a navigation mesh as pairs of points, those along its borders
// apart from those between its polygons, for the editor to draw.
struct DEVEX_API NavMeshLines
{
    std::vector<math::Vec3> borders;
    std::vector<math::Vec3> inner;
    std::size_t polygons = 0;
};
[[nodiscard]] DEVEX_API core::Result<NavMeshLines> navMeshLines(const asset::NavMeshData& navMesh);

} // namespace devex::navigation
