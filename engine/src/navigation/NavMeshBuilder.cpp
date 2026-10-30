#include "TileCache.hpp"

#include <devex/asset/TerrainMaps.hpp>
#include <devex/navigation/NavMeshBuilder.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/NavigationComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Scene.hpp>

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <span>

namespace devex::navigation {
namespace {

using scene::Entity;

constexpr int polygonSides = 12;

struct TerrainGround
{
    const asset::TerrainMaps* maps = nullptr;
    math::Vec3 origin{0.0f};
    float size = 0.0f;
    math::Aabb bounds;
    std::uint64_t triangles = 0;
};

// Jolt splits a cell along (x,z)--(x+1,z+1). A missing diagonal sample removes both
// triangles; either other missing corner removes just the triangle that uses it.
[[nodiscard]] unsigned terrainTrianglesAt(const asset::TerrainMaps& maps, std::uint32_t x, std::uint32_t z)
{
    const std::size_t first = std::size_t{z} * maps.resolution + x;
    const std::size_t below = first + maps.resolution;
    if (maps.holes[first] > 127 || maps.holes[below + 1] > 127)
    {
        return 0;
    }
    return (maps.holes[below] <= 127 ? 1u : 0u) | (maps.holes[first + 1] <= 127 ? 2u : 0u);
}

[[nodiscard]] core::Result<std::vector<TerrainGround>> terrainGrounds(const scene::Scene& scene,
                                                                     const TerrainSource& source)
{
    std::vector<TerrainGround> grounds;
    if (!source)
    {
        return grounds;
    }
    for (auto [entity, terrain] : scene.view<scene::Terrain>())
    {
        if (!terrain.collision || !terrain.data.isValid())
        {
            continue;
        }
        const asset::TerrainMaps* const maps = source(terrain.data);
        if (maps == nullptr)
        {
            continue;
        }
        if (const auto valid = asset::validate(*maps); !valid)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "terrain '{}': {}",
                                   scene.name(entity), valid.error().message);
        }
        const auto* const world = scene.tryGet<scene::WorldTransform>(entity);
        TerrainGround ground{.maps = maps,
                             .origin = world != nullptr ? math::Vec3(world->matrix[3]) : math::Vec3{0.0f},
                             .size = terrain.size};
        if (!std::isfinite(terrain.size) || terrain.size <= 0.0f ||
            terrain.size / static_cast<float>(maps->resolution - 1) <= 0.0f ||
            !std::isfinite(ground.origin.x) || !std::isfinite(ground.origin.y) || !std::isfinite(ground.origin.z))
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "terrain '{}' has an invalid size or position", scene.name(entity));
        }
        const auto [low, high] = std::minmax_element(maps->heights.begin(), maps->heights.end());
        ground.bounds = {ground.origin + math::Vec3{0.0f, *low, 0.0f},
                         ground.origin + math::Vec3{terrain.size, *high, terrain.size}};
        const std::uint32_t cells = maps->resolution - 1;
        if (std::ranges::none_of(maps->holes, [](std::uint8_t hole) { return hole > 127; }))
        {
            ground.triangles = std::uint64_t{cells} * cells * 2;
        }
        else
        {
            for (std::uint32_t z = 0; z < cells; ++z)
            {
                for (std::uint32_t x = 0; x < cells; ++x)
                {
                    const unsigned mask = terrainTrianglesAt(*maps, x, z);
                    ground.triangles += (mask & 1u) + ((mask >> 1) & 1u);
                }
            }
        }
        if (ground.triangles > 0)
        {
            grounds.push_back(ground);
        }
    }
    return grounds;
}

