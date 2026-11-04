#include <devex/runtime/GridWorld.hpp>

#include <devex/core/Log.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/CsgComponents.hpp>

#include <span>
#include <utility>
#include <vector>

namespace devex::runtime {

namespace {

[[nodiscard]] std::uint64_t mixed(std::uint64_t hash, std::span<const std::byte> bytes) noexcept
{
    for (const std::byte byte : bytes)
    {
        hash = (hash ^ std::to_integer<std::uint64_t>(byte)) * 1099511628211ull;
    }
    return hash;
}

// One mesh of everything that collides, in the space of the grid, for the physics and the navigation.
[[nodiscard]] asset::MeshData collisionOf(const scene::GridMap& grid, const scene::GridPalette& palette, AssetManager& assets)
{
    asset::MeshData merged;
    for (std::size_t index = 0; index < scene::gridCellCount(grid); ++index)
    {
        const scene::GridCell cell = scene::gridCell(grid, index);
        if (cell.piece < 0 || static_cast<std::size_t>(cell.piece) >= palette.pieces.size())
        {
            continue;
        }
        const math::Mat4 place = scene::gridCellMatrix(grid, cell);
        for (const scene::GridPiecePart& part : palette.pieces[static_cast<std::size_t>(cell.piece)].parts)
        {
            const asset::MeshData* const mesh = part.collides ? assets.meshData(part.collisionMesh.isValid() ? part.collisionMesh : part.mesh) : nullptr;
            if (mesh == nullptr)
            {
                continue;
            }
            const math::Mat4 world = place * part.transform;
            const auto first = static_cast<std::uint32_t>(merged.vertices.size());
            for (const asset::Vertex& vertex : mesh->vertices)
            {
                asset::Vertex& added = merged.vertices.emplace_back();
                added.position = math::Vec3(world * math::Vec4(vertex.position, 1.0f));
            }
            for (const std::uint32_t triangleVertex : mesh->indices)
            {
                merged.indices.push_back(first + triangleVertex);
            }
        }
    }
    return merged;
}

} // namespace

void GridWorld::update(scene::Scene& scene, AssetManager& assets)
{
    for (auto& [uuid, built] : m_built)
    {
        built.seen = false;
    }
    for ([[maybe_unused]] auto [entity, grid] : scene.view<scene::GridMap>())
    {
        const core::Uuid uuid = scene.uuid(entity);
        Built& built = m_built[uuid];
        built.seen = true;
        built.palette = scene::gridPalette(grid.palette);

        std::uint64_t signature = 14695981039346656037ull;
        signature = mixed(signature, std::as_bytes(std::span(grid.cells)));
        signature = mixed(signature, std::as_bytes(std::span(&grid.cellSize, 1)));
        const auto* const palette = built.palette.get();
        signature = mixed(signature, std::as_bytes(std::span(&palette, 1)));
        signature = mixed(signature, std::as_bytes(std::span(&grid.collision, 1)));
        if (built.signature != signature)
        {
            built.signature = signature;
            if (built.collisionMesh.isValid())
            {
                assets.removeGeneratedMesh(built.collisionMesh);
                built.collisionMesh = {};
            }
            if (grid.collision && palette != nullptr)
            {
                asset::MeshData merged = collisionOf(grid, *palette, assets);
                if (!merged.indices.empty())
                {
                    // Another identifier for each result, for the physics, which keeps its shapes by mesh.
                    built.collisionMesh = asset::AssetId{core::Uuid::fromParts(signature, std::hash<core::Uuid>{}(uuid))};
                    assets.addGeneratedMesh(built.collisionMesh, std::move(merged), false);
                }
            }
        }
    }

    // What collides, as any generated mesh; added apart, as it adds to the scene the view goes through.
    for (const auto& [uuid, built] : m_built)
    {
        const scene::Entity entity = scene.findEntity(uuid);
        if (built.seen && scene.isAlive(entity))
        {
            scene::GeneratedMesh* result = scene.tryGet<scene::GeneratedMesh>(entity);
            if (result == nullptr)
            {
                result = &scene.add<scene::GeneratedMesh>(entity);
            }
            result->mesh = built.collisionMesh;
            result->collision = built.collisionMesh.isValid();
        }
    }
    std::erase_if(m_built, [&](const auto& entry) {
        if (!entry.second.seen)
        {
            if (entry.second.collisionMesh.isValid())
            {
                assets.removeGeneratedMesh(entry.second.collisionMesh);
            }
            const scene::Entity gone = scene.findEntity(entry.first);
            if (scene.isAlive(gone) && scene.has<scene::GeneratedMesh>(gone))
            {
                scene.remove<scene::GeneratedMesh>(gone);
            }
        }
        return !entry.second.seen;
    });
}

void GridWorld::extract(const scene::Scene& scene, AssetManager& assets, render::RenderWorld& world) const
{
    // What draws each part of each piece, read once for all the cells that show it.
    struct Part
    {
        render::MeshHandle mesh;
        std::uint32_t submesh = 0;
        render::MaterialHandle material;
        math::Mat4 transform{1.0f};
    };
    for ([[maybe_unused]] const auto [entity, transform, grid] : scene.view<scene::WorldTransform, scene::GridMap>())
    {
        const auto found = m_built.find(scene.uuid(entity));
        const scene::GridPalette* const palette = found != m_built.end() ? found->second.palette.get() : nullptr;
        if (palette == nullptr)
        {
            continue;
        }
        std::vector<std::vector<Part>> pieces(palette->pieces.size());
        for (std::size_t piece = 0; piece < palette->pieces.size(); ++piece)
        {
            for (const scene::GridPiecePart& part : palette->pieces[piece].parts)
            {
                const LoadedMesh* const mesh = assets.mesh(part.mesh);
                if (mesh == nullptr)
                {
                    continue;
                }
                const render::MaterialHandle override = part.material.isValid() ? assets.material(part.material) : render::MaterialHandle{};
                for (std::uint32_t submesh = 0; submesh < mesh->submeshMaterials.size(); ++submesh)
                {
                    const asset::AssetId material = mesh->submeshMaterials[submesh];
                    const bool overridden = part.material.isValid() || !material.isValid();
                    pieces[piece].push_back({.mesh = mesh->handle, .submesh = submesh, .material = overridden ? override : assets.material(material), .transform = part.transform});
                }
            }
        }
        for (std::size_t index = 0; index < scene::gridCellCount(grid); ++index)
        {
            const scene::GridCell cell = scene::gridCell(grid, index);
            if (cell.piece < 0 || static_cast<std::size_t>(cell.piece) >= pieces.size())
            {
                continue;
            }
            const math::Mat4 place = transform.matrix * scene::gridCellMatrix(grid, cell);
            for (const Part& part : pieces[static_cast<std::size_t>(cell.piece)])
            {
                world.meshes.push_back({
                    .mesh = part.mesh,
                    .submesh = part.submesh,
                    .material = part.material,
                    .transform = place * part.transform,
                    .objectId = entity.index + 1,
                });
            }
        }
    }
}

void GridWorld::clear(AssetManager& assets)
{
    for (const auto& [uuid, built] : m_built)
    {
        if (built.collisionMesh.isValid())
        {
            assets.removeGeneratedMesh(built.collisionMesh);
        }
    }
    m_built.clear();
}

} // namespace devex::runtime
