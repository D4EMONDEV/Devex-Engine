#include <devex/asset/Artifact.hpp>
#include <devex/asset/MeshData.hpp>
#include <devex/asset/Primitives.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

using Catch::Matchers::WithinAbs;
using devex::asset::MeshData;
using devex::math::Vec3;

TEST_CASE("Mesh LODs reduce geometry while retaining materials and skin", "[asset][lod]")
{
    using namespace devex;
    const bool skinned = GENERATE(false, true);
    auto mesh = asset::makeUvSphere(1.0f, 32, 16);
    const auto sourceVertices = mesh.vertices;
    const auto sourceIndices = mesh.indices;
    const auto half = static_cast<std::uint32_t>(mesh.indices.size() / 6 * 3);
    mesh.submeshes = {
        {.firstIndex = 0, .indexCount = half, .material = asset::AssetId::generate()},
        {.firstIndex = half, .indexCount = static_cast<std::uint32_t>(mesh.indices.size()) - half}};
    if (skinned)
    {
        mesh.inverseBind.assign(2, math::Mat4{1});
        for (const auto& vertex : mesh.vertices)
        {
            const float weight = std::clamp(vertex.position.y * 0.4f + 0.5f, 0.0f, 1.0f);
            mesh.skin.push_back({.joints = {0, 1, 0, 0}, .weights = {weight, 1 - weight, 0, 0}});
        }
    }
    asset::generateMeshLods(mesh);
    REQUIRE(mesh.lods.size() >= 2);
    REQUIRE(asset::validate(mesh));
    CHECK(mesh.indices == sourceIndices);
    CHECK(mesh.vertices.size() == sourceVertices.size());
    std::size_t previous = mesh.indices.size();
    float error = 0;
    for (const auto& lod : mesh.lods)
    {
        CHECK(lod.indices.size() < previous);
        CHECK(lod.vertices.size() < sourceVertices.size());
        CHECK(lod.error >= error);
        REQUIRE(lod.submeshes.size() == 2);
        CHECK(lod.submeshes[0].material == mesh.submeshes[0].material);
        REQUIRE(lod.skin.size() == (skinned ? lod.vertices.size() : 0));
        for (std::size_t v = 0; v < lod.vertices.size(); ++v)
        {
            const auto& vertex = lod.vertices[v];
            const auto found =
                std::ranges::find_if(sourceVertices,
                                     [&](const auto& original)
                                     {
                                         return original.position == vertex.position &&
                                                original.uv == vertex.uv &&
                                                original.normal == vertex.normal;
                                     });
            REQUIRE(found != sourceVertices.end());
            if (skinned)
            {
                const auto i = std::size_t(found - sourceVertices.begin());
                CHECK(lod.skin[v].joints == mesh.skin[i].joints);
                CHECK(lod.skin[v].weights == mesh.skin[i].weights);
            }
        }
        previous = lod.indices.size();
        error = lod.error;
    }
    const auto bytes = asset::encodeMesh(mesh);
    const auto decoded = asset::decodeMesh(bytes);
    REQUIRE(decoded);
    REQUIRE(decoded->lods.size() == mesh.lods.size());
    for (std::size_t i = 0; i < mesh.lods.size(); ++i)
    {
        CHECK(decoded->lods[i].indices == mesh.lods[i].indices);
        CHECK(decoded->lods[i].error == mesh.lods[i].error);
        CHECK(decoded->lods[i].vertices.size() == mesh.lods[i].vertices.size());
    }
    CHECK_FALSE(asset::decodeMesh(std::span(bytes).first(bytes.size() - 1)));
    mesh.lods[0].error = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(asset::validate(mesh));
}

TEST_CASE("LOD generation can be disabled and invalid level counts fail safely", "[asset][lod]")
{
    using namespace devex;
    auto mesh = asset::makeUvSphere();
    asset::generateMeshLods(mesh, 0);
    CHECK(mesh.lods.empty());
    auto bytes = asset::encodeMesh(mesh);
    const std::uint32_t count = ~std::uint32_t{0};
    std::memcpy(bytes.data() + bytes.size() - sizeof(count), &count, sizeof(count));
    CHECK_FALSE(asset::decodeMesh(bytes));
    asset::generateMeshLods(mesh);
    REQUIRE_FALSE(mesh.lods.empty());
    mesh.lods.front().submeshes.front().material = asset::AssetId::generate();
    CHECK_FALSE(asset::validate(mesh));
}

