#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace devex::asset {

struct Vertex
{
    math::Vec3 position{0.0f};
    math::Vec3 normal{0.0f, 1.0f, 0.0f};
    // Texture coordinates with the origin at the top-left corner, as in glTF.
    math::Vec2 uv{0.0f};
    // Direction of increasing u in XYZ; W is the sign of the bitangent, cross(normal, tangent) * w.
    math::Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

// How a vertex follows the joints of a skeleton: up to four of them, with weights that sum to one.
// Joints are indices into the inverse bind matrices of the mesh, and into the bones of the
// SkinnedMeshRenderer that draws it.
struct VertexSkin
{
    std::array<std::uint16_t, 4> joints{};
    math::Vec4 weights{0.0f};
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
    // Empty for a static mesh; otherwise one entry per vertex.
    std::vector<VertexSkin> skin;
    // One per joint of a skinned mesh: from the space of the mesh to the space of the joint at
    // bind time. A bone matrix is the world transform of the joint times this matrix.
    std::vector<math::Mat4> inverseBind;
};

// Whether the mesh carries skinning attributes, and must be drawn with its bones.
[[nodiscard]] bool isSkinned(const MeshData& mesh) noexcept;

// Checks that the mesh has triangles, that every index refers to an existing vertex and that
// submeshes cover whole triangles inside the index buffer.
[[nodiscard]] core::Result<void> validate(const MeshData& mesh);

// The submeshes of the mesh, with the implicit one when the list is empty.
[[nodiscard]] std::vector<Submesh> submeshesOf(const MeshData& mesh);

// Replaces the normals with the area-weighted average of the adjacent triangle normals.
void computeNormals(MeshData& mesh);

// Computes MikkTSpace tangents, the standard that normal map bakers follow. Vertices shared by
// triangles whose tangents differ are split, so the vertex count may grow. Indices keep their
// order, and submeshes stay valid.
void computeTangents(MeshData& mesh);

} // namespace devex::asset
