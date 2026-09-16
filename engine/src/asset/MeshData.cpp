#include <devex/asset/MeshData.hpp>

#include <algorithm>

namespace devex::asset {

core::Result<void> validate(const MeshData& mesh)
{
    if (mesh.vertices.empty() || mesh.indices.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the mesh has no triangles");
    }
    if (mesh.indices.size() % 3 != 0)
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "{} indices do not form whole triangles", mesh.indices.size());
    }
    const auto outOfRange = std::ranges::find_if(mesh.indices, [&mesh](std::uint32_t index) {
        return index >= mesh.vertices.size();
    });
    if (outOfRange != mesh.indices.end())
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "index {} refers past the {} vertices", *outOfRange,
                               mesh.vertices.size());
    }
    return {};
}

void computeNormals(MeshData& mesh)
{
    for (Vertex& vertex : mesh.vertices)
    {
        vertex.normal = math::Vec3{0.0f};
    }

    for (std::size_t first = 0; first + 2 < mesh.indices.size(); first += 3)
    {
        Vertex& a = mesh.vertices[mesh.indices[first]];
        Vertex& b = mesh.vertices[mesh.indices[first + 1]];
        Vertex& c = mesh.vertices[mesh.indices[first + 2]];
        // The cross product length is twice the triangle area, which weights the average.
        const math::Vec3 faceNormal = math::cross(b.position - a.position, c.position - a.position);
        a.normal += faceNormal;
        b.normal += faceNormal;
        c.normal += faceNormal;
    }

    for (Vertex& vertex : mesh.vertices)
    {
        const float length = math::length(vertex.normal);
        vertex.normal = length > 0.0f ? vertex.normal / length : math::Vec3{0.0f, 1.0f, 0.0f};
    }
}

} // namespace devex::asset