// Only two rows of samples are expanded at a time, even when one tile covers a whole terrain.
[[nodiscard]] core::Result<bool> rasterizeTerrain(rcContext& context, const rcConfig& config,
                                                 const TerrainGround& ground, rcHeightfield& heightfield)
{
    if (ground.bounds.min.x > config.bmax[0] || ground.bounds.max.x < config.bmin[0] ||
        ground.bounds.min.z > config.bmax[2] || ground.bounds.max.z < config.bmin[2])
    {
        return false;
    }
    const asset::TerrainMaps& maps = *ground.maps;
    const std::uint32_t cells = maps.resolution - 1;
    const float spacing = ground.size / static_cast<float>(cells);
    const auto cell = [cells, spacing](float offset, bool end) {
        const float coordinate = end ? std::ceil(offset / spacing) : std::floor(offset / spacing);
        return static_cast<std::uint32_t>(std::clamp(coordinate, 0.0f, static_cast<float>(cells)));
    };
    const auto firstX = cell(config.bmin[0] - ground.origin.x, false);
    const auto lastX = cell(config.bmax[0] - ground.origin.x, true);
    const auto firstZ = cell(config.bmin[2] - ground.origin.z, false);
    const auto lastZ = cell(config.bmax[2] - ground.origin.z, true);
    if (firstX >= lastX || firstZ >= lastZ)
    {
        return false;
    }
    const int row = static_cast<int>(lastX - firstX + 1);
    std::vector<float> vertices(static_cast<std::size_t>(row) * 6);
    std::vector<int> triangles;
    triangles.reserve(static_cast<std::size_t>(row - 1) * 6);
    std::vector<unsigned char> areas;
    bool rasterized = false;
    for (std::uint32_t z = firstZ; z < lastZ; ++z)
    {
        for (std::uint32_t side = 0; side < 2; ++side)
        {
            for (std::uint32_t x = firstX; x <= lastX; ++x)
            {
                const std::size_t vertex = (std::size_t{side} * static_cast<std::size_t>(row) + x - firstX) * 3;
                vertices[vertex] = ground.origin.x + static_cast<float>(x) * spacing;
                vertices[vertex + 1] = ground.origin.y + maps.heights[std::size_t{z + side} * maps.resolution + x];
                vertices[vertex + 2] = ground.origin.z + static_cast<float>(z + side) * spacing;
            }
        }
        triangles.clear();
        for (std::uint32_t x = firstX; x < lastX; ++x)
        {
            const unsigned mask = terrainTrianglesAt(maps, x, z);
            const int top = static_cast<int>(x - firstX);
            if ((mask & 1u) != 0)
            {
                triangles.insert(triangles.end(), {top, top + row, top + row + 1});
            }
            if ((mask & 2u) != 0)
            {
                triangles.insert(triangles.end(), {top, top + row + 1, top + 1});
            }
        }
        if (triangles.empty())
        {
            continue;
        }
        const int count = static_cast<int>(triangles.size() / 3);
        areas.assign(static_cast<std::size_t>(count), 0);
        rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, vertices.data(), row * 2,
                               triangles.data(), count, areas.data());
        if (!rcRasterizeTriangles(&context, vertices.data(), row * 2, triangles.data(), areas.data(),
                                 count, heightfield, config.walkableClimb))
        {
            return core::makeError(core::ErrorCode::OutOfMemory, "the terrain cannot be cut into voxels");
        }
        rasterized = true;
    }
    return rasterized;
}

// Adds a triangle facing away from the inside of its shape, as Recast finds the ground from the
// winding of triangles.
void addTriangle(NavGeometry& geometry, math::Vec3 a, math::Vec3 b, math::Vec3 c, math::Vec3 inside)
{
    const math::Vec3 normal = math::cross(b - a, c - a);
    if (math::dot(normal, (a + b + c) / 3.0f - inside) < 0.0f)
    {
        std::swap(b, c);
    }
    const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
    geometry.vertices.insert(geometry.vertices.end(), {a, b, c});
    geometry.indices.insert(geometry.indices.end(), {first, first + 1, first + 2});
}

// A convex solid given by rings of points: each ring is a slice at one height, all with as many
// points, the first and last rings closed by fans.
void addRings(NavGeometry& geometry, const std::vector<std::vector<math::Vec3>>& rings, math::Vec3 inside)
{
    for (std::size_t ring = 0; ring + 1 < rings.size(); ++ring)
    {
        const std::vector<math::Vec3>& low = rings[ring];
        const std::vector<math::Vec3>& high = rings[ring + 1];
        for (std::size_t side = 0; side < low.size(); ++side)
        {
            const std::size_t next = (side + 1) % low.size();
            addTriangle(geometry, low[side], low[next], high[next], inside);
            addTriangle(geometry, low[side], high[next], high[side], inside);
        }
    }
    for (const std::vector<math::Vec3>* cap : {&rings.front(), &rings.back()})
    {
        for (std::size_t side = 1; side + 1 < cap->size(); ++side)
        {
            addTriangle(geometry, (*cap)[0], (*cap)[side], (*cap)[side + 1], inside);
        }
    }
}

