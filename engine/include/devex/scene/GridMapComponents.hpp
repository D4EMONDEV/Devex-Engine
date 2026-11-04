#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Pieces laid on a 3D grid, as Godot's GridMap: rooms, corridors and terrain made of modules, painted in the
// view. A piece is not an entity, which keeps thousands of them light.
namespace devex::scene {

// Where a piece is, which one, and how many quarter turns around Y it has made.
struct DEVEX_API GridCell
{
    math::IVec3 position{0};
    std::int32_t piece = 0;
    std::int32_t rotation = 0;
};

struct DEVEX_API GridMap
{
    // A scene whose top entities are the pieces: their meshes and their colliders. The position of each is
    // where it stands in that scene, and does not count here.
    asset::AssetId palette;
    // In meters. A piece stands at the middle of its cell, at the bottom.
    math::Vec3 cellSize{2.0f};
    // Whether pieces whose palette entities have colliders collide, and can be walked on by navigation.
    bool collision = true;
    // Five numbers for each cell: its position, its piece and its rotation.
    std::vector<std::int32_t> cells;
};

DEVEX_DECLARE_ENGINE_REFLECTION(GridMap);

[[nodiscard]] DEVEX_API std::size_t gridCellCount(const GridMap& grid) noexcept;
[[nodiscard]] DEVEX_API GridCell gridCell(const GridMap& grid, std::size_t index) noexcept;
[[nodiscard]] DEVEX_API std::optional<GridCell> findGridCell(const GridMap& grid, math::IVec3 position) noexcept;
// Puts a piece in a cell, replacing what was there. A rotation goes around, as four quarter turns do.
DEVEX_API void setGridCell(GridMap& grid, math::IVec3 position, std::int32_t piece, std::int32_t rotation = 0);
// False when the cell was empty.
DEVEX_API bool removeGridCell(GridMap& grid, math::IVec3 position);

// In the space of the entity: the cell a position is in, and where a piece of a cell stands.
[[nodiscard]] DEVEX_API math::IVec3 gridCellAt(const GridMap& grid, math::Vec3 position) noexcept;
[[nodiscard]] DEVEX_API math::Vec3 gridCellPosition(const GridMap& grid, math::IVec3 cell) noexcept;
[[nodiscard]] DEVEX_API math::Mat4 gridCellMatrix(const GridMap& grid, const GridCell& cell) noexcept;

// What a palette holds.
struct DEVEX_API GridPiecePart
{
    asset::AssetId mesh;
    // Takes the place of those of the mesh when valid.
    asset::AssetId material;
    // From the part to the piece.
    math::Mat4 transform{1.0f};
    bool collides = false;
    // What collides, when not the mesh shown.
    asset::AssetId collisionMesh;
};

struct DEVEX_API GridPiece
{
    std::string name;
    std::vector<GridPiecePart> parts;
};

struct DEVEX_API GridPalette
{
    std::vector<GridPiece> pieces;
};

// The pieces of a scene, kept while it does not change. Null when it cannot be read, or for no scene.
[[nodiscard]] DEVEX_API std::shared_ptr<const GridPalette> gridPalette(asset::AssetId scene);

} // namespace devex::scene
