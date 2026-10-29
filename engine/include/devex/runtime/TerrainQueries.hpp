#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/scene/Scene.hpp>

#include <optional>

// The ground of the terrains of a scene, in the world: where it stands under a point, which way it
// faces, and where a ray meets it. What C++ game code reads, and C# through Terrains.
namespace devex::runtime {

// Where a ray met the ground of a terrain.
struct DEVEX_API TerrainHit
{
    math::Vec3 point{0.0f};
    math::Vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
};

// The height in the world of the ground of the terrain of an entity, above or below a point; nothing
// outside its square, over a hole, or when its data cannot be loaded.
[[nodiscard]] DEVEX_API std::optional<float> terrainHeightAt(const scene::Scene& scene, AssetManager& assets, scene::Entity terrain,
                                                             math::Vec3 point);
// The same over every terrain of the scene: the first that covers the point.
[[nodiscard]] DEVEX_API std::optional<float> terrainHeightAt(const scene::Scene& scene, AssetManager& assets, math::Vec3 point);
// Which way its ground faces there.
[[nodiscard]] DEVEX_API std::optional<math::Vec3> terrainNormalAt(const scene::Scene& scene, AssetManager& assets, scene::Entity terrain,
                                                                  math::Vec3 point);
// Whether its ground has a hole there.
[[nodiscard]] DEVEX_API bool isTerrainHoleAt(const scene::Scene& scene, AssetManager& assets, scene::Entity terrain, math::Vec3 point);
// Where a ray of the world first meets the ground of the terrain within maxDistance, passing its holes.
[[nodiscard]] DEVEX_API std::optional<TerrainHit> raycastTerrain(const scene::Scene& scene, AssetManager& assets, scene::Entity terrain,
                                                                 math::Vec3 origin, math::Vec3 direction, float maxDistance);
// The same over every terrain of the scene: the nearest hit.
[[nodiscard]] DEVEX_API std::optional<TerrainHit> raycastTerrain(const scene::Scene& scene, AssetManager& assets, math::Vec3 origin,
                                                                 math::Vec3 direction, float maxDistance);

} // namespace devex::runtime