// A ring of points around the Y axis of a matrix.
[[nodiscard]] std::vector<math::Vec3> ring(const math::Mat4& world, math::Vec3 center, float radius, float y)
{
    std::vector<math::Vec3> points;
    for (int side = 0; side < polygonSides; ++side)
    {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(side) / polygonSides;
        points.push_back(math::Vec3(world * math::Vec4(center + math::Vec3{std::cos(angle) * radius, y, std::sin(angle) * radius}, 1.0f)));
    }
    return points;
}

// The body a collider belongs to: the closest RigidBody on it or above it.
[[nodiscard]] const scene::RigidBody* bodyOf(const scene::Scene& scene, Entity entity)
{
    for (Entity current = entity; current.isValid(); current = scene.parent(current))
    {
        if (const scene::RigidBody* const body = scene.tryGet<scene::RigidBody>(current))
        {
            return body;
        }
    }
    return nullptr;
}

// Colliders that move are left out: the navigation mesh is baked once.
[[nodiscard]] bool staysPut(const scene::Scene& scene, Entity entity, bool trigger)
{
    const scene::RigidBody* const body = bodyOf(scene, entity);
    return !trigger && (body == nullptr || body->type == scene::BodyType::Static) && !scene.has<scene::CharacterController>(entity);
}

// Layers of one tile: the scenery overlapping it, rasterized into voxels, eroded by the size of
// the agent, and cut into layers that do not overlap in height.
[[nodiscard]] core::Result<void> rasterizeTile(rcContext& context, const rcConfig& base, const std::vector<float>& vertices,
                                               const std::vector<int>& triangles,
                                               const std::vector<std::array<float, 4>>& triangleBounds,
                                               std::span<const TerrainGround> terrains, int tileX, int tileZ,
                                               detail::ZstdCompressor& compressor, std::vector<std::vector<std::byte>>& layers)
{
    rcConfig config = base;
    const float tileWidth = static_cast<float>(config.tileSize) * config.cs;
    config.bmin[0] = base.bmin[0] + static_cast<float>(tileX) * tileWidth - static_cast<float>(config.borderSize) * config.cs;
    config.bmin[2] = base.bmin[2] + static_cast<float>(tileZ) * tileWidth - static_cast<float>(config.borderSize) * config.cs;
    config.bmax[0] = base.bmin[0] + static_cast<float>(tileX + 1) * tileWidth + static_cast<float>(config.borderSize) * config.cs;
    config.bmax[2] = base.bmin[2] + static_cast<float>(tileZ + 1) * tileWidth + static_cast<float>(config.borderSize) * config.cs;

    const auto heightfield = std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>(rcAllocHeightfield(), &rcFreeHeightField);
    if (!heightfield ||
        !rcCreateHeightfield(&context, *heightfield, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch))
    {
        return core::makeError(core::ErrorCode::OutOfMemory, "the voxels of a tile cannot be made");
    }
    // Only the triangles over the tile.
    std::vector<int> overlapping;
    for (std::size_t triangle = 0; triangle < triangleBounds.size(); ++triangle)
    {
        const std::array<float, 4>& bounds = triangleBounds[triangle];
        if (bounds[0] <= config.bmax[0] && bounds[2] >= config.bmin[0] && bounds[1] <= config.bmax[2] && bounds[3] >= config.bmin[2])
        {
            overlapping.insert(overlapping.end(), triangles.begin() + static_cast<std::ptrdiff_t>(triangle * 3),
                               triangles.begin() + static_cast<std::ptrdiff_t>(triangle * 3 + 3));
        }
    }
    bool rasterized = !overlapping.empty();
    if (!overlapping.empty())
    {
        const int count = static_cast<int>(overlapping.size() / 3);
        std::vector<unsigned char> areas(static_cast<std::size_t>(count), 0);
        const int vertexCount = static_cast<int>(vertices.size() / 3);
        rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, vertices.data(), vertexCount, overlapping.data(), count, areas.data());
        if (!rcRasterizeTriangles(&context, vertices.data(), vertexCount, overlapping.data(), areas.data(), count, *heightfield,
                                  config.walkableClimb))
        {
            return core::makeError(core::ErrorCode::OutOfMemory, "the scenery of a tile cannot be cut into voxels");
        }
    }
    for (const TerrainGround& terrain : terrains)
    {
        const auto added = rasterizeTerrain(context, config, terrain, *heightfield);
        if (!added)
        {
            return std::unexpected(added.error());
        }
        rasterized = rasterized || *added;
    }
    if (!rasterized)
    {
        return {};
    }
    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

    const auto compact =
        std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>(rcAllocCompactHeightfield(), &rcFreeCompactHeightfield);
    if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightfield, *compact) ||
        !rcErodeWalkableArea(&context, config.walkableRadius, *compact))
    {
        return core::makeError(core::ErrorCode::OutOfMemory, "the walkable voxels of a tile cannot be found");
    }
    const auto layerSet = std::unique_ptr<rcHeightfieldLayerSet, decltype(&rcFreeHeightfieldLayerSet)>(
        rcAllocHeightfieldLayerSet(), &rcFreeHeightfieldLayerSet);
    if (!layerSet || !rcBuildHeightfieldLayers(&context, *compact, config.borderSize, config.walkableHeight, *layerSet))
    {
        return core::makeError(core::ErrorCode::OutOfMemory, "the layers of a tile cannot be made");
    }
    for (int index = 0; index < layerSet->nlayers; ++index)
    {
        const rcHeightfieldLayer& layer = layerSet->layers[index];
        dtTileCacheLayerHeader header{};
        header.magic = DT_TILECACHE_MAGIC;
        header.version = DT_TILECACHE_VERSION;
        header.tx = tileX;
        header.ty = tileZ;
        header.tlayer = index;
        std::memcpy(header.bmin, layer.bmin, sizeof(header.bmin));
        std::memcpy(header.bmax, layer.bmax, sizeof(header.bmax));
        header.width = static_cast<unsigned char>(layer.width);
        header.height = static_cast<unsigned char>(layer.height);
        header.minx = static_cast<unsigned char>(layer.minx);
        header.maxx = static_cast<unsigned char>(layer.maxx);
        header.miny = static_cast<unsigned char>(layer.miny);
        header.maxy = static_cast<unsigned char>(layer.maxy);
        header.hmin = static_cast<unsigned short>(layer.hmin);
        header.hmax = static_cast<unsigned short>(layer.hmax);
        unsigned char* data = nullptr;
        int size = 0;
        if (dtStatusFailed(dtBuildTileCacheLayer(&compressor, &header, layer.heights, layer.areas, layer.cons, &data, &size)))
        {
            return core::makeError(core::ErrorCode::OutOfMemory, "a layer of a tile cannot be compressed");
        }
        const auto* const bytes = reinterpret_cast<const std::byte*>(data);
        layers.emplace_back(bytes, bytes + size);
        dtFree(data);
    }
    return {};
}

} // namespace

