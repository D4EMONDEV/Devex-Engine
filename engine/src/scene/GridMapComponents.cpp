#include <devex/scene/GridMapComponents.hpp>

#include <devex/core/Log.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/Scene.hpp>

#include <chrono>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <utility>

namespace devex::scene {

namespace {

constexpr std::size_t cellStride = 5;
// How long what was read of a palette is believed before reading it again.
constexpr std::chrono::seconds paletteCheck{1};

[[nodiscard]] std::optional<std::size_t> indexOf(const GridMap& grid, math::IVec3 position) noexcept
{
    for (std::size_t index = 0; index + cellStride <= grid.cells.size(); index += cellStride)
    {
        if (grid.cells[index] == position.x && grid.cells[index + 1] == position.y && grid.cells[index + 2] == position.z)
        {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool hasCollider(const Scene& scene, Entity entity)
{
    return scene.has<BoxCollider>(entity) || scene.has<SphereCollider>(entity) || scene.has<CapsuleCollider>(entity) || scene.has<CylinderCollider>(entity) ||
           scene.has<MeshCollider>(entity);
}

// The meshes of an entity and of what is under it, seen from the piece.
void addParts(const Scene& scene, Entity entity, const math::Mat4& toPiece, GridPiece& piece)
{
    if (const MeshRenderer* const renderer = scene.tryGet<MeshRenderer>(entity); renderer != nullptr && renderer->mesh.isValid())
    {
        const MeshCollider* const collider = scene.tryGet<MeshCollider>(entity);
        piece.parts.push_back({
            .mesh = renderer->mesh,
            .material = renderer->material,
            .transform = toPiece,
            .collides = hasCollider(scene, entity),
            .collisionMesh = collider != nullptr ? collider->mesh : asset::AssetId{},
        });
    }
    for (Entity child = scene.firstChild(entity); scene.isAlive(child); child = scene.nextSibling(child))
    {
        const Transform* const local = scene.tryGet<Transform>(child);
        addParts(scene, child, local != nullptr ? toPiece * local->matrix() : toPiece, piece);
    }
}

[[nodiscard]] GridPiece pieceOf(const Scene& scene, Entity entity)
{
    GridPiece piece{.name = scene.name(entity), .parts = {}};
    // Where it stands in the palette does not count, which leaves room to lay the pieces out there.
    Transform placed;
    if (const Transform* const local = scene.tryGet<Transform>(entity))
    {
        placed = *local;
        placed.position = math::Vec3{0.0f};
    }
    addParts(scene, entity, placed.matrix(), piece);
    return piece;
}

[[nodiscard]] core::Result<GridPalette> readPalette(asset::AssetId asset)
{
    Scene scene;
    core::Result<Entity> root = instantiatePrefab(scene, asset);
    if (!root)
    {
        return std::unexpected(root.error());
    }
    GridPalette palette;
    // A scene with one top entity has its pieces under it, unless it is a piece itself: it has nothing under it, or a
    // mesh of its own, which what only holds the pieces has not.
    const Entity first = scene.firstChild(*root);
    if (!scene.isAlive(first) || scene.has<MeshRenderer>(*root))
    {
        palette.pieces.push_back(pieceOf(scene, *root));
        return palette;
    }
    for (Entity child = first; scene.isAlive(child); child = scene.nextSibling(child))
    {
        palette.pieces.push_back(pieceOf(scene, child));
    }
    return palette;
}

[[nodiscard]] bool samePalette(const GridPalette& left, const GridPalette& right)
{
    if (left.pieces.size() != right.pieces.size())
    {
        return false;
    }
    for (std::size_t piece = 0; piece < left.pieces.size(); ++piece)
    {
        const GridPiece& a = left.pieces[piece];
        const GridPiece& b = right.pieces[piece];
        if (a.name != b.name || a.parts.size() != b.parts.size())
        {
            return false;
        }
        for (std::size_t part = 0; part < a.parts.size(); ++part)
        {
            const GridPiecePart& x = a.parts[part];
            const GridPiecePart& y = b.parts[part];
            if (x.mesh != y.mesh || x.material != y.material || x.transform != y.transform || x.collides != y.collides || x.collisionMesh != y.collisionMesh)
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace

DEVEX_REFLECT(GridMap)
{
    type.field("palette", &GridMap::palette, {.assetType = "scene", .group = "Grid"})
        .field("cell_size", &GridMap::cellSize)
        .field("collision", &GridMap::collision)
        .field("cells", &GridMap::cells, {.hidden = true});
}

std::size_t gridCellCount(const GridMap& grid) noexcept
{
    return grid.cells.size() / cellStride;
}

GridCell gridCell(const GridMap& grid, std::size_t index) noexcept
{
    const std::int32_t* const cell = &grid.cells[index * cellStride];
    return {.position = {cell[0], cell[1], cell[2]}, .piece = cell[3], .rotation = cell[4]};
}

std::optional<GridCell> findGridCell(const GridMap& grid, math::IVec3 position) noexcept
{
    const std::optional<std::size_t> index = indexOf(grid, position);
    return index ? std::optional(gridCell(grid, *index / cellStride)) : std::nullopt;
}

void setGridCell(GridMap& grid, math::IVec3 position, std::int32_t piece, std::int32_t rotation)
{
    const std::int32_t turned = ((rotation % 4) + 4) % 4;
    if (const std::optional<std::size_t> index = indexOf(grid, position))
    {
        grid.cells[*index + 3] = piece;
        grid.cells[*index + 4] = turned;
        return;
    }
    grid.cells.insert(grid.cells.end(), {position.x, position.y, position.z, piece, turned});
}

bool removeGridCell(GridMap& grid, math::IVec3 position)
{
    const std::optional<std::size_t> index = indexOf(grid, position);
    if (!index)
    {
        return false;
    }
    const auto first = grid.cells.begin() + static_cast<std::ptrdiff_t>(*index);
    grid.cells.erase(first, first + static_cast<std::ptrdiff_t>(cellStride));
    return true;
}

math::IVec3 gridCellAt(const GridMap& grid, math::Vec3 position) noexcept
{
    const auto along = [](float value, float size) { return static_cast<std::int32_t>(std::floor(size > 0.0f ? value / size : 0.0f)); };
    return {along(position.x, grid.cellSize.x), along(position.y, grid.cellSize.y), along(position.z, grid.cellSize.z)};
}

math::Vec3 gridCellPosition(const GridMap& grid, math::IVec3 cell) noexcept
{
    return {(static_cast<float>(cell.x) + 0.5f) * grid.cellSize.x, static_cast<float>(cell.y) * grid.cellSize.y, (static_cast<float>(cell.z) + 0.5f) * grid.cellSize.z};
}

math::Mat4 gridCellMatrix(const GridMap& grid, const GridCell& cell) noexcept
{
    Transform transform;
    transform.position = gridCellPosition(grid, cell.position);
    transform.rotation = math::angleAxis(static_cast<float>(cell.rotation) * std::numbers::pi_v<float> * 0.5f, math::Vec3{0.0f, 1.0f, 0.0f});
    return transform.matrix();
}

std::shared_ptr<const GridPalette> gridPalette(asset::AssetId scene)
{
    struct Read
    {
        std::shared_ptr<const GridPalette> palette;
        std::chrono::steady_clock::time_point checked;
    };
    static std::unordered_map<asset::AssetId, Read> read;
    if (!scene.isValid())
    {
        return nullptr;
    }
    const auto now = std::chrono::steady_clock::now();
    Read& entry = read[scene];
    const bool first = entry.checked == std::chrono::steady_clock::time_point{};
    if (!first && now - entry.checked < paletteCheck)
    {
        return entry.palette;
    }
    entry.checked = now;
    core::Result<GridPalette> palette = readPalette(scene);
    if (!palette)
    {
        if (first)
        {
            DEVEX_LOG_WARNING("Cannot read the palette of a grid: {}", palette.error());
        }
        entry.palette = nullptr;
        return nullptr;
    }
    // The same object while it does not change, which tells those who keep what they made of it.
    if (entry.palette == nullptr || !samePalette(*entry.palette, *palette))
    {
        entry.palette = std::make_shared<const GridPalette>(*std::move(palette));
    }
    return entry.palette;
}

} // namespace devex::scene
