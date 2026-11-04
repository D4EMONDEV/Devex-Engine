#include <devex/csg/CsgBuilder.hpp>

#include <manifold/manifold.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace devex::csg {

namespace {

// Where faces that meet at more than this have a hard edge, in degrees.
constexpr double sharpAngle = 45.0;

using Materials = std::vector<std::pair<int, asset::AssetId>>;

[[nodiscard]] manifold::mat3x4 toManifold(const math::Mat4& matrix)
{
    return manifold::mat3x4(manifold::vec3(matrix[0][0], matrix[0][1], matrix[0][2]), manifold::vec3(matrix[1][0], matrix[1][1], matrix[1][2]),
                            manifold::vec3(matrix[2][0], matrix[2][1], matrix[2][2]), manifold::vec3(matrix[3][0], matrix[3][1], matrix[3][2]));
}

// The horizontal direction to the right of a way of going, as far as it has one.
[[nodiscard]] math::Vec3 sideOf(math::Vec3 direction) noexcept
{
    const math::Vec3 side = math::cross(direction, math::Vec3{0.0f, 1.0f, 0.0f});
    return math::length(side) > 1.0e-4f ? math::normalize(side) : math::Vec3{1.0f, 0.0f, 0.0f};
}

// The section of an extrusion along its line: four corners around each point of it, joined to the next.
[[nodiscard]] manifold::Manifold extruded(const Node& node)
{
    std::vector<math::Vec3> line;
    line.reserve(node.line.size());
    for (const math::Vec3 point : node.line)
    {
        if (line.empty() || math::length(point - line.back()) > 1.0e-4f)
        {
            line.push_back(point);
        }
    }
    if (node.closed && line.size() > 1 && math::length(line.front() - line.back()) <= 1.0e-4f)
    {
        line.pop_back();
    }
    const bool closed = node.closed && line.size() >= 3;
    if (line.size() < 2 || node.width <= 0.0f || node.depth <= 0.0f)
    {
        return {};
    }

    manifold::MeshGL mesh;
    mesh.numProp = 3;
    const std::size_t count = line.size();
    mesh.vertProperties.reserve(count * 4 * 3);
    const float half = node.width * 0.5f;
    for (std::size_t index = 0; index < count; ++index)
    {
        const math::Vec3 here = line[index];
        const math::Vec3 before = closed ? line[(index + count - 1) % count] : line[index == 0 ? 0 : index - 1];
        const math::Vec3 after = closed ? line[(index + 1) % count] : line[std::min(index + 1, count - 1)];
        const math::Vec3 arriving = math::normalize(index == 0 && !closed ? after - here : here - before);
        const math::Vec3 leaving = math::normalize(index == count - 1 && !closed ? here - before : after - here);
        const math::Vec3 along = math::normalize(arriving + leaving);
        const math::Vec3 flat = sideOf(arriving);
        const math::Vec3 sum = flat + sideOf(leaving);
        // Straight up or down between two sides that cancel out: any side will do.
        const math::Vec3 right = math::length(sum) > 1.0e-4f ? math::normalize(sum) : flat;
        const math::Vec3 up = math::cross(right, along);
        // Wider across a bend, so that the band keeps its width on both sides of it, up to a point.
        const math::Vec3 reach = right * (half / std::max(math::dot(right, flat), 0.34f));
        for (const math::Vec3 corner : {here - reach, here + reach, here + reach - up * node.depth, here - reach - up * node.depth})
        {
            mesh.vertProperties.insert(mesh.vertProperties.end(), {corner.x, corner.y, corner.z});
        }
    }
    const auto vertex = [](std::size_t ring, std::size_t corner) { return static_cast<std::uint32_t>(ring * 4 + corner % 4); };
    const std::size_t lengths = closed ? count : count - 1;
    for (std::size_t ring = 0; ring < lengths; ++ring)
    {
        const std::size_t next = (ring + 1) % count;
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
            mesh.triVerts.insert(mesh.triVerts.end(), {vertex(ring, corner), vertex(ring, corner + 1), vertex(next, corner + 1)});
            mesh.triVerts.insert(mesh.triVerts.end(), {vertex(ring, corner), vertex(next, corner + 1), vertex(next, corner)});
        }
    }
    if (!closed)
    {
        mesh.triVerts.insert(mesh.triVerts.end(), {vertex(0, 0), vertex(0, 3), vertex(0, 2), vertex(0, 0), vertex(0, 2), vertex(0, 1)});
        const std::size_t last = count - 1;
        mesh.triVerts.insert(mesh.triVerts.end(), {vertex(last, 0), vertex(last, 1), vertex(last, 2), vertex(last, 0), vertex(last, 2), vertex(last, 3)});
    }
    return manifold::Manifold(mesh);
}