NavGeometry collectGeometry(const scene::Scene& scene, const MeshSource& meshes)
{
    NavGeometry geometry;
    const auto worldOf = [&](Entity entity) {
        const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
        return world != nullptr ? world->matrix : math::Mat4{1.0f};
    };
    for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::BoxCollider>())
    {
        if (!staysPut(scene, entity, collider.trigger))
        {
            continue;
        }
        const math::Mat4 world = worldOf(entity);
        const math::Vec3 half = math::abs(collider.size) * 0.5f;
        const auto corner = [&](float x, float y, float z) {
            return math::Vec3(world * math::Vec4(collider.center + half * math::Vec3{x, y, z}, 1.0f));
        };
        std::vector<std::vector<math::Vec3>> rings{
            {corner(-1, -1, -1), corner(1, -1, -1), corner(1, -1, 1), corner(-1, -1, 1)},
            {corner(-1, 1, -1), corner(1, 1, -1), corner(1, 1, 1), corner(-1, 1, 1)},
        };
        addRings(geometry, rings, math::Vec3(world * math::Vec4(collider.center, 1.0f)));
    }
    for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::CylinderCollider>())
    {
        if (staysPut(scene, entity, collider.trigger))
        {
            const math::Mat4 world = worldOf(entity);
            const float half = collider.height * 0.5f;
            addRings(geometry, {ring(world, collider.center, collider.radius, -half), ring(world, collider.center, collider.radius, half)},
                     math::Vec3(world * math::Vec4(collider.center, 1.0f)));
        }
    }
    for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::CapsuleCollider>())
    {
        if (staysPut(scene, entity, collider.trigger))
        {
            // Rounded ends as one step each.
            const math::Mat4 world = worldOf(entity);
            const float half = std::max(collider.height * 0.5f - collider.radius, 0.0f);
            const float inner = collider.radius * 0.7f;
            addRings(geometry,
                     {ring(world, collider.center, inner, -half - collider.radius * 0.7f), ring(world, collider.center, collider.radius, -half),
                      ring(world, collider.center, collider.radius, half), ring(world, collider.center, inner, half + collider.radius * 0.7f)},
                     math::Vec3(world * math::Vec4(collider.center, 1.0f)));
        }
    }
    for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::SphereCollider>())
    {
        if (staysPut(scene, entity, collider.trigger))
        {
            const math::Mat4 world = worldOf(entity);
            std::vector<std::vector<math::Vec3>> rings;
            for (int band = 1; band < 6; ++band)
            {
                const float angle = std::numbers::pi_v<float> * static_cast<float>(band) / 6.0f;
                rings.push_back(ring(world, collider.center, collider.radius * std::sin(angle), -collider.radius * std::cos(angle)));
            }
            addRings(geometry, rings, math::Vec3(world * math::Vec4(collider.center, 1.0f)));
        }
    }
    for ([[maybe_unused]] auto [entity, collider] : scene.view<scene::MeshCollider>())
    {
        if (!staysPut(scene, entity, collider.trigger) || !meshes)
        {
            continue;
        }
        asset::AssetId id = collider.mesh;
        if (!id.isValid())
        {
            const scene::MeshRenderer* const renderer = scene.tryGet<scene::MeshRenderer>(entity);
            id = renderer != nullptr ? renderer->mesh : asset::AssetId{};
        }
        const asset::MeshData* const mesh = id.isValid() ? meshes(id) : nullptr;
        if (mesh == nullptr)
        {
            continue;
        }
        const math::Mat4 world = worldOf(entity);
        const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
        for (const asset::Vertex& vertex : mesh->vertices)
        {
            geometry.vertices.push_back(math::Vec3(world * math::Vec4(vertex.position, 1.0f)));
        }
        for (const std::uint32_t index : mesh->indices)
        {
            geometry.indices.push_back(first + index);
        }
    }
    return geometry;
}

