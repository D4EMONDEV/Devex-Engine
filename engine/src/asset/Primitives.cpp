#include <devex/asset/Primitives.hpp>
#include <devex/core/Assert.hpp>

#include <algorithm>
#include <array>
#include <numbers>

namespace devex::asset {
namespace {

// Appends a square face. Right and up span the face, and right x up is its outward normal, so the
// corners below are counter-clockwise when seen from outside.
void addQuad(MeshData& mesh, math::Vec3 center, math::Vec3 right, math::Vec3 up)
{
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const math::Vec3 normal = math::normalize(math::cross(right, up));

    mesh.vertices.push_back({center - right - up, normal, {0.0f, 1.0f}});
    mesh.vertices.push_back({center + right - up, normal, {1.0f, 1.0f}});
    mesh.vertices.push_back({center + right + up, normal, {1.0f, 0.0f}});
    mesh.vertices.push_back({center - right + up, normal, {0.0f, 0.0f}});

    for (const std::uint32_t corner : {0u, 1u, 2u, 0u, 2u, 3u})
    {
        mesh.indices.push_back(base + corner);
    }
}

} // namespace

MeshData makeCube(float size)
{
    const float half = size * 0.5f;
    const math::Vec3 x{half, 0.0f, 0.0f};
    const math::Vec3 y{0.0f, half, 0.0f};
    const math::Vec3 z{0.0f, 0.0f, half};

    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);
    addQuad(mesh, x, -z, y);
    addQuad(mesh, -x, z, y);
    addQuad(mesh, y, x, -z);
    addQuad(mesh, -y, x, z);
    addQuad(mesh, z, x, y);
    addQuad(mesh, -z, -x, y);
    computeTangents(mesh);
    mesh.bounds = computeBounds(mesh);
    return mesh;
}

MeshData makePlane(float size)
{
    const float half = size * 0.5f;
    MeshData mesh;
    addQuad(mesh, math::Vec3{0.0f}, {half, 0.0f, 0.0f}, {0.0f, 0.0f, -half});
    computeTangents(mesh);
    mesh.bounds = computeBounds(mesh);
    return mesh;
}

MeshData makeUvSphere(float radius, std::uint32_t segments, std::uint32_t rings)
{
    DEVEX_ASSERT_MSG(segments >= 3 && rings >= 2, "a sphere needs 3 segments and 2 rings");
    segments = std::max(segments, 3u);
    rings = std::max(rings, 2u);

    MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(rings + 1) * (segments + 1));
    for (std::uint32_t ring = 0; ring <= rings; ++ring)
    {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float polar = v * std::numbers::pi_v<float>;
        for (std::uint32_t segment = 0; segment <= segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float azimuth = u * 2.0f * std::numbers::pi_v<float>;
            const math::Vec3 normal{std::sin(polar) * std::sin(azimuth), std::cos(polar),
                                    std::sin(polar) * std::cos(azimuth)};
            mesh.vertices.push_back({normal * radius, normal, {u, v}});
        }
    }

    // Rows go from the north pole down and columns towards +X from the +Z meridian, so each quad
    // spans top-left, bottom-left, bottom-right and top-right when seen from outside.
    mesh.indices.reserve(static_cast<std::size_t>(rings) * segments * 6);
    for (std::uint32_t ring = 0; ring < rings; ++ring)
    {
        for (std::uint32_t segment = 0; segment < segments; ++segment)
        {
            const std::uint32_t topLeft = ring * (segments + 1) + segment;
            const std::uint32_t bottomLeft = topLeft + segments + 1;
            const std::array quad{topLeft, bottomLeft, bottomLeft + 1, topLeft, bottomLeft + 1,
                                  topLeft + 1};
            mesh.indices.insert(mesh.indices.end(), quad.begin(), quad.end());
        }
    }
    computeTangents(mesh);
    mesh.bounds = computeBounds(mesh);
    return mesh;
}

std::optional<MeshData> makeBuiltinMesh(AssetId id)
{
    if (id == builtin::cubeMesh)
    {
        return makeCube();
    }
    if (id == builtin::sphereMesh)
    {
        return makeUvSphere(0.5f, 48, 24);
    }
    if (id == builtin::planeMesh)
    {
        return makePlane();
    }
    return std::nullopt;
}

} // namespace devex::asset
