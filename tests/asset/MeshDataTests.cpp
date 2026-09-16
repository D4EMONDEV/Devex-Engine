#include <devex/asset/MeshData.hpp>
#include <devex/asset/Primitives.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

using Catch::Matchers::WithinAbs;
using devex::asset::MeshData;
using devex::math::Vec3;

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