asset::NavMeshBuildSettings buildSettingsOf(const scene::NavMeshSurface& surface) noexcept
{
    return {
        .agentRadius = surface.agentRadius,
        .agentHeight = surface.agentHeight,
        .agentMaxClimb = surface.agentMaxClimb,
        .agentMaxSlope = surface.agentMaxSlope,
        .cellSize = surface.cellSize,
        .cellHeight = surface.cellHeight,
        .tileSize = surface.tileSize,
    };
}

namespace {

[[nodiscard]] core::Result<asset::NavMeshData> bake(const NavGeometry& geometry,
                                                  std::span<const TerrainGround> terrains,
                                                  const asset::NavMeshBuildSettings& settings)
{
    std::uint64_t triangleCount = geometry.triangleCount();
    for (const TerrainGround& terrain : terrains)
    {
        triangleCount += terrain.triangles;
    }
    if (triangleCount > std::numeric_limits<std::uint32_t>::max())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the scene has too many triangles to bake");
    }
    asset::NavMeshData navMesh{.settings = settings, .triangles = static_cast<std::uint32_t>(triangleCount)};
    if (core::Result<void> valid = asset::validate(navMesh); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (triangleCount == 0)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the scene has no static ground for agents to walk on");
    }
    std::vector<float> vertices;
    vertices.reserve(geometry.vertices.size() * 3);
    for (const math::Vec3 vertex : geometry.vertices)
    {
        vertices.insert(vertices.end(), {vertex.x, vertex.y, vertex.z});
    }
    std::vector<int> triangles(geometry.indices.begin(), geometry.indices.end());
    std::vector<std::array<float, 4>> triangleBounds;
    for (std::size_t triangle = 0; triangle < triangles.size() / 3; ++triangle)
    {
        std::array<float, 4> bounds{FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (int corner = 0; corner < 3; ++corner)
        {
            const math::Vec3 point = geometry.vertices[geometry.indices[triangle * 3 + static_cast<std::size_t>(corner)]];
            bounds = {std::min(bounds[0], point.x), std::min(bounds[1], point.z), std::max(bounds[2], point.x), std::max(bounds[3], point.z)};
        }
        triangleBounds.push_back(bounds);
    }

    rcConfig config{};
    config.cs = settings.cellSize;
    config.ch = settings.cellHeight;
    config.walkableSlopeAngle = math::degrees(settings.agentMaxSlope);
    config.walkableHeight = static_cast<int>(std::ceil(settings.agentHeight / config.ch));
    config.walkableClimb = static_cast<int>(std::floor(settings.agentMaxClimb / config.ch));
    config.walkableRadius = static_cast<int>(std::ceil(settings.agentRadius / config.cs));
    config.maxEdgeLen = static_cast<int>(12.0f / config.cs);
    config.maxSimplificationError = 1.3f;
    config.minRegionArea = 8 * 8;
    config.mergeRegionArea = 20 * 20;
    config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    config.tileSize = settings.tileSize;
    config.borderSize = config.walkableRadius + 3;
    config.width = config.tileSize + config.borderSize * 2;
    config.height = config.width;
    config.detailSampleDist = config.cs * 6.0f;
    config.detailSampleMaxError = config.ch;
    if (config.width > 255)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "tiles of {} voxels are too large for agents this wide", config.tileSize);
    }
    math::Aabb bounds;
    for (const math::Vec3 vertex : geometry.vertices)
    {
        bounds.add(vertex);
    }
    for (const TerrainGround& terrain : terrains)
    {
        bounds.add(terrain.bounds.min);
        bounds.add(terrain.bounds.max);
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        config.bmin[axis] = bounds.min[axis];
        config.bmax[axis] = bounds.max[axis];
        const double extent = static_cast<double>(bounds.max[axis]) - bounds.min[axis];
        const double cell = axis == 1 ? config.ch : config.cs;
        if (!std::isfinite(extent) || extent < 0.0 || extent / cell > std::numeric_limits<int>::max() / 2)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the scene is too large for these navigation voxels");
        }
    }
    int cellsX = 0;
    int cellsZ = 0;
    rcCalcGridSize(config.bmin, config.bmax, config.cs, &cellsX, &cellsZ);
    navMesh.tilesX = (cellsX + config.tileSize - 1) / config.tileSize;
    navMesh.tilesZ = (cellsZ + config.tileSize - 1) / config.tileSize;
    navMesh.origin = {config.bmin[0], config.bmin[1], config.bmin[2]};
    navMesh.boundsMax = {config.bmax[0], config.bmax[1], config.bmax[2]};
    if (core::Result<void> valid = asset::validate(navMesh); !valid)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the scene is too large for voxels of {} m", config.cs);
    }

    rcContext context(false);
    detail::ZstdCompressor compressor;
    for (int z = 0; z < navMesh.tilesZ; ++z)
    {
        for (int x = 0; x < navMesh.tilesX; ++x)
        {
            if (core::Result<void> tile = rasterizeTile(context, config, vertices, triangles, triangleBounds, terrains,
                                                       x, z, compressor, navMesh.layers);
                !tile)
            {
                return std::unexpected(tile.error());
            }
        }
    }
    if (navMesh.layers.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "agents of this size fit nowhere on the ground of the scene");
    }
    return navMesh;
}

