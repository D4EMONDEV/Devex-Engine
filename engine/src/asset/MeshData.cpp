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
    for (const Submesh& submesh : mesh.submeshes)
    {
        const std::uint64_t end =
            static_cast<std::uint64_t>(submesh.firstIndex) + submesh.indexCount;
        if (submesh.indexCount == 0 || submesh.indexCount % 3 != 0 ||
            submesh.firstIndex % 3 != 0 || end > mesh.indices.size())
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "submesh [{}, {}) does not select whole triangles of the {} indices",
                                   submesh.firstIndex, end, mesh.indices.size());
        }
    }
    return {};
}

std::vector<Submesh> submeshesOf(const MeshData& mesh)
{
    if (!mesh.submeshes.empty())
    {
        return mesh.submeshes;
    }
    return {Submesh{.firstIndex = 0, .indexCount = static_cast<std::uint32_t>(mesh.indices.size())}};
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