namespace {

// Counts triangles whose counter-clockwise normal points away from the mesh center, and
// triangles that point inwards. Degenerate triangles, such as those at sphere poles, count in
// neither.
struct Winding
{
    std::size_t outward = 0;
    std::size_t inward = 0;
};

Winding windingOf(const MeshData& mesh, Vec3 center)
{
    Winding winding;
    for (std::size_t first = 0; first + 2 < mesh.indices.size(); first += 3)
    {
        const Vec3 a = mesh.vertices[mesh.indices[first]].position;
        const Vec3 b = mesh.vertices[mesh.indices[first + 1]].position;
        const Vec3 c = mesh.vertices[mesh.indices[first + 2]].position;
        const Vec3 normal = devex::math::cross(b - a, c - a);
        if (devex::math::length(normal) < 1e-6f)
        {
            continue;
        }
        const float facing = devex::math::dot(normal, (a + b + c) / 3.0f - center);
        (facing > 0.0f ? winding.outward : winding.inward) += 1;
    }
    return winding;
}

void checkUnitNormals(const MeshData& mesh)
{
    for (const devex::asset::Vertex& vertex : mesh.vertices)
    {
        CHECK_THAT(devex::math::length(vertex.normal), WithinAbs(1.0, 1e-4));
    }
}

} // namespace

TEST_CASE("Validation rejects malformed meshes", "[asset][mesh]")
{
    MeshData mesh;
    CHECK_FALSE(devex::asset::validate(mesh).has_value());

    mesh.vertices.resize(3);
    mesh.indices = {0, 1};
    CHECK(devex::asset::validate(mesh).error().message == "2 indices do not form whole triangles");

    mesh.indices = {0, 1, 3};
    CHECK(devex::asset::validate(mesh).error().message == "index 3 refers past the 3 vertices");

    mesh.indices = {0, 1, 2};
    CHECK(devex::asset::validate(mesh).has_value());
}

TEST_CASE("Computed normals follow counter-clockwise winding", "[asset][mesh]")
{
    MeshData mesh;
    mesh.vertices = {{{0.0f, 0.0f, 0.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}};
    mesh.indices = {0, 1, 2};

    devex::asset::computeNormals(mesh);

    for (const devex::asset::Vertex& vertex : mesh.vertices)
    {
        CHECK(vertex.normal == Vec3{0.0f, 0.0f, 1.0f});
    }
}

TEST_CASE("Primitives are valid, outward-facing and have unit normals", "[asset][primitives]")
{
    const auto [name, mesh] = GENERATE(
        std::pair{std::string("cube"), devex::asset::makeCube(2.0f)},
        std::pair{std::string("sphere"), devex::asset::makeUvSphere(1.0f, 12, 6)});
    INFO(name);

    REQUIRE(devex::asset::validate(mesh).has_value());
    checkUnitNormals(mesh);

    const Winding winding = windingOf(mesh, Vec3{0.0f});
    CHECK(winding.outward > 0);
    CHECK(winding.inward == 0);
}

TEST_CASE("The plane faces up with counter-clockwise triangles", "[asset][primitives]")
{
    const MeshData plane = devex::asset::makePlane(4.0f);

    REQUIRE(devex::asset::validate(plane).has_value());
    CHECK(plane.vertices.size() == 4);
    // Seen from below the plane, every triangle must point towards the viewer above.
    const Winding winding = windingOf(plane, Vec3{0.0f, -1.0f, 0.0f});
    CHECK(winding.outward == 2);
    CHECK(plane.vertices.front().normal == Vec3{0.0f, 1.0f, 0.0f});
}

TEST_CASE("The cube spans the requested size", "[asset][primitives]")
{
    const MeshData cube = devex::asset::makeCube(2.0f);

    CHECK(cube.vertices.size() == 24);
    CHECK(cube.indices.size() == 36);
    for (const devex::asset::Vertex& vertex : cube.vertices)
    {
        CHECK_THAT(std::abs(vertex.position.x), WithinAbs(1.0, 1e-6));
        CHECK_THAT(std::abs(vertex.position.y), WithinAbs(1.0, 1e-6));
        CHECK_THAT(std::abs(vertex.position.z), WithinAbs(1.0, 1e-6));
    }
}

TEST_CASE("MikkTSpace tangents follow increasing u with glTF handedness", "[asset][mesh]")
{
    const MeshData plane = devex::asset::makePlane(2.0f);
    for (const devex::asset::Vertex& vertex : plane.vertices)
    {
        CHECK_THAT(vertex.tangent.x, WithinAbs(1.0, 1e-5));
        CHECK_THAT(vertex.tangent.y, WithinAbs(0.0, 1e-5));
        // cross(normal, tangent) points towards decreasing v, the top of the texture.
        CHECK(vertex.tangent.w == 1.0f);
    }

    // Faces share no vertex, and each face's corners share their tangent: nothing is split.
    const MeshData cube = devex::asset::makeCube();
    CHECK(cube.vertices.size() == 24);
    CHECK(devex::asset::validate(cube).has_value());
}
