#include <devex/asset/import/GltfImporter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using devex::math::Vec3;
using devex::math::Vec4;

namespace {

const std::filesystem::path dataDirectory{DEVEX_TEST_DATA_DIRECTORY};

} // namespace

TEST_CASE("A glTF triangle imports with its node hierarchy transform", "[asset][gltf]")
{
    const auto scene = devex::asset::importGltf(dataDirectory / "triangle.gltf");
    REQUIRE(scene.has_value());

    REQUIRE(scene->meshes.size() == 1);
    const devex::asset::ImportedMesh& mesh = scene->meshes.front();
    CHECK(mesh.name == "Triangle");
    REQUIRE(mesh.data.vertices.size() == 3);
    CHECK(mesh.data.vertices[1].position == Vec3{1.0f, 0.0f, 0.0f});
    CHECK(mesh.data.vertices[2].position == Vec3{0.0f, 1.0f, 0.0f});
    CHECK(mesh.data.indices == std::vector<std::uint32_t>{0, 1, 2});
    // The file has no normals: they are computed from the counter-clockwise winding.
    CHECK(mesh.data.vertices[0].normal == Vec3{0.0f, 0.0f, 1.0f});

    REQUIRE(scene->instances.size() == 1);
    CHECK(scene->instances.front().mesh == 0);
    // Parent and child translations combine.
    CHECK(scene->instances.front().transform[3] == Vec4{2.0f, 3.0f, 0.0f, 1.0f});
}

TEST_CASE("Importing a missing file reports an error", "[asset][gltf]")
{
    const auto scene = devex::asset::importGltf(dataDirectory / "missing.gltf");

    REQUIRE_FALSE(scene.has_value());
    CHECK(scene.error().code == devex::core::ErrorCode::Io);
}