// The detail triangles follow the ground inside polygons whose contours alone may look flat.
void appendPolygonLines(const dtMeshTile& tile, int polygon, NavMeshLines& lines)
{
    const dtPoly& poly = tile.polys[polygon];
    const dtPolyDetail& detail = tile.detailMeshes[polygon];
    const auto vertex = [&](unsigned char index) {
        return index < poly.vertCount ? &tile.verts[poly.verts[index] * 3]
                                      : &tile.detailVerts[(detail.vertBase + index - poly.vertCount) * 3];
    };
    for (int triangle = 0; triangle < detail.triCount; ++triangle)
    {
        const unsigned char* const indices = &tile.detailTris[(detail.triBase + triangle) * 4];
        for (int edge = 0; edge < 3; ++edge)
        {
            const int next = (edge + 1) % 3;
            const float* const from = vertex(indices[edge]);
            const float* const to = vertex(indices[next]);
            std::vector<math::Vec3>* list = &lines.inner;
            if ((dtGetDetailTriEdgeFlags(indices[3], edge) & DT_DETAIL_EDGE_BOUNDARY) != 0)
            {
                // Match the segment to its polygon edge in XZ; the heights may bend between corners.
                for (int side = 0; side < poly.vertCount; ++side)
                {
                    const float* const a = &tile.verts[poly.verts[side] * 3];
                    const float* const b = &tile.verts[poly.verts[(side + 1) % poly.vertCount] * 3];
                    float along = 0.0f;
                    constexpr float tolerance = 0.01f * 0.01f;
                    if (dtDistancePtSegSqr2D(from, a, b, along) > tolerance ||
                        dtDistancePtSegSqr2D(to, a, b, along) > tolerance)
                    {
                        continue;
                    }
                    const unsigned short neighbour = poly.neis[side];
                    if (neighbour == 0)
                    {
                        list = &lines.borders;
                    }
                    else if ((neighbour & DT_EXT_LINK) == 0 && static_cast<int>(neighbour) - 1 < polygon)
                    {
                        list = nullptr;
                    }
                    break;
                }
            }
            else if (indices[edge] > indices[next])
            {
                // A shared triangle edge once, in increasing vertex order.
                continue;
            }
            if (list != nullptr)
            {
                list->push_back({from[0], from[1], from[2]});
                list->push_back({to[0], to[1], to[2]});
            }
        }
    }
}

} // namespace

