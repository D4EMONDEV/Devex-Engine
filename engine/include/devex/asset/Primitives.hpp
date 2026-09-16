#pragma once

#include <devex/asset/MeshData.hpp>

#include <cstdint>

// Procedural meshes centered on the origin, with flat faces where edges are sharp.
namespace devex::asset {

[[nodiscard]] MeshData makeCube(float size = 1.0f);

// A square in the XZ plane, facing +Y.
[[nodiscard]] MeshData makePlane(float size = 1.0f);

[[nodiscard]] MeshData makeUvSphere(float radius = 0.5f, std::uint32_t segments = 32,
                                    std::uint32_t rings = 16);

} // namespace devex::asset
