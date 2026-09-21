#include <devex/asset/MeshData.hpp>

#include <mikktspace.h>

#include <algorithm>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace devex::asset {

bool isSkinned(const MeshData& mesh) noexcept
{
    return !mesh.skin.empty();
}

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
    if (!mesh.skin.empty() && mesh.skin.size() != mesh.vertices.size())
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "{} skinned vertices for {} vertices", mesh.skin.size(),
                               mesh.vertices.size());
    }
    if (!mesh.skin.empty() && mesh.inverseBind.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "the skinned mesh has no bind pose");
    }
    for (const VertexSkin& skin : mesh.skin)
    {
        const auto missing = std::ranges::find_if(skin.joints, [&mesh](std::uint16_t joint) {
            return joint >= mesh.inverseBind.size();
        });
        if (missing != skin.joints.end())
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "joint {} refers past the {} joints of the bind pose", *missing,
                                   mesh.inverseBind.size());
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

namespace devex::asset {
namespace {

// One vertex per triangle corner while MikkTSpace runs.
struct TangentJob
{
    std::vector<Vertex> corners;
};

[[nodiscard]] Vertex& corner(const SMikkTSpaceContext* context, int face, int vertex) noexcept
{
    auto* const job = static_cast<TangentJob*>(context->m_pUserData);
    return job->corners[static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(vertex)];
}

} // namespace

void computeTangents(MeshData& mesh)
{
    if (mesh.indices.size() < 3)
    {
        return;
    }

    TangentJob job;
    job.corners.reserve(mesh.indices.size());
    for (const std::uint32_t index : mesh.indices)
    {
        job.corners.push_back(mesh.vertices[index]);
    }

    SMikkTSpaceInterface callbacks{};
    callbacks.m_getNumFaces = [](const SMikkTSpaceContext* context) {
        return static_cast<int>(static_cast<TangentJob*>(context->m_pUserData)->corners.size() / 3);
    };
    callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
    callbacks.m_getPosition = [](const SMikkTSpaceContext* context, float position[], int face, int vertex) {
        std::memcpy(position, &corner(context, face, vertex).position, 3 * sizeof(float));
    };
    callbacks.m_getNormal = [](const SMikkTSpaceContext* context, float normal[], int face, int vertex) {
        std::memcpy(normal, &corner(context, face, vertex).normal, 3 * sizeof(float));
    };
    callbacks.m_getTexCoord = [](const SMikkTSpaceContext* context, float uv[], int face, int vertex) {
        std::memcpy(uv, &corner(context, face, vertex).uv, 2 * sizeof(float));
    };
    callbacks.m_setTSpaceBasic = [](const SMikkTSpaceContext* context, const float tangent[],
                                    float sign, int face, int vertex) {
        // The texture origin is at the top-left, so the bitangent sign is the opposite of
        // MikkTSpace's, as glTF expects.
        corner(context, face, vertex).tangent = {tangent[0], tangent[1], tangent[2], -sign};
    };
    SMikkTSpaceContext context{&callbacks, &job};
    genTangSpaceDefault(&context);

    // Corners with identical attributes become one vertex again. A skinned mesh also keeps
    // corners apart when they follow different joints.
    const bool skinned = isSkinned(mesh);
    std::unordered_map<std::string, std::uint32_t> welded;
    welded.reserve(job.corners.size());
    std::vector<Vertex> vertices;
    std::vector<VertexSkin> skin;
    vertices.reserve(mesh.vertices.size());
    for (std::size_t index = 0; index < job.corners.size(); ++index)
    {
        // The index still refers to the vertex this corner came from: it is replaced below.
        const VertexSkin corner = skinned ? mesh.skin[mesh.indices[index]] : VertexSkin{};
        std::string key(reinterpret_cast<const char*>(&job.corners[index]), sizeof(Vertex));
        if (skinned)
        {
            key.append(reinterpret_cast<const char*>(&corner), sizeof(VertexSkin));
        }
        const auto [found, inserted] =
            welded.try_emplace(std::move(key), static_cast<std::uint32_t>(vertices.size()));
        if (inserted)
        {
            vertices.push_back(job.corners[index]);
            if (skinned)
            {
                skin.push_back(corner);
            }
        }
        mesh.indices[index] = found->second;
    }
    mesh.vertices = std::move(vertices);
    mesh.skin = std::move(skin);
}

} // namespace devex::asset
