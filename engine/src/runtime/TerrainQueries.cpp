#include <devex/runtime/TerrainQueries.hpp>

#include <devex/scene/Components.hpp>

namespace devex::runtime {

namespace {

// The maps of a terrain, its size and its corner in the world.
struct Ground
{
    const asset::TerrainMaps* maps = nullptr;
    float size = 0.0f;
    math::Vec3 origin{0.0f};
};

[[nodiscard]] std::optional<Ground> groundOf(const scene::Scene& scene, AssetManager& assets, scene::Entity entity)
{
    const scene::Terrain* const terrain = scene.isAlive(entity) ? scene.tryGet<scene::Terrain>(entity) : nullptr;
    AssetManager::LoadedTerrain* const loaded = terrain != nullptr && terrain->data.isValid() ? assets.terrain(terrain->data) : nullptr;
    if (loaded == nullptr || terrain->size <= 0.0f)
    {
        return std::nullopt;
    }
    // Laid from the position of its entity, as it is drawn, whatever its rotation and scale.
    const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
    return Ground{.maps = &loaded->maps, .size = terrain->size, .origin = world != nullptr ? math::Vec3(world->matrix[3]) : math::Vec3{0.0f}};
}

[[nodiscard]] bool covers(const Ground& ground, math::Vec3 local) noexcept
{
    return local.x >= 0.0f && local.z >= 0.0f && local.x <= ground.size && local.z <= ground.size;
}

} // namespace

std::optional<float> terrainHeightAt(const scene::Scene& scene, AssetManager& assets, scene::Entity terrain, math::Vec3 point)
{
    const std::optional<Ground> ground = groundOf(scene, assets, terrain);
    if (!ground)
    {
        return std::nullopt;
    }
    const math::Vec3 local = point - ground->origin;
    if (!covers(*ground, local) || asset::isTerrainHole(*ground->maps, ground->size, local.x, local.z))
    {
        return std::nullopt;
    }
    return ground->origin.y + asset::terrainHeight(*ground->maps, ground->size, local.x, local.z);
}

std::optional<float> terrainHeightAt(const scene::Scene& scene, AssetManager& assets, math::Vec3 point)
{
    for ([[maybe_unused]] auto [entity, terrain] : scene.view<scene::Terrain>())
    {
        if (const std::optional<float> height = terrainHeightAt(scene, assets, entity, point))
        {
            return height;
        }
    }
    return std::nullopt;
}

std::optional<math::Vec3> terrainNormalAt(const scene::Scene& scene, AssetManager& assets, scene::Entity terrain, math::Vec3 point)
{
    const std::optional<Ground> ground = groundOf(scene, assets, terrain);
    if (!ground)
    {
        return std::nullopt;
    }
    const math::Vec3 local = point - ground->origin;
    if (!covers(*ground, local) || asset::isTerrainHole(*ground->maps, ground->size, local.x, local.z))
    {
        return std::nullopt;
    }
    return asset::terrainNormal(*ground->maps, ground->size, local.x, local.z);
}

bool isTerrainHoleAt(const scene::Scene& scene, AssetManager& assets, scene::Entity terrain, math::Vec3 point)
{
    const std::optional<Ground> ground = groundOf(scene, assets, terrain);
    if (!ground)
    {
        return false;
    }
    const math::Vec3 local = point - ground->origin;
    return covers(*ground, local) && asset::isTerrainHole(*ground->maps, ground->size, local.x, local.z);
}

std::optional<TerrainHit> raycastTerrain(const scene::Scene& scene, AssetManager& assets, scene::Entity terrain, math::Vec3 origin,
                                         math::Vec3 direction, float maxDistance)
{
    const std::optional<Ground> ground = groundOf(scene, assets, terrain);
    const float length = math::length(direction);
    if (!ground || length <= 0.0f)
    {
        return std::nullopt;
    }
    direction /= length;
    const std::optional<float> distance = asset::raycastTerrain(*ground->maps, ground->size, origin - ground->origin, direction, maxDistance);
    if (!distance)
    {
        return std::nullopt;
    }
    const math::Vec3 point = origin + direction * *distance;
    const math::Vec3 local = point - ground->origin;
    return TerrainHit{.point = point, .normal = asset::terrainNormal(*ground->maps, ground->size, local.x, local.z), .distance = *distance};
}

std::optional<TerrainHit> raycastTerrain(const scene::Scene& scene, AssetManager& assets, math::Vec3 origin, math::Vec3 direction,
                                         float maxDistance)
{
    std::optional<TerrainHit> nearest;
    for ([[maybe_unused]] auto [entity, terrain] : scene.view<scene::Terrain>())
    {
        const std::optional<TerrainHit> hit = raycastTerrain(scene, assets, entity, origin, direction, maxDistance);
        if (hit && (!nearest || hit->distance < nearest->distance))
        {
            nearest = hit;
        }
    }
    return nearest;
}

} // namespace devex::runtime