[[nodiscard]] manifold::Manifold ownShape(const Node& node)
{
    const auto sides = static_cast<int>(std::max<std::uint32_t>(node.sides, 3));
    switch (node.shape)
    {
    case Shape::Group:
        return {};
    case Shape::Box:
        if (node.size.x <= 0.0f || node.size.y <= 0.0f || node.size.z <= 0.0f)
        {
            return {};
        }
        return manifold::Manifold::Cube(manifold::vec3(node.size.x, node.size.y, node.size.z), true).Transform(toManifold(node.transform));
    case Shape::Sphere:
        if (node.radius <= 0.0f)
        {
            return {};
        }
        return manifold::Manifold::Sphere(node.radius, sides).Transform(toManifold(node.transform));
    case Shape::Cylinder: {
        if (node.radius <= 0.0f || node.height <= 0.0f)
        {
            return {};
        }
        // Manifold makes it along Z, which this stands up along Y.
        math::Mat4 upright{1.0f};
        upright[1] = math::Vec4(0.0f, 0.0f, -1.0f, 0.0f);
        upright[2] = math::Vec4(0.0f, 1.0f, 0.0f, 0.0f);
        return manifold::Manifold::Cylinder(node.height, node.radius, node.radius, sides, true).Transform(toManifold(node.transform * upright));
    }
    case Shape::Extrusion:
        return extruded(node);
    }
    return {};
}

[[nodiscard]] manifold::Manifold combined(const Node& node, Materials& materials)
{
    manifold::Manifold result = ownShape(node);
    // Who each of its faces came from, whatever is done to it, which says their material.
    if (!result.IsEmpty())
    {
        result = result.AsOriginal();
        materials.emplace_back(result.OriginalID(), node.material);
    }
    for (const Node& child : node.children)
    {
        manifold::Manifold other = combined(child, materials);
        switch (child.operation)
        {
        case Operation::Union:
            result += other;
            break;
        case Operation::Subtract:
            result -= other;
            break;
        case Operation::Intersect:
            result ^= other;
            break;
        }
    }
    return result;
}

// What faces show of a texture: one meter of it per meter, seen from the side they look at, upright on walls.
void mapTexture(asset::Vertex& vertex)
{
    const math::Vec3 normal = vertex.normal;
    const math::Vec3 position = vertex.position;
    const math::Vec3 away{std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)};
    math::Vec3 along;
    math::Vec3 down;
    if (away.y >= away.x && away.y >= away.z)
    {
        along = {1.0f, 0.0f, 0.0f};
        down = {0.0f, 0.0f, normal.y >= 0.0f ? 1.0f : -1.0f};
    }
    else if (away.x >= away.z)
    {
        along = {0.0f, 0.0f, normal.x >= 0.0f ? -1.0f : 1.0f};
        down = {0.0f, -1.0f, 0.0f};
    }
    else
    {
        along = {normal.z >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
        down = {0.0f, -1.0f, 0.0f};
    }
    vertex.uv = {math::dot(position, along), math::dot(position, down)};
    const math::Vec3 tangent = along - normal * math::dot(normal, along);
    const float size = math::length(tangent);
    const math::Vec3 unit = size > 1.0e-4f ? tangent / size : along;
    vertex.tangent = math::Vec4(unit, math::dot(math::cross(normal, unit), down) >= 0.0f ? 1.0f : -1.0f);
}

} // namespace

core::Result<asset::MeshData> buildMesh(const Node& root)
{
    Materials materials;
    manifold::Manifold result = combined(root, materials);
    if (result.Status() != manifold::Manifold::Error::NoError)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "These shapes do not make a solid, as a road that crosses itself would");
    }
    asset::MeshData mesh;
    if (result.IsEmpty())
    {
        return mesh;
    }
    const manifold::MeshGL solid = result.CalculateNormals(0, sharpAngle).GetMeshGL(0);
    if (solid.numProp < 6)
    {
        return core::makeError(core::ErrorCode::Platform, "The faces of the shapes have no normals");
    }
    const std::size_t vertices = solid.vertProperties.size() / solid.numProp;
    mesh.vertices.resize(vertices);
    for (std::size_t index = 0; index < vertices; ++index)
    {
        const float* const values = &solid.vertProperties[index * solid.numProp];
        asset::Vertex& vertex = mesh.vertices[index];
        vertex.position = {values[0], values[1], values[2]};
        const math::Vec3 normal{values[3], values[4], values[5]};
        vertex.normal = math::length(normal) > 1.0e-6f ? math::normalize(normal) : math::Vec3{0.0f, 1.0f, 0.0f};
        mapTexture(vertex);
    }

    // Each material in one range of the indices: what came from each shape, as Manifold says.
    std::vector<asset::AssetId> order;
    const auto materialOf = [&](std::size_t run) {
        const int original = run < solid.runOriginalID.size() ? static_cast<int>(solid.runOriginalID[run]) : -1;
        const auto found = std::ranges::find(materials, original, &Materials::value_type::first);
        return found != materials.end() ? found->second : asset::AssetId{};
    };
    for (std::size_t run = 0; run + 1 < solid.runIndex.size(); ++run)
    {
        const asset::AssetId material = materialOf(run);
        if (std::ranges::find(order, material) == order.end())
        {
            order.push_back(material);
        }
    }
    mesh.indices.reserve(solid.triVerts.size());
    for (const asset::AssetId material : order)
    {
        const auto first = static_cast<std::uint32_t>(mesh.indices.size());
        for (std::size_t run = 0; run + 1 < solid.runIndex.size(); ++run)
        {
            if (materialOf(run) == material)
            {
                mesh.indices.insert(mesh.indices.end(), solid.triVerts.begin() + solid.runIndex[run], solid.triVerts.begin() + solid.runIndex[run + 1]);
            }
        }
        mesh.submeshes.push_back({.firstIndex = first, .indexCount = static_cast<std::uint32_t>(mesh.indices.size()) - first, .material = material});
    }
    return mesh;
}

} // namespace devex::csg
