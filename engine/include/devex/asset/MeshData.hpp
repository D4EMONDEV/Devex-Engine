#pragma once

#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <vector>

namespace devex::asset {

struct Vertex
{
    math::Vec3 position{0.0f};
    math::Vec3 normal{0.0f, 1.0f, 0.0f};
    // Texture coordinates with the origin at the top-left corner, as in glTF.
    math::Vec2 uv{0.0f};
};

// Indexed triangle list in the engine conventions: Y-up, right-handed, counter-clockwise front
// faces.
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Checks that the mesh has triangles and that every index refers to an existing vertex.
[[nodiscard]] core::Result<void> validate(const MeshData& mesh);

// Replaces the normals with the area-weighted average of the adjacent triangle normals.
void computeNormals(MeshData& mesh);

} // namespace devex::asset
