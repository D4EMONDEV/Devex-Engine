#include <devex/asset/Artifact.hpp>
#include <devex/asset/Primitives.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using devex::asset::AssetId;
using devex::asset::AssetType;

TEST_CASE("Meshes with submeshes survive encoding", "[asset][artifact]")
{
    devex::asset::MeshData mesh = devex::asset::makeCube();
    const AssetId material = AssetId::generate();
    mesh.submeshes = {{.firstIndex = 0, .indexCount = 12, .material = material},
                      {.firstIndex = 12, .indexCount = 24}};

    const std::vector<std::byte> bytes = devex::asset::encodeMesh(mesh);
    CHECK(devex::asset::artifactType(bytes) == AssetType::Mesh);

    const auto decoded = devex::asset::decodeMesh(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->vertices.size() == mesh.vertices.size());
    CHECK(decoded->vertices[5].uv == mesh.vertices[5].uv);
    CHECK(decoded->indices == mesh.indices);
    REQUIRE(decoded->submeshes.size() == 2);
    CHECK(decoded->submeshes[0].material == material);
    CHECK(decoded->submeshes[1].indexCount == 24);
}

TEST_CASE("Textures, materials and models survive encoding", "[asset][artifact]")
{
    devex::asset::TextureData texture{.format = devex::asset::TextureFormat::Bc7Srgb};
    texture.mips.push_back({.width = 6, .height = 5, .bytes = std::vector<std::byte>(4 * 16)});
    texture.mips.push_back({.width = 3, .height = 2, .bytes = std::vector<std::byte>(16)});
    const auto decodedTexture = devex::asset::decodeTexture(devex::asset::encodeTexture(texture));
    REQUIRE(decodedTexture.has_value());
    CHECK(decodedTexture->format == texture.format);
    CHECK(decodedTexture->mips.size() == 2);

    const devex::asset::MaterialData material{
        .baseColorFactor = {0.5f, 0.25f, 1.0f, 0.75f},
        .baseColorTexture = AssetId::generate(),
        .normalTexture = AssetId::generate(),
        .emissiveFactor = {1.0f, 0.5f, 0.0f},
        .alphaMode = devex::asset::AlphaMode::Mask,
        .doubleSided = true,
    };
    CHECK(devex::asset::decodeMaterial(devex::asset::encodeMaterial(material)) == material);

    devex::asset::ModelData model;
    model.nodes.push_back({.name = "Root"});
    model.nodes.push_back({
        .name = "Child",
        .parent = 0,
        .translation = {1.0f, 2.0f, 3.0f},
        .rotation = devex::math::angleAxis(0.5f, devex::math::Vec3{0.0f, 1.0f, 0.0f}),
        .mesh = AssetId::generate(),
    });
    const auto decodedModel = devex::asset::decodeModel(devex::asset::encodeModel(model));
    REQUIRE(decodedModel.has_value());
    REQUIRE(decodedModel->nodes.size() == 2);
    CHECK(decodedModel->nodes[1].name == "Child");
    CHECK(decodedModel->nodes[1].parent == 0);
    CHECK(decodedModel->nodes[1].rotation == model.nodes[1].rotation);
    CHECK(decodedModel->nodes[1].mesh == model.nodes[1].mesh);
}

TEST_CASE("Corrupt or mismatched cooked data is rejected", "[asset][artifact]")
{
    std::vector<std::byte> bytes = devex::asset::encodeMesh(devex::asset::makePlane());

    // Another type.
    CHECK(devex::asset::decodeTexture(bytes).error().code == devex::core::ErrorCode::Parse);

    // Truncated data.
    const std::vector<std::byte> truncated(bytes.begin(),
                                           bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2));
    CHECK_FALSE(devex::asset::decodeMesh(truncated).has_value());

    // An older layout version: the version follows the magic and the type.
    bytes[8] = std::byte{0};
    CHECK(devex::asset::decodeMesh(bytes).error().code == devex::core::ErrorCode::Unsupported);

    // Not an asset at all.
    const std::vector<std::byte> text(2, std::byte{0x68});
    CHECK_FALSE(devex::asset::artifactType(text).has_value());
}
