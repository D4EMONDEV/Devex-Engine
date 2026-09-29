#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/asset/MeshData.hpp>

#include <cstdint>
#include <optional>

// Procedural meshes centered on the origin, with flat faces where edges are sharp.
namespace devex::asset {

[[nodiscard]] DEVEX_API MeshData makeCube(float size = 1.0f);

// A square in the XZ plane, facing +Y.
[[nodiscard]] DEVEX_API MeshData makePlane(float size = 1.0f);

[[nodiscard]] DEVEX_API MeshData makeUvSphere(float radius = 0.5f, std::uint32_t segments = 32,
                                              std::uint32_t rings = 16);

// The mesh of a built-in mesh identifier (builtin::cubeMesh...), or nothing for other identifiers.
[[nodiscard]] DEVEX_API std::optional<MeshData> makeBuiltinMesh(AssetId id);

} // namespace devex::asset
