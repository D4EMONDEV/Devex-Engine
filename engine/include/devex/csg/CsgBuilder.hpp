#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/MeshData.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <vector>

// Solids added to and cut out of one another, whose result is one mesh: what blocks out a level.
namespace devex::csg {

enum class Shape : std::uint8_t
{
    // Nothing of its own: only what its children make.
    Group,
    // Centered on its transform.
    Box,
    Sphere,
    // Along Y.
    Cylinder,
    // A rectangular section along a line: its top on the line, as a road.
    Extrusion,
};

enum class Operation : std::uint8_t
{
    Union,
    Subtract,
    Intersect,
};

// A solid and what is done to it by its children, in order. Each child first makes its own result with its
// children, then combines it with what came before by its operation. The operation of the first one of
// all does nothing.
struct DEVEX_API Node
{
    Shape shape = Shape::Group;
    Operation operation = Operation::Union;
    // From the space of the shape to that of the result.
    math::Mat4 transform{1.0f};
    math::Vec3 size{1.0f};
    float radius = 0.5f;
    float height = 1.0f;
    std::uint32_t sides = 24;
    asset::AssetId material;
    // For an extrusion, in the space of the result; its transform does not count. Closed, it joins itself.
    std::vector<math::Vec3> line;
    bool closed = false;
    float width = 2.0f;
    float depth = 0.2f;
    std::vector<Node> children;
};

// The result, in the space of the root, with hard edges where its faces meet at an angle, one submesh for
// each material, and textures mapped in meters from the side each face looks at. Empty when nothing is left.
[[nodiscard]] DEVEX_API core::Result<asset::MeshData> buildMesh(const Node& root);

} // namespace devex::csg