core::Result<asset::NavMeshData> bakeNavMesh(const NavGeometry& geometry, const asset::NavMeshBuildSettings& settings)
{
    return bake(geometry, {}, settings);
}

core::Result<asset::NavMeshData> bakeNavMesh(const scene::Scene& scene, const MeshSource& meshes,
                                          const TerrainSource& terrains, const asset::NavMeshBuildSettings& settings)
{
    const auto grounds = terrainGrounds(scene, terrains);
    if (!grounds)
    {
        return std::unexpected(grounds.error());
    }
    return bake(collectGeometry(scene, meshes), *grounds, settings);
}

core::Result<NavMeshLines> navMeshLines(const asset::NavMeshData& navMesh)
{
    core::Result<std::unique_ptr<detail::TileCache>> cache = detail::TileCache::create(navMesh, 1);
    if (!cache)
    {
        return std::unexpected(cache.error());
    }
    NavMeshLines lines;
    const dtNavMesh& mesh = (*cache)->mesh();
    for (int index = 0; index < mesh.getMaxTiles(); ++index)
    {
        const dtMeshTile* const tile = mesh.getTile(index);
        if (tile == nullptr || tile->header == nullptr)
        {
            continue;
        }
        for (int polygon = 0; polygon < tile->header->polyCount; ++polygon)
        {
            const dtPoly& poly = tile->polys[polygon];
            if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
            {
                continue;
            }
            ++lines.polygons;
            appendPolygonLines(*tile, polygon, lines);
        }
    }
    return lines;
}

} // namespace devex::navigation
