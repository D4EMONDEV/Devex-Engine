#pragma once

#include <devex/asset/AssetId.hpp>
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

// A range of the index buffer drawn with one material.
struct Submesh
{
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    // Material used unless the mesh renderer overrides it; invalid selects the default material.
    AssetId material;
};

// Indexed triangle list in the engine conventions: Y-up, right-handed, counter-clockwise front
// faces.
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    // Empty means a single submesh covering every index with the default material.
    std::vector<Submesh> submeshes;
};

// Checks that the mesh has triangles, that every index refers to an existing vertex and that
// submeshes cover whole triangles inside the index buffer.
[[nodiscard]] core::Result<void> validate(const MeshData& mesh);

// The submeshes of the mesh, with the implicit one when the list is empty.
[[nodiscard]] std::vector<Submesh> submeshesOf(const MeshData& mesh);

// Replaces the normals with the area-weighted average of the adjacent triangle normals.
void computeNormals(MeshData& mesh);

} // namespace devex::asset
